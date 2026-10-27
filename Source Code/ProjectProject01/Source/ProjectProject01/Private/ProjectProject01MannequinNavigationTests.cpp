#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MultiplayTestGameMode.h"
#include "MannequinAICharacter.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Navigation/PathFollowingComponent.h"
#include "ProjectNavigationCrowdManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMannequinNavigationSafetyTest,
    "ProjectProject01.Multiplayer.MannequinNavigationSafety",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMannequinNavigationSafetyTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(true).CreateAISystem(true).ShouldSimulatePhysics(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto Box=[&World](FVector Center,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>();
        auto* Shape=NewObject<UBoxComponent>(Actor);Actor->SetRootComponent(Shape);
        Actor->AddInstanceComponent(Shape);Shape->SetBoxExtent(Extent);
        Shape->SetCollisionProfileName(TEXT("BlockAll"));Shape->SetMobility(EComponentMobility::Static);
        Actor->SetActorLocation(Center);Shape->RegisterComponent();
        return Actor;
    };
    auto* Floor=Box(FVector(0,0,-20),FVector(1000,1000,20));
    auto* Mode=World->SpawnActor<AMultiplayTestGameMode>();
    auto* Pawn=World->SpawnActor<AMannequinAICharacter>();
    Pawn->SetActorLocation(FVector(0,0,100));
    FVector Supported;
    TestTrue(TEXT("Capsule-height spawn resolves onto a real supporting floor"),Mode->FindSupportedMannequinLocation(Pawn,FVector(0,0,100),Supported));
    TestTrue(TEXT("Resolved capsule bottom stays above marble floor"),Supported.Z>Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    TestFalse(TEXT("No-floor spawn is rejected instead of AlwaysSpawn in air"),Mode->FindSupportedMannequinLocation(Pawn,FVector(2000,0,100),Supported));
    TestFalse(TEXT("Wrong-storey spawn cannot fall back to a floor twelve metres below"),Mode->FindSupportedMannequinLocation(Pawn,FVector(0,0,1300),Supported));
    auto* Wall=Box(FVector(400,0,150),FVector(20,500,150));
    TestFalse(TEXT("Capsule overlapping a wall is not a valid spawn/formation target"),Mode->FindSupportedMannequinLocation(Pawn,FVector(400,0,100),Supported));
    Pawn->SetActorLocation(FVector(0,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.5f));
    Mode->MaintainMannequinFloorSafety(Pawn);
    TestTrue(TEXT("Verified supported location is remembered"),Mode->MultiplayerLastSupportedLocations.Contains(Pawn));
    TestFalse(TEXT("Multiplayer AI cannot walk over an unconnected ledge"),Pawn->GetCharacterMovement()->bCanWalkOffLedges);
    const FVector Safe=Pawn->GetActorLocation();
    Pawn->SetActorLocation(FVector(2000,0,-300));Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    AddExpectedError(TEXT("Recovered multiplayer mannequin"),EAutomationExpectedErrorFlags::Contains,1);
    Mode->MaintainMannequinFloorSafety(Pawn);
    TestTrue(TEXT("AI falling through bad floor returns only to its verified supported position"),Pawn->GetActorLocation().Equals(Safe,3.f));
    TestTrue(TEXT("Recovered AI resumes walking"),Pawn->GetCharacterMovement()->IsMovingOnGround());
    FVector Goal;
    TestFalse(TEXT("Missing NavMesh cannot create a direct-line path through walls"),Mode->FindReachableMannequinGoal(Pawn,FVector(600,0,0),300,Goal));
    auto* AI=World->SpawnActor<AAIController>();AI->Possess(Pawn);
    Pawn->SetMultiplayCommand(EMultiplayMannequinCommand::FollowMannequinPlayer);
    Mode->MoveMannequinOnCompletePath(Pawn,AI,FVector(600,0,0),300);
    TestTrue(TEXT("Unreachable follow keeps the player's command for later route recovery"),Pawn->GetMultiplayCommand()==EMultiplayMannequinCommand::FollowMannequinPlayer);
    Pawn->SetManualControlEnabled(true);Pawn->SetActorLocation(FVector(2000,0,-300));
    Mode->MaintainMannequinFloorSafety(Pawn);
    TestTrue(TEXT("AI floor recovery never teleports a manually controlled player"),Pawn->GetActorLocation().Equals(FVector(2000,0,-300)));
    Pawn->SetManualControlEnabled(false);Pawn->SetActorLocation(Safe);
    // Build a temporary, unsaved NavMesh: a wall blocks the direct line but there
    // are open passages around its ends. No project map/asset is changed by this fixture.
    auto* Bounds=World->SpawnActor<ANavMeshBoundsVolume>();
    auto* BoundsBox=NewObject<UBoxComponent>(Bounds);Bounds->AddInstanceComponent(BoundsBox);
    BoundsBox->SetupAttachment(Bounds->GetRootComponent());BoundsBox->SetBoxExtent(FVector(1200,1200,400));
    BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);BoundsBox->SetCanEverAffectNavigation(false);BoundsBox->RegisterComponent();
    auto* Nav=UNavigationSystemV1::GetCurrent(World);
    if (TestNotNull(TEXT("Temporary world has a navigation system"),Nav))
    {
        Nav->OnWorldInitDone(FNavigationSystemRunMode::EditorMode);
        Nav->RemoveNavigationBuildLock(0xff,UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
        Nav->Build();
        if (auto* Data=Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) Data->EnsureBuildCompletion();
        TestTrue(TEXT("Project crowd manager retains Detour but handles world teardown"),Nav->GetCrowdManager()&&Nav->GetCrowdManager()->IsA<UProjectNavigationCrowdManager>());
        const bool bRoute=Mode->FindReachableMannequinGoal(Pawn,FVector(800,0,0),150,Goal);
        TestTrue(TEXT("A reachable goal behind a wall resolves using a complete A* detour"),bRoute);
        if (bRoute)
        {
            auto* Path=Nav->FindPathToLocationSynchronously(World,Pawn->GetNavAgentLocation(),Goal,Pawn);
            TestTrue(TEXT("Detour has intermediate corners, not direct motion through wall"),Path && !Path->IsPartial() && Path->PathPoints.Num()>2);
            TestTrue(TEXT("Path leaves the blocked wall corridor"),Path && Path->PathPoints.ContainsByPredicate([](const FVector& P){return FMath::Abs(P.Y)>500;}));
            Mode->MultiplayerNavigationStates.Remove(Pawn);
            Mode->MoveMannequinOnCompletePath(Pawn,AI,FVector(800,0,0),150);
            const auto ActivePath=AI->GetPathFollowingComponent()->GetPath();
            TestTrue(TEXT("Real follow move request uses the complete wall-detour path"),ActivePath.IsValid() && !ActivePath->IsPartial() && ActivePath->GetPathPoints().Num()>2);
        }
        TestFalse(TEXT("Projection cannot target another storey"),Mode->FindReachableMannequinGoal(Pawn,FVector(800,0,1200),150,Goal));
    }
    World->DestroyWorld(false);
    // Static game NavMeshes are built once. Use a second immutable fixture for
    // the disconnected case rather than pretending a static mesh updates at runtime.
    World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    Box(FVector(0,0,-20),FVector(1000,1000,20));Box(FVector(400,0,150),FVector(20,1100,150));
    Mode=World->SpawnActor<AMultiplayTestGameMode>();Pawn=World->SpawnActor<AMannequinAICharacter>();Pawn->SetActorLocation(Safe);
    Bounds=World->SpawnActor<ANavMeshBoundsVolume>();
    BoundsBox=NewObject<UBoxComponent>(Bounds);Bounds->AddInstanceComponent(BoundsBox);BoundsBox->SetupAttachment(Bounds->GetRootComponent());
    BoundsBox->SetBoxExtent(FVector(1200,1200,400));BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BoundsBox->SetCanEverAffectNavigation(false);BoundsBox->RegisterComponent();
    Nav=UNavigationSystemV1::GetCurrent(World);
    if(TestNotNull(TEXT("Sealed fixture navigation system"),Nav))
    {
        Nav->OnWorldInitDone(FNavigationSystemRunMode::EditorMode);
        Nav->RemoveNavigationBuildLock(0xff,UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);Nav->Build();
        if(auto* Data=Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))Data->EnsureBuildCompletion();
        TestFalse(TEXT("A fully sealed wall rejects partial paths rather than claiming it can reach the goal"),Mode->FindReachableMannequinGoal(Pawn,FVector(800,0,0),150,Goal));
    }
    World->DestroyWorld(false);
    return true;
}
#endif

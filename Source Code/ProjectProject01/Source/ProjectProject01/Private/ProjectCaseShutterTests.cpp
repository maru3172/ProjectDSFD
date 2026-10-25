#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "FireShutterActor.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectShutterTest,"ProjectProject01.CaseRecords.ShutterAuthorityAndLegacy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectShutterTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if(!TestNotNull(TEXT("Isolated shutter world"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};World->SetGameInstance(NewObject<UGameInstance>());
    FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));
    if(!TestTrue(TEXT("Authoritative multiplayer fixture"),World->SetGameMode(URL)))return false;
    auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();auto* PC=World->SpawnActor<AMultiplayTestPlayerController>();
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Pawn=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(300,0,0)),Spawn);PC->Possess(Pawn);
    auto* Single=World->SpawnActor<AFireShutterActor>();Single->DispatchBeginPlay();
    Single->HandleInteraction(Pawn);Single->Tick(1);
    TestTrue(TEXT("Existing single-player movement still opens halfway after one second"),FMath::IsNearlyEqual(Single->CurrentOpenAlpha,.5f));
    Single->HandleInteraction(Pawn);Single->Tick(1);TestTrue(TEXT("Existing single-player toggle still reverses without teleporting"),FMath::IsNearlyZero(Single->CurrentOpenAlpha));Single->Destroy();
    World->WorldType=EWorldType::PIE;World->SetPlayInEditorInitialNetMode(NM_ListenServer);
    Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::InProgress;
    FProjectProject01ValidatedJoinClaim Claim;Claim.UserId=TEXT("1");Claim.MatchId=FGuid::NewGuid().ToString();Claim.Role=TEXT("Survivor");PC->SetAuthenticatedLobbyIdentity(Claim);
    PC->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Active);
    FMultiplayTestServerPlayerRecord Record;Record.UserId=Claim.UserId;Record.Role=Claim.Role;Record.SurvivorState=EMultiplayTestSurvivorState::Active;Record.SurvivorPawn=Pawn;Mode->MatchPlayerRecords.Add(PC,Record);
    auto* Shutter=World->SpawnActor<AFireShutterActor>();Shutter->OnRep_Motion();
    TestTrue(TEXT("Replication cannot animate before component home transforms are captured"),Shutter->ItemMesh->GetRelativeLocation().IsNearlyZero());Shutter->DispatchBeginPlay();
    TestTrue(TEXT("Placed shutter is replicated and movable"),Shutter->GetIsReplicated()&&Shutter->ItemMesh->Mobility==EComponentMobility::Movable);
    const FVector Origin=Pawn->GetPawnViewLocation(),Direction=(Shutter->GetActorLocation()-Origin).GetSafeNormal();
    TestFalse(TEXT("An arbitrary distant client origin cannot operate a shutter"),Shutter->TryInteractFromView(Pawn,Origin+FVector(10000,0,0),Direction,500,ECC_Visibility));
    auto* Wall=World->SpawnActor<AActor>();auto* Block=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Block);Block->SetBoxExtent(FVector(20,200,200));Block->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Block->SetCollisionResponseToAllChannels(ECR_Block);Block->RegisterComponent();Wall->SetActorLocation(FVector(150,0,50));
    TestFalse(TEXT("A wall between the view and shutter prevents interaction"),Shutter->TryInteractFromView(Pawn,Origin,Direction,500,ECC_Visibility));Wall->Destroy();
    TestTrue(TEXT("Active admitted survivor can operate an actually hit shutter"),Shutter->TryInteractFromView(Pawn,Origin,Direction,500,ECC_Visibility));
    TestTrue(TEXT("Server motion targets open"),Shutter->Motion.Target==1);
    Shutter->Motion.StartedAt-=1;Shutter->Tick(0);
    TestTrue(TEXT("Server-time interpolation advances between sparse network updates"),FMath::IsNearlyEqual(Shutter->CurrentOpenAlpha,.5f));
    Shutter->HandleInteraction(Pawn);TestTrue(TEXT("Mid-motion reversal preserves current position"),FMath::IsNearlyEqual(Shutter->Motion.Start,.5f)&&Shutter->Motion.Target==0);
    Shutter->SetRole(ROLE_SimulatedProxy);TestFalse(TEXT("A client cannot change authoritative shutter motion"),Shutter->TryInteractFromView(Pawn,Origin,Direction,500,ECC_Visibility));Shutter->SetRole(ROLE_Authority);
    Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::Results;
    TestFalse(TEXT("Result phase prevents interaction"),Shutter->CanInteract_Implementation(Pawn));
    return true;
}
#endif

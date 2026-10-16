#include "ProjectProject01SpatialAudio.h"
#include "FootstepSynthComponent.h"
#include "PlayerHeartbeatComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSpatialAudioSettingsTest,"ProjectProject01.Audio.SpatialOcclusion",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectSpatialAudioSettingsTest::RunTest(const FString&)
{
    const auto* Footstep=GetDefault<UFootstepSynthComponent>();
    const auto* Pain=GetDefault<UPainGroanSynthComponent>();
    const auto* Fall=GetDefault<UBodyFallSynthComponent>();
    TestTrue(TEXT("Footsteps use occlusion"),Footstep->AttenuationOverrides.bEnableOcclusion!=0);
    TestTrue(TEXT("Merged room uses surface collision, not a convex room hull"),Footstep->AttenuationOverrides.bUseComplexCollisionForOcclusion!=0);
    TestTrue(TEXT("Pain/scream uses occlusion"),Pain->AttenuationOverrides.bEnableOcclusion!=0);
    TestTrue(TEXT("Body fall uses occlusion"),Fall->AttenuationOverrides.bEnableOcclusion!=0);
    TestFalse(TEXT("Heartbeat remains non-spatial"),GetDefault<UHeartbeatSynthComponent>()->bAllowSpatialization!=0);
    TestFalse(TEXT("Capture confirmation remains non-spatial"),GetDefault<UMannequinCaptureSynthComponent>()->bAllowSpatialization!=0);
    for (const FSoundAttenuationSettings* Settings : { &Footstep->AttenuationOverrides, &Pain->AttenuationOverrides, &Fall->AttenuationOverrides })
    {
        TestEqual(TEXT("All world SFX start distance attenuation at 30m"),Settings->AttenuationShapeExtents.X,3000.0);
        TestEqual(TEXT("Distance fade spans another 30m"),Settings->FalloffDistance,3000.0f);
        for (const float Distance : { 0.0f,100.0f,1000.0f,2999.0f,3000.0f })
            TestTrue(FString::Printf(TEXT("Distance %.0fcm stays at full distance gain"),Distance),FMath::IsNearlyEqual(
                Settings->Evaluate(FTransform::Identity,FVector(Distance,0,0)),1.0f));
        TestTrue(TEXT("Just beyond 30m fades smoothly, not abruptly"),Settings->Evaluate(FTransform::Identity,FVector(3001,0,0))>0.999f);
        TestTrue(TEXT("45m is halfway through the distance fade"),FMath::IsNearlyEqual(Settings->Evaluate(FTransform::Identity,FVector(4500,0,0)),0.5f));
        TestTrue(TEXT("60m reaches silence"),FMath::IsNearlyZero(Settings->Evaluate(FTransform::Identity,FVector(6000,0,0))));
        TestTrue(TEXT("Beyond 60m stays silent"),FMath::IsNearlyZero(Settings->Evaluate(FTransform::Identity,FVector(6500,0,0))));
        TestFalse(TEXT("Open-space distance does not independently muffle the sound"),Settings->bAttenuateWithLPF!=0);
        TestFalse(TEXT("Facing away does not apply an unrelated focus-volume reduction"),Settings->bEnableListenerFocus!=0);
        TestEqual(TEXT("Native occlusion cannot be bypassed by a copied plugin slot"),Settings->PluginSettings.OcclusionPluginSettingsArray.Num(),0);
    }
    TestEqual(TEXT("Occlusion uses visibility blockers"),static_cast<ECollisionChannel>(Pain->AttenuationOverrides.OcclusionTraceChannel),ECC_Visibility);
    TestTrue(TEXT("Occluded sound is quieter, not silent"),Pain->AttenuationOverrides.OcclusionVolumeAttenuation>0.0f&&Pain->AttenuationOverrides.OcclusionVolumeAttenuation<1.0f);
    TestEqual(TEXT("Wall-only gain has a clearly separate level"),Pain->AttenuationOverrides.OcclusionVolumeAttenuation,0.25f);
    TestEqual(TEXT("Wall-only filter removes higher frequencies"),Pain->AttenuationOverrides.OcclusionLowPassFilterFrequency,600.0f);
    auto* Original=NewObject<USoundAttenuation>();Original->Attenuation.bEnableOcclusion=false;Original->Attenuation.FalloffDistance=3456.0f;
    Original->Attenuation.bAttenuate=false;Original->Attenuation.AttenuationShape=EAttenuationShape::Cone;
    Original->Attenuation.PluginSettings.OcclusionPluginSettingsArray.Add(nullptr);
    auto* Copy=ProjectProject01SpatialAudio::MakeOccludedCopy(GetTransientPackage(),Original,Footstep->AttenuationOverrides);
    TestTrue(TEXT("Asset playback uses transient copy"),Copy!=Original&&Copy->HasAnyFlags(RF_Transient));
    TestFalse(TEXT("Original designer asset not modified"),Original->Attenuation.bEnableOcclusion!=0);
    TestTrue(TEXT("Asset copy enables occlusion"),Copy->Attenuation.bEnableOcclusion!=0);
    TestEqual(TEXT("Original asset radius stays untouched"),Original->Attenuation.FalloffDistance,3456.0f);
    TestEqual(TEXT("Copied asset also follows 30m plus 30m policy"),Copy->Attenuation.AttenuationShapeExtents.X,3000.0);
    TestEqual(TEXT("Copied asset falloff overrides obsolete short designer range"),Copy->Attenuation.FalloffDistance,3000.0f);
    TestTrue(TEXT("Copied asset cannot disable distance attenuation"),Copy->Attenuation.bAttenuate!=0);
    TestEqual(TEXT("Null plugin slot removed from runtime copy"),Copy->Attenuation.PluginSettings.OcclusionPluginSettingsArray.Num(),0);
    TestEqual(TEXT("Original plugin slot is not edited"),Original->Attenuation.PluginSettings.OcclusionPluginSettingsArray.Num(),1);
    auto* Instance=NewObject<UFootstepSynthComponent>();
    Instance->bOverrideAttenuation=false;Instance->bAllowSpatialization=false;Instance->bIsUISound=true;
    Instance->AttenuationOverrides.FalloffDistance=10.0f;
    ProjectProject01SpatialAudio::ConfigureWorldSynth(*Instance);
    TestTrue(TEXT("Blueprint-disabled attenuation override is restored before playback"),Instance->bOverrideAttenuation!=0);
    TestTrue(TEXT("Blueprint-disabled spatialization is restored before playback"),Instance->bAllowSpatialization!=0);
    TestFalse(TEXT("World SFX cannot accidentally play as unoccluded UI audio"),Instance->bIsUISound!=0);
    TestEqual(TEXT("Runtime Blueprint instance gets the new distance policy"),Instance->AttenuationOverrides.FalloffDistance,3000.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSpatialAudioCollisionTest,"ProjectProject01.Audio.OcclusionBlockers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectSpatialAudioCollisionTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    // CreateWorld initializes the world itself. Calling InitializeNewWorld again duplicates WorldSettings.
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    AActor* Wall=World->SpawnActor<AActor>();
    UBoxComponent* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(20.0f,100.0f,100.0f));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Box->RegisterComponent();Box->SetWorldLocation(FVector(150.0f,0.0f,50.0f));
    FHitResult Hit;
    TestTrue(TEXT("Closed wall blocks sound trace"),World->LineTraceSingleByChannel(Hit,FVector(0.0f,0.0f,50.0f),FVector(300.0f,0.0f,50.0f),ECC_Visibility));
    Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
    TestFalse(TEXT("Open/non-blocking geometry restores clear trace"),World->LineTraceSingleByChannel(Hit,FVector(0.0f,0.0f,50.0f),FVector(300.0f,0.0f,50.0f),ECC_Visibility));
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSpatialRoomSurfaceTest,"ProjectProject01.Audio.RoomSurfaceOcclusion",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectSpatialRoomSurfaceTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    UStaticMesh* Original=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!IsValid(Original)||!IsValid(Original->GetBodySetup()))
    {
        AddError(TEXT("Engine cube surface fixture missing"));World->DestroyWorld(false);return false;
    }
    // Transient fixture only: deliberately oversized convex collision around a small real surface.
    // This reproduces a merged room's simple hull incorrectly filling empty space.
    UStaticMesh* Mesh=DuplicateObject<UStaticMesh>(Original,GetTransientPackage());
    Mesh->SetFlags(RF_Transient);
    UBodySetup* Body=Mesh->GetBodySetup();
    if (!TestTrue(TEXT("Fixture collision is a private copy, not an engine asset"),Body!=Original->GetBodySetup()))
    {
        World->DestroyWorld(false);return false;
    }
    Body->AggGeom=FKAggregateGeom();
    FKBoxElem Hull;Hull.X=600;Hull.Y=600;Hull.Z=600;
    Body->AggGeom.BoxElems.Add(Hull);
    Body->CollisionTraceFlag=CTF_UseSimpleAndComplex;
    Body->InvalidatePhysicsData();Body->CreatePhysicsMeshes();
    AActor* Room=World->SpawnActor<AActor>();
    auto* Surface=NewObject<UStaticMeshComponent>(Room);Room->SetRootComponent(Surface);
    Surface->SetStaticMesh(Mesh);Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Surface->SetCollisionResponseToAllChannels(ECR_Ignore);Surface->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Surface->RegisterComponent();
    FCollisionQueryParams Simple(SCENE_QUERY_STAT(RoomSimpleSound),false);
    FCollisionQueryParams Actual(SCENE_QUERY_STAT(RoomSurfaceSound),true);
    FHitResult Hit;
    TestTrue(TEXT("Old coarse hull incorrectly blocks an empty-space ray"),World->LineTraceSingleByChannel(Hit,FVector(-400,200,100),FVector(400,200,100),ECC_Visibility,Simple));
    TestFalse(TEXT("Actual surfaces leave empty space audible"),World->LineTraceSingleByChannel(Hit,FVector(-400,200,100),FVector(400,200,100),ECC_Visibility,Actual));
    TestTrue(TEXT("Real solid surface between rooms still occludes"),World->LineTraceSingleByChannel(Hit,FVector(-200,0,0),FVector(200,0,0),ECC_Visibility,Actual));
    // Both endpoints above the floor of one enclosed room must remain audible to one another.
    Surface->SetWorldScale3D(FVector(10,10,0.1));Surface->SetWorldLocation(FVector(0,0,-5));
    TestFalse(TEXT("Floor below both endpoints is not a separating wall"),World->LineTraceSingleByChannel(Hit,FVector(-200,0,20),FVector(200,0,100),ECC_Visibility,Actual));
    TestTrue(TEXT("Different floors separated by a real slab are occluded"),World->LineTraceSingleByChannel(Hit,FVector(0,0,-100),FVector(0,0,100),ECC_Visibility,Actual));
    Surface->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
    TestFalse(TEXT("Opening/removing the actual blocker clears the sound ray"),World->LineTraceSingleByChannel(Hit,FVector(0,0,-100),FVector(0,0,100),ECC_Visibility,Actual));
    auto AddWall=[&](const FVector& Location,const FVector& Scale)
    {
        auto* Wall=NewObject<UStaticMeshComponent>(Room);Wall->SetStaticMesh(Original);
        Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
        Wall->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Wall->RegisterComponent();
        Wall->SetWorldScale3D(Scale);Wall->SetWorldLocation(Location);
    };
    AddWall(FVector(-500,0,150),FVector(0.1,10,3));AddWall(FVector(500,0,150),FVector(0.1,10,3));
    AddWall(FVector(0,-500,150),FVector(10,0.1,3));AddWall(FVector(0,500,150),FVector(10,0.1,3));
    AddWall(FVector(0,0,300),FVector(10,10,0.1));
    TestFalse(TEXT("Listener in the same enclosed room still hears a clear direct path"),World->LineTraceSingleByChannel(Hit,FVector(0,0,100),FVector(100,100,100),ECC_Visibility,Actual));
    TestTrue(TEXT("Listener outside that enclosed room hears through a real wall"),World->LineTraceSingleByChannel(Hit,FVector(0,0,100),FVector(700,100,100),ECC_Visibility,Actual));
    World->DestroyWorld(false);
    return true;
}
#endif

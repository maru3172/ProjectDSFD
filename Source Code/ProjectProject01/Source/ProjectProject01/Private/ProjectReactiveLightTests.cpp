#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "MannequinReactiveLight.h"
#include "MannequinAICharacter.h"
#include "ProjectProject01GameInstance.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"

namespace
{
    UWorld* LightTestWorld()
    {
        const auto V=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        return UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReactiveLightPresentationTest,"ProjectProject01.Lighting.MannequinFaultPresentation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReactiveLightPresentationTest::RunTest(const FString&)
{
    UWorld* World=LightTestWorld();
    if (!TestNotNull(TEXT("Test world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World);World->DestroyWorld(false); };
    auto* PC=World->SpawnActor<APlayerController>();
    PC->SetAsLocalPlayerController();
    // This isolated world has not initialized gameplay actors, so register its controller explicitly.
    World->AddController(PC);
    TestTrue(TEXT("The test uses a registered local viewer"),World->GetFirstPlayerController()==PC && PC->IsLocalController());
    auto* View=World->SpawnActor<AActor>();
    PC->SetViewTarget(View);
    auto* Light=World->SpawnActor<AMannequinReactiveLight>();
    Light->SetActorLocation(FVector(0,0,300));
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    TArray<AMannequinAICharacter*> Bodies;
    auto Body=[&](int32 N)
    {
        auto* B=World->SpawnActor<AMannequinAICharacter>(FVector(100+N*120,0,90),FRotator::ZeroRotator,Params);
        Bodies.Add(B); return B;
    };
    Body(0); Body(1);
    Light->RefreshDetectedMannequinsFromWorld();
    TestEqual(TEXT("Two nearby bodies are counted but do not trigger faults"),Light->GetDetectedMannequinCount(),2);
    TestEqual(TEXT("Sub-threshold brightness stays steady"),Light->TargetThreat,0.f);
    Body(2); World->Tick(LEVELTICK_All,.21f); Light->RefreshDetectedMannequinsFromWorld();
    TestTrue(TEXT("Third body enables the first tier"),Light->GetThreatTier()==1 && Light->TargetThreat>0);
    const float MovingThreat=Light->TargetThreat;
    for (auto* B:Bodies) B->SetFrozenBySurvivorVision(true);
    World->Tick(LEVELTICK_All,.21f); Light->RefreshDetectedMannequinsFromWorld();
    TestEqual(TEXT("Watched/frozen bodies still produce the same threat"),Light->TargetThreat,MovingThreat);
    for (int32 I=3;I<6;++I) Body(I);
    World->Tick(LEVELTICK_All,.21f); Light->RefreshDetectedMannequinsFromWorld();
    TestEqual(TEXT("Six bodies enable the second tier"),Light->GetThreatTier(),2);
    Bodies.Last()->SetActorLocation(FVector(100,0,1200));
    World->Tick(LEVELTICK_All,.21f); Light->RefreshDetectedMannequinsFromWorld();
    TestEqual(TEXT("Other-floor bodies are excluded"),Light->GetDetectedMannequinCount(),5);
    auto* Tall=World->SpawnActor<AMannequinReactiveLight>(); Tall->SetActorLocation(FVector(0,0,1280));
    Tall->RefreshDetectedMannequinsFromWorld();
    TestTrue(TEXT("Existing 12.8m mall ceiling fixtures still react to bodies on the viewer's floor"),Tall->TargetThreat>0);
    TestEqual(TEXT("A tall fixture cannot count bodies from the other floor"),Tall->GetDetectedMannequinCount(),5);
    auto* Far=World->SpawnActor<AMannequinReactiveLight>(); Far->SetActorLocation(FVector(30000,0,300));
    Far->RefreshDetectedMannequinsFromWorld();
    TestEqual(TEXT("Distant fixtures are not globally flickered"),Far->TargetThreat,0.f);
    auto* Settings=UProjectProject01GameUserSettings::Get();
    const float OldFlash=Settings->GetScreenFlashScale();
    const auto OldVFX=Settings->GetVFXIntensity();
    ON_SCOPE_EXIT { Settings->SetScreenFlashScale(OldFlash); Settings->SetVFXIntensity(OldVFX); };
    Settings->SetVFXIntensity(EProjectProject01VFXIntensity::Standard);
    Settings->SetScreenFlashScale(1); Light->ApplyBrightness(.3f);
    TestTrue(TEXT("Actual spotlight follows continuous brightness"),FMath::IsNearlyEqual(Light->ReactiveSpotLight->Intensity,Light->LightOnIntensity*.3f));
    Settings->SetScreenFlashScale(0); Light->ApplyBrightness(.3f);
    TestTrue(TEXT("Changing settings during an event restores steady light"),FMath::IsNearlyEqual(Light->ReactiveSpotLight->Intensity,Light->LightOnIntensity));
    Light->FaultPattern.Strength=1; Light->RefreshAfterCheckpointRestore();
    TestEqual(TEXT("Checkpoint restore clears the old fault envelope"),Light->FaultPattern.Strength,0.f);
    TestEqual(TEXT("Checkpoint restore retains threat detection"),Light->GetDetectedMannequinCount(),5);
    // Exercise the real DSP without requiring an output device, and verify its silent/active ranges.
    auto* Synth=NewObject<UReactiveLightElectricalSynth>(Light);
    int32 Rate=48000; Synth->Init(Rate);
    TArray<float> Samples; Samples.SetNumZeroed(48000);
    Synth->OnGenerateAudio(Samples.GetData(),Samples.Num());
    float Peak=0; for(float S:Samples) Peak=FMath::Max(Peak,FMath::Abs(S));
    TestEqual(TEXT("No fault means no electrical sound"),Peak,0.f);
    Synth->TargetGain=1; Synth->OnGenerateAudio(Samples.GetData(),Samples.Num());
    Peak=0; double Sum=0; for(float S:Samples){Peak=FMath::Max(Peak,FMath::Abs(S));Sum+=S*S;}
    TestTrue(TEXT("Electrical sound is finite, audible and does not clip"),FMath::IsFinite(Sum)&&Peak>.03f&&Peak<.25f);
    TestTrue(TEXT("Electrical audio is positional and wall-occluded"),Synth->bAllowSpatialization&&Synth->AttenuationOverrides.bEnableOcclusion);
    TestTrue(TEXT("Electrical audio reaches listeners beneath the existing tall ceiling"),
        Synth->AttenuationOverrides.AttenuationShapeExtents.X+Synth->AttenuationOverrides.FalloffDistance>1280);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReactiveLightRenderTest,"ProjectProject01.Lighting.MannequinFaultRender",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectReactiveLightRenderTest::RunTest(const FString&)
{
    auto* World=LightTestWorld(); GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World);World->DestroyWorld(false); };
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    for (int32 I=0;I<2;++I)
    {
        auto* Mesh=World->SpawnActor<AStaticMeshActor>();
        Mesh->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Mesh->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Mesh->SetActorLocation(I ? FVector(450,0,170) : FVector(0,0,-40));
        Mesh->SetActorScale3D(I ? FVector(.2f,14,4) : FVector(14,14,.2f));
    }
    auto* Light=World->SpawnActor<AMannequinReactiveLight>();
    Light->SetActorLocation(FVector(0,0,400)); Light->SetActorRotation(FRotator(-90,0,0));
    auto* Capture=World->SpawnActor<ASceneCapture2D>();
    Capture->SetActorTransform(FTransform((FVector(100,0,50)-FVector(-550,-450,300)).Rotation(),FVector(-550,-450,300)));
    auto* Comp=Capture->GetCaptureComponent2D(); Comp->bCaptureEveryFrame=false; Comp->bCaptureOnMovement=false;
    Comp->CaptureSource=SCS_FinalColorLDR; Comp->bAlwaysPersistRenderingState=true; Comp->FOVAngle=65;
    Comp->ShowFlags.SetMotionBlur(false);
    auto& PP=Comp->PostProcessSettings; PP.bOverride_AutoExposureMethod=true; PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true; PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_AutoExposureBias=true; PP.AutoExposureBias=0; Comp->PostProcessBlendWeight=1;
    auto* Target=NewObject<UTextureRenderTarget2D>(); Target->ClearColor=FLinearColor::Black;
    Target->InitCustomFormat(640,360,PF_B8G8R8A8,false); Target->UpdateResourceImmediate(true); Comp->TextureTarget=Target;
    auto* Settings=UProjectProject01GameUserSettings::Get(); const float Old=Settings->GetScreenFlashScale();
    ON_SCOPE_EXIT { Settings->SetScreenFlashScale(Old); };
    Settings->SetScreenFlashScale(1);
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("ReactiveLightQA"); IFileManager::Get().MakeDirectory(*Dir,true);
    double Sums[2]={0,0};
    for (int32 Frame=0;Frame<2;++Frame)
    {
        Light->ApplyBrightness(Frame ? .25f : 1.f);
        for(int32 I=0;I<8;++I){Comp->CaptureScene();FlushRenderingCommands();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();}
        TArray<FColor> Pixels; FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
        if (!TestTrue(TEXT("Read actual scene render"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags))) return false;
        for (FColor& P:Pixels) { Sums[Frame]+=P.R+P.G+P.B; P.A=255; }
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(640,360,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(Dir/(Frame ? TEXT("Fault.png") : TEXT("Steady.png"))));
    }
    AddInfo(FString::Printf(TEXT("Scene brightness steady=%.0f fault=%.0f"),Sums[0],Sums[1]));
    TestTrue(TEXT("The fixture dims real world surfaces, not a screen overlay"),Sums[0]>Sums[1]*1.1 && Sums[1]>1000);
    return true;
}
#endif

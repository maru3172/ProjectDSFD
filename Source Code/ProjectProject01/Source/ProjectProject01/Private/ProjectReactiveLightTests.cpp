#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "MannequinReactiveLight.h"
#include "MannequinAICharacter.h"
#include "ProjectProject01GameInstance.h"
#include "Components/SpotLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/PointLight.h"
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
#include "Materials/MaterialInstanceDynamic.h"
#include "ConvexVolume.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"

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
    auto* Floor=World->SpawnActor<AStaticMeshActor>();
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->SetActorLocation(FVector(0,0,-25));Floor->SetActorScale3D(FVector(1000,1000,.5));
    auto* PC=World->SpawnActor<APlayerController>();
    PC->SetAsLocalPlayerController();
    // This isolated world has not run player initialization, so provide its camera manager explicitly.
    PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
    PC->PlayerCameraManager->InitializeFor(PC);
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
    TestTrue(TEXT("Existing 12.8m mall ceiling fixtures react to bodies on the floor beneath the fixture"),Tall->TargetThreat>0);
    TestEqual(TEXT("A tall fixture cannot count bodies from the other floor"),Tall->GetDetectedMannequinCount(),5);
    auto* Far=World->SpawnActor<AMannequinReactiveLight>(); Far->SetActorLocation(FVector(30000,0,300));
    Far->RefreshDetectedMannequinsFromWorld();
    TestEqual(TEXT("Distant fixtures are not globally flickered"),Far->TargetThreat,0.f);
    const float FixtureThreat=Light->TargetThreat;
    auto* DistantCamera=World->SpawnActor<ACameraActor>();DistantCamera->SetActorLocation(FVector(20000,0,160));
    PC->SetViewTarget(DistantCamera);
    if(!TestNotNull(TEXT("Local camera manager"),PC->PlayerCameraManager.Get()))return false;
    PC->PlayerCameraManager->UpdateCamera(0);
    FVector Eye;FRotator Facing;PC->GetPlayerViewPoint(Eye,Facing);
    TestTrue(TEXT("The viewer really moved far from the tested fixture"),Eye.X>19000);
    Light->RefreshDetectedMannequinsFromWorld();
    TestEqual(TEXT("Moving the camera cannot change fixture-local mannequin count"),Light->GetDetectedMannequinCount(),5);
    TestEqual(TEXT("Moving the camera cannot weaken fixture-local threat"),Light->TargetThreat,FixtureThreat);
    PC->SetViewTarget(View);PC->PlayerCameraManager->UpdateCamera(0);
    auto* NearB=World->SpawnActor<AMannequinReactiveLight>();NearB->SetActorLocation(FVector(200,0,300));
    auto* NearC=World->SpawnActor<AMannequinReactiveLight>();NearC->SetActorLocation(FVector(-200,0,300));
    int32 PeakActive=0;
    for(int32 Frame=0;Frame<300;++Frame)
    {
        World->Tick(LEVELTICK_All,1.f/30);
        int32 Active=0;
        for(auto* Lamp:{Light,NearB,NearC})
        {
            if(Frame%6==0)Lamp->RefreshDetectedMannequinsFromWorld();
            Lamp->Tick(1.f/30);Active+=!Lamp->FaultPattern.Keys.IsEmpty();
        }
        PeakActive=FMath::Max(PeakActive,Active);
    }
    TestTrue(TEXT("Actual nearby fixtures can run overlapping faults in the same local world"),PeakActive>=2);
    auto* Settings=UProjectProject01GameUserSettings::Get();
    const float OldFlash=Settings->GetScreenFlashScale();
    const auto OldVFX=Settings->GetVFXIntensity();
    ON_SCOPE_EXIT { Settings->SetScreenFlashScale(OldFlash); Settings->SetVFXIntensity(OldVFX); };
    Settings->SetVFXIntensity(EProjectProject01VFXIntensity::Standard);
    Light->FaultPattern.SectionLevels=Light->WakeFromSections=FVector::OneVector;
    Settings->SetScreenFlashScale(1); Light->ApplyBrightness(.3f);
    TestTrue(TEXT("Actual spotlight follows continuous brightness"),FMath::IsNearlyEqual(Light->ReactiveSpotLight->Intensity,Light->LightOnIntensity*.3f));
    Settings->SetScreenFlashScale(0); Light->ApplyBrightness(.3f);
    TestTrue(TEXT("Changing settings during an event restores steady light"),FMath::IsNearlyEqual(Light->ReactiveSpotLight->Intensity,Light->LightOnIntensity));
    Light->BulbMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    auto* BulbMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/MyProject/Lighting/M_ReactiveBulb.M_ReactiveBulb"));
    if(!TestNotNull(TEXT("Runtime bulb material for outage verification"),BulbMaterial))return false;
    Light->BulbMesh->SetMaterial(0,BulbMaterial);
    Settings->SetScreenFlashScale(1);Light->ApplyBrightness(.0002f);Light->ApplyBrightness(0);
    TestTrue(TEXT("Outage turns the spotlight off, including its final near-zero cache transition"),
        Light->ReactiveSpotLight->Intensity==0&&!Light->ReactiveSpotLight->IsVisible());
    auto* BulbInstance=Cast<UMaterialInstanceDynamic>(Light->BulbMesh->GetMaterial(0));
    float Emission=-1;
    TestTrue(TEXT("Outage removes bulb emissive glow too"),BulbInstance
        &&BulbInstance->GetScalarParameterValue(FMaterialParameterInfo(Light->EmissiveParameterName),Emission)&&Emission==0);
    Light->ApplyBrightness(1);
    TestTrue(TEXT("The next lit pulse retains the requested full brightness"),
        Light->ReactiveSpotLight->IsVisible()&&Light->ReactiveSpotLight->Intensity==Light->LightOnIntensity);
    TestTrue(TEXT("A lit pulse restores the bulb emissive strength"),BulbInstance
        &&BulbInstance->GetScalarParameterValue(FMaterialParameterInfo(Light->EmissiveParameterName),Emission)&&Emission==Light->EmissiveOnValue);
    Light->FaultPattern.SectionLevels=FVector(.02,1,.2);Light->ApplyBrightness(1);
    FLinearColor Sections;
    TestTrue(TEXT("The connected bulb material receives independently dimmed sections"),BulbInstance
        &&BulbInstance->GetVectorParameterValue(FMaterialParameterInfo(TEXT("LampSections")),Sections)&&Sections.R<.1&&Sections.G>.9&&Sections.B<.3);
    TestTrue(TEXT("Partial emission also makes the surrounding light waver, without adding extra lights"),
        Light->ReactiveSpotLight->Intensity<Light->LightOnIntensity&&Light->ReactiveSpotLight->Intensity>Light->LightOnIntensity*.5);
    Settings->SetScreenFlashScale(0);Light->ApplyBrightness(.2);
    TestTrue(TEXT("Flash-disabled clients keep all diffuser sections steadily illuminated"),BulbInstance
        &&BulbInstance->GetVectorParameterValue(FMaterialParameterInfo(TEXT("LampSections")),Sections)&&Sections.Equals(FLinearColor::White,.001));
    Settings->SetScreenFlashScale(1);Light->FaultPattern.SectionLevels=FVector::OneVector;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReactiveLightVisibilityTest,"ProjectProject01.Lighting.MannequinFaultVisibility",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReactiveLightVisibilityTest::RunTest(const FString&)
{
    auto* World=LightTestWorld();GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto* Light=World->SpawnActor<AMannequinReactiveLight>();
    Light->SetActorLocationAndRotation(FVector(0,0,3000),FRotator(-90,0,0));
    Light->ReactiveSpotLight->SetAttenuationRadius(4500);
    // A small visible patch of flooring. The bulb is well outside it, but its light reaches it.
    FConvexVolume Visible;
    for(const FVector& Axis:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1)})
        Visible.Planes.Add(FPlane(Axis,200));
    Visible.Init();
    TestFalse(TEXT("The bulb itself is outside the visible region"),Visible.IntersectSphere(Light->GetActorLocation(),10));
    TestTrue(TEXT("Visible flooring keeps an offscreen fixture's light effect alive"),Light->IntersectsInfluence(Visible));
    Light->SetActorLocation(FVector(20000,0,3000));
    TestFalse(TEXT("A distant cone which cannot illuminate the view is culled"),Light->IntersectsInfluence(Visible));
    Light->bPresentationAllowed=true;Light->bPresentationRelevant=false;
    Light->FaultPattern.Reset(12);Light->FaultPattern.BeginEvent(3);
    const float Elapsed=Light->FaultPattern.Elapsed;
    Light->Tick(.1f);
    TestEqual(TEXT("Culled fixtures do not advance detailed fault calculations"),Light->FaultPattern.Elapsed,Elapsed);
    Light->bPresentationRelevant=true;Light->TargetThreat=1;Light->CurrentDetectedMannequinCount=9;
    Light->bElectricalSound=false;Light->WakeFromBrightness=1;Light->WakeBlendElapsed=0;Light->FaultPattern.Strength=1;
    Light->FaultPattern.Keys.Reset();Light->FaultPattern.Keys.Add({0,0});Light->FaultPattern.Keys.Add({1,0});
    Light->Tick(1.f/30);
    TestTrue(TEXT("Waking a hidden fixture blends from its held brightness instead of popping off"),Light->LastBrightness>.9f&&Light->LastBrightness<1);
    for(int32 I=0;I<8;++I)Light->Tick(1.f/30);
    TestEqual(TEXT("Wake smoothing finishes and permits the genuine full outage"),Light->LastBrightness,0.f);
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
    // Use the actual corridor point-light settings as the reference, including its candela units.
    Capture->SetActorLocationAndRotation(FVector(0,0,970),FRotator(-90,0,0)); Comp->FOVAngle=45;
    auto* Reference=World->SpawnActor<APointLight>();
    auto* ReferenceLight=Reference->FindComponentByClass<UPointLightComponent>();
    ReferenceLight->SetMobility(EComponentMobility::Movable);
    ReferenceLight->SetIntensityUnits(ELightUnits::Candelas);ReferenceLight->SetIntensity(85);
    ReferenceLight->SetAttenuationRadius(2000);Reference->SetActorLocation(FVector(0,0,300));
    ReferenceLight->SetCastShadows(false);
    double Floor[5]={};
    const float Heights[]={330.f,1270.f,1270.f,414.f,414.f};
    const float Intensities[]={0.f,137206.8f,940195.f,83786.43f,0.f};
    const float Radii[]={2000.f,1960.6132f,2346.0605f,2015.5635f,2015.5635f};
    const TCHAR* Images[]={TEXT("Corridor_Reference.png"),TEXT("Corridor_TallBefore.png"),TEXT("Corridor_TallMatched.png"),TEXT("Corridor_ShowroomMatched.png"),TEXT("FullOutage_Surface.png")};
    for(int32 Case=0;Case<5;++Case)
    {
        ReferenceLight->SetVisibility(Case==0);
        Light->SetActorLocation(FVector(0,0,Heights[Case]-30.f)); // Floor top is at Z=-30.
        Light->ReactiveSpotLight->SetAttenuationRadius(Radii[Case]);
        Light->LightOnIntensity=Intensities[Case]; Light->LastBrightness=-1; Light->ApplyBrightness(Case==4?0:1);
        for(int32 I=0;I<8;++I){Comp->CaptureScene();FlushRenderingCommands();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();}
        TArray<FColor> Pixels; FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
        if(!TestTrue(TEXT("Read calibrated surface render"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)))return false;
        for(int32 Y=168;Y<192;++Y)for(int32 X=308;X<332;++X)
        {const auto& P=Pixels[Y*640+X];Floor[Case]+=(P.R+P.G+P.B)/(3.0*24*24);}
        for(auto& P:Pixels)P.A=255;
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(640,360,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(Dir/Images[Case]));
    }
    AddInfo(FString::Printf(TEXT("Floor pixels corridor=%.2f tall-before=%.2f tall-matched=%.2f showroom-matched=%.2f"),Floor[0],Floor[1],Floor[2],Floor[3]));
    TestTrue(TEXT("Tall-ceiling surface lighting is visibly brighter"),Floor[2]>Floor[1]*1.25);
    TestTrue(TEXT("Tall-ceiling floor matches the corridor reference within 10 percent"),Floor[0]>1 && FMath::Abs(Floor[2]-Floor[0])<Floor[0]*.1);
    TestTrue(TEXT("Showroom floor also matches the corridor reference within 10 percent"),FMath::Abs(Floor[3]-Floor[0])<Floor[0]*.1);
    AddInfo(FString::Printf(TEXT("Unlit floor pixels after full outage: %.2f"),Floor[4]));
    // Final-color display dithering can leave sub-1% noise above RGB black even with zero light output.
    // Physical intensity, visibility and emissive are independently checked in the presentation test.
    TestTrue(TEXT("The same real floor becomes black when its only light is extinguished"),Floor[4]<2&&Floor[4]<Floor[3]*.01);
    Light->BulbMesh->SetStaticMesh(Cube);
    Light->BulbMesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/MyProject/Lighting/M_ReactiveBulb.M_ReactiveBulb")));
    Light->BulbMesh->SetWorldLocationAndRotation(FVector(0,0,200),FRotator::ZeroRotator);
    Light->BulbMesh->SetWorldScale3D(FVector(4,1,.05));Light->BulbMesh->SetCastShadow(false);
    Capture->SetActorLocationAndRotation(FVector(0,0,700),FRotator(-90,-90,0));
    Light->WakeBlendElapsed=.25f;Light->WakeFromSections=FVector::OneVector;
    double Bar[2][3]={};
    for(int32 Frame=0;Frame<2;++Frame)
    {
        Light->FaultPattern.SectionLevels=Frame?FVector(0,1,.02):FVector::OneVector;Light->ApplyBrightness(1);
        for(int32 I=0;I<8;++I){Comp->CaptureScene();FlushRenderingCommands();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();}
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
        if(!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags))return false;
        for(int32 Part=0;Part<3;++Part)for(int32 Y=175;Y<185;++Y)for(int32 X=155+Part*160;X<165+Part*160;++X)
        {const auto& P=Pixels[Y*640+X];Bar[Frame][Part]+=(P.R+P.G+P.B)/300.;}
        for(auto& P:Pixels)P.A=255;
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(640,360,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(Dir/(Frame?TEXT("Diffuser_Discharge.png"):TEXT("Diffuser_Steady.png"))));
    }
    AddInfo(FString::Printf(TEXT("Diffuser partial pixels ends=%.1f/%.1f centre=%.1f; steady=%.1f/%.1f/%.1f"),Bar[1][0],Bar[1][2],Bar[1][1],Bar[0][0],Bar[0][1],Bar[0][2]));
    TestTrue(TEXT("The actual shader shows a dark end, a bright centre and a weakly glowing other end"),
        FMath::Min(Bar[1][0],Bar[1][2])<Bar[1][1]*.3&&FMath::Max(Bar[1][0],Bar[1][2])<Bar[1][1]*.85&&Bar[1][1]>100);
    return true;
}
#endif

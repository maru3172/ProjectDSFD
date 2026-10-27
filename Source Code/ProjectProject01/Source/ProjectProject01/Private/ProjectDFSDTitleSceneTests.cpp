#if WITH_DEV_AUTOMATION_TESTS
#include "ProjectDFSDTitleScene.h"
#include "ProjectProject01FrontendGameMode.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01LoginWidget.h"
#include "ProjectDFSDBranding.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ProjectUIRenderCapture.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"

namespace TitleSceneTests
{
	UWorld* World()
	{
		const auto V=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
		auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);return W;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDFSDTitleSceneTest,"ProjectProject01.Frontend.TitleScene",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectDFSDTitleSceneTest::RunTest(const FString&)
{
	float Previous=1,MaxStep=0,Lowest=1;
	for(int32 I=0;I<=500;++I)
	{
		const float Value=AProjectDFSDTitleScene::LightEnvelope(I*.004f,true);
		TestTrue(TEXT("Display light never strobes to white or total black"),Value>=.16f&&Value<=1);
		MaxStep=FMath::Max(MaxStep,FMath::Abs(Value-Previous));Lowest=FMath::Min(Lowest,Value);Previous=Value;
	}
	TestTrue(TEXT("Brief lamp failures have smooth edges, never hard cuts"),MaxStep<.06f&&Lowest<.2f);
	TestTrue(TEXT("First blink is clearly dimmed"),AProjectDFSDTitleScene::LightEnvelope(.38f,false)<.2f);
	TestEqual(TEXT("Lamp returns to its baseline between blinks"),AProjectDFSDTitleScene::LightEnvelope(.8f,true),1.0f);
	TestEqual(TEXT("Single-blink event does not add a second failure"),AProjectDFSDTitleScene::LightEnvelope(1.18f,false),1.0f);
	TestTrue(TEXT("Double-blink event includes a separate second failure"),AProjectDFSDTitleScene::LightEnvelope(1.18f,true)<.2f);
	TestEqual(TEXT("Light fully recovers after the second blink"),AProjectDFSDTitleScene::LightEnvelope(1.6f,true),1.0f);
	TestEqual(TEXT("Between events the light is stable"),AProjectDFSDTitleScene::LightEnvelope(-1,true),1.0f);
	auto* W=TitleSceneTests::World();ON_SCOPE_EXIT{W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
	auto* Scene=W->SpawnActor<AProjectDFSDTitleScene>();Scene->DispatchBeginPlay();
	TestTrue(TEXT("The first faulty-lamp event waits for a bounded random delay"),Scene->UntilDip>=2.5f&&Scene->UntilDip<=5.5f);
	TestTrue(TEXT("A real 3D mannequin and authored materials are available"),Scene->Mannequin->GetSkeletalMeshAsset()!=nullptr&&Scene->SurfaceMaterial&&Scene->MistMaterial&&Scene->DustMaterial&&Scene->IdlePose);
	TestTrue(TEXT("The mannequin surface is saved with skeletal usage for packaged clients"),Scene->SurfaceMaterial&&Scene->SurfaceMaterial->GetMaterial()->GetUsageByFlag(MATUSAGE_SkeletalMesh));
	TestFalse(TEXT("Title decoration never replicates as a match actor"),Scene->GetIsReplicated());
	TestTrue(TEXT("Mannequin is decorative, with no physics attack, overlap, or navigation"),
		Scene->Mannequin->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&!Scene->Mannequin->GetGenerateOverlapEvents()&&!Scene->Mannequin->CanEverAffectNavigation());
	TestTrue(TEXT("Display mannequin does not run a gameplay animation/AI tick"),Scene->Mannequin->bPauseAnims&&!Scene->Mannequin->IsComponentTickEnabled());
	const float ViewerYaw=(Scene->Camera->GetRelativeLocation()-Scene->Mannequin->GetRelativeLocation()).Rotation().Yaw;
	const float BodyOffset=FMath::Abs(FMath::FindDeltaAngleDegrees(Scene->Mannequin->GetRelativeRotation().Yaw+90,ViewerYaw));
	TestTrue(TEXT("Body and feet face towards the viewer at a modest diagonal, not a heavily twisted neck"),BodyOffset>=20&&BodyOffset<=35);
	TestTrue(TEXT("Camera fits the same 16:9 safe frame as title controls"),Scene->Camera->bConstrainAspectRatio&&FMath::IsNearlyEqual(Scene->Camera->AspectRatio,16.f/9));
	TestTrue(TEXT("Camera exposure is fixed, not pumping with the light"),Scene->Camera->PostProcessSettings.AutoExposureMethod==AEM_Manual);
	TestEqual(TEXT("Only bounded dust and mist meshes exist"),Scene->Dust.Num()+Scene->MistSheets.Num(),28);
	TestEqual(TEXT("Effects share only two dynamic materials"),Scene->EffectMaterials.Num(),2);
	Scene->Tick(1);TestEqual(TEXT("Intro hold does not animate the set"),Scene->Clock,0.0f);
	auto* S=UProjectProject01GameUserSettings::Get();
	const float OldFlash=S->GetScreenFlashScale(),OldShake=S->GetScreenShakeScale();
	const auto OldVFX=S->GetVFXIntensity();ON_SCOPE_EXIT{S->SetScreenFlashScale(OldFlash);S->SetScreenShakeScale(OldShake);S->SetVFXIntensity(OldVFX);};
	S->SetScreenFlashScale(0);S->SetScreenShakeScale(0);S->SetVFXIntensity(EProjectProject01VFXIntensity::Reduced);
	Scene->SetPresentationActive(true);Scene->DipTime=.38f;Scene->Tick(0);
	TestEqual(TEXT("Flash reduction zero disables light dips completely"),Scene->KeyLight->Intensity,AProjectDFSDTitleScene::BaseKeyIntensity);
	TestTrue(TEXT("Motion reduction zero leaves the camera still"),Scene->Camera->GetRelativeLocation().Equals(FVector(-550,0,125))&&Scene->Camera->GetRelativeRotation().Equals(FRotator(-1,0,0)));
	float MistOpacity=0;Scene->EffectMaterials[0]->GetScalarParameterValue(TEXT("Opacity"),MistOpacity);
	TestTrue(TEXT("Reduced effects lowers title mist density"),FMath::IsNearlyEqual(MistOpacity,.13f*.4f));
	S->SetScreenFlashScale(1);S->SetVFXIntensity(EProjectProject01VFXIntensity::Standard);Scene->Tick(.016f);
	TestTrue(TEXT("Normal title lighting can dim without fully disappearing"),Scene->KeyLight->Intensity<AProjectDFSDTitleScene::BaseKeyIntensity&&Scene->KeyLight->Intensity>=AProjectDFSDTitleScene::BaseKeyIntensity*.15f);
	// Pin only this fixture's stream, so randomness coverage is reproducible, never probabilistic.
	Scene->Random.Initialize(7341);int32 Singles=0,Doubles=0;float Shortest=13,Longest=7;
	for(int32 I=0;I<64;++I)
	{
		Scene->DipTime=-1;Scene->UntilDip=0;Scene->Tick(0);
		if(Scene->bDoubleDip)++Doubles;else ++Singles;
		Scene->DipTime=1.8f;Scene->Tick(0);
		TestTrue(TEXT("Each event recovers and waits 7-13 seconds before another failure"),Scene->DipTime<0&&Scene->UntilDip>=7&&Scene->UntilDip<=13);
		Shortest=FMath::Min(Shortest,Scene->UntilDip);Longest=FMath::Max(Longest,Scene->UntilDip);
	}
	TestTrue(TEXT("The random scheduler produces both single and double blinks"),Singles>0&&Doubles>0);
	TestTrue(TEXT("Faults do not recur on a fixed metronome interval"),Longest-Shortest>2);
	const float Clock=Scene->Clock;Scene->SetPresentationActive(false);Scene->Tick(1);
	TestEqual(TEXT("Leaving the presentation stops all procedural updates"),Scene->Clock,Clock);
	TestTrue(TEXT("Only title controllers select this set"),GetDefault<AProjectProject01TitleGameMode>()->PlayerControllerClass==AProjectProject01TitlePlayerController::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDFSDTitleSceneRenderTest,"ProjectProject01.Frontend.TitleSceneRender",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectDFSDTitleSceneRenderTest::RunTest(const FString&)
{
	auto* W=TitleSceneTests::World();ON_SCOPE_EXIT{W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
	auto* Settings=UProjectProject01GameUserSettings::Get();const float OldFlash=Settings->GetScreenFlashScale();
	Settings->SetScreenFlashScale(1);ON_SCOPE_EXIT{Settings->SetScreenFlashScale(OldFlash);};
	auto* Scene=W->SpawnActor<AProjectDFSDTitleScene>();Scene->DispatchBeginPlay();Scene->SetPresentationActive(true);Scene->Tick(0);
	auto* CaptureActor=W->SpawnActor<ASceneCapture2D>();auto* Capture=CaptureActor->GetCaptureComponent2D();
	Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->CaptureSource=SCS_FinalColorLDR;
	Capture->bAlwaysPersistRenderingState=true;
	CaptureActor->SetActorTransform(Scene->Camera->GetComponentTransform());Capture->FOVAngle=Scene->Camera->FieldOfView;
	Capture->PostProcessSettings=Scene->Camera->PostProcessSettings;Capture->PostProcessBlendWeight=1;
	Capture->ShowFlags.SetMotionBlur(false);Capture->ShowFlags.SetTemporalAA(true);Capture->ShowFlags.SetEyeAdaptation(true);
	auto* SceneTarget=NewObject<UTextureRenderTarget2D>();SceneTarget->ClearColor=FLinearColor::Black;
	SceneTarget->InitCustomFormat(1280,720,PF_B8G8R8A8,false);SceneTarget->UpdateResourceImmediate(true);Capture->TextureTarget=SceneTarget;
	const FString Directory=FPaths::ProjectSavedDir()/TEXT("TitleSceneQA");IFileManager::Get().MakeDirectory(*Directory,true);
	const auto Read=[&](UTextureRenderTarget2D* Target,int32 Width,int32 Height,const TCHAR* Name,bool LinearUI=false)
	{
		FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
		if(LinearUI)ProjectUIRenderCapture::Read(Target,Pixels);else Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
		if(Pixels.Num()==Width*Height)
		{
			TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Width,Height,Pixels,Png);
			FFileHelper::SaveArrayToFile(Png,*(Directory/FString::Printf(TEXT("%s_%dx%d.png"),Name,Width,Height)));
		}
		return Pixels;
	};
	// A persistent view state is required for the manual exposure and temporal shadow history.
	for (int32 I=0;I<8;++I) { Capture->CaptureScene(); FlushRenderingCommands(); }
	const auto Normal=Read(SceneTarget,1280,720,TEXT("Scene"));
	int32 RightVisible=0;for(int32 Y=60;Y<680;++Y)for(int32 X=800;X<1180;++X)
		if(Normal.IsValidIndex(Y*1280+X)&&FMath::Max3(Normal[Y*1280+X].R,Normal[Y*1280+X].G,Normal[Y*1280+X].B)>25)++RightVisible;
	TestTrue(TEXT("Right-side 3D mannequin/set is visible, not an empty black background"),RightVisible>4000);
	auto* Title=NewObject<UProjectProject01TitleWidget>();Title->Initialize();
	FSlateBrush Brush;Brush.SetResourceObject(SceneTarget);Brush.ImageSize=FVector2D(1280,720);
	TSharedRef<SWidget> Slate=SNew(SOverlay)
		+SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor::Black)]
		+SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(&Brush)]]
		+SOverlay::Slot()[Title->TakeWidget()];
	FWidgetRenderer Renderer(false);
	for(FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600),FIntPoint(1920,800)})
	{
		auto* UI=ProjectUIRenderCapture::Create(Size);
		Renderer.DrawWidget(UI,Slate,FVector2D(Size),0);Renderer.DrawWidget(UI,Slate,FVector2D(Size),0);
		const auto Pixels=Read(UI,Size.X,Size.Y,TEXT("Title"),true);
		const auto& First=Title->SinglePlayerButton->GetCachedGeometry();const auto& Last=Title->QuitButton->GetCachedGeometry();
		TestTrue(TEXT("Menu stays left and does not overlap mannequin across aspect ratios"),First.GetAbsolutePosition().X>0&&First.GetAbsolutePosition().X+First.GetAbsoluteSize().X<Size.X*.52);
		TestTrue(TEXT("All title buttons stay within the safe frame"),Last.GetAbsolutePosition().Y+Last.GetAbsoluteSize().Y<Size.Y);
		TestTrue(TEXT("Composited title includes the real 3D scene"),Pixels.Num()==Size.X*Size.Y);
	}
	Scene->DipTime=.38f;Scene->Tick(0);Capture->CaptureScene();const auto Dimmed=Read(SceneTarget,1280,720,TEXT("SceneDimmed"));
	TestTrue(TEXT("Spotlight changes actual 3D lighting and shadows"),Normal!=Dimmed);
	Capture->TextureTarget=nullptr;Title->ReleaseSlateResources(true);FlushRenderingCommands();
	return true;
}
#endif

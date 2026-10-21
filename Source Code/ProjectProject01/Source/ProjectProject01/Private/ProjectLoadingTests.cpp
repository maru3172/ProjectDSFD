#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectLoadingBarrier.h"
#include "ProjectLoadingScreenState.h"
#include "ProjectSpectatorCameraManager.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "ProjectProject01VoiceChatSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLoadingBarrierTest,"ProjectProject01.Multiplayer.Loading.Barrier",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectLoadingBarrierTest::RunTest(const FString&)
{
	FProjectLoadingBarrier Gate;Gate.Begin(3,10);const FString Epoch=Gate.Epoch;
	Gate.Join(TEXT("A"));Gate.Join(TEXT("B"));Gate.Join(TEXT("C"));
	TestFalse(TEXT("Unknown identity cannot report ready"),Gate.Report(TEXT("D"),Epoch,100));
	TestFalse(TEXT("Old world cannot open the next world's barrier"),Gate.Report(TEXT("A"),TEXT("old"),100));
	TestFalse(TEXT("Out-of-range progress rejected"),Gate.Report(TEXT("A"),Epoch,101));
	Gate.Report(TEXT("A"),Epoch,100);Gate.Report(TEXT("A"),Epoch,80);Gate.Report(TEXT("B"),Epoch,100);
	TestEqual(TEXT("Progress never rolls backward"),Gate.Progress[TEXT("A")],uint8(100));
	TestFalse(TEXT("Two ready clients do not start a three-client game"),Gate.AllReady());
	Gate.Report(TEXT("C"),Epoch,100);TestTrue(TEXT("All participants required"),Gate.AllReady());
	Gate.Leave(TEXT("B"));TestFalse(TEXT("Disconnected ready participant no longer counts"),Gate.AllReady());
	TestFalse(TEXT("Bounded wait does not expire early"),Gate.Expired(129));TestTrue(TEXT("Bounded wait expires"),Gate.Expired(131));
	Gate.Begin(3,200);TestTrue(TEXT("New map resets the epoch and readiness"),Gate.Epoch!=Epoch && Gate.Progress.IsEmpty());
	FProjectLoadingScreenState State;State.Begin(true,10);State.Advance(70,TEXT("map"));State.Advance(30,TEXT("stale"));
	TestEqual(TEXT("Presentation progress is monotonic"),State.Read().Progress,uint8(70));
	State.Advance(100,TEXT("ready"));TestFalse(TEXT("Local ready does not hide loading before release"),State.TryClose(20));
	{FScopeLock Lock(&State.Mutex);State.Data.Released=true;}
	TestFalse(TEXT("Fast loads retain a readable minimum"),State.TryClose(10.1));
	TestTrue(TEXT("Release starts closing"),State.TryClose(20));
	TestTrue(TEXT("Loading fades through black, not directly into game"),FMath::IsNearlyEqual(State.ContentOpacity(20.2),.5f,.01f));
	TestEqual(TEXT("Full black at handoff"),State.ContentOpacity(20.4),0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLoadingRenderTest,"ProjectProject01.Multiplayer.Loading.Render",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectLoadingRenderTest::RunTest(const FString&)
{
	const FString Directory=FPaths::ProjectSavedDir()/TEXT("LoadingScreenQA");IFileManager::Get().MakeDirectory(*Directory,true);
	for(const FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
	{
		auto State=MakeShared<FProjectLoadingScreenState,ESPMode::ThreadSafe>();State->Begin(true,FPlatformTime::Seconds()-12);
		{FScopeLock Lock(&State->Mutex);State->Data.Progress=90;State->Data.OwnId=TEXT("A");State->Data.OwnName=TEXT("나의 생존자");
			FProjectLoadingParticipant A;A.UserId=TEXT("A");A.DisplayName=State->Data.OwnName;A.Progress=90;
			FProjectLoadingParticipant B;B.UserId=TEXT("B");B.DisplayName=TEXT("다른 참가자");B.Progress=100;
			FProjectLoadingParticipant C;C.UserId=TEXT("C");C.DisplayName=TEXT("접속 중인 참가자");C.Progress=0;State->Data.Participants={A,B,C};}
		const auto Widget=MakeProjectLoadingScreenWidget(State);FWidgetRenderer Renderer(true);
		auto* Target=Renderer.DrawWidget(Widget,FVector2D(Size));
		for(int32 Frame=0;Frame<4;++Frame)Renderer.DrawWidget(Target,Widget,FVector2D(Size),.016f);
		FlushRenderingCommands();
		TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
		TestTrue(TEXT("Loading UI renders real progress pixels"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
		int32 OwnBarRows=0;
		if(Pixels.Num()==Size.X*Size.Y)
			for(int32 Y=Size.Y-130;Y<Size.Y-25;++Y)
			{
				const auto& Pixel=Pixels[Y*Size.X+Size.X/2];
				if(int32(Pixel.R)>int32(Pixel.G)+10 && int32(Pixel.R)>int32(Pixel.B)+20)++OwnBarRows;
			}
		TestTrue(TEXT("Own wide gauge remains visible at bottom center, at its original 24px thickness"),OwnBarRows>=20 && OwnBarRows<=24);
		TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
		TestTrue(TEXT("Save loading UI for visual inspection"),FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("Loading_%dx%d.png"),Size.X,Size.Y))));
		{FScopeLock Lock(&State->Mutex);State->Data.Progress=10;State->Data.OwnRole=TEXT("생존자");State->Data.Stage=TEXT("게임 서버 준비 중…");State->Data.CanCancel=true;
			for(auto& Participant:State->Data.Participants)Participant.Progress=0;}
		for(int32 Frame=0;Frame<4;++Frame)Renderer.DrawWidget(Target,Widget,FVector2D(Size),.016f);
		FlushRenderingCommands();Pixels.Reset();Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
		FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
		TestTrue(TEXT("Server allocation renders within the same loading screen"),FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("ServerPreparing_%dx%d.png"),Size.X,Size.Y))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDeadSpectatorTest,"ProjectProject01.Multiplayer.Spectator.FreeLookAndListenOnly",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectDeadSpectatorTest::RunTest(const FString&)
{
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Target=World->SpawnActor<APlayerCharacter>(Spawn);Target->SetActorLocation(FVector(100,200,300));Target->SetActorRotation(FRotator(0,30,0));
	if(auto* Class=LoadClass<APlayerCharacter>(nullptr,TEXT("/Game/MyProject/BP_PlayerCharacter.BP_PlayerCharacter_C")))
	{
		auto* Actual=World->SpawnActor<APlayerCharacter>(Class,Spawn);
		if(auto* Arm=Actual->FindComponentByClass<USpringArmComponent>())
			AddInfo(FString::Printf(TEXT("Survivor camera rig: arm=%s length=%.1f inheritPitch=%d usePawn=%d lag=%d"),*Arm->GetRelativeLocation().ToString(),Arm->TargetArmLength,Arm->bInheritPitch,Arm->bUsePawnControlRotation,Arm->bEnableCameraLag));
		if(auto* Camera=Actual->FindComponentByClass<UCameraComponent>())
			AddInfo(FString::Printf(TEXT("Survivor camera offset: %s usePawn=%d"),*Camera->GetRelativeLocation().ToString(),Camera->bUsePawnControlRotation));
	}
	auto* PC=World->SpawnActor<AMultiplayTestPlayerController>();PC->SetAsLocalPlayerController();World->AddController(PC);
	auto* Local=NewObject<ULocalPlayer>(GEngine);Local->PlayerController=PC;PC->Player=Local;
	PC->InputComponent=NewObject<UEnhancedInputComponent>(PC);PC->MatchPhase=EMultiplayTestMatchPhase::InProgress;
	PC->SurvivorState=EMultiplayTestSurvivorState::Eliminated;
	PC->SurvivorSpectatorView.bEnabled=true;PC->SurvivorSpectatorView.Target=Target;
	PC->PlayerCameraManager=World->SpawnActor<AProjectSpectatorCameraManager>();PC->PlayerCameraManager->InitializeFor(PC);
	PC->ApplySurvivorSpectatorView();PC->bShowMouseCursor=false;
	PC->SetControlRotation(FRotator(15,110,0));PC->PlayerCameraManager->UpdateCamera(.016f);
	TestTrue(FString::Printf(TEXT("Observer can look away from the living target (%s)"),*PC->PlayerCameraManager->GetCameraRotation().ToString()),PC->PlayerCameraManager->GetCameraRotation().Equals(FRotator(15,110,0),.1f));
	TestTrue(TEXT("Observer position follows target, without a free-flying pawn"),PC->GetViewTarget()==Target && !PC->GetPawn());
	FMinimalViewInfo Anchor;Target->CalcCamera(.016f,Anchor);
	const FVector ExpectedOffset=-Target->GetActorForwardVector().GetSafeNormal2D()*90.f+FVector(0,0,40);
	const FVector ExpectedLocation=Anchor.Location+ExpectedOffset;
	TestTrue(TEXT("Spectator mount is behind the target body and above its first-person camera"),PC->PlayerCameraManager->GetCameraLocation().Equals(ExpectedLocation,.1f));
	for(const float YawOffset:{0.f,90.f,180.f,270.f})
	{
		const FRotator Look(0,Target->GetActorRotation().Yaw+YawOffset,0);
		PC->SetControlRotation(Look);PC->PlayerCameraManager->UpdateCamera(.016f);
		TestTrue(TEXT("Turning the observer does not orbit the camera into the target's face"),PC->PlayerCameraManager->GetCameraLocation().Equals(ExpectedLocation,.1f));
		TestTrue(TEXT("All four viewing directions remain independently controllable"),PC->PlayerCameraManager->GetCameraRotation().Equals(Look,.1f));
	}
	TestTrue(TEXT("Body remains visible; no actor-hidden or per-view hide list is applied"),!Target->IsHidden() && !PC->HiddenActors.Contains(Target));
	auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(2,50,50));Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);
	Wall->SetActorLocationAndRotation(Anchor.Location+ExpectedOffset.GetSafeNormal()*10.f,ExpectedOffset.Rotation());Box->RegisterComponent();
	PC->PlayerCameraManager->UpdateCamera(.016f);
	TestTrue(TEXT("A close rear wall shortens camera retreat instead of clipping through it"),FVector::Distance(Anchor.Location,PC->PlayerCameraManager->GetCameraLocation())<14.f);
	Wall->Destroy();
	PC->SetControlRotation(FRotator(15,110,0));PC->PlayerCameraManager->UpdateCamera(.016f);
	TestTrue(TEXT("Observer does not rotate target body"),Target->GetActorRotation().Equals(FRotator(0,30,0),.1f));
	PC->RotationInput=FRotator::ZeroRotator;PC->HandleMannequinLook(FInputActionValue(FVector2D(2,1)));
	TestFalse(TEXT("Dead observer mouse input remains bound"),PC->RotationInput.IsNearlyZero());
	auto* Instance=NewObject<UGameInstance>();auto* Voice=NewObject<UProjectProject01VoiceChatSubsystem>(Instance);
	Voice->bConfiguredForSurvivor=true;Voice->bJoinedChannel=true;Voice->OwnerController=PC;Voice->MatchId=TEXT("match");
	Voice->ParticipantAccountIds.Add(TEXT("alive-eos"),TEXT("alive-account"));FProjectVoiceAudibility Peer;Peer.UserId=TEXT("alive-account");Peer.Gain=1;
	Voice->UpdateAuthoritativeAudibility(TEXT("match"),true,{Peer});
	TestEqual(TEXT("Dead observer retains living teammate receive gain"),Voice->GetParticipantDistanceGain(TEXT("alive-eos")),1.f);
	Voice->SetPushToTalkHeld(true);Voice->ToggleOpenMic();TestFalse(TEXT("Dead observer cannot turn on either microphone mode"),Voice->IsTransmitting() || Voice->IsOpenMicEnabled());
	PC->SurvivorState=EMultiplayTestSurvivorState::Active;Voice->SetPushToTalkHeld(true);
	TestTrue(TEXT("Active survivor may transmit with fresh server authority"),Voice->IsTransmitting());
	PC->SurvivorState=EMultiplayTestSurvivorState::Eliminated;Voice->StopSpeaking();
	TestTrue(TEXT("Death cancels held microphone but preserves channel"),!Voice->IsTransmitting() && !Voice->IsPushToTalkHeld() && Voice->IsVoiceReady());
	Voice->LeaveMatch();TestTrue(TEXT("Leaving/kick disables speaking and hearing, clears identity"),!Voice->IsVoiceReady() && !Voice->IsConfiguredForSurvivor() && Voice->MatchId.IsEmpty());
	World->DestroyWorld(false);return true;
}
#endif

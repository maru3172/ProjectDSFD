#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PlayerCharacter.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectSpectatorCameraManager.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "PlayerFeedbackWidgets.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "UObject/GarbageCollection.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSurvivorPresentationTest,"ProjectProject01.Multiplayer.Spectator.SharedPresentation",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectSurvivorPresentationTest::RunTest(const FString&)
{
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	auto* GI=NewObject<UGameInstance>();World->SetGameInstance(GI);GI->Init();
	auto* Target=World->SpawnActor<APlayerCharacter>();
	auto* PC=World->SpawnActor<AMultiplayTestPlayerController>();PC->SetAsLocalPlayerController();World->AddController(PC);
	auto* Local=NewObject<ULocalPlayer>(GEngine);Local->PlayerController=PC;PC->Player=Local;
	PC->MatchPhase=EMultiplayTestMatchPhase::InProgress;PC->SurvivorState=EMultiplayTestSurvivorState::Eliminated;
	PC->SurvivorSpectatorView.bEnabled=true;PC->SurvivorSpectatorView.Target=Target;PC->SetViewTarget(Target);
	PC->PlayerCameraManager=World->SpawnActor<AProjectSpectatorCameraManager>();PC->PlayerCameraManager->InitializeFor(PC);
	PC->ApplySurvivorSpectatorView();Target->Camera->Activate(true);Target->bFindCameraComponentWhenViewTarget=true;
	Target->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	auto* Settings=UProjectProject01GameUserSettings::Get();const float OldScale=Settings->GetConfiguredScreenShakeScale();
	const bool OldEnabled=Settings->IsCameraShakeEnabled();
	ON_SCOPE_EXIT{Settings->SetScreenShakeScale(OldScale);Settings->SetCameraShakeEnabled(OldEnabled);GI->Shutdown();World->SetGameInstance(nullptr);World->DestroyWorld(false);};
	Settings->SetCameraShakeEnabled(true);Settings->SetScreenShakeScale(1);
	Target->ObserverPresentation.StepOffset=-1.3f;Target->ObserverPresentation.StepPitch=-.13f;
	FMinimalViewInfo POV;Target->CalcCamera(0,POV);
	TestTrue(TEXT("Observer receives an unscaled foot-contact dip"),FMath::IsNearlyEqual(POV.Location.Z,Target->Camera->GetComponentLocation().Z-1.3,.001));
	Settings->SetCameraShakeEnabled(false);Target->CalcCamera(0,POV);
	TestTrue(TEXT("Observer's own OFF setting removes the dip regardless of sender"),POV.Location.Equals(Target->Camera->GetComponentLocation(),.001));
	TestEqual(TEXT("OFF never erases the replicated raw effect for another viewer"),Target->ObserverPresentation.StepOffset,-1.3f);
	Settings->SetCameraShakeEnabled(true);Settings->SetScreenShakeScale(.5);Target->CalcCamera(0,POV);
	TestTrue(TEXT("Observer's own reduced setting halves the shared dip"),FMath::IsNearlyEqual(POV.Location.Z,Target->Camera->GetComponentLocation().Z-.65,.001));
	Target->RecordObserverFeedback(1,0,.8f,false);Target->UpdateObserverPresentation(0);
	TestTrue(TEXT("An observed authoritative hit creates local damage VFX and camera reaction"),Target->DamageFeedbackWidget!=nullptr && Target->bCameraHitFeedbackActive);
	Target->UpdateCameraFeedback(.04f);PC->SetControlRotation(FRotator(0,120,0));PC->PlayerCameraManager->UpdateCamera(.016f);
	TestTrue(TEXT("Free look adds the target's hit tilt, rather than overwriting it"),PC->PlayerCameraManager->GetCameraRotation().Pitch>0);
	Target->RestoreFeedbackCamera();Target->bGameOver=true;Target->RecordObserverFeedback(0,1,1,true);Target->UpdateObserverPresentation(0);
	TestTrue(TEXT("Observed fatal event creates death fade and holds the target through the fall"),Target->DeathFeedbackWidget!=nullptr && Target->bCameraDeathFeedbackActive && Target->IsDeathPresentationPending());
	Target->UpdateCameraFeedback(.3f);
	TestTrue(TEXT("Observed death lowers the same first-person camera"),Target->Camera->GetRelativeLocation().Z<Target->FeedbackCameraStartLocation.Z);
	PC->SurvivorSpectatorView.bEnabled=false;Target->UpdateObserverPresentation(0);PC->PlayerCameraManager->UpdateCamera(.016f);
	TestTrue(TEXT("Retarget/exit removes old target damage/death overlays and camera offsets"),!Target->DamageFeedbackWidget && !Target->DeathFeedbackWidget && !Target->bCameraDeathFeedbackActive && !PC->HiddenActors.Contains(Target));
	FProjectSurvivorPresentation Bad;Bad.BPM=401;TestFalse(TEXT("Out-of-range client cosmetic state is rejected"),Bad.IsValid());
	Bad={};Bad.Proximity=std::numeric_limits<float>::quiet_NaN();TestFalse(TEXT("Non-finite cosmetic state is rejected"),Bad.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDiagnosticGCTest,"ProjectProject01.Diagnostics.GCMeasurement",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectDiagnosticGCTest::RunTest(const FString&)
{
	auto* World=UWorld::CreateWorld(EWorldType::Game,false);World->AddToRoot();
	auto* Diagnostics=World->GetSubsystem<UProjectProject01DiagnosticsSubsystem>();
	if(!TestNotNull(TEXT("Diagnostic world subsystem"),Diagnostics)){World->RemoveFromRoot();World->DestroyWorld(false);return false;}
	Diagnostics->bCaptureActive=true;
	// Explicit GC is confined to this test, never initiated by the collector in gameplay.
	CollectGarbage(RF_NoFlags);
	const auto* Event=Diagnostics->Events.FindByPredicate([](const auto& E){return E.Code==TEXT("GarbageCollectionMemoryDelta");});
	if(TestNotNull(TEXT("Real engine GC broadcasts are measured"),Event))
	{
		TestTrue(TEXT("GC event includes memory, object counts and elapsed time"),Event->Message.Contains(TEXT("PhysicalBeforeBytes=")) && Event->Message.Contains(TEXT("UObjectsAfter=")) && Event->Message.Contains(TEXT("ElapsedMilliseconds=")));
	}
	Diagnostics->bCaptureActive=false;World->RemoveFromRoot();World->DestroyWorld(false);return true;
}
#endif

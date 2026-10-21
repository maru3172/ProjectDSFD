#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01LocomotionFeedback.h"
#include "PlayerCharacter.h"
#include "FootstepSynthComponent.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectProject01GameInstance.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLocomotionFeedbackTest, "ProjectProject01.Player.FootContactCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectLocomotionFeedbackTest::RunTest(const FString&)
{
	FProjectProject01LocomotionFeedback Walk, Right, Run, RunRight;
	Walk.Contact(false, false); Right.Contact(false, true); Run.Contact(true, false); RunRight.Contact(true, true);
	TestEqual(TEXT("No camera jump on contact"), Walk.GetOffset(), 0.0f);
	TestEqual(TEXT("No pitch jump on contact"), Walk.GetPitchOffset(), 0.0f);
	Walk.Advance(0.32f * 0.28f); Right.Advance(0.32f * 0.28f); Run.Advance(0.26f * 0.28f);
	RunRight.Advance(0.26f * 0.28f);
	TestTrue(TEXT("Walking dip is thirty percent stronger at 1.3 centimetres"), FMath::IsNearlyEqual(Walk.GetOffset(), -1.3f));
	TestTrue(TEXT("Right walking contact retains ten percent extra depth at 1.43 centimetres"), FMath::IsNearlyEqual(Right.GetOffset(), -1.43f, 0.00001f));
	TestTrue(TEXT("Right running contact is ten percent deeper at 1.76 centimetres"), FMath::IsNearlyEqual(RunRight.GetOffset(), -1.76f, 0.00001f));
	TestTrue(TEXT("Running dip is slightly stronger at 1.6 centimetres"), FMath::IsNearlyEqual(Run.GetOffset(), -1.6f));
	TestTrue(TEXT("Walking nod remains small at 0.13 degrees"), FMath::IsNearlyEqual(Walk.GetPitchOffset(), -0.13f, 0.00001f));
	TestTrue(TEXT("Running nod peaks downward at 0.2 degrees"), FMath::IsNearlyEqual(Run.GetPitchOffset(), -0.2f, 0.00001f));
	TestTrue(TEXT("Right foot nod is also ten percent stronger"), FMath::IsNearlyEqual(Right.GetPitchOffset(), -0.143f, 0.00001f) &&
		FMath::IsNearlyEqual(RunRight.GetPitchOffset(), -0.22f, 0.00001f));
	TestEqual(TEXT("Zero accessibility shake disables bob entirely"), Run.GetOffset(0), 0.0f);
	TestEqual(TEXT("Zero accessibility shake disables nod entirely"), Run.GetPitchOffset(0), 0.0f);
	TestTrue(TEXT("Partial shake setting scales displacement"), FMath::IsNearlyEqual(Walk.GetOffset(0.5f), -0.65f));
	TestTrue(TEXT("Partial shake setting scales nod"), FMath::IsNearlyEqual(Walk.GetPitchOffset(0.5f), -0.065f, 0.00001f));
	TestTrue(TEXT("Right foot pitch is ten percent lower without louder gain"), FMath::IsNearlyEqual(
		FProjectProject01LocomotionFeedback::FootPitchScale(true) / FProjectProject01LocomotionFeedback::FootPitchScale(false), 0.90f));
	const float BeforeOverlap = Walk.GetOffset(), PitchBeforeOverlap = Walk.GetPitchOffset(); Walk.Contact(true, true);
	TestEqual(TEXT("Overlapping contact cannot reset the view height abruptly"), Walk.GetOffset(), BeforeOverlap);
	TestEqual(TEXT("Switching walk/run preserves current pitch on contact"), Walk.GetPitchOffset(), PitchBeforeOverlap);
	Walk.Advance(1); Right.Advance(1); Run.Advance(1); RunRight.Advance(1);
	TestTrue(TEXT("Stopping restores original height, with no persistent oscillator"), Walk.GetOffset()==0 && Right.GetOffset()==0 && Run.GetOffset()==0 && RunRight.GetOffset()==0);
	TestTrue(TEXT("Stopping restores original pitch with no drift"), Walk.GetPitchOffset()==0 && Right.GetPitchOffset()==0 && Run.GetPitchOffset()==0 && RunRight.GetPitchOffset()==0);
	Walk.Advance(500); TestEqual(TEXT("Idle never starts new motion"), Walk.GetOffset(), 0.0f);

	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	if (!TestNotNull(TEXT("Isolated camera fixture"), World)) return false;
	auto* Player=World->SpawnActor<APlayerCharacter>();
	auto* PC=World->SpawnActor<APlayerController>();
	// No GameMode/viewport in this isolated world: mirror GameMode's explicit local designation.
	PC->SetAsLocalPlayerController();
	if (!PC->PlayerCameraManager) PC->SpawnPlayerCameraManager();
	PC->Possess(Player); PC->SetViewTarget(Player);
	Player->Camera->Activate(true);
	Player->GetCharacterMovement()->SetUpdatedComponent(Player->GetRootComponent());
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Player->GetCharacterMovement()->Velocity=FVector(200,0,0);
	AddInfo(FString::Printf(TEXT("Fixture local=%d lifecycle=%d camera=%d view=%d ground=%d"),
		Player->IsLocallyControlled(), Player->CanPlayTensionBreathing(), Player->Camera->IsActive(),
		PC->GetViewTarget()==Player, Player->GetCharacterMovement()->IsMovingOnGround()));
	TestTrue(TEXT("Local single-player can use walking feedback"), Player->CanUseLocomotionCamera());
	auto* Foot=Player->FootstepSFXComponent.Get();
	Foot->Configure(EProjectProject01FootstepStyle::MarbleSneaker,0.0f,false);
	Foot->OnFootContact.AddUObject(Player, &APlayerCharacter::HandleFootContact);
	int32 Contacts=0; bool LastRight=true;
	Foot->OnFootContact.AddLambda([&](bool, bool RightFoot) { ++Contacts; LastRight=RightFoot; });
	Foot->TriggerFootstep(false);
	TestTrue(TEXT("Muted footsteps still deliver exactly one left contact"), Contacts==1 && !LastRight);
	Foot->TriggerFootstep(false);
	TestEqual(TEXT("Duplicate foot notify is rejected, not extra camera motion"), Contacts, 1);
	Player->LocomotionCameraFeedback.Advance(0.08f);
	const FVector CameraLocation=Player->Camera->GetRelativeLocation();
	const FRotator CameraRotation=Player->Camera->GetRelativeRotation();
	const float CameraFOV=Player->Camera->FieldOfView;
	const FRotator ControlRotation=PC->GetControlRotation();
	auto* Settings=UProjectProject01GameUserSettings::Get();
	const float PreviousShake=Settings->GetConfiguredScreenShakeScale();
	const bool PreviousEnabled=Settings->IsCameraShakeEnabled();
	Settings->SetCameraShakeEnabled(true);
	Settings->SetScreenShakeScale(0); FMinimalViewInfo Still; Player->CalcCamera(0,Still);
	Settings->SetScreenShakeScale(1); FMinimalViewInfo Bobbed; Player->CalcCamera(0,Bobbed);
	FMinimalViewInfo Repeated; Player->CalcCamera(0,Repeated);
	Settings->SetCameraShakeEnabled(false);FMinimalViewInfo Disabled;Player->CalcCamera(0,Disabled);
	TestTrue(TEXT("Camera shaking toggle suppresses actual locomotion displacement and pitch"),Disabled.Location.Equals(Still.Location) && Disabled.Rotation.Equals(Still.Rotation));
	Settings->SetScreenShakeScale(PreviousShake);
	Settings->SetCameraShakeEnabled(PreviousEnabled);
	TestTrue(TEXT("Actual CalcCamera adds only vertical translation and downward pitch"), Bobbed.Location.Z<Still.Location.Z &&
		FMath::IsNearlyEqual(Bobbed.Location.X,Still.Location.X) && FMath::IsNearlyEqual(Bobbed.Location.Y,Still.Location.Y) &&
		Bobbed.Rotation.Pitch<Still.Rotation.Pitch && FMath::IsNearlyEqual(Bobbed.Rotation.Yaw,Still.Rotation.Yaw) &&
		FMath::IsNearlyEqual(Bobbed.Rotation.Roll,Still.Rotation.Roll) && Bobbed.FOV==Still.FOV);
	TestTrue(TEXT("Repeated view evaluation never accumulates nod or modifies mouse input"),
		Repeated.Location.Equals(Bobbed.Location) && Repeated.Rotation.Equals(Bobbed.Rotation) && PC->GetControlRotation().Equals(ControlRotation));
	TestTrue(TEXT("No component transform/FOV drift or AI sight mutation"), CameraLocation.Equals(Player->Camera->GetRelativeLocation()) &&
		CameraRotation.Equals(Player->Camera->GetRelativeRotation()) && CameraFOV==Player->Camera->FieldOfView);
	Player->bCameraHitFeedbackActive=true;
	TestFalse(TEXT("Hit stumble takes priority"), Player->CanUseLocomotionCamera()); Player->bCameraHitFeedbackActive=false;
	Player->bCameraDeathFeedbackActive=true;
	TestFalse(TEXT("Death fall takes priority"), Player->CanUseLocomotionCamera()); Player->bCameraDeathFeedbackActive=false;
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	TestFalse(TEXT("No foot bob while airborne"), Player->CanUseLocomotionCamera());
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	PC->UnPossess();
	TestFalse(TEXT("Remote/unpossessed character cannot shake local view"), Player->CanUseLocomotionCamera());
	auto* Multi=World->SpawnActor<AMultiplayTestPlayerController>();
	Multi->SetAsLocalPlayerController();
	if (!Multi->PlayerCameraManager) Multi->SpawnPlayerCameraManager();
	Multi->Possess(Player); Multi->SetViewTarget(Player);
	Multi->SetServerMatchState(EMultiplayTestMatchPhase::InProgress, EMultiplayTestSurvivorState::Active);
	TestTrue(TEXT("Active multiplayer survivor uses the same feedback"), Player->CanUseLocomotionCamera());
	Multi->SetServerMatchState(EMultiplayTestMatchPhase::InProgress, EMultiplayTestSurvivorState::NotApplicable);
	TestFalse(TEXT("Mannequin role has no human walking bob"), Player->CanUseLocomotionCamera());
	Multi->SetServerMatchState(EMultiplayTestMatchPhase::InProgress, EMultiplayTestSurvivorState::Eliminated);
	TestFalse(TEXT("Spectators have no walking bob"), Player->CanUseLocomotionCamera());
	Multi->SetServerMatchState(EMultiplayTestMatchPhase::Results, EMultiplayTestSurvivorState::Active);
	TestFalse(TEXT("Results have no walking bob"), Player->CanUseLocomotionCamera());
	Foot->OnFootContact.Clear();
	World->DestroyWorld(false);
	return true;
}
#endif

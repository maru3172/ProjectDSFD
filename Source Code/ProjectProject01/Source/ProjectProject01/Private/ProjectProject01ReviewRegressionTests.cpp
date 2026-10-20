#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "PlayerCharacter.h"
#include "MannequinReactiveLight.h"
#include "MultiplayTestGameMode.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01LevelTransitionTrigger.h"
#include "ProjectProject01CheckpointStore.h"
#include "ProjectProject01SinglePlayerSaveGame.h"
#include "ProjectProject01SaveSubsystem.h"
#include "ProjectProject01LoginWidget.h"
#include "Components/TextBlock.h"
#include "InputCoreTypes.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "ProjectProject01CheckpointAI.h"
#include "BTService_MannequinAI.h"
#include "MannequinAICharacter.h"
#include "MannequinAIController.h"
#include "HelperRearGuardCharacter.h"
#include "HelperRearGuardAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "UObject/UObjectHash.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "ProjectProject01ResultOutbox.h"
#include "ProjectProject01TravelDeadline.h"
#include "PlayerHeartbeatComponent.h"
#include "Engine/LocalPlayer.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Components/EditableTextBox.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Async/TaskGraphInterfaces.h"
#include "ProjectProject01CrashFiles.h"
#include "Engine/PendingNetGame.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Serialization/BufferArchive.h"
#include "RenderingThread.h"

namespace
{
	UWorld* CreateReviewWorld()
	{
		const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
		return UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReviewRuntimeRegressionTest,"ProjectProject01.Review.Accessibility",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReviewRuntimeRegressionTest::RunTest(const FString&)
{
	auto* Settings=UProjectProject01GameUserSettings::Get();
	const float OldShake=Settings->GetScreenShakeScale(),OldFlash=Settings->GetScreenFlashScale();
	const auto OldVFX=Settings->GetVFXIntensity();
	UWorld* World=CreateReviewWorld();
	ON_SCOPE_EXIT { Settings->SetScreenShakeScale(OldShake);Settings->SetScreenFlashScale(OldFlash);Settings->SetVFXIntensity(OldVFX);World->DestroyWorld(false); };
	auto* Player=World->SpawnActor<APlayerCharacter>();
	const FVector BaseLocation(0,0,160);const FRotator BaseRotation(0,10,0);
	auto BeginFeedback=[&](bool Death)
	{
		Player->RestoreFeedbackCamera();Player->Camera->SetRelativeLocation(BaseLocation);Player->Camera->SetRelativeRotation(BaseRotation);
		Player->FeedbackCameraStartLocation=BaseLocation;Player->FeedbackCameraStartRotation=BaseRotation;
		Player->CameraFallPitchDirection=1;Player->CameraFallRollDirection=1;
		Player->bCameraHitFeedbackActive=!Death;Player->bCameraDeathFeedbackActive=Death;
	};
	Settings->SetScreenShakeScale(0);BeginFeedback(false);Player->UpdateCameraFeedback(0.04f);
	TestTrue(TEXT("Zero shake disables direct hit translations and rotations"),Player->Camera->GetRelativeLocation().Equals(BaseLocation)&&Player->Camera->GetRelativeRotation().Equals(BaseRotation));
	Settings->SetScreenShakeScale(1);BeginFeedback(false);Player->UpdateCameraFeedback(0.04f);
	const FVector FullHit=Player->Camera->GetRelativeLocation()-BaseLocation;
	TestFalse(TEXT("Default hit stumble remains active"),FullHit.IsNearlyZero());
	Settings->SetScreenShakeScale(0.5f);BeginFeedback(false);Player->UpdateCameraFeedback(0.04f);
	TestTrue(TEXT("Partial shake scales direct camera translation"),(Player->Camera->GetRelativeLocation()-BaseLocation).Equals(FullHit*0.5f,0.0001f));
	Settings->SetScreenShakeScale(0);BeginFeedback(true);Player->UpdateCameraFeedback(0.5f);
	TestTrue(TEXT("Zero shake disables neck turn, fall tilt and drop without cancelling death timing"),Player->Camera->GetRelativeLocation().Equals(BaseLocation)&&Player->Camera->GetRelativeRotation().Equals(BaseRotation)&&Player->bCameraDeathFeedbackActive);
	Settings->SetScreenShakeScale(1);BeginFeedback(true);Player->UpdateCameraFeedback(0.5f);
	TestTrue(TEXT("Default death fall and neck turn remain active"),Player->Camera->GetRelativeLocation().Z<BaseLocation.Z&&!Player->Camera->GetRelativeRotation().Equals(BaseRotation));
	auto* Light=World->SpawnActor<AMannequinReactiveLight>();
	Light->CurrentDetectedMannequinCount=3;
	Settings->SetVFXIntensity(EProjectProject01VFXIntensity::Standard);Settings->SetScreenFlashScale(0);Light->ApplyLightState(false);
	TestTrue(TEXT("Zero flash gives steady light even during logical off state"),FMath::IsNearlyEqual(Light->ReactiveSpotLight->Intensity,Light->LightOnIntensity)&&Light->ReactiveSpotLight->IsVisible()&&!Light->bLightOn);
	Settings->SetScreenFlashScale(0.5f);Light->ApplyLightState(false);
	TestTrue(TEXT("Reduced flash dims instead of hard black strobing"),FMath::IsNearlyEqual(Light->ReactiveSpotLight->Intensity,Light->LightOnIntensity*0.5f));
	Settings->SetScreenFlashScale(1);Light->ApplyLightState(false);
	TestEqual(TEXT("Default off state still turns the light off"),Light->ReactiveSpotLight->Intensity,0.0f);
	TestEqual(TEXT("Accessibility presentation cannot alter mannequin detection"),Light->CurrentDetectedMannequinCount,3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReviewMatchCarryoverTest,"ProjectProject01.Review.MatchCarryover",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReviewMatchCarryoverTest::RunTest(const FString&)
{
	for (const bool bForfeit : {false,true})
	for (const bool bDeathBeforeWaiting : {false,true})
	{
	UWorld* World=CreateReviewWorld();ON_SCOPE_EXIT { World->DestroyWorld(false); };
#if WITH_EDITOR
	World->WorldType=EWorldType::PIE;
	auto* SeamlessSetting=IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"));
	const int32 PreviousSetting=SeamlessSetting ? SeamlessSetting->GetInt() : 0;
	ON_SCOPE_EXIT { if(SeamlessSetting) SeamlessSetting->SetWithCurrentPriority(PreviousSetting); };
	if(SeamlessSetting) SeamlessSetting->SetWithCurrentPriority(0);
#endif
	auto* Instance=NewObject<UProjectProject01GameInstance>();World->SetGameInstance(Instance);
	auto* Previous=World->SpawnActor<AMultiplayTestGameMode>();
	Previous->AuthoritativeMatchId=TEXT("review-match");Previous->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::InProgress;
	Previous->MatchStartWorldSeconds=-100.0;
	TArray<AMultiplayTestPlayerController*> Players;
	for(int32 Index=0;Index<3;++Index)
	{
		auto* PC=World->SpawnActor<AMultiplayTestPlayerController>();Players.Add(PC);
		PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>(); PC->PlayerCameraManager->InitializeFor(PC);
		FProjectProject01ValidatedJoinClaim Claim;Claim.UserId=FString::FromInt(Index+1);Claim.MatchId=TEXT("review-match");
		Claim.Role=Index==0?TEXT("Mannequin"):TEXT("Survivor");PC->SetAuthenticatedLobbyIdentity(Claim);
		FMultiplayTestServerPlayerRecord Record;Record.UserId=Claim.UserId;Record.MatchId=Claim.MatchId;Record.Role=Claim.Role;
		Record.SurvivorState=Index==0?EMultiplayTestSurvivorState::NotApplicable:
			Index==1?EMultiplayTestSurvivorState::TransitionWaiting:EMultiplayTestSurvivorState::Eliminated;
		Record.bForfeited=Index==2 && bForfeit;
		Record.CaptureCount=2;Record.RescueCount=3;Record.FirstCaptureSeconds=25;
		Previous->MatchPlayerRecords.Add(PC,Record);
	}
	FMultiplayTestServerPlayerRecord Left;Left.UserId=TEXT("left");Left.Role=TEXT("Survivor");Left.MatchId=TEXT("review-match");Left.bForfeited=true;Left.SurvivorState=EMultiplayTestSurvivorState::Eliminated;
	Previous->DisconnectedMatchRecords.Add(Left.UserId,Left);
	Players[1]->SurvivorSpectatorView.bEnabled=true; // A living survivor waiting at the trigger, not permanently dead.
	Players[2]->SurvivorSpectatorView.bEnabled=bDeathBeforeWaiting; // Or the death handoff timer never ran before travel.
	// A mannequin camera already exists in the DEPARTING world. This is the
	// case the earlier arrival-only fixture missed while the GameMode kept ticking.
	auto* OldMannequin=World->SpawnActor<AMannequinAICharacter>();
	Players[0]->ViewedMannequin=OldMannequin;
#if WITH_EDITOR
	// Reproduce ticket-free PIE: the server match record exists but the ticket identity is empty.
	Players[0]->SetAuthenticatedLobbyIdentity(FProjectProject01ValidatedJoinClaim());
#endif
	Previous->PrepareLevelTransitionTravel();
	const uint32 DepartureSerial=Players[0]->TransitionFadeSerial;
	for (int32 Frame=0;Frame<10;++Frame)
	{
		Previous->FinalizeLevelTransitionArrivals();
		Previous->PrepareLevelTransitionTravel();
	}
	TestTrue(TEXT("Old-map arrival ticks cannot consume the mannequin's departure flags"),Players[0]->IsLevelTransitionCarryover());
	TestEqual(TEXT("Old-map ticks cannot restart the mannequin's blackout serial"),Players[0]->TransitionFadeSerial,DepartureSerial);
	for (auto* PC:Players) PC->ServerConfirmLevelTransitionBlack_Implementation(PC->TransitionFadeSerial);
	Previous->FinalizeLevelTransitionArrivals();
	TestTrue(TEXT("The complete blackout barrier survives old-map ticks until travel"),Previous->AreLevelTransitionClientsBlack());
	TestTrue(TEXT("Travel snapshot includes connected dead spectators, not departed players"),Instance->ServerMatchCarryover&&Instance->ServerMatchCarryover->Players.Num()==3);
	TestFalse(TEXT("Dead or forfeited survivor cannot spawn after travel"),Players[2]->ShouldSpawnAfterLevelTransition());
	TestTrue(TEXT("Only the advancing survivor is marked for a new spawn"),Players[1]->ShouldSpawnAfterLevelTransition());
	auto* LateExit=World->SpawnActor<AMultiplayTestPlayerController>();
	LateExit->PrepareLevelTransitionCarryover(true);
	LateExit->PrepareLevelTransitionCarryover(false);
	LateExit->PrepareLevelTransitionCarryover(true);
	TestFalse(TEXT("Repeated preparation cannot undo a late death/forfeit spawn veto"),LateExit->ShouldSpawnAfterLevelTransition());
#if WITH_EDITOR
	TestTrue(TEXT("PIE enables seamless travel instead of silently starting a fresh match"),SeamlessSetting && SeamlessSetting->GetInt()==1);
	TestEqual(TEXT("PIE mannequin identity and role survive the next GameMode"),Players[0]->GetAuthenticatedRole(),FString(TEXT("Mannequin")));
#endif
	Players[0]->ViewedMannequin=nullptr; // Old-map actor references are not destination-ready.
	auto* Next=World->SpawnActor<AMultiplayTestGameMode>();
	for(auto* PC:Players)Next->RegisterMatchPlayer(PC);
	TestEqual(TEXT("Next map does not wait for departed controllers"),Next->ExpectedMatchPlayers,3);
	TestTrue(TEXT("Dead survivor state remains eliminated in the destination"),Next->MatchPlayerRecords.FindChecked(Players[2]).SurvivorState==EMultiplayTestSurvivorState::Eliminated);
	TestEqual(TEXT("Forfeit flag is not erased by travel"),Next->MatchPlayerRecords.FindChecked(Players[2]).bForfeited,bForfeit);
	Next->HandleStartingNewPlayer_Implementation(Players[2]);
	TestNull(TEXT("Destination starting-player path does not respawn a dead spectator"),Players[2]->GetPawn());
	TestEqual(TEXT("Capture total survives a map change"),Next->MatchPlayerRecords.FindChecked(Players[0]).CaptureCount,2);
	TestEqual(TEXT("Rescue total survives a map change"),Next->MatchPlayerRecords.FindChecked(Players[1]).RescueCount,3);
	TestEqual(TEXT("First capture time remains match-wide"),Next->MatchPlayerRecords.FindChecked(Players[0]).FirstCaptureSeconds,25.0);
	Next->TryStartAuthoritativeMatch();
	TestTrue(TEXT("Transition with a dead spectator reaches InProgress"),Next->AuthoritativeMatchPhase==EMultiplayTestMatchPhase::InProgress);
	TestTrue(TEXT("Missing new Pawn keeps camera handoff pending instead of consuming it with a null target"),Players[1]->IsLevelTransitionCarryover() && Players[2]->IsLevelTransitionCarryover());
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* LivePawn=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(2000,0,100)),Spawn);
	Players[1]->Possess(LivePawn);
	auto* Mannequin=World->SpawnActor<AMannequinAICharacter>();
	Players[0]->SetViewedMannequin(Mannequin);
	Next->FinalizeLevelTransitionArrivals();
	TestTrue(TEXT("Previously waiting survivor now owns and views its new playable Pawn"),Players[1]->GetPawn()==LivePawn && Players[1]->GetViewTarget()==LivePawn && !Players[1]->SurvivorSpectatorView.bEnabled);
	TestTrue(TEXT("Dead survivor watches the new-map living Pawn in either death/wait order"),Players[2]->GetViewTarget()==LivePawn && Players[2]->SurvivorSpectatorView.bEnabled && Players[2]->SurvivorSpectatorView.Target==LivePawn);
	Players[2]->SetViewTarget(Players[2]);
	Players[2]->OnRep_Pawn();
	TestTrue(TEXT("Pawn replication cannot overwrite the persisted dead spectator camera"),Players[2]->GetViewTarget()==LivePawn);
	Players[2]->EnterSurvivorSpectator(Mannequin,false);
	TestFalse(TEXT("A mannequin can never become the survivor spectator target"),Players[2]->GetViewTarget()==Mannequin);
	Next->RefreshSurvivorSpectatorTargets();
	TestTrue(TEXT("Server repairs an invalid spectator target"),Players[2]->GetViewTarget()==LivePawn);
	LivePawn->SetActorHiddenInGame(true);
	auto* Replacement=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(2200,0,100)),Spawn);
	Players[1]->Possess(Replacement);
	Next->MatchPlayerRecords.FindChecked(Players[1]).SurvivorPawn=LivePawn; // Deliberately stale snapshot.
	Next->RefreshSurvivorSpectatorTargets();
	TestTrue(TEXT("Spectating follows current possession, never the stale previous Pawn snapshot"),Players[2]->GetViewTarget()==Replacement);
	Next->HandleStartingNewPlayer_Implementation(Players[2]);
	TestNull(TEXT("Consumed carryover flags cannot enable a later dead-player restart"),Players[2]->GetPawn());
	TestTrue(TEXT("Result timer includes the previous map"),Next->GetElapsedMatchSeconds()>=100.0);
	TestTrue(TEXT("Departed survivor record is preserved without increasing arrival count"),Next->DisconnectedMatchRecords.Contains(TEXT("left")));
	for(int32 Index=0;Index<Players.Num();++Index)
	{
		auto* PC=Players[Index];
		TestFalse(TEXT("Arrival clears carryover and restores looking"),PC->IsLevelTransitionCarryover()||PC->IsLookInputIgnored());
		TestEqual(TEXT("Only the eliminated spectator keeps movement disabled"),PC->IsMoveInputIgnored(),Index==2);
	}
	}
	// Client-side replication order without a viewport, backend or real network.
	{
	UWorld* ClientWorld=CreateReviewWorld(); ON_SCOPE_EXIT { ClientWorld->DestroyWorld(false); };
	ClientWorld->SetGameInstance(NewObject<UGameInstance>());
	auto* DeadClient=ClientWorld->SpawnActor<AMultiplayTestPlayerController>(); DeadClient->SetAsLocalPlayerController();
	DeadClient->PlayerCameraManager=ClientWorld->SpawnActor<APlayerCameraManager>(); DeadClient->PlayerCameraManager->InitializeFor(DeadClient);
	DeadClient->SurvivorState=EMultiplayTestSurvivorState::Eliminated;
	DeadClient->bLocalLevelTransitionFadeHeld=true;
	DeadClient->ClientCompleteLevelTransition_Implementation(nullptr,false);
	TestTrue(TEXT("Unmapped spectator target keeps the client black and pending"),DeadClient->bClientArrivalViewPending && DeadClient->bLocalLevelTransitionFadeHeld);
	auto* NewLivingPawn=ClientWorld->SpawnActor<APlayerCharacter>();
	DeadClient->SurvivorSpectatorView.Target=NewLivingPawn;
	DeadClient->OnRep_SurvivorSpectatorView();
	TestTrue(TEXT("A mapped actor alone cannot reveal a world that has not begun play"),DeadClient->bClientArrivalViewPending);
	ClientWorld->SetBegunPlay(true);
	for (int32 Frame=0;Frame<5;++Frame) DeadClient->TryCompleteClientArrivalView(0.05f);
	TestTrue(TEXT("Late target replication completes dead-client spectating"),!DeadClient->bClientArrivalViewPending && !DeadClient->bLocalLevelTransitionFadeHeld && DeadClient->GetViewTarget()==NewLivingPawn && DeadClient->IsMoveInputIgnored());
	DeadClient->ClientCompleteLevelTransition_Implementation(nullptr,false);
	for (int32 Frame=0;Frame<5;++Frame) DeadClient->TryCompleteClientArrivalView(0.05f);
	TestTrue(TEXT("A null RPC parameter cannot erase an already-replicated spectator target"),!DeadClient->bClientArrivalViewPending && DeadClient->GetViewTarget()==NewLivingPawn);
	auto* LiveClient=ClientWorld->SpawnActor<AMultiplayTestPlayerController>(); LiveClient->SetAsLocalPlayerController();
	LiveClient->PlayerCameraManager=ClientWorld->SpawnActor<APlayerCameraManager>(); LiveClient->PlayerCameraManager->InitializeFor(LiveClient);
	LiveClient->SurvivorState=EMultiplayTestSurvivorState::Active;
	LiveClient->SurvivorSpectatorView.bEnabled=true; LiveClient->SurvivorSpectatorView.Target=NewLivingPawn;
	LiveClient->bLocalLevelTransitionFadeHeld=true;
	LiveClient->ClientCompleteLevelTransition_Implementation(nullptr,true);
	TestTrue(TEXT("Living arrival waits for its own Pawn, not the old observed player"),LiveClient->bClientArrivalViewPending && !LiveClient->SurvivorSpectatorView.bEnabled);
	auto* OwnPawn=ClientWorld->SpawnActor<APlayerCharacter>(); LiveClient->SetPawn(OwnPawn);
	LiveClient->TryCompleteClientArrivalView();
	TestTrue(TEXT("Arrival needs stable ready frames rather than an instantaneous Pawn notification"),LiveClient->bClientArrivalViewPending);
	for (int32 Frame=0;Frame<5;++Frame) LiveClient->TryCompleteClientArrivalView(0.05f);
	TestTrue(TEXT("Late own-Pawn replication restores direct play and self camera"),!LiveClient->bClientArrivalViewPending && !LiveClient->IsMoveInputIgnored() && !LiveClient->SurvivorSpectatorView.bEnabled && LiveClient->GetViewTarget()==OwnPawn);
	ClientWorld->SetBegunPlay(false);
	auto* MannequinClient=ClientWorld->SpawnActor<AMultiplayTestPlayerController>(); MannequinClient->SetAsLocalPlayerController();
	MannequinClient->PlayerCameraManager=ClientWorld->SpawnActor<APlayerCameraManager>(); MannequinClient->PlayerCameraManager->InitializeFor(MannequinClient);
	MannequinClient->bLocalMultiplayerMannequinRole=true; MannequinClient->bLocalLevelTransitionFadeHeld=true;
	MannequinClient->ClientCompleteLevelTransition_Implementation(nullptr,true);
	TestTrue(TEXT("Mannequin stays black while its new view is unmapped"),MannequinClient->bClientArrivalViewPending && MannequinClient->bLocalLevelTransitionFadeHeld);
	auto* NewMannequin=ClientWorld->SpawnActor<AMannequinAICharacter>(); MannequinClient->ViewedMannequin=NewMannequin;
	ClientWorld->SetBegunPlay(true);
	for (int32 Notify=0;Notify<10;++Notify) MannequinClient->TryCompleteClientArrivalView();
	TestTrue(TEXT("Repeated mannequin notifications in one frame cannot consume readiness time"),MannequinClient->bClientArrivalViewPending);
	for (int32 Frame=0;Frame<5;++Frame) MannequinClient->TryCompleteClientArrivalView(0.05f);
	TestTrue(TEXT("Ready mannequin switches to its new body before reveal"),!MannequinClient->bClientArrivalViewPending && !MannequinClient->bLocalLevelTransitionFadeHeld && MannequinClient->GetViewTarget()==NewMannequin);
	// This fixture toggles readiness directly; restore it before destroying the synthetic world.
	ClientWorld->SetBegunPlay(false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReviewTravelRollbackTest,"ProjectProject01.Review.TravelRollback",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReviewTravelRollbackTest::RunTest(const FString&)
{
	UWorld* World=CreateReviewWorld();ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Instance=NewObject<UProjectProject01GameInstance>();World->SetGameInstance(Instance);
	auto* Mode=World->SpawnActor<AMultiplayTestGameMode>();
	Mode->AuthoritativeMatchId=TEXT("rollback-match");Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::InProgress;
	Mode->TimelineLog=MakeShared<FProjectMatchTimelineLog>();Mode->TimelineLog->PreviousLevelsSeconds=40;
	Mode->TimelineLevelStartWorldSeconds=-20;
	auto* PC=World->SpawnActor<AMultiplayTestPlayerController>();
	PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();PC->PlayerCameraManager->InitializeFor(PC);
	auto* Survivor=World->SpawnActor<APlayerCharacter>();PC->Possess(Survivor);PC->SetViewTarget(Survivor);
	Survivor->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	FMultiplayTestServerPlayerRecord Record;Record.UserId=TEXT("survivor");Record.Role=TEXT("Survivor");Record.MatchId=TEXT("rollback-match");
	Record.SurvivorPawn=Survivor;Record.SurvivorState=EMultiplayTestSurvivorState::Active;Record.RescueCount=4;
	Mode->MatchPlayerRecords.Add(PC,Record);
	TestTrue(TEXT("Survivor can enter transition waiting"),Mode->MarkSurvivorWaitingForLevelTransition(Survivor));
	Mode->PrepareLevelTransitionTravel();Mode->PrepareLevelTransitionTravel();
	TestTrue(TEXT("Waiting survivor is hidden, locked and snapshotted"),Survivor->IsHidden()&&PC->IsMoveInputIgnored()&&Instance->ServerMatchCarryover.IsValid());
	Mode->CancelLevelTransitionTravel();
	TestFalse(TEXT("Rollback clears departure-only arrival suppression"),Mode->bLevelTransitionDeparting);
	TestTrue(TEXT("Rejected travel restores movement, visibility and collision"),!Survivor->IsHidden()&&Survivor->GetActorEnableCollision()&&Survivor->GetCharacterMovement()->MovementMode==MOVE_Walking);
	TestTrue(TEXT("Rejected travel restores original pawn view"),PC->GetViewTarget()==Survivor);
	TestFalse(TEXT("Rejected travel clears stacked movement locks"),PC->IsMoveInputIgnored());
	TestFalse(TEXT("Rejected travel clears stacked look locks"),PC->IsLookInputIgnored());
	TestFalse(TEXT("Rejected travel clears carryover flag"),PC->IsLevelTransitionCarryover());
	TestTrue(TEXT("No stale match carryover survives rollback"),!Instance->ServerMatchCarryover.IsValid());
	TestEqual(TEXT("Timeline is resumed without counting old-map duration twice"),Mode->GetTimelineElapsedSeconds(),60.0);
	TestEqual(TEXT("Original match statistics survive rollback"),Mode->MatchPlayerRecords.FindChecked(PC).RescueCount,4);
	TestTrue(TEXT("Survivor is active again"),Mode->MatchPlayerRecords.FindChecked(PC).SurvivorState==EMultiplayTestSurvivorState::Active);
	Mode->CancelLevelTransitionTravel();
	TestFalse(TEXT("Repeated cancellation remains input-unlocked"),PC->IsMoveInputIgnored());
	// Failure after a result must never resurrect an eliminated survivor.
	Mode->MarkSurvivorWaitingForLevelTransition(Survivor);Mode->PrepareLevelTransitionTravel();
	Mode->MatchPlayerRecords.FindChecked(PC).SurvivorState=EMultiplayTestSurvivorState::Eliminated;
	Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::Results;Mode->CancelLevelTransitionTravel();
	TestTrue(TEXT("Late failure cannot resurrect a finished match"),Survivor->IsHidden()&&Mode->MatchPlayerRecords.FindChecked(PC).SurvivorState==EMultiplayTestSurvivorState::Eliminated);

	auto* Trigger=World->SpawnActor<AProjectProject01LevelTransitionTrigger>();
	TestFalse(TEXT("Unset destination cannot accept players"),Trigger->IsDestinationAvailable());
	Trigger->DestinationLevel=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/ReviewMissingMap.ReviewMissingMap")));
	TestFalse(TEXT("Unavailable or uncooked map cannot accept players"),Trigger->IsDestinationAvailable());
	PC->ResetIgnoreMoveInput();PC->ResetIgnoreLookInput();
	Trigger->SingleTransitionController=PC;Trigger->bSingleMoveIgnored=false;Trigger->bSingleLookIgnored=false;
	PC->SetIgnoreMoveInput(true);PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);
	Trigger->bTransitionStarted=true;Instance->MarkPendingSinglePlayerLevelTransitionFade();
	AddExpectedMessage(TEXT("No level replacement within the 30-second travel deadline"),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1);
	Trigger->HandleTravelTimeout();
	TestTrue(TEXT("Single-player timeout restores input and rearms the trigger"),!PC->IsMoveInputIgnored()&&!PC->IsLookInputIgnored()&&!Trigger->bTransitionStarted&&Trigger->TransitionVolume->GetCollisionEnabled()==ECollisionEnabled::QueryOnly);
	TestFalse(TEXT("Cancelled single travel cannot leak fade flag into a later game"),Instance->ConsumePendingSinglePlayerLevelTransitionFade());
    TestTrue(TEXT("Long async travel gets bounded slow-disk grace"),Trigger->ExtendAsyncTravelGrace(true)&&Trigger->ExtendAsyncTravelGrace(true)&&Trigger->ExtendAsyncTravelGrace(true));
    TestFalse(TEXT("Fourth deadline terminates async waiting rather than hanging forever"),Trigger->ExtendAsyncTravelGrace(true));
    TestFalse(TEXT("Non-async travel failure has no extra grace"),Trigger->ExtendAsyncTravelGrace(false));
    TestTrue(TEXT("GameInstance watchdog survives the old trigger's world being replaced during async load"),FProjectTravelDeadline::Evaluate(121,true,true)==EProjectTravelDeadline::Abort);
    TestTrue(TEXT("Completed new world cancels the watchdog"),FProjectTravelDeadline::Evaluate(121,false,true)==EProjectTravelDeadline::Complete);
    TestTrue(TEXT("Slow but bounded disk load does not abort early"),FProjectTravelDeadline::Evaluate(119,true,true)==EProjectTravelDeadline::Wait);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReviewCheckpointStoreTest,"ProjectProject01.Review.CheckpointStore",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReviewCheckpointStoreTest::RunTest(const FString&)
{
	auto* Instance=NewObject<UProjectProject01GameInstance>();
	auto* Store=NewObject<UProjectProject01SaveSubsystem>(Instance);
	Store->TestSlotNamespace=FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Base=Store->GetPrimarySlotName(1);
	ON_SCOPE_EXIT { for(int32 Index=1;Index<=3;++Index)Store->DeleteSaveSlot(Index); };
	auto MakeSave=[](int64 Revision,int32 Day)
	{
		auto* Save=NewObject<UProjectProject01SinglePlayerSaveGame>();
		Save->SavedMapPackageName=TEXT("/Game/ReviewCheckpointFixture");Save->CheckpointId=FName(*FString::Printf(TEXT("Checkpoint_%lld"),Revision));
		Save->SavedAtUtc=FDateTime(2026,10,Day);Save->LastPlayedAtUtc=Save->SavedAtUtc;Save->SaveRevision=Revision;
		Save->IntegrityHash=Save->CalculateIntegrityHash();return Save;
	};
	auto Bytes=[](const FString& Slot)
	{
		TArray<uint8> Data;UGameplayStatics::LoadDataFromSlot(Data,Slot,0);return Data;
	};
	auto Write=[](USaveGame* Save,const FString& Slot)
	{
		TArray<uint8> Data;return ProjectCheckpointStore::Encode(Save,Data)&&UGameplayStatics::SaveDataToSlot(Data,Slot,0);
	};
	auto Current=[&]()
	{
		return ProjectCheckpointStore::SelectNewest(Base,[&](const FString& Slot){FString Error;return Store->LoadValidatedSlot(Slot,Error);});
	};
	auto Complete=[&](const FString& Slot,const UProjectProject01SinglePlayerSaveGame* Save,bool bSucceeded)
	{
		Store->bSaveInProgress=true;Store->PendingWriteSlot=Slot;
		Store->PendingSaveRevision=Save->SaveRevision;Store->PendingSaveHash=Save->IntegrityHash;
		Store->HandleAsyncSaveComplete(Slot,0,bSucceeded);
		return !Store->bSaveInProgress;
	};
	// All filesystem access is confined to this run's random test slot namespace.
	auto* Legacy=MakeSave(0,15);Legacy->SchemaVersion=5;
	Legacy->PreviewPng={1,2,3};Legacy->IntegrityHash=Legacy->CalculateIntegrityHash();
	TestTrue(TEXT("Create legacy v5 fixture"),UGameplayStatics::SaveGameToSlot(Legacy,Base,0));
	const auto Original=Bytes(Base);
	auto Selection=Current();TestNotNull(TEXT("Legacy primary remains readable"),Selection.Save);
	if(!Selection.Save)return false;
	TestTrue(TEXT("v5 migration retains optional preview bytes"),Selection.Save->PreviewPng==Legacy->PreviewPng);
	const FString A=ProjectCheckpointStore::NextWriteSlot(Base,Selection);
	TestTrue(TEXT("Upgrade never targets the only legacy save"),A!=Base);
	auto* First=MakeSave(1,14);
	First->PreviewPng={1,2,3};First->PreviewCrc=FCrc::MemCrc32(First->PreviewPng.GetData(),First->PreviewPng.Num());
	TestTrue(TEXT("Failed write callback terminates the operation"),Complete(A,First,false));
	TestTrue(TEXT("Failed write leaves original bytes unchanged"),Bytes(Base)==Original);
	TestEqual(TEXT("Previous snapshot survives failed write"),Current().Save->SaveRevision,int64(0));
	TestTrue(TEXT("First new generation writes beside legacy save"),Write(First,A));
	TestTrue(TEXT("Read-back validation completes generation commit"),Complete(A,First,true));
	Selection=Current();TestEqual(TEXT("Generation order wins over a backwards system clock"),Selection.Save->SaveRevision,int64(1));
	const auto FirstBytes=Bytes(A);
	const FString B=ProjectCheckpointStore::NextWriteSlot(Base,Selection);
	TestTrue(TEXT("Next write never overwrites current known-good generation"),B!=A&&B!=Base);
	auto* Damaged=MakeSave(2,13);Damaged->RemainingDeathCount=1; // invalidate its checksum
	TestTrue(TEXT("Write interrupted/corrupt generation fixture"),Write(Damaged,B));
	TestTrue(TEXT("Corrupt read-back releases the busy flag"),Complete(B,Damaged,true));
	TestEqual(TEXT("Corrupt next generation is never selected"),Current().Save->SaveRevision,int64(1));
	TestTrue(TEXT("Last known-good generation was never rewritten"),Bytes(A)==FirstBytes);
	auto* Second=MakeSave(2,13);TestTrue(TEXT("Retry commits inactive generation"),Write(Second,B));
	Complete(B,Second,true);const auto SecondBytes=Bytes(B);
	TestEqual(TEXT("Newest valid generation selected"),Current().Save->SaveRevision,int64(2));
	TArray<uint8> Truncated=SecondBytes;Truncated.SetNum(12);UGameplayStatics::SaveDataToSlot(Truncated,B,0);
	TestEqual(TEXT("Torn header is rejected before UObject deserialization"),Current().Save->SaveRevision,int64(1));
	TArray<uint8> BitFlipped=SecondBytes;BitFlipped[32]^=0x80;UGameplayStatics::SaveDataToSlot(BitFlipped,B,0);
	TestEqual(TEXT("Corrupt world bytes are rejected before UObject deserialization"),Current().Save->SaveRevision,int64(1));
	TArray<uint8> ImageDamage=FirstBytes;ImageDamage.Last()^=0x01;
	auto* ImageOnly=Cast<UProjectProject01SinglePlayerSaveGame>(ProjectCheckpointStore::Decode(ImageDamage));
	TestTrue(TEXT("Image-only corruption does not invalidate world state"),ImageOnly&&ImageOnly->HasValidIntegrity()&&ImageOnly->PreviewCrc!=FCrc::MemCrc32(ImageOnly->PreviewPng.GetData(),ImageOnly->PreviewPng.Num()));
	UGameplayStatics::SaveDataToSlot(SecondBytes,B,0);
	TestTrue(TEXT("Load recency persists independently"),Store->RecordLastPlayed(1,Current().Save));
	TestTrue(TEXT("Recency updates never rewrite any world snapshot"),Bytes(A)==FirstBytes&&Bytes(B)==SecondBytes&&Bytes(Base)==Original);
	TestTrue(TEXT("Slot list uses separate last-played metadata"),Store->GetSaveSlotInfo(1).LastPlayedAtUtc>Second->LastPlayedAtUtc);
	auto* Restarted=NewObject<UProjectProject01SaveSubsystem>(Instance);Restarted->TestSlotNamespace=Store->TestSlotNamespace;
	TestEqual(TEXT("Fresh store instance recovers without an in-memory commit pointer"),Restarted->GetSaveSlotInfo(1).CheckpointId,Second->CheckpointId);
	Second->RemainingDeathCount=0;Write(Second,B);
	TestEqual(TEXT("Damage to latest generation falls back to previous one"),Restarted->GetSaveSlotInfo(1).CheckpointId,First->CheckpointId);
	TestEqual(TEXT("Newer snapshot's recency cannot contaminate fallback"),Restarted->GetSaveSlotInfo(1).LastPlayedAtUtc,First->LastPlayedAtUtc);
	auto* Future=MakeSave(100,16);Future->SchemaVersion=UProjectProject01SinglePlayerSaveGame::CurrentSchemaVersion+1;Future->IntegrityHash=Future->CalculateIntegrityHash();
	Write(Future,B);
	TestEqual(TEXT("Future schema generation cannot displace supported snapshot"),Current().Save->SaveRevision,int64(1));
	auto* UncommittedLegacy=MakeSave(1000,17);UGameplayStatics::SaveGameToSlot(UncommittedLegacy,Base+TEXT("_Pending"),0);
	TestEqual(TEXT("Old implementation's uncommitted pending file is ignored"),Current().Save->SaveRevision,int64(1));
	First->SaveRevision=999;TestFalse(TEXT("Generation number is covered by integrity hash"),First->HasValidIntegrity());
	Store->RecordLastPlayed(1,Current().Save);
	auto* Recency=Cast<UProjectProject01CheckpointRecency>(ProjectCheckpointStore::Decode(Bytes(Base+TEXT("_Recent"))));
	TestNotNull(TEXT("Recency fixture exists"),Recency);if(!Recency)return false;
	Recency->Checksum++;Write(Recency,Base+TEXT("_Recent"));
	TestEqual(TEXT("Damaged recency falls back to snapshot metadata"),Store->GetSaveSlotInfo(1).LastPlayedAtUtc,First->LastPlayedAtUtc);
	TestTrue(TEXT("Delete removes legacy, generations and metadata together"),Store->DeleteSaveSlot(1));
	for(const auto& Slot:ProjectCheckpointStore::AllSlots(Base))TestFalse(TEXT("No deleted slot can resurrect from a sidecar"),UGameplayStatics::DoesSaveGameExist(Slot,0));
	TestFalse(TEXT("Deleted slot is not offered as Continue"),Store->HasAnyRecoverableSave());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReviewRoleGuideKeysTest,"ProjectProject01.Review.RoleGuideKeys",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReviewRoleGuideKeysTest::RunTest(const FString&)
{
	auto* Settings=UProjectProject01GameUserSettings::Get();
	const FKey OldForward=Settings->GetMoveForwardKey(),OldLeft=Settings->GetMoveLeftKey(),OldBack=Settings->GetMoveBackwardKey(),OldRight=Settings->GetMoveRightKey();
	ON_SCOPE_EXIT {Settings->SetMoveForwardKey(OldForward);Settings->SetMoveLeftKey(OldLeft);Settings->SetMoveBackwardKey(OldBack);Settings->SetMoveRightKey(OldRight);};
	Settings->SetMoveForwardKey(EKeys::NumPadEight);Settings->SetMoveLeftKey(EKeys::NumPadFour);
	Settings->SetMoveBackwardKey(EKeys::NumPadTwo);Settings->SetMoveRightKey(EKeys::NumPadSix);
	auto* Guide=NewObject<UProjectProject01GameGuideWidget>();
	Guide->TitleText=NewObject<UTextBlock>(Guide);Guide->GoalText=NewObject<UTextBlock>(Guide);Guide->ControlsText=NewObject<UTextBlock>(Guide);
	for(bool bMannequin : {false,true})
	{
		Guide->bMannequinRole=bMannequin;Guide->RefreshGuideText();
		const FString Text=Guide->ControlsText->GetText().ToString();
		TestFalse(TEXT("Role guide never retains hard-coded WASD after rebinding"),Text.Contains(TEXT("W/A/S/D")));
		for(const auto& Key:{EKeys::NumPadEight,EKeys::NumPadFour,EKeys::NumPadTwo,EKeys::NumPadSix})
			TestTrue(TEXT("Guide displays every configured movement key"),Text.Contains(Key.GetDisplayName().ToString()));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReviewSingleAIStateTest,"ProjectProject01.Review.SingleAIState",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReviewSingleAIStateTest::RunTest(const FString&)
{
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(true).ShouldSimulatePhysics(false);
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* BBAsset=NewObject<UBlackboardData>();
	for(const FName Key:{FName(TEXT("PlayerLookingAtMannequin")),FName(TEXT("RoamingSequenceCompleted")),FName(TEXT("IsOutsideOuterRange")),FName(TEXT("IsBetweenPlayerRanges")),FName(TEXT("IsInsideInnerRange")),FName(TEXT("IsInChaseSector")),FName(TEXT("IsInRoamingSector"))})
	{FBlackboardEntry Entry;Entry.EntryName=Key;Entry.KeyType=NewObject<UBlackboardKeyType_Bool>(BBAsset);BBAsset->Keys.Add(Entry);}
	for(const FName Key:{FName(TEXT("RoamingLocation")),FName(TEXT("PendingMoveLocation"))})
	{FBlackboardEntry Entry;Entry.EntryName=Key;Entry.KeyType=NewObject<UBlackboardKeyType_Vector>(BBAsset);BBAsset->Keys.Add(Entry);}
	FBlackboardEntry Target;Target.EntryName=TEXT("TargetActor");Target.KeyType=NewObject<UBlackboardKeyType_Object>(BBAsset);BBAsset->Keys.Add(Target);
	auto* Asset=NewObject<UBehaviorTree>();Asset->BlackboardAsset=BBAsset;
	auto* Root=NewObject<UBTComposite_Sequence>(Asset);Asset->RootNode=Root;
	auto* Template=NewObject<UBTService_MannequinAI>(Root);Root->Services.Add(Template);
	auto* WaitTemplate=NewObject<UBTTask_Wait>(Root);FBTCompositeChild Child;Child.ChildTask=WaitTemplate;Root->Children.Add(Child);
	auto* OldPawn=World->SpawnActor<AMannequinAICharacter>();auto* NewPawn=World->SpawnActor<AMannequinAICharacter>();
	auto* OldAI=World->SpawnActor<AMannequinAIController>();auto* NewAI=World->SpawnActor<AMannequinAIController>();
	OldAI->Possess(OldPawn);NewAI->Possess(NewPawn);
	TestTrue(TEXT("Real BT starts for source actor"),OldAI->RunBehaviorTree(Asset));
	TestTrue(TEXT("Real BT starts for restored actor"),NewAI->RunBehaviorTree(Asset));
	auto* OldTree=Cast<UBehaviorTreeComponent>(OldAI->GetBrainComponent());auto* NewTree=Cast<UBehaviorTreeComponent>(NewAI->GetBrainComponent());
	if(!OldTree||!NewTree)return false;
	OldTree->TickComponent(0.01f,LEVELTICK_All,nullptr);NewTree->TickComponent(0.01f,LEVELTICK_All,nullptr);
	auto Service=[](UBehaviorTreeComponent* Tree)
	{
		TArray<UObject*> Children;GetObjectsWithOuter(Tree,Children,EGetObjectsFlags::None);
		for(auto* Object:Children)if(auto* Result=Cast<UBTService_MannequinAI>(Object))return Result;
		return static_cast<UBTService_MannequinAI*>(nullptr);
	};
	auto* OldService=Service(OldTree);auto* NewService=Service(NewTree);
	TestTrue(TEXT("BT services are per-controller instances, not the shared template"),OldService&&NewService&&OldService!=NewService&&OldService!=Template);
	if(!OldService||!NewService)return false;
	OldService->RoamingDestination=FVector(100,200,0);OldService->bHasRoamingDestination=true;OldService->bRoamingCommitted=true;
	OldService->ActiveRangeRegion=EMannequinRangeRegion::InsideInnerRange;OldService->PendingRangeRegion=EMannequinRangeRegion::BetweenRangesOuterRoamingArea;
	OldService->bHasPendingOutwardTransition=true;OldService->bPendingTransitionUsesCommittedLocation=true;OldService->PendingMoveLocation=FVector(300,400,0);
	OldService->NextRoamingQueryTime=100.75;
	TestTrue(TEXT("Service value snapshot restores"),FProjectCheckpointAI::RestoreService(NewService,FProjectCheckpointAI::CaptureService(OldService,100.0),2.0));
	TestEqual(TEXT("Repath delay is rebased to new world time, not old absolute time"),NewService->NextRoamingQueryTime,2.75);
	OldPawn->ActivePostPossessionCommand=AMannequinAICharacter::EPostPossessionCommand::ChaseNearestSurvivor;
	OldPawn->bPostPossessionChaseCommandQueued=true;OldPawn->SetFrozen(true);
	auto* OldPlayer=World->SpawnActor<APlayerCharacter>();auto* NewPlayer=World->SpawnActor<APlayerCharacter>();
	OldPawn->CaughtSurvivorsInCurrentContact.Add(OldPlayer);
	auto* OldBB=OldAI->GetBlackboardComponent();OldBB->SetValueAsObject(TEXT("TargetActor"),OldPlayer);
	OldBB->SetValueAsBool(TEXT("IsInRoamingSector"),true);OldBB->SetValueAsVector(TEXT("RoamingLocation"),OldService->RoamingDestination);
	OldBB->SetValueAsVector(TEXT("PendingMoveLocation"),OldService->PendingMoveLocation);
	if(const auto* Wait=Cast<UBTTask_Wait>(OldTree->GetActiveNode()))
		Wait->GetSpecialNodeMemory<FBTTaskMemory>(OldTree->GetNodeMemory(Wait,OldTree->GetActiveInstanceIdx()))->NextTickRemainingTime=2.5f;
	else {AddError(TEXT("Wait task did not activate in checkpoint BT fixture"));return false;}
	const auto Data=FProjectCheckpointAI::Capture(OldPawn,[&](AActor* Actor){return Actor==OldPlayer?FName(TEXT("player")):NAME_None;});
	TestTrue(TEXT("AI snapshot restores after actors have been remapped"),FProjectCheckpointAI::Restore(NewPawn,Data,[&](FName Id)->AActor*{return Id==TEXT("player")?NewPlayer:nullptr;}));
	TestTrue(TEXT("Command, queued command and frozen state restored"),NewPawn->ShouldChasePostPossessionCommand()&&NewPawn->bPostPossessionChaseCommandQueued&&NewPawn->bLegacyAnimationFrozen);
	TestTrue(TEXT("Distributed movement and pending outward transition persist"),NewService->bRoamingCommitted&&NewService->bHasPendingOutwardTransition&&NewService->RoamingDestination.Equals(FVector(100,200,0))&&NewService->PendingMoveLocation.Equals(FVector(300,400,0)));
	TestTrue(TEXT("Blackboard target and contact use new-world player, never old pointer"),NewAI->GetBlackboardComponent()->GetValueAsObject(TEXT("TargetActor"))==NewPlayer&&NewPawn->CaughtSurvivorsInCurrentContact.Contains(NewPlayer));
	TestTrue(TEXT("Blackboard sector and movement destination restored"),NewAI->GetBlackboardComponent()->GetValueAsBool(TEXT("IsInRoamingSector"))&&NewAI->GetBlackboardComponent()->GetValueAsVector(TEXT("RoamingLocation")).Equals(FVector(100,200,0)));
	NewAI->Tick(0);TestEqual(TEXT("Active Wait task resumes its remaining duration"),NewAI->RestoredWaitNodeIndex,INDEX_NONE);
	if(const auto* Wait=Cast<UBTTask_Wait>(NewTree->GetActiveNode()))TestEqual(TEXT("Wait memory uses saved remaining seconds"),Wait->GetSpecialNodeMemory<FBTTaskMemory>(NewTree->GetNodeMemory(Wait,NewTree->GetActiveInstanceIdx()))->NextTickRemainingTime,2.5f);
	TestFalse(TEXT("Restoring instance state never writes the shared service template"),Template->bRoamingCommitted);
	auto* Helper=World->SpawnActor<AHelperRearGuardCharacter>();auto* HelperAI=World->SpawnActor<AHelperRearGuardAIController>();HelperAI->Possess(Helper);
	HelperAI->LastMoveTarget=FVector(700,0,0);HelperAI->bHasMoveTarget=true;HelperAI->bWaitingForRepath=true;HelperAI->WaitUntilTime=World->GetTimeSeconds()+0.4;
	const auto HelperData=FProjectCheckpointAI::Capture(Helper,[](AActor*){return NAME_None;});HelperAI->bHasMoveTarget=false;HelperAI->WaitUntilTime=0;
	TestTrue(TEXT("Helper navigation/wait snapshot restores"),FProjectCheckpointAI::Restore(Helper,HelperData,[](FName){return static_cast<AActor*>(nullptr);}));
	TestTrue(TEXT("Helper target and remaining repath wait survive"),HelperAI->bHasMoveTarget&&HelperAI->bWaitingForRepath&&HelperAI->LastMoveTarget.Equals(FVector(700,0,0))&&FMath::IsNearlyEqual(HelperAI->WaitUntilTime-World->GetTimeSeconds(),0.4));
	return true;
}
namespace
{
class FReviewResponse final:public IHttpResponse
{
public:
    FString Json,Url=TEXT("http://127.0.0.1/fixture");TArray<uint8> Bytes;
    explicit FReviewResponse(const FString& Text):Json(Text){FTCHARToUTF8 Data(*Text);Bytes.Append(reinterpret_cast<const uint8*>(Data.Get()),Data.Length());}
    const FString& GetURL() const override{return Url;}
    const FString& GetEffectiveURL() const override{return Url;}
    EHttpRequestStatus::Type GetStatus() const override{return EHttpRequestStatus::Succeeded;}
    EHttpFailureReason GetFailureReason() const override{return EHttpFailureReason::None;}
    FString GetURLParameter(const FString&) const override{return {};}
    FString GetHeader(const FString&) const override{return {};}
    TArray<FString> GetAllHeaders() const override{return {};}
    FString GetContentType() const override{return TEXT("application/json");}
    uint64 GetContentLength() const override{return Bytes.Num();}
    const TArray<uint8>& GetContent() const override{return Bytes;}
    TArray<uint8> TakeContent() override{return Bytes;}
    int32 GetResponseCode() const override{return 200;}
    FString GetContentAsString() const override{return Json;}
    FUtf8StringView GetContentAsUtf8StringView() const override{return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()),Bytes.Num());}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLoginRegistrationTest,"ProjectProject01.Operations.LoginRegistration",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectLoginRegistrationTest::RunTest(const FString&)
{
    UWorld* World=CreateReviewWorld();auto* Instance=NewObject<UProjectProject01GameInstance>();World->SetGameInstance(Instance);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);Context.OwningGameInstance=Instance;Instance->WorldContext=&Context;
    Instance->UGameInstance::Init();auto* Auth=Instance->GetSubsystem<UProjectProject01AuthSubsystem>();
    UUserWidget* Base=NewObject<UProjectProject01LoginWidget>(World);Base->Initialize();auto* UI=CastChecked<UProjectProject01LoginWidget>(Base);
    auto* Local=NewObject<ULocalPlayer>(GEngine);UI->SetPlayerContext(FLocalPlayerContext(Local,World));
    ON_SCOPE_EXIT{Auth->OnRegistrationCompleted.RemoveDynamic(UI,&UProjectProject01LoginWidget::HandleRegistrationResult);UI->ReleaseSlateResources(true);Instance->UGameInstance::Shutdown();Instance->WorldContext=nullptr;GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    Auth->bServiceReachable=true;UI->bMultiplayerServiceAvailable=true;
    Auth->OnRegistrationCompleted.AddUniqueDynamic(UI,&UProjectProject01LoginWidget::HandleRegistrationResult);
    TestEqual(TEXT("Only login form is shown initially"),UI->AuthenticationPages->GetActiveWidgetIndex(),0);
    TestEqual(TEXT("Login and registration are separate pages"),UI->AuthenticationPages->GetNumWidgets(),2);
    TestTrue(TEXT("Both password fields are masked"),UI->PasswordInput->GetIsPassword()&&UI->RegistrationPasswordInput->GetIsPassword());
    auto Render=[&](const TCHAR* Name)
    {
        // Render only the tree: do not run NativeConstruct's service polling or touch a live backend.
        auto Slate=UI->WidgetTree->RootWidget->TakeWidget();FWidgetRenderer Renderer(true);const FVector2D Size(1280,720);
        auto* Target=Renderer.DrawWidget(Slate,Size);Renderer.DrawWidget(Target,Slate,Size,.016f);FlushRenderingCommands();FBufferArchive Image;
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("ReviewRemediation/LoginRegistrationUI");IFileManager::Get().MakeDirectory(*Directory,true);
        TestTrue(TEXT("Authentication page render exports"),FImageUtils::ExportRenderTarget2DAsPNG(Target,Image));
        TestTrue(TEXT("Authentication page screenshot saved"),FFileHelper::SaveArrayToFile(Image,*(Directory/(FString(Name)+TEXT(".png")))));
    };
    Render(TEXT("Login"));
    UI->AccountIdInput->SetText(FText::FromString(TEXT("LoginAccount")));UI->PasswordInput->SetText(FText::FromString(TEXT("not-kept")));
    UI->HandleOpenRegistrationClicked();
    TestEqual(TEXT("Register button opens registration form"),UI->AuthenticationPages->GetActiveWidgetIndex(),1);
    TestTrue(TEXT("Opening registration clears login password"),UI->PasswordInput->GetText().IsEmpty());
    UI->RegistrationAccountIdInput->SetText(FText::FromString(TEXT("NewAccount")));
    UI->RegistrationPasswordInput->SetText(FText::FromString(TEXT("ExamplePassword123!")));UI->DisplayNameInput->SetText(FText::FromString(TEXT("테스트 닉네임")));
    Render(TEXT("Registration"));
    UI->bAuthenticationRequestPending=true;UI->HandleServiceStatusChanged(false,TEXT(""),TEXT(""),TEXT(""));
    TestFalse(TEXT("Service poll cannot enable duplicate registration while request pending"),UI->RegistrationSubmitButton->GetIsEnabled());
    TestFalse(TEXT("Submitted registration fields cannot change before completion"),UI->RegistrationAccountIdInput->GetIsEnabled()||UI->RegistrationPasswordInput->GetIsEnabled()||UI->DisplayNameInput->GetIsEnabled());
    UI->HandleReturnToLoginClicked();TestEqual(TEXT("Pending request cannot switch forms"),UI->AuthenticationPages->GetActiveWidgetIndex(),1);
    UI->HandleRegistrationResult(false,TEXT("이미 사용 중인 계정 ID입니다."),TEXT(""));
    TestEqual(TEXT("Duplicate registration stays on signup form"),UI->AuthenticationPages->GetActiveWidgetIndex(),1);
    TestTrue(TEXT("Registration error remains visible"),UI->StatusText->GetText().ToString().Contains(TEXT("이미 사용 중")));
    TestTrue(TEXT("Failed registration permits correction and retry"),UI->RegistrationSubmitButton->GetIsEnabled());
    auto Request=FHttpModule::Get().CreateRequest();Auth->ActiveRequest=Request;Auth->AuthState=EProjectProject01AuthState::Registering;
    UI->bAuthenticationRequestPending=true;
    auto Response=MakeShared<FReviewResponse,ESPMode::ThreadSafe>(TEXT("{\"message\":\"계정이 생성되었습니다.\",\"displayName\":\"테스트 닉네임\",\"registrationOnly\":true}"));
    Auth->HandleRequestComplete(Request,Response,true,UProjectProject01AuthSubsystem::EAuthOperation::Register,TEXT("/api/auth/register"),TEXT("{}"),0);
    TestTrue(TEXT("Registration success leaves no authenticated session"),!Auth->IsSignedIn()&&Auth->AccessToken.IsEmpty()&&Auth->RefreshToken.IsEmpty());
    TestEqual(TEXT("Successful registration returns to login, never lobby"),UI->AuthenticationPages->GetActiveWidgetIndex(),0);
    TestEqual(TEXT("New account ID is prefilled for explicit login"),UI->AccountIdInput->GetText().ToString(),FString(TEXT("NewAccount")));
    TestTrue(TEXT("Registration success clears passwords and nickname"),UI->PasswordInput->GetText().IsEmpty()&&UI->RegistrationPasswordInput->GetText().IsEmpty()&&UI->DisplayNameInput->GetText().IsEmpty());
    TestTrue(TEXT("Login is enabled after account-only registration"),UI->LoginButton->GetIsEnabled());
    TestTrue(TEXT("Success message asks user to log in"),UI->StatusText->GetText().ToString().Contains(TEXT("로그인해주세요")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReviewAsyncStateTest,"ProjectProject01.Review.AsyncState",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReviewAsyncStateTest::RunTest(const FString&)
{
    UWorld* World=CreateReviewWorld();auto* Instance=NewObject<UProjectProject01GameInstance>();World->SetGameInstance(Instance);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);Context.OwningGameInstance=Instance;Instance->WorldContext=&Context;
    // Base initialization only: no crash marker, heartbeat or EOS platform startup from Project Init.
    Instance->UGameInstance::Init();auto* Auth=Instance->GetSubsystem<UProjectProject01AuthSubsystem>();auto* Lobby=Instance->GetSubsystem<UProjectProject01LobbySubsystem>();
    ON_SCOPE_EXIT{Auth->AccessToken.Reset();Auth->AuthState=EProjectProject01AuthState::SignedOut;Instance->UGameInstance::Shutdown();Instance->WorldContext=nullptr;GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    if(!TestNotNull(TEXT("Fixture auth"),Auth)||!TestNotNull(TEXT("Fixture lobby"),Lobby))return false;
    auto* Local=NewObject<ULocalPlayer>(GEngine);auto* Settings=UProjectProject01GameUserSettings::Get();
    UUserWidget* WidgetBase=NewObject<UProjectProject01SettingsWidget>(World);WidgetBase->Initialize();auto* Widget=CastChecked<UProjectProject01SettingsWidget>(WidgetBase);
    Widget->SetPlayerContext(FLocalPlayerContext(Local,World));
    TestTrue(TEXT("Settings widget resolves fixture game instance"),Widget->GetGameInstance()==Instance);
    const FString OldAddress=Settings->GetDirectServerAddress();const float OldVolume=Settings->GetMasterVolume();
    Widget->DirectServerAddressInput->SetText(FText::FromString(TEXT("192.168.88.42")));Widget->PendingMasterVolume=0.123f;
    Auth->AuthState=EProjectProject01AuthState::SignedIn;Auth->AccessToken=TEXT("fixture-not-a-real-token");
    Widget->ApplyPendingSettings();
    TestEqual(TEXT("F19 rejected signed-in address does not save a new IP"),Settings->GetDirectServerAddress(),OldAddress);
    TestEqual(TEXT("F19 rejected apply has no partial volume mutation"),Settings->GetMasterVolume(),OldVolume);
    TestTrue(TEXT("F19 settings explains signed-in rejection"),Widget->StatusText->GetText().ToString().Contains(TEXT("로그인 또는 경기")));
    Auth->AuthState=EProjectProject01AuthState::SignedOut;Auth->AccessToken.Reset();
    Auth->AuthState=EProjectProject01AuthState::SignedIn;Auth->AccessToken=TEXT("fixture-session");
    const uint64 SurveyGeneration=Auth->GetSessionGeneration();
    TestTrue(TEXT("C07 current account and world can receive survey response"),Instance->IsSurveySessionCurrent(SurveyGeneration,World));
    Auth->ForceClearLocalSession();Auth->AuthState=EProjectProject01AuthState::SignedIn;Auth->AccessToken=TEXT("replacement-session");
    TestFalse(TEXT("C07 logout/account change invalidates an older survey response"),Instance->IsSurveySessionCurrent(SurveyGeneration,World));
    auto PreviousGuide=FHttpModule::Get().CreateRequest();auto CurrentGuide=FHttpModule::Get().CreateRequest();Auth->GuideProgressRequest=CurrentGuide;
    Auth->HandleGuideProgressRequestComplete(PreviousGuide,nullptr,false);
    TestTrue(TEXT("C07 delayed old guide completion cannot cancel the new account request"),Auth->GuideProgressRequest==CurrentGuide);
    Auth->GuideProgressRequest.Reset();Auth->AccessToken.Reset();Auth->AuthState=EProjectProject01AuthState::SignedOut;
    Lobby->CurrentRoom.RoomId=TEXT("session-room");Lobby->CurrentRoom.bStarted=true;
    TestTrue(TEXT("C05 multiplayer failure uses session state, not map name"),Instance->IsMultiplayerFailureWorld(World));
    auto* OtherWorld=CreateReviewWorld();ON_SCOPE_EXIT{OtherWorld->DestroyWorld(false);};
    TestFalse(TEXT("C05 another PIE world's network failure cannot log this account out"),Instance->IsMultiplayerFailureWorld(OtherWorld));
    Lobby->ForceClearLocalSession();

    TArray<FString> Requested;Lobby->ReviewRequestSink=[&](const FString& Endpoint){Requested.Add(Endpoint);};
    auto Old=FHttpModule::Get().CreateRequest();Lobby->ActiveRequest=Old;
    Lobby->RefreshLeaderboard(EProjectProject01LeaderboardRole::Survivor,EProjectProject01LeaderboardSort::Rescues,EProjectProject01LeaderboardScope::Team);
    const FString Latest=Lobby->DesiredLeaderboardEndpoint;
    auto OldResponse=MakeShared<FReviewResponse,ESPMode::ThreadSafe>(TEXT("{\"entries\":[{\"rank\":1,\"displayName\":\"OLD\"}]}"));
    Lobby->HandleRequestComplete(Old,OldResponse,true,EProjectProject01LobbyOperation::RefreshLeaderboard,TEXT("GET"),TEXT("/api/leaderboards/Mannequin?sort=Overall&scope=Individual"),TEXT(""),0);
    TestTrue(TEXT("F20 old role response never populates the latest tab"),Lobby->LeaderboardEntries.IsEmpty());
    TestTrue(TEXT("F20 completion dispatches exactly the latest requested scope"),Requested.Num()==1&&Requested[0]==Latest);
    auto Current=FHttpModule::Get().CreateRequest();Lobby->ActiveRequest=Current;
    auto Empty=MakeShared<FReviewResponse,ESPMode::ThreadSafe>(TEXT("{\"entries\":[]}"));
    Lobby->HandleRequestComplete(Current,Empty,true,EProjectProject01LobbyOperation::RefreshLeaderboard,TEXT("GET"),Latest,TEXT(""),0);
    TestTrue(TEXT("F20 queued refresh completes without another loop"),!Lobby->bLeaderboardRefreshQueued&&Requested.Num()==1);

    UUserWidget* LobbyBase=NewObject<UProjectProject01LobbyWidget>(World);LobbyBase->Initialize();auto* UI=CastChecked<UProjectProject01LobbyWidget>(LobbyBase);UI->SetPlayerContext(FLocalPlayerContext(Local,World));
    Lobby->CurrentRoom.RoomId=TEXT("room");Lobby->CurrentRoom.MatchId=TEXT("match");Lobby->CurrentRoom.bStarted=true;
    UI->AdmissionRoomId=TEXT("room");UI->AdmissionMatchId=TEXT("match");UI->bGameStartFadeComplete=true;
    Lobby->ActiveRequest=Current;UI->ContinueGameAdmission(1);
    TestEqual(TEXT("F01 fade completion waits for in-flight room poll"),UI->AdmissionTicketAttempts,0);
    Lobby->ActiveRequest.Reset();UI->ContinueGameAdmission(0);
    TestEqual(TEXT("F01 room poll release triggers ticket continuation"),UI->AdmissionTicketAttempts,1);
    TestTrue(TEXT("F01 admission continuation calls ticket endpoint"),Requested.Last()==TEXT("/api/game-tickets"));
    Lobby->ActiveRequest=Current;UI->ContinueGameAdmission(16);
    TestFalse(TEXT("F01 held request has a bounded black-screen wait"),UI->bGameStartFadeComplete);
    UI->AdmissionRoomId=TEXT("room");UI->AdmissionMatchId=TEXT("old-match");UI->bGameStartFadeComplete=true;UI->ContinueGameAdmission(0);
    TestFalse(TEXT("F01 match change cancels pending admission"),UI->bGameStartFadeComplete);
    Lobby->ActiveRequest.Reset();Lobby->ReviewRequestSink=nullptr;Lobby->ForceClearLocalSession();

    // No live HTTP or map travel: this fixture has no signed-in account and the widget is not constructed in a viewport.
    Lobby->CurrentRoom.RoomId=TEXT("queued-party");Lobby->CurrentRoom.bRandomParty=true;
    Lobby->bMatchmaking=true;Lobby->MatchSearchId=TEXT("queued-search");Lobby->ActiveRequest=FHttpModule::Get().CreateRequest();
    UI->bShowingMatchmaking=true;UI->HandleLogoutClicked();
    TestTrue(TEXT("Random queue logout invalidates local queue, party and in-flight polling"),
        !Lobby->IsMatchmaking()&&!Lobby->HasCurrentRoom()&&!Lobby->IsRequestInFlight()&&Lobby->MatchSearchId.IsEmpty());
    TestTrue(TEXT("Logout blocks duplicate clicks and prevents automatic party creation"),
        UI->bLobbyLogoutInProgress&&!UI->GetIsEnabled()&&!UI->bShowingMatchmaking&&UI->bPartyCreationRequested);
    const uint64 ClearedGeneration=Auth->GetSessionGeneration();UI->HandleLogoutClicked();
    TestEqual(TEXT("Duplicate logout does not repeat session clearing"),Auth->GetSessionGeneration(),ClearedGeneration);
    UI->RefreshLobbyState();TestFalse(TEXT("Pending logout never starts another poll or party request"),Lobby->IsRequestInFlight());
    UI->HandleLogoutResult(false,TEXT("fixture failure"),TEXT(""));
    TestTrue(TEXT("Failed logout restores usable lobby controls"),!UI->bLobbyLogoutInProgress&&UI->GetIsEnabled());
    Lobby->CurrentRoom.RoomId=TEXT("confirmed-room");Lobby->CurrentRoom.bRandomParty=true;Lobby->CurrentRoom.bRandomMatch=true;
    UI->HandleLogoutClicked();TestFalse(TEXT("Confirmed matching cannot be cancelled through logout handler"),UI->bLobbyLogoutInProgress);
    Lobby->ForceClearLocalSession();

    auto* Helper=World->SpawnActor<AHelperRearGuardCharacter>();auto* AI=World->SpawnActor<AHelperRearGuardAIController>();AI->Possess(Helper);
    Helper->SetActorLocation(FVector(-10000,0,0));Helper->GuardedPlayer=World->SpawnActor<APlayerCharacter>();Helper->bFollowDistanceLocked=true;Helper->LockedRetreatTarget=FVector(10000,0,0);
    AI->RepathInterval=0;AI->AcceptanceRadius=10;AI->ProgressDistance=20;AI->StuckTimeout=1;AI->StuckWaitTime=0.5;AI->bHasMoveTarget=true;AI->LastProgressLocation=Helper->GetActorLocation();AI->LastProgressTime=-0.5;
    AI->Tick(0.25f);TestEqual(TEXT("F21 failed repath cannot refresh the stuck clock"),AI->LastProgressTime,-0.5);
    AI->LastProgressTime=-1.1;AI->Tick(0.25f);
    TestTrue(TEXT("F21 blocked helper enters bounded recovery wait"),AI->bWaitingForRepath&&!AI->bHasMoveTarget&&AI->WaitUntilTime>World->GetTimeSeconds());
    AI->WaitUntilTime=-1;AI->Tick(0.25f);TestFalse(TEXT("F21 recovery wait expires and permits a new path request"),AI->bWaitingForRepath);

    auto* Diagnostic=NewObject<UProjectProject01DiagnosticsSubsystem>(World);
    Diagnostic->TestRunId=TEXT("ReviewInterrupted_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);Diagnostic->TestName=TEXT("review");
    Diagnostic->TestDefinition.FailureCriteria=TEXT("RiskCount==0");Diagnostic->Samples.AddDefaulted();
    auto Stop=[&](const FString& Reason)
    {Diagnostic->Events.Reset();Diagnostic->bCaptureActive=true;Diagnostic->StopCapture(Reason);FString Xml;FFileHelper::LoadFileToString(Xml,*FPaths::Combine(Diagnostic->GetOutputDirectory(),TEXT("TestReport.xml")));return Xml;};
    TestTrue(TEXT("F24 world teardown cannot become Pass despite passing criteria"),Stop(TEXT("World ended")).Contains(TEXT("Outcome=\"Inconclusive\"")));
    TestTrue(TEXT("F24 normal completion can still pass measured criteria"),Stop(TEXT("Completed")).Contains(TEXT("Outcome=\"Pass\"")));

    auto* Save=Instance->GetSubsystem<UProjectProject01SaveSubsystem>();FString Notice;
    TestTrue(TEXT("C04 identical data version is compatible without warning"),Save->CheckDataCompatibility(TEXT("v"),TEXT("v"),{},Notice)&&Notice.IsEmpty());
    TestTrue(TEXT("C04 tuning-only changes remain loadable with an explicit warning"),Save->CheckDataCompatibility(TEXT("old"),TEXT("new"),{},Notice)&&!Notice.IsEmpty());
    TestFalse(TEXT("C04 explicitly incompatible data version is blocked without deleting it"),Save->CheckDataCompatibility(TEXT("old"),TEXT("new"),{TEXT("old")},Notice));
    Save->PendingLoad=NewObject<UProjectProject01SinglePlayerSaveGame>();Save->PreviousSlotBeforeLoad=2;Save->CurrentSlotIndex=3;
    Save->HandleLoadTravelFailure(World,ETravelFailure::LoadMapFailure,TEXT("fixture"));
    TestTrue(TEXT("C04 failed map load clears PendingLoad and restores previous logical slot"),!Save->PendingLoad&&Save->CurrentSlotIndex==2);
    Save->BaselineMapPackageName=TEXT("owned");Save->HandlePostLoadMap(OtherWorld);
    TestEqual(TEXT("C04 other PIE map load cannot replace this save baseline"),Save->BaselineMapPackageName,FString(TEXT("owned")));
    World->SpawnActor<AHelperRearGuardCharacter>();
    TestFalse(TEXT("C04 duplicate checkpoint actor IDs fail before overwriting state"),Save->ValidateActorIdentities(World,Notice));

    const FString Evidence=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("ReviewRemediation"),TEXT("Evidence-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".json"));
    auto Marker=MakeShared<FJsonObject>();const FDateTime Now=FDateTime::UtcNow();
    TestTrue(TEXT("C03 first missing native evidence enters grace, not permanent exclusion"),ProjectProject01CrashFiles::AwaitNativeEvidence(Evidence,Marker,Now));
    TestTrue(TEXT("C03 native evidence writer gets the full grace interval"),ProjectProject01CrashFiles::AwaitNativeEvidence(Evidence,Marker,Now+FTimespan::FromSeconds(119)));
    FString Stored;FFileHelper::LoadFileToString(Stored,*Evidence);TSharedPtr<FJsonObject> Restarted;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Stored),Restarted);
    TestFalse(TEXT("C03 persisted grace expires even after another game process restart"),ProjectProject01CrashFiles::AwaitNativeEvidence(Evidence,Restarted,Now+FTimespan::FromSeconds(121)));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReviewSynthStopTest,"ProjectProject01.Review.SynthStop",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReviewSynthStopTest::RunTest(const FString&)
{
    auto* Pain=NewObject<UPainGroanSynthComponent>();auto* Fall=NewObject<UBodyFallSynthComponent>();auto* Capture=NewObject<UMannequinCaptureSynthComponent>();
    int32 Rate=8000;Pain->Init(Rate);Fall->Init(Rate);Capture->Init(Rate);
    TArray<float> Audio;Audio.SetNumZeroed(16000);
    Pain->PlaybackGeneration.Increment();Pain->AudioGeneration=1;Pain->bVoiceActive=true;Pain->VoiceDurationSeconds=0.01f;
    Fall->PlaybackGeneration.Increment();Fall->AudioGeneration=1;Fall->bImpactActive=true;
    Capture->PlaybackGeneration.Increment();Capture->AudioGeneration=1;Capture->bImpactActive=true;
    Pain->OnGenerateAudio(Audio.GetData(),Audio.Num());Fall->OnGenerateAudio(Audio.GetData(),Audio.Num());Capture->OnGenerateAudio(Audio.GetData(),Audio.Num());
    TestTrue(TEXT("F23 all one-shot voices reach their inactive tail"),!Pain->bVoiceActive&&!Fall->bImpactActive&&!Capture->bImpactActive);
    // Drain the actual audio-to-game-thread Stop tasks. UObject weak refs and generation guards are exercised.
    Pain->PlaybackGeneration.Increment();Pain->bVoiceActive=true;
    FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
    TestTrue(TEXT("F23 an old stop task cannot consume a newly triggered generation"),Pain->bVoiceActive&&Pain->PlaybackGeneration.GetValue()==2);
    TestEqual(TEXT("F23 stale audio completion does not invoke Stop on the new generation"),Pain->ReviewCompletedStopCount,0);
    TestEqual(TEXT("F23 body-fall tail invokes actual Stop exactly once on game thread"),Fall->ReviewCompletedStopCount,1);
    TestEqual(TEXT("F23 capture tail invokes actual Stop exactly once on game thread"),Capture->ReviewCompletedStopCount,1);
    TestFalse(TEXT("F23 completed body fall does not remain an active audio source"),Fall->IsPlaying());
    TestFalse(TEXT("F23 completed capture does not remain an active audio source"),Capture->IsPlaying());
    const FString Root=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("ReviewRemediation/NativeOutbox"),FGuid::NewGuid().ToString(EGuidFormats::Digits));
    FString Path,Again;
    TestTrue(TEXT("F26 native producer persists its durable envelope"),FProjectResultOutbox::Persist(Root,TEXT("http://127.0.0.1:59999"),TEXT("/api/server/matches/complete"),TEXT("{\"matchId\":\"fixture\"}"),Path));
    TestTrue(TEXT("F26 duplicate enqueue uses the same journal"),FProjectResultOutbox::Persist(Root,TEXT("http://127.0.0.1:59999"),TEXT("/api/server/matches/complete"),TEXT("{\"matchId\":\"fixture\"}"),Again)&&Path==Again);
    FString Text;FFileHelper::LoadFileToString(Text,*Path);TestFalse(TEXT("F26 journal has no bearer or server secret"),Text.Contains(TEXT("Secret"))||Text.Contains(TEXT("Bearer")));
    TestTrue(TEXT("F26 retry classification matches host recovery"),FProjectResultOutbox::IsTransient(503)&&FProjectResultOutbox::IsTransient(0)&&!FProjectResultOutbox::IsTransient(403));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectNetworkIsolationTest,"ProjectProject01.Review.NetworkIsolation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectNetworkIsolationTest::RunTest(const FString&)
{
    auto* A=NewObject<UProjectProject01GameInstance>();auto* B=NewObject<UProjectProject01GameInstance>();
    UWorld* WA=CreateReviewWorld();UWorld* WB=CreateReviewWorld();WA->SetGameInstance(A);WB->SetGameInstance(B);
    auto& CA=GEngine->CreateNewWorldContext(EWorldType::PIE);CA.SetCurrentWorld(WA);CA.OwningGameInstance=A;CA.PendingNetGame=NewObject<UPendingNetGame>();
    auto& CB=GEngine->CreateNewWorldContext(EWorldType::PIE);CB.SetCurrentWorld(WB);CB.OwningGameInstance=B;CB.PendingNetGame=NewObject<UPendingNetGame>();
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(WA);GEngine->DestroyWorldContext(WB);WA->DestroyWorld(false);WB->DestroyWorld(false);};
    TestTrue(TEXT("C06 pending handshake callback resolves its exact PIE owner A"),A->ResolveEncryptionOwner(CA.PendingNetGame)==A);
    TestTrue(TEXT("C06 pending handshake callback resolves its exact PIE owner B"),A->ResolveEncryptionOwner(CB.PendingNetGame)==B);
    TestNull(TEXT("C06 unowned callback fails closed instead of taking the last PIE client"),A->ResolveEncryptionOwner(nullptr));
    A->PendingClientEncryptionKey.Init(11,32);B->PendingClientEncryptionKey.Init(22,32);
    uint8 AKey=0,BKey=0;
    A->ReceivedNetworkEncryptionAck(FOnEncryptionKeyResponse::CreateLambda([&](const FEncryptionKeyResponse& Response){AKey=Response.EncryptionData.Key[0];}));
    B->ReceivedNetworkEncryptionAck(FOnEncryptionKeyResponse::CreateLambda([&](const FEncryptionKeyResponse& Response){BKey=Response.EncryptionData.Key[0];}));
    TestTrue(TEXT("C06 concurrent PIE keys are distinct and consumed per instance"),AKey==11&&BKey==22&&A->PendingClientEncryptionKey.IsEmpty()&&B->PendingClientEncryptionKey.IsEmpty());
    FProjectProject01ValidatedJoinClaim Claim;Claim.UserId=TEXT("A");Claim.ValidUntilUtc=FDateTime::UtcNow()+FTimespan::FromSeconds(20);A->ValidatedClaims.Add(TEXT("same-token"),Claim);
    FProjectProject01ValidatedJoinClaim Out;
    TestFalse(TEXT("C06 another PIE game cannot consume an account claim"),B->ConsumeValidatedJoinClaim(TEXT("same-token"),Out));
    TestTrue(TEXT("C06 owner consumes the claim once"),A->ConsumeValidatedJoinClaim(TEXT("same-token"),Out)&&Out.UserId==TEXT("A")&&!A->ConsumeValidatedJoinClaim(TEXT("same-token"),Out));
    return true;
}
#endif

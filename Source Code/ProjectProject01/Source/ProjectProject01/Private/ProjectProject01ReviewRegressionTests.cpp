#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
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
	UWorld* World=CreateReviewWorld();ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Instance=NewObject<UProjectProject01GameInstance>();World->SetGameInstance(Instance);
	auto* Previous=World->SpawnActor<AMultiplayTestGameMode>();
	Previous->AuthoritativeMatchId=TEXT("review-match");Previous->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::InProgress;
	Previous->MatchStartWorldSeconds=-100.0;
	TArray<AMultiplayTestPlayerController*> Players;
	for(int32 Index=0;Index<2;++Index)
	{
		auto* PC=World->SpawnActor<AMultiplayTestPlayerController>();Players.Add(PC);
		FProjectProject01ValidatedJoinClaim Claim;Claim.UserId=FString::FromInt(Index+1);Claim.MatchId=TEXT("review-match");
		Claim.Role=Index==0?TEXT("Mannequin"):TEXT("Survivor");PC->SetAuthenticatedLobbyIdentity(Claim);
		FMultiplayTestServerPlayerRecord Record;Record.UserId=Claim.UserId;Record.MatchId=Claim.MatchId;Record.Role=Claim.Role;
		Record.SurvivorState=Index==0?EMultiplayTestSurvivorState::NotApplicable:EMultiplayTestSurvivorState::TransitionWaiting;
		Record.CaptureCount=2;Record.RescueCount=3;Record.FirstCaptureSeconds=25;
		Previous->MatchPlayerRecords.Add(PC,Record);
	}
	FMultiplayTestServerPlayerRecord Left;Left.UserId=TEXT("left");Left.Role=TEXT("Survivor");Left.MatchId=TEXT("review-match");Left.bForfeited=true;Left.SurvivorState=EMultiplayTestSurvivorState::Eliminated;
	Previous->DisconnectedMatchRecords.Add(Left.UserId,Left);
	Previous->PrepareLevelTransitionTravel();
	TestTrue(TEXT("Travel snapshot contains only two connected controllers"),Instance->ServerMatchCarryover&&Instance->ServerMatchCarryover->Players.Num()==2);
	auto* Next=World->SpawnActor<AMultiplayTestGameMode>();
	for(auto* PC:Players)Next->RegisterMatchPlayer(PC);
	TestEqual(TEXT("Next map does not wait for the departed third controller"),Next->ExpectedMatchPlayers,2);
	TestEqual(TEXT("Capture total survives a map change"),Next->MatchPlayerRecords.FindChecked(Players[0]).CaptureCount,2);
	TestEqual(TEXT("Rescue total survives a map change"),Next->MatchPlayerRecords.FindChecked(Players[1]).RescueCount,3);
	TestEqual(TEXT("First capture time remains match-wide"),Next->MatchPlayerRecords.FindChecked(Players[0]).FirstCaptureSeconds,25.0);
	Next->TryStartAuthoritativeMatch();
	TestTrue(TEXT("Two-player transition reaches InProgress"),Next->AuthoritativeMatchPhase==EMultiplayTestMatchPhase::InProgress);
	TestTrue(TEXT("Result timer includes the previous map"),Next->GetElapsedMatchSeconds()>=100.0);
	TestTrue(TEXT("Departed survivor record is preserved without increasing arrival count"),Next->DisconnectedMatchRecords.Contains(TEXT("left")));
	for(auto* PC:Players)TestFalse(TEXT("Arrival unlocks controller input and carryover flag"),PC->IsLevelTransitionCarryover()||PC->IsMoveInputIgnored()||PC->IsLookInputIgnored());
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
#endif

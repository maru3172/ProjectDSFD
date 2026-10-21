#include "MultiplayTestGameMode.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01Tutorial.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"

void AMultiplayTestPlayerController::ClientBeginLoadingGate_Implementation(const FString& Epoch,const FString& OwnId,const FString& OwnName,int32 Expected,bool Released)
{
	if(!IsLocalController() || (Epoch==LoadingGateEpoch && bLoadingGateRequired))return;
	LoadingGateEpoch=Epoch;bLoadingGateRequired=true;bLoadingGateReleased=Released;LastReportedLoadingStage=0;
	if(auto* GI=GetGameInstance())if(auto* Loading=GI->GetSubsystem<UProjectLoadingScreenSubsystem>())Loading->BeginMultiplayerGate(Epoch,OwnId,OwnName,Expected,Released);
}
void AMultiplayTestPlayerController::ClientUpdateLoadingParticipants_Implementation(const FString& Epoch,const TArray<FProjectLoadingParticipant>& Participants)
{if(auto* GI=GetGameInstance())if(auto* Loading=GI->GetSubsystem<UProjectLoadingScreenSubsystem>())Loading->UpdateParticipants(Epoch,Participants);}
void AMultiplayTestPlayerController::ClientReleaseLoadingGate_Implementation(const FString& Epoch)
{
	if(Epoch!=LoadingGateEpoch)return;bLoadingGateReleased=true;
	if(auto* GI=GetGameInstance())if(auto* Loading=GI->GetSubsystem<UProjectLoadingScreenSubsystem>())Loading->ReleaseMultiplayerGate(Epoch);
	TryRevealInitialGameplayFade();TryCompleteClientArrivalView();
}
void AMultiplayTestPlayerController::ClientFailLoadingGate_Implementation(const FString& Message)
{
	bLoadingGateRequired=false;LoadingGateEpoch.Reset();
	if(auto* GI=GetGameInstance())
	{
		if(auto* Loading=GI->GetSubsystem<UProjectLoadingScreenSubsystem>())Loading->CancelLoading();
		if(auto* Auth=GI->GetSubsystem<UProjectProject01AuthSubsystem>();Auth && Auth->IsSignedIn())
		{Auth->InvalidateSessionFromServerFailure(Message);return;}
	}
	ClientReturnToMainMenuWithTextReason(FText::FromString(Message));
}
void AMultiplayTestPlayerController::ServerReportLoadingStage_Implementation(const FString& Epoch,uint8 Progress)
{if(auto* Mode=GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>())Mode->ReceiveLoadingStage(this,Epoch,Progress);}
bool AMultiplayTestPlayerController::TryPassLoadingGate()
{
	if(!bLoadingGateRequired)return true;
	if(auto* GI=GetGameInstance())if(auto* Loading=GI->GetSubsystem<UProjectLoadingScreenSubsystem>())Loading->NotifyViewReady();
	if(!LoadingGateEpoch.IsEmpty() && LastReportedLoadingStage<100)
	{LastReportedLoadingStage=100;ServerReportLoadingStage(LoadingGateEpoch,100);}
	return bLoadingGateReleased;
}

void AMultiplayTestGameMode::InitializeLoadingGate(AMultiplayTestPlayerController* Controller)
{
	if(!Controller || GetNetMode()==NM_Standalone || AProjectProject01TutorialGameMode::IsTutorialWorld(GetWorld()))return;
	if(LoadingGate.Epoch.IsEmpty())LoadingGate.Begin(ExpectedMatchPlayers,FPlatformTime::Seconds());
	const auto* Record=MatchPlayerRecords.Find(Controller);if(!Record)return;
	const FString Id=Record->UserId.IsEmpty()?Controller->GetName():Record->UserId;
	LoadingGate.Join(Id);
	Controller->ClientBeginLoadingGate(LoadingGate.Epoch,Id,Record->DisplayName,LoadingGate.Expected,LoadingGate.Released);
	LoadingBroadcastSeconds=1;
}
void AMultiplayTestGameMode::ReceiveLoadingStage(AMultiplayTestPlayerController* Controller,const FString& Epoch,uint8 Progress)
{
	if(!HasAuthority() || !Controller || Controller->GetWorld()!=GetWorld())return;
	const auto* Record=MatchPlayerRecords.Find(Controller);if(!Record || Record->bDisconnected)return;
	// Ownership, registered identity and per-world epoch are necessary; this RPC
	// never grants a login, role, game ticket or authority to change match results.
	if(Progress==100 && (!IsValid(Controller->GetViewTarget()) || !GetWorld()->HasBegunPlay()))return;
	LoadingGate.Report(Record->UserId.IsEmpty()?Controller->GetName():Record->UserId,Epoch,Progress);
}
bool AMultiplayTestGameMode::TickLoadingGate(float Delta)
{
	if(LoadingGate.Epoch.IsEmpty() || LoadingGate.Released)return false;
	// Freeze arrivals immediately, including pawns created after the first join.
	for(TActorIterator<ACharacter> It(GetWorld());It;++It)if(auto* Move=It->GetCharacterMovement())
	{
		if(!LoadingMovementModes.Contains(*It))LoadingMovementModes.Add(*It,TPair<uint8,uint8>(uint8(Move->MovementMode),Move->CustomMovementMode));
		Move->StopMovementImmediately();Move->DisableMovement();
	}
	if(AuthoritativeMatchPhase==EMultiplayTestMatchPhase::RoleAssignment)FinalizeLevelTransitionArrivals();
	LoadingBroadcastSeconds+=Delta;
	TSet<FString> Present;TArray<FProjectLoadingParticipant> Rows;
	for(const auto& Pair:MatchPlayerRecords)if(auto* PC=Pair.Key.Get();PC && !Pair.Value.bDisconnected)
	{
		const FString Id=Pair.Value.UserId.IsEmpty()?PC->GetName():Pair.Value.UserId;Present.Add(Id);
		FProjectLoadingParticipant Row;Row.UserId=Id;Row.DisplayName=Pair.Value.DisplayName.IsEmpty()?PC->GetName():Pair.Value.DisplayName;
		Row.Progress=LoadingGate.Progress.FindRef(Id);Rows.Add(Row);
	}
	for(auto It=LoadingGate.Progress.CreateIterator();It;++It)if(!Present.Contains(It.Key()))It.RemoveCurrent();
	if(LoadingBroadcastSeconds>=.5f)
	{
		LoadingBroadcastSeconds=0;
		for(const auto& Pair:MatchPlayerRecords)if(auto* PC=Pair.Key.Get())PC->ClientUpdateLoadingParticipants(LoadingGate.Epoch,Rows);
	}
	if(LoadingGate.AllReady() && AuthoritativeMatchPhase==EMultiplayTestMatchPhase::RoleAssignment)
	{
		LoadingGate.Released=true;MatchStartWorldSeconds=GetWorld()->GetTimeSeconds();
		if(MatchCarryover)PreviousMatchSeconds=MatchCarryover->ElapsedSeconds;
		for(const auto& Pair:LoadingMovementModes)if(auto* Character=Pair.Key.Get())
			if(auto* Move=Character->GetCharacterMovement())Move->SetMovementMode(EMovementMode(Pair.Value.Key),Pair.Value.Value);
		LoadingMovementModes.Reset();SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::InProgress);BeginMatchTimeline();
		UE_LOG(LogProjectProject01Multiplayer,Log,TEXT("All %d participants ready; loading barrier released."),LoadingGate.Expected);
		for(const auto& Pair:MatchPlayerRecords)if(auto* PC=Pair.Key.Get())
		{PC->ClientUpdateLoadingParticipants(LoadingGate.Epoch,Rows);PC->ClientReleaseLoadingGate(LoadingGate.Epoch);}
		return false;
	}
	if(LoadingGate.Expired(FPlatformTime::Seconds()))
	{
		LoadingGate.Released=true;SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Ending);
		for(const auto& Pair:MatchPlayerRecords)if(auto* PC=Pair.Key.Get())PC->ClientFailLoadingGate(TEXT("참가자 로딩 준비가 120초 안에 완료되지 않았습니다. 다시 로그인해 주세요."));
		// A failed load is not a played match and must not award a victory/result.
		if(IsRunningDedicatedServer())GetWorldTimerManager().SetTimerForNextTick([](){FPlatformMisc::RequestExit(false);});
		return false;
	}
	return true;
}

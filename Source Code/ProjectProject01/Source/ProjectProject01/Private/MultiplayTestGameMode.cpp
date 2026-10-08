// File: Source/ProjectProject01/Private/MultiplayTestGameMode.cpp
// Build target: ProjectProject01Server / ProjectProject01

#include "MultiplayTestGameMode.h"

#include "MannequinAICharacter.h"
#include "MannequinAIController.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "ProjectProject01TuningData.h"
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "Components/CapsuleComponent.h"
#include "Engine/TriggerBox.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY(LogProjectProject01Multiplayer);

namespace
{
	struct FMatchResolutionSummary
	{
		int32 SurvivorCount = 0;
		int32 ResolvedCount = 0;
		int32 EliminatedCount = 0;

		bool IsComplete() const { return SurvivorCount > 0 && ResolvedCount == SurvivorCount; }
		bool DidMannequinWin() const { return IsComplete() && EliminatedCount == SurvivorCount; }
	};

	FMatchResolutionSummary EvaluateMatchResolution(
		const TArray<EMultiplayTestSurvivorState>& SurvivorStates)
	{
		FMatchResolutionSummary Summary;
		Summary.SurvivorCount = SurvivorStates.Num();
		for (const EMultiplayTestSurvivorState State : SurvivorStates)
		{
			Summary.ResolvedCount += State == EMultiplayTestSurvivorState::Eliminated ||
				State == EMultiplayTestSurvivorState::Escaped ? 1 : 0;
			Summary.EliminatedCount += State == EMultiplayTestSurvivorState::Eliminated ? 1 : 0;
		}
		return Summary;
	}

	bool IsPointInsideVisionCone(
		const FVector& ViewOrigin,
		const FVector& ViewDirection,
		const FVector& TargetPoint,
		const float HalfAngleDegrees)
	{
		const FVector ToTarget = TargetPoint - ViewOrigin;
		if (ToTarget.IsNearlyZero() || ViewDirection.IsNearlyZero() || ToTarget.ContainsNaN() || ViewDirection.ContainsNaN())
		{
			return false;
		}

		const float MinimumDotProduct = FMath::Cos(FMath::DegreesToRadians(FMath::Max(0.0f, HalfAngleDegrees)));
		return FVector::DotProduct(ViewDirection.GetSafeNormal(), ToTarget.GetSafeNormal()) >= MinimumDotProduct;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01SurvivorVisionConeTest,
	"ProjectProject01.Multiplayer.SurvivorVisionCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01SurvivorVisionConeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A point directly ahead is inside the vision cone"),
		IsPointInsideVisionCone(FVector::ZeroVector, FVector::ForwardVector, FVector(100.0f, 0.0f, 0.0f), 55.0f));
	TestFalse(TEXT("A point directly behind is outside the vision cone"),
		IsPointInsideVisionCone(FVector::ZeroVector, FVector::ForwardVector, FVector(-100.0f, 0.0f, 0.0f), 55.0f));
	TestFalse(TEXT("An invalid zero-distance target is rejected"),
		IsPointInsideVisionCone(FVector::ZeroVector, FVector::ForwardVector, FVector::ZeroVector, 55.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01MatchResolutionTest,
	"ProjectProject01.Multiplayer.MatchResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01MatchResolutionTest::RunTest(const FString& Parameters)
{
	const FMatchResolutionSummary InProgress = EvaluateMatchResolution({
		EMultiplayTestSurvivorState::Eliminated,
		EMultiplayTestSurvivorState::Active });
	TestFalse(TEXT("One active survivor keeps the match in progress"), InProgress.IsComplete());

	const FMatchResolutionSummary MannequinVictory = EvaluateMatchResolution({
		EMultiplayTestSurvivorState::Eliminated,
		EMultiplayTestSurvivorState::Eliminated });
	TestTrue(TEXT("All eliminated survivors complete the match"), MannequinVictory.IsComplete());
	TestTrue(TEXT("All eliminated survivors produce a mannequin victory"), MannequinVictory.DidMannequinWin());

	const FMatchResolutionSummary MixedResolution = EvaluateMatchResolution({
		EMultiplayTestSurvivorState::Eliminated,
		EMultiplayTestSurvivorState::Escaped });
	TestTrue(TEXT("Escaped and eliminated survivors complete the match"), MixedResolution.IsComplete());
	TestFalse(TEXT("Any escaped survivor prevents a mannequin victory"), MixedResolution.DidMannequinWin());
	return true;
}
#endif

AMultiplayTestGameMode::AMultiplayTestGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	// 콘텐츠 Blueprint를 불러오지 못해도 네트워크 이동이 가능한 네이티브 Pawn으로 안전하게 대체한다.
	DefaultPawnClass = APlayerCharacter::StaticClass();

	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(
		TEXT("/Game/MyProject/BP_PlayerCharacter"));
	if (PlayerPawnClassFinder.Succeeded())
	{
		DefaultPawnClass = PlayerPawnClassFinder.Class;
	}
	else
	{
		ensureMsgf(false,
			TEXT("MultiplayTestGameMode could not load /Game/MyProject/BP_PlayerCharacter. Falling back to APlayerCharacter."));
		UE_LOG(LogProjectProject01Multiplayer, Error,
			TEXT("MultiplayTestGameMode could not load /Game/MyProject/BP_PlayerCharacter; using APlayerCharacter."));
	}

	PlayerControllerClass = AMultiplayTestPlayerController::StaticClass();
}

void AMultiplayTestGameMode::BeginPlay()
{
	Super::BeginPlay();
	AuthoritativeMatchPhase = EMultiplayTestMatchPhase::Waiting;
	BindEscapeTriggers();

	if (UWorld* World = GetWorld(); IsValid(World))
	{
		if (UProjectProject01TuningSubsystem* TuningSubsystem = World->GetSubsystem<UProjectProject01TuningSubsystem>())
		{
			FMannequinAITuningRow Tuning;
			if (TuningSubsystem->GetMannequinTuning(Tuning))
			{
				ApplyMannequinTuning(Tuning);
			}
		}
	}
}

void AMultiplayTestGameMode::ApplyMannequinTuning(const FMannequinAITuningRow& Tuning)
{
	SurvivorVisionCheckIntervalSeconds = FMath::Max(0.0f, Tuning.SurvivorVisionCheckIntervalSeconds);
	SurvivorVisionHalfAngleDegrees = FMath::Max(0.0f, Tuning.SurvivorVisionHalfAngleDegrees);
}

void AMultiplayTestGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || DeltaSeconds <= 0.0f)
	{
		return;
	}

	if (AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress &&
		MatchTimeLimitSeconds > 0.0f && GetElapsedMatchSeconds() >= MatchTimeLimitSeconds)
	{
		FinishAuthoritativeMatch(TEXT("TimeLimit"));
	}
	ProcessReconnectTimeouts();

	SurvivorVisionCheckAccumulatorSeconds += DeltaSeconds;
	if (SurvivorVisionCheckAccumulatorSeconds < FMath::Max(SurvivorVisionCheckIntervalSeconds, 0.01f))
	{
		return;
	}

	SurvivorVisionCheckAccumulatorSeconds = 0.0f;
	UpdateSurvivorVisionFrozenStates();
}

void AMultiplayTestGameMode::PreLogin(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}
	if (GetNetMode() == NM_Standalone || !bRequireGameJoinTicket)
	{
		if (GetNumPlayers() >= ExpectedMatchPlayers)
		{
			ErrorMessage = TEXT("The match is full.");
		}
		return;
	}

	const FString Ticket = UGameplayStatics::ParseOption(Options, TEXT("GameTicket"));
	const FString RequestedRoomId = UGameplayStatics::ParseOption(Options, TEXT("LobbyRoomId"));
	FProjectProject01ValidatedJoinClaim Claim;
	if (Ticket.IsEmpty() || !UProjectProject01GameInstance::ConsumeValidatedJoinClaim(Ticket, Claim))
	{
		ErrorMessage = TEXT("A valid one-time game connection ticket is required.");
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Security rejected a connection from %s without a validated game ticket."), *Address);
		return;
	}
	if (Claim.RoomId.IsEmpty() || Claim.MatchId.IsEmpty() ||
		(Claim.Role != TEXT("Mannequin") && Claim.Role != TEXT("Survivor")) ||
		(!RequestedRoomId.IsEmpty() && !RequestedRoomId.Equals(Claim.RoomId, ESearchCase::CaseSensitive)))
	{
		ErrorMessage = TEXT("The game connection ticket does not match this room.");
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Security rejected a game ticket with inconsistent room or role data."));
		return;
	}
	FString VersionError;
	if (!FProjectProject01VersionContract::Matches(
		Claim.ClientBuildVersion, Claim.DedicatedServerBuildVersion, Claim.ApiVersion, Claim.GameDataVersion,
		Claim.NetworkProtocolVersion, VersionError))
	{
		ErrorMessage = VersionError;
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Security rejected an incompatible client: %s"), *VersionError);
		return;
	}

	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (!Claim.UserId.IsEmpty() && Pair.Value.UserId == Claim.UserId && Pair.Key.IsValid())
		{
			ErrorMessage = TEXT("This account is already connected to the match.");
			return;
		}
	}
	const FMultiplayTestServerPlayerRecord* DisconnectedRecord =
		Claim.UserId.IsEmpty() ? nullptr : DisconnectedMatchRecords.Find(Claim.UserId);
	const FMultiplayTestMatchResult* CompletedResult =
		Claim.UserId.IsEmpty() ? nullptr : CompletedResultsByUserId.Find(Claim.UserId);
	const bool bKnownReconnect =
		(DisconnectedRecord != nullptr && !DisconnectedRecord->bForfeited &&
		 DisconnectedRecord->MatchId == Claim.MatchId && DisconnectedRecord->Role == Claim.Role) ||
		(CompletedResult != nullptr && CompletedResult->MatchId == Claim.MatchId && CompletedResult->Role == Claim.Role);
	if ((AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress ||
		 AuthoritativeMatchPhase == EMultiplayTestMatchPhase::Ending ||
		 AuthoritativeMatchPhase == EMultiplayTestMatchPhase::Results) && !bKnownReconnect)
	{
		ErrorMessage = TEXT("The match is already in progress and this account has no reconnect slot.");
		return;
	}
	if (!bKnownReconnect && GetNumPlayers() >= ExpectedMatchPlayers)
	{
		ErrorMessage = TEXT("The match is full.");
		return;
	}

	PendingValidatedJoinClaims.Add(Ticket, MoveTemp(Claim));
}

FString AMultiplayTestGameMode::InitNewPlayer(
	APlayerController* NewPlayerController,
	const FUniqueNetIdRepl& UniqueId,
	const FString& Options,
	const FString& Portal)
{
	AMultiplayTestPlayerController* MultiplayController = Cast<AMultiplayTestPlayerController>(NewPlayerController);
	FString LobbyRole;
	const FString Ticket = UGameplayStatics::ParseOption(Options, TEXT("GameTicket"));
	if (FProjectProject01ValidatedJoinClaim* Claim = PendingValidatedJoinClaims.Find(Ticket))
	{
		LobbyRole = Claim->Role;
		if (IsValid(MultiplayController))
		{
			MultiplayController->SetAuthenticatedLobbyIdentity(*Claim);
		}
		PendingValidatedJoinClaims.Remove(Ticket);
	}
	else if (!bRequireGameJoinTicket || GetNetMode() == NM_Standalone)
	{
		// 티켓 검증을 명시적으로 끈 로컬 호환 모드에서만 과거 URL 역할 옵션을 허용한다.
		LobbyRole = UGameplayStatics::ParseOption(Options, TEXT("LobbyRole"));
	}
	if (IsValid(MultiplayController) &&
		(LobbyRole.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase) ||
		 LobbyRole.Equals(TEXT("Survivor"), ESearchCase::IgnoreCase)))
	{
		ExplicitRoleControllers.Add(MultiplayController);
		if (LobbyRole.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase))
		{
			RequestedMannequinControllers.Add(MultiplayController);
		}
		UE_LOG(LogProjectProject01Multiplayer, Log,
			TEXT("%s received validated lobby role %s."), *GetNameSafe(MultiplayController), *LobbyRole);
	}
	return Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
}

void AMultiplayTestGameMode::PostLogin(APlayerController* NewPlayer)
{
	AMultiplayTestPlayerController* NewMultiplayController = Cast<AMultiplayTestPlayerController>(NewPlayer);
	const bool bHasExplicitRole = IsValid(NewMultiplayController) && ExplicitRoleControllers.Contains(NewMultiplayController);
	const bool bRequestedMannequin = IsValid(NewMultiplayController) && RequestedMannequinControllers.Contains(NewMultiplayController);
	if (IsValid(NewMultiplayController) && !MannequinController.IsValid() &&
		(bRequestedMannequin || !bHasExplicitRole))
	{
		MannequinController = NewMultiplayController;
		UE_LOG(LogProjectProject01Multiplayer, Log,
			TEXT("%s was assigned as the mannequin controller (%s)."), *NewMultiplayController->GetName(),
			bRequestedMannequin ? TEXT("lobby role") : TEXT("legacy first-player fallback"));
	}
	else if (!IsValid(NewMultiplayController))
	{
		UE_LOG(LogProjectProject01Multiplayer, Error,
			TEXT("MultiplayTest requires AMultiplayTestPlayerController, but %s connected."), *GetNameSafe(NewPlayer));
	}

	Super::PostLogin(NewPlayer);

	if (IsValid(NewMultiplayController))
	{
		if (const FMultiplayTestMatchResult* CompletedResult = CompletedResultsByUserId.Find(
			NewMultiplayController->GetAuthenticatedUserId()))
		{
			NewMultiplayController->SetServerMatchState(EMultiplayTestMatchPhase::Results,
				CompletedResult->Role == TEXT("Survivor") ? EMultiplayTestSurvivorState::Eliminated : EMultiplayTestSurvivorState::NotApplicable);
			NewMultiplayController->DeliverMatchResultToOwner(*CompletedResult);
			return;
		}
		if (RestoreDisconnectedMatchPlayer(NewMultiplayController))
		{
			return;
		}
	}

	// 마네킹 역할은 Pawn을 빙의하지 않고 시작한다. 서버가 유효한 0~9 슬롯 중 하나를
	// 무작위로 골라 시점만 배정하며, 해당 마네킹의 AIController는 R 입력 전까지 유지된다.
	if (IsMannequinController(NewMultiplayController))
	{
		AssignRandomInitialMannequinView(NewMultiplayController);
	}

	RegisterMatchPlayer(NewMultiplayController);
	TryStartAuthoritativeMatch();
}

void AMultiplayTestGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	const AMultiplayTestPlayerController* MultiplayController =
		Cast<AMultiplayTestPlayerController>(NewPlayer);
	if (IsMannequinController(MultiplayController))
	{
		// 마네킹 역할은 일반 생존자 Pawn을 생성하지 않지만, OnlySpectator로 만들지도 않는다.
		// OnlySpectator는 APlayerController::OnPossess에서 이후 R 빙의를 거부하므로 시점 전용 대기만 수행한다.
		return;
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AMultiplayTestGameMode::Logout(AController* Exiting)
{
	AMultiplayTestPlayerController* ExitingMultiplayController = Cast<AMultiplayTestPlayerController>(Exiting);
	FMultiplayTestServerPlayerRecord LeavingRecord;
	bool bHadMatchRecord = false;
	if (FMultiplayTestServerPlayerRecord* Record = MatchPlayerRecords.Find(ExitingMultiplayController))
	{
		SnapshotPlayerRecord(ExitingMultiplayController, *Record);
		LeavingRecord = *Record;
		bHadMatchRecord = true;
	}
	RequestedMannequinControllers.Remove(ExitingMultiplayController);
	ExplicitRoleControllers.Remove(ExitingMultiplayController);
	if (IsMannequinController(ExitingMultiplayController))
	{
		if (AMannequinAICharacter* ControlledMannequin = Cast<AMannequinAICharacter>(ExitingMultiplayController->GetPawn());
			IsValid(ControlledMannequin))
		{
			ExitingMultiplayController->UnPossess();
			QueueDefaultAIControllerRestore(ControlledMannequin);
		}

		MannequinController.Reset();
		UE_LOG(LogProjectProject01Multiplayer, Log, TEXT("The mannequin controller disconnected."));
	}

	if (bHadMatchRecord && AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress &&
		!LeavingRecord.bForfeited && !ExitingMultiplayController->bVoluntaryExitDeclared)
	{
		LeavingRecord.bDisconnected = true;
		LeavingRecord.ReconnectDeadlineWorldSeconds = GetWorld()->GetTimeSeconds() + ReconnectGraceSeconds;
		DisconnectedMatchRecords.Add(LeavingRecord.UserId, LeavingRecord);
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("%s disconnected unexpectedly; reconnect is allowed for %.1f seconds."),
			*LeavingRecord.UserId, ReconnectGraceSeconds);
	}
	else if (bHadMatchRecord && LeavingRecord.bForfeited && !LeavingRecord.UserId.IsEmpty())
	{
		DisconnectedMatchRecords.Add(LeavingRecord.UserId, LeavingRecord);
	}
	if (AuthoritativeMatchPhase != EMultiplayTestMatchPhase::Ending &&
		AuthoritativeMatchPhase != EMultiplayTestMatchPhase::Results)
	{
		MatchPlayerRecords.Remove(ExitingMultiplayController);
		ResultVerificationStatus.Remove(ExitingMultiplayController);
	}

	Super::Logout(Exiting);

	// 방에 남은 모든 사용자가 복귀한 뒤 다음 경기를 시작하면 이전 경기의 Actor 상태가
	// 남지 않도록, 마지막 접속자가 빠진 데디케이티드 서버 월드를 초기 맵으로 다시 연다.
	// 짧은 지연은 같은 프레임의 Logout/PlayerArray 정리가 끝난 뒤 인원 수를 확인하기 위함이다.
	if (UWorld* World = GetWorld(); IsValid(World) && World->GetNetMode() == NM_DedicatedServer)
	{
		World->GetTimerManager().ClearTimer(EmptyDedicatedServerResetTimer);
		World->GetTimerManager().SetTimer(
			EmptyDedicatedServerResetTimer,
			this,
			&AMultiplayTestGameMode::ResetDedicatedServerWorldIfEmpty,
			1.0f,
			false);
	}
}

void AMultiplayTestGameMode::DeclareVoluntaryExit(AMultiplayTestPlayerController* RequestingController)
{
	if (!HasAuthority() || !IsValid(RequestingController) ||
		AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
	{
		return;
	}
	if (FMultiplayTestServerPlayerRecord* Record = MatchPlayerRecords.Find(RequestingController))
	{
		SnapshotPlayerRecord(RequestingController, *Record);
		ApplyForfeit(*Record, TEXT("VoluntaryExit"));
		RequestingController->SetServerMatchState(AuthoritativeMatchPhase, Record->SurvivorState);
		if (Record->Role == TEXT("Survivor") &&
			AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress)
		{
			EnterSurvivorSpectator(RequestingController);
		}
	}
}

void AMultiplayTestGameMode::BindEscapeTriggers()
{
	if (!HasAuthority() || !IsValid(GetWorld()))
	{
		return;
	}

	int32 BoundTriggerCount = 0;
	for (TActorIterator<ATriggerBox> It(GetWorld()); It; ++It)
	{
		ATriggerBox* Trigger = *It;
		if (!IsValid(Trigger) || !Trigger->ActorHasTag(EscapeTriggerActorTag))
		{
			continue;
		}

		Trigger->OnActorBeginOverlap.AddUniqueDynamic(
			this, &AMultiplayTestGameMode::HandleEscapeTriggerBeginOverlap);
		++BoundTriggerCount;
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Bound %d escape TriggerBox actor(s) tagged %s."),
		BoundTriggerCount, *EscapeTriggerActorTag.ToString());
}

void AMultiplayTestGameMode::HandleEscapeTriggerBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	MarkSurvivorEscaped(Cast<APlayerCharacter>(OtherActor));
}

void AMultiplayTestGameMode::RegisterMatchPlayer(AMultiplayTestPlayerController* Controller)
{
	if (!HasAuthority() || !IsValid(Controller) || MatchPlayerRecords.Contains(Controller))
	{
		return;
	}

	FMultiplayTestServerPlayerRecord Record;
	Record.UserId = Controller->GetAuthenticatedUserId();
	Record.MatchId = Controller->GetAuthenticatedMatchId();
	// 티켓 검증을 생략하는 Standalone/로컬 PIE에서도 통신 기능을 서로 격리된
	// 하나의 테스트 매치로 묶는다. 실제 티켓 기반 접속에서는 인증된 값을 그대로 쓴다.
	if (Record.UserId.IsEmpty() && (GetNetMode() == NM_Standalone || !bRequireGameJoinTicket))
	{
		Record.UserId = FString::Printf(TEXT("LocalPIE-%d"), Controller->GetUniqueID());
	}
	if (Record.MatchId.IsEmpty() && (GetNetMode() == NM_Standalone || !bRequireGameJoinTicket))
	{
		if (AuthoritativeMatchId.IsEmpty())
		{
			AuthoritativeMatchId = FString::Printf(TEXT("LocalPIE-%s"), *GetWorld()->GetMapName());
		}
		Record.MatchId = AuthoritativeMatchId;
	}
	Record.Role = IsMannequinController(Controller) ? TEXT("Mannequin") : TEXT("Survivor");
	Record.SurvivorState = Record.Role == TEXT("Survivor")
		? EMultiplayTestSurvivorState::Active
		: EMultiplayTestSurvivorState::NotApplicable;
	Record.SurvivorPawn = Record.Role == TEXT("Survivor")
		? Cast<APlayerCharacter>(Controller->GetPawn())
		: nullptr;

	if (AuthoritativeMatchId.IsEmpty() && !Record.MatchId.IsEmpty())
	{
		AuthoritativeMatchId = Record.MatchId;
	}
	MatchPlayerRecords.Add(Controller, MoveTemp(Record));
	Controller->SetServerMatchState(
		EMultiplayTestMatchPhase::RoleAssignment,
		MatchPlayerRecords.FindChecked(Controller).SurvivorState);
	Controller->ClientConfigureSurvivorCommunication(
		MatchPlayerRecords.FindChecked(Controller).UserId,
		MatchPlayerRecords.FindChecked(Controller).MatchId,
		MatchPlayerRecords.FindChecked(Controller).Role);
}

bool AMultiplayTestGameMode::RestoreDisconnectedMatchPlayer(AMultiplayTestPlayerController* Controller)
{
	if (!HasAuthority() || !IsValid(Controller) || Controller->GetAuthenticatedUserId().IsEmpty())
	{
		return false;
	}
	FMultiplayTestServerPlayerRecord* Saved = DisconnectedMatchRecords.Find(Controller->GetAuthenticatedUserId());
	if (Saved == nullptr || Saved->MatchId != Controller->GetAuthenticatedMatchId() || Saved->bForfeited)
	{
		return false;
	}

	FMultiplayTestServerPlayerRecord Restored = *Saved;
	DisconnectedMatchRecords.Remove(Controller->GetAuthenticatedUserId());
	Restored.bDisconnected = false;
	Restored.ReconnectDeadlineWorldSeconds = 0.0;
	if (Restored.Role == TEXT("Survivor"))
	{
		APlayerCharacter* Survivor = Cast<APlayerCharacter>(Controller->GetPawn());
		Restored.SurvivorPawn = Survivor;
		if (IsValid(Survivor))
		{
			Survivor->SetActorTransform(Restored.LastPawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
			Survivor->SetRemainingDeathCountForGameMode(Restored.RemainingDeathCount);
			if (Restored.SurvivorState != EMultiplayTestSurvivorState::Active)
			{
				if (UCharacterMovementComponent* Movement = Survivor->GetCharacterMovement(); IsValid(Movement))
				{
					Movement->StopMovementImmediately();
					Movement->DisableMovement();
				}
				Survivor->SetActorEnableCollision(false);
				Survivor->SetActorHiddenInGame(true);
			}
		}
	}
	MatchPlayerRecords.Add(Controller, Restored);
	Controller->SetServerMatchState(AuthoritativeMatchPhase, Restored.SurvivorState);
	Controller->ClientConfigureSurvivorCommunication(
		Restored.UserId, Restored.MatchId, Restored.Role);

	if (Restored.Role == TEXT("Mannequin"))
	{
		MannequinController = Controller;
		if (Restored.ViewedMannequinSlot >= 0)
		{
			TryPossessMannequin(Controller, Restored.ViewedMannequinSlot);
			if (Restored.bWasManualMannequinControl)
			{
				TryEnableMannequinManualControl(Controller);
			}
		}
	}
	else if (Restored.SurvivorState == EMultiplayTestSurvivorState::Eliminated ||
		Restored.SurvivorState == EMultiplayTestSurvivorState::Escaped)
	{
		APlayerCharacter* Target = FindLivingSurvivorSpectatorTarget(Controller, 1);
		Controller->EnterSurvivorSpectator(
			Target, Restored.SurvivorState == EMultiplayTestSurvivorState::Escaped && IsValid(Target));
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Restored reconnecting %s player %s for match %s."),
		*Restored.Role, *Restored.UserId, *Restored.MatchId);
	return true;
}

void AMultiplayTestGameMode::SnapshotPlayerRecord(
	AMultiplayTestPlayerController* Controller,
	FMultiplayTestServerPlayerRecord& Record)
{
	if (!IsValid(Controller))
	{
		return;
	}
	if (Record.Role == TEXT("Survivor"))
	{
		APlayerCharacter* Survivor = Cast<APlayerCharacter>(Controller->GetPawn());
		if (!IsValid(Survivor))
		{
			Survivor = Record.SurvivorPawn.Get();
		}
		if (IsValid(Survivor))
		{
			Record.LastPawnTransform = Survivor->GetActorTransform();
			Record.RemainingDeathCount = Survivor->GetRemainingDeathCount();
		}
	}
	else
	{
		if (AMannequinAICharacter* Viewed = Controller->GetViewedMannequin(); IsValid(Viewed))
		{
			Record.ViewedMannequinSlot = Viewed->GetControlSlot();
		}
		Record.bWasManualMannequinControl = Cast<AMannequinAICharacter>(Controller->GetPawn()) != nullptr;
	}
}

void AMultiplayTestGameMode::ApplyForfeit(FMultiplayTestServerPlayerRecord& Record, const FString& Reason)
{
	if (Record.bForfeited || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
	{
		return;
	}
	Record.bForfeited = true;
	Record.bDisconnected = false;
	UE_LOG(LogProjectProject01Multiplayer, Warning,
		TEXT("Player %s (%s) forfeited: %s."), *Record.UserId, *Record.Role, *Reason);
	if (Reason == TEXT("ReconnectTimeout"))
	{
		if (UProjectProject01GameInstance* GameInstance = GetGameInstance<UProjectProject01GameInstance>();
			IsValid(GameInstance))
		{
			GameInstance->NotifyAuthoritativePlayerForfeit(
				AuthoritativeMatchId, Record.UserId, Record.Role);
		}
	}

	if (Record.Role == TEXT("Mannequin"))
	{
		bMatchEndedByForfeit = true;
		bSurvivorVictoryByMannequinForfeit = true;
		FinishAuthoritativeMatch(TEXT("MannequinForfeit"));
		return;
	}

	Record.SurvivorState = EMultiplayTestSurvivorState::Eliminated;
	if (APlayerCharacter* Survivor = Record.SurvivorPawn.Get(); IsValid(Survivor))
	{
		if (UCharacterMovementComponent* Movement = Survivor->GetCharacterMovement(); IsValid(Movement))
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		Survivor->SetActorEnableCollision(false);
		Survivor->SetActorHiddenInGame(true);
	}
	CheckForMatchCompletion();
}

void AMultiplayTestGameMode::ProcessReconnectTimeouts()
{
	if (AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress || !IsValid(GetWorld()))
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (TPair<FString, FMultiplayTestServerPlayerRecord>& Pair : DisconnectedMatchRecords)
	{
		if (!Pair.Value.bForfeited && Pair.Value.ReconnectDeadlineWorldSeconds > 0.0 &&
			Now >= Pair.Value.ReconnectDeadlineWorldSeconds)
		{
			ApplyForfeit(Pair.Value, TEXT("ReconnectTimeout"));
			if (AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
			{
				break;
			}
		}
	}
}

void AMultiplayTestGameMode::TryStartAuthoritativeMatch()
{
	if (!HasAuthority() || AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress ||
		AuthoritativeMatchPhase == EMultiplayTestMatchPhase::Ending ||
		AuthoritativeMatchPhase == EMultiplayTestMatchPhase::Results ||
		MatchPlayerRecords.Num() < ExpectedMatchPlayers)
	{
		return;
	}

	int32 MannequinCount = 0;
	int32 SurvivorCount = 0;
	FString ValidatedMatchId;
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		const FMultiplayTestServerPlayerRecord& Record = Pair.Value;
		MannequinCount += Record.Role == TEXT("Mannequin") ? 1 : 0;
		SurvivorCount += Record.Role == TEXT("Survivor") ? 1 : 0;
		if (!Record.MatchId.IsEmpty())
		{
			if (ValidatedMatchId.IsEmpty())
			{
				ValidatedMatchId = Record.MatchId;
			}
			else if (ValidatedMatchId != Record.MatchId)
			{
				UE_LOG(LogProjectProject01Multiplayer, Error,
					TEXT("Cannot start: authenticated players have different MatchId values."));
				return;
			}
		}
	}

	if (MannequinCount != 1 || SurvivorCount != ExpectedMatchPlayers - 1)
	{
		return;
	}

	AuthoritativeMatchId = ValidatedMatchId.IsEmpty()
		? FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)
		: ValidatedMatchId;
	MatchStartWorldSeconds = GetWorld()->GetTimeSeconds();
	MatchEndWorldSeconds = 0.0;
	bFinishCommitted = false;
	bMatchEndedByForfeit = false;
	bSurvivorVictoryByMannequinForfeit = false;
	DisconnectedMatchRecords.Reset();
	CompletedResultsByUserId.Reset();
	ResultVerificationStatus.Reset();
	SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::InProgress);
	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Authoritative match %s started with %d player(s)."),
		*AuthoritativeMatchId, MatchPlayerRecords.Num());
}

void AMultiplayTestGameMode::SetAuthoritativeMatchPhase(const EMultiplayTestMatchPhase NewPhase)
{
	AuthoritativeMatchPhase = NewPhase;
	for (TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (AMultiplayTestPlayerController* Controller = Pair.Key.Get(); IsValid(Controller))
		{
			Controller->SetServerMatchState(NewPhase, Pair.Value.SurvivorState);
		}
	}
}

FMultiplayTestServerPlayerRecord* AMultiplayTestGameMode::FindSurvivorRecord(APlayerCharacter* Survivor)
{
	for (TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		AMultiplayTestPlayerController* Controller = Pair.Key.Get();
		if (Pair.Value.Role == TEXT("Survivor") &&
			(Pair.Value.SurvivorPawn.Get() == Survivor ||
			 (IsValid(Controller) && Controller->GetPawn() == Survivor)))
		{
			Pair.Value.SurvivorPawn = Survivor;
			return &Pair.Value;
		}
	}
	return nullptr;
}

const FMultiplayTestServerPlayerRecord* AMultiplayTestGameMode::FindSurvivorRecord(
	const APlayerCharacter* Survivor) const
{
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		const AMultiplayTestPlayerController* Controller = Pair.Key.Get();
		if (Pair.Value.Role == TEXT("Survivor") &&
			(Pair.Value.SurvivorPawn.Get() == Survivor ||
			 (IsValid(Controller) && Controller->GetPawn() == Survivor)))
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

APlayerCharacter* AMultiplayTestGameMode::FindLivingSurvivorSpectatorTarget(
	const AMultiplayTestPlayerController* RequestingController,
	const int32 Direction) const
{
	TArray<APlayerCharacter*> LivingSurvivors;
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (Pair.Key.Get() == RequestingController || Pair.Value.Role != TEXT("Survivor") ||
			Pair.Value.SurvivorState != EMultiplayTestSurvivorState::Active)
		{
			continue;
		}
		APlayerCharacter* Candidate = Pair.Value.SurvivorPawn.Get();
		if (!IsValid(Candidate) && Pair.Key.IsValid())
		{
			Candidate = Cast<APlayerCharacter>(Pair.Key->GetPawn());
		}
		if (IsValid(Candidate))
		{
			LivingSurvivors.Add(Candidate);
		}
	}
	if (LivingSurvivors.IsEmpty())
	{
		return nullptr;
	}

	const AActor* CurrentTarget = IsValid(RequestingController) ? RequestingController->GetViewTarget() : nullptr;
	int32 CurrentIndex = LivingSurvivors.IndexOfByKey(CurrentTarget);
	if (CurrentIndex == INDEX_NONE)
	{
		return LivingSurvivors[0];
	}
	const int32 Step = Direction < 0 ? -1 : 1;
	return LivingSurvivors[(CurrentIndex + Step + LivingSurvivors.Num()) % LivingSurvivors.Num()];
}

void AMultiplayTestGameMode::EnterSurvivorSpectator(AMultiplayTestPlayerController* Controller)
{
	if (!HasAuthority() || !IsValid(Controller))
	{
		return;
	}
	Controller->EnterSurvivorSpectator(FindLivingSurvivorSpectatorTarget(Controller, 1));
}

void AMultiplayTestGameMode::CycleSurvivorSpectator(
	AMultiplayTestPlayerController* RequestingController,
	const int32 Direction)
{
	if (!HasAuthority() || !IsValid(RequestingController))
	{
		return;
	}
	const FMultiplayTestServerPlayerRecord* Record = MatchPlayerRecords.Find(RequestingController);
	if (Record == nullptr || Record->Role != TEXT("Survivor") ||
		(Record->SurvivorState != EMultiplayTestSurvivorState::Eliminated &&
		 Record->SurvivorState != EMultiplayTestSurvivorState::Escaped))
	{
		return;
	}
	RequestingController->EnterSurvivorSpectator(
		FindLivingSurvivorSpectatorTarget(RequestingController, Direction));
}

bool AMultiplayTestGameMode::TryBroadcastSurvivorPing(
	AMultiplayTestPlayerController* RequestingController,
	const EProjectProject01PingType Type,
	const FVector& Location)
{
	if (!HasAuthority() || !IsValid(RequestingController) || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
	{
		return false;
	}
	const FMultiplayTestServerPlayerRecord* SenderRecord = MatchPlayerRecords.Find(RequestingController);
	if (SenderRecord == nullptr || !SenderRecord->Role.Equals(TEXT("Survivor"), ESearchCase::IgnoreCase) ||
		SenderRecord->SurvivorState != EMultiplayTestSurvivorState::Active || !IsValid(RequestingController->GetPawn()) ||
		FVector::DistSquared(RequestingController->GetPawn()->GetActorLocation(), Location) > FMath::Square(3100.0f))
	{
		return false;
	}
	const FString SenderName = IsValid(RequestingController->PlayerState)
		? RequestingController->PlayerState->GetPlayerName() : TEXT("생존자");
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		AMultiplayTestPlayerController* Target = Pair.Key.Get();
		if (IsValid(Target) && Pair.Value.Role.Equals(TEXT("Survivor"), ESearchCase::IgnoreCase) &&
			Pair.Value.SurvivorState == EMultiplayTestSurvivorState::Active)
		{
			Target->ClientReceiveTeamPing(Type, Location, SenderName);
		}
	}
	return true;
}

void AMultiplayTestGameMode::NotifyExistingMannequinCatch(APlayerCharacter* Survivor)
{
	if (!HasAuthority() || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress ||
		!IsValid(Survivor) || !Survivor->IsGameOver())
	{
		return;
	}

	FMultiplayTestServerPlayerRecord* SurvivorRecord = FindSurvivorRecord(Survivor);
	if (SurvivorRecord == nullptr || SurvivorRecord->SurvivorState != EMultiplayTestSurvivorState::Active)
	{
		return;
	}

	SurvivorRecord->SurvivorState = EMultiplayTestSurvivorState::Eliminated;
	const double CaptureSeconds = GetElapsedMatchSeconds();
	if (UCharacterMovementComponent* Movement = Survivor->GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	Survivor->SetActorEnableCollision(false);
	Survivor->SetActorHiddenInGame(true);
	for (TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (Pair.Value.Role == TEXT("Mannequin"))
		{
			++Pair.Value.CaptureCount;
			if (Pair.Value.FirstCaptureSeconds <= 0.0)
			{
				Pair.Value.FirstCaptureSeconds = CaptureSeconds;
			}
		}
		if (Pair.Value.SurvivorPawn.Get() == Survivor)
		{
			if (AMultiplayTestPlayerController* Controller = Pair.Key.Get(); IsValid(Controller))
			{
				Controller->SetServerMatchState(AuthoritativeMatchPhase, SurvivorRecord->SurvivorState);
			}
		}
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Survivor %s was authoritatively eliminated at %.2f seconds."),
		*GetNameSafe(Survivor), CaptureSeconds);
	if (AMultiplayTestPlayerController* EliminatedController = Cast<AMultiplayTestPlayerController>(Survivor->GetController());
		IsValid(EliminatedController))
	{
		// 탈락 순간에는 현재 카메라를 바로 잘라 바꾸지 않고 암전한 뒤
		// 살아 있는 생존자 관전 시점으로 전환한다. 마지막 생존자라 관전
		// 대상이 없으면 암전을 유지하고 이어지는 결과 화면을 표시한다.
		EliminatedController->EnterSurvivorSpectator(
			FindLivingSurvivorSpectatorTarget(EliminatedController, 1), true);
	}
	CheckForMatchCompletion();
}

bool AMultiplayTestGameMode::MarkSurvivorEscaped(APlayerCharacter* Survivor)
{
	if (!HasAuthority() || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress || !IsValid(Survivor))
	{
		return false;
	}

	FMultiplayTestServerPlayerRecord* Record = FindSurvivorRecord(Survivor);
	if (Record == nullptr || Record->SurvivorState != EMultiplayTestSurvivorState::Active)
	{
		return false;
	}

	Record->SurvivorState = EMultiplayTestSurvivorState::Escaped;
	Record->EscapeSeconds = GetElapsedMatchSeconds();
	if (UCharacterMovementComponent* Movement = Survivor->GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	Survivor->SetActorEnableCollision(false);
	Survivor->SetActorHiddenInGame(true);
	if (AMultiplayTestPlayerController* Controller = Cast<AMultiplayTestPlayerController>(Survivor->GetController());
		IsValid(Controller))
	{
		Controller->SetServerMatchState(AuthoritativeMatchPhase, Record->SurvivorState);
		if (APlayerCharacter* LivingSurvivor = FindLivingSurvivorSpectatorTarget(Controller, 1);
			IsValid(LivingSurvivor))
		{
			Controller->EnterSurvivorSpectator(LivingSurvivor, true);
		}
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Survivor %s escaped at %.2f seconds."), *GetNameSafe(Survivor), Record->EscapeSeconds);
	CheckForMatchCompletion();
	return true;
}

void AMultiplayTestGameMode::CheckForMatchCompletion()
{
	TArray<EMultiplayTestSurvivorState> SurvivorStates;
	bool bAnySurvivorForfeited = false;
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (Pair.Value.Role == TEXT("Survivor"))
		{
			SurvivorStates.Add(Pair.Value.SurvivorState);
			bAnySurvivorForfeited |= Pair.Value.bForfeited;
		}
	}
	for (const TPair<FString, FMultiplayTestServerPlayerRecord>& Pair : DisconnectedMatchRecords)
	{
		if (Pair.Value.Role == TEXT("Survivor"))
		{
			SurvivorStates.Add(Pair.Value.SurvivorState);
			bAnySurvivorForfeited |= Pair.Value.bForfeited;
		}
	}

	const FMatchResolutionSummary Summary = EvaluateMatchResolution(SurvivorStates);
	if (Summary.IsComplete())
	{
		if (Summary.DidMannequinWin())
		{
			bMatchEndedByForfeit = bAnySurvivorForfeited;
			for (TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
			{
				if (Pair.Value.Role == TEXT("Mannequin"))
				{
					Pair.Value.AllCapturedSeconds = GetElapsedMatchSeconds();
				}
			}
			for (TPair<FString, FMultiplayTestServerPlayerRecord>& Pair : DisconnectedMatchRecords)
			{
				if (Pair.Value.Role == TEXT("Mannequin"))
				{
					Pair.Value.AllCapturedSeconds = GetElapsedMatchSeconds();
				}
			}
		}
		FinishAuthoritativeMatch(Summary.DidMannequinWin()
			? TEXT("AllSurvivorsEliminated") : TEXT("AllSurvivorsResolved"));
	}
}

double AMultiplayTestGameMode::GetElapsedMatchSeconds() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World) || MatchStartWorldSeconds <= 0.0)
	{
		return 0.001;
	}
	const double EndSeconds = MatchEndWorldSeconds > 0.0
		? MatchEndWorldSeconds
		: static_cast<double>(World->GetTimeSeconds());
	return FMath::Max(0.001, EndSeconds - MatchStartWorldSeconds);
}

void AMultiplayTestGameMode::FinishAuthoritativeMatch(const FString& Reason)
{
	if (!HasAuthority() || bFinishCommitted || AuthoritativeMatchPhase != EMultiplayTestMatchPhase::InProgress)
	{
		return;
	}

	bFinishCommitted = true;
	MatchEndWorldSeconds = GetWorld()->GetTimeSeconds();
	SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Ending);
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		if (AMultiplayTestPlayerController* Controller = Pair.Key.Get(); IsValid(Controller))
		{
			if (APawn* Pawn = Controller->GetPawn(); IsValid(Pawn))
			{
				if (UCharacterMovementComponent* Movement = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent());
					IsValid(Movement))
				{
					Movement->StopMovementImmediately();
					Movement->DisableMovement();
				}
			}
		}
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("Authoritative match %s finished once: %s."), *AuthoritativeMatchId, *Reason);
	SubmitAuthoritativeResults();
}

FMultiplayTestMatchResult AMultiplayTestGameMode::BuildFinalResult(
	const AMultiplayTestPlayerController* Controller,
	const FMultiplayTestServerPlayerRecord& Record) const
{
	FMultiplayTestMatchResult Result;
	Result.MatchId = AuthoritativeMatchId;
	Result.Role = Record.Role;
	Result.MatchDurationSeconds = GetElapsedMatchSeconds();
	Result.CaptureCount = Record.CaptureCount;
	Result.FirstCaptureSeconds = Record.FirstCaptureSeconds;
	Result.AllCapturedSeconds = Record.AllCapturedSeconds;
	Result.RescueCount = Record.RescueCount;
	Result.EscapeSeconds = Record.EscapeSeconds;
	if (Record.Role == TEXT("Mannequin"))
	{
		Result.bSuccess = !Record.bForfeited && !bSurvivorVictoryByMannequinForfeit && Record.AllCapturedSeconds > 0.0;
		Result.Outcome = Record.bForfeited ? TEXT("Forfeited")
			: Result.bSuccess && bMatchEndedByForfeit ? TEXT("SurvivorForfeitVictory")
			: Result.bSuccess ? TEXT("MannequinVictory") : TEXT("SurvivorVictory");
	}
	else
	{
		Result.bSuccess = !Record.bForfeited &&
			(Record.SurvivorState == EMultiplayTestSurvivorState::Escaped || bSurvivorVictoryByMannequinForfeit);
		Result.Outcome = Record.bForfeited ? TEXT("Forfeited")
			: bSurvivorVictoryByMannequinForfeit ? TEXT("OpponentForfeit")
			: Result.bSuccess
			? TEXT("Escaped")
			: Record.SurvivorState == EMultiplayTestSurvivorState::Eliminated
				? TEXT("Eliminated")
				: TEXT("NotEscaped");
	}
	if (!bMatchEndedByForfeit && !Record.bForfeited)
	{
		if (const bool* Verification = ResultVerificationStatus.Find(Controller))
		{
			Result.bLeaderboardVerificationReady = *Verification;
		}
	}
	return Result;
}

void AMultiplayTestGameMode::SubmitAuthoritativeResults()
{
	if (bMatchEndedByForfeit)
	{
		PendingResultSubmissions = 0;
		PresentFinalResults();
		return;
	}
	UProjectProject01GameInstance* GameInstance = GetGameInstance<UProjectProject01GameInstance>();
	ResultVerificationStatus.Reset();
	struct FPendingAuthoritativeSubmission
	{
		TWeakObjectPtr<AMultiplayTestPlayerController> Controller;
		FString UserId;
		FMultiplayTestMatchResult Result;
	};
	TArray<FPendingAuthoritativeSubmission> Submissions;

	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		AMultiplayTestPlayerController* Controller = Pair.Key.Get();
		if (!IsValid(Controller) || Pair.Value.bForfeited)
		{
			continue;
		}
		ResultVerificationStatus.Add(Controller, false);
		if (!IsValid(GameInstance) || Pair.Value.UserId.IsEmpty() || AuthoritativeMatchId.IsEmpty())
		{
			continue;
		}

		FPendingAuthoritativeSubmission& Submission = Submissions.AddDefaulted_GetRef();
		Submission.Controller = Controller;
		Submission.UserId = Pair.Value.UserId;
		Submission.Result = BuildFinalResult(Controller, Pair.Value);
	}

	PendingResultSubmissions = Submissions.Num();
	if (PendingResultSubmissions == 0)
	{
		PresentFinalResults();
		return;
	}

	for (const FPendingAuthoritativeSubmission& Submission : Submissions)
	{
		const TWeakObjectPtr<AMultiplayTestGameMode> WeakThis(this);
		const TWeakObjectPtr<AMultiplayTestPlayerController> WeakController = Submission.Controller;
		GameInstance->SubmitAuthoritativeMatchResultWithCallback(
			Submission.Result.MatchId,
			Submission.UserId,
			Submission.Result.Role,
			Submission.Result.bSuccess,
			Submission.Result.CaptureCount,
			Submission.Result.FirstCaptureSeconds,
			Submission.Result.AllCapturedSeconds,
			Submission.Result.RescueCount,
			Submission.Result.EscapeSeconds,
			[WeakThis, WeakController](const bool bSucceeded)
			{
				if (AMultiplayTestGameMode* GameMode = WeakThis.Get(); IsValid(GameMode))
				{
					GameMode->HandleResultSubmissionComplete(WeakController, bSucceeded);
				}
			});
	}
}

void AMultiplayTestGameMode::HandleResultSubmissionComplete(
	const TWeakObjectPtr<AMultiplayTestPlayerController> Controller,
	const bool bSucceeded)
{
	if (ResultVerificationStatus.Contains(Controller))
	{
		ResultVerificationStatus.FindChecked(Controller) = bSucceeded;
	}
	PendingResultSubmissions = FMath::Max(0, PendingResultSubmissions - 1);
	if (PendingResultSubmissions == 0)
	{
		PresentFinalResults();
	}
}

void AMultiplayTestGameMode::PresentFinalResults()
{
	if (AuthoritativeMatchPhase == EMultiplayTestMatchPhase::Results)
	{
		return;
	}
	SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Results);
	for (const TPair<TWeakObjectPtr<AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord>& Pair : MatchPlayerRecords)
	{
		const FMultiplayTestMatchResult Result = BuildFinalResult(Pair.Key.Get(), Pair.Value);
		if (!Pair.Value.UserId.IsEmpty())
		{
			CompletedResultsByUserId.Add(Pair.Value.UserId, Result);
		}
		if (AMultiplayTestPlayerController* Controller = Pair.Key.Get(); IsValid(Controller))
		{
			Controller->DeliverMatchResultToOwner(Result);
		}
	}
	for (const TPair<FString, FMultiplayTestServerPlayerRecord>& Pair : DisconnectedMatchRecords)
	{
		CompletedResultsByUserId.Add(Pair.Key, BuildFinalResult(nullptr, Pair.Value));
	}
}

void AMultiplayTestGameMode::ResetDedicatedServerWorldIfEmpty()
{
	UWorld* World = GetWorld();
	if (!ensureMsgf(IsValid(World), TEXT("Dedicated server reset requires a valid world.")) ||
		World->GetNetMode() != NM_DedicatedServer || GetNumPlayers() > 0)
	{
		return;
	}

	static const FString MultiplayTestMap = TEXT("/Game/MyProject/Level/MultiplayTest");
	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("No players remain. Resetting the dedicated server world to %s."), *MultiplayTestMap);
	if (!World->ServerTravel(MultiplayTestMap, true))
	{
		UE_LOG(LogProjectProject01Multiplayer, Error,
			TEXT("Failed to reset the dedicated server world to %s."), *MultiplayTestMap);
	}
}

void AMultiplayTestGameMode::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);

	if (APlayerCharacter* SurvivorPlayer = Cast<APlayerCharacter>(PlayerPawn))
	{
		// MultiplayTest에는 조력자가 없으므로 생존자는 추가 포획 허용 횟수 없이 시작한다.
		SurvivorPlayer->SetRemainingDeathCountForGameMode(0);
	}
}

bool AMultiplayTestGameMode::TryPossessMannequin(AMultiplayTestPlayerController* RequestingController, int32 Slot)
{
	if (!HasAuthority() || !IsValid(RequestingController) || !IsMannequinController(RequestingController))
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected mannequin slot request %d from %s."), Slot, *GetNameSafe(RequestingController));
		return false;
	}

	AMannequinAICharacter* TargetMannequin = FindMannequinBySlot(Slot);
	if (!IsValid(TargetMannequin))
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("No unique mannequin was found for control slot %d."), Slot);
		return false;
	}

	AMannequinAICharacter* PreviousViewedMannequin = RequestingController->GetViewedMannequin();
	if (PreviousViewedMannequin == TargetMannequin)
	{
		return true;
	}

	if (APlayerController* ExistingPlayerController = Cast<APlayerController>(TargetMannequin->GetController());
		IsValid(ExistingPlayerController) && ExistingPlayerController != RequestingController)
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected mannequin slot %d because %s is already player-controlled."), Slot, *GetNameSafe(TargetMannequin));
		return false;
	}

	AMannequinAICharacter* PreviousManuallyControlledMannequin = Cast<AMannequinAICharacter>(RequestingController->GetPawn());
	if (IsValid(PreviousManuallyControlledMannequin))
	{
		PreviousManuallyControlledMannequin->SetManualControlEnabled(false);
		PreviousManuallyControlledMannequin->ActivatePostPossessionCommand();
		RequestingController->UnPossess();
		UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(GetWorld(), EProjectProject01DiagnosticSeverity::Normal,
			TEXT("MannequinPlayerUnPossess"), FString::Printf(TEXT("Controller=%s; NextSlot=%d; Command=%s"),
				*GetNameSafe(RequestingController), Slot, *PreviousManuallyControlledMannequin->GetDiagnosticCommandState(GetWorld()->GetTimeSeconds())),
			PreviousManuallyControlledMannequin);
		QueueDefaultAIControllerRestore(PreviousManuallyControlledMannequin);
	}

	// 번호키 단계에서는 AIController를 유지하고, 플레이어는 시점만 공유한다.
	RequestingController->SetViewedMannequin(TargetMannequin);
	UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(GetWorld(), EProjectProject01DiagnosticSeverity::Normal,
		TEXT("MannequinViewSelectionChanged"), FString::Printf(TEXT("Controller=%s; Slot=%d"), *GetNameSafe(RequestingController), Slot), TargetMannequin);
	TargetMannequin->ForceNetUpdate();
	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("%s now views mannequin slot %d (%s) while its AI remains active."),
		*RequestingController->GetName(), Slot, *TargetMannequin->GetName());
	return true;
}

bool AMultiplayTestGameMode::TryEnableMannequinManualControl(AMultiplayTestPlayerController* RequestingController)
{
	if (!HasAuthority() || !IsValid(RequestingController) || !IsMannequinController(RequestingController))
	{
		return false;
	}

	AMannequinAICharacter* TargetMannequin = RequestingController->GetViewedMannequin();
	if (!IsValid(TargetMannequin))
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected manual-control request from %s because no viewed mannequin is available."),
			*GetNameSafe(RequestingController));
		return false;
	}

	if (APlayerController* ExistingPlayerController = Cast<APlayerController>(TargetMannequin->GetController());
		IsValid(ExistingPlayerController) && ExistingPlayerController != RequestingController)
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected manual control of %s because it is already player-controlled."), *GetNameSafe(TargetMannequin));
		return false;
	}

	if (RequestingController->GetPawn() == TargetMannequin)
	{
		TargetMannequin->SetManualControlEnabled(true);
		return true;
	}

	if (UCharacterMovementComponent* Movement = TargetMannequin->GetCharacterMovement(); IsValid(Movement))
	{
		Movement->StopMovementImmediately();
	}

	RequestingController->Possess(TargetMannequin);
	if (!ensureMsgf(RequestingController->GetPawn() == TargetMannequin,
		TEXT("Failed to enable manual control for mannequin %s."), *GetNameSafe(TargetMannequin)))
	{
		QueueDefaultAIControllerRestore(TargetMannequin);
		RequestingController->SetViewedMannequin(TargetMannequin);
		return false;
	}

	TargetMannequin->SetManualControlEnabled(true);
	UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(GetWorld(), EProjectProject01DiagnosticSeverity::Normal,
		TEXT("MannequinPlayerPossess"), FString::Printf(TEXT("Controller=%s; Slot=%d"), *GetNameSafe(RequestingController), TargetMannequin->GetControlSlot()), TargetMannequin);
	TargetMannequin->ForceNetUpdate();
	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("%s enabled manual control of mannequin %s."),
		*RequestingController->GetName(), *TargetMannequin->GetName());
	return true;
}

bool AMultiplayTestGameMode::TryQueuePostPossessionChaseCommand(AMultiplayTestPlayerController* RequestingController)
{
	if (!HasAuthority() || !IsValid(RequestingController) || !IsMannequinController(RequestingController))
	{
		return false;
	}

	AMannequinAICharacter* ManuallyControlledMannequin = Cast<AMannequinAICharacter>(RequestingController->GetPawn());
	if (!IsValid(ManuallyControlledMannequin) || !ManuallyControlledMannequin->IsManualControlEnabled())
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected post-possession chase command from %s because manual control is not active."),
			*GetNameSafe(RequestingController));
		return false;
	}

	const bool bQueued = ManuallyControlledMannequin->QueuePostPossessionChaseCommand();
	UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(GetWorld(), bQueued ? EProjectProject01DiagnosticSeverity::Normal : EProjectProject01DiagnosticSeverity::Warning,
		TEXT("MannequinCommandQueued"), FString::Printf(TEXT("Controller=%s; Queued=%s"), *GetNameSafe(RequestingController), bQueued ? TEXT("true") : TEXT("false")), ManuallyControlledMannequin);
	return bQueued;
}

void AMultiplayTestGameMode::QueueDefaultAIControllerRestore(AMannequinAICharacter* Mannequin) const
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || !IsValid(Mannequin))
	{
		return;
	}

	const TWeakObjectPtr<AMannequinAICharacter> WeakMannequin(Mannequin);
	World->GetTimerManager().SetTimerForNextTick([WeakMannequin]()
	{
		AMannequinAICharacter* RestoredMannequin = WeakMannequin.Get();
		if (!IsValid(RestoredMannequin) || !RestoredMannequin->HasAuthority())
		{
			return;
		}

		// 슬롯을 빠르게 전환해 이 마네킹을 다시 플레이어가 잡은 경우에는 AI를 덮어쓰지 않는다.
		if (IsValid(RestoredMannequin->GetController()))
		{
			return;
		}

		RestoredMannequin->SpawnDefaultController();
		AMannequinAIController* RestoredAIController = Cast<AMannequinAIController>(RestoredMannequin->GetController());
		if (!ensureMsgf(IsValid(RestoredAIController),
			TEXT("Mannequin %s could not restore its default AIController after player possession."),
			*GetNameSafe(RestoredMannequin)))
		{
			UE_LOG(LogProjectProject01Multiplayer, Error,
				TEXT("Mannequin %s has no valid default AIController after control transfer."),
				*GetNameSafe(RestoredMannequin));
			return;
		}

		RestoredMannequin->ForceNetUpdate();
		UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(RestoredMannequin->GetWorld(), EProjectProject01DiagnosticSeverity::Normal,
			TEXT("MannequinAIControlRestored"), FString::Printf(TEXT("Controller=%s"), *GetNameSafe(RestoredAIController)), RestoredMannequin);
		UE_LOG(LogProjectProject01Multiplayer, Verbose,
			TEXT("Restored AI control for mannequin %s after player control transfer."),
			*GetNameSafe(RestoredMannequin));
	});
}

AMannequinAICharacter* AMultiplayTestGameMode::FindMannequinBySlot(int32 Slot) const
{
	if (Slot < 0 || Slot > 9 || !IsValid(GetWorld()))
	{
		return nullptr;
	}

	TArray<AActor*> MannequinActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), MannequinActors);

	AMannequinAICharacter* MatchingMannequin = nullptr;
	for (AActor* Actor : MannequinActors)
	{
		AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(Actor);
		if (!IsValid(Mannequin) || Mannequin->GetControlSlot() != Slot)
		{
			continue;
		}

		if (IsValid(MatchingMannequin))
		{
			UE_LOG(LogProjectProject01Multiplayer, Error,
				TEXT("Duplicate mannequin control slot %d found on %s and %s."),
				Slot, *MatchingMannequin->GetName(), *Mannequin->GetName());
			return nullptr;
		}

		MatchingMannequin = Mannequin;
	}

	return MatchingMannequin;
}

void AMultiplayTestGameMode::AssignRandomInitialMannequinView(
	AMultiplayTestPlayerController* Controller)
{
	if (!HasAuthority() || !IsMannequinController(Controller))
	{
		return;
	}

	TArray<int32> AvailableSlots;
	AvailableSlots.Reserve(10);
	for (int32 Slot = 0; Slot <= 9; ++Slot)
	{
		if (IsValid(FindMannequinBySlot(Slot)))
		{
			AvailableSlots.Add(Slot);
		}
	}

	if (AvailableSlots.IsEmpty())
	{
		UE_LOG(LogProjectProject01Multiplayer, Error,
			TEXT("No valid mannequin control slot is available for the initial view of %s."),
			*GetNameSafe(Controller));
		return;
	}

	const int32 SelectedSlot = AvailableSlots[FMath::RandHelper(AvailableSlots.Num())];
	if (!ensureMsgf(TryPossessMannequin(Controller, SelectedSlot),
		TEXT("Failed to assign mannequin slot %d as the initial view for %s."),
		SelectedSlot, *GetNameSafe(Controller)))
	{
		return;
	}

	UE_LOG(LogProjectProject01Multiplayer, Log,
		TEXT("%s received random initial mannequin view slot %d."),
		*GetNameSafe(Controller), SelectedSlot);
}

bool AMultiplayTestGameMode::IsMannequinController(const AMultiplayTestPlayerController* Controller) const
{
	return IsValid(Controller) && MannequinController.Get() == Controller;
}

bool AMultiplayTestGameMode::IsSurvivorController(const APlayerController* Controller) const
{
	const APawn* ControlledPawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	return IsValid(ControlledPawn) && !IsMannequinController(Cast<AMultiplayTestPlayerController>(Controller)) &&
		!ControlledPawn->IsA<AMannequinAICharacter>();
}

bool AMultiplayTestGameMode::CanSurvivorSeeMannequin(
	const APlayerController* SurvivorController,
	const AMannequinAICharacter* Mannequin) const
{
	FProjectProject01DiagnosticWorkScope DiagnosticScope(GetWorld());
	DiagnosticScope.AddVisionCheck();
	if (!IsValid(SurvivorController) || !IsValid(Mannequin) || !IsValid(GetWorld()))
	{
		return false;
	}

	const APawn* SurvivorPawn = SurvivorController->GetPawn();
	const UCapsuleComponent* MannequinCapsule = Mannequin->GetCapsuleComponent();
	if (!IsValid(SurvivorPawn) || !IsValid(MannequinCapsule))
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	SurvivorController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	if (ViewLocation.ContainsNaN() || ViewRotation.ContainsNaN())
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected invalid survivor view data from %s."), *GetNameSafe(SurvivorController));
		return false;
	}

	const FVector CapsuleCenter = MannequinCapsule->GetComponentLocation();
	const FVector CapsuleUp = MannequinCapsule->GetUpVector();
	const float CapsuleHalfHeight = MannequinCapsule->GetScaledCapsuleHalfHeight();
	const FVector TargetPoints[] =
	{
		CapsuleCenter,
		CapsuleCenter + CapsuleUp * (CapsuleHalfHeight * 0.55f),
		CapsuleCenter - CapsuleUp * (CapsuleHalfHeight * 0.35f)
	};

	FCollisionQueryParams TraceParameters(SCENE_QUERY_STAT(SurvivorVision), true, SurvivorPawn);
	TraceParameters.AddIgnoredActor(SurvivorController);
	for (const FVector& TargetPoint : TargetPoints)
	{
		if (!IsPointInsideVisionCone(ViewLocation, ViewRotation.Vector(), TargetPoint, SurvivorVisionHalfAngleDegrees))
		{
			continue;
		}

		FHitResult HitResult;
		DiagnosticScope.AddLineTrace();
		const bool bReachedMannequin = GetWorld()->LineTraceSingleByChannel(
			HitResult, ViewLocation, TargetPoint, ECC_GameTraceChannel1, TraceParameters) && HitResult.GetActor() == Mannequin;
		if (bReachedMannequin)
		{
			return true;
		}
	}

	return false;
}

void AMultiplayTestGameMode::UpdateSurvivorVisionFrozenStates()
{
	if (!HasAuthority() || !IsValid(GetWorld()))
	{
		return;
	}

	TArray<APlayerController*> SurvivorControllers;
	for (FConstPlayerControllerIterator ControllerIterator = GetWorld()->GetPlayerControllerIterator(); ControllerIterator; ++ControllerIterator)
	{
		APlayerController* Controller = ControllerIterator->Get();
		if (IsSurvivorController(Controller))
		{
			SurvivorControllers.Add(Controller);
		}
	}

	TArray<AActor*> MannequinActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMannequinAICharacter::StaticClass(), MannequinActors);
	for (AActor* Actor : MannequinActors)
	{
		AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(Actor);
		if (!IsValid(Mannequin))
		{
			continue;
		}

		bool bObservedByAnySurvivor = false;
		for (const APlayerController* SurvivorController : SurvivorControllers)
		{
			if (CanSurvivorSeeMannequin(SurvivorController, Mannequin))
			{
				bObservedByAnySurvivor = true;
				break;
			}
		}

		Mannequin->SetFrozenBySurvivorVision(bObservedByAnySurvivor);
	}
}

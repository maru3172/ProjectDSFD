// File: Source/ProjectProject01/Public/MultiplayTestGameMode.h
// Build target: ProjectProject01Server / ProjectProject01

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MultiplayTestPlayerController.h"
#include "MultiplayMannequinTypes.h"
#include "ProjectProject01GameInstance.h"
#include "MultiplayTestGameMode.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogProjectProject01Multiplayer, Log, All);
struct FMannequinAITuningRow;
enum class EProjectProject01SpawnRole : uint8;

struct FMultiplayTestServerPlayerRecord
{
	FString UserId;
	FString MatchId;
	FString Role;
	TWeakObjectPtr<class APlayerCharacter> SurvivorPawn;
	EMultiplayTestSurvivorState SurvivorState = EMultiplayTestSurvivorState::NotApplicable;
	int32 CaptureCount = 0;
	int32 RescueCount = 0;
	double FirstCaptureSeconds = 0.0;
	double AllCapturedSeconds = 0.0;
	double EscapeSeconds = 0.0;
	FTransform LastPawnTransform = FTransform::Identity;
	int32 RemainingDeathCount = 0;
	int32 ViewedMannequinSlot = -1;
	bool bWasManualMannequinControl = false;
	bool bForfeited = false;
	bool bDisconnected = false;
	double ReconnectDeadlineWorldSeconds = 0.0;
};

/**
 * MultiplayTest 전용 게임 모드입니다.
 * 기존 AProjectPracticeGameModeBase를 상속하지 않으므로 조력자를 자동 생성하지 않습니다.
 */
UCLASS(Config=Game)
class PROJECTPROJECT01_API AMultiplayTestGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMultiplayTestGameMode();
	virtual void BeginPlay() override;

	/** 서버가 DataTable의 검증된 생존자 시야 수치를 적용한다. */
	void ApplyMannequinTuning(const FMannequinAITuningRow& Tuning);
	void GetDiagnosticSurvivorVisionTuning(float& OutIntervalSeconds, float& OutHalfAngleDegrees) const
	{
		OutIntervalSeconds = SurvivorVisionCheckIntervalSeconds;
		OutHalfAngleDegrees = SurvivorVisionHalfAngleDegrees;
	}

	bool TryPossessMannequin(class AMultiplayTestPlayerController* RequestingController, int32 Slot);
	bool TryEnableMannequinManualControl(class AMultiplayTestPlayerController* RequestingController);
	bool TryIssueMannequinCommand(class AMultiplayTestPlayerController* RequestingController,
		EMultiplayMannequinCommand Command);
	bool TryResetFollowingMannequins(class AMultiplayTestPlayerController* RequestingController);
	bool ToggleMannequinCatchDamage(class AMultiplayTestPlayerController* RequestingController);
	void DeclareVoluntaryExit(class AMultiplayTestPlayerController* RequestingController);
	void CycleSurvivorSpectator(class AMultiplayTestPlayerController* RequestingController, int32 Direction);
	bool TryBroadcastSurvivorPing(class AMultiplayTestPlayerController* RequestingController,
		EProjectProject01PingType Type, const FVector& Location);
	/** 맵 이동 영역에 들어온 생존자를 관전 대기 상태로 확정합니다. */
	bool MarkSurvivorWaitingForLevelTransition(class APlayerCharacter* Survivor);
	/** 한 명 이상 대기 중이고 더 이상 활동 중인 생존자가 없으면 true입니다. */
	bool CanCommitLevelTransition() const;
	/** seamless travel 직전 각 컨트롤러의 다음 맵 스폰 여부와 암전을 준비합니다. */
	void PrepareLevelTransitionTravel();

	/** 기존 마네킹 접촉 포획 로직이 생존자를 게임 오버로 만든 뒤 서버 경기 기록에 알린다. */
	void NotifyExistingMannequinCatch(class APlayerCharacter* Survivor);
	/** 서버가 확정한 포획 성공 피드백을 이번 경기의 마네킹 플레이어에게만 전달합니다. */
	void NotifyMannequinCaptureFeedback(bool bElimination);

	/** Escape 태그 TriggerBox 또는 이후의 탈출 장치가 서버에서 호출하는 탈출 확정 경로다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Multiplayer|Match")
	bool MarkSurvivorEscaped(class APlayerCharacter* Survivor);

	virtual void Tick(float DeltaSeconds) override;
	virtual void PreLogin(
		const FString& Options,
		const FString& Address,
		const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;
	virtual FString InitNewPlayer(
		APlayerController* NewPlayerController,
		const FUniqueNetIdRepl& UniqueId,
		const FString& Options,
		const FString& Portal = TEXT("")) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void Logout(AController* Exiting) override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;
	virtual void HandleSeamlessTravelPlayer(AController*& Controller) override;

private:
	void InitializeRoleSpawnPoints();
	void InitializeMannequinSpawns();
	void UpdateMultiplayerMannequinAI();
	void StopAllMannequinsForMatchEnd();
	class APlayerCharacter* FindDetectedSurvivor(const class AMannequinAICharacter* Mannequin) const;
	bool IsActiveSurvivorTarget(const class APlayerCharacter* Survivor) const;
	bool CanMannequinDetectSurvivor(const class AMannequinAICharacter* Mannequin,
		const class APlayerCharacter* Survivor) const;
	class AMannequinAICharacter* GetMannequinPlayerFocusPawn() const;
	bool SelectNextPatrolTarget(class AMannequinAICharacter* Mannequin,
		const FVector& SearchOrigin, FVector& OutTarget);
	void MoveMannequinToPatrolTarget(class AMannequinAICharacter* Mannequin, class AAIController* AIController);
	void MoveMannequinToFollowTarget(class AMannequinAICharacter* Mannequin, class AAIController* AIController,
		class AMannequinAICharacter* Leader, const TArray<class AMannequinAICharacter*>& Followers);
	void StopMannequinAI(class AMannequinAICharacter* Mannequin) const;
	TArray<class AProjectProject01RoleSpawnPoint*> GetRoleSpawnPoints(
		EProjectProject01SpawnRole SpawnRole) const;
	class AMannequinAICharacter* FindMannequinBySlot(int32 Slot) const;
	void AssignRandomInitialMannequinView(class AMultiplayTestPlayerController* Controller);
	void CompleteInitialMannequinViewAssignment(
		class AMultiplayTestPlayerController* Controller,
		int32 Slot,
		int32 RemainingAttempts);
	void QueueDefaultAIControllerRestore(class AMannequinAICharacter* Mannequin) const;
	void ResetDedicatedServerWorldIfEmpty();
	bool IsMannequinController(const class AMultiplayTestPlayerController* Controller) const;
	bool IsSurvivorController(const APlayerController* Controller) const;
	bool CanSurvivorSeeMannequin(const APlayerController* SurvivorController,
		const class AMannequinAICharacter* Mannequin) const;
	void UpdateSurvivorVisionFrozenStates();
	void BindEscapeTriggers();
	void RegisterMatchPlayer(class AMultiplayTestPlayerController* Controller);
	bool RestoreDisconnectedMatchPlayer(class AMultiplayTestPlayerController* Controller);
	void SnapshotPlayerRecord(class AMultiplayTestPlayerController* Controller, FMultiplayTestServerPlayerRecord& Record);
	void ApplyForfeit(FMultiplayTestServerPlayerRecord& Record, const FString& Reason);
	void ProcessReconnectTimeouts();
	void EnterSurvivorSpectator(class AMultiplayTestPlayerController* Controller);
	class APlayerCharacter* FindLivingSurvivorSpectatorTarget(const class AMultiplayTestPlayerController* RequestingController, int32 Direction) const;
	void TryStartAuthoritativeMatch();
	void SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase NewPhase);
	void CheckForMatchCompletion();
	void FinishAuthoritativeMatch(const FString& Reason);
	void SubmitAuthoritativeResults();
	void HandleResultSubmissionComplete(
		TWeakObjectPtr<class AMultiplayTestPlayerController> Controller,
		bool bSucceeded);
	void PresentFinalResults();
	void FinalizeLevelTransitionArrivals();
	FMultiplayTestMatchResult BuildFinalResult(
		const class AMultiplayTestPlayerController* Controller,
		const FMultiplayTestServerPlayerRecord& Record) const;
	FMultiplayTestServerPlayerRecord* FindSurvivorRecord(class APlayerCharacter* Survivor);
	const FMultiplayTestServerPlayerRecord* FindSurvivorRecord(const class APlayerCharacter* Survivor) const;
	double GetElapsedMatchSeconds() const;

	UFUNCTION()
	void HandleEscapeTriggerBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Survivor Vision",
		meta = (ClampMin = "0.01", UIMin = "0.01", Units = "s"))
	float SurvivorVisionCheckIntervalSeconds = 0.05f;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Survivor Vision",
		meta = (ClampMin = "1.0", ClampMax = "89.0", UIMin = "1.0", UIMax = "89.0", Units = "deg"))
	float SurvivorVisionHalfAngleDegrees = 55.0f;

	float SurvivorVisionCheckAccumulatorSeconds = 0.0f;
	float MultiplayerAIUpdateAccumulatorSeconds = 0.0f;
	float GameServerHeartbeatAccumulatorSeconds = 0.0f;
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, FVector> MultiplayerPatrolTargets;
	/** 현재 목적지에 도착하기 전에 A* 검증까지 끝낸 다음 정찰 목적지입니다. */
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, FVector> MultiplayerPatrolNextTargets;
	/** 이동 요청은 살아 있지만 실제로 벽 등에 막힌 정찰 마네킹을 감지하기 위한 진행 상태입니다. */
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, FVector> MultiplayerPatrolLastLocations;
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, float> MultiplayerPatrolLastDistances;
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, float> MultiplayerPatrolStalledSeconds;
	/** 정찰 도착 후 가끔 주변을 살피는 정지 행동의 서버 권한 상태입니다. */
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, float> MultiplayerObservationRemainingSeconds;
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, float> MultiplayerObservationElapsedSeconds;
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, float> MultiplayerObservationBaseYaw;
	/** 각 마네킹이 최근 방문한 정찰 목적지입니다. 같은 구역을 짧게 반복하는 것을 막습니다. */
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, TArray<FVector>> MultiplayerPatrolTargetHistory;
	/** 정찰 중 생존자 시야에 노출된 뒤, 시야가 풀리면 추격할 대상을 기억합니다. */
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, TWeakObjectPtr<class APlayerCharacter>> MultiplayerObservedChaseTargets;
	float MultiplayerDetectionRadius = 500.0f;
	float MultiplayerPatrolRadius = 1200.0f;
	float MultiplayerFollowDistance = 300.0f;
	float MultiplayerFollowSeparation = 175.0f;
	float MultiplayerRepathInterval = 0.2f;
	float MultiplayerAcceptanceRadius = 60.0f;

	/** 현재 목적지에 이 거리만큼 접근하면 다음 정찰 경로를 미리 계산합니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
	float MultiplayerPatrolPrecomputeDistance = 350.0f;

	/** 이 거리 안에서는 도착으로 보고 준비된 다음 목적지로 끊김 없이 넘깁니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "60.0", UIMin = "60.0", Units = "cm"))
	float MultiplayerPatrolHandoffDistance = 90.0f;

	/** 정찰 목적지 도착 시 주변을 살펴보는 정지 행동에 들어갈 확률입니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MultiplayerObservationChance = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s"))
	float MultiplayerObservationMinSeconds = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s"))
	float MultiplayerObservationMaxSeconds = 3.0f;

	/** 주변을 살필 때 기준 방향 좌우로 회전할 최대 각도입니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "0.0", ClampMax = "120.0", UIMin = "0.0", UIMax = "120.0", Units = "deg"))
	float MultiplayerObservationYawDegrees = 55.0f;

	/** 이 시간 동안 위치/목적지 거리가 거의 변하지 않으면 막힌 경로를 버리고 새 정찰점을 찾습니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "0.5", UIMin = "0.5", Units = "s"))
	float MultiplayerPatrolStuckTimeoutSeconds = 1.5f;

	/** 정찰 진행으로 인정할 최소 이동 거리입니다. 작은 충돌 떨림은 이동으로 보지 않습니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float MultiplayerPatrolMinimumProgressDistance = 12.0f;

	/** 최근 몇 개의 정찰 구역을 재방문 금지 대상으로 기억할지 결정합니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "1", UIMin = "1", UIMax = "10"))
	int32 MultiplayerPatrolHistorySize = 3;

	/** 최근 정찰 목적지 주변에서 새 목적지를 뽑지 않는 거리입니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "0.0", Units = "cm"))
	float MultiplayerPatrolRevisitAvoidanceRadius = 800.0f;

	/** 최근 방문 기록이 가득 차면 탐색 반경을 넓혀 맵의 다른 구역으로 이동시킵니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MultiplayerPatrolExplorationRadiusMultiplier = 2.0f;

	/** 한 번의 정찰점 선택에서 비교할 완전한 A* 경로 후보 수입니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Mannequin AI|Patrol",
		meta = (ClampMin = "1", UIMin = "1", UIMax = "64"))
	int32 MultiplayerPatrolCandidateAttempts = 16;

	// 로비에서 무작위 배정된 한 명만 마네킹을 조종한다. 역할 옵션이 없는 기존 PIE는 첫 접속자를 사용한다.
	TWeakObjectPtr<class AMultiplayTestPlayerController> MannequinController;
	TSet<TWeakObjectPtr<class AMultiplayTestPlayerController>> RequestedMannequinControllers;
	TSet<TWeakObjectPtr<class AMultiplayTestPlayerController>> ExplicitRoleControllers;
	TMap<FString, FProjectProject01ValidatedJoinClaim> PendingValidatedJoinClaims;
	FTimerHandle EmptyDedicatedServerResetTimer;
	TMap<TWeakObjectPtr<class AMultiplayTestPlayerController>, FMultiplayTestServerPlayerRecord> MatchPlayerRecords;
	TMap<FString, FMultiplayTestServerPlayerRecord> DisconnectedMatchRecords;
	TMap<FString, FMultiplayTestMatchResult> CompletedResultsByUserId;
	TMap<TWeakObjectPtr<AController>, TWeakObjectPtr<AActor>> AssignedSurvivorSpawnPoints;
	TMap<TWeakObjectPtr<class AMultiplayTestPlayerController>, bool> ResultVerificationStatus;
	EMultiplayTestMatchPhase AuthoritativeMatchPhase = EMultiplayTestMatchPhase::Waiting;
	FString AuthoritativeMatchId;
	double MatchStartWorldSeconds = 0.0;
	double MatchEndWorldSeconds = 0.0;
	int32 PendingResultSubmissions = 0;
	bool bFinishCommitted = false;
	bool bMatchEndedByForfeit = false;
	bool bSurvivorVictoryByMannequinForfeit = false;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Match", meta = (ClampMin = "1", UIMin = "1"))
	int32 ExpectedMatchPlayers = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Spawning", meta = (ClampMin = "1", UIMin = "1"))
	int32 SurvivorSpawnCount = 2;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Spawning", meta = (ClampMin = "1", ClampMax = "10", UIMin = "1", UIMax = "10"))
	int32 MannequinSpawnCount = 10;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Spawning")
	TSubclassOf<class AMannequinAICharacter> MannequinPawnClass;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Match", meta = (ClampMin = "1.0", UIMin = "1.0", Units = "s"))
	float MatchTimeLimitSeconds = 1800.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Reconnect", meta = (ClampMin = "1.0", UIMin = "1.0", Units = "s"))
	float ReconnectGraceSeconds = 30.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Match")
	FName EscapeTriggerActorTag = TEXT("ProjectProject01Escape");

	UPROPERTY(Config)
	bool bRequireGameJoinTicket = true;
};

// File: Source/ProjectProject01/Public/MultiplayTestGameMode.h
// Build target: ProjectProject01Server / ProjectProject01

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MultiplayTestPlayerController.h"
#include "MultiplayMannequinTypes.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectLoadingBarrier.h"
#include "ProjectItemRoguelikeRuntime.h"
#include "MultiplayTestGameMode.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogProjectProject01Multiplayer, Log, All);
struct FMannequinAITuningRow;
enum class EProjectProject01SpawnRole : uint8;

struct FMultiplayTestServerPlayerRecord
{
	FString UserId;
	FString MatchId;
	FString Role;
	FString DisplayName;
	TWeakObjectPtr<class APlayerCharacter> SurvivorPawn;
	EMultiplayTestSurvivorState SurvivorState = EMultiplayTestSurvivorState::NotApplicable;
	int32 CaptureCount = 0;
	int32 RescueCount = 0;
	double FirstCaptureSeconds = 0.0;
	double AllCapturedSeconds = 0.0;
	double EscapeSeconds = 0.0;
	FTransform LastPawnTransform = FTransform::Identity;
	int32 RemainingDeathCount = 0;
	bool bInfected = false;       // Custom game infection: out as a survivor, now plays one mannequin body (whole match).
	int32 PlaygroundCaught = 0;   // Custom game infinite lives: times this survivor was caught (whole match).
	int32 PlaygroundCatches = 0;  // Custom game infinite lives: catches the mannequin side made (whole match).
	int32 ViewedMannequinSlot = -1;
	bool bWasManualMannequinControl = false;
	bool bForfeited = false;
	bool bDisconnected = false;
    bool bServerBot=false;
    bool bReplacedByBot=false; // A stand-in bot plays on in this survivor's place (left alive mid-match); the bots count the stand-in instead.
	double ReconnectDeadlineWorldSeconds = 0.0;
};

struct FMultiplayTestMatchCarryover
{
    FProjectCustomMatchRules CustomRules;
    FProjectMatchAffixes Affixes;
    FProjectRogueCarry Rogue; // Item roguelike picks so far (ProjectItemRoguelikeRuntime.h).
    FString PlaygroundVoteCard; bool bPlaygroundVoteDone=false; // Custom game rule-card vote, decided on the first floor.
    int32 EscortSurvivorWins=0,EscortMannequinWins=0,EscortRoundsPlayed=0; // Custom game escort rounds so far.
    TArray<FString> PlaygroundFloorRules; bool bPlaygroundFloorRulesSet=false; // Per-floor rules: the next floor's draw.
    FString MatchId;
    FString BotRoomId;
    TArray<FProjectMatchBotSpec> Bots;
    TMap<FString,FMultiplayTestServerPlayerRecord> Players,Disconnected;
    TArray<FProjectMatchBotSpec> StandIns;TMap<FString,FString> StandInOwners; // UProjectMatchBots stand-ins.
    double ElapsedSeconds=0,PreparedRealSeconds=0,PreparedWorldSeconds=0;
    TSharedPtr<struct FProjectBotLearning> BotLearning; // Bot habits/relief for the next floor (ProjectMatchBots.h).
};

/**
 * MultiplayTest 전용 게임 모드입니다.
 * 기존 AProjectPracticeGameModeBase를 상속하지 않으므로 조력자를 자동 생성하지 않습니다.
 */
UCLASS(Config=Game)
class PROJECTPROJECT01_API AMultiplayTestGameMode : public AGameModeBase
{
	GENERATED_BODY()
	friend class FProjectReviewMatchCarryoverTest;
	friend class FProjectForfeitRulesTest;
	friend class FProjectCustomRulesInfiniteLivesTest;
	friend class FProjectCustomRulesHideRevealTest;
	friend class FProjectCustomRulesGhostsTest;
	friend class FProjectCustomRulesInfectionTest;
	friend class FProjectCustomRulesGazeLimitTest;
	friend class FProjectCustomRulesDarknessTest;
	friend class FProjectCustomRulesHandicapTest;
	friend class FProjectCustomRulesEscapeModesTest;
	friend class FProjectCustomRulesFireTest;
	friend class FProjectCustomRulesHideAndSeekWavesTest;
	friend class FProjectCustomRulesRuleVoteTest;
	friend class FProjectCustomRulesGazeRulesTest;
	friend class FProjectCustomRulesEscortTest;
	friend class FProjectCustomRulesEscalatorsTest;
	friend class FProjectCustomRulesTeamAndFloorTest;
	friend class FProjectReviewTravelRollbackTest;
	friend class FProjectElevatorTest;
	friend class FProjectShutterTest;
	friend class FProjectMannequinViewCommandTest;
	friend class FProjectMannequinNavigationSafetyTest;
	friend class FProjectSpectatorFadeTest;
	friend class FProjectSurvivorPresentationTest;
	friend class AProjectProject01TutorialGameMode;
    friend class UProjectMatchBots;
    friend class UProjectAffixRuntime;
    friend class UProjectItemRoguelikeRuntime;
    friend class FProjectItemRoguelikeTest;
    friend class FProjectMatchBotsRuntimeTest;
    friend class FProjectCustomRulesTest;
    friend class FProjectActualBotMatchEditorTest;
    friend class FProjectBotSimulationTest;
    friend class AProjectOpenObserverController;

public:
	const FString& GetServerMatchId() const { return AuthoritativeMatchId; }
	bool IsReadyForServerAllocation();
	void InitializeLoadingGate(AMultiplayTestPlayerController* Controller);
	void ReceiveLoadingStage(AMultiplayTestPlayerController* Controller,const FString& Epoch,uint8 Progress);
	AMultiplayTestGameMode();
	virtual void BeginPlay() override;
    virtual APlayerController* SpawnPlayerController(ENetRole InRemoteRole,const FString& Options) override;
    bool HasOpenObservers()const;

	/** 서버가 DataTable의 검증된 생존자 시야 수치를 적용한다. */
	void ApplyMannequinTuning(const FMannequinAITuningRow& Tuning);
    bool AcceptCustomRules(const FString& Match,const FProjectCustomMatchRules& Rules,FString& Error);
    const FProjectCustomMatchRules& GetCustomMatchRules()const{return CustomMatchRules;}
    /** Affixes the match ticket carried (ProjectAffixes.h). Fixed for the whole match, floors included. */
    bool AcceptMatchAffixes(const FString& Match,const FProjectMatchAffixes& Affixes,FString& Error);
    /** Custom rules on: their affix ids and item-card switch must equal the ticket's affix set and match mode. */
    static bool IsPlaygroundTicketConsistent(const FProjectCustomMatchRules& Rules,const FProjectMatchAffixes& Affixes,EProjectMatchMode Mode,FString& Error);
    const FProjectMatchAffixes& GetMatchAffixes()const{return MatchAffixes;}
    class UProjectAffixRuntime* GetAffixRuntime()const{return AffixRuntime;}
    /** PvP match mode from the ticket (normal or item roguelike). Fixed for the whole match like the affixes. */
    bool AcceptMatchMode(const FString& Match,EProjectMatchMode Mode,FString& Error);
    class UProjectItemRoguelikeRuntime* GetItemRoguelike()const{return ItemRoguelike;}
    bool IsItemRoguelikeMatch()const{return ItemRoguelike&&ItemRoguelike->IsActive();}
    /** An affix rule is in play: from the ticket (bot match affixes) or handed over as an item roguelike buff. */
    /** Custom game infinite lives (Docs/CustomGamePlayground.md 4.1). */
    // Escort gives survivors endless respawns too (with the escort wear-down).
    bool HasPlaygroundInfiniteLives()const{return CustomMatchRules.bEnabled&&(CustomMatchRules.bInfiniteLives||CustomMatchRules.bEscort);}
    /** Custom game infection (Docs/CustomGamePlayground.md 4.2, 11). */
    bool HasPlaygroundInfection()const{return CustomMatchRules.bEnabled&&CustomMatchRules.bInfection;}
    /** This controller is an infected survivor now playing a mannequin body (may attack with it). */
    bool IsInfectedController(const class AMultiplayTestPlayerController* Controller)const;
    /** Spawns the infected body at At and hands it to this survivor's controller. */
    class AMannequinAICharacter* SpawnInfectedBody(class AMultiplayTestPlayerController* Controller,const FTransform& At);
    /** Where this survivor starts again on this floor (infinite lives, fire). False: no known start. */
    bool FindPlaygroundFloorStart(const class APlayerCharacter* Survivor,FTransform& Out)const;
    /** Custom game spreading fire (Docs/CustomGamePlayground.md 12.1). */
    bool HasPlaygroundFire()const{return CustomMatchRules.bEnabled&&CustomMatchRules.bSpreadingFire;}
    /** Custom game flashlight gaze (Docs/CustomGamePlayground.md 15). */
    bool HasPlaygroundFlashlight()const{return CustomMatchRules.bEnabled&&CustomMatchRules.bFlashlight;}
    /** Custom game escort (Docs/CustomGamePlayground.md 16). */
    bool HasPlaygroundEscort()const{return CustomMatchRules.bEnabled&&CustomMatchRules.bEscort;}
    /** Custom game broken escalators (Docs/CustomGamePlayground.md 17). */
    bool HasPlaygroundEscalators()const{return CustomMatchRules.bEnabled&&CustomMatchRules.EscalatorWorking>0;}
    /** The escapee was caught (contact or direct attack): the mannequin side takes this floor's round. */
    void OnPlaygroundEscapeeCaught(class APlayerCharacter* Escapee);
    /** Custom game hide-and-seek: the first minute, while survivors hide (bodies frozen, mannequin screen dark). */
    bool IsPlaygroundHidePhase()const;
    /** Custom game rule-card vote: one player's vote (Docs/CustomGamePlayground.md 12.2). False when not counted. */
    bool VotePlaygroundRuleCard(class AMultiplayTestPlayerController* Controller,int32 Serial,int32 Index);
    /** The card the vote turned on for this match (empty: none or no vote). */
    const FString& GetPlaygroundVoteCard()const{return PlaygroundVote.Chosen;}
    static constexpr double FireStartSeconds=120.0;
    static constexpr double FireSpreadSeconds=30.0;
    static constexpr double FireDamageSeconds=5.0;
    static constexpr int32 FireMaxCells=24;
    /** Custom game ghost survivors (Docs/CustomGamePlayground.md 4.3). */
    bool HasPlaygroundGhosts()const{return CustomMatchRules.bEnabled&&CustomMatchRules.bGhosts;}
    /** An eliminated survivor in a ghost match asks to whisper: 5 s to the living, then 15 s before the next. */
    bool TryOpenGhostWhisper(class AMultiplayTestPlayerController* Ghost);
    static constexpr double GhostWhisperSeconds=5.0;
    static constexpr double GhostWhisperCooldown=15.0;
    static constexpr double GhostPingCooldown=20.0;
    /** Counts an infinite-lives catch and gives where this survivor starts again on this floor. False: no known start. */
    bool HandlePlaygroundCatch(class APlayerCharacter* Survivor,FTransform& OutRespawn);
    bool HasAffixRule(const TCHAR* Id)const{return MatchAffixes.Has(Id)||(ItemRoguelike&&ItemRoguelike->HasBuff(Id));}
    /** The affix runtime has work: ticket affixes, or an item roguelike match (its items and buffs run there too). */
    bool AreAffixRulesActive()const{return MatchAffixes.IsActive()||IsItemRoguelikeMatch();}
	void GetDiagnosticSurvivorVisionTuning(float& OutIntervalSeconds, float& OutHalfAngleDegrees) const
	{
		OutIntervalSeconds = SurvivorVisionCheckIntervalSeconds;
		OutHalfAngleDegrees = SurvivorVisionHalfAngleDegrees;
	}

	bool TryPossessMannequin(class AMultiplayTestPlayerController* RequestingController, int32 Slot);
	bool TryEnableMannequinManualControl(class AMultiplayTestPlayerController* RequestingController);
	/** Target: a mannequin picked with the command wheel; null means the one the player is viewing. */
	bool TryIssueMannequinCommand(class AMultiplayTestPlayerController* RequestingController,
		EMultiplayMannequinCommand Command, class AMannequinAICharacter* Target = nullptr);
	bool TryResetFollowingMannequins(class AMultiplayTestPlayerController* RequestingController);
	bool ToggleMannequinCatchDamage(class AMultiplayTestPlayerController* RequestingController);
	bool TryMannequinAttack(class AMultiplayTestPlayerController* RequestingController);
	void DeclareVoluntaryExit(class AMultiplayTestPlayerController* RequestingController);
	void CycleSurvivorSpectator(class AMultiplayTestPlayerController* RequestingController, int32 Direction);
	bool TryBroadcastSurvivorPing(class AMultiplayTestPlayerController* RequestingController,
		EProjectProject01PingType Type, const FVector& Location);
	/** 맵 이동 영역에 들어온 생존자를 관전 대기 상태로 확정합니다. */
	bool MarkSurvivorWaitingForLevelTransition(class APlayerCharacter* Survivor, bool bFadeToSpectator = true);
	/** 한 명 이상 대기 중이고 더 이상 활동 중인 생존자가 없으면 true입니다. */
	bool CanCommitLevelTransition() const;
	/** 사망/포획/확정 기권만 인정. 층 이동 대기·탈출·재접속 유예는 인정하지 않습니다. */
	bool CanUseEmergencyElevator() const;
	/** 이 높이(cm) 아래로 떨어진 생존자는 사망합니다. 맵의 가장 낮은 시작 지점에서 FallDeathDepth만큼 아래이며,
	 *  시작 지점이 없는 맵은 월드 KillZ만 씁니다. */
	float GetFallDeathZ() const;
	/** 가장 낮은 시작 지점 아래로 이만큼(cm) 떨어지면 낙사합니다. */
	UPROPERTY(EditAnywhere, Config, Category = "Multiplay|Fall", meta = (ClampMin = "100"))
	float FallDeathDepth = 1000.f;
private:
	mutable float CachedFallDeathZ = 0.f;
	mutable bool bFallDeathZReady = false;
public:
	/** Server-owned match membership, including ticket-free local PIE role assignments. */
	bool IsRegisteredActiveSurvivor(const class APlayerCharacter* Survivor) const;
    /** A human survivor other than Except still in play (not caught, escaped, gone or forfeited). */
    bool HasOtherHumanSurvivorInPlay(const class AMultiplayTestPlayerController* Except) const;
    /** Leaving while in play is a desertion: in PvP, and in a bot match with two or more people (else a loss). */
    bool CountsAsDesertion() const;
	bool IsAuthoritativeMatchInProgress() const { return AuthoritativeMatchPhase == EMultiplayTestMatchPhase::InProgress; }
	/** seamless travel 직전 각 컨트롤러의 다음 맵 스폰 여부와 암전을 준비합니다. */
	void PrepareLevelTransitionTravel(float FadeSeconds = 0.6f);
	bool AreLevelTransitionClientsBlack() const;
	/** Old-world travel rejection/timeout: undo only transition-owned state. */
	void CancelLevelTransitionTravel();

	/** 기존 마네킹 접촉 포획 로직이 생존자를 게임 오버로 만든 뒤 서버 경기 기록에 알린다. */
	/** bCaughtByMannequin=false: a fall death eliminates the survivor without crediting the mannequin. */
	void NotifyExistingMannequinCatch(class APlayerCharacter* Survivor, bool bCaughtByMannequin = true);
	/** 서버가 확정한 포획 성공 피드백을 이번 경기의 마네킹 플레이어에게만 전달합니다. */
	void NotifyMannequinCaptureFeedback(bool bElimination);
	void NotifySurvivorDeathCountLost(class APlayerCharacter* Survivor);

	/** Escape 태그 TriggerBox 또는 이후의 탈출 장치가 서버에서 호출하는 탈출 확정 경로다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Multiplayer|Match")
	bool MarkSurvivorEscaped(class APlayerCharacter* Survivor);

	/** Top-left match notice for the human players (all of them, or only the mannequins). Never in the tutorial. */
	void BroadcastMatchNotice(const FString& Message, bool bMannequinsOnly, float HoldSeconds = 3.0f);

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

protected:
	virtual class AMannequinAICharacter* GetMannequinPlayerFocusPawn() const;

private:
    UPROPERTY(Transient) TObjectPtr<class UProjectMatchBots> MatchBots;
    UPROPERTY(Transient) TObjectPtr<class UProjectAffixRuntime> AffixRuntime;
    UPROPERTY(Transient) TObjectPtr<class UProjectItemRoguelikeRuntime> ItemRoguelike;
    FString MatchModeMatchId;
    /** The order a body had when a human took direct control; it goes back to it when the player lets go. */
    TMap<TWeakObjectPtr<class AMannequinAICharacter>, EMultiplayMannequinCommand> PreManualMannequinCommands;
	void UpdateVoiceAudibility();
	float VoiceAudibilityAccumulator=0;
	void RecordTimeline(EProjectMatchEvent Kind, const FString& PlayerName = {}, const FString& Detail = {});
	void BeginMatchTimeline();
	double GetTimelineElapsedSeconds() const;
	TSharedPtr<FProjectMatchTimelineLog> TimelineLog;
    TSharedPtr<FMultiplayTestMatchCarryover> MatchCarryover;
    double PreviousMatchSeconds=0;
	double TimelineLevelStartWorldSeconds = 0.0;
	bool bTimelineTravelPrepared = false;
	double TimelineSecondsBeforeTravel = 0.0;
	struct FTransitionPawnSnapshot
	{
		bool bHidden = false;
		bool bCollision = true;
		uint8 MovementMode = 0;
		uint8 CustomMovementMode = 0;
	};
	TMap<TWeakObjectPtr<APlayerCharacter>, FTransitionPawnSnapshot> TransitionPawnSnapshots;
	friend class FProjectMatchTimelineServerTest;
	friend class FProjectMannequinSearchTest;
	bool bServerWorldInitialized = false;
	bool bServerReadinessLogged = false;
	bool TickLoadingGate(float Delta);
	FProjectLoadingBarrier LoadingGate;
	float LoadingBroadcastSeconds=0;
	TMap<TWeakObjectPtr<ACharacter>,TPair<uint8,uint8>> LoadingMovementModes;
	bool ContinueLastSeenSearch(class AMannequinAICharacter* Mannequin, class AAIController* Controller, double Now);
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, FMultiplayMannequinSearchState> MultiplayerSearchStates;
	UPROPERTY(EditDefaultsOnly, Category="Multiplayer|Mannequin AI|Search", meta=(ClampMin="1.0",Units="s"))
	float MultiplayerSearchTimeoutSeconds = 10.0f;
	UPROPERTY(EditDefaultsOnly, Category="Multiplayer|Mannequin AI|Search", meta=(ClampMin="0.1",Units="s"))
	float MultiplayerSearchObserveSeconds = 2.5f;
	void InitializeRoleSpawnPoints();
	void InitializeMannequinSpawns();
	void UpdateMultiplayerMannequinAI();
	void StopAllMannequinsForMatchEnd();
	class APlayerCharacter* FindDetectedSurvivor(const class AMannequinAICharacter* Mannequin) const;
	bool IsActiveSurvivorTarget(const class APlayerCharacter* Survivor) const;
	bool CanMannequinDetectSurvivor(const class AMannequinAICharacter* Mannequin,
		const class APlayerCharacter* Survivor) const;
	void AssignMannequinCommand(class AMannequinAICharacter* Target, EMultiplayMannequinCommand Command);
	bool ResetFollowingMannequinCommands();
	void SetGlobalMannequinCatchDamage(bool bEnabled);
	bool SelectNextPatrolTarget(class AMannequinAICharacter* Mannequin,
		const FVector& SearchOrigin, FVector& OutTarget);
	void MoveMannequinToPatrolTarget(class AMannequinAICharacter* Mannequin, class AAIController* AIController);
	void MoveMannequinToFollowTarget(class AMannequinAICharacter* Mannequin, class AAIController* AIController,
		class AMannequinAICharacter* Leader, const TArray<class AMannequinAICharacter*>& Followers);
	void StopMannequinAI(class AMannequinAICharacter* Mannequin) const;
	bool FindSupportedMannequinLocation(const class AMannequinAICharacter* Mannequin,
		const FVector& DesiredCenter, FVector& OutCenter) const;
	void MaintainMannequinFloorSafety(class AMannequinAICharacter* Mannequin);
	bool FindReachableMannequinGoal(class AMannequinAICharacter* Mannequin,
		const FVector& DesiredFeet, float FallbackRadius, FVector& OutGoal, int32 Retry = 0) const;
	void MoveMannequinOnCompletePath(class AMannequinAICharacter* Mannequin,
		class AAIController* Controller, const FVector& DesiredFeet, float FallbackRadius);
	struct FMannequinNavigationState
	{
		FVector RequestedGoal = FVector::ZeroVector;
		FVector ResolvedGoal = FVector::ZeroVector;
		FVector ProgressLocation = FVector::ZeroVector;
		double LastProgressSeconds = 0.0;
		double LastRequestSeconds = -1.0;
		int32 Retry = 0;
	};
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, FMannequinNavigationState> MultiplayerNavigationStates;
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, FVector> MultiplayerLastSupportedLocations;
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
	/** Bot match with human survivors, all of whom are eliminated (ends the match; blocks level travel). */
	bool AreAllHumanSurvivorsEliminatedInBotMatch() const;
	/** Bot match with human survivors, all of whom are caught or escaped (ends the match). */
	bool AreAllHumanSurvivorsOutInBotMatch() const;
	virtual void FinishAuthoritativeMatch(const FString& Reason);
	void SubmitAuthoritativeResults();
	void HandleResultSubmissionComplete(
		TWeakObjectPtr<class AMultiplayTestPlayerController> Controller,
		bool bSucceeded);
	void PresentFinalResults();
	void FinalizeLevelTransitionArrivals();
	void RefreshSurvivorSpectatorTargets();
	float SpectatorRefreshSeconds = 0;
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
	float MultiplayerManualAttackRange = 180.0f;

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
	/** Where each survivor stood when this floor's match went live (custom game infinite lives returns them here). */
	TMap<TWeakObjectPtr<AController>, FTransform> PlaygroundFloorStarts;
	/** Custom game "hiding too long reveals you": per survivor, and the second light of each 5 s reveal. */
	TMap<TWeakObjectPtr<class APlayerCharacter>, FPlaygroundHideTracker> PlaygroundHides;
	struct FPlaygroundRevealEcho { FVector At = FVector::ZeroVector; float In = 0.f; };
	TArray<FPlaygroundRevealEcho> PlaygroundRevealEchoes;
	void TickPlaygroundHideReveal(float DeltaSeconds);
	/** Ghost survivors: when each whisper window ends, and when the next whisper / ping may start (world seconds). */
	TMap<TWeakObjectPtr<class AMultiplayTestPlayerController>, double> GhostWhisperUntil, GhostWhisperReadyAt, GhostPingReadyAt;
	bool IsGhost(const FMultiplayTestServerPlayerRecord& Record) const;
	/** Custom game spreading fire: the cells of this floor, when the next spread happens, and per-survivor damage pacing. */
	TArray<TWeakObjectPtr<class AProjectPlaygroundFire>> PlaygroundFires;
	double PlaygroundNextFireSpread = -1.0;
	TMap<TWeakObjectPtr<class APlayerCharacter>, double> PlaygroundFireDamageAt;
	void TickPlaygroundFire();
	/** Custom game hide-and-seek and waves (Docs/CustomGamePlayground.md 12.2). */
	bool bPlaygroundHideAnnounced = false, bPlaygroundHuntStarted = false;
	void TickPlaygroundHideAndSeekAndWaves();
	class AMannequinAICharacter* SpawnWaveBody();
	/** Custom game phase 4: pair-gaze bodies (chosen once per floor) and the flashlight rule on survivors. */
	bool bPlaygroundPairGazeChosen = false;
	void TickPlaygroundGazeRules();
	/** Eyes a pair-gaze body needs: 2, or 1 once only one survivor is still in play. */
	int32 PlaygroundPairGazeNeeded(const TArray<APlayerController*>& SurvivorControllers) const;
	/** Custom game escort: one round per floor, best of three; carried between floors. */
	int32 EscortSurvivorWins = 0, EscortMannequinWins = 0, EscortRoundsPlayed = 0;
	bool bEscortRoundOver = false, bEscortGoalFinal = false, bEscortAfraidNoticed = false;
	TWeakObjectPtr<class APlayerCharacter> PlaygroundEscapee;
	TWeakObjectPtr<class AActor> EscortGoal;
	TWeakObjectPtr<class AProjectProject01LevelTransitionTrigger> EscortTravel;
	FVector EscortPingTarget = FVector::ZeroVector;
	double EscortPingUntil = 0, EscortHoldUntil = 0, EscortNextMoveAt = 0, EscortNextFearNoticeAt = 0;
	TMap<FString, int32> EscortDowns; // Respawns this round per survivor (the wear-down).
	TSet<TWeakObjectPtr<class AProjectProject01LevelTransitionTrigger>> EscortKnownBroken; // Broken ways the escapee has found.
	/** Custom game broken escalators: which ways work is rolled once per floor; the working ones get a mannequin-only mark. */
	bool bPlaygroundEscalatorsChosen = false;
	/** Per-floor rules (Docs/CustomGamePlayground.md 18): this floor's drawn sight/environment rules. */
	TArray<FString> PlaygroundFloorDrawn;
	bool bPlaygroundFloorNoticeSent = false;
	TArray<TWeakObjectPtr<class AActor>> PlaygroundExitMarks;
	void TickPlaygroundEscalators();
	/** A way to the next floor that works (for the escort's forced travel); null when none. */
	class AProjectProject01LevelTransitionTrigger* FindWorkingFloorTrigger() const;
	void TickPlaygroundEscort();
	class APlayerCharacter* SpawnPlaygroundEscapee();
	void ChoosePlaygroundEscortGoal();
	/** True when the escapee is alone (no survivor within 8 m) and a body is in its sight within 12 m. */
	bool IsPlaygroundEscapeeAfraid(const class APlayerCharacter* Escapee) const;
	void EndPlaygroundEscortRound(bool bSurvivorsWon);
	void ContinuePlaygroundEscort();
	void HearPlaygroundEscortPing(EProjectProject01PingType Type, const FVector& Location, const class AMultiplayTestPlayerController* Sender);
	void ApplyPlaygroundEscortWear(class APlayerCharacter* Survivor, const FMultiplayTestServerPlayerRecord& Record);
	FString PlaygroundEscortScore() const { return FString::Printf(TEXT("생존자 %d : 마네킹 %d"), EscortSurvivorWins, EscortMannequinWins); }
	/** Custom game rule-card vote, held at the black loading gate like the item card pick. */
	struct FPlaygroundVote{bool bOpen=false,bDone=false;int32 Serial=0;double EndsAt=0,NextSendAt=0;TArray<FString> Cards;TMap<FString,int32> Votes;FString Chosen;};
	FPlaygroundVote PlaygroundVote;
	/** True while the gate must stay black for the vote. */
	bool HoldPlaygroundRuleVote(double RealNow);
	void ClosePlaygroundRuleVote();
	void SendPlaygroundRuleVote(double RealNow);
	/** Turns the voted card's rule on for the rest of the match (every floor) and updates the players' screens. */
	void ApplyPlaygroundVoteCard(const FString& Card);
	FString PlaygroundVoterKey(const FMultiplayTestServerPlayerRecord& Record) const;
	float PlaygroundHandicapBodyScale() const;
	class AProjectPlaygroundFire* SpawnPlaygroundFire(const FVector& At);
	bool IsInPlaygroundFire(const FVector& At) const;
	/** Custom game escape modes: the last one alone got out (teammates share the win), and who got out first. */
	bool bPlaygroundLastSavedAll = false;
	FString PlaygroundFirstOut;
	/** Custom game gaze limit: when each body started being watched without a break (world seconds). */
	TMap<TWeakObjectPtr<class AMannequinAICharacter>, double> PlaygroundGazeSince;
	bool ApplyPlaygroundGazeLimit(class AMannequinAICharacter* Body, bool bObserved);
	/** Custom game personal handicap: 1 + this player's percent for one field (1 when none). */
	float PlaygroundHandicapScale(const class AController* Controller, int32 FProjectPlaygroundHandicap::* Field) const;
	/** Survivor handicap on a newly registered pawn: speed and stamina each floor, death count on the first floor. */
	void ApplySurvivorHandicap(class AMultiplayTestPlayerController* Controller, FMultiplayTestServerPlayerRecord& Record);
	/** The mannequin player's body speed handicap on every mannequin body of this floor. */
	void ApplyMannequinHandicap();
	/** Custom game darkness / sound-only: how far a survivor's freezing gaze reaches (cm). 0 = no limit. */
	float PlaygroundVisionRange() const;
	/** Infection: the body each infected player (by user id, or display name in ticket-free play) plays this floor. */
	TMap<FString, TWeakObjectPtr<class AMannequinAICharacter>> InfectedBodies;
	static FString InfectedKey(const FMultiplayTestServerPlayerRecord& Record) { return Record.UserId.IsEmpty() ? Record.DisplayName : Record.UserId; }
	/** Next floor or a reconnect: a mannequin spawn point away from the living survivors. */
	bool FindInfectedSpawn(FTransform& Out) const;
	void SpawnInfectedForCarriedRecords();
	void TickGhostWhispers();
	void ShowPlaygroundReveal(const FVector& At) const;
	TMap<TWeakObjectPtr<class AMultiplayTestPlayerController>, bool> ResultVerificationStatus;
	EMultiplayTestMatchPhase AuthoritativeMatchPhase = EMultiplayTestMatchPhase::Waiting;
	FString AuthoritativeMatchId;
	double MatchStartWorldSeconds = 0.0;
	double MatchEndWorldSeconds = 0.0;
	int32 PendingResultSubmissions = 0;
	bool bFinishCommitted = false;
	bool bMatchEndedByForfeit = false;
	bool bSurvivorVictoryByMannequinForfeit = false;
	bool bGlobalMannequinCatchDamageEnabled = true;
	/** This GameMode owns departure. Only the NEW map's GameMode may finalize arrival. */
	bool bLevelTransitionDeparting = false;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Match", meta = (ClampMin = "1", UIMin = "1"))
	int32 ExpectedMatchPlayers = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Spawning", meta = (ClampMin = "1", UIMin = "1"))
	int32 SurvivorSpawnCount = 2;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Spawning", meta = (ClampMin = "1", ClampMax = "10", UIMin = "1", UIMax = "10"))
    int32 MannequinSpawnCount = 10;
    FProjectCustomMatchRules CustomMatchRules;
    FProjectCustomMatchRules CustomRulesTicket; // As the ticket said; CustomMatchRules may also have the voted card on.
    FProjectMatchAffixes MatchAffixes;FString AffixesMatchId;
    FString CustomRulesMatchId;
    int32 StandardMannequinCount=10;
    float StandardTimeLimit=1800;
    bool bCustomDefaultsCaptured=false;

	/** 생존자 시작 지점에서 이 거리(cm) 안의 마네킹 후보는 먼 후보가 모자랄 때만 씁니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Spawning", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float MannequinSurvivorSpawnClearance = 2000.f;

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

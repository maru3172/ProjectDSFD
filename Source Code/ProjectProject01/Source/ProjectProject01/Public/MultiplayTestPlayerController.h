// File: Source/ProjectProject01/Public/MultiplayTestPlayerController.h
// Build target: ProjectProject01Server / ProjectProject01

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MultiplayMannequinTypes.h"
#include "ProjectProject01MatchTimeline.h"
#include "ProjectProject01VoiceAudibility.h"
#include "ProjectLoadingScreenSubsystem.h"
#include "MultiplayTestPlayerController.generated.h"

struct FProjectProject01ValidatedJoinClaim;

UENUM(BlueprintType)
enum class EProjectProject01PingType : uint8
{
	Help,
	Danger,
	Location
};

UENUM(BlueprintType)
enum class EMultiplayTestMatchPhase : uint8
{
	Waiting,
	RoleAssignment,
	InProgress,
	Ending,
	Results,
	ReturningToRoom
};

UENUM(BlueprintType)
enum class EMultiplayTestSurvivorState : uint8
{
	NotApplicable,
	Active,
	/** 다음 맵 이동 영역에 먼저 도착하여 남은 생존자를 관전하며 기다리는 상태입니다. */
	TransitionWaiting,
	Eliminated,
	Escaped
};

/** Owner-only camera intent survives RPC/Pawn replication order changes. */
USTRUCT()
struct FMultiplaySurvivorSpectatorView
{
	GENERATED_BODY()
	UPROPERTY() bool bEnabled = false;
	UPROPERTY() TObjectPtr<class APlayerCharacter> Target;
	UPROPERTY() bool bFadeTransition = false;
	UPROPERTY() uint32 RequestId = 0;
};

/** Final match-only statistics for the separately admitted observer; never a rating submission. */
USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectObservedPlayerResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) FString Role;
    UPROPERTY(BlueprintReadOnly) FString Outcome;
    UPROPERTY(BlueprintReadOnly) bool bSuccess=false;
    UPROPERTY(BlueprintReadOnly) int32 CaptureCount=0;
    UPROPERTY(BlueprintReadOnly) int32 RescueCount=0;
    UPROPERTY(BlueprintReadOnly) double FirstCaptureSeconds=0;
    UPROPERTY(BlueprintReadOnly) double AllCapturedSeconds=0;
    UPROPERTY(BlueprintReadOnly) double EscapeSeconds=0;
};

/** 서버가 확정하여 해당 소유 클라이언트에만 전달하는 한 경기의 결과입니다. */
USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FMultiplayTestMatchResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString MatchId;

	UPROPERTY(BlueprintReadOnly)
	FString Role;

	UPROPERTY(BlueprintReadOnly)
	FString Outcome;

	UPROPERTY(BlueprintReadOnly)
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly)
	bool bLeaderboardVerificationReady = false;

	/** 결과 화면은 먼저 표시됐지만 백엔드 기록 검증 응답은 아직 기다리는 중입니다. */
	UPROPERTY(BlueprintReadOnly)
	bool bLeaderboardVerificationPending = false;

	UPROPERTY(BlueprintReadOnly)
	double MatchDurationSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly)
	int32 CaptureCount = 0;

	UPROPERTY(BlueprintReadOnly)
	double FirstCaptureSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly)
	double AllCapturedSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly)
	int32 RescueCount = 0;

	UPROPERTY(BlueprintReadOnly)
	double EscapeSeconds = 0.0;

	/** Server-confirmed events delivered only after the match ends. */
	UPROPERTY(BlueprintReadOnly)
	TArray<FProjectMatchTimelineEvent> Timeline;
	UPROPERTY(BlueprintReadOnly)
	int32 OmittedTimelineEvents = 0;
    UPROPERTY(BlueprintReadOnly) TArray<FProjectObservedPlayerResult> ObservedPlayers;
};

/** 마네킹 슬롯 선택 입력을 서버에 요청하는 MultiplayTest 전용 컨트롤러입니다. */
UCLASS()
class PROJECTPROJECT01_API AMultiplayTestPlayerController : public APlayerController
{
	GENERATED_BODY()
	friend class FProjectSurvivorPresentationTest;

public:
	AMultiplayTestPlayerController();
    friend class UProjectMatchBots;
    friend class AProjectOpenObserverController;
    void AdjustObserverViewStream(int32 Delta);
    FRotator GetObserverViewRotation()const{return ObserverViewRotation;}
    UFUNCTION(Client,Reliable) void ClientObserverViewStream(bool Enabled);
    UFUNCTION(Server,Unreliable) void ServerObserverViewRotation(FRotator Rotation);
    bool IsServerControlledBot()const{return bServerControlledBot;}
    virtual bool IsLocalController()const override;
	friend class FProjectElevatorTest;
	friend class FProjectReviewMatchCarryoverTest;
	friend class FProjectSpectatorFadeTest;
	friend class FProjectDeadSpectatorTest;
	UFUNCTION(Client,Unreliable)
	void ClientUpdateVoiceAudibility(const FString& InMatchId,bool bAllowed,const TArray<FProjectVoiceAudibility>& Peers);
	class AMannequinAICharacter* GetViewedMannequin() const;
	void SetViewedMannequin(class AMannequinAICharacter* Mannequin);
	FRotator ResolveMannequinCameraRotation(class AMannequinAICharacter* Mannequin, float DeltaSeconds);
	bool IsMannequinCameraAligning() const { return bManualCameraAligning; }
	bool IsInitialGameplayPresentationReady() const { return !bInitialGameplayFadePending && !bInitialGameplayFadeScheduled; }
	float GetMannequinStatusBottom() const;
	bool IsManualMannequinCameraReady() const { return bCameraWasManual && !bManualCameraAligning; }
	void SetAuthenticatedLobbyIdentity(const FProjectProject01ValidatedJoinClaim& Claim);
	const FString& GetAuthenticatedDisplayName() const { return AuthenticatedDisplayName; }
	void SetServerMatchState(EMultiplayTestMatchPhase NewPhase, EMultiplayTestSurvivorState NewSurvivorState);
	void DeliverMatchResultToOwner(const FMultiplayTestMatchResult& Result);
	UFUNCTION(Client,Reliable)
	void ClientQueueDeathCamera(class APlayerCharacter* Victim,const FString& Reason);
	void DeclareVoluntaryExit();
	void EnterSurvivorSpectator(class AActor* InitialTarget, bool bFadeTransition = false);
	bool IsFreeSpectatingTarget(const AActor* Target) const;
	/** 서버의 seamless map travel에서 이 컨트롤러와 다음 맵 생존 여부를 보존합니다. */
	void PrepareLevelTransitionCarryover(bool bSpawnInNextLevel, float FadeSeconds = 0.6f);
	bool IsLevelTransitionCarryover() const { return bLevelTransitionCarryover; }
	bool IsLevelTransitionBlackReady() const { return bLevelTransitionCarryover && bServerTransitionBlackReady; }
	bool ShouldSpawnAfterLevelTransition() const { return bSpawnAfterLevelTransition; }
	void CompleteLevelTransitionCarryover(class AActor* InitialViewTarget, bool bCanPlay);
	void CaptureLevelTransitionState();
	void CancelLevelTransitionCarryover(bool bRestoreGameplay);
	const FString& GetAuthenticatedUserId() const { return AuthenticatedUserId; }
	const FString& GetAuthenticatedMatchId() const { return AuthenticatedMatchId; }
	const FString& GetAuthenticatedRole() const { return AuthenticatedRole; }
	bool IsLocalMannequinPlayer() const { return IsLocalController() && bLocalMultiplayerRoleKnown && bLocalMultiplayerMannequinRole; }
	EMultiplayTestMatchPhase GetMatchPhase() const { return MatchPhase; }
	EMultiplayTestSurvivorState GetSurvivorState() const { return SurvivorState; }
	bool HasQueuedManualMannequinCommand() const { return bManualMannequinCommandIssued; }
	UFUNCTION(Client,Reliable) void ClientNotifyCaseDiscovery(int32 Count);
	FString GetMannequinCommandNotice() const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual void SeamlessTravelFrom(APlayerController* OldPC) override;
	virtual void PostSeamlessTravel() override;
    virtual void ReceivedPlayer() override;
	virtual void PlayerTick(float DeltaTime) override;
	UFUNCTION(Client,Reliable) void ClientBeginLoadingGate(const FString& Epoch,const FString& OwnId,const FString& OwnName,int32 Expected,bool bReleased);
	UFUNCTION(Client,Unreliable) void ClientUpdateLoadingParticipants(const FString& Epoch,const TArray<FProjectLoadingParticipant>& Participants);
	UFUNCTION(Client,Reliable) void ClientReleaseLoadingGate(const FString& Epoch);
	UFUNCTION(Client,Reliable) void ClientFailLoadingGate(const FString& Message);
	UFUNCTION(Server,Reliable) void ServerReportLoadingStage(const FString& Epoch,uint8 Progress);
	bool TryPassLoadingGate();
	FString LoadingGateEpoch;
	bool bLoadingGateRequired=false,bLoadingGateReleased=false;
	uint8 LastReportedLoadingStage=0;

protected:
	virtual void BeginPlay() override;
    void InitializeLocalPresentation();
    bool bLocalPresentationInitialized=false;
	virtual void SetupInputComponent() override;
	virtual void OnRep_Pawn() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    bool bServerControlledBot=false;
    int32 ObserverViewConsumers=0;
    bool bSendObserverView=false;
    double NextObserverViewAt=0,LastObserverViewAt=-1;
    FRotator ObserverViewRotation=FRotator::ZeroRotator;
	UFUNCTION() void OnRep_SurvivorSpectatorView();
	void ApplySurvivorSpectatorView();
	void TryFinishSpectatorFade();
	void ReleaseLocalDeathBlackouts();
	bool bLocalMatchResultRequested = false;
	FMultiplayTestMatchResult PendingLocalMatchResult;
	FTimerHandle LocalMatchResultHandoffTimer;
	double LocalMatchResultHandoffDeadline = 0;
	bool bResultBlackoutStarted = false; // Result handoff blacked the screen out itself (once per match end).
	double LocalDeathCameraDeadline = 0;
	uint32 LastSpectatorFadeRequest = 0;
	uint32 SpectatorRequestSerial = 0;
	bool bSpectatorFadePending = false;
	double SpectatorSwitchReadyAt = 0;
	void TryCompleteClientArrivalView(float ReadyDeltaSeconds = 0.0f);
	void TryConfirmLevelTransitionBlack();
	uint32 TransitionFadeSerial = 0;
	uint32 LocalTransitionFadeSerial = 0;
	bool bServerTransitionBlackReady = false;
	bool bLocalTransitionBlackConfirmed = false;
	double LocalTransitionBlackSince = 0;
	TWeakObjectPtr<UWorld> ClientDepartureWorld;
	TWeakObjectPtr<AActor> ClientArrivalStableTarget;
	float ClientArrivalStableSeconds = 0;
	UPROPERTY(ReplicatedUsing=OnRep_SurvivorSpectatorView)
	FMultiplaySurvivorSpectatorView SurvivorSpectatorView;
	TWeakObjectPtr<AActor> LocalSpectatorLookTarget;
	bool bClientArrivalViewPending = false;
	bool bClientArrivalCanPlay = false;
	TWeakObjectPtr<AActor> ClientArrivalViewTarget;
	friend class FProjectMannequinCameraTest;
	void EnsureMannequinLookBinding();
	void RemoveMannequinLookBinding();
	void HandleMannequinLook(const struct FInputActionValue& Value);
	UPROPERTY(Transient)
	TObjectPtr<class UInputAction> MannequinCameraLookAction;
	UPROPERTY(Transient)
	TObjectPtr<class UInputMappingContext> MannequinCameraInputContext;
	uint32 MannequinLookBindingHandle = 0;
	TWeakObjectPtr<class AMannequinAICharacter> LocalCameraMannequin;
	FRotator LocalMannequinCameraRotation = FRotator::ZeroRotator;
	FRotator ManualCameraAlignFrom = FRotator::ZeroRotator;
	FRotator ManualCameraAlignTo = FRotator::ZeroRotator;
	float ManualCameraAlignSeconds = 0;
	bool bCameraWasManual = false;
	bool bManualCameraAligning = false;
	void SelectMannequinSlot0();
	void SelectMannequinSlot1();
	void SelectMannequinSlot2();
	void SelectMannequinSlot3();
	void SelectMannequinSlot4();
	void SelectMannequinSlot5();
	void SelectMannequinSlot6();
	void SelectMannequinSlot7();
	void SelectMannequinSlot8();
	void SelectMannequinSlot9();
	void RequestMannequinSlot(int32 Slot);
	void RequestMannequinManualControl();
	void RequestMannequinPatrolCommand();
	void RequestMannequinRangeChaseCommand();
	void RequestMannequinFollowCommand();
	void RequestResetFollowingMannequins();
	void RequestToggleMannequinCatchDamage();
	void RequestMannequinAttack();
	void ToggleSessionMenu();
	void SelectPreviousSpectatorTarget();
	void SelectNextSpectatorTarget();
	void RequestTeamPing(EProjectProject01PingType Type);
	void PollMultiplayerService();
	/** 권위 서버가 생존자 역할을 확인한 로컬 플레이어에게만 음성 상태 UI를 표시한다. */
	void SetSurvivorVoiceStatusVisible(bool bVisible);
	/** 마네킹 역할의 로컬 플레이어에게만 번호별 현재 행동 HUD를 표시한다. */
	void SetMannequinStatusVisible(bool bVisible);
	/** 결과 화면의 감쇄가 끝난 뒤 심박·신음·발소리 합성기를 완전히 정지합니다. */
	void StopMatchGameplayAudio();
	/** 최초 멀티 경기 진입은 역할별 시점 대상이 준비된 뒤에만 검은 화면을 걷어낸다. */
	void TryRevealInitialGameplayFade();
	void CompleteInitialGameplayFade();

	UFUNCTION()
	void HandleServiceStatusChanged(bool bMaintenanceEnabled, const FString& Announcement,
		const FString& ShutdownAtUtc, const FString& Message);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestMannequinSlot(int32 Slot);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestMannequinManualControl();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestMannequinCommand(EMultiplayMannequinCommand Command);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerResetFollowingMannequins();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerToggleMannequinCatchDamage();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestMannequinAttack();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDeclareVoluntaryExit();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCycleSurvivorSpectator(int32 Direction);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestTeamPing(EProjectProject01PingType Type);

	UFUNCTION(Client, Reliable)
	void ClientApplyViewedMannequin(class AMannequinAICharacter* Mannequin);

	UFUNCTION(Client, Reliable)
	void ClientPresentMatchResult(const FMultiplayTestMatchResult& Result);

	UFUNCTION(Client, Reliable)
	void ClientEnterSurvivorSpectator(const FMultiplaySurvivorSpectatorView& View);

	UFUNCTION(Client, Reliable)
	void ClientPrepareForLevelTransition(float FadeSeconds, uint32 RequestId);
	UFUNCTION(Server, Reliable)
	void ServerConfirmLevelTransitionBlack(uint32 RequestId);

	UFUNCTION(Client, Reliable)
	void ClientCompleteLevelTransition(class AActor* InitialViewTarget, bool bCanPlay);

	UFUNCTION(Client, Reliable)
	void ClientCaptureLevelTransitionState();
	UFUNCTION(Client, Reliable)
	void ClientCancelLevelTransition(bool bRestoreGameplay);

	UFUNCTION(Client, Unreliable)
	void ClientReceiveTeamPing(EProjectProject01PingType Type, FVector_NetQuantize Location, const FString& SenderName);

	UFUNCTION(Client, Reliable)
	void ClientConfigureSurvivorCommunication(const FString& UserId, const FString& MatchId, const FString& InRole);

	UFUNCTION(Client, Reliable)
	void ClientNotifyMannequinCatchDamage(bool bEnabled);

	UFUNCTION(Client, Reliable)
	void ClientNotifyMannequinCommand(const FString& Message);
	FString MannequinCommandNotice;
	float MannequinCommandNoticeUntil = 0;

	UFUNCTION(Client, Reliable)
	void ClientPlayMannequinCaptureFeedback(bool bElimination);

	UFUNCTION()
	void OnRep_ViewedMannequin();

	void ApplyViewedMannequinCamera(class AMannequinAICharacter* Mannequin);
	bool AllowServerRequest(uint8 RequestType, int32 MaximumCalls, float WindowSeconds);

	/** 소유 클라이언트가 Pawn 복제 이후에도 다시 적용할 수 있는 마지막 선택 시점 대상입니다. */
	UPROPERTY(ReplicatedUsing = OnRep_ViewedMannequin)
	TObjectPtr<class AMannequinAICharacter> ViewedMannequin;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01SessionMenuWidget> SessionMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01MatchResultWidget> MatchResultWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01ServiceNoticeWidget> ServiceNoticeWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01VoiceStatusWidget> VoiceStatusWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01MannequinStatusWidget> MannequinStatusWidget;

	UPROPERTY(VisibleAnywhere, Category="Feedback")
	TObjectPtr<class UMannequinCaptureSynthComponent> MannequinCaptureSFXComponent;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01MannequinCaptureFeedbackWidget> MannequinCaptureFeedbackWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01MannequinCaptureCameraModifier> MannequinCaptureCameraModifier;

	UPROPERTY(Replicated)
	EMultiplayTestMatchPhase MatchPhase = EMultiplayTestMatchPhase::Waiting;

	UPROPERTY(Replicated)
	EMultiplayTestSurvivorState SurvivorState = EMultiplayTestSurvivorState::NotApplicable;

	FString AuthenticatedUserId;
	FString AuthenticatedDisplayName;
	FString AuthenticatedMatchId;
	FString AuthenticatedRole;
	float RpcWindowStartSeconds[10] = {};
	int32 RpcWindowCallCount[10] = {};
	bool bVoluntaryExitDeclared = false;
	bool bHadAuthenticatedSession = false;
	/** 직접 조작 해제 후 적용할 명령이 지정됐는지 소유 클라이언트 HUD에도 복제한다. */
	UPROPERTY(Replicated)
	bool bManualMannequinCommandIssued = false;
	/** 서버에서 seamless travel 동안만 유지되는 다음 맵 진입 정보입니다. */
	bool bLevelTransitionCarryover = false;
	bool bSpawnAfterLevelTransition = false;
	struct FTransitionInputSnapshot
	{
		bool bCaptured = false;
		bool bMoveIgnored = false;
		bool bLookIgnored = false;
		TWeakObjectPtr<AActor> ViewTarget;
		void Capture(APlayerController* Controller);
		void Restore(APlayerController* Controller);
	};
	FTransitionInputSnapshot ServerTransitionSnapshot;
	FTransitionInputSnapshot LocalTransitionSnapshot;
	FTimerHandle SpectatorFadeTimer;
	/** 클라이언트 카메라가 맵 로딩 중 검은 화면을 유지해야 하는지 나타냅니다. */
	bool bLocalLevelTransitionFadeHeld = false;
	/** 새 경기 첫 화면이 Pawn/마네킹 시점 준비 전에 노출되는 것을 막는다. */
	bool bInitialGameplayFadePending = false;
	bool bInitialGameplayFadeScheduled = false;
	/** 월드/시점/스트리밍 준비가 연속으로 유지된 시간입니다. 로딩 중 화면 노출을 막습니다. */
	float InitialGameplayReadyStableSeconds = 0.0f;
	bool bLocalMultiplayerRoleKnown = false;
	bool bLocalMultiplayerMannequinRole = false;
	/** PIE 다중 클라이언트 테스트에서 이 마네킹 플레이어에게 가이드를 한 번만 표시한다. */
	bool bRoleGuideShownForThisController = false;
	FTimerHandle ServiceStatusTimer;
	FTimerHandle InitialGameplayRevealTimer;
	FTimerHandle InitialGameplayReadinessTimer;
	FTimerHandle MatchGameplayAudioStopTimer;

	friend class AMultiplayTestGameMode;
};

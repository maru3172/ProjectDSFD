// File: Source/ProjectProject01/Public/MultiplayTestPlayerController.h
// Build target: ProjectProject01Server / ProjectProject01

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MultiplayMannequinTypes.h"
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
};

/** 마네킹 슬롯 선택 입력을 서버에 요청하는 MultiplayTest 전용 컨트롤러입니다. */
UCLASS()
class PROJECTPROJECT01_API AMultiplayTestPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMultiplayTestPlayerController();
	class AMannequinAICharacter* GetViewedMannequin() const;
	void SetViewedMannequin(class AMannequinAICharacter* Mannequin);
	void SetAuthenticatedLobbyIdentity(const FProjectProject01ValidatedJoinClaim& Claim);
	void SetServerMatchState(EMultiplayTestMatchPhase NewPhase, EMultiplayTestSurvivorState NewSurvivorState);
	void DeliverMatchResultToOwner(const FMultiplayTestMatchResult& Result);
	void DeclareVoluntaryExit();
	void EnterSurvivorSpectator(class AActor* InitialTarget, bool bFadeTransition = false);
	/** 서버의 seamless map travel에서 이 컨트롤러와 다음 맵 생존 여부를 보존합니다. */
	void PrepareLevelTransitionCarryover(bool bSpawnInNextLevel);
	bool IsLevelTransitionCarryover() const { return bLevelTransitionCarryover; }
	bool ShouldSpawnAfterLevelTransition() const { return bSpawnAfterLevelTransition; }
	void CompleteLevelTransitionCarryover(class AActor* InitialViewTarget, bool bCanPlay);
	const FString& GetAuthenticatedUserId() const { return AuthenticatedUserId; }
	const FString& GetAuthenticatedMatchId() const { return AuthenticatedMatchId; }
	const FString& GetAuthenticatedRole() const { return AuthenticatedRole; }
	EMultiplayTestMatchPhase GetMatchPhase() const { return MatchPhase; }
	EMultiplayTestSurvivorState GetSurvivorState() const { return SurvivorState; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual void SeamlessTravelFrom(APlayerController* OldPC) override;
	virtual void PostSeamlessTravel() override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnRep_Pawn() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
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
	void ToggleSessionMenu();
	void SelectPreviousSpectatorTarget();
	void SelectNextSpectatorTarget();
	void RequestTeamPing(EProjectProject01PingType Type);
	void PollMultiplayerService();
	/** 권위 서버가 생존자 역할을 확인한 로컬 플레이어에게만 음성 상태 UI를 표시한다. */
	void SetSurvivorVoiceStatusVisible(bool bVisible);
	/** 최초 멀티 경기 진입은 역할별 시점 대상이 준비된 뒤에만 검은 화면을 걷어낸다. */
	void TryRevealInitialGameplayFade();

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
	void ClientEnterSurvivorSpectator(class AActor* InitialTarget, bool bFadeTransition);

	UFUNCTION(Client, Reliable)
	void ClientPrepareForLevelTransition();

	UFUNCTION(Client, Reliable)
	void ClientCompleteLevelTransition(class AActor* InitialViewTarget, bool bCanPlay);

	UFUNCTION(Client, Unreliable)
	void ClientReceiveTeamPing(EProjectProject01PingType Type, FVector_NetQuantize Location, const FString& SenderName);

	UFUNCTION(Client, Reliable)
	void ClientConfigureSurvivorCommunication(const FString& UserId, const FString& MatchId, const FString& InRole);

	UFUNCTION(Client, Reliable)
	void ClientNotifyMannequinCatchDamage(bool bEnabled);

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
	FString AuthenticatedMatchId;
	FString AuthenticatedRole;
	float RpcWindowStartSeconds[9] = {};
	int32 RpcWindowCallCount[9] = {};
	bool bVoluntaryExitDeclared = false;
	bool bHadAuthenticatedSession = false;
	/** V로 직접 조작한 현재 마네킹에 빙의 해제 후 적용할 명령(Z/X/C)이 지정되었는지 서버에서만 추적한다. */
	bool bManualMannequinCommandIssued = false;
	/** 서버에서 seamless travel 동안만 유지되는 다음 맵 진입 정보입니다. */
	bool bLevelTransitionCarryover = false;
	bool bSpawnAfterLevelTransition = false;
	/** 클라이언트 카메라가 맵 로딩 중 검은 화면을 유지해야 하는지 나타냅니다. */
	bool bLocalLevelTransitionFadeHeld = false;
	/** 새 경기 첫 화면이 Pawn/마네킹 시점 준비 전에 노출되는 것을 막는다. */
	bool bInitialGameplayFadePending = false;
	bool bLocalMultiplayerRoleKnown = false;
	bool bLocalMultiplayerMannequinRole = false;
	/** PIE 다중 클라이언트 테스트에서 이 마네킹 플레이어에게 가이드를 한 번만 표시한다. */
	bool bRoleGuideShownForThisController = false;
	FTimerHandle ServiceStatusTimer;

	friend class AMultiplayTestGameMode;
};

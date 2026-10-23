// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "ProjectProject01TuningData.h"
#include "ProjectProject01LocomotionFeedback.h"
#include "ProjectSurvivorPresentation.h"
#include "PlayerCharacter.generated.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
class AMannequinAICharacter;
class AHelperRearGuardCharacter;
class UPlayerHeartbeatComponent;
class UHeartbeatSynthComponent;
class UPainGroanSynthComponent;
class UBodyFallSynthComponent;
class UFootstepSynthComponent;
class UProjectProject01SessionMenuWidget;
class UProjectProject01DamageFeedbackWidget;
class UProjectProject01DeathFeedbackWidget;
class UProjectProject01TransientNoticeWidget;

class UPlayerInteractionComponent;

UCLASS()
class PROJECTPROJECT01_API APlayerCharacter : public ACharacter
{
	GENERATED_BODY()
	friend class AProjectTutorialTargetCharacter;
	friend class AProjectMatchBotCharacter;
	friend class FProjectFreePracticeRuntimeTest;

public:
	// Sets default values for this character's properties
	APlayerCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// AI가 직접 추격을 시작할 내부 반경(cm)이다.
	UFUNCTION(BlueprintPure, Category = "AI|Range")
	float GetDirectChaseRadius() const;

	// AI가 배회할 고리 영역의 외부 반경(cm)이다.
	UFUNCTION(BlueprintPure, Category = "AI|Range")
	float GetRoamingOuterRadius() const;
	
	UFUNCTION(BlueprintPure, Category = "AI|Direction")
	float GetDirectChaseHalfAngleDegrees() const;

	// 플레이어 중심에서 직접 추격 부채꼴 끝까지의 반경(cm)이다.
	UFUNCTION(BlueprintPure, Category = "AI|Direction")
	float GetDirectChaseSectorRadius() const;
	
	UFUNCTION(BlueprintPure, Category = "AI|Direction")
	FVector GetAIMovementDirection() const;

	/** 서버가 적 AI DataTable의 검증된 추적 기준값을 적용한다. */
	void ApplyMannequinTuning(const FMannequinAITuningRow& Tuning);

	/** 서버가 Player DataTable의 검증된 이동·심박 값을 적용한다. */
	void ApplyPlayerTuning(const FPlayerTuningRow& Tuning);

	/** 남은 포획 허용 횟수입니다. 0이면 파트너가 제거된 상태입니다. */
	UFUNCTION(BlueprintPure, Category = "Player|Death")
	int32 GetRemainingDeathCount() const { return RemainingDeathCount; }

	/** 데스카운트가 0인 상태에서 다시 포획돼 게임 오버가 되었는지 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "Player|Death")
	bool IsGameOver() const { return bGameOver; }
	/** Only the active local survivor hears their own ambient breathing, never title/results/spectating. */
	bool CanPlayTensionBreathing() const;
	bool IsSprintingForBreathing() const { return bIsSprinting && GetVelocity().SizeSquared2D() > 400.0f; }
	/** Local player currently seeing this pawn, including a dead free-look observer. */
	APlayerController* GetLocalPresentationController() const;
	bool IsLocallyObserved() const;
	bool IsDeathPresentationPending() const;
	FRotator GetPresentationRotationOffset() const;
	const FProjectSurvivorPresentation& GetObserverPresentation() const { return ObserverPresentation; }

	/** 마네킹 서버가 물리 접촉 포획을 확정할 때만 호출합니다. */
	bool HandleMannequinCatch(AMannequinAICharacter* CatchingMannequin);

	/** 서버 GameMode가 게임 규칙별 시작 데스카운트를 설정한다. */
	void SetRemainingDeathCountForGameMode(int32 NewDeathCount);

	/** 싱글 자동 체크포인트 진입 시 이번 판의 시작 데스카운트로 회복합니다. */
	void ResetDeathCountAtCheckpoint();

	/** 싱글플레이의 서버 권위 승패 판정이 암전 결과 화면을 한 번만 표시한다. */
	void PresentSinglePlayerResult(bool bEscaped);

	/** 파트너가 실제 후퇴 이동 중 접촉을 확인했을 때만 차감합니다. 게임 오버는 발생시키지 않습니다. */
	bool HandlePartnerPushDeath(AHelperRearGuardCharacter* PushingHelper);
	void GetDiagnosticAppliedPlayerTuning(FPlayerTuningRow& OutTuning) const;

	/** 멀티 GameMode가 사망 카메라와 암전이 끝나기 전에 관전 시점으로 잘라 바꾸지 않도록 기다릴 시간입니다. */
	float GetDeathSequenceHandoffDelaySeconds() const;

	UFUNCTION(BlueprintPure, Category = "Player|Stamina")
	float GetCurrentStamina() const { return CurrentStamina; }

	UFUNCTION(BlueprintPure, Category = "Player|Stamina")
	float GetMaxStamina() const { return MaxStamina; }

	/** 검증된 싱글플레이 저장 파일의 상태를 현재 Pawn에 복원합니다. */
	void RestoreSinglePlayerState(const FTransform& SavedTransform, int32 SavedDeathCount, float SavedStamina,
		const FVector& SavedVelocity = FVector::ZeroVector,
		const FRotator& RestoredControlRotation = FRotator::ZeroRotator);

	/** 저장된 로컬 키 설정을 현재 Enhanced Input 컨텍스트에 다시 적용합니다. */
	void ApplyUserInputSettings();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void PawnClientRestart() override;
	virtual void PossessedBy(AController* NewController) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;
	/** Local overlay handoff only; never changes gameplay death/respawn state. */
	void ReleaseLocalDeathBlackout(const APlayerController* PresentationOwner);
	bool HasLocalDeathBlackout(const APlayerController* PresentationOwner) const;
	float GetLocalDeathPresentationRemaining(const APlayerController* PresentationOwner) const;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	friend class FProjectDeadSpectatorTest;
	friend class FProjectSurvivorPresentationTest;
	UFUNCTION(Server, Unreliable)
	void ServerUpdateObserverPresentation(const FProjectSurvivorPresentation& Presentation);
	UPROPERTY(Replicated) FProjectSurvivorPresentation ObserverPresentation;
	UPROPERTY(Replicated) FProjectSurvivorFeedback ObserverFeedback;
	void RecordObserverFeedback(float Pitch,float Roll,float Severity,bool Fatal);
	void UpdateObserverPresentation(float DeltaTime);
	double LastPresentationSent=-1;
	double LastPresentationReceived=-1;
	uint32 LastObservedFeedbackSerial=0;
	bool bWasLocallyObserved=false;
	/** 서버가 Player DataTable 값을 바꾼 뒤 소유 클라이언트의 로컬 심박 계산에도 같은 값을 적용합니다. */
	UFUNCTION(Client, Reliable)
	void ClientApplyPlayerTuning(const FPlayerTuningRow& Tuning);
	/** 서버 ApplyPlayerTuning과 클라이언트 RPC가 공유하는 수치·컴포넌트 적용 본문입니다. 복제/RPC는 하지 않습니다. */
	void ApplyPlayerTuningValues(const FPlayerTuningRow& Tuning);

	UFUNCTION(Client, Reliable)
	void ClientPlayDamageFeedback(float FallPitchDirection, float FallRollDirection, float DamageSeverity);

	UFUNCTION(Client, Reliable)
	void ClientPlayDeathSequence(float FallPitchDirection, float FallRollDirection);

	/** PIE 네트워크 실행에서도 싱글 GameMode가 확정한 탈출 결과를 소유 클라이언트에 표시합니다. */
	UFUNCTION(Client, Reliable)
	void ClientPresentSinglePlayerResult(bool bEscaped);

	/** 서버가 확정한 신음과 사망 충돌음을 모든 근처 클라이언트가 월드 공간음으로 재생합니다. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayPainAndDeathWorldSFX(bool bFatal, float Intensity);

	UPROPERTY(VisibleAnywhere, Category = "Component")
	class USpringArmComponent* SpringArm;
	UPROPERTY(VisibleAnywhere, Category = "Component")
	class UCameraComponent* Camera;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputMappingContext* IMC_PlayerInput;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputAction* IA_Move;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputAction* IA_Look;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputAction* IA_Sprint;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RuntimeSprintInputContext;
	
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputAction* IA_Interact;

	// 입력 이벤트 발생 시 실행할 함수
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();
	void SetSprinting(bool bNewSprinting);
	void InitializeLocalPlayerInput();
	void TryRevealInitialStandaloneFade();
	void RegisterRuntimeSprintMapping(UEnhancedInputLocalPlayerSubsystem* InputSubsystem);
	void ToggleSessionMenu();
	void BindSinglePlayerSaveNotifications();

	UFUNCTION()
	void HandleCheckpointSaveCompleted(bool bSucceeded, const FString& Message);

	UPROPERTY(Transient)
	TObjectPtr<UProjectProject01SessionMenuWidget> SessionMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectProject01TransientNoticeWidget> CheckpointNoticeWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01MatchResultWidget> SinglePlayerResultWidget;

	bool bSinglePlayerResultPresented = false;
	bool bInitialStandaloneSpawnFadePlayed = false;
	float InitialStandaloneReadyStableSeconds = 0.0f;
	float InitialStandaloneRevealFadeSeconds = 1.5f;
	/** PIE 다중 클라이언트 테스트에서 공유 설정과 무관하게 이 생존자에게 가이드를 한 번만 표시한다. */
	bool bRoleGuideShownForThisPawn = false;
	FTimerHandle SinglePlayerDeathResultTimer;
	FTimerHandle InitialStandaloneRevealTimer;

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetSprinting(bool bNewSprinting);

	UFUNCTION(Client, Reliable)
	void ClientCorrectSprinting(bool bAuthoritativeSprinting);
	
	// 서버가 권한을 가지며 모든 클라이언트에 복제하는 달리기 상태다.
	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting, VisibleInstanceOnly, Category = "Player",
		meta = (AllowPrivateAccess = "true"))
	bool bIsSprinting = false;
	float SprintRpcWindowStartSeconds = 0.0f;
	int32 SprintRpcWindowCallCount = 0;

	// 작은 원: 이 반경 안에서는 마네킹 AI가 직접 추격하는 용도로 사용한다.
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "AI|Range",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float DirectChaseRadius = 500.0f;

	// 큰 원: DirectChaseRadius와 이 반경 사이를 마네킹 AI의 랜덤 이동 영역으로 사용한다.
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "AI|Range",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float RoamingOuterRadius = 1500.0f;

	// 서버에서 DataTable로 설정하고 소유 클라이언트에도 복제하는 이동 속도(cm/s)다.
	UPROPERTY(ReplicatedUsing = OnRep_PlayerWalkSpeed, VisibleInstanceOnly, Category = "Player",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm/s"))
	float PlayerWalkSpeed = 600.0f;
	
	// 서버 DataTable로 설정하고 모든 클라이언트에 복제하는 달리기 최대 속도(cm/s)다.
	UPROPERTY(ReplicatedUsing = OnRep_PlayerSprintSpeed, VisibleInstanceOnly, Category = "Player",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm/s"))
	float PlayerSprintSpeed = 750.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Player|Stamina", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float MaxStamina = 100.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Player|Stamina", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StaminaDrainPerSecond = 5.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Player|Stamina", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float StaminaRecoveryPerSecond = 3.0f;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Player|Stamina", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float CurrentStamina = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Player|Stamina|Debug", meta = (AllowPrivateAccess = "true"))
	bool bShowStaminaDebug = true;

	// PIE/Development 플레이 중 두 범위를 디버그 원으로 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Range|Debug",
		meta = (AllowPrivateAccess = "true"))
	bool bShowAIRangeDebug = true;

	void DrawAIRangeDebug();
	
	// 이동 방향을 중심으로 직접 추격 영역이 펼쳐지는 좌우 반각이다.
	// 90도면 현재와 동일하게 앞뒤 반원으로 나뉜다.
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "AI|Direction", 
		meta = (
			AllowPrivateAccess = "true",
			ClampMin = "0.0",
			ClampMax = "180.0",
			UIMin = "0.0",
			UIMax = "180.0",
			Units = "deg"
		))
	float DirectChaseHalfAngleDegrees = 90.0f;

	// 직접 추격 부채꼴은 내부원 밖에서 시작해 이 반경까지 이어진다.
	// 서버 DataTable 값을 모든 클라이언트에 일관되게 표시·판정하도록 복제한다.
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "AI|Direction",
		meta = (
			AllowPrivateAccess = "true",
			ClampMin = "0.0",
			UIMin = "0.0",
			Units = "cm"
		))
	float DirectChaseSectorRadius = 1000.0f;

	void ApplyPlayerWalkSpeed();
	void ApplySprintingState(bool bNewSprinting);
	void UpdateStamina(float DeltaTime);
	void DrawStaminaDebug() const;
	AHelperRearGuardCharacter* ResolveStaminaHelper();
	void ApplyGameOverState();
	void RemoveGuardingHelpers();
	void ResolveFallDirection(const FVector& HitSourceLocation, float& OutPitchDirection, float& OutRollDirection) const;
	void PlayLocalDamageFeedback(float FallPitchDirection, float FallRollDirection, float DamageSeverity,
		bool bAlsoShowVFX = true);
	void PlayLocalDeathSequence(float FallPitchDirection, float FallRollDirection);
	void UpdateCameraFeedback(float DeltaTime);
	void RestoreFeedbackCamera();
	bool CanUseLocomotionCamera() const;
	void HandleFootContact(bool bRunning, bool bRightFoot);
	FProjectProject01LocomotionFeedback LocomotionCameraFeedback;
	friend class FProjectLocomotionFeedbackTest;
	friend class FProjectReviewRuntimeRegressionTest;
	void PlayPainSFX(bool bFatal, float Intensity);
	void PlayBodyFallSFX();

	UFUNCTION()
	void OnRep_PlayerWalkSpeed();

	UFUNCTION()
	void OnRep_PlayerSprintSpeed();

	UFUNCTION()
	void OnRep_IsSprinting();

	UFUNCTION()
	void OnRep_GameOver();

	/** 세 번의 포획까지 차감되는 생존자별 카운트입니다. 서버가 변경하고 모든 클라이언트에 복제합니다. */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Player|Death", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 RemainingDeathCount = 3;

	/** 게임 모드가 정한 이번 판의 시작값이며 자동 체크포인트 회복에 사용합니다. */
	int32 StartingDeathCount = 3;

	/** 카운트 0 이후 다음 포획에서 서버가 확정하는 게임 오버 상태입니다. */
	UPROPERTY(ReplicatedUsing = OnRep_GameOver, VisibleInstanceOnly, Category = "Player|Death", meta = (AllowPrivateAccess = "true"))
	bool bGameOver = false;
	
	FVector LastAIMovementDirection = FVector::ZeroVector;
	
	// 플레이어 심박수 관련 변수
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat", meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UPlayerHeartbeatComponent> HeartbeatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Heartbeat|SFX", meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UHeartbeatSynthComponent> HeartbeatSFXComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Damage|SFX", meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UPainGroanSynthComponent> PainGroanSFXComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Death|SFX", meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UBodyFallSynthComponent> BodyFallSFXComponent;

	/** 대리석 위 운동화 걷기·달리기 효과음을 로컬에서 생성합니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Footstep|SFX", meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UFootstepSynthComponent> FootstepSFXComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectProject01DamageFeedbackWidget> DamageFeedbackWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectProject01DeathFeedbackWidget> DeathFeedbackWidget;

	float DamageFeedbackIntroSeconds = 0.1f;
	float DamageFeedbackFadeSeconds = 2.0f;
	float DamageFeedbackMaxOpacity = 0.7f;
	float DamageFeedbackMaxThicknessPixels = 150.0f;
	float PainGroanSFXVolume = 0.9f;
	float DamageCameraStumbleSeconds = 0.08f;
	float DamageCameraRecoverySeconds = 0.24f;
	float DamageCameraTiltDegrees = 6.0f;
	float DeathCameraStumbleSeconds = 0.16f;
	float DeathCameraFallSeconds = 0.9f;
	float DeathBlackFadeSeconds = 0.65f;
	float DeathMessageHoldSeconds = 0.85f;
	float DeathCameraDropCentimeters = 120.0f;
	float DeathCameraTiltDegrees = 82.0f;
	float BodyFallSFXVolume = 0.95f;
	double NextDeathCountDamageAllowedWorldSeconds = -1.0;
	bool bCameraHitFeedbackActive = false;
	bool bCameraDeathFeedbackActive = false;
	bool bBodyFallSFXPlayed = false;
	FTimerHandle BodyFallWorldSFXTimer;
	float CameraFeedbackElapsedSeconds = 0.0f;
	float CameraFallPitchDirection = 0.0f;
	float CameraFallRollDirection = 0.0f;
	FVector FeedbackCameraStartLocation = FVector::ZeroVector;
	FRotator FeedbackCameraStartRotation = FRotator::ZeroRotator;
	bool bStaminaInitialized = false;
	TWeakObjectPtr<AHelperRearGuardCharacter> StaminaHelper;
	
	// 플레이어와 아이템의 상호작용 관련
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Interaction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPlayerInteractionComponent> InteractionComponent;

	// =========================================================================================================================
	// 카메라 B 키 디버깅 관련
	// =========================================================================================================================

	// PIE 표시 화면만 탑뷰로 전환한다. AI 시야 카메라는 변경하지 않는다.
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "Debug|Top View")
    bool bUseTopViewInPIE = false;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_Player_DebugCamera;

    UPROPERTY(VisibleAnywhere, Category = "Debug|Top View")
    TObjectPtr<class UCameraComponent> DebugTopViewCamera;

	friend class FProjectProject01TopViewExtension;
    void ToggleDebugCamera();
    void RestoreDebugControls();
    void UpdateDebugCursorAim();
    TWeakObjectPtr<class APlayerController> DebugInputController;
    FMatrix DebugClipToWorld = FMatrix::Identity;
    FIntRect DebugViewRect = FIntRect(0, 0, 0, 0);
    FRotator SavedControlRotation = FRotator::ZeroRotator;
    FRotator SavedCameraRelativeRotation = FRotator::ZeroRotator;
    bool bHasDebugView = false;
    bool bDebugControlsActive = false;
    bool bSkipNextLookInput = false;
    bool bSavedUseControllerRotationYaw = false;
    bool bSavedOrientRotationToMovement = false;
    bool bSavedUseControllerDesiredRotation = false;
    bool bSavedCameraUsePawnControlRotation = false;
    TSharedPtr<class FProjectProject01TopViewExtension, ESPMode::ThreadSafe> TopViewExtension;
};

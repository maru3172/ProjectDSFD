// File: Source/ProjectProject01/Public/ProjectProject01GameInstance.h
// Build target: ProjectProject01 / ProjectProject01Server, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/NetworkDelegates.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "ProjectRequestCircuit.h"
#include "ProjectProject01GameInstance.generated.h"

class SBorder;
struct FProjectMatchTimelineLog;
struct FMultiplayTestMatchCarryover;

UENUM(BlueprintType)
enum class EProjectProject01VFXIntensity : uint8
{
	Standard,
	Reduced
};

UENUM(BlueprintType)
enum class EProjectProject01ColorVisionMode : uint8
{
	Normal,
	Deuteranopia,
	Protanopia,
	Tritanopia
};

/** 로컬 PC에만 저장되는 화면·음향·입력 환경설정입니다. */
UCLASS(Config=GameUserSettings)
class PROJECTPROJECT01_API UProjectProject01GameUserSettings final : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UProjectProject01GameUserSettings(const FObjectInitializer& ObjectInitializer);

	static UProjectProject01GameUserSettings* Get();

	virtual void SetToDefaults() override;
	virtual void LoadSettings(bool bForceReload = false) override;
	virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;
	virtual void ApplyNonResolutionSettings() override;

	void ValidateProjectSettings();
	void ApplyProjectSettings(bool bApplicationActive = true);
	void ApplyAudioSettings(bool bApplicationActive = true);
	/** 사용자 설정값은 보존한 채, 사망·경기 종료 연출 동안 게임 효과음만 서서히 낮춥니다. */
	void FadeGameplaySFX(float TargetMultiplier = 0.12f, float FadeSeconds = 1.5f);
	/** 다음 플레이를 시작할 때 일시적인 효과음 감쇄를 원래 사용자 음량으로 복구합니다. */
	void RestoreGameplaySFX(float FadeSeconds = 0.15f);

	float GetMasterVolume() const { return MasterVolume; }
	float GetSFXVolume() const { return SFXVolume; }
	float GetMusicVolume() const { return MusicVolume; }
	float GetUIVolume() const { return UIVolume; }
	bool IsMuteAllEnabled() const { return bMuteAll; }
	bool IsMuteWhenUnfocusedEnabled() const { return bMuteWhenUnfocused; }
	bool IsMotionBlurEnabled() const { return bMotionBlurEnabled; }
	float GetDisplayGammaSetting() const { return DisplayGammaSetting; }
	float GetMouseSensitivity() const { return MouseSensitivity; }
	EProjectProject01VFXIntensity GetVFXIntensity() const { return VFXIntensity; }
	float GetLocalVFXScale() const { return VFXIntensity == EProjectProject01VFXIntensity::Reduced ? 0.4f : 1.0f; }
	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Accessibility")
	bool AreSubtitlesEnabled() const { return bSubtitlesEnabled; }
	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Accessibility")
	bool AreEnhancedVisualCuesEnabled() const { return bEnhancedVisualCues; }
	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Accessibility")
	EProjectProject01ColorVisionMode GetColorVisionMode() const { return ColorVisionMode; }
	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Accessibility")
	float GetColorVisionSeverity() const { return ColorVisionSeverity; }
	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Accessibility")
	float GetScreenFlashScale() const { return ScreenFlashScale; }
	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Accessibility")
	float GetScreenDistortionScale() const { return ScreenDistortionScale; }
	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Accessibility")
	float GetScreenShakeScale() const { return bCameraShakeEnabled ? ScreenShakeScale : 0.0f; }
	bool IsCameraShakeEnabled() const { return bCameraShakeEnabled; }
	float GetConfiguredScreenShakeScale() const { return ScreenShakeScale; }
	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Accessibility")
	float GetUIReadableScale() const { return UIReadableScale; }
	float GetLocalFlashScale() const { return GetLocalVFXScale() * ScreenFlashScale; }
	float GetLocalDistortionScale() const { return GetLocalVFXScale() * ScreenDistortionScale; }
	const FString& GetDirectServerAddress() const { return DirectServerAddress; }
	FKey GetMoveForwardKey() const;
	FKey GetMoveBackwardKey() const;
	FKey GetMoveLeftKey() const;
	FKey GetMoveRightKey() const;
	FKey GetSprintKey() const;
	FKey GetVoicePushToTalkKey() const;
	FKey GetVoiceToggleKey() const;
	FKey GetHelpPingKey() const;
	FKey GetDangerPingKey() const;
	FKey GetLocationPingKey() const;
	bool HasSeenSurvivorGuide() const { return bHasSeenSurvivorGuide; }
	bool HasSeenMannequinGuide() const { return bHasSeenMannequinGuide; }
	void MarkSurvivorGuideSeen();
	void MarkMannequinGuideSeen();

	void SetMasterVolume(float Value) { MasterVolume = Value; }
	void SetSFXVolume(float Value) { SFXVolume = Value; }
	void SetMusicVolume(float Value) { MusicVolume = Value; }
	void SetUIVolume(float Value) { UIVolume = Value; }
	void SetMuteAllEnabled(bool bValue) { bMuteAll = bValue; }
	void SetMuteWhenUnfocusedEnabled(bool bValue) { bMuteWhenUnfocused = bValue; }
	void SetMotionBlurEnabled(bool bValue) { bMotionBlurEnabled = bValue; }
	void SetDisplayGammaSetting(float Value) { DisplayGammaSetting = Value; }
	void SetMouseSensitivity(float Value) { MouseSensitivity = Value; }
	void SetVFXIntensity(EProjectProject01VFXIntensity Value) { VFXIntensity = Value; }
	void SetSubtitlesEnabled(bool bValue) { bSubtitlesEnabled = bValue; }
	void SetEnhancedVisualCuesEnabled(bool bValue) { bEnhancedVisualCues = bValue; }
	void SetColorVisionMode(EProjectProject01ColorVisionMode Value) { ColorVisionMode = Value; }
	void SetColorVisionSeverity(float Value) { ColorVisionSeverity = Value; }
	void SetScreenFlashScale(float Value) { ScreenFlashScale = Value; }
	void SetScreenDistortionScale(float Value) { ScreenDistortionScale = Value; }
	void SetScreenShakeScale(float Value) { ScreenShakeScale = Value; }
	void SetCameraShakeEnabled(bool bValue) { bCameraShakeEnabled = bValue; }
	void SetUIReadableScale(float Value) { UIReadableScale = Value; }
	void SetDirectServerAddress(const FString& Value) { DirectServerAddress = Value; }
	void SetMoveForwardKey(FKey Key) { MoveForwardKeyName = Key.GetFName(); }
	void SetMoveBackwardKey(FKey Key) { MoveBackwardKeyName = Key.GetFName(); }
	void SetMoveLeftKey(FKey Key) { MoveLeftKeyName = Key.GetFName(); }
	void SetMoveRightKey(FKey Key) { MoveRightKeyName = Key.GetFName(); }
	void SetSprintKey(FKey Key) { SprintKeyName = Key.GetFName(); }
	void SetVoicePushToTalkKey(FKey Key) { VoicePushToTalkKeyName = Key.GetFName(); }
	void SetVoiceToggleKey(FKey Key) { VoiceToggleKeyName = Key.GetFName(); }
	void SetHelpPingKey(FKey Key) { HelpPingKeyName = Key.GetFName(); }
	void SetDangerPingKey(FKey Key) { DangerPingKeyName = Key.GetFName(); }
	void SetLocationPingKey(FKey Key) { LocationPingKeyName = Key.GetFName(); }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Audio")
	class USoundClass* GetSFXSoundClass();

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Audio")
	class USoundClass* GetMusicSoundClass();

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Settings|Audio")
	class USoundClass* GetUISoundClass();

private:
	void EnsureRuntimeAudioObjects();
	FKey GetValidatedKey(FName StoredName, FKey Fallback) const;

	UPROPERTY(Config)
	float MasterVolume = 1.0f;

	UPROPERTY(Config)
	float SFXVolume = 1.0f;

	UPROPERTY(Config)
	float MusicVolume = 1.0f;

	UPROPERTY(Config)
	float UIVolume = 1.0f;

	UPROPERTY(Config)
	bool bMuteAll = false;

	UPROPERTY(Config)
	bool bMuteWhenUnfocused = true;

	UPROPERTY(Config)
	bool bMotionBlurEnabled = true;

	UPROPERTY(Config)
	float DisplayGammaSetting = 2.2f;

	UPROPERTY(Config)
	float MouseSensitivity = 1.0f;

	UPROPERTY(Config)
	EProjectProject01VFXIntensity VFXIntensity = EProjectProject01VFXIntensity::Standard;

	UPROPERTY(Config)
	bool bSubtitlesEnabled = true;

	UPROPERTY(Config)
	bool bEnhancedVisualCues = true;

	UPROPERTY(Config)
	EProjectProject01ColorVisionMode ColorVisionMode = EProjectProject01ColorVisionMode::Normal;

	UPROPERTY(Config)
	float ColorVisionSeverity = 5.0f;

	UPROPERTY(Config)
	float ScreenFlashScale = 1.0f;

	UPROPERTY(Config)
	float ScreenDistortionScale = 1.0f;

	UPROPERTY(Config)
	float ScreenShakeScale = 1.0f;

	UPROPERTY(Config)
	bool bCameraShakeEnabled = true;

	UPROPERTY(Config)
	float UIReadableScale = 1.0f;

	UPROPERTY(Config)
	FString DirectServerAddress;

	UPROPERTY(Config)
	FName MoveForwardKeyName = TEXT("W");

	UPROPERTY(Config)
	FName MoveBackwardKeyName = TEXT("S");

	UPROPERTY(Config)
	FName MoveLeftKeyName = TEXT("A");

	UPROPERTY(Config)
	FName MoveRightKeyName = TEXT("D");

	UPROPERTY(Config)
	FName SprintKeyName = TEXT("LeftShift");

	UPROPERTY(Config)
	FName VoicePushToTalkKeyName = TEXT("F2");

	UPROPERTY(Config)
	FName VoiceToggleKeyName = TEXT("U");

	UPROPERTY(Config)
	FName HelpPingKeyName = TEXT("E");

	UPROPERTY(Config)
	FName DangerPingKeyName = TEXT("R");

	UPROPERTY(Config)
	FName LocationPingKeyName = TEXT("T");

	/** 역할별 최초 게임 가이드 표시 여부. 환경설정 초기화와 별도로 유지한다. */
	UPROPERTY(Config)
	bool bHasSeenSurvivorGuide = false;

	UPROPERTY(Config)
	bool bHasSeenMannequinGuide = false;

	UPROPERTY(Transient)
	TObjectPtr<class USoundClass> RuntimeSFXSoundClass;

	UPROPERTY(Transient)
	TObjectPtr<class USoundClass> RuntimeMusicSoundClass;

	UPROPERTY(Transient)
	TObjectPtr<class USoundClass> RuntimeUISoundClass;

	UPROPERTY(Transient)
	TObjectPtr<class USoundMix> RuntimeSoundMix;

	bool bRuntimeSoundMixPushed = false;
	float GameplaySFXMultiplier = 1.0f;
};

struct PROJECTPROJECT01_API FProjectProject01ValidatedJoinClaim
{
	FString UserId;
	FString DisplayName;
	FString RoomId;
	FString MatchId;
	FString Role;
	FString ClientBuildVersion;
	FString DedicatedServerBuildVersion;
	FString ApiVersion;
	FString GameDataVersion;
	FString NetworkProtocolVersion;
	FDateTime ValidUntilUtc;
};

struct PROJECTPROJECT01_API FProjectProject01SurveyQuestion
{
	FString Id;
	FString Type;
	FString Prompt;
	TArray<FString> Options;
	bool bRequired = true;
	bool bAllowMultiple = false;
	int32 MaxSelections = 1;
	void ApplySelection(TArray<int32>& Selected, const int32 OptionIndex, const bool bChecked) const
	{
		if (!Options.IsValidIndex(OptionIndex)) return;
		Selected.Remove(OptionIndex);
		if (bChecked) Selected.Add(OptionIndex);
		const int32 Limit = bAllowMultiple ? FMath::Clamp(MaxSelections > 0 ? MaxSelections : Options.Num(), 1, FMath::Max(1, Options.Num())) : 1;
		while (Selected.Num() > Limit) Selected.RemoveAt(0);
	}
};

struct PROJECTPROJECT01_API FProjectProject01SurveyDefinition
{
	FString SurveyId;
	FString Title;
	FString Description;
	TArray<FProjectProject01SurveyQuestion> Questions;
	bool IsValid() const { return !SurveyId.IsEmpty() && !Title.IsEmpty() && !Questions.IsEmpty(); }
};

struct PROJECTPROJECT01_API FProjectProject01SurveyAnswer
{
	FString QuestionId;
	FString Value;
	TArray<FString> Values;
};

/** 인증 티켓과 UE 5.8 AES-GCM 네트워크 암호화 핸드셰이크를 연결합니다. */
UCLASS(Config=Game)
class PROJECTPROJECT01_API UProjectProject01GameInstance final : public UGameInstance
{
	GENERATED_BODY()

public:
	/** Auth/Lobby transport protection is per local game instance, never replicated or persisted. */
	FProjectRequestCircuit BackendRequestCircuit;
	/** Process-local: returning to title does not replay startup logos. No account/save state is changed. */
	bool ConsumeStartupIntro() { if (bStartupIntroShown) return false; bStartupIntroShown = true; return true; }
	/** Server-only transient history; never replicated and never written to account/save data. */
	TSharedPtr<FProjectMatchTimelineLog> ServerMatchTimeline;
    TSharedPtr<FMultiplayTestMatchCarryover> ServerMatchCarryover;
	virtual void Init() override;
	virtual void Shutdown() override;
    void ArmLevelTransitionWatchdog();
    void CancelLevelTransitionWatchdog();
	virtual void ReceivedNetworkEncryptionToken(
		const FString& EncryptionToken,
		const FOnEncryptionKeyResponse& Delegate) override;
	virtual void ReceivedNetworkEncryptionAck(const FOnEncryptionKeyResponse& Delegate) override;

	/** 사용자가 직접 입력한 서버 IPv4 주소를 인증·로비·보안 API 전체에 한 번에 적용합니다. */
	bool ConfigureMultiplayerServerAddress(
		const FString& ServerAddress,
		FString& OutError);
	FString GetConfiguredMultiplayerServerAddress() const;

	/** 로비 API가 발급한 티켓과 키를 네트워크 이동 직전에 메모리에만 보관합니다. */
	bool ConfigurePendingGameConnection(
		const FString& Ticket,
		const FString& EncryptionKeyBase64,
		const FString& RoomId,
		const FString& MatchId,
		const FString& Role);

	/** 경기 접속을 끝내거나 취소할 때 메모리에 남은 일회용 티켓과 키를 제거합니다. */
	void ClearPendingGameConnection();

	/** PreLogin이 암호화 핸드셰이크에서 검증된 claim을 한 번 가져옵니다. */
	bool ConsumeValidatedJoinClaim(
		const FString& Ticket,
		FProjectProject01ValidatedJoinClaim& OutClaim);

	/** 실제 경기 종료 로직에서 서버만 호출할 검증 결과 제출 경계입니다. */
	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Security")
	void SubmitAuthoritativeMatchResult(
		const FString& MatchId,
		const FString& UserId,
		const FString& Role,
		bool bSuccess,
		int32 CaptureCount,
		double FirstCaptureSeconds,
		double AllCapturedSeconds,
		int32 RescueCount,
		double EscapeSeconds);

	/** 서버 검증 저장이 끝난 뒤 결과 화면을 열 수 있도록 성공 여부를 돌려주는 네이티브 경로입니다. */
	void SubmitAuthoritativeMatchResultWithCallback(
		const FString& MatchId,
		const FString& UserId,
		const FString& Role,
		bool bSuccess,
		int32 CaptureCount,
		double FirstCaptureSeconds,
		double AllCapturedSeconds,
		int32 RescueCount,
		double EscapeSeconds,
		TFunction<void(bool)> Completion);

	/** 재접속 유예시간이 만료된 플레이어를 백엔드 방에서도 서버 권위로 정리합니다. */
	void NotifyAuthoritativeMatchEnded(const FString& MatchId);
	void NotifyAuthoritativePlayerForfeit(const FString& MatchId, const FString& UserId, const FString& Role);

	/** 데디케이티드 서버가 실행 중임을 백엔드에 알립니다. 서버 월드에서만 전송됩니다. */
	void SendGameServerHeartbeat();

	/** 현재 운영자가 활성화한 설문을 조회합니다. 활성 설문이 없으면 bHasSurvey=false입니다. */
	void FetchActivePlayerSurvey(
		TFunction<void(bool, bool, const FProjectProject01SurveyDefinition&, const FString&)> Completion);
	/** 타이틀 설문 진입 의도만 유지하며 인증 정보는 별도 AuthSubsystem이 관리합니다. */
	void SetSurveyLoginRequested(bool bRequested) { bSurveyLoginRequested = bRequested; }
	bool IsSurveyLoginRequested() const { return bSurveyLoginRequested; }
	bool ConsumeSurveyLoginRequest()
	{
		const bool bRequested = bSurveyLoginRequested;
		bSurveyLoginRequested = false;
		return bRequested;
	}

	/** 로그인한 계정의 동적 설문 답변을 백엔드에 저장합니다. */
	void SubmitPlayerSurvey(
		const FProjectProject01SurveyDefinition& Survey,
		const TArray<FProjectProject01SurveyAnswer>& Answers,
		TFunction<void(bool, const FString&)> Completion);

	/** 싱글 레벨 전환 트리거가 다음 월드의 짧은 0.6초 암전 해제를 예약합니다. */
	void MarkPendingSinglePlayerLevelTransitionFade();
	bool ConsumePendingSinglePlayerLevelTransitionFade();

	/** 카메라뿐 아니라 UMG까지 덮는 최상단 화면 전체 페이드입니다. GameInstance와 함께 맵 이동을 통과합니다. */
	void FadeEntireScreenToBlack(float DurationSeconds);
	void HoldEntireScreenBlack();
	void FadeEntireScreenFromBlack(float DurationSeconds);
	void ClearEntireScreenFade();
	bool IsEntireScreenBlack() const { return !bEntireScreenFadeAnimating && EntireScreenFadeOpacity >= 1.0f; }

private:
	void HandleApplicationActivationChanged(bool bApplicationActive);
	friend class FProjectSpectatorFadeTest;
	void HandleNetworkFailure(UWorld* World, class UNetDriver* NetDriver,
		ENetworkFailure::Type FailureType, const FString& ErrorString);
	void BeginSafeLogoutAfterGameServerFailure();
	bool bStartupIntroShown = false;
	bool bPendingSinglePlayerLevelTransitionFade = false;
	bool bSurveyLoginRequested = false;
	FString ServerBootId;
	double ManagedHeartbeatFailureSince = 0.0;
	void EnsureEntireScreenFadeOverlay();
	void ApplyEntireScreenFadeOpacity();
	bool TickEntireScreenFade(float DeltaSeconds);

	UFUNCTION()
	void HandleSafeLogoutCompleted(bool bSuccess, const FString& Message, const FString& DisplayName);

	UFUNCTION()
	void HandleAuthenticationSessionInvalidated(const FString& Message);
	void ReturnToLoginAndClearMultiplayerState();
	void InitializeCrashReportCollection();
	void RefreshCrashReportContext(UWorld* World);
	void RecoverPendingCrashReports();
	void UploadPendingCrashReport(const FString& ReportPath);
	void UploadCrashAttachment(const FString& ReportPath, const FString& ReportId, const TArray<FString>& Files, int32 Index);
	void WriteActiveCrashMarker(UWorld* World);
	FString BuildSafeCrashReportJson(UWorld* World) const;
	FString LoadGameServerSharedSecret() const;
	bool IsBackendUrlAllowed() const;
	void RemovePendingRequest(const TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe>& Request);

	UPROPERTY(Config)
	FString SecurityApiBaseUrl = TEXT("http://127.0.0.1:5080");

	/** Hamachi 등 암호화된 개발 VPN에서만 허용할 정확한 HTTP API 주소입니다. 운영 배포에서는 비워 둡니다. */
	UPROPERTY(Config)
	FString AllowedInsecureVpnSecurityApiBaseUrl;

	UPROPERTY(Transient)
	FString RuntimeMultiplayerServerAddress;

	UPROPERTY(Config)
	float SecurityRequestTimeoutSeconds = 8.0f;

	TArray<TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe>> PendingSecurityRequests;
	FDelegateHandle ApplicationActivationHandle;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle PostWorldInitializationHandle;
	FTSTicker::FDelegateHandle CrashUploadRetryTickerHandle;
	FTSTicker::FDelegateHandle EntireScreenFadeTickerHandle;
	TSharedPtr<SBorder> EntireScreenFadeOverlay;
	TWeakObjectPtr<class UGameViewportClient> EntireScreenFadeViewport;
	float EntireScreenFadeOpacity = 0.0f;
	float EntireScreenFadeStartOpacity = 0.0f;
	float EntireScreenFadeTargetOpacity = 0.0f;
	float EntireScreenFadeElapsedSeconds = 0.0f;
	float EntireScreenFadeDurationSeconds = 0.0f;
	bool bEntireScreenFadeAnimating = false;
	bool bEntireScreenFadeAttached = false;
	TSet<FString> CrashReportsInFlight;
	FString CrashRunId;
	FString CrashStartedAtUtc;
	FString ActiveCrashMarkerPath;
	FString PendingCrashMatchId;
	FString PendingCrashPlayerRole;
	bool bHandlingFailedGameConnection = false;
    friend class FProjectNetworkIsolationTest;
    friend class AProjectProject01LevelTransitionTrigger;
    void AbortStalledLevelTravel();
    FTSTicker::FDelegateHandle LevelTransitionWatchdog;
    static UProjectProject01GameInstance* ResolveEncryptionOwner(const UObject* CallbackOwner);
    static void BindEncryptionDispatchers();
    friend class FProjectReviewAsyncStateTest;
    friend class FProjectLoginRegistrationTest;
    bool IsSurveySessionCurrent(uint64 Generation,const UWorld* World) const;
    bool IsMultiplayerFailureWorld(const UWorld* World) const;
    TArray<uint8> PendingClientEncryptionKey;
    FString PendingClientTicket;
    TMap<FString,FProjectProject01ValidatedJoinClaim> ValidatedClaims;
	bool bHandlingSessionInvalidation = false;
};

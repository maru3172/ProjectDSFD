// File: Source/ProjectProject01/Public/ProjectProject01GameInstance.h
// Build target: ProjectProject01 / ProjectProject01Server, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/NetworkDelegates.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "ProjectProject01GameInstance.generated.h"

UENUM(BlueprintType)
enum class EProjectProject01VFXIntensity : uint8
{
	Standard,
	Reduced
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
	FKey GetMoveForwardKey() const;
	FKey GetMoveBackwardKey() const;
	FKey GetMoveLeftKey() const;
	FKey GetMoveRightKey() const;
	FKey GetSprintKey() const;

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
	void SetMoveForwardKey(FKey Key) { MoveForwardKeyName = Key.GetFName(); }
	void SetMoveBackwardKey(FKey Key) { MoveBackwardKeyName = Key.GetFName(); }
	void SetMoveLeftKey(FKey Key) { MoveLeftKeyName = Key.GetFName(); }
	void SetMoveRightKey(FKey Key) { MoveRightKeyName = Key.GetFName(); }
	void SetSprintKey(FKey Key) { SprintKeyName = Key.GetFName(); }

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
	FName MoveForwardKeyName = TEXT("W");

	UPROPERTY(Config)
	FName MoveBackwardKeyName = TEXT("S");

	UPROPERTY(Config)
	FName MoveLeftKeyName = TEXT("A");

	UPROPERTY(Config)
	FName MoveRightKeyName = TEXT("D");

	UPROPERTY(Config)
	FName SprintKeyName = TEXT("LeftShift");

	UPROPERTY(Transient)
	TObjectPtr<class USoundClass> RuntimeSFXSoundClass;

	UPROPERTY(Transient)
	TObjectPtr<class USoundClass> RuntimeMusicSoundClass;

	UPROPERTY(Transient)
	TObjectPtr<class USoundClass> RuntimeUISoundClass;

	UPROPERTY(Transient)
	TObjectPtr<class USoundMix> RuntimeSoundMix;

	bool bRuntimeSoundMixPushed = false;
};

struct PROJECTPROJECT01_API FProjectProject01ValidatedJoinClaim
{
	FString UserId;
	FString DisplayName;
	FString RoomId;
	FString MatchId;
	FString Role;
	FDateTime ValidUntilUtc;
};

/** 인증 티켓과 UE 5.8 AES-GCM 네트워크 암호화 핸드셰이크를 연결합니다. */
UCLASS(Config=Game)
class PROJECTPROJECT01_API UProjectProject01GameInstance final : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;
	virtual void ReceivedNetworkEncryptionToken(
		const FString& EncryptionToken,
		const FOnEncryptionKeyResponse& Delegate) override;
	virtual void ReceivedNetworkEncryptionAck(const FOnEncryptionKeyResponse& Delegate) override;

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
	static bool ConsumeValidatedJoinClaim(
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

private:
	void HandleApplicationActivationChanged(bool bApplicationActive);
	FString LoadGameServerSharedSecret() const;
	bool IsBackendUrlAllowed() const;
	void RemovePendingRequest(const TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe>& Request);

	UPROPERTY(Config)
	FString SecurityApiBaseUrl = TEXT("http://127.0.0.1:5080");

	UPROPERTY(Config)
	float SecurityRequestTimeoutSeconds = 8.0f;

	TArray<TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe>> PendingSecurityRequests;
	FDelegateHandle ApplicationActivationHandle;
};

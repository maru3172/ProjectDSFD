#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "HttpFwd.h"
#include "ProjectProject01VoiceChatSubsystem.generated.h"

class IVoiceChat;
class IVoiceChatUser;
struct FVoiceChatResult;

UCLASS(Config=Game)
class PROJECTPROJECT01_API UProjectProject01VoiceChatSubsystem final
	: public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bConfiguredForSurvivor; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

	void ConfigureForMatch(const FString& UserId, const FString& MatchId, const FString& Role,
		class APlayerController* LocalController);
	void LeaveMatch();
	void SetPushToTalkHeld(bool bHeld);
	void ToggleOpenMic();

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Voice")
	bool IsVoiceReady() const { return bJoinedChannel; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Voice")
	bool IsOpenMicEnabled() const { return bOpenMic; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Voice")
	bool IsConfiguredForSurvivor() const { return bConfiguredForSurvivor; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Voice")
	bool IsPushToTalkHeld() const { return bPushToTalkHeld; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Voice")
	bool IsTransmitting() const { return bJoinedChannel && (bOpenMic || bPushToTalkHeld); }

private:
	void ApplyRuntimeEosConfiguration();
	void BeginProductUserLogin();
	void LoginWithDeviceId();
	void CreateProductUser(void* ContinuanceToken);
	void CompleteProductUserLogin(void* ProductUserId);
	void BeginConnect();
	void BeginLogin();
	void BeginJoin();
	void RequestJoinCredential();
	void HandleJoinCredentialResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
	void ApplyTransmitMode();
	void HandleInitialized(const FVoiceChatResult& Result);
	void HandleConnected(const FVoiceChatResult& Result);
	void HandleLoggedIn(const FString& PlayerName, const FVoiceChatResult& Result);
	void HandleJoined(const FString& JoinedChannel, const FVoiceChatResult& Result);
	void HandleChannelExited(const FString& ExitedChannel, const FVoiceChatResult& Reason);
	void ReportFailure(const TCHAR* Stage, const FVoiceChatResult& Result);

	UPROPERTY(Config)
	bool bEnableSurvivorProximityVoice = true;

	/** 개발 빌드 전용입니다. Shipping에서는 코드가 강제로 무시합니다. */
	UPROPERTY(Config)
	bool bAllowInsecureDevelopmentCredentials = true;

	/** EOS 클라이언트 비밀키는 이 이름의 환경변수에서만 읽으며 ini/Git에 기록하지 않는다. */
	UPROPERTY(Config)
	FString GameClientSecretEnvironmentVariable = TEXT("PROJECTPROJECT01_EOS_GAME_CLIENT_SECRET");

	UPROPERTY(Config, meta=(ClampMin="0.0", Units="cm"))
	float FullVolumeDistance = 350.0f;

	UPROPERTY(Config, meta=(ClampMin="1.0", Units="cm"))
	float MaximumAudibleDistance = 1800.0f;

	UPROPERTY(Config, meta=(ClampMin="0.01"))
	float DistanceRolloff = 1.0f;

	/** 위치 음성 갱신 간격. 기본 10Hz로 네트워크/플러그인 호출 비용을 제한한다. */
	UPROPERTY(Config, meta=(ClampMin="0.02", Units="s"))
	float PositionUpdateIntervalSeconds = 0.1f;

	TWeakObjectPtr<class APlayerController> OwnerController;
	IVoiceChat* VoiceChat = nullptr;
	IVoiceChatUser* VoiceUser = nullptr;
	FString LocalUserId;
	FString MatchId;
	FString ChannelName;
	FString PendingJoinCredential;
	bool bConfiguredForSurvivor = false;
	bool bJoinedChannel = false;
	bool bProductUserLoginPending = false;
	bool bJoinCredentialPending = false;
	bool bPushToTalkHeld = false;
	bool bOpenMic = false;
	float PositionUpdateAccumulatorSeconds = 0.0f;
	float RejoinDelaySeconds = 0.0f;
	FDelegateHandle ChannelExitedDelegateHandle;
};

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
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

private:
	void BeginConnect();
	void BeginLogin();
	void BeginJoin();
	void ApplyTransmitMode();
	void HandleInitialized(const FVoiceChatResult& Result);
	void HandleConnected(const FVoiceChatResult& Result);
	void HandleLoggedIn(const FString& PlayerName, const FVoiceChatResult& Result);
	void HandleJoined(const FString& JoinedChannel, const FVoiceChatResult& Result);
	void ReportFailure(const TCHAR* Stage, const FVoiceChatResult& Result);

	UPROPERTY(Config)
	bool bEnableSurvivorProximityVoice = true;

	/** 개발 빌드 전용입니다. Shipping에서는 코드가 강제로 무시합니다. */
	UPROPERTY(Config)
	bool bAllowInsecureDevelopmentCredentials = true;

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
	FString ChannelName;
	bool bConfiguredForSurvivor = false;
	bool bJoinedChannel = false;
	bool bPushToTalkHeld = false;
	bool bOpenMic = false;
	float PositionUpdateAccumulatorSeconds = 0.0f;
};

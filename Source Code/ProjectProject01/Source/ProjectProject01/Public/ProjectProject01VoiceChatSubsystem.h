#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "HttpFwd.h"
#include "ProjectProject01VoiceParticipantControls.h"
#include "ProjectProject01VoiceAudibility.h"
#include "ProjectProject01VoiceChatSubsystem.generated.h"

class IVoiceChat;
class IVoiceChatUser;
struct FVoiceChatResult;
namespace Audio { class FAudioCapture; }

struct FProjectVoiceDevice { FString Id; FString Name; };
enum class EProjectVoiceConnectionPhase : uint8 { Inactive, Initializing, Identity, Connecting, Login, Token, Joining, Ready, RetryWaiting, Failed, Suspended };

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
	void SuspendForLevelTravel();
	void RetryVoiceConnection();
	void UpdateAuthoritativeAudibility(const FString& InMatchId,bool bAllowed,const TArray<FProjectVoiceAudibility>& Peers);
	float CalculateDistanceGain(float Distance) const { return ProjectVoiceAudibility::DistanceGain(Distance,FullVolumeDistance,MaximumAudibleDistance,DistanceRolloff); }
	FString GetConnectionStatusText() const;
	bool CanRetryVoiceConnection() const { return bConfiguredForSurvivor && ConnectionPhase==EProjectVoiceConnectionPhase::Failed; }
	void SetPushToTalkHeld(bool bHeld);
	void ToggleOpenMic();
	bool PrepareAudioDevices();
	TArray<FProjectVoiceDevice> GetInputDevices() const;
	TArray<FProjectVoiceDevice> GetOutputDevices() const;
	const FString& GetPreferredInput() const { return PreferredInput; }
	const FString& GetPreferredOutput() const { return PreferredOutput; }
	float GetReceiveVolume() const { return ReceiveVolume; }
	void SetAudioPreferences(const FString& Input, const FString& Output, float Volume);
	TArray<FString> GetVoiceParticipants() const;
	TArray<FProjectVoiceParticipantState> GetVoiceParticipantStates() const;
	FString GetParticipantDisplayName(const FString& Id) const;
	void SetParticipantMuted(const FString& Id, bool bMuted);
	bool IsParticipantMuted(const FString& Id) const { return ParticipantControls.Muted.Contains(Id); }
	void SetParticipantVolume(const FString& Id, float Volume);
	float GetParticipantVolume(const FString& Id) const { return ParticipantControls.GetVolume(Id); }
	void ResetParticipantPreferences();
	bool StartMicrophoneTest(const FString& InputId, FString& Error);
	void StopMicrophoneTest();
	bool IsMicrophoneTestActive() const { return MicrophoneCapture.IsValid(); }
	float GetMicrophoneLevel() const { return MicrophoneLevel->Load(); }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Voice")
	bool IsVoiceReady() const { return bJoinedChannel; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Voice")
	bool IsOpenMicEnabled() const { return bOpenMic; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Voice")
	bool IsConfiguredForSurvivor() const { return bConfiguredForSurvivor; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Voice")
	bool IsPushToTalkHeld() const { return bPushToTalkHeld; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Voice")
	bool IsTransmitting() const { return bJoinedChannel && HasFreshAudibility() && bAuthoritativeVoiceAllowed && !IsMicrophoneTestActive() && (bOpenMic || bPushToTalkHeld); }

private:
	friend class FProjectVoiceParticipantIsolationTest;
	friend class FProjectVoiceLifecycleTest;
	friend class FProjectVoiceAudibilityTest;
	void StopSession(bool bPreservePreferences);
	void CancelVoiceRequests();
	void BeginConnectionAttempt();
	void FailConnection(const FString& Message, bool bRetryable=true);
	void SetConnectionPhase(EProjectVoiceConnectionPhase Phase);
	bool IsCurrentSession(uint64 Generation) const { return bConfiguredForSurvivor && Generation==SessionGeneration; }
	uint64 SessionGeneration=1;
	EProjectVoiceConnectionPhase ConnectionPhase=EProjectVoiceConnectionPhase::Inactive;
	FString ConnectionFailure;
	FString AccountUserId;
	int32 ConnectionRetryCount=0;
	float ConnectionStageSeconds=0;
	FHttpRequestPtr JoinCredentialRequest;
	FHttpRequestPtr RosterRequest;
	TMap<FString,FString> ParticipantNames;
	float RosterRefreshSeconds=0;
	void RefreshParticipantNames();
	void ApplyParticipantNames(const TSharedPtr<class FJsonObject>& Json);
	void ApplyAudioPreferences();
	FString PreferredInput;
	FString PreferredOutput;
	float ReceiveVolume = 1.0f;
	bool bDeviceInitializationPending = false;
	FProjectVoiceParticipantControls ParticipantControls;
	TMap<FString,FString> ParticipantAccountIds;
	TMap<FString,float> AuthoritativePeerGains;
	double LastAudibilityUpdateSeconds=0;
	bool bAuthoritativeVoiceAllowed=false;
	TMap<FString,float> AppliedPeerGains;
	int8 AppliedTransmitState=-1;
	bool HasFreshAudibility() const;
	float GetParticipantDistanceGain(const FString& Id) const;
	void ApplyParticipantReceiveSettings(const FString& Id);
	void HandleParticipantAdded(const FString& JoinedChannel, const FString& ParticipantId);
	TSharedPtr<Audio::FAudioCapture> MicrophoneCapture;
	TSharedRef<TAtomic<float>, ESPMode::ThreadSafe> MicrophoneLevel = MakeShared<TAtomic<float>, ESPMode::ThreadSafe>(0.0f);
	void ApplyRuntimeEosConfiguration();
	void BeginProductUserLogin();
	void LoginWithDeviceId();
	void CreateProductUser(void* ContinuanceToken);
	void CompleteProductUserLogin(void* ProductUserId);
	void BeginConnect();
	void BeginLogin();
	void BeginJoin();
	void RequestJoinCredential(bool bIdentityRefreshOnly=false);
	void HandleJoinCredentialResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, uint64 Generation, bool bIdentityRefreshOnly);
	void ApplyTransmitMode();
	void HandleInitialized(const FVoiceChatResult& Result);
	void HandleConnected(const FVoiceChatResult& Result, uint64 Generation);
	void HandleLoggedIn(const FString& PlayerName, const FVoiceChatResult& Result, uint64 Generation);
	void HandleJoined(const FString& JoinedChannel, const FVoiceChatResult& Result, uint64 Generation);
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
	float AppliedReceiveVolume = -1.0f;
	void UpdateReceiveVolume();
	FDelegateHandle ChannelExitedDelegateHandle;
	FDelegateHandle ParticipantAddedDelegateHandle;
};

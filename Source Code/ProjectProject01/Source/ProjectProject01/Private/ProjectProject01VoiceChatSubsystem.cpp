#include "ProjectProject01VoiceChatSubsystem.h"

#include "VoiceChat.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectProject01Voice, Log, All);

void UProjectProject01VoiceChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UProjectProject01VoiceChatSubsystem::Deinitialize()
{
	LeaveMatch();
	if (VoiceChat != nullptr && VoiceUser != nullptr)
	{
		VoiceChat->ReleaseUser(VoiceUser);
	}
	VoiceUser = nullptr;
	VoiceChat = nullptr;
	Super::Deinitialize();
}

TStatId UProjectProject01VoiceChatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectProject01VoiceChatSubsystem, STATGROUP_Tickables);
}

void UProjectProject01VoiceChatSubsystem::ConfigureForMatch(
	const FString& UserId,
	const FString& MatchId,
	const FString& Role,
	APlayerController* LocalController)
{
	LeaveMatch();
	if (!bEnableSurvivorProximityVoice || !Role.Equals(TEXT("Survivor"), ESearchCase::IgnoreCase) ||
		UserId.IsEmpty() || MatchId.IsEmpty() || !IsValid(LocalController))
	{
		return;
	}

	bConfiguredForSurvivor = true;
	OwnerController = LocalController;
	LocalUserId = UserId;
	ChannelName = FString::Printf(TEXT("pp01-survivors-%s"), *MatchId.Replace(TEXT("-"), TEXT("")));
	VoiceChat = IVoiceChat::Get();
	if (VoiceChat == nullptr)
	{
		UE_LOG(LogProjectProject01Voice, Warning,
			TEXT("VoiceChat provider is unavailable. Configure EOSVoiceChat credentials to enable survivor voice."));
		return;
	}
	if (!VoiceChat->IsInitialized())
	{
		VoiceChat->Initialize(FOnVoiceChatInitializeCompleteDelegate::CreateUObject(
			this, &UProjectProject01VoiceChatSubsystem::HandleInitialized));
		return;
	}
	BeginConnect();
}

void UProjectProject01VoiceChatSubsystem::LeaveMatch()
{
	bConfiguredForSurvivor = false;
	bPushToTalkHeld = false;
	bOpenMic = false;
	if (VoiceUser != nullptr)
	{
		VoiceUser->TransmitToNoChannels();
		if (bJoinedChannel && !ChannelName.IsEmpty())
		{
			VoiceUser->LeaveChannel(ChannelName, FOnVoiceChatChannelLeaveCompleteDelegate());
		}
	}
	bJoinedChannel = false;
	PositionUpdateAccumulatorSeconds = 0.0f;
	OwnerController.Reset();
	ChannelName.Reset();
}

void UProjectProject01VoiceChatSubsystem::SetPushToTalkHeld(const bool bHeld)
{
	bPushToTalkHeld = bHeld;
	ApplyTransmitMode();
}

void UProjectProject01VoiceChatSubsystem::ToggleOpenMic()
{
	if (!bConfiguredForSurvivor) return;
	bOpenMic = !bOpenMic;
	ApplyTransmitMode();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, bOpenMic ? FColor::Green : FColor::Silver,
			bOpenMic ? TEXT("생존자 근접 음성: 상시 송신 켜짐") : TEXT("생존자 근접 음성: 눌러서 말하기"));
	}
}

void UProjectProject01VoiceChatSubsystem::Tick(float DeltaTime)
{
	if (!bJoinedChannel || VoiceUser == nullptr || ChannelName.IsEmpty()) return;
	PositionUpdateAccumulatorSeconds += DeltaTime;
	if (PositionUpdateAccumulatorSeconds < FMath::Max(0.02f, PositionUpdateIntervalSeconds)) return;
	PositionUpdateAccumulatorSeconds = 0.0f;
	APlayerController* Controller = OwnerController.Get();
	APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	if (IsValid(Pawn))
	{
		VoiceUser->Set3DPosition(ChannelName, Pawn->GetActorLocation());
	}
}

void UProjectProject01VoiceChatSubsystem::BeginConnect()
{
	if (!bConfiguredForSurvivor || VoiceChat == nullptr) return;
	if (VoiceChat->IsConnected())
	{
		BeginLogin();
		return;
	}
	VoiceChat->Connect(FOnVoiceChatConnectCompleteDelegate::CreateUObject(
		this, &UProjectProject01VoiceChatSubsystem::HandleConnected));
}

void UProjectProject01VoiceChatSubsystem::BeginLogin()
{
	if (!bConfiguredForSurvivor || VoiceChat == nullptr) return;
	if (VoiceUser == nullptr) VoiceUser = VoiceChat->CreateUser();
	if (VoiceUser == nullptr)
	{
		UE_LOG(LogProjectProject01Voice, Error, TEXT("VoiceChat provider could not create a local user."));
		return;
	}
	if (VoiceUser->IsLoggedIn())
	{
		BeginJoin();
		return;
	}

	FString Credential;
#if !UE_BUILD_SHIPPING
	if (bAllowInsecureDevelopmentCredentials)
	{
		Credential = VoiceUser->InsecureGetLoginToken(LocalUserId);
	}
#endif
	if (Credential.IsEmpty())
	{
		UE_LOG(LogProjectProject01Voice, Warning,
			TEXT("Voice login credential is unavailable. Shipping must obtain a short-lived EOS voice token from the backend."));
		return;
	}
	VoiceUser->Login(FPlatformUserId::CreateFromInternalId(0), LocalUserId, Credential,
		FOnVoiceChatLoginCompleteDelegate::CreateUObject(this, &UProjectProject01VoiceChatSubsystem::HandleLoggedIn));
}

void UProjectProject01VoiceChatSubsystem::BeginJoin()
{
	if (!bConfiguredForSurvivor || VoiceUser == nullptr || ChannelName.IsEmpty()) return;
	FVoiceChatChannel3dProperties Properties;
	Properties.AttenuationModel = EVoiceChatAttenuationModel::InverseByDistance;
	Properties.MinDistance = FMath::Max(0.0f, FullVolumeDistance);
	Properties.MaxDistance = FMath::Max(Properties.MinDistance + 1.0f, MaximumAudibleDistance);
	Properties.Rolloff = FMath::Max(0.01f, DistanceRolloff);

	FString Credential;
#if !UE_BUILD_SHIPPING
	if (bAllowInsecureDevelopmentCredentials)
	{
		Credential = VoiceUser->InsecureGetJoinToken(ChannelName, EVoiceChatChannelType::Positional, Properties);
	}
#endif
	if (Credential.IsEmpty())
	{
		UE_LOG(LogProjectProject01Voice, Warning,
			TEXT("Voice channel credential is unavailable. Backend-issued EOS join tokens are required for shipping."));
		return;
	}
	VoiceUser->JoinChannel(ChannelName, Credential, EVoiceChatChannelType::Positional,
		FOnVoiceChatChannelJoinCompleteDelegate::CreateUObject(this, &UProjectProject01VoiceChatSubsystem::HandleJoined),
		Properties);
}

void UProjectProject01VoiceChatSubsystem::ApplyTransmitMode()
{
	if (VoiceUser == nullptr || !bJoinedChannel) return;
	if (bOpenMic || bPushToTalkHeld)
	{
		TSet<FString> Channels;
		Channels.Add(ChannelName);
		VoiceUser->TransmitToSpecificChannels(Channels);
	}
	else
	{
		VoiceUser->TransmitToNoChannels();
	}
}

void UProjectProject01VoiceChatSubsystem::HandleInitialized(const FVoiceChatResult& Result)
{
	if (!Result.IsSuccess()) { ReportFailure(TEXT("Initialize"), Result); return; }
	BeginConnect();
}

void UProjectProject01VoiceChatSubsystem::HandleConnected(const FVoiceChatResult& Result)
{
	if (!Result.IsSuccess()) { ReportFailure(TEXT("Connect"), Result); return; }
	BeginLogin();
}

void UProjectProject01VoiceChatSubsystem::HandleLoggedIn(const FString& PlayerName, const FVoiceChatResult& Result)
{
	if (!Result.IsSuccess()) { ReportFailure(TEXT("Login"), Result); return; }
	BeginJoin();
}

void UProjectProject01VoiceChatSubsystem::HandleJoined(const FString& JoinedChannel, const FVoiceChatResult& Result)
{
	if (!Result.IsSuccess()) { ReportFailure(TEXT("JoinChannel"), Result); return; }
	bJoinedChannel = bConfiguredForSurvivor && JoinedChannel == ChannelName;
	ApplyTransmitMode();
	UE_LOG(LogProjectProject01Voice, Log, TEXT("Joined survivor-only positional voice channel %s."), *JoinedChannel);
}

void UProjectProject01VoiceChatSubsystem::ReportFailure(const TCHAR* Stage, const FVoiceChatResult& Result)
{
	UE_LOG(LogProjectProject01Voice, Warning, TEXT("Voice %s failed: %s"), Stage, *LexToString(Result));
}

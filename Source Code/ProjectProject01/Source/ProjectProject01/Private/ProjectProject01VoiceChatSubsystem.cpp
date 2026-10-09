#include "ProjectProject01VoiceChatSubsystem.h"

#include "ProjectProject01AuthSubsystem.h"
#include "EOSVoiceChat.h"
#include "IEOSSDKManager.h"
#include "VoiceChat.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "eos_connect.h"
#include "eos_sdk.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectProject01Voice, Log, All);

namespace ProjectProject01Voice
{
	struct FCallbackContext
	{
		TWeakObjectPtr<UProjectProject01VoiceChatSubsystem> Owner;
	};

	EOS_HConnect GetConnectHandle(IVoiceChat* VoiceChat)
	{
		if (VoiceChat == nullptr) return nullptr;
		FEOSVoiceChat* EosVoiceChat = static_cast<FEOSVoiceChat*>(VoiceChat);
		const IEOSPlatformHandlePtr PlatformHandle = EosVoiceChat->GetPlatformHandle();
		return PlatformHandle.IsValid()
			? EOS_Platform_GetConnectInterface(static_cast<EOS_HPlatform>(*PlatformHandle))
			: nullptr;
	}
}

void UProjectProject01VoiceChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UProjectProject01VoiceChatSubsystem::Deinitialize()
{
	LeaveMatch();
	if (VoiceChat != nullptr && VoiceUser != nullptr)
	{
		if (ChannelExitedDelegateHandle.IsValid())
		{
			VoiceUser->OnVoiceChatChannelExited().Remove(ChannelExitedDelegateHandle);
			ChannelExitedDelegateHandle.Reset();
		}
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
	const FString& InMatchId,
	const FString& Role,
	APlayerController* LocalController)
{
	LeaveMatch();
	if (!bEnableSurvivorProximityVoice || !Role.Equals(TEXT("Survivor"), ESearchCase::IgnoreCase) ||
		UserId.IsEmpty() || InMatchId.IsEmpty() || !IsValid(LocalController))
	{
		return;
	}

	bConfiguredForSurvivor = true;
	OwnerController = LocalController;
	LocalUserId.Reset();
	MatchId = InMatchId;
	ChannelName = FString::Printf(TEXT("pp01-survivors-%s"), *InMatchId.Replace(TEXT("-"), TEXT("")));
	ApplyRuntimeEosConfiguration();
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
	BeginProductUserLogin();
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
	bProductUserLoginPending = false;
	bJoinCredentialPending = false;
	PositionUpdateAccumulatorSeconds = 0.0f;
	RejoinDelaySeconds = 0.0f;
	OwnerController.Reset();
	LocalUserId.Reset();
	MatchId.Reset();
	ChannelName.Reset();
	PendingJoinCredential.Reset();
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
	if (RejoinDelaySeconds > 0.0f)
	{
		RejoinDelaySeconds = FMath::Max(0.0f, RejoinDelaySeconds - DeltaTime);
		if (RejoinDelaySeconds <= 0.0f && bConfiguredForSurvivor && !bJoinedChannel)
		{
			RequestJoinCredential();
		}
	}
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

void UProjectProject01VoiceChatSubsystem::ApplyRuntimeEosConfiguration()
{
	FString ConfiguredSecret;
	GConfig->GetString(TEXT("EOSVoiceChat"), TEXT("ClientSecret"), ConfiguredSecret, GEngineIni);
	if (!ConfiguredSecret.IsEmpty()) return;
	const FString ClientSecret = FPlatformMisc::GetEnvironmentVariable(*GameClientSecretEnvironmentVariable);
	if (ClientSecret.IsEmpty())
	{
		UE_LOG(LogProjectProject01Voice, Warning,
			TEXT("EOS game-client secret is not configured. Set %s outside Git before testing voice."),
			*GameClientSecretEnvironmentVariable);
		return;
	}
	GConfig->SetString(TEXT("EOSVoiceChat"), TEXT("ClientSecret"), *ClientSecret, GEngineIni);
}

void UProjectProject01VoiceChatSubsystem::BeginProductUserLogin()
{
	if (!bConfiguredForSurvivor || bProductUserLoginPending) return;
	EOS_HConnect ConnectHandle = ProjectProject01Voice::GetConnectHandle(VoiceChat);
	if (ConnectHandle == nullptr)
	{
		UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS Connect interface is unavailable."));
		return;
	}
	bProductUserLoginPending = true;
	auto* Context = new ProjectProject01Voice::FCallbackContext{this};
	FTCHARToUTF8 DeviceModel(TEXT("ProjectProject01 Windows PC"));
	EOS_Connect_CreateDeviceIdOptions Options{};
	Options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
	Options.DeviceModel = DeviceModel.Get();
	EOS_Connect_CreateDeviceId(ConnectHandle, &Options, Context,
		[](const EOS_Connect_CreateDeviceIdCallbackInfo* Data)
		{
			TUniquePtr<ProjectProject01Voice::FCallbackContext> CallbackContext(
				static_cast<ProjectProject01Voice::FCallbackContext*>(Data->ClientData));
			UProjectProject01VoiceChatSubsystem* Owner = CallbackContext->Owner.Get();
			if (!IsValid(Owner) || !Owner->bConfiguredForSurvivor) return;
			if (Data->ResultCode != EOS_EResult::EOS_Success && Data->ResultCode != EOS_EResult::EOS_DuplicateNotAllowed)
			{
				Owner->bProductUserLoginPending = false;
				UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS device identity creation failed: %s"),
					UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
				return;
			}
			Owner->LoginWithDeviceId();
		});
}

void UProjectProject01VoiceChatSubsystem::LoginWithDeviceId()
{
	EOS_HConnect ConnectHandle = ProjectProject01Voice::GetConnectHandle(VoiceChat);
	if (ConnectHandle == nullptr)
	{
		bProductUserLoginPending = false;
		return;
	}
	FTCHARToUTF8 DisplayName(TEXT("ProjectProject01Player"));
	EOS_Connect_Credentials Credentials{};
	Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
	Credentials.Token = nullptr;
	Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
	EOS_Connect_UserLoginInfo UserInfo{};
	UserInfo.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
	UserInfo.DisplayName = DisplayName.Get();
	EOS_Connect_LoginOptions Options{};
	Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
	Options.Credentials = &Credentials;
	Options.UserLoginInfo = &UserInfo;
	auto* Context = new ProjectProject01Voice::FCallbackContext{this};
	EOS_Connect_Login(ConnectHandle, &Options, Context,
		[](const EOS_Connect_LoginCallbackInfo* Data)
		{
			TUniquePtr<ProjectProject01Voice::FCallbackContext> CallbackContext(
				static_cast<ProjectProject01Voice::FCallbackContext*>(Data->ClientData));
			UProjectProject01VoiceChatSubsystem* Owner = CallbackContext->Owner.Get();
			if (!IsValid(Owner) || !Owner->bConfiguredForSurvivor) return;
			if (Data->ResultCode == EOS_EResult::EOS_Success)
			{
				Owner->CompleteProductUserLogin(Data->LocalUserId);
			}
			else if (Data->ResultCode == EOS_EResult::EOS_InvalidUser && Data->ContinuanceToken != nullptr)
			{
				Owner->CreateProductUser(Data->ContinuanceToken);
			}
			else
			{
				Owner->bProductUserLoginPending = false;
				UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS device login failed: %s"),
					UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
			}
		});
}

void UProjectProject01VoiceChatSubsystem::CreateProductUser(void* ContinuanceToken)
{
	EOS_HConnect ConnectHandle = ProjectProject01Voice::GetConnectHandle(VoiceChat);
	if (ConnectHandle == nullptr)
	{
		bProductUserLoginPending = false;
		return;
	}
	EOS_Connect_CreateUserOptions Options{};
	Options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
	Options.ContinuanceToken = static_cast<EOS_ContinuanceToken>(ContinuanceToken);
	auto* Context = new ProjectProject01Voice::FCallbackContext{this};
	EOS_Connect_CreateUser(ConnectHandle, &Options, Context,
		[](const EOS_Connect_CreateUserCallbackInfo* Data)
		{
			TUniquePtr<ProjectProject01Voice::FCallbackContext> CallbackContext(
				static_cast<ProjectProject01Voice::FCallbackContext*>(Data->ClientData));
			UProjectProject01VoiceChatSubsystem* Owner = CallbackContext->Owner.Get();
			if (!IsValid(Owner) || !Owner->bConfiguredForSurvivor) return;
			if (Data->ResultCode == EOS_EResult::EOS_Success)
			{
				Owner->CompleteProductUserLogin(Data->LocalUserId);
			}
			else
			{
				Owner->bProductUserLoginPending = false;
				UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS product user creation failed: %s"),
					UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
			}
		});
}

void UProjectProject01VoiceChatSubsystem::CompleteProductUserLogin(void* ProductUserId)
{
	bProductUserLoginPending = false;
	char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1]{};
	int32 BufferLength = UE_ARRAY_COUNT(Buffer);
	const EOS_EResult Result = EOS_ProductUserId_ToString(static_cast<EOS_ProductUserId>(ProductUserId), Buffer, &BufferLength);
	if (Result != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS Product User ID serialization failed: %s"),
			UTF8_TO_TCHAR(EOS_EResult_ToString(Result)));
		return;
	}
	LocalUserId = UTF8_TO_TCHAR(Buffer);
	BeginConnect();
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
	if (!ChannelExitedDelegateHandle.IsValid())
	{
		ChannelExitedDelegateHandle = VoiceUser->OnVoiceChatChannelExited().AddUObject(
			this, &UProjectProject01VoiceChatSubsystem::HandleChannelExited);
	}
	if (VoiceUser->IsLoggedIn())
	{
		BeginJoin();
		return;
	}

	VoiceUser->Login(FPlatformUserId::CreateFromInternalId(0), LocalUserId, TEXT("EOSConnect"),
		FOnVoiceChatLoginCompleteDelegate::CreateUObject(this, &UProjectProject01VoiceChatSubsystem::HandleLoggedIn));
}

void UProjectProject01VoiceChatSubsystem::BeginJoin()
{
	if (!bConfiguredForSurvivor || VoiceUser == nullptr || ChannelName.IsEmpty()) return;
	RequestJoinCredential();
}

void UProjectProject01VoiceChatSubsystem::RequestJoinCredential()
{
	if (!bConfiguredForSurvivor || VoiceUser == nullptr || MatchId.IsEmpty() || LocalUserId.IsEmpty() ||
		bJoinCredentialPending || bJoinedChannel) return;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	if (!IsValid(Auth) || !Auth->IsSignedIn() || Auth->GetAccessTokenForAuthenticatedRequest().IsEmpty())
	{
		UE_LOG(LogProjectProject01Voice, Warning, TEXT("Voice token request requires a signed-in project account."));
		return;
	}
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("matchId"), MatchId);
	Body->SetStringField(TEXT("productUserId"), LocalUserId);
	FString SerializedBody;
	FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&SerializedBody));
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Auth->GetApiBaseUrlForAuthenticatedRequest() + TEXT("/api/voice/token"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Auth->GetAccessTokenForAuthenticatedRequest());
	Request->SetContentAsString(SerializedBody);
	Request->OnProcessRequestComplete().BindUObject(this, &UProjectProject01VoiceChatSubsystem::HandleJoinCredentialResponse);
	bJoinCredentialPending = true;
	if (!Request->ProcessRequest())
	{
		bJoinCredentialPending = false;
		RejoinDelaySeconds = 2.0f;
	}
}

void UProjectProject01VoiceChatSubsystem::HandleJoinCredentialResponse(
	FHttpRequestPtr Request, FHttpResponsePtr Response, const bool bSucceeded)
{
	bJoinCredentialPending = false;
	if (!bConfiguredForSurvivor) return;
	if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS voice room token request failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
		RejoinDelaySeconds = 3.0f;
		return;
	}
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json) || !Json.IsValid())
	{
		RejoinDelaySeconds = 3.0f;
		return;
	}
	const FString ReturnedChannel = Json->GetStringField(TEXT("channelName"));
	const FString ClientBaseUrl = Json->GetStringField(TEXT("clientBaseUrl"));
	const FString ParticipantToken = Json->GetStringField(TEXT("participantToken"));
	if (!ReturnedChannel.Equals(ChannelName, ESearchCase::IgnoreCase) || ClientBaseUrl.IsEmpty() || ParticipantToken.IsEmpty())
	{
		UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS voice token response was invalid or for another channel."));
		return;
	}
	ChannelName = ReturnedChannel;
	TSharedRef<FJsonObject> Credential = MakeShared<FJsonObject>();
	Credential->SetStringField(TEXT("override_userid"), LocalUserId);
	Credential->SetStringField(TEXT("client_base_url"), ClientBaseUrl);
	Credential->SetStringField(TEXT("participant_token"), ParticipantToken);
	PendingJoinCredential.Reset();
	FJsonSerializer::Serialize(Credential, TJsonWriterFactory<>::Create(&PendingJoinCredential));
	FVoiceChatChannel3dProperties Properties;
	Properties.AttenuationModel = EVoiceChatAttenuationModel::InverseByDistance;
	Properties.MinDistance = FMath::Max(0.0f, FullVolumeDistance);
	Properties.MaxDistance = FMath::Max(Properties.MinDistance + 1.0f, MaximumAudibleDistance);
	Properties.Rolloff = FMath::Max(0.01f, DistanceRolloff);
	VoiceUser->JoinChannel(ChannelName, PendingJoinCredential, EVoiceChatChannelType::Positional,
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
	BeginProductUserLogin();
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

void UProjectProject01VoiceChatSubsystem::HandleChannelExited(
	const FString& ExitedChannel, const FVoiceChatResult& Reason)
{
	if (ExitedChannel != ChannelName) return;
	bJoinedChannel = false;
	if (bConfiguredForSurvivor)
	{
		UE_LOG(LogProjectProject01Voice, Warning,
			TEXT("EOS voice channel exited (%s). A fresh short-lived token will be requested."), *LexToString(Reason));
		RejoinDelaySeconds = 2.0f;
	}
}

void UProjectProject01VoiceChatSubsystem::ReportFailure(const TCHAR* Stage, const FVoiceChatResult& Result)
{
	UE_LOG(LogProjectProject01Voice, Warning, TEXT("Voice %s failed: %s"), Stage, *LexToString(Result));
}

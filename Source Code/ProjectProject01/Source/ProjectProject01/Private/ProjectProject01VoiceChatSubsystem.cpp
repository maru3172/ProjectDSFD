#include "ProjectProject01VoiceChatSubsystem.h"
#include "ProjectProject01VersionContract.h"

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
#include "AudioCaptureCore.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectProject01Voice, Log, All);

namespace ProjectProject01Voice
{
	struct FCallbackContext
	{
		TWeakObjectPtr<UProjectProject01VoiceChatSubsystem> Owner;
		uint64 Generation;
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
	GConfig->GetString(TEXT("ProjectProject01.VoicePreferences"), TEXT("Input"), PreferredInput, GGameUserSettingsIni);
	GConfig->GetString(TEXT("ProjectProject01.VoicePreferences"), TEXT("Output"), PreferredOutput, GGameUserSettingsIni);
	GConfig->GetFloat(TEXT("ProjectProject01.VoicePreferences"), TEXT("Volume"), ReceiveVolume, GGameUserSettingsIni);
	ReceiveVolume = FMath::IsFinite(ReceiveVolume) ? FMath::Clamp(ReceiveVolume,0.0f,1.0f) : 1.0f;
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
		if (ParticipantAddedDelegateHandle.IsValid())
		{
			VoiceUser->OnVoiceChatPlayerAdded().Remove(ParticipantAddedDelegateHandle);
			ParticipantAddedDelegateHandle.Reset();
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
	const bool bAllowed=bEnableSurvivorProximityVoice && Role.Equals(TEXT("Survivor"),ESearchCase::IgnoreCase) &&
		!UserId.IsEmpty() && !InMatchId.IsEmpty() && IsValid(LocalController);
	const bool bSameMatch=bAllowed && AccountUserId==UserId && MatchId==InMatchId;
	StopSession(bSameMatch);
	if (!bAllowed)
	{
		return;
	}

	bConfiguredForSurvivor = true;
	AccountUserId=UserId;
	OwnerController = LocalController;
	LocalUserId.Reset();
	MatchId = InMatchId;
	ChannelName = FString::Printf(TEXT("pp01-survivors-%s"), *InMatchId.Replace(TEXT("-"), TEXT("")));
	ConnectionRetryCount=0;
	BeginConnectionAttempt();
}

void UProjectProject01VoiceChatSubsystem::LeaveMatch()
{
	StopSession(false);
}

void UProjectProject01VoiceChatSubsystem::SuspendForLevelTravel()
{
	if (bConfiguredForSurvivor) StopSession(true);
}

void UProjectProject01VoiceChatSubsystem::CancelVoiceRequests()
{
	for(FHttpRequestPtr* Request:{&JoinCredentialRequest,&RosterRequest})
		if(Request->IsValid())
		{
			(*Request)->OnProcessRequestComplete().Unbind();(*Request)->CancelRequest();Request->Reset();
		}
	bJoinCredentialPending=false;
}

void UProjectProject01VoiceChatSubsystem::StopSession(bool bPreservePreferences)
{
	AppliedTransmitState=-1;AppliedPeerGains.Reset();SmoothedPeerDistanceGains.Reset();
	LastAudibilityUpdateSeconds=0;bAuthoritativeVoiceAllowed=false;AuthoritativePeerGains.Reset();
	++SessionGeneration; // Invalidate before cancellation or provider callbacks can run.
	bConfiguredForSurvivor = false;
	bPushToTalkHeld = false;
	bOpenMic = false;
	CancelVoiceRequests();
	StopMicrophoneTest();
	if (VoiceUser != nullptr)
	{
		VoiceUser->TransmitToNoChannels();
		if (bJoinedChannel && !ChannelName.IsEmpty())
		{
			VoiceUser->LeaveChannel(ChannelName, FOnVoiceChatChannelLeaveCompleteDelegate());
		}
	}
	if(!bPreservePreferences)
	{
		ResetParticipantPreferences();ParticipantNames.Reset();ParticipantAccountIds.Reset();AccountUserId.Reset();MatchId.Reset();
	}
	bJoinedChannel = false;
	bProductUserLoginPending = false;
	bJoinCredentialPending = false;
	PositionUpdateAccumulatorSeconds = 0.0f;
	RejoinDelaySeconds = 0.0f;
	OwnerController.Reset();
	LocalUserId.Reset();
	ChannelName.Reset();
	PendingJoinCredential.Reset();
	ConnectionFailure.Reset();ConnectionRetryCount=0;RosterRefreshSeconds=0;
	SetConnectionPhase(bPreservePreferences?EProjectVoiceConnectionPhase::Suspended:EProjectVoiceConnectionPhase::Inactive);
}

void UProjectProject01VoiceChatSubsystem::SetConnectionPhase(EProjectVoiceConnectionPhase Phase)
{
	ConnectionPhase=Phase;ConnectionStageSeconds=0;
}

void UProjectProject01VoiceChatSubsystem::BeginConnectionAttempt()
{
	if(!bConfiguredForSurvivor)return;
	AppliedTransmitState=-1;AppliedPeerGains.Reset();
	++SessionGeneration;CancelVoiceRequests();bProductUserLoginPending=false;
	bJoinedChannel=false;ConnectionFailure.Reset();
	SetConnectionPhase(EProjectVoiceConnectionPhase::Initializing);
	if(!VoiceChat){ApplyRuntimeEosConfiguration();VoiceChat=IVoiceChat::Get();}
	if(!VoiceChat){FailConnection(TEXT("음성 기능을 사용할 수 없습니다. EOS 설정을 확인하세요."),false);return;}
	if(VoiceChat->IsInitialized())BeginProductUserLogin();
	else PrepareAudioDevices(); // One shared SDK initialization; no account-specific work in that callback.
}

void UProjectProject01VoiceChatSubsystem::RetryVoiceConnection()
{
	if(!CanRetryVoiceConnection())return;
	ConnectionRetryCount=0;BeginConnectionAttempt();
}

void UProjectProject01VoiceChatSubsystem::FailConnection(const FString& Message,bool bRetryable)
{
	if(!bConfiguredForSurvivor)return;
	++SessionGeneration;CancelVoiceRequests();bProductUserLoginPending=false;bJoinedChannel=false;
	if(VoiceUser)VoiceUser->TransmitToNoChannels();
	PendingJoinCredential.Reset();ConnectionFailure=Message;
	if(bRetryable && ConnectionRetryCount<3)
	{
		RejoinDelaySeconds=2.0f*FMath::Pow(2.0f,ConnectionRetryCount++);
		SetConnectionPhase(EProjectVoiceConnectionPhase::RetryWaiting);
	}
	else {RejoinDelaySeconds=0;SetConnectionPhase(EProjectVoiceConnectionPhase::Failed);}
}

FString UProjectProject01VoiceChatSubsystem::GetConnectionStatusText() const
{
	if(ConnectionPhase==EProjectVoiceConnectionPhase::Failed)return TEXT("연결 실패: ")+ConnectionFailure;
	if(ConnectionPhase==EProjectVoiceConnectionPhase::RetryWaiting)
		return FString::Printf(TEXT("재시도 대기 (%d/3): %s"),ConnectionRetryCount,*ConnectionFailure);
	switch(ConnectionPhase)
	{
	case EProjectVoiceConnectionPhase::Initializing:return TEXT("음성 장치 초기화 중");
	case EProjectVoiceConnectionPhase::Identity:return TEXT("EOS 사용자 인증 중");
	case EProjectVoiceConnectionPhase::Connecting:return TEXT("음성 서비스 연결 중");
	case EProjectVoiceConnectionPhase::Login:return TEXT("음성 사용자 로그인 중");
	case EProjectVoiceConnectionPhase::Token:return TEXT("음성 참가 허가 요청 중");
	case EProjectVoiceConnectionPhase::Joining:return TEXT("생존자 음성 채널 입장 중");
	case EProjectVoiceConnectionPhase::Ready:return TEXT("연결됨");
	case EProjectVoiceConnectionPhase::Suspended:return TEXT("맵 이동 중");
	default:return TEXT("경기 중 생존자 전용");
	}
}

void UProjectProject01VoiceChatSubsystem::SetPushToTalkHeld(const bool bHeld)
{
	if (!bConfiguredForSurvivor)
	{
		bPushToTalkHeld = false;
		ApplyTransmitMode();
		return;
	}
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
	if(!bConfiguredForSurvivor)return;
	UpdateReceiveVolume();
	ApplyTransmitMode();
	if(ConnectionPhase!=EProjectVoiceConnectionPhase::Ready && ConnectionPhase!=EProjectVoiceConnectionPhase::Failed &&
		ConnectionPhase!=EProjectVoiceConnectionPhase::RetryWaiting)
	{
		ConnectionStageSeconds+=FMath::Max(0.0f,DeltaTime);
		if(ConnectionStageSeconds>=10){FailConnection(TEXT("음성 연결 단계의 응답 시간이 초과되었습니다."));return;}
	}
	if (RejoinDelaySeconds > 0.0f)
	{
		RejoinDelaySeconds = FMath::Max(0.0f, RejoinDelaySeconds - DeltaTime);
		if (RejoinDelaySeconds <= 0.0f && bConfiguredForSurvivor && !bJoinedChannel)
		{
			BeginConnectionAttempt();
		}
	}
	if (!bJoinedChannel || VoiceUser == nullptr || ChannelName.IsEmpty()) return;
	RosterRefreshSeconds+=DeltaTime;
	if(RosterRefreshSeconds>=15){RosterRefreshSeconds=0;RefreshParticipantNames();}
	// The server still sends distance decisions at 10Hz. Blend them every client
	// frame instead of applying another 100ms stair-step in the EOS receive gain.
	const TArray<FString> Participants=GetVoiceParticipants();
	for(auto It=SmoothedPeerDistanceGains.CreateIterator();It;++It)
		if(!Participants.Contains(It.Key())) { AppliedPeerGains.Remove(It.Key());It.RemoveCurrent(); }
	for(const FString& Id:Participants)ApplyParticipantReceiveSettings(Id,DeltaTime);
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
	SetConnectionPhase(EProjectVoiceConnectionPhase::Identity);
	EOS_HConnect ConnectHandle = ProjectProject01Voice::GetConnectHandle(VoiceChat);
	if (ConnectHandle == nullptr)
	{
		FailConnection(TEXT("EOS 사용자 인증 기능을 사용할 수 없습니다."),false);
		return;
	}
	bProductUserLoginPending = true;
	auto* Context = new ProjectProject01Voice::FCallbackContext{this,SessionGeneration};
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
			if (!IsValid(Owner) || !Owner->IsCurrentSession(CallbackContext->Generation)) return;
			if (Data->ResultCode != EOS_EResult::EOS_Success && Data->ResultCode != EOS_EResult::EOS_DuplicateNotAllowed)
			{
				Owner->bProductUserLoginPending = false;
				UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS device identity creation failed: %s"),
					UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
				Owner->FailConnection(TEXT("EOS 장치 인증에 실패했습니다."));
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
		FailConnection(TEXT("EOS 사용자 인증 기능을 사용할 수 없습니다."),false);
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
	auto* Context = new ProjectProject01Voice::FCallbackContext{this,SessionGeneration};
	EOS_Connect_Login(ConnectHandle, &Options, Context,
		[](const EOS_Connect_LoginCallbackInfo* Data)
		{
			TUniquePtr<ProjectProject01Voice::FCallbackContext> CallbackContext(
				static_cast<ProjectProject01Voice::FCallbackContext*>(Data->ClientData));
			UProjectProject01VoiceChatSubsystem* Owner = CallbackContext->Owner.Get();
			if (!IsValid(Owner) || !Owner->IsCurrentSession(CallbackContext->Generation)) return;
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
				Owner->FailConnection(TEXT("EOS 사용자 인증에 실패했습니다."));
			}
		});
}

void UProjectProject01VoiceChatSubsystem::CreateProductUser(void* ContinuanceToken)
{
	EOS_HConnect ConnectHandle = ProjectProject01Voice::GetConnectHandle(VoiceChat);
	if (ConnectHandle == nullptr)
	{
		bProductUserLoginPending = false;
		FailConnection(TEXT("EOS 사용자 생성 기능을 사용할 수 없습니다."),false);
		return;
	}
	EOS_Connect_CreateUserOptions Options{};
	Options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
	Options.ContinuanceToken = static_cast<EOS_ContinuanceToken>(ContinuanceToken);
	auto* Context = new ProjectProject01Voice::FCallbackContext{this,SessionGeneration};
	EOS_Connect_CreateUser(ConnectHandle, &Options, Context,
		[](const EOS_Connect_CreateUserCallbackInfo* Data)
		{
			TUniquePtr<ProjectProject01Voice::FCallbackContext> CallbackContext(
				static_cast<ProjectProject01Voice::FCallbackContext*>(Data->ClientData));
			UProjectProject01VoiceChatSubsystem* Owner = CallbackContext->Owner.Get();
			if (!IsValid(Owner) || !Owner->IsCurrentSession(CallbackContext->Generation)) return;
			if (Data->ResultCode == EOS_EResult::EOS_Success)
			{
				Owner->CompleteProductUserLogin(Data->LocalUserId);
			}
			else
			{
				Owner->bProductUserLoginPending = false;
				UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS product user creation failed: %s"),
					UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
				Owner->FailConnection(TEXT("EOS 사용자 생성에 실패했습니다."));
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
		FailConnection(TEXT("EOS 사용자 식별자를 확인하지 못했습니다."),false);
		return;
	}
	LocalUserId = UTF8_TO_TCHAR(Buffer);
	BeginConnect();
}

void UProjectProject01VoiceChatSubsystem::BeginConnect()
{
	if (!bConfiguredForSurvivor || VoiceChat == nullptr) return;
	SetConnectionPhase(EProjectVoiceConnectionPhase::Connecting);
	if (VoiceChat->IsConnected())
	{
		BeginLogin();
		return;
	}
	VoiceChat->Connect(FOnVoiceChatConnectCompleteDelegate::CreateUObject(
		this, &UProjectProject01VoiceChatSubsystem::HandleConnected,SessionGeneration));
}

void UProjectProject01VoiceChatSubsystem::BeginLogin()
{
	if (!bConfiguredForSurvivor || VoiceChat == nullptr) return;
	SetConnectionPhase(EProjectVoiceConnectionPhase::Login);
	if (VoiceUser == nullptr) VoiceUser = VoiceChat->CreateUser();
	ApplyAudioPreferences();
	if (VoiceUser == nullptr)
	{
		UE_LOG(LogProjectProject01Voice, Error, TEXT("VoiceChat provider could not create a local user."));
		FailConnection(TEXT("로컬 음성 사용자를 만들지 못했습니다."));
		return;
	}
	if (!ChannelExitedDelegateHandle.IsValid())
	{
		ChannelExitedDelegateHandle = VoiceUser->OnVoiceChatChannelExited().AddUObject(
			this, &UProjectProject01VoiceChatSubsystem::HandleChannelExited);
	}
	if (!ParticipantAddedDelegateHandle.IsValid())
	{
		ParticipantAddedDelegateHandle=VoiceUser->OnVoiceChatPlayerAdded().AddUObject(
			this,&UProjectProject01VoiceChatSubsystem::HandleParticipantAdded);
	}
	if (VoiceUser->IsLoggedIn())
	{
		BeginJoin();
		return;
	}

	VoiceUser->Login(FPlatformUserId::CreateFromInternalId(0), LocalUserId, TEXT("EOSConnect"),
		FOnVoiceChatLoginCompleteDelegate::CreateUObject(this, &UProjectProject01VoiceChatSubsystem::HandleLoggedIn,SessionGeneration));
}

void UProjectProject01VoiceChatSubsystem::BeginJoin()
{
	if (!bConfiguredForSurvivor || VoiceUser == nullptr || ChannelName.IsEmpty()) return;
	RequestJoinCredential();
}

void UProjectProject01VoiceChatSubsystem::RequestJoinCredential(bool bIdentityRefreshOnly)
{
	if (!bConfiguredForSurvivor || VoiceUser == nullptr || MatchId.IsEmpty() || LocalUserId.IsEmpty() ||
		bJoinCredentialPending || (bJoinedChannel && !bIdentityRefreshOnly)) return;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	if (!IsValid(Auth) || !Auth->IsSignedIn() || Auth->GetAccessTokenForAuthenticatedRequest().IsEmpty())
	{
		if(!bIdentityRefreshOnly)FailConnection(TEXT("음성 연결에는 로그인된 게임 계정이 필요합니다."),false);
		return;
	}
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("matchId"), MatchId);
	Body->SetStringField(TEXT("productUserId"), LocalUserId);
	EOS_Connect_CopyIdTokenOptions ProofOptions={};ProofOptions.ApiVersion=EOS_CONNECT_COPYIDTOKEN_API_LATEST;
	ProofOptions.LocalUserId=EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*LocalUserId));
	EOS_Connect_IdToken* Proof=nullptr;
	const EOS_HConnect Connect=ProjectProject01Voice::GetConnectHandle(VoiceChat);
	if(!Connect || EOS_Connect_CopyIdToken(Connect,&ProofOptions,&Proof)!=EOS_EResult::EOS_Success || !Proof || !Proof->JsonWebToken)
	{
		if(Proof)EOS_Connect_IdToken_Release(Proof);
		if(!bIdentityRefreshOnly)FailConnection(TEXT("EOS 음성 사용자 증명을 가져오지 못했습니다. 음성 연결을 다시 시도하세요."),false);
		return;
	}
	Body->SetStringField(TEXT("productUserIdToken"),UTF8_TO_TCHAR(Proof->JsonWebToken));
	EOS_Connect_IdToken_Release(Proof);
	FString SerializedBody;
	FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&SerializedBody));
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Auth->GetApiBaseUrlForAuthenticatedRequest() + TEXT("/api/voice/token"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Auth->GetAccessTokenForAuthenticatedRequest());
	FProjectProject01VersionContract::ApplyToRequest(Request);
	Request->SetContentAsString(SerializedBody);
	Request->SetTimeout(5.0f);
	Request->OnProcessRequestComplete().BindUObject(this, &UProjectProject01VoiceChatSubsystem::HandleJoinCredentialResponse,SessionGeneration,bIdentityRefreshOnly);
	JoinCredentialRequest=Request;
	bJoinCredentialPending = true;
	if(!bIdentityRefreshOnly)SetConnectionPhase(EProjectVoiceConnectionPhase::Token);
	if (!Request->ProcessRequest())
	{
		bJoinCredentialPending = false;
		JoinCredentialRequest.Reset();
		if(!bIdentityRefreshOnly)FailConnection(TEXT("음성 참가 허가 요청을 시작하지 못했습니다."));
	}
}

void UProjectProject01VoiceChatSubsystem::HandleJoinCredentialResponse(
	FHttpRequestPtr Request, FHttpResponsePtr Response, const bool bSucceeded,uint64 Generation,bool bIdentityRefreshOnly)
{
	if(!IsCurrentSession(Generation) || Request!=JoinCredentialRequest)return;
	JoinCredentialRequest.Reset();
	bJoinCredentialPending = false;
	if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS voice room token request failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
		if(!bIdentityRefreshOnly)
		{
			const int32 Code=Response.IsValid()?Response->GetResponseCode():0;
			FailConnection(Code==401||Code==403?TEXT("음성 참가 자격이 없거나 로그인이 만료되었습니다."):TEXT("음성 참가 허가 서버에 연결하지 못했습니다."),Code!=401&&Code!=403);
		}
		return;
	}
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json) || !Json.IsValid())
	{
		if(!bIdentityRefreshOnly)FailConnection(TEXT("음성 참가 허가 응답을 읽지 못했습니다."));
		return;
	}
	FString ReturnedChannel,ClientBaseUrl,ParticipantToken,ReturnedUser;
	Json->TryGetStringField(TEXT("channelName"),ReturnedChannel);
	Json->TryGetStringField(TEXT("clientBaseUrl"),ClientBaseUrl);
	Json->TryGetStringField(TEXT("participantToken"),ParticipantToken);
	Json->TryGetStringField(TEXT("productUserId"),ReturnedUser);
	if (ReturnedChannel!=ChannelName || ReturnedUser!=LocalUserId || ClientBaseUrl.IsEmpty() || ParticipantToken.IsEmpty())
	{
		UE_LOG(LogProjectProject01Voice, Warning, TEXT("EOS voice token response was invalid or for another channel."));
		if(!bIdentityRefreshOnly)FailConnection(TEXT("음성 참가 허가가 현재 사용자·경기와 일치하지 않습니다."),false);
		return;
	}
	ChannelName = ReturnedChannel;
	const TSharedPtr<FJsonObject>* Roster=nullptr;
	if(Json->TryGetObjectField(TEXT("roster"),Roster))ApplyParticipantNames(*Roster);
	if(bIdentityRefreshOnly)return; // Refresh backend identity after restart without interrupting an active call.
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
	SetConnectionPhase(EProjectVoiceConnectionPhase::Joining);
	VoiceUser->JoinChannel(ChannelName, PendingJoinCredential, EVoiceChatChannelType::Positional,
		FOnVoiceChatChannelJoinCompleteDelegate::CreateUObject(this, &UProjectProject01VoiceChatSubsystem::HandleJoined,SessionGeneration),
		Properties);
}

void UProjectProject01VoiceChatSubsystem::ApplyTransmitMode()
{
	if (VoiceUser == nullptr || !bJoinedChannel) return;
	const bool bTransmit=HasFreshAudibility() && bAuthoritativeVoiceAllowed && !IsMicrophoneTestActive() && (bOpenMic || bPushToTalkHeld);
	if(AppliedTransmitState==static_cast<int8>(bTransmit))return;
	AppliedTransmitState=static_cast<int8>(bTransmit);
	if (bTransmit)
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
	bDeviceInitializationPending = false;
	if (!Result.IsSuccess())
	{
		if(bConfiguredForSurvivor && ConnectionPhase==EProjectVoiceConnectionPhase::Initializing)ReportFailure(TEXT("Initialize"),Result);
		return;
	}
	if (bConfiguredForSurvivor && ConnectionPhase==EProjectVoiceConnectionPhase::Initializing) BeginProductUserLogin();
	else if(!bConfiguredForSurvivor)PrepareAudioDevices();
}

void UProjectProject01VoiceChatSubsystem::HandleConnected(const FVoiceChatResult& Result,uint64 Generation)
{
	if(!IsCurrentSession(Generation))return;
	if (!Result.IsSuccess()) { ReportFailure(TEXT("Connect"), Result); return; }
	BeginLogin();
}

void UProjectProject01VoiceChatSubsystem::HandleLoggedIn(const FString& PlayerName, const FVoiceChatResult& Result,uint64 Generation)
{
	if(!IsCurrentSession(Generation))return;
	if (!Result.IsSuccess()) { ReportFailure(TEXT("Login"), Result); return; }
	BeginJoin();
}

void UProjectProject01VoiceChatSubsystem::HandleJoined(const FString& JoinedChannel, const FVoiceChatResult& Result,uint64 Generation)
{
	if(!IsCurrentSession(Generation))
	{
		if(Result.IsSuccess() && VoiceUser && (!bConfiguredForSurvivor || JoinedChannel!=ChannelName))
			VoiceUser->LeaveChannel(JoinedChannel,FOnVoiceChatChannelLeaveCompleteDelegate());
		return;
	}
	if(JoinedChannel!=ChannelName)return;
	if (!Result.IsSuccess()) { ReportFailure(TEXT("JoinChannel"), Result); return; }
	bJoinedChannel = true;PendingJoinCredential.Reset();ConnectionFailure.Reset();ConnectionRetryCount=0;
	SetConnectionPhase(EProjectVoiceConnectionPhase::Ready);
	ApplyAudioPreferences();
	ApplyTransmitMode();
	RefreshParticipantNames();
	UE_LOG(LogProjectProject01Voice, Log, TEXT("Joined survivor-only positional voice channel %s."), *JoinedChannel);
}

void UProjectProject01VoiceChatSubsystem::HandleChannelExited(
	const FString& ExitedChannel, const FVoiceChatResult& Reason)
{
	if (ExitedChannel != ChannelName || !bConfiguredForSurvivor || !bJoinedChannel) return;
	bJoinedChannel = false;
	if (bConfiguredForSurvivor)
	{
		UE_LOG(LogProjectProject01Voice, Warning,
			TEXT("EOS voice channel exited (%s). A fresh short-lived token will be requested."), *LexToString(Reason));
		FailConnection(TEXT("음성 연결이 끊어졌습니다."));
	}
}

void UProjectProject01VoiceChatSubsystem::ReportFailure(const TCHAR* Stage, const FVoiceChatResult& Result)
{
	UE_LOG(LogProjectProject01Voice, Warning, TEXT("Voice %s failed: %s"), Stage, *LexToString(Result));
	FailConnection(FString::Printf(TEXT("음성 %s 단계에 실패했습니다."),Stage));
}

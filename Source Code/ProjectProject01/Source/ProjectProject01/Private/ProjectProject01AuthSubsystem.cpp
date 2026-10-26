// File: Source/ProjectProject01/Private/ProjectProject01AuthSubsystem.cpp
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "ProjectHttpDiagnostics.h"
#include "ProjectRequestProtection.h"

#include "Dom/JsonObject.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectProject01Auth, Log, All);

namespace ProjectProject01Auth
{
	constexpr int32 MinAccountLength = 3;
	constexpr int32 MaxAccountLength = 32;
	constexpr int32 MinPasswordLength = 10;
	constexpr int32 MaxPasswordLength = 128;

	bool IsRetryableStatus(const int32 StatusCode)
	{
		return StatusCode == 408 || StatusCode == 429 || StatusCode >= 500;
	}

	FString NormalizeBaseUrl(FString Url)
	{
		Url.TrimStartAndEndInline();
		while (Url.EndsWith(TEXT("/")))
		{
			Url.LeftChopInline(1);
		}
		return Url;
	}
}

void UProjectProject01AuthSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ApiBaseUrl = ProjectProject01Auth::NormalizeBaseUrl(ApiBaseUrl);
	AllowedInsecureVpnApiBaseUrl = ProjectProject01Auth::NormalizeBaseUrl(AllowedInsecureVpnApiBaseUrl);
	RequestTimeoutSeconds = FMath::Clamp(RequestTimeoutSeconds, 2.0f, 60.0f);
	MaxRetryCount = FMath::Clamp(MaxRetryCount, 0, 2);

	if (!IsApiBaseUrlAllowed())
	{
		UE_LOG(LogProjectProject01Auth, Error,
			TEXT("Authentication API must use HTTPS except for loopback or the explicitly configured VPN test endpoint: %s"),
			*ApiBaseUrl);
	}
	else if (ApiBaseUrl.Equals(AllowedInsecureVpnApiBaseUrl, ESearchCase::IgnoreCase) &&
		ApiBaseUrl.StartsWith(TEXT("http://")))
	{
		UE_LOG(LogProjectProject01Auth, Warning,
			TEXT("Authentication API is using the explicitly configured Hamachi/VPN HTTP test endpoint. Do not use this mode for public release."));
	}
	SessionMonitorTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UProjectProject01AuthSubsystem::TickSessionMonitor), 5.0f);
	ExitLogoutHandle = FCoreDelegates::OnPreExit.AddUObject(this, &ThisClass::LogoutOnExit);
}

void UProjectProject01AuthSubsystem::Deinitialize()
{
	LogoutOnExit(); // PIE stop also deinitializes the game instance, without exiting the editor.
	FCoreDelegates::OnPreExit.Remove(ExitLogoutHandle);
	if (SessionMonitorTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SessionMonitorTickerHandle);
		SessionMonitorTickerHandle.Reset();
	}
	if (SessionValidationRequest.IsValid())
	{
		SessionValidationRequest->OnProcessRequestComplete().Unbind();
		SessionValidationRequest->CancelRequest();
		SessionValidationRequest.Reset();
	}
	if (ServiceStatusRequest.IsValid())
	{
		ServiceStatusRequest->OnProcessRequestComplete().Unbind();
		ServiceStatusRequest->CancelRequest();
		ServiceStatusRequest.Reset();
	}
	if (GuideProgressRequest.IsValid())
	{
		GuideProgressRequest->OnProcessRequestComplete().Unbind();
		GuideProgressRequest->CancelRequest();
		GuideProgressRequest.Reset();
	}
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	if (ActiveRefreshCompletion)
	{
		TFunction<void(bool)> Completion = MoveTemp(ActiveRefreshCompletion);
		ActiveRefreshCompletion = nullptr;
		Completion(false);
	}
	ClearSession();
	Super::Deinitialize();
}

void UProjectProject01AuthSubsystem::LogoutOnExit()
{
	if (bExitLogoutSent || AccessToken.IsEmpty() || !IsApiBaseUrlAllowed()) return;
	bExitLogoutSent = true;
	// Independent of UObject callbacks and other pending UI operations. The server lease is the
	// fallback for force-kill, power loss, or shutdown before HTTP delivery can finish.
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiBaseUrl + TEXT("/api/auth/logout"));
	Request->SetVerb(TEXT("POST")); Request->SetTimeout(5);
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	FProjectProject01VersionContract::ApplyToRequest(Request);
	Request->SetContentAsString(TEXT("{}"));
	Request->ProcessRequest();
}

void UProjectProject01AuthSubsystem::RegisterAccount(
	const FString& AccountId,
	const FString& Password,
	const FString& DisplayName)
{
	FString Error;
	if (!ValidateCommonInput(AccountId, Password, Error))
	{
		CompleteOperation(EAuthOperation::Register, false, Error, FString());
		return;
	}

	const FString CleanDisplayName = DisplayName.TrimStartAndEnd();
	if (CleanDisplayName.Len() < 2 || CleanDisplayName.Len() > 32)
	{
		CompleteOperation(EAuthOperation::Register, false, TEXT("표시 이름은 2~32자여야 합니다."), FString());
		return;
	}
	if (!CanStartOperation(EAuthOperation::Register))
	{
		return;
	}

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("accountId"), AccountId.TrimStartAndEnd());
	Json->SetStringField(TEXT("password"), Password);
	Json->SetStringField(TEXT("displayName"), CleanDisplayName);
	Json->SetBoolField(TEXT("loginAfterRegistration"), false);
	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Json, Writer);
	SendRequest(EAuthOperation::Register, TEXT("/api/auth/register"), Body, 0);
}

void UProjectProject01AuthSubsystem::Login(const FString& AccountId, const FString& Password)
{
	FString Error;
	if (!ValidateCommonInput(AccountId, Password, Error))
	{
		CompleteOperation(EAuthOperation::Login, false, Error, FString());
		return;
	}
	if (!CanStartOperation(EAuthOperation::Login))
	{
		return;
	}

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("accountId"), AccountId.TrimStartAndEnd());
	Json->SetStringField(TEXT("password"), Password);
	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Json, Writer);
	SendRequest(EAuthOperation::Login, TEXT("/api/auth/login"), Body, 0);
}

void UProjectProject01AuthSubsystem::Logout()
{
	if (!IsSignedIn())
	{
		ClearSession();
		CompleteOperation(EAuthOperation::Logout, true, TEXT("로그아웃되었습니다."), FString());
		return;
	}
	if (!CanStartOperation(EAuthOperation::Logout))
	{
		return;
	}
	SendRequest(EAuthOperation::Logout, TEXT("/api/auth/logout"), TEXT("{}"), 0);
}

void UProjectProject01AuthSubsystem::ForceClearLocalSession()
{
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	if (SessionValidationRequest.IsValid())
	{
		SessionValidationRequest->OnProcessRequestComplete().Unbind();
		SessionValidationRequest->CancelRequest();
		SessionValidationRequest.Reset();
	}
	ActiveRefreshCompletion = nullptr;
	ClearSession();
}

void UProjectProject01AuthSubsystem::InvalidateSessionFromServerFailure(const FString& Message)
{
	InvalidateSession(Message);
}

void UProjectProject01AuthSubsystem::CancelPendingAuthentication()
{
	if (AuthState != EProjectProject01AuthState::Registering &&
		AuthState != EProjectProject01AuthState::SigningIn)
	{
		return;
	}
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	ActiveRefreshCompletion = nullptr;
	ClearSession();
}

void UProjectProject01AuthSubsystem::CheckMultiplayerServiceStatus()
{
	if (ServiceStatusRequest.IsValid() || ApiBaseUrl.IsEmpty() || !IsApiBaseUrlAllowed())
	{
		return;
	}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiBaseUrl + TEXT("/api/service/status"));
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	FProjectProject01VersionContract::ApplyToRequest(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->OnProcessRequestComplete().BindUObject(
		this, &UProjectProject01AuthSubsystem::HandleServiceStatusRequestComplete);
	ServiceStatusRequest = Request;
	if (!Request->ProcessRequest())
	{
		ServiceStatusRequest.Reset();
		bServiceReachable = false;
		OnServiceStatusChanged.Broadcast(false, FString(), FString(),
			TEXT("서버가 꺼져 있습니다."));
	}
}

void UProjectProject01AuthSubsystem::MarkRoleGuideSeenForAccount(const bool bMannequinRole)
{
	if (!IsSignedIn())
	{
		return;
	}

	bool& bSeenFlag = bMannequinRole
		? bSeenMannequinGuideForAccount
		: bSeenSurvivorGuideForAccount;
	if (bSeenFlag)
	{
		return;
	}
	bSeenFlag = true;

	// 한 클라이언트는 한 경기에서 하나의 역할만 확인한다. 이전 기록 요청이 아직 끝나지
	// 않았다면 다음 로그인에서 서버 상태가 다시 내려오므로 중복 요청하지 않는다.
	if (GuideProgressRequest.IsValid() || ApiBaseUrl.IsEmpty() || !IsApiBaseUrlAllowed())
	{
		return;
	}

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("role"), bMannequinRole ? TEXT("Mannequin") : TEXT("Survivor"));
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiBaseUrl + TEXT("/api/auth/guide-progress"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	FProjectProject01VersionContract::ApplyToRequest(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->SetContentAsString(Body);
	Request->OnProcessRequestComplete().BindUObject(
		this, &UProjectProject01AuthSubsystem::HandleGuideProgressRequestComplete);
	GuideProgressRequest = Request;
	if (!Request->ProcessRequest())
	{
		GuideProgressRequest.Reset();
		UE_LOG(LogProjectProject01Auth, Warning,
			TEXT("Could not start the account role-guide progress request."));
	}
}

void UProjectProject01AuthSubsystem::HandleGuideProgressRequestComplete(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	const bool bConnectedSuccessfully)
{
	if (GuideProgressRequest != Request)
	{
		return;
	}
	GuideProgressRequest.Reset();
	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() < 200 ||
		Response->GetResponseCode() >= 300)
	{
		UE_LOG(LogProjectProject01Auth, Warning,
			TEXT("Account role-guide progress was not stored; it will be retried after a later login."));
	}
}

void UProjectProject01AuthSubsystem::HandleServiceStatusRequestComplete(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	const bool bConnectedSuccessfully)
{
	if (ServiceStatusRequest != Request) return;
	ServiceStatusRequest.Reset();
	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() < 200 ||
		Response->GetResponseCode() >= 300)
	{
		bServiceReachable = false;
		OnServiceStatusChanged.Broadcast(false, FString(), FString(),
			TEXT("서버가 꺼져 있습니다."));
		return;
	}
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json) ||
		!Json.IsValid() || !Json->TryGetBoolField(TEXT("maintenanceEnabled"), bMaintenanceEnabled))
	{
		bServiceReachable = false;
		OnServiceStatusChanged.Broadcast(false, FString(), FString(),
			TEXT("서버가 꺼져 있습니다."));
		return;
	}
	bServiceReachable = true;
	ServiceAnnouncement.Reset();OperatorAnnouncement.Reset();
	ServiceShutdownAtUtc.Reset();
	Json->TryGetStringField(TEXT("announcement"), ServiceAnnouncement);
    Json->TryGetNumberField(TEXT("revision"), ServiceRevision);
    if(!Json->TryGetStringField(TEXT("baseAnnouncement"),OperatorAnnouncement))OperatorAnnouncement=ServiceAnnouncement;
    AnnouncementBroadcastId.Reset();AnnouncementTag.Reset();AnnouncementRepeatCount=1;
    Json->TryGetStringField(TEXT("announcementBroadcastId"),AnnouncementBroadcastId);Json->TryGetStringField(TEXT("announcementTag"),AnnouncementTag);Json->TryGetNumberField(TEXT("announcementRepeatCount"),AnnouncementRepeatCount);AnnouncementRepeatCount=FMath::Clamp(AnnouncementRepeatCount,1,100);
    bOperatorAnnouncementActive=!OperatorAnnouncement.IsEmpty();Json->TryGetBoolField(TEXT("announcementActive"),bOperatorAnnouncementActive);AnnouncementRemainingSeconds=-1;Json->TryGetNumberField(TEXT("announcementRemainingSeconds"),AnnouncementRemainingSeconds);
	Json->TryGetStringField(TEXT("shutdownAtUtc"), ServiceShutdownAtUtc);
	FString Message;
	Json->TryGetStringField(TEXT("message"), Message);
	OnServiceStatusChanged.Broadcast(
		bMaintenanceEnabled, ServiceAnnouncement, ServiceShutdownAtUtc, Message);
}

void UProjectProject01AuthSubsystem::RefreshSession(TFunction<void(bool)> Completion)
{
	if (RefreshToken.IsEmpty() || ActiveRequest.IsValid())
	{
		Completion(false);
		return;
	}
	ActiveRefreshCompletion = MoveTemp(Completion);
	AuthState = EProjectProject01AuthState::Refreshing;
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("refreshToken"), RefreshToken);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));
	SendRequest(EAuthOperation::Refresh, TEXT("/api/auth/refresh"), Body, 0);
}

void UProjectProject01AuthSubsystem::SendRequest(
	const EAuthOperation Operation,
	const FString& Endpoint,
	const FString& JsonBody,
	const int32 RetryIndex)
{
	if (ApiBaseUrl.IsEmpty() || !IsApiBaseUrlAllowed())
	{
		CompleteOperation(Operation, false, TEXT("인증 API 주소가 설정되지 않았습니다."), FString());
		return;
	}
	FString CircuitError;
	if(!ProjectRequestProtection::Acquire(GetGameInstance(),ApiBaseUrl,RequestTimeoutSeconds,ActiveCircuitTicket,CircuitError))
	{
		if(Operation==EAuthOperation::Logout){ClearSession();CompleteOperation(Operation,true,TEXT("이 기기에서 로그아웃했습니다."),FString());}
		else CompleteOperation(Operation,false,CircuitError,FString());
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiBaseUrl + Endpoint);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	FProjectProject01VersionContract::ApplyToRequest(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	if (Operation == EAuthOperation::Logout && !AccessToken.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	}
	Request->SetContentAsString(JsonBody);
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&UProjectProject01AuthSubsystem::HandleRequestComplete,
		Operation,
		Endpoint,
		JsonBody,
		RetryIndex);
	ActiveRequest = Request;

	if (!Request->ProcessRequest())
	{
		ProjectRequestProtection::Complete(GetGameInstance(),ActiveCircuitTicket,Request,nullptr,false);
		ProjectHttpDiagnostics::Complete(GetWorld(),TEXT("Auth"),Request,nullptr,false,RetryIndex,MaxRetryCount,false,RequestTimeoutSeconds);
		ActiveRequest.Reset();
		if (Operation == EAuthOperation::Logout)
		{
			ClearSession();
			CompleteOperation(Operation, true,
				TEXT("서버에 연결할 수 없어 이 기기의 로그인 상태를 정리했습니다."), FString());
		}
		else
		{
			CompleteOperation(Operation, false, TEXT("인증 요청을 시작하지 못했습니다."), FString());
		}
	}
}

void UProjectProject01AuthSubsystem::HandleRequestComplete(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	const bool bConnectedSuccessfully,
	const EAuthOperation Operation,
	FString Endpoint,
	FString JsonBody,
	const int32 RetryIndex)
{
	if (ActiveRequest != Request) return;
	ActiveRequest.Reset();

	const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
	ProjectHttpDiagnostics::Complete(GetWorld(),TEXT("Auth"),Request,Response,bConnectedSuccessfully,RetryIndex,MaxRetryCount,
		(!bConnectedSuccessfully||ProjectProject01Auth::IsRetryableStatus(StatusCode))&&RetryIndex<MaxRetryCount,RequestTimeoutSeconds);
	ProjectRequestProtection::Complete(GetGameInstance(),ActiveCircuitTicket,Request,Response,bConnectedSuccessfully);
	if ((!bConnectedSuccessfully || ProjectProject01Auth::IsRetryableStatus(StatusCode)) && RetryIndex < MaxRetryCount)
	{
		UE_LOG(LogProjectProject01Auth, Warning,
			TEXT("Authentication request retry %d/%d after status %d."), RetryIndex + 1, MaxRetryCount, StatusCode);
		SendRequest(Operation, Endpoint, JsonBody, RetryIndex + 1);
		return;
	}

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		if (Operation == EAuthOperation::Logout)
		{
			ClearSession();
			CompleteOperation(Operation, true,
				TEXT("인증 서버가 꺼져 있어 이 기기의 로그인 상태를 정리했습니다."), FString());
		}
		else
		{
			CompleteOperation(Operation, false, TEXT("인증 서버에 연결하지 못했습니다."), FString());
		}
		return;
	}

	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	const bool bJsonValid = FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid();
	FString Message = TEXT("인증 서버 응답을 처리하지 못했습니다.");
	if (bJsonValid)
	{
		Json->TryGetStringField(TEXT("message"), Message);
	}

	if (StatusCode < 200 || StatusCode >= 300 || !bJsonValid)
	{
		if (Operation == EAuthOperation::Logout && StatusCode == 401)
		{
			ClearSession();
			CompleteOperation(Operation, true, TEXT("이미 종료된 로그인 상태를 정리했습니다."), FString());
			return;
		}
		CompleteOperation(Operation, false, Message, FString());
		return;
	}

	FString DisplayName;
	Json->TryGetStringField(TEXT("displayName"), DisplayName);
	bool bRegistrationOnly = false;
	if (Operation == EAuthOperation::Register && Json->TryGetBoolField(TEXT("registrationOnly"), bRegistrationOnly) && bRegistrationOnly)
	{
		ClearSession();
		CompleteOperation(Operation, true, Message, DisplayName);
		return;
	}
	if (Operation == EAuthOperation::Login || Operation == EAuthOperation::Register || Operation == EAuthOperation::Refresh)
	{
		FString NewAccessToken;
		FString NewRefreshToken;
		if (!Json->TryGetStringField(TEXT("accessToken"), NewAccessToken) || NewAccessToken.IsEmpty() ||
			!Json->TryGetStringField(TEXT("refreshToken"), NewRefreshToken) || NewRefreshToken.IsEmpty())
		{
			CompleteOperation(Operation, false, TEXT("인증 서버가 필수 토큰을 반환하지 않았습니다."), FString());
			return;
		}
		AccessToken = MoveTemp(NewAccessToken);
        if(Operation!=EAuthOperation::Refresh)++SessionGeneration;
		RefreshToken = MoveTemp(NewRefreshToken);
		SignedInDisplayName = DisplayName;
		bSeenSurvivorGuideForAccount = false;
		bSeenMannequinGuideForAccount = false;
		Json->TryGetBoolField(TEXT("seenSurvivorGuide"), bSeenSurvivorGuideForAccount);
		Json->TryGetBoolField(TEXT("seenMannequinGuide"), bSeenMannequinGuideForAccount);
	}
	else if (Operation == EAuthOperation::Logout)
	{
		ClearSession();
	}

	CompleteOperation(Operation, true, Message, DisplayName);
}

void UProjectProject01AuthSubsystem::CompleteOperation(
	const EAuthOperation Operation,
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	if (Operation == EAuthOperation::Register)
	{
		AuthState = bSuccess && !AccessToken.IsEmpty() ? EProjectProject01AuthState::SignedIn : EProjectProject01AuthState::SignedOut;
		OnRegistrationCompleted.Broadcast(bSuccess, Message, DisplayName);
	}
	else if (Operation == EAuthOperation::Login)
	{
		AuthState = bSuccess ? EProjectProject01AuthState::SignedIn : EProjectProject01AuthState::SignedOut;
		OnLoginCompleted.Broadcast(bSuccess, Message, DisplayName);
	}
	else if (Operation == EAuthOperation::Refresh)
	{
		if (!bSuccess)
		{
			InvalidateSession(TEXT("로그인 세션이 만료되었거나 관리자에 의해 종료되었습니다."));
		}
		else
		{
			AuthState = EProjectProject01AuthState::SignedIn;
		}
		TFunction<void(bool)> Completion = MoveTemp(ActiveRefreshCompletion);
		ActiveRefreshCompletion = nullptr;
		if (Completion)
		{
			Completion(bSuccess);
		}
	}
	else
	{
		AuthState = bSuccess ? EProjectProject01AuthState::SignedOut : EProjectProject01AuthState::SignedIn;
		OnLogoutCompleted.Broadcast(bSuccess, Message, DisplayName);
	}
}

bool UProjectProject01AuthSubsystem::ValidateCommonInput(
	const FString& AccountId,
	const FString& Password,
	FString& OutError) const
{
	const FString CleanAccountId = AccountId.TrimStartAndEnd();
	if (CleanAccountId.Len() < ProjectProject01Auth::MinAccountLength ||
		CleanAccountId.Len() > ProjectProject01Auth::MaxAccountLength)
	{
		OutError = TEXT("계정 ID는 3~32자여야 합니다.");
		return false;
	}
	for (const TCHAR Character : CleanAccountId)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
		{
			OutError = TEXT("계정 ID에는 영문, 숫자, 밑줄만 사용할 수 있습니다.");
			return false;
		}
	}
	if (Password.Len() < ProjectProject01Auth::MinPasswordLength ||
		Password.Len() > ProjectProject01Auth::MaxPasswordLength)
	{
		OutError = TEXT("비밀번호는 10~128자여야 합니다.");
		return false;
	}
	OutError.Reset();
	return true;
}

bool UProjectProject01AuthSubsystem::CanStartOperation(const EAuthOperation Operation)
{
	if (ActiveRequest.IsValid())
	{
		const FString Message = TEXT("이전 인증 요청이 진행 중입니다.");
		if (Operation == EAuthOperation::Register)
		{
			OnRegistrationCompleted.Broadcast(false, Message, FString());
		}
		else if (Operation == EAuthOperation::Login)
		{
			OnLoginCompleted.Broadcast(false, Message, FString());
		}
		else
		{
			OnLogoutCompleted.Broadcast(false, Message, FString());
		}
		return false;
	}
	AuthState = Operation == EAuthOperation::Register
		? EProjectProject01AuthState::Registering
		: Operation == EAuthOperation::Login
			? EProjectProject01AuthState::SigningIn
			: Operation == EAuthOperation::Refresh
				? EProjectProject01AuthState::Refreshing
				: EProjectProject01AuthState::SigningOut;
	return true;
}

void UProjectProject01AuthSubsystem::ClearSession()
{
    ++SessionGeneration;
	if (GuideProgressRequest.IsValid())
	{
		GuideProgressRequest->OnProcessRequestComplete().Unbind();
		GuideProgressRequest->CancelRequest();
		GuideProgressRequest.Reset();
	}
	AccessToken.Reset();
	RefreshToken.Reset();
	SignedInDisplayName.Reset();
	bSeenSurvivorGuideForAccount = false;
	bSeenMannequinGuideForAccount = false;
	AuthState = EProjectProject01AuthState::SignedOut;
    OnLocalSessionCleared.Broadcast();
}

bool UProjectProject01AuthSubsystem::TickSessionMonitor(const float DeltaSeconds)
{
	(void)DeltaSeconds;
	CheckAuthenticatedSession();
	return true;
}

void UProjectProject01AuthSubsystem::CheckAuthenticatedSession()
{
	if (!IsSignedIn() || ActiveRequest.IsValid() || SessionValidationRequest.IsValid() ||
		ApiBaseUrl.IsEmpty() || !IsApiBaseUrlAllowed())
	{
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiBaseUrl + TEXT("/api/auth/heartbeat"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	FProjectProject01VersionContract::ApplyToRequest(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->OnProcessRequestComplete().BindUObject(
		this, &UProjectProject01AuthSubsystem::HandleSessionValidationComplete);
	SessionValidationRequest = Request;
	if (!Request->ProcessRequest())
	{
		SessionValidationRequest.Reset();
		InvalidateSession(TEXT("인증 서버가 꺼져 있습니다. 다시 로그인하세요."));
	}
}

void UProjectProject01AuthSubsystem::HandleSessionValidationComplete(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	const bool bConnectedSuccessfully)
{
	if (SessionValidationRequest != Request)
	{
		return;
	}
	SessionValidationRequest.Reset();
	if (!IsSignedIn())
	{
		return;
	}
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		InvalidateSession(TEXT("인증 서버 연결이 종료되었습니다. 다시 로그인하세요."));
		return;
	}

	const int32 StatusCode = Response->GetResponseCode();
	if (StatusCode >= 200 && StatusCode < 300)
	{
		return;
	}
	if (StatusCode == 401 && !RefreshToken.IsEmpty())
	{
		RefreshSession([](const bool) {});
		return;
	}

	FString Message = TEXT("인증 세션을 확인할 수 없습니다. 다시 로그인하세요.");
	TSharedPtr<FJsonObject> Json;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json) &&
		Json.IsValid())
	{
		Json->TryGetStringField(TEXT("message"), Message);
	}
	InvalidateSession(Message);
}

void UProjectProject01AuthSubsystem::InvalidateSession(const FString& Message)
{
	const bool bHadSession = AuthState != EProjectProject01AuthState::SignedOut ||
		!AccessToken.IsEmpty() || !RefreshToken.IsEmpty();
	if (!bHadSession)
	{
		return;
	}
	if (SessionValidationRequest.IsValid())
	{
		SessionValidationRequest->OnProcessRequestComplete().Unbind();
		SessionValidationRequest->CancelRequest();
		SessionValidationRequest.Reset();
	}
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	ActiveRefreshCompletion = nullptr;
	ClearSession();
	OnSessionInvalidated.Broadcast(Message.IsEmpty()
		? TEXT("로그인 세션이 종료되었습니다. 다시 로그인하세요.")
		: Message);
}

bool UProjectProject01AuthSubsystem::IsApiBaseUrlAllowed() const
{
	return ApiBaseUrl.StartsWith(TEXT("https://")) || ApiBaseUrl.StartsWith(TEXT("http://127.0.0.1")) ||
		ApiBaseUrl.StartsWith(TEXT("http://localhost")) ||
		(!AllowedInsecureVpnApiBaseUrl.IsEmpty() &&
			ApiBaseUrl.Equals(AllowedInsecureVpnApiBaseUrl, ESearchCase::IgnoreCase));
}

bool UProjectProject01AuthSubsystem::ConfigureLocalTestApiEndpoint(const FString& InApiBaseUrl)
{
	if (AuthState != EProjectProject01AuthState::SignedOut || ActiveRequest.IsValid())
	{
		return false;
	}
	if (ServiceStatusRequest.IsValid())
	{
		ServiceStatusRequest->OnProcessRequestComplete().Unbind();
		ServiceStatusRequest->CancelRequest();
		ServiceStatusRequest.Reset();
	}
	ApiBaseUrl = ProjectProject01Auth::NormalizeBaseUrl(InApiBaseUrl);
	AllowedInsecureVpnApiBaseUrl = ApiBaseUrl;
	const bool bAllowed = IsApiBaseUrlAllowed();
	bServiceReachable = false;
	bMaintenanceEnabled = false;
	ServiceAnnouncement.Reset();OperatorAnnouncement.Reset();
	ServiceShutdownAtUtc.Reset();
	OnServiceStatusChanged.Broadcast(false, FString(), FString(),
		bAllowed ? TEXT("서버 상태를 다시 확인하고 있습니다.") : TEXT("서버가 꺼져 있습니다."));
	return bAllowed;
}

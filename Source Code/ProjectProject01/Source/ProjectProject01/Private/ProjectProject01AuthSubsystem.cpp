// File: Source/ProjectProject01/Private/ProjectProject01AuthSubsystem.cpp
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01AuthSubsystem.h"

#include "Dom/JsonObject.h"
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
	RequestTimeoutSeconds = FMath::Clamp(RequestTimeoutSeconds, 2.0f, 60.0f);
	MaxRetryCount = FMath::Clamp(MaxRetryCount, 0, 2);

	if (!ApiBaseUrl.StartsWith(TEXT("https://")) && !ApiBaseUrl.StartsWith(TEXT("http://127.0.0.1")) &&
		!ApiBaseUrl.StartsWith(TEXT("http://localhost")))
	{
		UE_LOG(LogProjectProject01Auth, Error,
			TEXT("Authentication API must use HTTPS except for loopback development: %s"), *ApiBaseUrl);
	}
}

void UProjectProject01AuthSubsystem::Deinitialize()
{
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

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiBaseUrl + Endpoint);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
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
		ActiveRequest.Reset();
		CompleteOperation(Operation, false, TEXT("인증 요청을 시작하지 못했습니다."), FString());
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
	if (ActiveRequest == Request)
	{
		ActiveRequest.Reset();
	}

	const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
	if ((!bConnectedSuccessfully || ProjectProject01Auth::IsRetryableStatus(StatusCode)) && RetryIndex < MaxRetryCount)
	{
		UE_LOG(LogProjectProject01Auth, Warning,
			TEXT("Authentication request retry %d/%d after status %d."), RetryIndex + 1, MaxRetryCount, StatusCode);
		SendRequest(Operation, Endpoint, JsonBody, RetryIndex + 1);
		return;
	}

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		CompleteOperation(Operation, false, TEXT("인증 서버에 연결하지 못했습니다."), FString());
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
		CompleteOperation(Operation, false, Message, FString());
		return;
	}

	FString DisplayName;
	Json->TryGetStringField(TEXT("displayName"), DisplayName);
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
		RefreshToken = MoveTemp(NewRefreshToken);
		SignedInDisplayName = DisplayName;
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
		AuthState = bSuccess ? EProjectProject01AuthState::SignedIn : EProjectProject01AuthState::SignedOut;
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
			ClearSession();
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
	AccessToken.Reset();
	RefreshToken.Reset();
	SignedInDisplayName.Reset();
	AuthState = EProjectProject01AuthState::SignedOut;
}

bool UProjectProject01AuthSubsystem::IsApiBaseUrlAllowed() const
{
	return ApiBaseUrl.StartsWith(TEXT("https://")) || ApiBaseUrl.StartsWith(TEXT("http://127.0.0.1")) ||
		ApiBaseUrl.StartsWith(TEXT("http://localhost"));
}

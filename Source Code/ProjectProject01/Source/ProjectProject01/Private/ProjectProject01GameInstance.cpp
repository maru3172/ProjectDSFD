// File: Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp
// Build target: ProjectProject01 / ProjectProject01Server, Unreal Engine 5.8.2

#include "ProjectProject01GameInstance.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectProject01NetworkSecurity, Log, All);

namespace ProjectProject01NetworkSecurity
{
	constexpr int32 Aes256KeyBytes = 32;
	TArray<uint8> PendingClientEncryptionKey;
	FString PendingClientTicket;
	TMap<FString, FProjectProject01ValidatedJoinClaim> ValidatedClaims;

	FString NormalizeBaseUrl(FString Url)
	{
		Url.TrimStartAndEndInline();
		while (Url.EndsWith(TEXT("/")))
		{
			Url.LeftChopInline(1);
		}
		return Url;
	}

	void CompleteEncryptionFailure(
		const FOnEncryptionKeyResponse& Delegate,
		const EEncryptionResponse ResponseCode,
		const FString& Error)
	{
		FEncryptionKeyResponse Response(ResponseCode, Error);
		Delegate.ExecuteIfBound(Response);
	}

	bool ParseEncryptionKey(const FString& Base64, TArray<uint8>& OutKey)
	{
		OutKey.Reset();
		return FBase64::Decode(Base64, OutKey) && OutKey.Num() == Aes256KeyBytes;
	}
}

void UProjectProject01GameInstance::Init()
{
	SecurityApiBaseUrl = ProjectProject01NetworkSecurity::NormalizeBaseUrl(SecurityApiBaseUrl);
	SecurityRequestTimeoutSeconds = FMath::Clamp(SecurityRequestTimeoutSeconds, 2.0f, 30.0f);
	Super::Init();

	if (!IsBackendUrlAllowed())
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Error,
			TEXT("Security API must use HTTPS except for loopback development: %s"), *SecurityApiBaseUrl);
	}
}

void UProjectProject01GameInstance::Shutdown()
{
	for (const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& Request : PendingSecurityRequests)
	{
		if (Request.IsValid())
		{
			Request->OnProcessRequestComplete().Unbind();
			Request->CancelRequest();
		}
	}
	PendingSecurityRequests.Reset();
	Super::Shutdown();
}

bool UProjectProject01GameInstance::ConfigurePendingGameConnection(
	const FString& Ticket,
	const FString& EncryptionKeyBase64,
	const FString& RoomId,
	const FString& MatchId,
	const FString& Role)
{
	TArray<uint8> Key;
	if (Ticket.Len() < 48 || Ticket.Len() > 256 || RoomId.IsEmpty() || MatchId.IsEmpty() ||
		(Role != TEXT("Mannequin") && Role != TEXT("Survivor")) ||
		!ProjectProject01NetworkSecurity::ParseEncryptionKey(EncryptionKeyBase64, Key))
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Error,
			TEXT("Rejected malformed game ticket response before ClientTravel."));
		return false;
	}

	ProjectProject01NetworkSecurity::PendingClientTicket = Ticket;
	ProjectProject01NetworkSecurity::PendingClientEncryptionKey = MoveTemp(Key);
	return true;
}

void UProjectProject01GameInstance::ReceivedNetworkEncryptionToken(
	const FString& EncryptionToken,
	const FOnEncryptionKeyResponse& Delegate)
{
	if (EncryptionToken.Len() < 48 || EncryptionToken.Len() > 256 || !IsBackendUrlAllowed())
	{
		ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
			Delegate, EEncryptionResponse::InvalidToken, TEXT("Invalid game connection ticket."));
		return;
	}

	const FString ServerSecret = LoadGameServerSharedSecret();
	if (ServerSecret.Len() < 32)
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Error,
			TEXT("Dedicated server security secret is not configured."));
		ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
			Delegate, EEncryptionResponse::Failure, TEXT("Game server security is not configured."));
		return;
	}

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("ticket"), EncryptionToken);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/server/game-tickets/consume"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-ProjectProject01-Server-Secret"), ServerSecret);
	Request->SetContentAsString(Body);
	Request->SetTimeout(SecurityRequestTimeoutSeconds);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, EncryptionToken, Delegate](
			const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& CompletedRequest,
			const TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>& Response,
			const bool bConnectedSuccessfully)
		{
			RemovePendingRequest(CompletedRequest);
			if (!bConnectedSuccessfully || !Response.IsValid() ||
				Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
			{
				UE_LOG(LogProjectProject01NetworkSecurity, Warning,
					TEXT("Game ticket validation failed with HTTP status %d."),
					Response.IsValid() ? Response->GetResponseCode() : 0);
				ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
					Delegate, EEncryptionResponse::InvalidToken, TEXT("Game connection ticket was rejected."));
				return;
			}

			TSharedPtr<FJsonObject> JsonResponse;
			if (!FJsonSerializer::Deserialize(
				TJsonReaderFactory<>::Create(Response->GetContentAsString()), JsonResponse) || !JsonResponse.IsValid())
			{
				ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
					Delegate, EEncryptionResponse::Failure, TEXT("Invalid game ticket validation response."));
				return;
			}

			FString EncryptionKeyBase64;
			FProjectProject01ValidatedJoinClaim Claim;
			if (!JsonResponse->TryGetStringField(TEXT("encryptionKey"), EncryptionKeyBase64) ||
				!JsonResponse->TryGetStringField(TEXT("userId"), Claim.UserId) ||
				!JsonResponse->TryGetStringField(TEXT("displayName"), Claim.DisplayName) ||
				!JsonResponse->TryGetStringField(TEXT("roomId"), Claim.RoomId) ||
				!JsonResponse->TryGetStringField(TEXT("matchId"), Claim.MatchId) ||
				!JsonResponse->TryGetStringField(TEXT("role"), Claim.Role))
			{
				ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
					Delegate, EEncryptionResponse::Failure, TEXT("Incomplete game ticket validation response."));
				return;
			}

			TArray<uint8> Key;
			if (!ProjectProject01NetworkSecurity::ParseEncryptionKey(EncryptionKeyBase64, Key))
			{
				ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
					Delegate, EEncryptionResponse::NoKey, TEXT("Invalid AES-GCM key."));
				return;
			}

			const FDateTime NowUtc = FDateTime::UtcNow();
			for (auto It = ProjectProject01NetworkSecurity::ValidatedClaims.CreateIterator(); It; ++It)
			{
				if (It.Value().ValidUntilUtc <= NowUtc)
				{
					It.RemoveCurrent();
				}
			}
			Claim.ValidUntilUtc = NowUtc + FTimespan::FromSeconds(30.0);
			ProjectProject01NetworkSecurity::ValidatedClaims.Add(EncryptionToken, MoveTemp(Claim));
			FEncryptionKeyResponse KeyResponse;
			KeyResponse.Response = EEncryptionResponse::Success;
			KeyResponse.EncryptionData.Key = MoveTemp(Key);
			KeyResponse.EncryptionData.Identifier = TEXT("ProjectProject01Match");
			Delegate.ExecuteIfBound(KeyResponse);
		});

	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		RemovePendingRequest(Request);
		ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
			Delegate, EEncryptionResponse::Failure, TEXT("Unable to start game ticket validation."));
	}
}

void UProjectProject01GameInstance::ReceivedNetworkEncryptionAck(const FOnEncryptionKeyResponse& Delegate)
{
	if (ProjectProject01NetworkSecurity::PendingClientEncryptionKey.Num() !=
		ProjectProject01NetworkSecurity::Aes256KeyBytes)
	{
		ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
			Delegate, EEncryptionResponse::NoKey, TEXT("No pending AES-GCM key is available."));
		return;
	}

	FEncryptionKeyResponse Response;
	Response.Response = EEncryptionResponse::Success;
	Response.EncryptionData.Key = ProjectProject01NetworkSecurity::PendingClientEncryptionKey;
	Response.EncryptionData.Identifier = TEXT("ProjectProject01Match");
	Delegate.ExecuteIfBound(Response);
}

bool UProjectProject01GameInstance::ConsumeValidatedJoinClaim(
	const FString& Ticket,
	FProjectProject01ValidatedJoinClaim& OutClaim)
{
	if (FProjectProject01ValidatedJoinClaim* Claim = ProjectProject01NetworkSecurity::ValidatedClaims.Find(Ticket))
	{
		if (Claim->ValidUntilUtc <= FDateTime::UtcNow())
		{
			ProjectProject01NetworkSecurity::ValidatedClaims.Remove(Ticket);
			return false;
		}
		OutClaim = MoveTemp(*Claim);
		ProjectProject01NetworkSecurity::ValidatedClaims.Remove(Ticket);
		return true;
	}
	return false;
}

void UProjectProject01GameInstance::SubmitAuthoritativeMatchResult(
	const FString& MatchId,
	const FString& UserId,
	const FString& Role,
	const bool bSuccess,
	const int32 CaptureCount,
	const double FirstCaptureSeconds,
	const double AllCapturedSeconds,
	const int32 RescueCount,
	const double EscapeSeconds)
{
	const UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_Client || !IsBackendUrlAllowed())
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Warning,
			TEXT("Rejected authoritative match result outside a server world."));
		return;
	}
	const FString ServerSecret = LoadGameServerSharedSecret();
	if (ServerSecret.Len() < 32)
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Error,
			TEXT("Cannot submit match result because the server secret is unavailable."));
		return;
	}

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("matchId"), MatchId);
	Json->SetStringField(TEXT("userId"), UserId);
	Json->SetStringField(TEXT("role"), Role);
	Json->SetBoolField(TEXT("success"), bSuccess);
	Json->SetNumberField(TEXT("captureCount"), CaptureCount);
	Json->SetNumberField(TEXT("firstCaptureSeconds"), FirstCaptureSeconds);
	Json->SetNumberField(TEXT("allCapturedSeconds"), AllCapturedSeconds);
	Json->SetNumberField(TEXT("rescueCount"), RescueCount);
	Json->SetNumberField(TEXT("escapeSeconds"), EscapeSeconds);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/server/matches/results"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-ProjectProject01-Server-Secret"), ServerSecret);
	Request->SetContentAsString(Body);
	Request->SetTimeout(SecurityRequestTimeoutSeconds);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this](const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& CompletedRequest,
			const TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>& Response,
			const bool bConnectedSuccessfully)
		{
			RemovePendingRequest(CompletedRequest);
			if (!bConnectedSuccessfully || !Response.IsValid() ||
				Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
			{
				UE_LOG(LogProjectProject01NetworkSecurity, Error,
					TEXT("Authoritative match result submission failed with status %d."),
					Response.IsValid() ? Response->GetResponseCode() : 0);
			}
		});
	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		RemovePendingRequest(Request);
		UE_LOG(LogProjectProject01NetworkSecurity, Error, TEXT("Unable to start authoritative result submission."));
	}
}

FString UProjectProject01GameInstance::LoadGameServerSharedSecret() const
{
	FString Secret = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECTPROJECT01_GAME_SERVER_SECRET"));
	Secret.TrimStartAndEndInline();
#if !UE_BUILD_SHIPPING
	if (Secret.IsEmpty())
	{
		const FString LocalConfigPath = FPaths::Combine(
			FPaths::ProjectDir(), TEXT("Tools/ProjectProject01Backend/LocalMySql/ProjectProject01Backend.local.json"));
		FString JsonText;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(JsonText, *LocalConfigPath) &&
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) && Root.IsValid())
		{
			const TSharedPtr<FJsonObject>* GameServer = nullptr;
			if (Root->TryGetObjectField(TEXT("GameServer"), GameServer) && GameServer != nullptr && GameServer->IsValid())
			{
				(*GameServer)->TryGetStringField(TEXT("SharedSecret"), Secret);
				Secret.TrimStartAndEndInline();
			}
		}
	}
#endif
	return Secret;
}

bool UProjectProject01GameInstance::IsBackendUrlAllowed() const
{
	return SecurityApiBaseUrl.StartsWith(TEXT("https://")) ||
		SecurityApiBaseUrl.StartsWith(TEXT("http://127.0.0.1")) ||
		SecurityApiBaseUrl.StartsWith(TEXT("http://localhost"));
}

void UProjectProject01GameInstance::RemovePendingRequest(
	const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& Request)
{
	PendingSecurityRequests.Remove(Request);
}

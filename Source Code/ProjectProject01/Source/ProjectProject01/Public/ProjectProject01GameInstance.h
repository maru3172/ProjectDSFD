// File: Source/ProjectProject01/Public/ProjectProject01GameInstance.h
// Build target: ProjectProject01 / ProjectProject01Server, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/NetworkDelegates.h"
#include "ProjectProject01GameInstance.generated.h"

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

private:
	FString LoadGameServerSharedSecret() const;
	bool IsBackendUrlAllowed() const;
	void RemovePendingRequest(const TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe>& Request);

	UPROPERTY(Config)
	FString SecurityApiBaseUrl = TEXT("http://127.0.0.1:5080");

	UPROPERTY(Config)
	float SecurityRequestTimeoutSeconds = 8.0f;

	TArray<TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe>> PendingSecurityRequests;
};

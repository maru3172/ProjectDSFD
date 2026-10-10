// File: Source/ProjectProject01/Public/ProjectProject01AuthSubsystem.h
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectProject01AuthSubsystem.generated.h"

UENUM(BlueprintType)
enum class EProjectProject01AuthState : uint8
{
	SignedOut,
	Registering,
	SigningIn,
	Refreshing,
	SigningOut,
	SignedIn
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FProjectProject01AuthResult,
	bool, bSuccess,
	const FString&, Message,
	const FString&, DisplayName);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FProjectProject01ServiceStatusChanged,
	bool, bMaintenanceEnabled,
	const FString&, Announcement,
	const FString&, ShutdownAtUtc,
	const FString&, Message);

UCLASS(Config=Game)
class PROJECTPROJECT01_API UProjectProject01AuthSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category="ProjectProject01|Authentication")
	FProjectProject01AuthResult OnRegistrationCompleted;

	UPROPERTY(BlueprintAssignable, Category="ProjectProject01|Authentication")
	FProjectProject01AuthResult OnLoginCompleted;

	UPROPERTY(BlueprintAssignable, Category="ProjectProject01|Authentication")
	FProjectProject01AuthResult OnLogoutCompleted;

	UPROPERTY(BlueprintAssignable, Category="ProjectProject01|Service")
	FProjectProject01ServiceStatusChanged OnServiceStatusChanged;

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Authentication")
	void RegisterAccount(const FString& AccountId, const FString& Password, const FString& DisplayName);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Authentication")
	void Login(const FString& AccountId, const FString& Password);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Authentication")
	void Logout();

	/** 로그인 화면을 떠날 때 아직 끝나지 않은 로그인/회원가입 요청을 취소하고 잠금 상태를 해제합니다. */
	void CancelPendingAuthentication();

	/** 멀티플레이 서비스 상태만 확인합니다. 싱글플레이에서는 자동 호출하지 않습니다. */
	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Service")
	void CheckMultiplayerServiceStatus();

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Service")
	bool IsMultiplayerMaintenanceActive() const { return bMaintenanceEnabled; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Service")
	FString GetServiceAnnouncement() const { return ServiceAnnouncement; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Authentication")
	bool IsSignedIn() const { return AuthState == EProjectProject01AuthState::SignedIn && !AccessToken.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Authentication")
	EProjectProject01AuthState GetAuthState() const { return AuthState; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Authentication")
	FString GetSignedInDisplayName() const { return SignedInDisplayName; }

	/** 인증된 로비 API 요청에만 사용하는 현재 액세스 토큰입니다. 로그나 UI에는 노출하지 않습니다. */
	const FString& GetAccessTokenForAuthenticatedRequest() const { return AccessToken; }
	const FString& GetApiBaseUrlForAuthenticatedRequest() const { return ApiBaseUrl; }
	const FString& GetAllowedInsecureVpnApiBaseUrl() const { return AllowedInsecureVpnApiBaseUrl; }
	/** 로그인 전에 사용자가 직접 입력한 서버 IP의 테스트 API 주소를 적용합니다. */
	bool ConfigureLocalTestApiEndpoint(const FString& InApiBaseUrl);
	void RefreshSession(TFunction<void(bool)> Completion);

private:
	enum class EAuthOperation : uint8
	{
		Register,
		Login,
		Refresh,
		Logout
	};

	void SendRequest(EAuthOperation Operation, const FString& Endpoint, const FString& JsonBody, int32 RetryIndex);
	void HandleRequestComplete(
		TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<class IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bConnectedSuccessfully,
		EAuthOperation Operation,
		FString Endpoint,
		FString JsonBody,
		int32 RetryIndex);
	void CompleteOperation(EAuthOperation Operation, bool bSuccess, const FString& Message, const FString& DisplayName);
	void HandleServiceStatusRequestComplete(
		TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<class IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bConnectedSuccessfully);
	bool ValidateCommonInput(const FString& AccountId, const FString& Password, FString& OutError) const;
	bool CanStartOperation(EAuthOperation Operation);
	void ClearSession();
	bool IsApiBaseUrlAllowed() const;

	UPROPERTY(Config)
	FString ApiBaseUrl = TEXT("http://127.0.0.1:5080");

	/** Hamachi 등 암호화된 개발 VPN에서만 허용할 정확한 HTTP API 주소입니다. 운영 배포에서는 비워 둡니다. */
	UPROPERTY(Config)
	FString AllowedInsecureVpnApiBaseUrl;

	UPROPERTY(Config)
	float RequestTimeoutSeconds = 5.0f;

	UPROPERTY(Config)
	int32 MaxRetryCount = 0;

	UPROPERTY(Transient)
	EProjectProject01AuthState AuthState = EProjectProject01AuthState::SignedOut;

	UPROPERTY(Transient)
	FString AccessToken;

	UPROPERTY(Transient)
	FString RefreshToken;

	UPROPERTY(Transient)
	FString SignedInDisplayName;

	UPROPERTY(Transient)
	bool bMaintenanceEnabled = false;

	UPROPERTY(Transient)
	FString ServiceAnnouncement;

	UPROPERTY(Transient)
	FString ServiceShutdownAtUtc;

	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;
	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ServiceStatusRequest;
	TFunction<void(bool)> ActiveRefreshCompletion;
};

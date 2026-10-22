// File: Source/ProjectProject01/Public/ProjectProject01AuthSubsystem.h
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectProject01SessionInvalidated,
	const FString&, Message);

UCLASS(Config=Game)
class PROJECTPROJECT01_API UProjectProject01AuthSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
    FSimpleMulticastDelegate OnLocalSessionCleared;
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

	/** 관리자 강제 로그아웃, 세션 폐기 또는 인증 서버 종료를 모든 화면에 동일하게 전달합니다. */
	UPROPERTY(BlueprintAssignable, Category="ProjectProject01|Authentication")
	FProjectProject01SessionInvalidated OnSessionInvalidated;

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Authentication")
	void RegisterAccount(const FString& AccountId, const FString& Password, const FString& DisplayName);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Authentication")
	void Login(const FString& AccountId, const FString& Password);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Authentication")
	void Logout();

	/** 서버 연결이 끊겨 원격 로그아웃까지 실패했을 때 재접속 반복을 막기 위해 로컬 토큰을 즉시 폐기합니다. */
	void ForceClearLocalSession();

	/** 인증된 기능 요청이 서버 단절을 확인했을 때 모든 화면을 로그인 전 상태로 되돌립니다. */
	void InvalidateSessionFromServerFailure(const FString& Message);

	/** 로그인 화면을 떠날 때 아직 끝나지 않은 로그인/회원가입 요청을 취소하고 잠금 상태를 해제합니다. */
	void CancelPendingAuthentication();

	/** 멀티플레이 서비스 상태만 확인합니다. 싱글플레이에서는 자동 호출하지 않습니다. */
	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Service")
	void CheckMultiplayerServiceStatus();

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Service")
	bool IsMultiplayerMaintenanceActive() const { return bMaintenanceEnabled; }

	/** 마지막 상태 확인에서 백엔드가 정상 응답했는지 반환합니다. */
	UFUNCTION(BlueprintPure, Category="ProjectProject01|Service")
	bool IsMultiplayerServiceReachable() const { return bServiceReachable; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Service")
	FString GetServiceAnnouncement() const { return ServiceAnnouncement; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Authentication")
	bool IsSignedIn() const { return AuthState == EProjectProject01AuthState::SignedIn && !AccessToken.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Authentication")
	EProjectProject01AuthState GetAuthState() const { return AuthState; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Authentication")
	FString GetSignedInDisplayName() const { return SignedInDisplayName; }

	/** 패키징 클라이언트에서 현재 로그인 계정이 해당 역할의 최초 안내를 확인했는지 반환합니다. */
	bool HasSeenSurvivorGuideForAccount() const { return bSeenSurvivorGuideForAccount; }
	bool HasSeenMannequinGuideForAccount() const { return bSeenMannequinGuideForAccount; }
	/** 안내가 실제로 표시된 직후 계정별 진행 상태를 백엔드에 저장합니다. */
	void MarkRoleGuideSeenForAccount(bool bMannequinRole);

	/** 인증된 로비 API 요청에만 사용하는 현재 액세스 토큰입니다. 로그나 UI에는 노출하지 않습니다. */
	const FString& GetAccessTokenForAuthenticatedRequest() const { return AccessToken; }
	const FString& GetApiBaseUrlForAuthenticatedRequest() const { return ApiBaseUrl; }
	const FString& GetAllowedInsecureVpnApiBaseUrl() const { return AllowedInsecureVpnApiBaseUrl; }
	/** 로그인 전에 사용자가 직접 입력한 서버 IP의 테스트 API 주소를 적용합니다. */
	bool ConfigureLocalTestApiEndpoint(const FString& InApiBaseUrl);
	void RefreshSession(TFunction<void(bool)> Completion);
    uint64 GetSessionGeneration() const {return SessionGeneration;}

private:
    uint64 SessionGeneration=0;
	friend class FProjectStaleFrontendResponseTest;
	friend class FProjectReviewAsyncStateTest;
	friend class FProjectLoginRegistrationTest;
	friend class FProjectSocialWidgetLayoutTest;
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
	bool TickSessionMonitor(float DeltaSeconds);
	void LogoutOnExit();
	FDelegateHandle ExitLogoutHandle;
	bool bExitLogoutSent = false;
	void CheckAuthenticatedSession();
	void HandleSessionValidationComplete(
		TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<class IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bConnectedSuccessfully);
	void HandleGuideProgressRequestComplete(
		TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<class IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bConnectedSuccessfully);
	void InvalidateSession(const FString& Message);
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
	bool bSeenSurvivorGuideForAccount = false;

	UPROPERTY(Transient)
	bool bSeenMannequinGuideForAccount = false;

	UPROPERTY(Transient)
	bool bMaintenanceEnabled = false;

	UPROPERTY(Transient)
	bool bServiceReachable = false;

	UPROPERTY(Transient)
	FString ServiceAnnouncement;

	UPROPERTY(Transient)
	FString ServiceShutdownAtUtc;

	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;
	uint64 ActiveCircuitTicket=0;
	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ServiceStatusRequest;
	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> SessionValidationRequest;
	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> GuideProgressRequest;
	TFunction<void(bool)> ActiveRefreshCompletion;
	FTSTicker::FDelegateHandle SessionMonitorTickerHandle;
};

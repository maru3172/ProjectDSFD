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

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Authentication")
	void RegisterAccount(const FString& AccountId, const FString& Password, const FString& DisplayName);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Authentication")
	void Login(const FString& AccountId, const FString& Password);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Authentication")
	void Logout();

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Authentication")
	bool IsSignedIn() const { return AuthState == EProjectProject01AuthState::SignedIn && !AccessToken.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Authentication")
	EProjectProject01AuthState GetAuthState() const { return AuthState; }

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Authentication")
	FString GetSignedInDisplayName() const { return SignedInDisplayName; }

	/** 인증된 로비 API 요청에만 사용하는 현재 액세스 토큰입니다. 로그나 UI에는 노출하지 않습니다. */
	const FString& GetAccessTokenForAuthenticatedRequest() const { return AccessToken; }

private:
	enum class EAuthOperation : uint8
	{
		Register,
		Login,
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
	bool ValidateCommonInput(const FString& AccountId, const FString& Password, FString& OutError) const;
	bool CanStartOperation(EAuthOperation Operation);
	void ClearSession();

	UPROPERTY(Config)
	FString ApiBaseUrl = TEXT("http://127.0.0.1:5080");

	UPROPERTY(Config)
	float RequestTimeoutSeconds = 10.0f;

	UPROPERTY(Config)
	int32 MaxRetryCount = 1;

	UPROPERTY(Transient)
	EProjectProject01AuthState AuthState = EProjectProject01AuthState::SignedOut;

	UPROPERTY(Transient)
	FString AccessToken;

	UPROPERTY(Transient)
	FString RefreshToken;

	UPROPERTY(Transient)
	FString SignedInDisplayName;

	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;
};

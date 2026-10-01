// File: Source/ProjectProject01/Public/ProjectProject01LoginWidget.h
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectProject01LoginWidget.generated.h"

UCLASS()
class PROJECTPROJECT01_API UProjectProject01LoginWidget final : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleLoginClicked();

	UFUNCTION()
	void HandleRegisterClicked();

	UFUNCTION()
	void HandleLoginResult(bool bSuccess, const FString& Message, const FString& DisplayName);

	UFUNCTION()
	void HandleRegistrationResult(bool bSuccess, const FString& Message, const FString& DisplayName);

	void BuildWidgetTree();
	void SetRequestControlsEnabled(bool bEnabled);
	void SetStatus(const FString& Message, bool bIsError);
	void EnterLobby();

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> AccountIdInput;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> PasswordInput;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> DisplayNameInput;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LoginButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> RegisterButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> StatusText;
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01LobbyWidget final : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleLogoutClicked();

	UFUNCTION()
	void HandleLogoutResult(bool bSuccess, const FString& Message, const FString& DisplayName);

	void BuildWidgetTree();
	void SetStatus(const FString& Message, bool bIsError);

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LogoutButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> LobbyStatusText;
};

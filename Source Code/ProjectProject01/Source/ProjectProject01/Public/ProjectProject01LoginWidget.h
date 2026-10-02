// File: Source/ProjectProject01/Public/ProjectProject01LoginWidget.h
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectProject01LobbySubsystem.h"
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
	void HandleCreateRoomClicked();

	UFUNCTION()
	void HandleRefreshRoomsClicked();

	UFUNCTION()
	void HandleJoinRoomClicked();

	UFUNCTION()
	void HandleReadyClicked();

	UFUNCTION()
	void HandleStartRoomClicked();

	UFUNCTION()
	void HandleLeaveRoomClicked();

	UFUNCTION()
	void HandleSendChatClicked();

	UFUNCTION()
	void HandleLogoutClicked();

	UFUNCTION()
	void HandleLogoutResult(bool bSuccess, const FString& Message, const FString& DisplayName);

	UFUNCTION()
	void HandleLobbyRequestResult(EProjectProject01LobbyOperation Operation, bool bSuccess, const FString& Message);

	UFUNCTION()
	void HandleRoomListChanged();

	UFUNCTION()
	void HandleCurrentRoomChanged();

	void BuildWidgetTree();
	void SetStatus(const FString& Message, bool bIsError);
	void RefreshLobbyState();
	void RefreshRoomListView();
	void RefreshCurrentRoomView();
	void BeginLogout();
	void TravelToStartedGame(const FProjectProject01RoomState& RoomState);

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> RoomNameInput;

	UPROPERTY(Transient)
	TObjectPtr<class UCheckBox> PublicRoomCheckBox;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> CreatePasswordInput;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> CreateRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> RefreshRoomsButton;

	UPROPERTY(Transient)
	TObjectPtr<class UComboBoxString> RoomListComboBox;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> DirectRoomIdInput;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> JoinPasswordInput;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> JoinRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> CurrentRoomText;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> MemberListText;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReadyButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> StartRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LeaveRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UMultiLineEditableTextBox> ChatLogText;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> ChatInput;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SendChatButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LogoutButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> LobbyStatusText;

	TMap<FString, FString> RoomIdByDisplayOption;
	FTimerHandle LobbyRefreshTimer;
	bool bLogoutAfterLeave = false;
	bool bTravelRequested = false;
};

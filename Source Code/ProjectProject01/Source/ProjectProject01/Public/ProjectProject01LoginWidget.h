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
	void HandleTransferHostClicked();

	UFUNCTION()
	void HandleDeleteRoomClicked();

	UFUNCTION()
	void HandleCopyJoinCodeClicked();

	UFUNCTION()
	void HandleSendChatClicked();

	UFUNCTION()
	void HandleLogoutClicked();

	UFUNCTION()
	void HandleOpenLeaderboardTestClicked();

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
	TObjectPtr<class UEditableTextBox> DirectRoomCodeInput;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> JoinPasswordInput;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> JoinRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> CurrentRoomText;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> CopyJoinCodeButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> MemberListText;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReadyButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> StartRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LeaveRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UComboBoxString> HostTransferComboBox;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> TransferHostButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> DeleteRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UMultiLineEditableTextBox> ChatLogText;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> ChatInput;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SendChatButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LogoutButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> OpenLeaderboardTestButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> LobbyStatusText;

	TMap<FString, FString> JoinCodeByDisplayOption;
	TMap<FString, FString> UserIdByTransferOption;
	FTimerHandle LobbyRefreshTimer;
	bool bLogoutAfterLeave = false;
	bool bTravelRequested = false;
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01LeaderboardWidget final : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleRefreshClicked();

	UFUNCTION()
	void HandleRoleSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleSortSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleSubmitRecordClicked();

	UFUNCTION()
	void HandleReturnToLobbyClicked();

	UFUNCTION()
	void HandleLeaderboardChanged();

	UFUNCTION()
	void HandleLeaderboardRequestResult(
		EProjectProject01LobbyOperation Operation,
		bool bSuccess,
		const FString& Message);

	void BuildWidgetTree();
	void RefreshLeaderboardView();
	void RefreshSortOptions();
	void SetStatus(const FString& Message, bool bIsError);
	EProjectProject01LeaderboardRole GetSelectedRole() const;
	EProjectProject01LeaderboardSort GetSelectedSort() const;

	UPROPERTY(Transient)
	TObjectPtr<class UComboBoxString> RoleComboBox;

	UPROPERTY(Transient)
	TObjectPtr<class UComboBoxString> SortComboBox;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> RefreshButton;

	UPROPERTY(Transient)
	TObjectPtr<class UMultiLineEditableTextBox> LeaderboardText;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> CaptureCountInput;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> FirstCaptureSecondsInput;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> AllCapturedSecondsInput;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> RescueCountInput;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> EscapeSecondsInput;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SubmitRecordButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReturnToLobbyButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> LeaderboardStatusText;

	FString PendingTestMatchId;
	bool bUpdatingSortOptions = false;
};

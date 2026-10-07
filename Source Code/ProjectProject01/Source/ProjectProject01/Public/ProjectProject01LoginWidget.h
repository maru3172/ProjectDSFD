// File: Source/ProjectProject01/Public/ProjectProject01LoginWidget.h
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GenericPlatform/GenericWindow.h"
#include "InputCoreTypes.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01LoginWidget.generated.h"

class UProjectProject01SettingsWidget;

UCLASS()
class PROJECTPROJECT01_API UProjectProject01TitleWidget final : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleSinglePlayerClicked();

	UFUNCTION()
	void HandleMultiplayerClicked();

	UFUNCTION()
	void HandleSettingsClicked();

	UFUNCTION()
	void HandleQuitClicked();

	void BuildWidgetTree();
	void SetMenuEnabled(bool bEnabled);

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SinglePlayerButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> MultiplayerButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SettingsButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> QuitButton;

	UPROPERTY(Transient)
	TObjectPtr<UProjectProject01SettingsWidget> SettingsWidget;

	bool bNavigationRequested = false;
};

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
	void HandleReturnToTitleClicked();

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
	TObjectPtr<class UButton> ReturnToTitleButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> StatusText;
};

/** 싱글 및 멀티플레이 중 F1로 여는 세션 메뉴입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01SessionMenuWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	void ConfigureForSession(bool bInMultiplayer);
	void CloseMenu();

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	enum class EPendingExitAction : uint8
	{
		None,
		ReturnToRoom,
		ReturnToTitle,
		QuitGame
	};

	UFUNCTION()
	void HandleContinueClicked();

	UFUNCTION()
	void HandleReturnToRoomClicked();

	UFUNCTION()
	void HandleReturnToTitleClicked();

	UFUNCTION()
	void HandleSettingsClicked();

	UFUNCTION()
	void HandleQuitGameClicked();

	UFUNCTION()
	void HandleLobbyRequestResult(EProjectProject01LobbyOperation Operation, bool bSuccess, const FString& Message);

	UFUNCTION()
	void HandleLogoutResult(bool bSuccess, const FString& Message, const FString& DisplayName);

	void BuildWidgetTree();
	void BeginAuthenticatedExit(EPendingExitAction ExitAction);
	void BeginExitFade(EPendingExitAction ExitAction, const FString& Message);
	void ExecutePendingExitAfterFade();
	void BeginLogout();
	void CompleteExit();
	void SetBusy(bool bInBusy, const FString& Message = FString(), bool bIsError = false);
	void RestoreGameInput();

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ContinueButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReturnToRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SettingsButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReturnToTitleButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> QuitGameButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<class UBorder> ExitFadeOverlay;

	UPROPERTY(Transient)
	TObjectPtr<UProjectProject01SettingsWidget> SettingsWidget;

	bool bMultiplayer = false;
	bool bBusy = false;
	bool bExitFadeActive = false;
	float ExitFadeElapsedSeconds = 0.0f;
	float ExitFadeDurationSeconds = 0.65f;
	EPendingExitAction PendingExitAction = EPendingExitAction::None;
};

/** 서버가 확정한 멀티플레이 경기 결과를 표시하고 리더보드 등록 또는 방 복귀를 수행합니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01MatchResultWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	void ConfigureResult(const FMultiplayTestMatchResult& InResult);
	void ConfigureSinglePlayerResult(bool bEscaped);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void HandleRegisterClicked();

	UFUNCTION()
	void HandleReturnToRoomClicked();

	UFUNCTION()
	void HandleLeaderboardClicked();

	UFUNCTION()
	void HandleReturnToTitleClicked();

	UFUNCTION()
	void HandleLogoutClicked();

	UFUNCTION()
	void HandleLobbyRequestResult(EProjectProject01LobbyOperation Operation, bool bSuccess, const FString& Message);

	UFUNCTION()
	void HandleLogoutResult(bool bSuccess, const FString& Message, const FString& DisplayName);

	void BuildWidgetTree();
	void RefreshResultText();
	void BeginReturnToRoom();
	void BeginAuthenticatedExit(bool bReturnToTitle);
	void BeginLogout();
	void CompleteAuthenticatedExit();
	void SetBusy(bool bInBusy, const FString& Message = FString(), bool bIsError = false);

	FMultiplayTestMatchResult Result;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> ResultText;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> RegisterButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReturnToRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LeaderboardButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReturnToTitleButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LogoutButton;

	UPROPERTY(Transient)
	TObjectPtr<class UBorder> FadeBackground;

	UPROPERTY(Transient)
	TObjectPtr<class UVerticalBox> ResultMenu;

	bool bConfigured = false;
	bool bSinglePlayer = false;
	bool bBusy = false;
	bool bReturnAfterRegistration = false;
	bool bOpenLeaderboardAfterReturn = false;
	bool bPendingAuthenticatedExit = false;
	bool bPendingReturnToTitle = false;
	float FadeElapsedSeconds = 0.0f;
	float FadeDurationSeconds = 1.5f;
};

/** 타이틀과 F1 메뉴에서 공통으로 사용하는 로컬 환경설정 화면입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01SettingsWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	void ConfigureReturnWidget(UUserWidget* InReturnWidget);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	enum class EBindingTarget : uint8
	{
		None,
		Forward,
		Backward,
		Left,
		Right,
		Sprint
	};

	UFUNCTION() void HandleMasterVolumeChanged(float Value);
	UFUNCTION() void HandleSFXVolumeChanged(float Value);
	UFUNCTION() void HandleMusicVolumeChanged(float Value);
	UFUNCTION() void HandleUIVolumeChanged(float Value);
	UFUNCTION() void HandleFrameRateChanged(float Value);
	UFUNCTION() void HandleResolutionScaleChanged(float Value);
	UFUNCTION() void HandleBrightnessChanged(float Value);
	UFUNCTION() void HandleSensitivityChanged(float Value);
	UFUNCTION() void HandleMuteAllChanged(bool bChecked);
	UFUNCTION() void HandleMuteWhenUnfocusedChanged(bool bChecked);
	UFUNCTION() void HandleVSyncChanged(bool bChecked);
	UFUNCTION() void HandleMotionBlurChanged(bool bChecked);
	UFUNCTION() void HandleWindowModeChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleResolutionChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleOverallQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleAAQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleShadowQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleTextureQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleEffectsQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandlePostProcessQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleVFXIntensityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleForwardBindingClicked();
	UFUNCTION() void HandleBackwardBindingClicked();
	UFUNCTION() void HandleLeftBindingClicked();
	UFUNCTION() void HandleRightBindingClicked();
	UFUNCTION() void HandleSprintBindingClicked();
	UFUNCTION() void HandleApplyClicked();
	UFUNCTION() void HandleCancelClicked();
	UFUNCTION() void HandleDefaultsClicked();
	UFUNCTION() void HandleConfirmVideoClicked();
	UFUNCTION() void HandleRevertVideoClicked();

	void BuildWidgetTree();
	void LoadPendingFromSettings();
	void SetPendingDefaults();
	void RefreshAllControls();
	void RefreshValueLabels();
	void RefreshBindingLabels();
	void BeginBindingCapture(EBindingTarget Target);
	bool TryAssignBinding(FKey Key);
	void ApplyPendingSettings();
	void ConfirmPendingVideoMode();
	void RevertPendingVideoMode();
	void CloseToReturnWidget();
	void SetStatus(const FString& Message, bool bIsError = false);
	void SetQualityCombo(class UComboBoxString* Combo, int32 Quality);
	int32 QualityFromString(const FString& Item) const;
	FString QualityToString(int32 Quality) const;
	bool ParseResolution(const FString& Item, FIntPoint& OutResolution) const;
	void RefreshLocalPlayerInput() const;

	UPROPERTY(Transient) TObjectPtr<UUserWidget> ReturnWidget;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> VideoConfirmText;
	UPROPERTY(Transient) TObjectPtr<class UVerticalBox> VideoConfirmPanel;
	UPROPERTY(Transient) TObjectPtr<class USlider> MasterVolumeSlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> SFXVolumeSlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> MusicVolumeSlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> UIVolumeSlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> FrameRateSlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> ResolutionScaleSlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> BrightnessSlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> SensitivitySlider;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> MasterVolumeValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> SFXVolumeValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> MusicVolumeValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> UIVolumeValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> FrameRateValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> ResolutionScaleValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> BrightnessValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> SensitivityValueText;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> MuteAllCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> MuteWhenUnfocusedCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> VSyncCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> MotionBlurCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> WindowModeComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> ResolutionComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> OverallQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> AAQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> ShadowQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> TextureQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> EffectsQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> PostProcessQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> VFXIntensityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UButton> ForwardKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> BackwardKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> LeftKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> RightKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> SprintKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> ApplyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> CancelButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> DefaultsButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> ConfirmVideoButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> RevertVideoButton;

	float PendingMasterVolume = 1.0f;
	float PendingSFXVolume = 1.0f;
	float PendingMusicVolume = 1.0f;
	float PendingUIVolume = 1.0f;
	float PendingFrameRate = 120.0f;
	float PendingResolutionScale = 100.0f;
	float PendingBrightness = 2.2f;
	float PendingSensitivity = 1.0f;
	bool bPendingMuteAll = false;
	bool bPendingMuteWhenUnfocused = true;
	bool bPendingVSync = false;
	bool bPendingMotionBlur = true;
	EWindowMode::Type PendingWindowMode = EWindowMode::WindowedFullscreen;
	FIntPoint PendingResolution = FIntPoint(1920, 1080);
	int32 PendingOverallQuality = 3;
	int32 PendingAAQuality = 3;
	int32 PendingShadowQuality = 3;
	int32 PendingTextureQuality = 3;
	int32 PendingEffectsQuality = 3;
	int32 PendingPostProcessQuality = 3;
	EProjectProject01VFXIntensity PendingVFXIntensity = EProjectProject01VFXIntensity::Standard;
	FKey PendingForwardKey = EKeys::W;
	FKey PendingBackwardKey = EKeys::S;
	FKey PendingLeftKey = EKeys::A;
	FKey PendingRightKey = EKeys::D;
	FKey PendingSprintKey = EKeys::LeftShift;
	EBindingTarget BindingTarget = EBindingTarget::None;
	bool bRefreshingControls = false;
	bool bAwaitingVideoConfirmation = false;
	float VideoConfirmationSecondsRemaining = 0.0f;
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
	void HandleOpenCreateRoomClicked();

	UFUNCTION()
	void HandleCancelCreateRoomClicked();

	UFUNCTION()
	void HandleCreateRoomClicked();

	UFUNCTION()
	void HandleRefreshRoomsClicked();

	UFUNCTION()
	void HandleRoomSearchChanged(const FText& SearchText);

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
	void ShowLobbyView(int32 ViewIndex);

	UPROPERTY(Transient)
	TObjectPtr<class UWidgetSwitcher> LobbyViewSwitcher;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> OpenCreateRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> CancelCreateRoomButton;

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
	TObjectPtr<class UEditableTextBox> RoomSearchInput;

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
	bool bShowingCreateRoom = false;
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

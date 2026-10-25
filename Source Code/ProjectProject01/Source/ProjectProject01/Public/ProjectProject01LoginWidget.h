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
class UProjectProject01SessionMenuWidget;
class UProjectProject01SurveyWidget;
class UProjectProject01LobbyWidget;

/** 체크포인트나 목표 갱신에 재사용할 수 있는 좌측 상단 일시 알림입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01TransientNoticeWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowNotice(const FString& Message, float HoldSeconds = 1.5f, float FadeSeconds = 1.5f);

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildWidgetTree();

	UPROPERTY(Transient)
	TObjectPtr<class UBorder> NoticeBackground;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> NoticeText;

	float ElapsedSeconds = 0.0f;
	float HoldDurationSeconds = 1.5f;
	float FadeDurationSeconds = 1.5f;
};

/** 역할별 목표와 조작키를 보여주는 전체 화면 클릭 종료 가이드입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01GameGuideWidget final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectReviewRoleGuideKeysTest;

public:
	void Configure(bool bInMannequinRole, bool bInMultiplayer,
		bool bInReturnToSessionMenu, UProjectProject01SessionMenuWidget* InReturnMenu = nullptr);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION()
	void HandleScreenClicked();

	void BuildWidgetTree();
	void RefreshGuideText();
	void CloseGuide();

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ScreenButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> GoalText;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> ControlsText;

	UPROPERTY(Transient)
	TObjectPtr<UProjectProject01SessionMenuWidget> ReturnMenu;

	bool bMannequinRole = false;
	bool bMultiplayer = true;
	bool bReturnToSessionMenu = false;
};

/** 경기 중 음성 채널 연결 및 마이크 송신 상태를 우측 상단에 표시합니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01VoiceStatusWidget final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectVoiceParticipantWidgetTest;

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildWidgetTree();
	void RefreshVoiceStatus();

	UPROPERTY(Transient)
	TObjectPtr<class UBorder> StatusBackground;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> VoiceStatusText;
	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> TeammateVoiceText;
	float RefreshSeconds=0;
	// Last applied values: the HUD refreshes at 10 Hz but only re-sets text/colours when they change (no relayout).
	FString LastVoiceStatusKey;
	FString LastTeammateVoiceText;
};

/** 마네킹 플레이어에게만 각 번호의 실시간 행동 상태를 표시합니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01MannequinStatusWidget final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectMannequinCameraTest;

public:
	float GetContentBottom();
protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildWidgetTree();
	void RefreshMannequinStates();

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> StatusText;

	float RefreshAccumulatorSeconds = 0.0f;
	// Last applied status source string: the 5 Hz refresh re-sets the text only when it changes.
	FString LastStatusSource;
	void SetStatusSource(const FString& Source);
};

/** 멀티플레이 경기 중에도 서버 공지와 종료 예고를 보여주는 가벼운 상단 배너입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01ServiceNoticeWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetNotice(const FString& Message, bool bIsMaintenance);

protected:
	virtual bool Initialize() override;

private:
	void BuildWidgetTree();

	UPROPERTY(Transient)
	TObjectPtr<class UBorder> NoticeBackground;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> NoticeText;
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01SurveyOptionBinding final : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient) TObjectPtr<UProjectProject01SurveyWidget> Owner;
	int32 QuestionIndex = 0;
	int32 OptionIndex = 0;
	UFUNCTION() void HandleChanged(bool bChecked);
};

/** 로그인한 플레이어가 응답할 수 있는 설문입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01SurveyWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	void ConfigureSurvey(const FProjectProject01SurveyDefinition& InSurvey, UUserWidget* InReturnWidget,
		bool bInReturnToTitleAfterSurvey = false);
	void ShowSurveyNotice(const FString& Message, bool bSubmissionSucceeded);
	bool ShouldReturnToTitleAfterSurvey() const { return bReturnToTitleAfterSurvey; }
	void HandleOptionChanged(int32 QuestionIndex, int32 OptionIndex, bool bChecked);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION() void HandleSubmitClicked();
	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandleAcknowledgementClicked();
	UFUNCTION() void HandleSurveyLogoutCompleted(bool bSuccess, const FString& Message, const FString& DisplayName);
	void CloseSurvey();
	void FinishSurveyLogout();
	void BuildWidgetTree();
	void SetBusy(bool bInBusy, const FString& Message = FString(), bool bIsError = false);

	UPROPERTY(Transient) TObjectPtr<class UTextBlock> SurveyTitleText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> SurveyDescriptionText;
	UPROPERTY(Transient) TObjectPtr<class UVerticalBox> QuestionPanel;
	UPROPERTY(Transient) TArray<TObjectPtr<class UCheckBox>> ChoiceInputs;
	UPROPERTY(Transient) TArray<TObjectPtr<UProjectProject01SurveyOptionBinding>> OptionBindings;
	TArray<TArray<int32>> SelectedOptions;
	bool bUpdatingChoices = false;
	UPROPERTY(Transient) TArray<TObjectPtr<class UMultiLineEditableTextBox>> TextInputs;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<class UButton> SubmitButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> BackButton;
	UPROPERTY(Transient) TObjectPtr<class UBorder> AcknowledgementPanel;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> AcknowledgementText;
	UPROPERTY(Transient) TObjectPtr<class UButton> AcknowledgementButton;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ReturnWidget;
	FProjectProject01SurveyDefinition Survey;
	bool bBusy = false;
	bool bSubmitted = false;
	bool bAcknowledgementVisible = false;
	bool bReturnToTitleAfterSurvey = false;
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01TitleWidget final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectCheckpointSlotPreviewWidgetTest;
	friend class FProjectDFSDTitleSceneRenderTest;

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	enum class ESaveSlotConfirmationAction : uint8
	{
		None,
		StartNewGame,
		Delete
	};

	enum class ESinglePlayerLaunchAction : uint8
	{
		None,
		ContinueRecent,
		PlaySlot,
		NewGameSlot
	};

	UFUNCTION()
	void HandleSinglePlayerClicked();

	UFUNCTION()
	void HandleContinueSinglePlayerClicked();

	UFUNCTION()
	void HandleSaveSlot1PlayClicked();
	UFUNCTION()
	void HandleSaveSlot2PlayClicked();
	UFUNCTION()
	void HandleSaveSlot3PlayClicked();
	UFUNCTION()
	void HandleSaveSlot1NewGameClicked();
	UFUNCTION()
	void HandleSaveSlot2NewGameClicked();
	UFUNCTION()
	void HandleSaveSlot3NewGameClicked();
	UFUNCTION()
	void HandleSaveSlot1DeleteClicked();
	UFUNCTION()
	void HandleSaveSlot2DeleteClicked();
	UFUNCTION()
	void HandleSaveSlot3DeleteClicked();
	UFUNCTION()
	void HandleSaveSlotsBackClicked();
	UFUNCTION()
	void HandleSaveSlotConfirmationYesClicked();
	UFUNCTION()
	void HandleSaveSlotConfirmationNoClicked();

	UFUNCTION()
	void HandleMultiplayerClicked();
	UFUNCTION()
	void HandleSurveyClicked();
	void NavigateToMultiplayer(bool bForSurvey);

	UFUNCTION()
	void HandleSettingsClicked();

	UFUNCTION()
	void HandleQuitClicked();

	UFUNCTION()
	void HandleServiceStatusChanged(bool bMaintenanceEnabled, const FString& Announcement,
		const FString& ShutdownAtUtc, const FString& Message);

	void BuildWidgetTree();
	void SetMenuEnabled(bool bEnabled);
	void PollMultiplayerService();
	void RefreshSaveSlotPage();
	void PlaySaveSlot(int32 SlotIndex);
	void RequestStartNewGameInSaveSlot(int32 SlotIndex);
	void RequestDeleteSaveSlot(int32 SlotIndex);
	void StartNewGameInSaveSlot(int32 SlotIndex);
	void DeleteSaveSlot(int32 SlotIndex);
	void ShowSaveSlotConfirmation(ESaveSlotConfirmationAction Action, int32 SlotIndex);
	void HideSaveSlotConfirmation();
	void BeginSinglePlayerNavigation();
	void QueueSinglePlayerLaunch(ESinglePlayerLaunchAction Action, int32 SlotIndex = INDEX_NONE);
	void CompleteSinglePlayerLaunch();
	void CancelSinglePlayerLaunch(const FString& ErrorMessage);

	UPROPERTY(Transient)
	TObjectPtr<class UWidgetSwitcher> TitlePageSwitcher;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SinglePlayerButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ContinueSinglePlayerButton;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UTextBlock>> SaveSlotStatusTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UImage>> SaveSlotPreviewImages;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UButton>> SaveSlotPlayButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UButton>> SaveSlotNewGameButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UButton>> SaveSlotDeleteButtons;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SaveSlotsBackButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> SaveSlotsMessageText;

	UPROPERTY(Transient)
	TObjectPtr<class UBackgroundBlur> SaveSlotConfirmationBlur;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> SaveSlotConfirmationText;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SaveSlotConfirmationYesButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SaveSlotConfirmationNoButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> MultiplayerButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SettingsButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SurveyButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> QuitButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<class UBorder> SinglePlayerFadeOverlay;

	UPROPERTY(Transient)
	TObjectPtr<UProjectProject01SettingsWidget> SettingsWidget;

	bool bNavigationRequested = false;
	bool bMultiplayerServiceAvailable = false;
	bool bSinglePlayerFadeActive = false;
	float SinglePlayerFadeElapsedSeconds = 0.0f;
	float SinglePlayerFadeDurationSeconds = 1.5f;
	ESaveSlotConfirmationAction PendingSaveSlotConfirmationAction = ESaveSlotConfirmationAction::None;
	int32 PendingSaveSlotConfirmationSlot = INDEX_NONE;
	ESinglePlayerLaunchAction PendingSinglePlayerLaunchAction = ESinglePlayerLaunchAction::None;
	int32 PendingSinglePlayerLaunchSlot = INDEX_NONE;
	FTimerHandle ServiceStatusTimer;
	FTimerHandle SinglePlayerLaunchTimer;
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01LoginWidget final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectLoginRegistrationTest;

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleLoginClicked();

	UFUNCTION()
	void HandleRegisterClicked();
	UFUNCTION() void HandleOpenRegistrationClicked();
	UFUNCTION() void HandleReturnToLoginClicked();

	UFUNCTION()
	void HandleReturnToTitleClicked();

	UFUNCTION()
	void HandleLoginResult(bool bSuccess, const FString& Message, const FString& DisplayName);

	UFUNCTION()
	void HandleRegistrationResult(bool bSuccess, const FString& Message, const FString& DisplayName);

	UFUNCTION()
	void HandleServiceStatusChanged(bool bMaintenanceEnabled, const FString& Announcement,
		const FString& ShutdownAtUtc, const FString& Message);

	void BuildWidgetTree();
	void PollMultiplayerService();
	void SetRequestControlsEnabled(bool bEnabled);
	void SetStatus(const FString& Message, bool bIsError);
	void EnterLobby();
	void ShowRegistrationPage(bool bShow);
	UPROPERTY(Transient) TObjectPtr<class UWidgetSwitcher> AuthenticationPages;
	UPROPERTY(Transient) TObjectPtr<class UEditableTextBox> RegistrationAccountIdInput;
	UPROPERTY(Transient) TObjectPtr<class UEditableTextBox> RegistrationPasswordInput;
	UPROPERTY(Transient) TObjectPtr<class UButton> RegistrationSubmitButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> ReturnToLoginButton;
	bool bAuthenticationRequestPending = false;
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

	bool bMultiplayerServiceAvailable = false;
	FTimerHandle ServiceStatusTimer;
};

/** 싱글 및 멀티플레이 중 ESC로 여는 세션 메뉴입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01SessionMenuWidget final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectCheckpointSlotPreviewWidgetTest;

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
	void HandleSaveCheckpointClicked();

	UFUNCTION()
	void HandleManualSaveSlot1Clicked();

	UFUNCTION()
	void HandleManualSaveSlot2Clicked();

	UFUNCTION()
	void HandleManualSaveSlot3Clicked();

	UFUNCTION()
	void HandleManualSaveCancelClicked();

	UFUNCTION()
	void HandleLoadCheckpointClicked();

	UFUNCTION()
	void HandleLoadOtherCheckpointClicked();

	UFUNCTION()
	void HandleLoadSaveSlot1Clicked();

	UFUNCTION()
	void HandleLoadSaveSlot2Clicked();

	UFUNCTION()
	void HandleLoadSaveSlot3Clicked();

	UFUNCTION()
	void HandleLoadSaveCancelClicked();

	UFUNCTION()
	void HandleSaveOperationCompleted(bool bSucceeded, const FString& Message);

	UFUNCTION()
	void HandleReturnToRoomClicked();

	UFUNCTION()
	void HandleReturnToTitleClicked();

	UFUNCTION()
	void HandleSettingsClicked();

	UFUNCTION()
	void HandleGameRulesClicked();

	UFUNCTION()
	void HandleSurveyClicked();

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
	void ShowManualSaveSlots(bool bShow);
	void RefreshManualSaveSlots();
	void SaveCheckpointToSlot(int32 SlotIndex);
	void ShowLoadSaveSlots(bool bShow);
	void RefreshLoadSaveSlots();
	void LoadCheckpointFromSlot(int32 SlotIndex);
	void RefreshActiveSurvey();

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ContinueButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SaveCheckpointButton;

	UPROPERTY(Transient)
	TObjectPtr<class UWidgetSwitcher> SessionPageSwitcher;

	UPROPERTY(Transient)
	TObjectPtr<class UVerticalBox> ManualSaveSlotPanel;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UButton>> ManualSaveSlotButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UTextBlock>> ManualSaveSlotStatusTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UImage>> ManualSaveSlotPreviewImages;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ManualSaveCancelButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LoadCheckpointButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LoadOtherCheckpointButton;

	UPROPERTY(Transient)
	TObjectPtr<class UVerticalBox> LoadSaveSlotPanel;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UButton>> LoadSaveSlotButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UTextBlock>> LoadSaveSlotStatusTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UImage>> LoadSaveSlotPreviewImages;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LoadSaveCancelButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReturnToRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SettingsButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> GameRulesButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SurveyButton;

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

	UPROPERTY(Transient)
	TObjectPtr<UProjectProject01GameGuideWidget> GameGuideWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectProject01SurveyWidget> SurveyWidget;

	bool bMultiplayer = false;
	bool bActiveSurveyAvailable = false;
	FProjectProject01SurveyDefinition ActiveSurvey;
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
	friend class FProjectMatchTimelineWidgetTest;
	friend class FProjectResultBlackoutTest;
	void InitializeResultPresentation();
	UFUNCTION()
	void HandleTimelineClicked();
	UPROPERTY(Transient) TObjectPtr<class UButton> TimelineButton;
	UPROPERTY(Transient) TObjectPtr<class USizeBox> TimelineFrame;
	UPROPERTY(Transient) TObjectPtr<class UVerticalBox> TimelineRows;
	bool bShowingTimeline = false;
	UFUNCTION()
	void HandleRegisterClicked();

	UFUNCTION()
	void HandleReturnToRoomClicked();

	UFUNCTION()
	void HandleLeaderboardClicked();

	UFUNCTION()
	void HandleReturnToTitleClicked();

	UFUNCTION()
	void HandleLoadLastCheckpointClicked();

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
	TObjectPtr<class UButton> LoadLastCheckpointButton;

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

/** 타이틀과 ESC 메뉴에서 공통으로 사용하는 로컬 환경설정 화면입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01SettingsWidget final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectReviewAsyncStateTest;

public:
	void ConfigureReturnWidget(UUserWidget* InReturnWidget);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION() void HandleVoiceSettingsClicked();
	UPROPERTY(Transient) TObjectPtr<class UButton> VoiceSettingsButton;
	UPROPERTY(Transient) TObjectPtr<class UProjectProject01VoiceSettingsWidget> VoiceSettingsWidget;
	enum class EBindingTarget : uint8
	{
		None,
		Forward,
		Backward,
		Left,
		Right,
		Sprint,
		VoicePushToTalk,
		VoiceToggle,
		HelpPing,
		DangerPing,
		LocationPing
	};

	UFUNCTION() void HandleMasterVolumeChanged(float Value);
	UFUNCTION() void HandleSFXVolumeChanged(float Value);
	UFUNCTION() void HandleMusicVolumeChanged(float Value);
	UFUNCTION() void HandleUIVolumeChanged(float Value);
	UFUNCTION() void HandleFrameRateChanged(float Value);
	UFUNCTION() void HandleResolutionScaleChanged(float Value);
	UFUNCTION() void HandleBrightnessChanged(float Value);
	UFUNCTION() void HandleSensitivityChanged(float Value);
	UFUNCTION() void HandleColorVisionSeverityChanged(float Value);
	UFUNCTION() void HandleScreenFlashChanged(float Value);
	UFUNCTION() void HandleScreenDistortionChanged(float Value);
	UFUNCTION() void HandleScreenShakeChanged(float Value);
	UFUNCTION() void HandleUIReadableScaleChanged(float Value);
	UFUNCTION() void HandleMuteAllChanged(bool bChecked);
	UFUNCTION() void HandleMuteWhenUnfocusedChanged(bool bChecked);
	UFUNCTION() void HandleVSyncChanged(bool bChecked);
	UFUNCTION() void HandleMotionBlurChanged(bool bChecked);
	UFUNCTION() void HandleCameraShakeChanged(bool bChecked);
	UFUNCTION() void HandleSubtitlesChanged(bool bChecked);
	UFUNCTION() void HandleEnhancedVisualCuesChanged(bool bChecked);
	UFUNCTION() void HandleWindowModeChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleResolutionChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleOverallQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleAAQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleShadowQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleTextureQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleEffectsQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandlePostProcessQualityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleVFXIntensityChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleColorVisionModeChanged(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleForwardBindingClicked();
	UFUNCTION() void HandleBackwardBindingClicked();
	UFUNCTION() void HandleLeftBindingClicked();
	UFUNCTION() void HandleRightBindingClicked();
	UFUNCTION() void HandleSprintBindingClicked();
	UFUNCTION() void HandleVoicePushToTalkBindingClicked();
	UFUNCTION() void HandleVoiceToggleBindingClicked();
	UFUNCTION() void HandleHelpPingBindingClicked();
	UFUNCTION() void HandleDangerPingBindingClicked();
	UFUNCTION() void HandleLocationPingBindingClicked();
	UFUNCTION() void HandleApplyClicked();
	UFUNCTION() void HandleCancelClicked();
	UFUNCTION() void HandleDefaultsClicked();
	UFUNCTION() void HandleRevertVideoClicked();
	UFUNCTION() void HandleServerSettingsTabClicked();
	UFUNCTION() void HandleAudioSettingsTabClicked();
	UFUNCTION() void HandleVideoSettingsTabClicked();
	UFUNCTION() void HandleAccessibilitySettingsTabClicked();
	UFUNCTION() void HandleControlsSettingsTabClicked();

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
	void RestoreSettingsInputFocus();
	void SetActiveSettingsPage(int32 PageIndex);

	UPROPERTY(Transient) TObjectPtr<UUserWidget> ReturnWidget;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<class UWidgetSwitcher> SettingsPageSwitcher;
	UPROPERTY(Transient) TObjectPtr<class UButton> ServerSettingsTabButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> AudioSettingsTabButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> VideoSettingsTabButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> AccessibilitySettingsTabButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> ControlsSettingsTabButton;
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
	UPROPERTY(Transient) TObjectPtr<class USlider> ColorVisionSeveritySlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> ScreenFlashSlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> ScreenDistortionSlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> ScreenShakeSlider;
	UPROPERTY(Transient) TObjectPtr<class USlider> UIReadableScaleSlider;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> MasterVolumeValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> SFXVolumeValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> MusicVolumeValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> UIVolumeValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> FrameRateValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> ResolutionScaleValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> BrightnessValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> SensitivityValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> ColorVisionSeverityValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> ScreenFlashValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> ScreenDistortionValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> ScreenShakeValueText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> UIReadableScaleValueText;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> MuteAllCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> MuteWhenUnfocusedCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> VSyncCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> MotionBlurCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> CameraShakeCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> SubtitlesCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> EnhancedVisualCuesCheckBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> WindowModeComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> ResolutionComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> OverallQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> AAQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> ShadowQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> TextureQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> EffectsQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> PostProcessQualityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> VFXIntensityComboBox;
	UPROPERTY(Transient) TObjectPtr<class UComboBoxString> ColorVisionModeComboBox;
	UPROPERTY(Transient) TObjectPtr<class UEditableTextBox> DirectServerAddressInput;
	UPROPERTY(Transient) TObjectPtr<class UButton> ForwardKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> BackwardKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> LeftKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> RightKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> SprintKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> VoicePushToTalkKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> VoiceToggleKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> HelpPingKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> DangerPingKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> LocationPingKeyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> ApplyButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> CancelButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> DefaultsButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> RevertVideoButton;

	float PendingMasterVolume = 1.0f;
	float PendingSFXVolume = 1.0f;
	float PendingMusicVolume = 1.0f;
	float PendingUIVolume = 1.0f;
	float PendingFrameRate = 120.0f;
	float PendingResolutionScale = 100.0f;
	float PendingBrightness = 2.2f;
	float PendingSensitivity = 1.0f;
	float PendingColorVisionSeverity = 5.0f;
	float PendingScreenFlashScale = 1.0f;
	float PendingScreenDistortionScale = 1.0f;
	float PendingScreenShakeScale = 1.0f;
	float PendingUIReadableScale = 1.0f;
	bool bPendingMuteAll = false;
	bool bPendingMuteWhenUnfocused = true;
	bool bPendingVSync = false;
	bool bPendingMotionBlur = true;
	bool bPendingCameraShake = true;
	bool bPendingSubtitles = true;
	bool bPendingEnhancedVisualCues = true;
	FString PendingDirectServerAddress;
	EWindowMode::Type PendingWindowMode = EWindowMode::WindowedFullscreen;
	FIntPoint PendingResolution = FIntPoint(1920, 1080);
	int32 PendingOverallQuality = 3;
	int32 PendingAAQuality = 3;
	int32 PendingShadowQuality = 3;
	int32 PendingTextureQuality = 3;
	int32 PendingEffectsQuality = 3;
	int32 PendingPostProcessQuality = 3;
	EProjectProject01VFXIntensity PendingVFXIntensity = EProjectProject01VFXIntensity::Standard;
	EProjectProject01ColorVisionMode PendingColorVisionMode = EProjectProject01ColorVisionMode::Normal;
	FKey PendingForwardKey = EKeys::W;
	FKey PendingBackwardKey = EKeys::S;
	FKey PendingLeftKey = EKeys::A;
	FKey PendingRightKey = EKeys::D;
	FKey PendingSprintKey = EKeys::LeftShift;
	FKey PendingVoicePushToTalkKey = EKeys::F2;
	FKey PendingVoiceToggleKey = EKeys::U;
	FKey PendingHelpPingKey = EKeys::E;
	FKey PendingDangerPingKey = EKeys::R;
	FKey PendingLocationPingKey = EKeys::T;
	EBindingTarget BindingTarget = EBindingTarget::None;
	bool bRefreshingControls = false;
	bool bAwaitingVideoConfirmation = false;
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01RoomProfileBinding final : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UProjectProject01LobbyWidget> Owner;
    FString UserId;
    UFUNCTION() FEventReply HandleMouseButtonDown(FGeometry Geometry,const FPointerEvent& MouseEvent);
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01LobbySelectionBinding final : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UProjectProject01LobbyWidget> Owner;
    UPROPERTY(Transient) TObjectPtr<class UButton> Button;
    UPROPERTY(Transient) TObjectPtr<class UTextBlock> Label;
    FString Key, Caption;
    bool bHost=false;
    UFUNCTION() void HandleClicked();
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01LobbyWidget final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectLobbySessionPresentationTest;
	friend class FProjectReviewAsyncStateTest;
	friend class FProjectLobbyReadabilityTest;
    friend class UProjectProject01LobbySelectionBinding;

public:
	void PrepareTitleSurveyLogout();
    void OpenRoomMemberProfile(const FString& UserId);
    bool ShowRoomMemberMenu(const FString& UserId,const FPointerEvent& MouseEvent);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    UFUNCTION() void HandleOpenMatchClicked();
    UFUNCTION() void HandleBotMatchClicked();
    UFUNCTION() void HandleBotMatchCreateClicked();
    /** Difficulty hover help: per-option tooltip in the list, and the selected level on the closed box. */
    UFUNCTION() class UWidget* GenerateBotDifficultyItem(FString Item);
    UFUNCTION() void HandleBotDifficultyChanged(FString Item, ESelectInfo::Type SelectionType);
    UPROPERTY(Transient) TObjectPtr<class UComboBoxString> BotMatchRole;
    UPROPERTY(Transient) TObjectPtr<class UComboBoxString> RandomMatchRole;
    UPROPERTY(Transient) TObjectPtr<class UComboBoxString> RoomMatchRole;
    UPROPERTY(Transient) TObjectPtr<class UHorizontalBox> RoomRoleRow;
    UFUNCTION() void HandleMatchRoleChanged(FString Item, ESelectInfo::Type SelectionType);
    void RefreshRoleSelection();
    bool bRoleWritePending=false;
    UPROPERTY(Transient) TObjectPtr<class UComboBoxString> BotMatchDifficulty;
    UPROPERTY(Transient) TObjectPtr<class UButton> BotMatchCreateButton;
    bool bShowingBotMatch=false;
    bool bAutoStartBotMatch=false; // One-click bot match: start the room as soon as it is created.
    UFUNCTION() void HandleAddRoomBot();
    UFUNCTION() void HandleRemoveRoomBot();
    UFUNCTION() void HandleChangeRoomBotDifficulty();
    UPROPERTY(Transient) TObjectPtr<class UVerticalBox> RoomBotPanel;
    UPROPERTY(Transient) TObjectPtr<class UBorder> RoomToolsFrame;
    UPROPERTY(Transient) TObjectPtr<class UButton> RoomToolsToggleButton;
    bool bRoomToolsExpanded=false;
    UFUNCTION() void HandleRoomToolsToggle();
    UPROPERTY(Transient) TObjectPtr<class UWidgetSwitcher> RoomToolsSwitcher;
    UPROPERTY(Transient) TObjectPtr<class UButton> RoomBotsTabButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> RoomMembersTabButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> RoomSessionTabButton;
    UFUNCTION() void HandleRoomBotsTabClicked();
    UFUNCTION() void HandleRoomMembersTabClicked();
    UFUNCTION() void HandleRoomSessionTabClicked();
    UPROPERTY(Transient) TObjectPtr<class UComboBoxString> RoomBotSelection;
    UPROPERTY(Transient) TObjectPtr<class UComboBoxString> RoomBotDifficulty;
    UPROPERTY(Transient) TObjectPtr<class UButton> AddRoomBotButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> RemoveRoomBotButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> ChangeRoomBotButton;
    TArray<FString> RoomBotIds,RoomBotLabels;
    UPROPERTY() TObjectPtr<class UButton> SwitchObserverButton;
    UPROPERTY() TObjectPtr<class UVerticalBox> ParticipantManagement;
    UFUNCTION() void HandleSwitchObserver();
    UFUNCTION() void HandleMoveToObserver();
    UFUNCTION() void HandleMoveToPlayer();
    UFUNCTION() void HandleKickParticipant();
    UFUNCTION() void HandleTutorialClicked();
    UFUNCTION() void HandleSurvivorTutorialClicked();
    UFUNCTION() void HandleMannequinTutorialClicked();
    UFUNCTION() void HandleSurvivorPracticeClicked();
    UFUNCTION() void HandleMannequinPracticeClicked();
    void StartTutorial(bool bMannequin,bool bFreePractice=false);
    bool bShowingTutorial=false;
    FTimerHandle TutorialTravelTimer;
    UFUNCTION() void HandleRandomMatchClicked();
    UFUNCTION() void HandleMatchStartClicked();
    UFUNCTION() void HandleMatchCancelClicked();
    UFUNCTION() void HandleLobbyModesClicked();
    void RefreshMatchmakingView();
    void UpdateStartCountdownText();
    bool bPartyCreationRequested=false;
    bool bShowingMatchmaking=false,bShowingOpenMatching=false;
    int32 DisplayedMatchmakingElapsedSeconds=INDEX_NONE;
    UPROPERTY(Transient) TObjectPtr<class UButton> MatchStartButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> MatchCancelButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> MatchBackButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> MatchLogoutButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> ModesLogoutButton;
    UPROPERTY(Transient) TObjectPtr<class UTextBlock> MatchStatusText;
    UFUNCTION() void HandleLobbySettingsClicked();
    UPROPERTY(Transient) TObjectPtr<class UButton> LobbySettingsButton;
    UPROPERTY(Transient) TObjectPtr<UProjectProject01SettingsWidget> LobbySettingsWidget;
    UPROPERTY(Transient) TObjectPtr<class UProjectProject01SocialWidget> SocialOverlay;
    UPROPERTY(Transient) TObjectPtr<class UVerticalBox> MemberProfilePanel;
    UPROPERTY(Transient) TObjectPtr<class UVerticalBox> ObserverProfilePanel;
    UPROPERTY(Transient) TArray<TObjectPtr<UProjectProject01RoomProfileBinding>> MemberProfileBindings;
    TArray<FString> MemberProfileRowKeys;
    void PopulateRoomMemberProfiles(const TArray<FProjectProject01RoomMember>& Members,bool bRevealRoles);
	UFUNCTION() void HandleLobbySurveyClicked();
	void RefreshLobbySurvey();
	void OpenLobbySurvey(bool bFromTitle = false);
	UPROPERTY(Transient) TObjectPtr<class UButton> LobbySurveyButton;
	UPROPERTY(Transient) TObjectPtr<UProjectProject01SurveyWidget> LobbySurveyWidget;
	FProjectProject01SurveyDefinition LobbySurvey;
	int32 LobbySurveyPollCounter = 0;
	bool bLobbySurveyRequestPending = false;
	bool bOpenTitleSurveyOnRefresh = false;

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
	void PopulatePublicRooms(const TArray<FProjectProject01RoomSummary>& Rooms);
	void UpdatePublicRoomSelection();
    void SelectLobbyEntry(const FString& Key,bool bHost);
    void PopulateLobbyChoices(class UVerticalBox* Panel,const TArray<TPair<FString,FString>>& Entries,TArray<TObjectPtr<UProjectProject01LobbySelectionBinding>>& Bindings,bool bHost);
    void UpdateLobbyChoiceHighlight(const TArray<TObjectPtr<UProjectProject01LobbySelectionBinding>>& Bindings,const FString& Selected);

	UFUNCTION()
	void HandleJoinRoomClicked();
    UFUNCTION() void HandleOpenCodeJoinClicked();
    UFUNCTION() void HandleCloseCodeJoinClicked();
    UFUNCTION() void HandleJoinByCodeClicked();
    UPROPERTY(Transient) TObjectPtr<class UVerticalBox> CodeJoinPanel;
    UPROPERTY(Transient) TObjectPtr<class UButton> OpenCodeJoinButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> CodeJoinButton;
    UPROPERTY(Transient) TObjectPtr<class UEditableTextBox> PublicJoinPasswordInput;
    TSet<FString> PasswordPublicRoomCodes;

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
	void HandleChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleLogoutClicked();

	UFUNCTION()
	void HandleOpenLeaderboardTestClicked();

	UFUNCTION()
	void HandleLogoutResult(bool bSuccess, const FString& Message, const FString& DisplayName);

	UFUNCTION()
	void HandleServiceStatusChanged(bool bMaintenanceEnabled, const FString& Announcement,
		const FString& ShutdownAtUtc, const FString& Message);

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
	void BeginGameStartCountdown();
	void ShowGameSessionSearch(const FProjectProject01RoomState* RoomState=nullptr);
	void AdvanceGameStartCountdown();
	void BeginGameStartFade(const FProjectProject01RoomState& RoomState);
	void CompleteGameStartFade();
    void ContinueGameAdmission(float DeltaSeconds);
    FString AdmissionRoomId;
	FString AdmissionMatchId;
    float AdmissionWaitSeconds=0;
	float AdmissionRetrySeconds=0;
	int32 AdmissionTicketAttempts=0;
	void CancelGameStartCountdown();
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
	TObjectPtr<class UVerticalBox> PublicRoomList;
    UPROPERTY(Transient) TArray<TObjectPtr<UProjectProject01LobbySelectionBinding>> PublicRoomBindings;
    UPROPERTY(Transient) TArray<TObjectPtr<UProjectProject01LobbySelectionBinding>> HostTransferBindings;
	UPROPERTY(Transient) TObjectPtr<class UBorder> SelectedPublicRoomBanner;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> SelectedPublicRoomText;

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
	TObjectPtr<class UButton> ReadyButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> StartRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> LeaveRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UVerticalBox> HostTransferList;
    UPROPERTY(Transient) TObjectPtr<class USizeBox> HostTransferFrame;

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

	UPROPERTY(Transient)
	TObjectPtr<class UBorder> StartCountdownPanel;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> StartCountdownText;

    TMap<FString,FString> PublicRoomLabels;
    FString SelectedPublicRoomCode,SelectedHostTransferUserId;
	FTimerHandle LobbyRefreshTimer;
	FTimerHandle GameStartCountdownTimer;
	FTimerHandle GameStartFadeTimer;
	int32 GameStartCountdownNumber = 0;
	int32 ServiceStatusPollCounter = 0;
	bool bLogoutAfterLeave = false;
	bool bLobbyLogoutInProgress = false;
	bool bTravelRequested = false;
	bool bShowingCreateRoom = false;
	bool bGameStartCountdownActive = false;
	bool bServerPreparationPending = false;
	bool bGameStartCountdownComplete = false;
	bool bGameStartFadeActive = false;
	bool bGameStartFadeComplete = false;
	bool bStartConfirmationRequested = false;
	float GameStartFadeElapsedSeconds = 0.0f;
	float GameStartFadeDurationSeconds = 2.0f;
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
	void HandleMannequinTabClicked();

	UFUNCTION()
	void HandleSurvivorTabClicked();

	UFUNCTION()
	void HandleIndividualScopeClicked();

	UFUNCTION()
	void HandleTeamScopeClicked();

	UFUNCTION()
	void HandlePvPCategoryClicked();

	UFUNCTION()
	void HandleBotCategoryClicked();

	UFUNCTION()
	void HandleSortSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleSubmitRecordClicked();

	UFUNCTION()
	void HandleReturnToLobbyClicked();

	UFUNCTION()
	void HandleReturnToRoomClicked();

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
	void SetSelectedRole(EProjectProject01LeaderboardRole NewRole, bool bRefreshLeaderboard);
	void SetSelectedScope(EProjectProject01LeaderboardScope NewScope, bool bRefreshLeaderboard);
	void SetSelectedCategory(EProjectProject01LeaderboardCategory NewCategory, bool bRefreshLeaderboard);
	void SetStatus(const FString& Message, bool bIsError);
	EProjectProject01LeaderboardRole GetSelectedRole() const;
	EProjectProject01LeaderboardSort GetSelectedSort() const;
	EProjectProject01LeaderboardScope GetSelectedScope() const;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> PvPCategoryButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> BotCategoryButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> MannequinTabButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SurvivorTabButton;

	UPROPERTY(Transient)
	TObjectPtr<class UHorizontalBox> SurvivorScopeBar;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> IndividualScopeButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> TeamScopeButton;

	UPROPERTY(Transient)
	TObjectPtr<class UComboBoxString> SortComboBox;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> RefreshButton;

	UPROPERTY(Transient)
	TObjectPtr<class UMultiLineEditableTextBox> LeaderboardText;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> SubmitRecordButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReturnToLobbyButton;

	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReturnToRoomButton;

	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> LeaderboardStatusText;

	EProjectProject01LeaderboardRole SelectedRole = EProjectProject01LeaderboardRole::Mannequin;
	EProjectProject01LeaderboardScope SelectedScope = EProjectProject01LeaderboardScope::Individual;
	EProjectProject01LeaderboardCategory SelectedCategory = EProjectProject01LeaderboardCategory::PvP;
	bool bUpdatingSortOptions = false;
	bool bOpenedFromMatchResult = false;
	bool bReturnToLobbyAfterLeave = false;
};

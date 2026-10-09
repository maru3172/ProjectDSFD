// File: Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01LoginWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Kismet/GameplayStatics.h"
#include "PlayerCharacter.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01Localization.h"
#include "ProjectProject01SaveSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#endif

namespace ProjectProject01LoginUI
{
	UTextBlock* AddLabel(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& Text, const float FontSize)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FProjectProject01Localization::Text(Text));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor(0.82f, 0.82f, 0.82f, 1.0f));
		Background->SetPadding(FMargin(6.0f));
		Background->AddChild(Label);
		if (UVerticalBoxSlot* Slot = Root->AddChildToVerticalBox(Background))
		{
			Slot->SetPadding(FMargin(2.0f, 4.0f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Label;
	}

	UButton* AddButton(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& Text)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>();
		ButtonText->SetText(FProjectProject01Localization::Text(Text));
		ButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		ButtonText->SetJustification(ETextJustify::Center);
		Button->AddChild(ButtonText);
		if (UVerticalBoxSlot* Slot = Root->AddChildToVerticalBox(Button))
		{
			Slot->SetPadding(FMargin(6.0f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Button;
	}

	void SetComboBoxTextBlack(UComboBoxString* ComboBox)
	{
		if (!IsValid(ComboBox))
		{
			return;
		}
		const FSlateColor Black(FLinearColor::Black);
		FComboBoxStyle ComboStyle = ComboBox->GetWidgetStyle();
		ComboStyle.ComboButtonStyle.ButtonStyle
			.SetNormalForeground(Black)
			.SetHoveredForeground(Black)
			.SetPressedForeground(Black)
			.SetDisabledForeground(Black);
		ComboBox->SetWidgetStyle(ComboStyle);
		FTableRowStyle ItemStyle = ComboBox->GetItemStyle();
		ItemStyle.SetTextColor(Black).SetSelectedTextColor(Black);
		ComboBox->SetItemStyle(ItemStyle);
	}

	UTextBlock* AddPlainText(UWidgetTree* WidgetTree, UPanelWidget* Parent, const FString& Text, const float FontSize = 14.0f)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FProjectProject01Localization::Text(Text));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		Parent->AddChild(Label);
		return Label;
	}

	USlider* AddSliderRow(
		UWidgetTree* WidgetTree,
		UVerticalBox* Root,
		const FString& LabelText,
		const float MinValue,
		const float MaxValue,
		const float Step,
		TObjectPtr<UTextBlock>& OutValueText)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (UVerticalBoxSlot* RowSlot = Root->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(8.0f, 3.0f));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		UTextBlock* Label = AddPlainText(WidgetTree, Row, LabelText);
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Label->Slot))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		USlider* Slider = WidgetTree->ConstructWidget<USlider>();
		Slider->SetMinValue(MinValue);
		Slider->SetMaxValue(MaxValue);
		Slider->SetStepSize(Step);
		Row->AddChild(Slider);
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Slider->Slot))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetPadding(FMargin(8.0f, 0.0f));
		}
		OutValueText = AddPlainText(WidgetTree, Row, TEXT("0"));
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(OutValueText->Slot))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
		return Slider;
	}

	UCheckBox* AddCheckRow(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& LabelText)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (UVerticalBoxSlot* RowSlot = Root->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(8.0f, 3.0f));
		}
		UTextBlock* Label = AddPlainText(WidgetTree, Row, LabelText);
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Label->Slot))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		UCheckBox* CheckBox = WidgetTree->ConstructWidget<UCheckBox>();
		Row->AddChild(CheckBox);
		return CheckBox;
	}

	UComboBoxString* AddComboRow(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& LabelText)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (UVerticalBoxSlot* RowSlot = Root->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(8.0f, 3.0f));
		}
		UTextBlock* Label = AddPlainText(WidgetTree, Row, LabelText);
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Label->Slot))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		UComboBoxString* Combo = WidgetTree->ConstructWidget<UComboBoxString>();
		SetComboBoxTextBlack(Combo);
		Row->AddChild(Combo);
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Combo->Slot))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		return Combo;
	}

	UButton* AddKeyRow(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& LabelText)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (UVerticalBoxSlot* RowSlot = Root->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(8.0f, 3.0f));
		}
		UTextBlock* Label = AddPlainText(WidgetTree, Row, LabelText);
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Label->Slot))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>();
		ButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		ButtonText->SetJustification(ETextJustify::Center);
		Button->AddChild(ButtonText);
		Row->AddChild(Button);
		return Button;
	}

	void SetButtonText(UButton* Button, const FString& Text)
	{
		if (IsValid(Button))
		{
			if (UTextBlock* TextBlock = Cast<UTextBlock>(Button->GetContent()))
			{
				TextBlock->SetText(FProjectProject01Localization::Text(Text));
			}
		}
	}
}

bool UProjectProject01ServiceNoticeWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	SetVisibility(ESlateVisibility::Collapsed);
	SetIsFocusable(false);
	SetDesiredSizeInViewport(FVector2D(900.0f, 64.0f));
	SetAlignmentInViewport(FVector2D(0.5f, 0.0f));
	SetPositionInViewport(FVector2D(0.0f, 24.0f), false);
	return true;
}

void UProjectProject01ServiceNoticeWidget::SetNotice(
	const FString& Message,
	const bool bIsMaintenance)
{
	if (Message.IsEmpty())
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (IsValid(NoticeText))
	{
		NoticeText->SetText(FProjectProject01Localization::Text(Message));
	}
	if (IsValid(NoticeBackground))
	{
		NoticeBackground->SetBrushColor(bIsMaintenance
			? FLinearColor(0.55f, 0.03f, 0.03f, 0.94f)
			: FLinearColor(0.06f, 0.06f, 0.06f, 0.90f));
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UProjectProject01ServiceNoticeWidget::BuildWidgetTree()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}
	NoticeBackground = WidgetTree->ConstructWidget<UBorder>();
	NoticeBackground->SetPadding(FMargin(16.0f, 10.0f));
	WidgetTree->RootWidget = NoticeBackground;
	NoticeText = WidgetTree->ConstructWidget<UTextBlock>();
	NoticeText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	NoticeText->SetJustification(ETextJustify::Center);
	NoticeText->SetAutoWrapText(true);
	FSlateFontInfo Font = NoticeText->GetFont();
	Font.Size = 18;
	NoticeText->SetFont(Font);
	NoticeBackground->AddChild(NoticeText);
}

bool UProjectProject01TitleWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	SetIsFocusable(true);
	BuildWidgetTree();
	return true;
}

void UProjectProject01TitleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bNavigationRequested = false;

	if (IsValid(SinglePlayerButton))
	{
		SinglePlayerButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSinglePlayerClicked);
	}
	if (IsValid(ContinueSinglePlayerButton))
	{
		ContinueSinglePlayerButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleContinueSinglePlayerClicked);
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (const UProjectProject01SaveSubsystem* Saves = GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>())
			{
				ContinueSinglePlayerButton->SetIsEnabled(Saves->HasRecoverableSave());
			}
		}
	}
	if (IsValid(MultiplayerButton))
	{
		MultiplayerButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleMultiplayerClicked);
	}
	if (IsValid(SettingsButton))
	{
		SettingsButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSettingsClicked);
	}
	if (IsValid(QuitButton))
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleQuitClicked);
	}
}

namespace ProjectProject01TitleRoutes
{
	const FName SinglePlayerLevel(TEXT("/Game/MyProject/Level/LevelTest01"));
	const FName MultiplayerLoginLevel(TEXT("/Game/MyProject/Level/LoginLevel"));
	const FName TitleLevel(TEXT("/Game/MyProject/Level/TitleLevel"));
}

void UProjectProject01TitleWidget::NativeDestruct()
{
	if (IsValid(SinglePlayerButton))
	{
		SinglePlayerButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSinglePlayerClicked);
	}
	if (IsValid(ContinueSinglePlayerButton))
	{
		ContinueSinglePlayerButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleContinueSinglePlayerClicked);
	}
	if (IsValid(MultiplayerButton))
	{
		MultiplayerButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleMultiplayerClicked);
	}
	if (IsValid(SettingsButton))
	{
		SettingsButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSettingsClicked);
	}
	if (IsValid(QuitButton))
	{
		QuitButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleQuitClicked);
	}
	Super::NativeDestruct();
}

void UProjectProject01TitleWidget::HandleSinglePlayerClicked()
{
	if (bNavigationRequested || !IsValid(GetWorld()))
	{
		return;
	}

	bNavigationRequested = true;
	SetMenuEnabled(false);
	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		PlayerController->bShowMouseCursor = false;
		PlayerController->SetInputMode(FInputModeGameOnly());
	}
	// A local package name intentionally performs no backend, lobby, or dedicated-server connection.
	UGameplayStatics::OpenLevel(this, ProjectProject01TitleRoutes::SinglePlayerLevel, true);
}

void UProjectProject01TitleWidget::HandleContinueSinglePlayerClicked()
{
	if (bNavigationRequested) return;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (!IsValid(Saves) || !Saves->HasRecoverableSave()) return;
	bNavigationRequested = true;
	SetMenuEnabled(false);
	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		PlayerController->bShowMouseCursor = false;
		PlayerController->SetInputMode(FInputModeGameOnly());
	}
	if (!Saves->LoadLatestCheckpoint())
	{
		bNavigationRequested = false;
		SetMenuEnabled(true);
	}
}

void UProjectProject01TitleWidget::HandleMultiplayerClicked()
{
	if (bNavigationRequested || !IsValid(GetWorld()))
	{
		return;
	}

	bNavigationRequested = true;
	SetMenuEnabled(false);
	UGameplayStatics::OpenLevel(this, ProjectProject01TitleRoutes::MultiplayerLoginLevel, true);
}

void UProjectProject01TitleWidget::HandleSettingsClicked()
{
	if (bNavigationRequested || !IsValid(GetOwningPlayer()))
	{
		return;
	}
	SettingsWidget = CreateWidget<UProjectProject01SettingsWidget>(
		GetOwningPlayer(), UProjectProject01SettingsWidget::StaticClass());
	if (!ensureMsgf(IsValid(SettingsWidget), TEXT("Title menu could not create settings widget.")))
	{
		return;
	}
	SettingsWidget->ConfigureReturnWidget(this);
	SetVisibility(ESlateVisibility::Collapsed);
	SettingsWidget->AddToViewport(600);
}

void UProjectProject01TitleWidget::HandleQuitClicked()
{
	if (bNavigationRequested)
	{
		return;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!ensureMsgf(IsValid(PlayerController), TEXT("Title menu could not find its owning player controller.")))
	{
		return;
	}

	bNavigationRequested = true;
	SetMenuEnabled(false);
	UKismetSystemLibrary::QuitGame(this, PlayerController, EQuitPreference::Quit, false);
}

void UProjectProject01TitleWidget::BuildWidgetTree()
{
	if (!ensureMsgf(IsValid(WidgetTree), TEXT("Title widget has no WidgetTree.")))
	{
		return;
	}

	UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Overlay;

	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrushColor(FLinearColor(0.01f, 0.01f, 0.015f, 1.0f));
	if (UOverlaySlot* BackgroundSlot = Overlay->AddChildToOverlay(Background))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Menu = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UOverlaySlot* MenuSlot = Overlay->AddChildToOverlay(Menu))
	{
		MenuSlot->SetHorizontalAlignment(HAlign_Center);
		MenuSlot->SetVerticalAlignment(VAlign_Center);
		MenuSlot->SetPadding(FMargin(40.0f));
	}

	ProjectProject01LoginUI::AddLabel(WidgetTree, Menu, TEXT("ProjectProject01"), 32.0f);
	ProjectProject01LoginUI::AddLabel(
		WidgetTree,
		Menu,
		TEXT("싱글 플레이는 로컬 게임으로 시작하며, 멀티플레이는 로그인 화면으로 이동합니다."),
		14.0f);
	SinglePlayerButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("싱글 플레이"));
	ContinueSinglePlayerButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("싱글 이어하기"));
	MultiplayerButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("멀티플레이"));
	SettingsButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("환경설정"));
	QuitButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("게임 종료"));
}

void UProjectProject01TitleWidget::SetMenuEnabled(const bool bEnabled)
{
	if (IsValid(SinglePlayerButton))
	{
		SinglePlayerButton->SetIsEnabled(bEnabled);
	}
	if (IsValid(ContinueSinglePlayerButton))
	{
		bool bHasSave = false;
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (const UProjectProject01SaveSubsystem* Saves = GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>())
			{
				bHasSave = Saves->HasRecoverableSave();
			}
		}
		ContinueSinglePlayerButton->SetIsEnabled(bEnabled && bHasSave);
	}
	if (IsValid(MultiplayerButton))
	{
		MultiplayerButton->SetIsEnabled(bEnabled);
	}
	if (IsValid(SettingsButton))
	{
		SettingsButton->SetIsEnabled(bEnabled);
	}
	if (IsValid(QuitButton))
	{
		QuitButton->SetIsEnabled(bEnabled);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01TitleRoutesTest,
	"ProjectProject01.Frontend.TitleRoutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01TitleRoutesTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("TitleLevel package exists"),
		FPackageName::DoesPackageExist(ProjectProject01TitleRoutes::TitleLevel.ToString()));
	TestTrue(
		TEXT("Single-player LevelTest01 package exists"),
		FPackageName::DoesPackageExist(ProjectProject01TitleRoutes::SinglePlayerLevel.ToString()));
	TestTrue(
		TEXT("Multiplayer LoginLevel package exists"),
		FPackageName::DoesPackageExist(ProjectProject01TitleRoutes::MultiplayerLoginLevel.ToString()));
	return true;
}
#endif

bool UProjectProject01LoginWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	return true;
}

void UProjectProject01LoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(LoginButton))
	{
		LoginButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleLoginClicked);
	}
	if (IsValid(RegisterButton))
	{
		RegisterButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleRegisterClicked);
	}
	if (IsValid(ReturnToTitleButton))
	{
		ReturnToTitleButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleReturnToTitleClicked);
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLoginCompleted.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleLoginResult);
			Auth->OnRegistrationCompleted.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleRegistrationResult);
			Auth->OnServiceStatusChanged.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleServiceStatusChanged);
			Auth->CheckMultiplayerServiceStatus();
		}
	}
}

void UProjectProject01LoginWidget::NativeDestruct()
{
	if (IsValid(LoginButton))
	{
		LoginButton->OnClicked.RemoveDynamic(this, &UProjectProject01LoginWidget::HandleLoginClicked);
	}
	if (IsValid(RegisterButton))
	{
		RegisterButton->OnClicked.RemoveDynamic(this, &UProjectProject01LoginWidget::HandleRegisterClicked);
	}
	if (IsValid(ReturnToTitleButton))
	{
		ReturnToTitleButton->OnClicked.RemoveDynamic(this, &UProjectProject01LoginWidget::HandleReturnToTitleClicked);
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLoginCompleted.RemoveDynamic(this, &UProjectProject01LoginWidget::HandleLoginResult);
			Auth->OnRegistrationCompleted.RemoveDynamic(this, &UProjectProject01LoginWidget::HandleRegistrationResult);
			Auth->OnServiceStatusChanged.RemoveDynamic(this, &UProjectProject01LoginWidget::HandleServiceStatusChanged);
		}
	}
	Super::NativeDestruct();
}

void UProjectProject01LoginWidget::HandleReturnToTitleClicked()
{
	if (!IsValid(GetWorld()))
	{
		SetStatus(TEXT("타이틀 화면을 열 수 없습니다."), true);
		return;
	}

	SetRequestControlsEnabled(false);
	UGameplayStatics::OpenLevel(this, ProjectProject01TitleRoutes::TitleLevel, true);
}

void UProjectProject01LoginWidget::HandleLoginClicked()
{
	if (!IsValid(AccountIdInput) || !IsValid(PasswordInput))
	{
		SetStatus(TEXT("로그인 입력 UI를 찾지 못했습니다."), true);
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>()
		: nullptr;
	if (!IsValid(Auth))
	{
		SetStatus(TEXT("인증 시스템을 찾지 못했습니다."), true);
		return;
	}

	SetRequestControlsEnabled(false);
	SetStatus(TEXT("로그인 중..."), false);
	Auth->Login(AccountIdInput->GetText().ToString(), PasswordInput->GetText().ToString());
}

void UProjectProject01LoginWidget::HandleRegisterClicked()
{
	if (!IsValid(AccountIdInput) || !IsValid(PasswordInput) || !IsValid(DisplayNameInput))
	{
		SetStatus(TEXT("회원가입 입력 UI를 찾지 못했습니다."), true);
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>()
		: nullptr;
	if (!IsValid(Auth))
	{
		SetStatus(TEXT("인증 시스템을 찾지 못했습니다."), true);
		return;
	}

	SetRequestControlsEnabled(false);
	SetStatus(TEXT("계정 생성 중..."), false);
	Auth->RegisterAccount(
		AccountIdInput->GetText().ToString(),
		PasswordInput->GetText().ToString(),
		DisplayNameInput->GetText().ToString());
}

void UProjectProject01LoginWidget::HandleLoginResult(
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	SetRequestControlsEnabled(true);
	SetStatus(Message, !bSuccess);
	if (bSuccess)
	{
		EnterLobby();
	}
}

void UProjectProject01LoginWidget::HandleRegistrationResult(
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	SetRequestControlsEnabled(true);
	SetStatus(Message, !bSuccess);
	if (bSuccess)
	{
		EnterLobby();
	}
}

void UProjectProject01LoginWidget::HandleServiceStatusChanged(
	const bool bMaintenanceEnabled,
	const FString& Announcement,
	const FString& ShutdownAtUtc,
	const FString& Message)
{
	SetRequestControlsEnabled(!bMaintenanceEnabled);
	if (IsValid(ReturnToTitleButton)) ReturnToTitleButton->SetIsEnabled(true);
	if (bMaintenanceEnabled)
	{
		SetStatus(Message.IsEmpty()
			? TEXT("현재 멀티플레이 서버를 점검 중입니다. 싱글플레이는 타이틀에서 이용할 수 있습니다.")
			: Message, true);
	}
	else if (!Announcement.IsEmpty())
	{
		SetStatus(ShutdownAtUtc.IsEmpty() ? Announcement :
			FString::Printf(TEXT("%s (서버 종료 예정: %s)"), *Announcement, *ShutdownAtUtc), false);
	}
}

void UProjectProject01LoginWidget::BuildWidgetTree()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Root;
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("ProjectProject01"), 30.0f);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("로그인 / 회원가입"), 20.0f);

	AccountIdInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	AccountIdInput->SetHintText(FProjectProject01Localization::Text(TEXT("계정 ID (영문/숫자/밑줄 3~32자)")));
	AccountIdInput->SetForegroundColor(FLinearColor::Black);
	Root->AddChildToVerticalBox(AccountIdInput)->SetPadding(FMargin(6.0f));

	PasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	PasswordInput->SetHintText(FProjectProject01Localization::Text(TEXT("비밀번호 (10~128자)")));
	PasswordInput->SetForegroundColor(FLinearColor::Black);
	PasswordInput->SetIsPassword(true);
	Root->AddChildToVerticalBox(PasswordInput)->SetPadding(FMargin(6.0f));

	DisplayNameInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	DisplayNameInput->SetHintText(FProjectProject01Localization::Text(TEXT("표시 이름 (회원가입 시 2~32자)")));
	DisplayNameInput->SetForegroundColor(FLinearColor::Black);
	Root->AddChildToVerticalBox(DisplayNameInput)->SetPadding(FMargin(6.0f));

	LoginButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("로그인"));
	RegisterButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("회원가입"));
	ReturnToTitleButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("타이틀로 돌아가기"));
	StatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("인증 서버에 연결할 준비가 되었습니다."), 14.0f);
}

void UProjectProject01LoginWidget::SetRequestControlsEnabled(const bool bEnabled)
{
	bool bAuthenticationEnabled = bEnabled;
	if (const UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
	{
		if (const UProjectProject01AuthSubsystem* Auth =
			GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			bAuthenticationEnabled = bAuthenticationEnabled && !Auth->IsMultiplayerMaintenanceActive();
		}
	}
	if (IsValid(LoginButton))
	{
		LoginButton->SetIsEnabled(bAuthenticationEnabled);
	}
	if (IsValid(RegisterButton))
	{
		RegisterButton->SetIsEnabled(bAuthenticationEnabled);
	}
	if (IsValid(ReturnToTitleButton))
	{
		ReturnToTitleButton->SetIsEnabled(bEnabled);
	}
}

void UProjectProject01LoginWidget::SetStatus(const FString& Message, const bool bIsError)
{
	if (!IsValid(StatusText))
	{
		return;
	}
	StatusText->SetText(FProjectProject01Localization::Text(Message));
	StatusText->SetColorAndOpacity(bIsError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::Black));
}

void UProjectProject01LoginWidget::EnterLobby()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LobbyLevel")));
}

bool UProjectProject01SessionMenuWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	SetIsFocusable(true);
	BuildWidgetTree();
	return true;
}

void UProjectProject01SessionMenuWidget::ConfigureForSession(const bool bInMultiplayer)
{
	bMultiplayer = bInMultiplayer;
	if (IsValid(ReturnToRoomButton))
	{
		ReturnToRoomButton->SetVisibility(bMultiplayer ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(SaveCheckpointButton))
	{
		SaveCheckpointButton->SetVisibility(bMultiplayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(LoadCheckpointButton))
	{
		LoadCheckpointButton->SetVisibility(bMultiplayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		if (!bMultiplayer)
		{
			if (UGameInstance* GameInstance = GetGameInstance())
			{
				if (const UProjectProject01SaveSubsystem* Saves = GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>())
				{
					LoadCheckpointButton->SetIsEnabled(Saves->HasRecoverableSave());
				}
			}
		}
	}
	if (IsValid(StatusText))
	{
		StatusText->SetText(FProjectProject01Localization::Text(bMultiplayer
			? TEXT("멀티플레이는 메뉴를 열어도 서버 경기가 계속 진행됩니다.")
			: TEXT("싱글플레이가 일시정지되었습니다.")));
	}
}

void UProjectProject01SessionMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(ContinueButton)) ContinueButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleContinueClicked);
	if (IsValid(SaveCheckpointButton)) SaveCheckpointButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleSaveCheckpointClicked);
	if (IsValid(LoadCheckpointButton)) LoadCheckpointButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadCheckpointClicked);
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleReturnToRoomClicked);
	if (IsValid(SettingsButton)) SettingsButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleSettingsClicked);
	if (IsValid(ReturnToTitleButton)) ReturnToTitleButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleReturnToTitleClicked);
	if (IsValid(QuitGameButton)) QuitGameButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleQuitGameClicked);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->OnRequestCompleted.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleLobbyRequestResult);
		}
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleLogoutResult);
		}
		if (UProjectProject01SaveSubsystem* Saves = GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>())
		{
			Saves->OnSaveOperationCompleted.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleSaveOperationCompleted);
		}
	}
	SetKeyboardFocus();
}

void UProjectProject01SessionMenuWidget::NativeDestruct()
{
	if (IsValid(ContinueButton)) ContinueButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleContinueClicked);
	if (IsValid(SaveCheckpointButton)) SaveCheckpointButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleSaveCheckpointClicked);
	if (IsValid(LoadCheckpointButton)) LoadCheckpointButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadCheckpointClicked);
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleReturnToRoomClicked);
	if (IsValid(SettingsButton)) SettingsButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleSettingsClicked);
	if (IsValid(ReturnToTitleButton)) ReturnToTitleButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleReturnToTitleClicked);
	if (IsValid(QuitGameButton)) QuitGameButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleQuitGameClicked);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->OnRequestCompleted.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleLobbyRequestResult);
		}
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleLogoutResult);
		}
		if (UProjectProject01SaveSubsystem* Saves = GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>())
		{
			Saves->OnSaveOperationCompleted.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleSaveOperationCompleted);
		}
	}
	Super::NativeDestruct();
}

void UProjectProject01SessionMenuWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bExitFadeActive)
	{
		return;
	}
	ExitFadeElapsedSeconds += FMath::Max(0.0f, InDeltaTime);
	const float Alpha = FMath::Clamp(
		ExitFadeElapsedSeconds / FMath::Max(0.01f, ExitFadeDurationSeconds), 0.0f, 1.0f);
	if (IsValid(ExitFadeOverlay))
	{
		ExitFadeOverlay->SetRenderOpacity(Alpha);
	}
	if (Alpha >= 1.0f)
	{
		bExitFadeActive = false;
		ExecutePendingExitAfterFade();
	}
}

FReply UProjectProject01SessionMenuWidget::NativeOnKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && !bBusy)
	{
		CloseMenu();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UProjectProject01SessionMenuWidget::HandleContinueClicked()
{
	CloseMenu();
}

void UProjectProject01SessionMenuWidget::HandleSaveCheckpointClicked()
{
	if (bMultiplayer || bBusy)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	APlayerCharacter* Player = GetOwningPlayerPawn<APlayerCharacter>();
	if (!IsValid(Saves) || !IsValid(Player))
	{
		SetBusy(false, TEXT("저장할 싱글플레이 상태를 찾지 못했습니다."), true);
		return;
	}
	SetBusy(true, TEXT("체크포인트를 저장하는 중..."));
	Saves->SaveCheckpoint(Player, TEXT("ManualPauseMenu"));
}

void UProjectProject01SessionMenuWidget::HandleLoadCheckpointClicked()
{
	if (bMultiplayer || bBusy)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (!IsValid(Saves))
	{
		SetBusy(false, TEXT("저장 시스템을 찾지 못했습니다."), true);
		return;
	}
	SetBusy(true, TEXT("마지막 체크포인트를 불러오는 중..."));
	if (Saves->LoadLatestCheckpoint())
	{
		CloseMenu();
	}
}

void UProjectProject01SessionMenuWidget::HandleSaveOperationCompleted(
	const bool bSucceeded,
	const FString& Message)
{
	SetBusy(false, Message, !bSucceeded);
	if (bSucceeded && IsValid(LoadCheckpointButton))
	{
		LoadCheckpointButton->SetIsEnabled(true);
	}
}

void UProjectProject01SessionMenuWidget::HandleReturnToRoomClicked()
{
	if (!bMultiplayer || bBusy)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || !Lobby->HasCurrentRoom())
	{
		SetBusy(false, TEXT("유지할 대기방 정보를 찾지 못했습니다."), true);
		return;
	}

	BeginExitFade(EPendingExitAction::ReturnToRoom,
		TEXT("게임 서버 접속을 끝내고 방으로 돌아가는 중..."));
}

void UProjectProject01SessionMenuWidget::HandleSettingsClicked()
{
	if (bBusy || !IsValid(GetOwningPlayer()))
	{
		return;
	}
	SettingsWidget = CreateWidget<UProjectProject01SettingsWidget>(
		GetOwningPlayer(), UProjectProject01SettingsWidget::StaticClass());
	if (!ensureMsgf(IsValid(SettingsWidget), TEXT("Session menu could not create settings widget.")))
	{
		return;
	}
	SettingsWidget->ConfigureReturnWidget(this);
	SetVisibility(ESlateVisibility::Collapsed);
	SettingsWidget->AddToViewport(600);
}

void UProjectProject01SessionMenuWidget::HandleReturnToTitleClicked()
{
	if (bBusy)
	{
		return;
	}
	if (bMultiplayer)
	{
		BeginAuthenticatedExit(EPendingExitAction::ReturnToTitle);
		return;
	}

	RestoreGameInput();
	UGameplayStatics::OpenLevel(this, ProjectProject01TitleRoutes::TitleLevel, true);
}

void UProjectProject01SessionMenuWidget::HandleQuitGameClicked()
{
	if (bBusy)
	{
		return;
	}
	if (bMultiplayer)
	{
		BeginAuthenticatedExit(EPendingExitAction::QuitGame);
		return;
	}

	RestoreGameInput();
	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		UKismetSystemLibrary::QuitGame(this, PlayerController, EQuitPreference::Quit, false);
	}
}

void UProjectProject01SessionMenuWidget::BeginAuthenticatedExit(const EPendingExitAction ExitAction)
{
	BeginExitFade(ExitAction, TEXT("방과 로그인 세션을 안전하게 정리하는 중..."));
}

void UProjectProject01SessionMenuWidget::BeginExitFade(
	const EPendingExitAction ExitAction,
	const FString& Message)
{
	PendingExitAction = ExitAction;
	SetBusy(true, Message);
	if (AMultiplayTestPlayerController* MultiplayerController = Cast<AMultiplayTestPlayerController>(GetOwningPlayer());
		IsValid(MultiplayerController))
	{
		MultiplayerController->DeclareVoluntaryExit();
	}
	ExitFadeElapsedSeconds = 0.0f;
	bExitFadeActive = true;
	if (IsValid(ExitFadeOverlay))
	{
		ExitFadeOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);
		ExitFadeOverlay->SetRenderOpacity(0.0f);
	}
}

void UProjectProject01SessionMenuWidget::ExecutePendingExitAfterFade()
{

	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (PendingExitAction == EPendingExitAction::ReturnToRoom)
	{
		if (IsValid(Lobby) && Lobby->HasCurrentRoom())
		{
			Lobby->ReturnToRoom();
			return;
		}
		PendingExitAction = EPendingExitAction::None;
		SetBusy(false, TEXT("유지할 대기방 정보를 찾지 못했습니다."), true);
		return;
	}
	if (IsValid(Lobby) && Lobby->HasCurrentRoom())
	{
		Lobby->LeaveRoom();
		return;
	}
	BeginLogout();
}

void UProjectProject01SessionMenuWidget::BeginLogout()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	if (!IsValid(Auth))
	{
		PendingExitAction = EPendingExitAction::None;
		SetBusy(false, TEXT("인증 시스템을 찾지 못해 로그아웃하지 않았습니다."), true);
		return;
	}
	Auth->Logout();
}

void UProjectProject01SessionMenuWidget::HandleLobbyRequestResult(
	const EProjectProject01LobbyOperation Operation,
	const bool bSuccess,
	const FString& Message)
{
	if (Operation == EProjectProject01LobbyOperation::ReturnToRoom)
	{
		if (!bSuccess)
		{
			PendingExitAction = EPendingExitAction::None;
			SetBusy(false, Message, true);
			return;
		}
		RestoreGameInput();
		PendingExitAction = EPendingExitAction::None;
		if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
		{
			PlayerController->ClientTravel(TEXT("/Game/MyProject/Level/LobbyLevel"), TRAVEL_Absolute);
		}
		return;
	}

	if (Operation == EProjectProject01LobbyOperation::LeaveRoom &&
		PendingExitAction != EPendingExitAction::None)
	{
		if (bSuccess)
		{
			BeginLogout();
		}
		else
		{
			PendingExitAction = EPendingExitAction::None;
			SetBusy(false, Message, true);
		}
	}
}

void UProjectProject01SessionMenuWidget::HandleLogoutResult(
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	if (PendingExitAction == EPendingExitAction::None)
	{
		return;
	}
	if (!bSuccess)
	{
		PendingExitAction = EPendingExitAction::None;
		SetBusy(false, Message, true);
		return;
	}
	CompleteExit();
}

void UProjectProject01SessionMenuWidget::CompleteExit()
{
	const EPendingExitAction CompletedAction = PendingExitAction;
	PendingExitAction = EPendingExitAction::None;
	RestoreGameInput();

	APlayerController* PlayerController = GetOwningPlayer();
	if (!ensureMsgf(IsValid(PlayerController), TEXT("Session menu could not find its owning player controller.")))
	{
		SetBusy(false, TEXT("플레이어 컨트롤러를 찾지 못했습니다."), true);
		return;
	}
	if (CompletedAction == EPendingExitAction::ReturnToTitle)
	{
		PlayerController->ClientTravel(ProjectProject01TitleRoutes::TitleLevel.ToString(), TRAVEL_Absolute);
	}
	else if (CompletedAction == EPendingExitAction::QuitGame)
	{
		UKismetSystemLibrary::QuitGame(this, PlayerController, EQuitPreference::Quit, false);
	}
}

void UProjectProject01SessionMenuWidget::CloseMenu()
{
	if (bBusy)
	{
		return;
	}
	RestoreGameInput();
	RemoveFromParent();
}

void UProjectProject01SessionMenuWidget::RestoreGameInput()
{
	if (!bMultiplayer)
	{
		UGameplayStatics::SetGamePaused(this, false);
	}
	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		PlayerController->bShowMouseCursor = false;
		PlayerController->SetInputMode(FInputModeGameOnly());
	}
}

void UProjectProject01SessionMenuWidget::SetBusy(
	const bool bInBusy,
	const FString& Message,
	const bool bIsError)
{
	bBusy = bInBusy;
	if (IsValid(ContinueButton)) ContinueButton->SetIsEnabled(!bBusy);
	if (IsValid(SaveCheckpointButton)) SaveCheckpointButton->SetIsEnabled(!bBusy);
	if (IsValid(LoadCheckpointButton) && bBusy) LoadCheckpointButton->SetIsEnabled(false);
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->SetIsEnabled(!bBusy);
	if (IsValid(SettingsButton)) SettingsButton->SetIsEnabled(!bBusy);
	if (IsValid(ReturnToTitleButton)) ReturnToTitleButton->SetIsEnabled(!bBusy);
	if (IsValid(QuitGameButton)) QuitGameButton->SetIsEnabled(!bBusy);
	if (IsValid(StatusText) && !Message.IsEmpty())
	{
		StatusText->SetText(FProjectProject01Localization::Text(Message));
		StatusText->SetColorAndOpacity(bIsError
			? FSlateColor(FLinearColor::Red)
			: FSlateColor(FLinearColor::Black));
	}
	if (!bBusy && bIsError && IsValid(ExitFadeOverlay))
	{
		ExitFadeOverlay->SetRenderOpacity(0.0f);
		ExitFadeOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UProjectProject01SessionMenuWidget::BuildWidgetTree()
{
	if (!ensureMsgf(IsValid(WidgetTree), TEXT("Session menu widget has no WidgetTree.")))
	{
		return;
	}

	UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Overlay;
	UBorder* DimBackground = WidgetTree->ConstructWidget<UBorder>();
	DimBackground->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.72f));
	if (UOverlaySlot* BackgroundSlot = Overlay->AddChildToOverlay(DimBackground))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Menu = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UOverlaySlot* MenuSlot = Overlay->AddChildToOverlay(Menu))
	{
		MenuSlot->SetHorizontalAlignment(HAlign_Center);
		MenuSlot->SetVerticalAlignment(VAlign_Center);
		MenuSlot->SetPadding(FMargin(40.0f));
	}
	ProjectProject01LoginUI::AddLabel(WidgetTree, Menu, TEXT("게임 메뉴"), 30.0f);
	StatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Menu, TEXT("ESC를 다시 누르면 게임으로 돌아갑니다."), 14.0f);
	ContinueButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("게임 계속"));
	SaveCheckpointButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("현재 체크포인트 저장"));
	LoadCheckpointButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("마지막 체크포인트 불러오기"));
	ReturnToRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("방으로 돌아가기"));
	SettingsButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("환경설정"));
	ReturnToTitleButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("타이틀로 돌아가기"));
	QuitGameButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("게임 종료"));

	ExitFadeOverlay = WidgetTree->ConstructWidget<UBorder>();
	ExitFadeOverlay->SetBrushColor(FLinearColor::Black);
	ExitFadeOverlay->SetRenderOpacity(0.0f);
	ExitFadeOverlay->SetVisibility(ESlateVisibility::Collapsed);
	if (UOverlaySlot* FadeSlot = Overlay->AddChildToOverlay(ExitFadeOverlay))
	{
		FadeSlot->SetHorizontalAlignment(HAlign_Fill);
		FadeSlot->SetVerticalAlignment(VAlign_Fill);
	}
}

bool UProjectProject01MatchResultWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	SetIsFocusable(true);
	BuildWidgetTree();
	return true;
}

void UProjectProject01MatchResultWidget::ConfigureResult(const FMultiplayTestMatchResult& InResult)
{
	Result = InResult;
	bSinglePlayer = false;
	bConfigured = true;
	RefreshResultText();
}

void UProjectProject01MatchResultWidget::ConfigureSinglePlayerResult(const bool bEscaped)
{
	Result = FMultiplayTestMatchResult();
	Result.Role = TEXT("SinglePlayer");
	Result.Outcome = bEscaped ? TEXT("Escaped") : TEXT("Eliminated");
	Result.bSuccess = bEscaped;
	bSinglePlayer = true;
	bConfigured = true;
	RefreshResultText();
}

void UProjectProject01MatchResultWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(RegisterButton))
	{
		RegisterButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01MatchResultWidget::HandleRegisterClicked);
	}
	if (IsValid(ReturnToRoomButton))
	{
		ReturnToRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01MatchResultWidget::HandleReturnToRoomClicked);
	}
	if (IsValid(LeaderboardButton))
	{
		LeaderboardButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01MatchResultWidget::HandleLeaderboardClicked);
	}
	if (IsValid(ReturnToTitleButton))
	{
		ReturnToTitleButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01MatchResultWidget::HandleReturnToTitleClicked);
	}
	if (IsValid(LogoutButton))
	{
		LogoutButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01MatchResultWidget::HandleLogoutClicked);
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->OnRequestCompleted.AddUniqueDynamic(this, &UProjectProject01MatchResultWidget::HandleLobbyRequestResult);
		}
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.AddUniqueDynamic(this, &UProjectProject01MatchResultWidget::HandleLogoutResult);
		}
	}
	FadeElapsedSeconds = 0.0f;
	if (IsValid(FadeBackground))
	{
		FadeBackground->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	}
	if (IsValid(ResultMenu))
	{
		ResultMenu->SetRenderOpacity(0.0f);
		ResultMenu->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	SetBusy(false);
	RefreshResultText();
	SetKeyboardFocus();
}

void UProjectProject01MatchResultWidget::NativeDestruct()
{
	if (IsValid(RegisterButton))
	{
		RegisterButton->OnClicked.RemoveDynamic(this, &UProjectProject01MatchResultWidget::HandleRegisterClicked);
	}
	if (IsValid(ReturnToRoomButton))
	{
		ReturnToRoomButton->OnClicked.RemoveDynamic(this, &UProjectProject01MatchResultWidget::HandleReturnToRoomClicked);
	}
	if (IsValid(LeaderboardButton))
	{
		LeaderboardButton->OnClicked.RemoveDynamic(this, &UProjectProject01MatchResultWidget::HandleLeaderboardClicked);
	}
	if (IsValid(ReturnToTitleButton))
	{
		ReturnToTitleButton->OnClicked.RemoveDynamic(this, &UProjectProject01MatchResultWidget::HandleReturnToTitleClicked);
	}
	if (IsValid(LogoutButton))
	{
		LogoutButton->OnClicked.RemoveDynamic(this, &UProjectProject01MatchResultWidget::HandleLogoutClicked);
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->OnRequestCompleted.RemoveDynamic(this, &UProjectProject01MatchResultWidget::HandleLobbyRequestResult);
		}
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.RemoveDynamic(this, &UProjectProject01MatchResultWidget::HandleLogoutResult);
		}
	}
	Super::NativeDestruct();
}

void UProjectProject01MatchResultWidget::NativeTick(
	const FGeometry& MyGeometry,
	const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (FadeElapsedSeconds >= FadeDurationSeconds)
	{
		return;
	}

	FadeElapsedSeconds = FMath::Min(FadeDurationSeconds, FadeElapsedSeconds + FMath::Max(0.0f, InDeltaTime));
	const float FadeAlpha = FadeDurationSeconds > KINDA_SMALL_NUMBER
		? FMath::Clamp(FadeElapsedSeconds / FadeDurationSeconds, 0.0f, 1.0f)
		: 1.0f;
	if (IsValid(FadeBackground))
	{
		FadeBackground->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.98f * FadeAlpha));
	}
	if (IsValid(ResultMenu))
	{
		const float MenuAlpha = FMath::Clamp((FadeAlpha - 0.72f) / 0.28f, 0.0f, 1.0f);
		ResultMenu->SetRenderOpacity(MenuAlpha);
		if (FadeAlpha >= 1.0f)
		{
			ResultMenu->SetVisibility(ESlateVisibility::Visible);
			SetBusy(bBusy);
		}
	}
}

void UProjectProject01MatchResultWidget::HandleRegisterClicked()
{
	if (bSinglePlayer || bBusy || FadeElapsedSeconds < FadeDurationSeconds ||
		!bConfigured || !Result.bSuccess || !Result.bLeaderboardVerificationReady)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || Result.MatchId.IsEmpty())
	{
		SetBusy(false, TEXT("리더보드에 등록할 경기 정보를 찾지 못했습니다."), true);
		return;
	}

	const EProjectProject01LeaderboardRole Role = Result.Role.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase)
		? EProjectProject01LeaderboardRole::Mannequin
		: EProjectProject01LeaderboardRole::Survivor;
	bReturnAfterRegistration = true;
	SetBusy(true, TEXT("서버가 검증한 성공 기록을 리더보드에 등록하는 중..."));
	Lobby->SubmitLeaderboardRecord(Result.MatchId, Role, true,
		Result.CaptureCount, Result.FirstCaptureSeconds, Result.AllCapturedSeconds,
		Result.RescueCount, Result.EscapeSeconds);
}

void UProjectProject01MatchResultWidget::HandleReturnToRoomClicked()
{
	if (!bSinglePlayer && !bBusy && FadeElapsedSeconds >= FadeDurationSeconds)
	{
		bReturnAfterRegistration = false;
		BeginReturnToRoom();
	}
}

void UProjectProject01MatchResultWidget::HandleLeaderboardClicked()
{
	if (bSinglePlayer || bBusy || FadeElapsedSeconds < FadeDurationSeconds)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (IsValid(Lobby) && Lobby->HasCurrentRoom())
	{
		bOpenLeaderboardAfterReturn = true;
		SetBusy(true, TEXT("경기 종료 상태를 방에 반영한 뒤 리더보드를 여는 중..."));
		Lobby->ReturnToRoom();
		return;
	}
	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		PlayerController->ClientTravel(TEXT("/Game/MyProject/Level/LeaderBoardTest"), TRAVEL_Absolute);
	}
}

void UProjectProject01MatchResultWidget::HandleReturnToTitleClicked()
{
	if (bBusy || FadeElapsedSeconds < FadeDurationSeconds)
	{
		return;
	}
	if (!bSinglePlayer)
	{
		BeginAuthenticatedExit(true);
		return;
	}
	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
		PlayerController->ClientTravel(ProjectProject01TitleRoutes::TitleLevel.ToString(), TRAVEL_Absolute);
	}
}

void UProjectProject01MatchResultWidget::HandleLogoutClicked()
{
	if (!bSinglePlayer && !bBusy && FadeElapsedSeconds >= FadeDurationSeconds)
	{
		BeginAuthenticatedExit(false);
	}
}

void UProjectProject01MatchResultWidget::HandleLobbyRequestResult(
	const EProjectProject01LobbyOperation Operation,
	const bool bSuccess,
	const FString& Message)
{
	if (Operation == EProjectProject01LobbyOperation::SubmitLeaderboardRecord && bReturnAfterRegistration)
	{
		if (!bSuccess)
		{
			bReturnAfterRegistration = false;
			SetBusy(false, Message, true);
			return;
		}
		SetBusy(true, TEXT("기록 등록이 완료되어 기존 방으로 돌아갑니다..."));
		BeginReturnToRoom();
		return;
	}

	if (Operation == EProjectProject01LobbyOperation::LeaveRoom && bPendingAuthenticatedExit)
	{
		if (bSuccess)
		{
			BeginLogout();
		}
		else
		{
			bPendingAuthenticatedExit = false;
			SetBusy(false, Message, true);
		}
		return;
	}

	if (Operation != EProjectProject01LobbyOperation::ReturnToRoom)
	{
		return;
	}
	if (!bSuccess)
	{
		SetBusy(false, Message, true);
		return;
	}

	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		const FString Destination = bOpenLeaderboardAfterReturn
			? TEXT("/Game/MyProject/Level/LeaderBoardTest")
			: TEXT("/Game/MyProject/Level/LobbyLevel");
		bOpenLeaderboardAfterReturn = false;
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
		PlayerController->bShowMouseCursor = false;
		PlayerController->SetInputMode(FInputModeGameOnly());
		PlayerController->ClientTravel(Destination, TRAVEL_Absolute);
	}
}

void UProjectProject01MatchResultWidget::BeginReturnToRoom()
{
	bOpenLeaderboardAfterReturn = false;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || !Lobby->HasCurrentRoom())
	{
		// 방을 거치지 않은 직접 PIE 검증에서는 복귀 API를 호출할 방이 없으므로
		// 결과 화면에 갇히지 않고 로비 테스트 레벨로 안전하게 돌아간다.
		if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
		{
			PlayerController->SetIgnoreMoveInput(false);
			PlayerController->SetIgnoreLookInput(false);
			PlayerController->bShowMouseCursor = false;
			PlayerController->SetInputMode(FInputModeGameOnly());
			PlayerController->ClientTravel(TEXT("/Game/MyProject/Level/LobbyLevel"), TRAVEL_Absolute);
			return;
		}
		SetBusy(false, TEXT("복귀할 기존 방 또는 로컬 플레이어를 찾지 못했습니다."), true);
		return;
	}
	SetBusy(true, TEXT("게임 서버 접속을 끝내고 기존 방으로 돌아가는 중..."));
	Lobby->ReturnToRoom();
}

void UProjectProject01MatchResultWidget::BeginAuthenticatedExit(const bool bReturnToTitle)
{
	bPendingAuthenticatedExit = true;
	bPendingReturnToTitle = bReturnToTitle;
	SetBusy(true, TEXT("방과 로그인 세션을 안전하게 정리하는 중..."));

	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (IsValid(Lobby) && Lobby->HasCurrentRoom())
	{
		Lobby->LeaveRoom();
		return;
	}
	BeginLogout();
}

void UProjectProject01MatchResultWidget::BeginLogout()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	if (!IsValid(Auth))
	{
		bPendingAuthenticatedExit = false;
		SetBusy(false, TEXT("인증 시스템을 찾지 못해 로그아웃하지 않았습니다."), true);
		return;
	}
	Auth->Logout();
}

void UProjectProject01MatchResultWidget::HandleLogoutResult(
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	(void)DisplayName;
	if (!bPendingAuthenticatedExit)
	{
		return;
	}
	if (!bSuccess)
	{
		bPendingAuthenticatedExit = false;
		SetBusy(false, Message, true);
		return;
	}
	CompleteAuthenticatedExit();
}

void UProjectProject01MatchResultWidget::CompleteAuthenticatedExit()
{
	const bool bGoToTitle = bPendingReturnToTitle;
	bPendingAuthenticatedExit = false;
	bPendingReturnToTitle = false;
	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
		PlayerController->ClientTravel(
			(bGoToTitle ? ProjectProject01TitleRoutes::TitleLevel : ProjectProject01TitleRoutes::MultiplayerLoginLevel).ToString(),
			TRAVEL_Absolute);
	}
}

void UProjectProject01MatchResultWidget::RefreshResultText()
{
	if (!bConfigured || !IsValid(ResultText))
	{
		return;
	}

	FString Details;
	if (bSinglePlayer)
	{
		Details = Result.bSuccess ? TEXT("탈출하였습니다") : TEXT("죽었습니다");
	}
	else if (Result.Outcome == TEXT("Forfeited"))
	{
		Details = TEXT("기권했습니다");
	}
	else if (Result.Outcome == TEXT("OpponentForfeit"))
	{
		Details = TEXT("상대 기권으로 승리했습니다");
	}
	else if (Result.Outcome == TEXT("SurvivorForfeitVictory"))
	{
		Details = TEXT("생존자의 기권으로 승리했습니다!");
	}
	else if (!Result.bSuccess)
	{
		Details = TEXT("패배했습니다");
	}
	else if (Result.Role.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase))
	{
		Details = TEXT("전원 붙잡았습니다");
	}
	else
	{
		Details = TEXT("탈출하였습니다");
	}

	if (!bSinglePlayer)
	{
		Details += FString::Printf(TEXT("\n역할: %s\n전체 경기 시간: %.2f초"),
			*Result.Role, Result.MatchDurationSeconds);
		if (Result.Role.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase))
		{
			Details += FString::Printf(TEXT("\n붙잡은 횟수: %d\n첫 포획 시간: %.2f초\n전원 제거 시간: %.2f초"),
				Result.CaptureCount, Result.FirstCaptureSeconds, Result.AllCapturedSeconds);
		}
		else
		{
			Details += FString::Printf(TEXT("\n구출 횟수: %d\n탈출 시간: %.2f초"),
				Result.RescueCount, Result.EscapeSeconds);
		}
	}
	ResultText->SetText(FProjectProject01Localization::Text(Details));

	const bool bCanRegister = !bSinglePlayer && Result.bSuccess && Result.bLeaderboardVerificationReady;
	if (IsValid(RegisterButton))
	{
		RegisterButton->SetVisibility(!bSinglePlayer && Result.bSuccess
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		RegisterButton->SetIsEnabled(bCanRegister && !bBusy);
	}
	if (IsValid(LeaderboardButton)) LeaderboardButton->SetVisibility(bSinglePlayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->SetVisibility(bSinglePlayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (IsValid(LogoutButton)) LogoutButton->SetVisibility(bSinglePlayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (IsValid(StatusText))
	{
		const FString Status = bSinglePlayer
			? FString()
			: !Result.bSuccess
			? TEXT("실패 기록은 리더보드에 등록되지 않습니다.")
			: Result.bLeaderboardVerificationReady
				? TEXT("기록을 등록하거나 등록하지 않고 기존 방으로 돌아갈 수 있습니다.")
				: TEXT("서버의 리더보드 검증 기록에 실패했습니다. 방 복귀만 가능합니다.");
		StatusText->SetText(FProjectProject01Localization::Text(Status));
		StatusText->SetVisibility(bSinglePlayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		StatusText->SetColorAndOpacity(Result.bSuccess && !Result.bLeaderboardVerificationReady
			? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::White));
	}
}

void UProjectProject01MatchResultWidget::SetBusy(
	const bool bInBusy,
	const FString& Message,
	const bool bIsError)
{
	bBusy = bInBusy;
	const bool bFadeFinished = FadeElapsedSeconds >= FadeDurationSeconds;
	if (IsValid(RegisterButton))
	{
		RegisterButton->SetIsEnabled(bFadeFinished && !bBusy && !bSinglePlayer &&
			Result.bSuccess && Result.bLeaderboardVerificationReady);
	}
	if (IsValid(LeaderboardButton)) LeaderboardButton->SetIsEnabled(bFadeFinished && !bBusy);
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->SetIsEnabled(bFadeFinished && !bBusy);
	if (IsValid(ReturnToTitleButton)) ReturnToTitleButton->SetIsEnabled(bFadeFinished && !bBusy);
	if (IsValid(LogoutButton)) LogoutButton->SetIsEnabled(bFadeFinished && !bBusy);
	if (IsValid(StatusText) && !Message.IsEmpty())
	{
		StatusText->SetText(FProjectProject01Localization::Text(Message));
		StatusText->SetColorAndOpacity(bIsError
			? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::White));
	}
}

void UProjectProject01MatchResultWidget::BuildWidgetTree()
{
	if (!ensureMsgf(IsValid(WidgetTree), TEXT("Match result widget has no WidgetTree.")) ||
		IsValid(WidgetTree->RootWidget))
	{
		return;
	}

	UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Overlay;
	FadeBackground = WidgetTree->ConstructWidget<UBorder>();
	FadeBackground->SetBrushColor(FLinearColor::Transparent);
	if (UOverlaySlot* BackgroundSlot = Overlay->AddChildToOverlay(FadeBackground))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ResultMenu = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UOverlaySlot* MenuSlot = Overlay->AddChildToOverlay(ResultMenu))
	{
		MenuSlot->SetHorizontalAlignment(HAlign_Center);
		MenuSlot->SetVerticalAlignment(VAlign_Center);
	}
	ResultText = WidgetTree->ConstructWidget<UTextBlock>();
	ResultText->SetText(FProjectProject01Localization::Text(TEXT("결과를 불러오는 중...")));
	ResultText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ResultText->SetJustification(ETextJustify::Center);
	FSlateFontInfo ResultFont = ResultText->GetFont();
	ResultFont.Size = 36;
	ResultText->SetFont(ResultFont);
	ResultMenu->AddChildToVerticalBox(ResultText)->SetPadding(FMargin(12.0f));

	StatusText = WidgetTree->ConstructWidget<UTextBlock>();
	StatusText->SetText(FProjectProject01Localization::Text(TEXT("결과를 처리하는 중...")));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	StatusText->SetJustification(ETextJustify::Center);
	ResultMenu->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(8.0f));

	RegisterButton = ProjectProject01LoginUI::AddButton(WidgetTree, ResultMenu, TEXT("성공 기록을 리더보드에 등록"));
	LeaderboardButton = ProjectProject01LoginUI::AddButton(WidgetTree, ResultMenu, TEXT("리더보드"));
	ReturnToRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, ResultMenu, TEXT("방으로 돌아가기"));
	LogoutButton = ProjectProject01LoginUI::AddButton(WidgetTree, ResultMenu, TEXT("로그아웃"));
	ReturnToTitleButton = ProjectProject01LoginUI::AddButton(WidgetTree, ResultMenu, TEXT("타이틀로 돌아가기"));
}

void UProjectProject01SettingsWidget::ConfigureReturnWidget(UUserWidget* InReturnWidget)
{
	ReturnWidget = InReturnWidget;
}

bool UProjectProject01SettingsWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	SetIsFocusable(true);
	BuildWidgetTree();
	return true;
}

void UProjectProject01SettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	LoadPendingFromSettings();
	RefreshAllControls();

	MasterVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleMasterVolumeChanged);
	SFXVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleSFXVolumeChanged);
	MusicVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleMusicVolumeChanged);
	UIVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleUIVolumeChanged);
	FrameRateSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleFrameRateChanged);
	ResolutionScaleSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleResolutionScaleChanged);
	BrightnessSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleBrightnessChanged);
	SensitivitySlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleSensitivityChanged);
	ColorVisionSeveritySlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleColorVisionSeverityChanged);
	ScreenFlashSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleScreenFlashChanged);
	ScreenDistortionSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleScreenDistortionChanged);
	ScreenShakeSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleScreenShakeChanged);
	UIReadableScaleSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleUIReadableScaleChanged);
	MuteAllCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleMuteAllChanged);
	MuteWhenUnfocusedCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleMuteWhenUnfocusedChanged);
	VSyncCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleVSyncChanged);
	MotionBlurCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleMotionBlurChanged);
	SubtitlesCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleSubtitlesChanged);
	EnhancedVisualCuesCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleEnhancedVisualCuesChanged);
	WindowModeComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleWindowModeChanged);
	ResolutionComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleResolutionChanged);
	OverallQualityComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleOverallQualityChanged);
	AAQualityComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleAAQualityChanged);
	ShadowQualityComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleShadowQualityChanged);
	TextureQualityComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleTextureQualityChanged);
	EffectsQualityComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleEffectsQualityChanged);
	PostProcessQualityComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandlePostProcessQualityChanged);
	VFXIntensityComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleVFXIntensityChanged);
	ColorVisionModeComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleColorVisionModeChanged);
	ForwardKeyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleForwardBindingClicked);
	BackwardKeyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleBackwardBindingClicked);
	LeftKeyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleLeftBindingClicked);
	RightKeyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleRightBindingClicked);
	SprintKeyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleSprintBindingClicked);
	VoicePushToTalkKeyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleVoicePushToTalkBindingClicked);
	VoiceToggleKeyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleVoiceToggleBindingClicked);
	HelpPingKeyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleHelpPingBindingClicked);
	DangerPingKeyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleDangerPingBindingClicked);
	LocationPingKeyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleLocationPingBindingClicked);
	ApplyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleApplyClicked);
	CancelButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleCancelClicked);
	DefaultsButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleDefaultsClicked);
	ConfirmVideoButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleConfirmVideoClicked);
	RevertVideoButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleRevertVideoClicked);
	RestoreSettingsInputFocus();
}

void UProjectProject01SettingsWidget::NativeDestruct()
{
	if (IsValid(MasterVolumeSlider)) MasterVolumeSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleMasterVolumeChanged);
	if (IsValid(SFXVolumeSlider)) SFXVolumeSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleSFXVolumeChanged);
	if (IsValid(MusicVolumeSlider)) MusicVolumeSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleMusicVolumeChanged);
	if (IsValid(UIVolumeSlider)) UIVolumeSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleUIVolumeChanged);
	if (IsValid(FrameRateSlider)) FrameRateSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleFrameRateChanged);
	if (IsValid(ResolutionScaleSlider)) ResolutionScaleSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleResolutionScaleChanged);
	if (IsValid(BrightnessSlider)) BrightnessSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleBrightnessChanged);
	if (IsValid(SensitivitySlider)) SensitivitySlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleSensitivityChanged);
	if (IsValid(ColorVisionSeveritySlider)) ColorVisionSeveritySlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleColorVisionSeverityChanged);
	if (IsValid(ScreenFlashSlider)) ScreenFlashSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleScreenFlashChanged);
	if (IsValid(ScreenDistortionSlider)) ScreenDistortionSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleScreenDistortionChanged);
	if (IsValid(ScreenShakeSlider)) ScreenShakeSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleScreenShakeChanged);
	if (IsValid(UIReadableScaleSlider)) UIReadableScaleSlider->OnValueChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleUIReadableScaleChanged);
	if (IsValid(MuteAllCheckBox)) MuteAllCheckBox->OnCheckStateChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleMuteAllChanged);
	if (IsValid(MuteWhenUnfocusedCheckBox)) MuteWhenUnfocusedCheckBox->OnCheckStateChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleMuteWhenUnfocusedChanged);
	if (IsValid(VSyncCheckBox)) VSyncCheckBox->OnCheckStateChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleVSyncChanged);
	if (IsValid(MotionBlurCheckBox)) MotionBlurCheckBox->OnCheckStateChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleMotionBlurChanged);
	if (IsValid(SubtitlesCheckBox)) SubtitlesCheckBox->OnCheckStateChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleSubtitlesChanged);
	if (IsValid(EnhancedVisualCuesCheckBox)) EnhancedVisualCuesCheckBox->OnCheckStateChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleEnhancedVisualCuesChanged);
	if (IsValid(WindowModeComboBox)) WindowModeComboBox->OnSelectionChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleWindowModeChanged);
	if (IsValid(ResolutionComboBox)) ResolutionComboBox->OnSelectionChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleResolutionChanged);
	if (IsValid(OverallQualityComboBox)) OverallQualityComboBox->OnSelectionChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleOverallQualityChanged);
	if (IsValid(AAQualityComboBox)) AAQualityComboBox->OnSelectionChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleAAQualityChanged);
	if (IsValid(ShadowQualityComboBox)) ShadowQualityComboBox->OnSelectionChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleShadowQualityChanged);
	if (IsValid(TextureQualityComboBox)) TextureQualityComboBox->OnSelectionChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleTextureQualityChanged);
	if (IsValid(EffectsQualityComboBox)) EffectsQualityComboBox->OnSelectionChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleEffectsQualityChanged);
	if (IsValid(PostProcessQualityComboBox)) PostProcessQualityComboBox->OnSelectionChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandlePostProcessQualityChanged);
	if (IsValid(VFXIntensityComboBox)) VFXIntensityComboBox->OnSelectionChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleVFXIntensityChanged);
	if (IsValid(ColorVisionModeComboBox)) ColorVisionModeComboBox->OnSelectionChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleColorVisionModeChanged);
	if (IsValid(ForwardKeyButton)) ForwardKeyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleForwardBindingClicked);
	if (IsValid(BackwardKeyButton)) BackwardKeyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleBackwardBindingClicked);
	if (IsValid(LeftKeyButton)) LeftKeyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleLeftBindingClicked);
	if (IsValid(RightKeyButton)) RightKeyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleRightBindingClicked);
	if (IsValid(SprintKeyButton)) SprintKeyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleSprintBindingClicked);
	if (IsValid(VoicePushToTalkKeyButton)) VoicePushToTalkKeyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleVoicePushToTalkBindingClicked);
	if (IsValid(VoiceToggleKeyButton)) VoiceToggleKeyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleVoiceToggleBindingClicked);
	if (IsValid(HelpPingKeyButton)) HelpPingKeyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleHelpPingBindingClicked);
	if (IsValid(DangerPingKeyButton)) DangerPingKeyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleDangerPingBindingClicked);
	if (IsValid(LocationPingKeyButton)) LocationPingKeyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleLocationPingBindingClicked);
	if (IsValid(ApplyButton)) ApplyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleApplyClicked);
	if (IsValid(CancelButton)) CancelButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleCancelClicked);
	if (IsValid(DefaultsButton)) DefaultsButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleDefaultsClicked);
	if (IsValid(ConfirmVideoButton)) ConfirmVideoButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleConfirmVideoClicked);
	if (IsValid(RevertVideoButton)) RevertVideoButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleRevertVideoClicked);
	Super::NativeDestruct();
}

void UProjectProject01SettingsWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bAwaitingVideoConfirmation)
	{
		return;
	}
	VideoConfirmationSecondsRemaining -= InDeltaTime;
	if (VideoConfirmationSecondsRemaining <= 0.0f)
	{
		RevertPendingVideoMode();
		return;
	}
	if (IsValid(VideoConfirmText))
	{
		VideoConfirmText->SetText(FText::FromString(FString::Printf(
			TEXT("이 화면 설정을 유지하시겠습니까? %.0f초 후 자동 복구됩니다."),
			FMath::CeilToFloat(VideoConfirmationSecondsRemaining))));
	}
}

FReply UProjectProject01SettingsWidget::NativeOnKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (BindingTarget != EBindingTarget::None)
	{
		if (Key == EKeys::Escape)
		{
			BindingTarget = EBindingTarget::None;
			RefreshBindingLabels();
			SetStatus(TEXT("키 변경을 취소했습니다."));
			return FReply::Handled();
		}
		TryAssignBinding(Key);
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		HandleCancelClicked();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UProjectProject01SettingsWidget::HandleMasterVolumeChanged(const float Value) { if (!bRefreshingControls) { PendingMasterVolume = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleSFXVolumeChanged(const float Value) { if (!bRefreshingControls) { PendingSFXVolume = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleMusicVolumeChanged(const float Value) { if (!bRefreshingControls) { PendingMusicVolume = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleUIVolumeChanged(const float Value) { if (!bRefreshingControls) { PendingUIVolume = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleFrameRateChanged(const float Value) { if (!bRefreshingControls) { PendingFrameRate = FMath::RoundToFloat(Value); RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleResolutionScaleChanged(const float Value) { if (!bRefreshingControls) { PendingResolutionScale = FMath::RoundToFloat(Value); RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleBrightnessChanged(const float Value) { if (!bRefreshingControls) { PendingBrightness = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleSensitivityChanged(const float Value) { if (!bRefreshingControls) { PendingSensitivity = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleColorVisionSeverityChanged(const float Value) { if (!bRefreshingControls) { PendingColorVisionSeverity = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleScreenFlashChanged(const float Value) { if (!bRefreshingControls) { PendingScreenFlashScale = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleScreenDistortionChanged(const float Value) { if (!bRefreshingControls) { PendingScreenDistortionScale = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleScreenShakeChanged(const float Value) { if (!bRefreshingControls) { PendingScreenShakeScale = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleUIReadableScaleChanged(const float Value) { if (!bRefreshingControls) { PendingUIReadableScale = Value; RefreshValueLabels(); } }
void UProjectProject01SettingsWidget::HandleMuteAllChanged(const bool bChecked) { if (!bRefreshingControls) bPendingMuteAll = bChecked; }
void UProjectProject01SettingsWidget::HandleMuteWhenUnfocusedChanged(const bool bChecked) { if (!bRefreshingControls) bPendingMuteWhenUnfocused = bChecked; }
void UProjectProject01SettingsWidget::HandleVSyncChanged(const bool bChecked) { if (!bRefreshingControls) bPendingVSync = bChecked; }
void UProjectProject01SettingsWidget::HandleMotionBlurChanged(const bool bChecked) { if (!bRefreshingControls) bPendingMotionBlur = bChecked; }
void UProjectProject01SettingsWidget::HandleSubtitlesChanged(const bool bChecked) { if (!bRefreshingControls) bPendingSubtitles = bChecked; }
void UProjectProject01SettingsWidget::HandleEnhancedVisualCuesChanged(const bool bChecked) { if (!bRefreshingControls) bPendingEnhancedVisualCues = bChecked; }

void UProjectProject01SettingsWidget::HandleWindowModeChanged(const FString Item, ESelectInfo::Type)
{
	if (bRefreshingControls) return;
	if (Item == TEXT("전체화면")) PendingWindowMode = EWindowMode::Fullscreen;
	else if (Item == TEXT("테두리 없는 창")) PendingWindowMode = EWindowMode::WindowedFullscreen;
	else PendingWindowMode = EWindowMode::Windowed;
}

void UProjectProject01SettingsWidget::HandleResolutionChanged(const FString Item, ESelectInfo::Type)
{
	if (!bRefreshingControls) ParseResolution(Item, PendingResolution);
}

void UProjectProject01SettingsWidget::HandleOverallQualityChanged(const FString Item, ESelectInfo::Type)
{
	if (bRefreshingControls) return;
	const int32 Quality = QualityFromString(Item);
	PendingOverallQuality = Quality;
	if (Quality >= 0)
	{
		PendingAAQuality = Quality;
		PendingShadowQuality = Quality;
		PendingTextureQuality = Quality;
		PendingEffectsQuality = Quality;
		PendingPostProcessQuality = Quality;
		RefreshAllControls();
	}
}

void UProjectProject01SettingsWidget::HandleAAQualityChanged(const FString Item, ESelectInfo::Type) { if (!bRefreshingControls) { PendingAAQuality = QualityFromString(Item); PendingOverallQuality = -1; OverallQualityComboBox->SetSelectedOption(TEXT("사용자 지정")); } }
void UProjectProject01SettingsWidget::HandleShadowQualityChanged(const FString Item, ESelectInfo::Type) { if (!bRefreshingControls) { PendingShadowQuality = QualityFromString(Item); PendingOverallQuality = -1; OverallQualityComboBox->SetSelectedOption(TEXT("사용자 지정")); } }
void UProjectProject01SettingsWidget::HandleTextureQualityChanged(const FString Item, ESelectInfo::Type) { if (!bRefreshingControls) { PendingTextureQuality = QualityFromString(Item); PendingOverallQuality = -1; OverallQualityComboBox->SetSelectedOption(TEXT("사용자 지정")); } }
void UProjectProject01SettingsWidget::HandleEffectsQualityChanged(const FString Item, ESelectInfo::Type) { if (!bRefreshingControls) { PendingEffectsQuality = QualityFromString(Item); PendingOverallQuality = -1; OverallQualityComboBox->SetSelectedOption(TEXT("사용자 지정")); } }
void UProjectProject01SettingsWidget::HandlePostProcessQualityChanged(const FString Item, ESelectInfo::Type) { if (!bRefreshingControls) { PendingPostProcessQuality = QualityFromString(Item); PendingOverallQuality = -1; OverallQualityComboBox->SetSelectedOption(TEXT("사용자 지정")); } }

void UProjectProject01SettingsWidget::HandleVFXIntensityChanged(const FString Item, ESelectInfo::Type)
{
	if (!bRefreshingControls)
	{
		PendingVFXIntensity = Item == TEXT("약하게")
			? EProjectProject01VFXIntensity::Reduced : EProjectProject01VFXIntensity::Standard;
	}
}

void UProjectProject01SettingsWidget::HandleColorVisionModeChanged(const FString Item, ESelectInfo::Type)
{
	if (bRefreshingControls) return;
	if (Item == TEXT("녹색약 보정")) PendingColorVisionMode = EProjectProject01ColorVisionMode::Deuteranopia;
	else if (Item == TEXT("적색약 보정")) PendingColorVisionMode = EProjectProject01ColorVisionMode::Protanopia;
	else if (Item == TEXT("청황색약 보정")) PendingColorVisionMode = EProjectProject01ColorVisionMode::Tritanopia;
	else PendingColorVisionMode = EProjectProject01ColorVisionMode::Normal;
}

void UProjectProject01SettingsWidget::HandleForwardBindingClicked() { BeginBindingCapture(EBindingTarget::Forward); }
void UProjectProject01SettingsWidget::HandleBackwardBindingClicked() { BeginBindingCapture(EBindingTarget::Backward); }
void UProjectProject01SettingsWidget::HandleLeftBindingClicked() { BeginBindingCapture(EBindingTarget::Left); }
void UProjectProject01SettingsWidget::HandleRightBindingClicked() { BeginBindingCapture(EBindingTarget::Right); }
void UProjectProject01SettingsWidget::HandleSprintBindingClicked() { BeginBindingCapture(EBindingTarget::Sprint); }
void UProjectProject01SettingsWidget::HandleVoicePushToTalkBindingClicked() { BeginBindingCapture(EBindingTarget::VoicePushToTalk); }
void UProjectProject01SettingsWidget::HandleVoiceToggleBindingClicked() { BeginBindingCapture(EBindingTarget::VoiceToggle); }
void UProjectProject01SettingsWidget::HandleHelpPingBindingClicked() { BeginBindingCapture(EBindingTarget::HelpPing); }
void UProjectProject01SettingsWidget::HandleDangerPingBindingClicked() { BeginBindingCapture(EBindingTarget::DangerPing); }
void UProjectProject01SettingsWidget::HandleLocationPingBindingClicked() { BeginBindingCapture(EBindingTarget::LocationPing); }
void UProjectProject01SettingsWidget::HandleApplyClicked() { ApplyPendingSettings(); }

void UProjectProject01SettingsWidget::HandleCancelClicked()
{
	if (bAwaitingVideoConfirmation) RevertPendingVideoMode();
	CloseToReturnWidget();
}

void UProjectProject01SettingsWidget::HandleDefaultsClicked()
{
	SetPendingDefaults();
	RefreshAllControls();
	SetStatus(TEXT("기본값을 불러왔습니다. 적용 버튼을 눌러 저장하세요."));
}

void UProjectProject01SettingsWidget::HandleConfirmVideoClicked() { ConfirmPendingVideoMode(); }
void UProjectProject01SettingsWidget::HandleRevertVideoClicked() { RevertPendingVideoMode(); }

void UProjectProject01SettingsWidget::LoadPendingFromSettings()
{
	UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get();
	if (!IsValid(Settings))
	{
		SetStatus(TEXT("사용자 설정 객체를 찾지 못했습니다."), true);
		SetPendingDefaults();
		return;
	}
	PendingMasterVolume = Settings->GetMasterVolume();
	PendingSFXVolume = Settings->GetSFXVolume();
	PendingMusicVolume = Settings->GetMusicVolume();
	PendingUIVolume = Settings->GetUIVolume();
	bPendingMuteAll = Settings->IsMuteAllEnabled();
	bPendingMuteWhenUnfocused = Settings->IsMuteWhenUnfocusedEnabled();
	bPendingVSync = Settings->IsVSyncEnabled();
	bPendingMotionBlur = Settings->IsMotionBlurEnabled();
	PendingFrameRate = FMath::Clamp(Settings->GetFrameRateLimit() <= 0.0f ? 240.0f : Settings->GetFrameRateLimit(), 30.0f, 240.0f);
	float Normalized = 0.0f, CurrentScale = 100.0f, MinScale = 50.0f, MaxScale = 100.0f;
	Settings->GetResolutionScaleInformationEx(Normalized, CurrentScale, MinScale, MaxScale);
	PendingResolutionScale = FMath::Clamp(CurrentScale, 50.0f, 100.0f);
	PendingBrightness = Settings->GetDisplayGammaSetting();
	PendingSensitivity = Settings->GetMouseSensitivity();
	bPendingSubtitles = Settings->AreSubtitlesEnabled();
	bPendingEnhancedVisualCues = Settings->AreEnhancedVisualCuesEnabled();
	PendingColorVisionMode = Settings->GetColorVisionMode();
	PendingColorVisionSeverity = Settings->GetColorVisionSeverity();
	PendingScreenFlashScale = Settings->GetScreenFlashScale();
	PendingScreenDistortionScale = Settings->GetScreenDistortionScale();
	PendingScreenShakeScale = Settings->GetScreenShakeScale();
	PendingUIReadableScale = Settings->GetUIReadableScale();
	PendingWindowMode = Settings->GetFullscreenMode();
	PendingResolution = Settings->GetScreenResolution();
	PendingOverallQuality = Settings->GetOverallScalabilityLevel();
	PendingAAQuality = Settings->GetAntiAliasingQuality();
	PendingShadowQuality = Settings->GetShadowQuality();
	PendingTextureQuality = Settings->GetTextureQuality();
	PendingEffectsQuality = Settings->GetVisualEffectQuality();
	PendingPostProcessQuality = Settings->GetPostProcessingQuality();
	PendingVFXIntensity = Settings->GetVFXIntensity();
	PendingForwardKey = Settings->GetMoveForwardKey();
	PendingBackwardKey = Settings->GetMoveBackwardKey();
	PendingLeftKey = Settings->GetMoveLeftKey();
	PendingRightKey = Settings->GetMoveRightKey();
	PendingSprintKey = Settings->GetSprintKey();
	PendingVoicePushToTalkKey = Settings->GetVoicePushToTalkKey();
	PendingVoiceToggleKey = Settings->GetVoiceToggleKey();
	PendingHelpPingKey = Settings->GetHelpPingKey();
	PendingDangerPingKey = Settings->GetDangerPingKey();
	PendingLocationPingKey = Settings->GetLocationPingKey();
}

void UProjectProject01SettingsWidget::SetPendingDefaults()
{
	PendingMasterVolume = PendingSFXVolume = PendingMusicVolume = PendingUIVolume = 1.0f;
	bPendingMuteAll = false;
	bPendingMuteWhenUnfocused = true;
	bPendingVSync = false;
	bPendingMotionBlur = true;
	PendingFrameRate = 120.0f;
	PendingResolutionScale = 100.0f;
	PendingBrightness = 2.2f;
	PendingSensitivity = 1.0f;
	bPendingSubtitles = true;
	bPendingEnhancedVisualCues = true;
	PendingColorVisionMode = EProjectProject01ColorVisionMode::Normal;
	PendingColorVisionSeverity = 5.0f;
	PendingScreenFlashScale = 1.0f;
	PendingScreenDistortionScale = 1.0f;
	PendingScreenShakeScale = 1.0f;
	PendingUIReadableScale = 1.0f;
	PendingWindowMode = EWindowMode::WindowedFullscreen;
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
	{
		PendingResolution = Settings->GetDesktopResolution();
	}
	PendingOverallQuality = PendingAAQuality = PendingShadowQuality = PendingTextureQuality =
		PendingEffectsQuality = PendingPostProcessQuality = 3;
	PendingVFXIntensity = EProjectProject01VFXIntensity::Standard;
	PendingForwardKey = EKeys::W;
	PendingBackwardKey = EKeys::S;
	PendingLeftKey = EKeys::A;
	PendingRightKey = EKeys::D;
	PendingSprintKey = EKeys::LeftShift;
	PendingVoicePushToTalkKey = EKeys::F2;
	PendingVoiceToggleKey = EKeys::U;
	PendingHelpPingKey = EKeys::Z;
	PendingDangerPingKey = EKeys::X;
	PendingLocationPingKey = EKeys::C;
}

void UProjectProject01SettingsWidget::RefreshAllControls()
{
	bRefreshingControls = true;
	MasterVolumeSlider->SetValue(PendingMasterVolume);
	SFXVolumeSlider->SetValue(PendingSFXVolume);
	MusicVolumeSlider->SetValue(PendingMusicVolume);
	UIVolumeSlider->SetValue(PendingUIVolume);
	FrameRateSlider->SetValue(PendingFrameRate);
	ResolutionScaleSlider->SetValue(PendingResolutionScale);
	BrightnessSlider->SetValue(PendingBrightness);
	SensitivitySlider->SetValue(PendingSensitivity);
	ColorVisionSeveritySlider->SetValue(PendingColorVisionSeverity);
	ScreenFlashSlider->SetValue(PendingScreenFlashScale);
	ScreenDistortionSlider->SetValue(PendingScreenDistortionScale);
	ScreenShakeSlider->SetValue(PendingScreenShakeScale);
	UIReadableScaleSlider->SetValue(PendingUIReadableScale);
	MuteAllCheckBox->SetIsChecked(bPendingMuteAll);
	MuteWhenUnfocusedCheckBox->SetIsChecked(bPendingMuteWhenUnfocused);
	VSyncCheckBox->SetIsChecked(bPendingVSync);
	MotionBlurCheckBox->SetIsChecked(bPendingMotionBlur);
	SubtitlesCheckBox->SetIsChecked(bPendingSubtitles);
	EnhancedVisualCuesCheckBox->SetIsChecked(bPendingEnhancedVisualCues);
	WindowModeComboBox->SetSelectedOption(PendingWindowMode == EWindowMode::Fullscreen
		? TEXT("전체화면") : PendingWindowMode == EWindowMode::WindowedFullscreen
		? TEXT("테두리 없는 창") : TEXT("창모드"));
	const FString ResolutionText = FString::Printf(TEXT("%d x %d"), PendingResolution.X, PendingResolution.Y);
	if (ResolutionComboBox->FindOptionIndex(ResolutionText) == INDEX_NONE) ResolutionComboBox->AddOption(ResolutionText);
	ResolutionComboBox->SetSelectedOption(ResolutionText);
	OverallQualityComboBox->SetSelectedOption(PendingOverallQuality < 0 ? TEXT("사용자 지정") : QualityToString(PendingOverallQuality));
	SetQualityCombo(AAQualityComboBox, PendingAAQuality);
	SetQualityCombo(ShadowQualityComboBox, PendingShadowQuality);
	SetQualityCombo(TextureQualityComboBox, PendingTextureQuality);
	SetQualityCombo(EffectsQualityComboBox, PendingEffectsQuality);
	SetQualityCombo(PostProcessQualityComboBox, PendingPostProcessQuality);
	VFXIntensityComboBox->SetSelectedOption(PendingVFXIntensity == EProjectProject01VFXIntensity::Reduced
		? TEXT("약하게") : TEXT("기본"));
	switch (PendingColorVisionMode)
	{
	case EProjectProject01ColorVisionMode::Deuteranopia: ColorVisionModeComboBox->SetSelectedOption(TEXT("녹색약 보정")); break;
	case EProjectProject01ColorVisionMode::Protanopia: ColorVisionModeComboBox->SetSelectedOption(TEXT("적색약 보정")); break;
	case EProjectProject01ColorVisionMode::Tritanopia: ColorVisionModeComboBox->SetSelectedOption(TEXT("청황색약 보정")); break;
	default: ColorVisionModeComboBox->SetSelectedOption(TEXT("보정 없음")); break;
	}
	bRefreshingControls = false;
	RefreshValueLabels();
	RefreshBindingLabels();
}

void UProjectProject01SettingsWidget::RefreshValueLabels()
{
	MasterVolumeValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(PendingMasterVolume * 100.0f))));
	SFXVolumeValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(PendingSFXVolume * 100.0f))));
	MusicVolumeValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(PendingMusicVolume * 100.0f))));
	UIVolumeValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(PendingUIVolume * 100.0f))));
	FrameRateValueText->SetText(FText::AsNumber(FMath::RoundToInt(PendingFrameRate)));
	ResolutionScaleValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(PendingResolutionScale))));
	BrightnessValueText->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), PendingBrightness)));
	SensitivityValueText->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), PendingSensitivity)));
	ColorVisionSeverityValueText->SetText(FText::FromString(FString::Printf(TEXT("%.0f/10"), PendingColorVisionSeverity)));
	ScreenFlashValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(PendingScreenFlashScale * 100.0f))));
	ScreenDistortionValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(PendingScreenDistortionScale * 100.0f))));
	ScreenShakeValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(PendingScreenShakeScale * 100.0f))));
	UIReadableScaleValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(PendingUIReadableScale * 100.0f))));
}

void UProjectProject01SettingsWidget::RefreshBindingLabels()
{
	auto LabelFor = [this](const EBindingTarget Target, const FKey Key)
	{
		return BindingTarget == Target ? TEXT("키를 누르세요 (Esc 취소)") : Key.GetDisplayName().ToString();
	};
	ProjectProject01LoginUI::SetButtonText(ForwardKeyButton, LabelFor(EBindingTarget::Forward, PendingForwardKey));
	ProjectProject01LoginUI::SetButtonText(BackwardKeyButton, LabelFor(EBindingTarget::Backward, PendingBackwardKey));
	ProjectProject01LoginUI::SetButtonText(LeftKeyButton, LabelFor(EBindingTarget::Left, PendingLeftKey));
	ProjectProject01LoginUI::SetButtonText(RightKeyButton, LabelFor(EBindingTarget::Right, PendingRightKey));
	ProjectProject01LoginUI::SetButtonText(SprintKeyButton, LabelFor(EBindingTarget::Sprint, PendingSprintKey));
	ProjectProject01LoginUI::SetButtonText(VoicePushToTalkKeyButton, LabelFor(EBindingTarget::VoicePushToTalk, PendingVoicePushToTalkKey));
	ProjectProject01LoginUI::SetButtonText(VoiceToggleKeyButton, LabelFor(EBindingTarget::VoiceToggle, PendingVoiceToggleKey));
	ProjectProject01LoginUI::SetButtonText(HelpPingKeyButton, LabelFor(EBindingTarget::HelpPing, PendingHelpPingKey));
	ProjectProject01LoginUI::SetButtonText(DangerPingKeyButton, LabelFor(EBindingTarget::DangerPing, PendingDangerPingKey));
	ProjectProject01LoginUI::SetButtonText(LocationPingKeyButton, LabelFor(EBindingTarget::LocationPing, PendingLocationPingKey));
}

void UProjectProject01SettingsWidget::BeginBindingCapture(const EBindingTarget Target)
{
	BindingTarget = Target;
	RefreshBindingLabels();
	SetStatus(TEXT("새 키를 누르세요. ESC는 안전 메뉴 키로 고정되어 있습니다."));
	SetKeyboardFocus();
}

bool UProjectProject01SettingsWidget::TryAssignBinding(const FKey Key)
{
	if (!Key.IsValid() || Key.IsGamepadKey() || Key == EKeys::Escape)
	{
		SetStatus(TEXT("사용할 수 없는 키입니다. ESC는 게임 메뉴를 위해 예약되어 있습니다."), true);
		return false;
	}
	const TArray<FKey> Existing = { PendingForwardKey, PendingBackwardKey, PendingLeftKey, PendingRightKey, PendingSprintKey,
		PendingVoicePushToTalkKey, PendingVoiceToggleKey, PendingHelpPingKey, PendingDangerPingKey, PendingLocationPingKey };
	int32 CurrentIndex = static_cast<int32>(BindingTarget) - 1;
	for (int32 Index = 0; Index < Existing.Num(); ++Index)
	{
		if (Index != CurrentIndex && Existing[Index] == Key)
		{
			SetStatus(FString::Printf(TEXT("%s 키는 이미 다른 필수 행동에 사용 중입니다."), *Key.GetDisplayName().ToString()), true);
			return false;
		}
	}
	switch (BindingTarget)
	{
	case EBindingTarget::Forward: PendingForwardKey = Key; break;
	case EBindingTarget::Backward: PendingBackwardKey = Key; break;
	case EBindingTarget::Left: PendingLeftKey = Key; break;
	case EBindingTarget::Right: PendingRightKey = Key; break;
	case EBindingTarget::Sprint: PendingSprintKey = Key; break;
	case EBindingTarget::VoicePushToTalk: PendingVoicePushToTalkKey = Key; break;
	case EBindingTarget::VoiceToggle: PendingVoiceToggleKey = Key; break;
	case EBindingTarget::HelpPing: PendingHelpPingKey = Key; break;
	case EBindingTarget::DangerPing: PendingDangerPingKey = Key; break;
	case EBindingTarget::LocationPing: PendingLocationPingKey = Key; break;
	default: return false;
	}
	BindingTarget = EBindingTarget::None;
	RefreshBindingLabels();
	SetStatus(TEXT("키를 변경했습니다. 적용 버튼을 눌러 저장하세요."));
	return true;
}

void UProjectProject01SettingsWidget::ApplyPendingSettings()
{
	UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get();
	if (!IsValid(Settings))
	{
		SetStatus(TEXT("사용자 설정 객체를 찾지 못했습니다."), true);
		return;
	}
	const bool bVideoModeChanged = Settings->GetScreenResolution() != PendingResolution ||
		Settings->GetFullscreenMode() != PendingWindowMode;
	Settings->SetMasterVolume(PendingMasterVolume);
	Settings->SetSFXVolume(PendingSFXVolume);
	Settings->SetMusicVolume(PendingMusicVolume);
	Settings->SetUIVolume(PendingUIVolume);
	Settings->SetMuteAllEnabled(bPendingMuteAll);
	Settings->SetMuteWhenUnfocusedEnabled(bPendingMuteWhenUnfocused);
	Settings->SetVSyncEnabled(bPendingVSync);
	Settings->SetMotionBlurEnabled(bPendingMotionBlur);
	Settings->SetFrameRateLimit(PendingFrameRate);
	Settings->SetResolutionScaleValueEx(PendingResolutionScale);
	Settings->SetDisplayGammaSetting(PendingBrightness);
	Settings->SetMouseSensitivity(PendingSensitivity);
	Settings->SetSubtitlesEnabled(bPendingSubtitles);
	Settings->SetEnhancedVisualCuesEnabled(bPendingEnhancedVisualCues);
	Settings->SetColorVisionMode(PendingColorVisionMode);
	Settings->SetColorVisionSeverity(PendingColorVisionSeverity);
	Settings->SetScreenFlashScale(PendingScreenFlashScale);
	Settings->SetScreenDistortionScale(PendingScreenDistortionScale);
	Settings->SetScreenShakeScale(PendingScreenShakeScale);
	Settings->SetUIReadableScale(PendingUIReadableScale);
	if (PendingOverallQuality >= 0)
	{
		Settings->SetOverallScalabilityLevel(PendingOverallQuality);
	}
	Settings->SetAntiAliasingQuality(PendingAAQuality);
	Settings->SetShadowQuality(PendingShadowQuality);
	Settings->SetTextureQuality(PendingTextureQuality);
	Settings->SetVisualEffectQuality(PendingEffectsQuality);
	Settings->SetPostProcessingQuality(PendingPostProcessQuality);
	Settings->SetVFXIntensity(PendingVFXIntensity);
	Settings->SetMoveForwardKey(PendingForwardKey);
	Settings->SetMoveBackwardKey(PendingBackwardKey);
	Settings->SetMoveLeftKey(PendingLeftKey);
	Settings->SetMoveRightKey(PendingRightKey);
	Settings->SetSprintKey(PendingSprintKey);
	Settings->SetVoicePushToTalkKey(PendingVoicePushToTalkKey);
	Settings->SetVoiceToggleKey(PendingVoiceToggleKey);
	Settings->SetHelpPingKey(PendingHelpPingKey);
	Settings->SetDangerPingKey(PendingDangerPingKey);
	Settings->SetLocationPingKey(PendingLocationPingKey);
	Settings->SetScreenResolution(PendingResolution);
	Settings->SetFullscreenMode(PendingWindowMode);
	Settings->ApplyNonResolutionSettings();
	RefreshLocalPlayerInput();
	if (bVideoModeChanged)
	{
		Settings->ApplyResolutionSettings(false);
		bAwaitingVideoConfirmation = true;
		VideoConfirmationSecondsRemaining = 15.0f;
		VideoConfirmPanel->SetVisibility(ESlateVisibility::Visible);
		SetStatus(TEXT("화면 설정을 확인해 주세요."));
		RestoreSettingsInputFocus();
		return;
	}
	Settings->SaveSettings();
	SetStatus(TEXT("환경설정을 적용하고 저장했습니다."));
	RestoreSettingsInputFocus();
}

void UProjectProject01SettingsWidget::ConfirmPendingVideoMode()
{
	if (!bAwaitingVideoConfirmation) return;
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
	{
		Settings->ConfirmVideoMode();
		Settings->SaveSettings();
	}
	bAwaitingVideoConfirmation = false;
	VideoConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
	SetStatus(TEXT("화면 설정을 유지하고 저장했습니다."));
	RestoreSettingsInputFocus();
}

void UProjectProject01SettingsWidget::RevertPendingVideoMode()
{
	if (!bAwaitingVideoConfirmation) return;
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
	{
		Settings->RevertVideoMode();
		Settings->ApplyResolutionSettings(false);
		Settings->SaveSettings();
		PendingResolution = Settings->GetScreenResolution();
		PendingWindowMode = Settings->GetFullscreenMode();
	}
	bAwaitingVideoConfirmation = false;
	VideoConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
	RefreshAllControls();
	SetStatus(TEXT("이전 화면 설정으로 자동 복구했습니다."));
	RestoreSettingsInputFocus();
}

void UProjectProject01SettingsWidget::CloseToReturnWidget()
{
	BindingTarget = EBindingTarget::None;
	RemoveFromParent();
	if (IsValid(ReturnWidget))
	{
		ReturnWidget->SetVisibility(ESlateVisibility::Visible);
		ReturnWidget->SetKeyboardFocus();
		if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
		{
			PlayerController->bShowMouseCursor = true;
			FInputModeGameAndUI InputMode;
			InputMode.SetWidgetToFocus(ReturnWidget->TakeWidget());
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			InputMode.SetHideCursorDuringCapture(false);
			PlayerController->SetInputMode(InputMode);
		}
	}
}

void UProjectProject01SettingsWidget::SetStatus(const FString& Message, const bool bIsError)
{
	if (IsValid(StatusText))
	{
		StatusText->SetText(FProjectProject01Localization::Text(Message));
		StatusText->SetColorAndOpacity(bIsError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::Black));
	}
}

void UProjectProject01SettingsWidget::SetQualityCombo(UComboBoxString* Combo, const int32 Quality)
{
	if (IsValid(Combo)) Combo->SetSelectedOption(QualityToString(Quality));
}

int32 UProjectProject01SettingsWidget::QualityFromString(const FString& Item) const
{
	if (Item == TEXT("낮음")) return 0;
	if (Item == TEXT("중간")) return 1;
	if (Item == TEXT("높음")) return 2;
	if (Item == TEXT("매우 높음")) return 3;
	return -1;
}

FString UProjectProject01SettingsWidget::QualityToString(const int32 Quality) const
{
	switch (FMath::Clamp(Quality, 0, 3))
	{
	case 0: return TEXT("낮음");
	case 1: return TEXT("중간");
	case 2: return TEXT("높음");
	default: return TEXT("매우 높음");
	}
}

bool UProjectProject01SettingsWidget::ParseResolution(const FString& Item, FIntPoint& OutResolution) const
{
	FString Left, Right;
	if (!Item.Split(TEXT("x"), &Left, &Right)) return false;
	Left.TrimStartAndEndInline();
	Right.TrimStartAndEndInline();
	const int32 X = FCString::Atoi(*Left);
	const int32 Y = FCString::Atoi(*Right);
	if (X < 640 || Y < 480) return false;
	OutResolution = FIntPoint(X, Y);
	return true;
}

void UProjectProject01SettingsWidget::RefreshLocalPlayerInput() const
{
	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		if (APlayerCharacter* Player = Cast<APlayerCharacter>(PlayerController->GetPawn()); IsValid(Player))
		{
			Player->ApplyUserInputSettings();
		}
	}
}

void UProjectProject01SettingsWidget::RestoreSettingsInputFocus()
{
	APlayerController* PlayerController = GetOwningPlayer();
	if (!IsValid(PlayerController))
	{
		return;
	}

	// Rebuilding the player's runtime input mappings switches the controller back to
	// GameOnly and hides the cursor. Settings is still open, so restore its UI focus.
	PlayerController->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
	SetKeyboardFocus();
}

void UProjectProject01SettingsWidget::BuildWidgetTree()
{
	if (!ensureMsgf(IsValid(WidgetTree), TEXT("Settings widget has no WidgetTree."))) return;
	UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Overlay;
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.025f, 0.96f));
	if (UOverlaySlot* BackgroundSlot = Overlay->AddChildToOverlay(Background))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Background->AddChild(Scroll);
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(Root);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("환경설정"), 30.0f);
	StatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("변경 후 적용을 눌러 저장하세요."), 14.0f);

	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("사운드"), 20.0f);
	MasterVolumeSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, Root, TEXT("전체 음량"), 0.0f, 1.0f, 0.01f, MasterVolumeValueText);
	SFXVolumeSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, Root, TEXT("효과음 음량"), 0.0f, 1.0f, 0.01f, SFXVolumeValueText);
	MusicVolumeSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, Root, TEXT("배경음악 음량"), 0.0f, 1.0f, 0.01f, MusicVolumeValueText);
	UIVolumeSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, Root, TEXT("UI 음량"), 0.0f, 1.0f, 0.01f, UIVolumeValueText);
	MuteAllCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, Root, TEXT("전체 음소거"));
	MuteWhenUnfocusedCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, Root, TEXT("게임이 백그라운드일 때 음소거"));

	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("화면 및 그래픽"), 20.0f);
	WindowModeComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, Root, TEXT("화면 모드"));
	WindowModeComboBox->AddOption(TEXT("전체화면")); WindowModeComboBox->AddOption(TEXT("테두리 없는 창")); WindowModeComboBox->AddOption(TEXT("창모드"));
	ResolutionComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, Root, TEXT("해상도"));
	TArray<FIntPoint> Resolutions;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
	for (const FIntPoint Resolution : Resolutions)
	{
		const FString Text = FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y);
		if (ResolutionComboBox->FindOptionIndex(Text) == INDEX_NONE) ResolutionComboBox->AddOption(Text);
	}
	VSyncCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, Root, TEXT("수직동기화"));
	FrameRateSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, Root, TEXT("최대 FPS"), 30.0f, 240.0f, 1.0f, FrameRateValueText);
	ResolutionScaleSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, Root, TEXT("해상도 스케일"), 50.0f, 100.0f, 1.0f, ResolutionScaleValueText);
	OverallQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, Root, TEXT("전체 그래픽 프리셋"));
	const TArray<FString> OverallOptions = {
		TEXT("낮음"), TEXT("중간"), TEXT("높음"), TEXT("매우 높음"), TEXT("사용자 지정") };
	for (const FString& Option : OverallOptions) OverallQualityComboBox->AddOption(Option);
	auto PopulateQuality = [](UComboBoxString* Combo)
	{
		Combo->AddOption(TEXT("낮음")); Combo->AddOption(TEXT("중간")); Combo->AddOption(TEXT("높음")); Combo->AddOption(TEXT("매우 높음"));
	};
	AAQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, Root, TEXT("안티앨리어싱 품질")); PopulateQuality(AAQualityComboBox);
	ShadowQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, Root, TEXT("그림자 품질")); PopulateQuality(ShadowQualityComboBox);
	TextureQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, Root, TEXT("텍스처 품질")); PopulateQuality(TextureQualityComboBox);
	EffectsQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, Root, TEXT("이펙트 품질")); PopulateQuality(EffectsQualityComboBox);
	PostProcessQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, Root, TEXT("후처리 품질")); PopulateQuality(PostProcessQualityComboBox);
	MotionBlurCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, Root, TEXT("모션 블러"));
	BrightnessSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, Root, TEXT("밝기 (안전 범위)"), 1.8f, 2.6f, 0.01f, BrightnessValueText);
	VFXIntensityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, Root, TEXT("화면 번쩍임·일렁임"));
	VFXIntensityComboBox->AddOption(TEXT("기본")); VFXIntensityComboBox->AddOption(TEXT("약하게"));

	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("접근성"), 20.0f);
	SubtitlesCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, Root, TEXT("자막 표시"));
	EnhancedVisualCuesCheckBox = ProjectProject01LoginUI::AddCheckRow(
		WidgetTree, Root, TEXT("색상 외 형태·문구로 정보 표시"));
	ColorVisionModeComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, Root, TEXT("색각 보정"));
	ColorVisionModeComboBox->AddOption(TEXT("보정 없음"));
	ColorVisionModeComboBox->AddOption(TEXT("녹색약 보정"));
	ColorVisionModeComboBox->AddOption(TEXT("적색약 보정"));
	ColorVisionModeComboBox->AddOption(TEXT("청황색약 보정"));
	ColorVisionSeveritySlider = ProjectProject01LoginUI::AddSliderRow(
		WidgetTree, Root, TEXT("색각 보정 강도"), 0.0f, 10.0f, 1.0f, ColorVisionSeverityValueText);
	ScreenFlashSlider = ProjectProject01LoginUI::AddSliderRow(
		WidgetTree, Root, TEXT("화면 번쩍임 강도"), 0.0f, 1.0f, 0.05f, ScreenFlashValueText);
	ScreenDistortionSlider = ProjectProject01LoginUI::AddSliderRow(
		WidgetTree, Root, TEXT("화면 일렁임 강도"), 0.0f, 1.0f, 0.05f, ScreenDistortionValueText);
	ScreenShakeSlider = ProjectProject01LoginUI::AddSliderRow(
		WidgetTree, Root, TEXT("화면 흔들림 강도"), 0.0f, 1.0f, 0.05f, ScreenShakeValueText);
	UIReadableScaleSlider = ProjectProject01LoginUI::AddSliderRow(
		WidgetTree, Root, TEXT("UI 크기·가독성"), 0.8f, 1.3f, 0.05f, UIReadableScaleValueText);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root,
		TEXT("자막은 자막 데이터가 있는 음성에 적용됩니다. 화면 흔들림 0%는 현재 흔들림을 즉시 중지하고 이후 효과의 기준값으로 저장합니다."), 12.0f);

	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("조작"), 20.0f);
	SensitivitySlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, Root, TEXT("마우스 감도"), 0.1f, 3.0f, 0.01f, SensitivityValueText);
	ForwardKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, Root, TEXT("앞으로 이동"));
	BackwardKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, Root, TEXT("뒤로 이동"));
	LeftKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, Root, TEXT("왼쪽 이동"));
	RightKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, Root, TEXT("오른쪽 이동"));
	SprintKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, Root, TEXT("달리기"));
	VoicePushToTalkKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, Root, TEXT("음성 채팅 누르고 말하기"));
	VoiceToggleKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, Root, TEXT("음성 채팅 상시 송신 전환"));
	HelpPingKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, Root, TEXT("도움 요청 핑"));
	DangerPingKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, Root, TEXT("위험 알림 핑"));
	LocationPingKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, Root, TEXT("위치 표시 핑"));
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("상호작용: 현재 공용 Input Action이 없어 아직 재설정할 수 없습니다. ESC 게임 메뉴는 고정입니다."), 12.0f);

	DefaultsButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("설정 초기화"));
	ApplyButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("적용"));
	CancelButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("취소 / 돌아가기"));
	VideoConfirmPanel = WidgetTree->ConstructWidget<UVerticalBox>();
	Root->AddChildToVerticalBox(VideoConfirmPanel);
	VideoConfirmText = ProjectProject01LoginUI::AddLabel(WidgetTree, VideoConfirmPanel, TEXT("이 화면 설정을 유지하시겠습니까?"), 14.0f);
	ConfirmVideoButton = ProjectProject01LoginUI::AddButton(WidgetTree, VideoConfirmPanel, TEXT("유지"));
	RevertVideoButton = ProjectProject01LoginUI::AddButton(WidgetTree, VideoConfirmPanel, TEXT("이전 설정으로 복구"));
	VideoConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01UserSettingsValidationTest,
	"ProjectProject01.Settings.ValidationAndSafeBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01UserSettingsValidationTest::RunTest(const FString& Parameters)
{
	UProjectProject01GameUserSettings* Settings = NewObject<UProjectProject01GameUserSettings>();
	TestNotNull(TEXT("Settings object can be created"), Settings);
	if (!IsValid(Settings)) return false;

	Settings->SetMasterVolume(4.0f);
	Settings->SetSFXVolume(-2.0f);
	Settings->SetDisplayGammaSetting(9.0f);
	Settings->SetMouseSensitivity(0.0f);
	Settings->SetColorVisionSeverity(99.0f);
	Settings->SetScreenFlashScale(-1.0f);
	Settings->SetScreenDistortionScale(2.0f);
	Settings->SetScreenShakeScale(-5.0f);
	Settings->SetUIReadableScale(4.0f);
	Settings->SetSprintKey(EKeys::Escape);
	Settings->ValidateProjectSettings();
	TestEqual(TEXT("Master volume is clamped"), Settings->GetMasterVolume(), 1.0f);
	TestEqual(TEXT("SFX volume is clamped"), Settings->GetSFXVolume(), 0.0f);
	TestEqual(TEXT("Brightness is clamped"), Settings->GetDisplayGammaSetting(), 2.6f);
	TestEqual(TEXT("Mouse sensitivity is clamped"), Settings->GetMouseSensitivity(), 0.1f);
	TestEqual(TEXT("Color vision severity is clamped"), Settings->GetColorVisionSeverity(), 10.0f);
	TestEqual(TEXT("Screen flash scale is clamped"), Settings->GetScreenFlashScale(), 0.0f);
	TestEqual(TEXT("Screen distortion scale is clamped"), Settings->GetScreenDistortionScale(), 1.0f);
	TestEqual(TEXT("Screen shake scale is clamped"), Settings->GetScreenShakeScale(), 0.0f);
	TestEqual(TEXT("Readable UI scale is clamped"), Settings->GetUIReadableScale(), 1.3f);
	TestTrue(TEXT("Reserved Escape restores safe sprint key"), Settings->GetSprintKey() == EKeys::LeftShift);

	Settings->SetMoveForwardKey(EKeys::Up);
	Settings->SetMoveBackwardKey(EKeys::Down);
	Settings->SetMoveLeftKey(EKeys::Left);
	Settings->SetMoveRightKey(EKeys::Right);
	Settings->SetSprintKey(EKeys::RightShift);
	Settings->ValidateProjectSettings();
	TestTrue(TEXT("Unique custom forward key remains"), Settings->GetMoveForwardKey() == EKeys::Up);
	TestTrue(TEXT("Unique custom sprint key remains"), Settings->GetSprintKey() == EKeys::RightShift);
	return true;
}
#endif

bool UProjectProject01LobbyWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	return true;
}

void UProjectProject01LobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(OpenCreateRoomButton)) OpenCreateRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleOpenCreateRoomClicked);
	if (IsValid(CancelCreateRoomButton)) CancelCreateRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleCancelCreateRoomClicked);
	if (IsValid(CreateRoomButton)) CreateRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleCreateRoomClicked);
	if (IsValid(RefreshRoomsButton)) RefreshRoomsButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleRefreshRoomsClicked);
	if (IsValid(RoomSearchInput)) RoomSearchInput->OnTextChanged.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleRoomSearchChanged);
	if (IsValid(JoinRoomButton)) JoinRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleJoinRoomClicked);
	if (IsValid(ReadyButton)) ReadyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleReadyClicked);
	if (IsValid(StartRoomButton)) StartRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleStartRoomClicked);
	if (IsValid(LeaveRoomButton)) LeaveRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLeaveRoomClicked);
	if (IsValid(TransferHostButton)) TransferHostButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleTransferHostClicked);
	if (IsValid(DeleteRoomButton)) DeleteRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleDeleteRoomClicked);
	if (IsValid(CopyJoinCodeButton)) CopyJoinCodeButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleCopyJoinCodeClicked);
	if (IsValid(SendChatButton)) SendChatButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleSendChatClicked);
	if (IsValid(OpenLeaderboardTestButton)) OpenLeaderboardTestButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleOpenLeaderboardTestClicked);
	if (IsValid(LogoutButton))
	{
		LogoutButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLogoutClicked);
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLogoutResult);
			Auth->OnServiceStatusChanged.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleServiceStatusChanged);
			if (!Auth->IsSignedIn())
			{
				SetStatus(TEXT("로그인 세션이 없습니다. 로그인 화면으로 돌아가세요."), true);
				return;
			}
			SetStatus(FString::Printf(TEXT("%s 님이 로그인했습니다."), *Auth->GetSignedInDisplayName()), false);
			Auth->CheckMultiplayerServiceStatus();
		}
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->OnRequestCompleted.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLobbyRequestResult);
			Lobby->OnRoomListChanged.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleRoomListChanged);
			Lobby->OnCurrentRoomChanged.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleCurrentRoomChanged);
			Lobby->RefreshCurrentRoom();
		}
	}

	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().SetTimer(
			LobbyRefreshTimer, this, &UProjectProject01LobbyWidget::RefreshLobbyState, 1.0f, true, 1.0f);
	}
}

void UProjectProject01LobbyWidget::NativeDestruct()
{
	if (IsValid(OpenCreateRoomButton)) OpenCreateRoomButton->OnClicked.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleOpenCreateRoomClicked);
	if (IsValid(CancelCreateRoomButton)) CancelCreateRoomButton->OnClicked.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleCancelCreateRoomClicked);
	if (IsValid(RoomSearchInput)) RoomSearchInput->OnTextChanged.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleRoomSearchChanged);
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(LobbyRefreshTimer);
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleLogoutResult);
			Auth->OnServiceStatusChanged.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleServiceStatusChanged);
		}
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->OnRequestCompleted.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleLobbyRequestResult);
			Lobby->OnRoomListChanged.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleRoomListChanged);
			Lobby->OnCurrentRoomChanged.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleCurrentRoomChanged);
		}
	}
	Super::NativeDestruct();
}

void UProjectProject01LobbyWidget::HandleOpenCreateRoomClicked()
{
	bShowingCreateRoom = true;
	ShowLobbyView(1);
}

void UProjectProject01LobbyWidget::HandleCancelCreateRoomClicked()
{
	bShowingCreateRoom = false;
	ShowLobbyView(0);
}

void UProjectProject01LobbyWidget::HandleCreateRoomClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || !IsValid(RoomNameInput) || !IsValid(PublicRoomCheckBox) || !IsValid(CreatePasswordInput))
	{
		SetStatus(TEXT("방 생성 UI 또는 로비 시스템을 찾지 못했습니다."), true);
		return;
	}
	Lobby->CreateRoom(RoomNameInput->GetText().ToString(), PublicRoomCheckBox->IsChecked(),
		CreatePasswordInput->GetText().ToString());
}

void UProjectProject01LobbyWidget::HandleRefreshRoomsClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->RefreshRooms();
		}
	}
}

void UProjectProject01LobbyWidget::HandleJoinRoomClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || !IsValid(DirectRoomCodeInput) || !IsValid(JoinPasswordInput))
	{
		SetStatus(TEXT("방 참가 UI 또는 로비 시스템을 찾지 못했습니다."), true);
		return;
	}
	FString JoinCode = DirectRoomCodeInput->GetText().ToString().TrimStartAndEnd();
	if (JoinCode.IsEmpty() && IsValid(RoomListComboBox))
	{
		const FString Selected = RoomListComboBox->GetSelectedOption();
		if (const FString* SelectedJoinCode = JoinCodeByDisplayOption.Find(Selected))
		{
			JoinCode = *SelectedJoinCode;
		}
	}
	Lobby->JoinRoom(JoinCode, JoinPasswordInput->GetText().ToString());
}

void UProjectProject01LobbyWidget::HandleReadyClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>();
			IsValid(Lobby) && Lobby->HasCurrentRoom())
		{
			Lobby->SetReady(!Lobby->GetCurrentRoom().bIsReady);
		}
	}
}

void UProjectProject01LobbyWidget::HandleStartRoomClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->StartRoom();
		}
	}
}

void UProjectProject01LobbyWidget::HandleLeaveRoomClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->LeaveRoom();
		}
	}
}

void UProjectProject01LobbyWidget::HandleTransferHostClicked()
{
	if (!IsValid(HostTransferComboBox))
	{
		SetStatus(TEXT("방장 위임 UI를 찾지 못했습니다."), true);
		return;
	}
	const FString SelectedOption = HostTransferComboBox->GetSelectedOption();
	const FString* TargetUserId = UserIdByTransferOption.Find(SelectedOption);
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || TargetUserId == nullptr)
	{
		SetStatus(TEXT("방장을 넘길 플레이어를 선택하세요."), true);
		return;
	}
	Lobby->TransferHost(*TargetUserId);
}

void UProjectProject01LobbyWidget::HandleDeleteRoomClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->DeleteRoom();
		}
	}
}

void UProjectProject01LobbyWidget::HandleCopyJoinCodeClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || !Lobby->HasCurrentRoom() || Lobby->GetCurrentRoom().JoinCode.IsEmpty())
	{
		SetStatus(TEXT("복사할 참가 코드가 없습니다."), true);
		return;
	}
	FPlatformApplicationMisc::ClipboardCopy(*Lobby->GetCurrentRoom().JoinCode);
	SetStatus(FString::Printf(TEXT("참가 코드 %s를 복사했습니다."), *Lobby->GetCurrentRoom().JoinCode), false);
}

void UProjectProject01LobbyWidget::HandleSendChatClicked()
{
	if (!IsValid(ChatInput))
	{
		return;
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->SendChat(ChatInput->GetText().ToString());
		}
	}
}

void UProjectProject01LobbyWidget::HandleLogoutClicked()
{
	if (IsValid(LogoutButton))
	{
		LogoutButton->SetIsEnabled(false);
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (IsValid(Lobby) && Lobby->HasCurrentRoom())
	{
		bLogoutAfterLeave = true;
		SetStatus(TEXT("방에서 나온 뒤 로그아웃합니다..."), false);
		Lobby->LeaveRoom();
		return;
	}
	BeginLogout();
}

void UProjectProject01LobbyWidget::HandleOpenLeaderboardTestClicked()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LeaderBoardTest")));
}

void UProjectProject01LobbyWidget::BeginLogout()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	if (!IsValid(Auth))
	{
		SetStatus(TEXT("인증 시스템을 찾지 못했습니다."), true);
		if (IsValid(LogoutButton)) LogoutButton->SetIsEnabled(true);
		return;
	}
	SetStatus(TEXT("로그아웃 중..."), false);
	Auth->Logout();
}

void UProjectProject01LobbyWidget::HandleLogoutResult(
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	SetStatus(Message, !bSuccess);
	if (bSuccess)
	{
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LoginLevel")));
		return;
	}
	if (IsValid(LogoutButton))
	{
		LogoutButton->SetIsEnabled(true);
	}
}

void UProjectProject01LobbyWidget::HandleServiceStatusChanged(
	const bool bMaintenanceEnabled,
	const FString& Announcement,
	const FString& ShutdownAtUtc,
	const FString& Message)
{
	if (bMaintenanceEnabled)
	{
		SetStatus(Message.IsEmpty() ? TEXT("멀티플레이 서버 점검 중입니다.") : Message, true);
	}
	else if (!Announcement.IsEmpty())
	{
		SetStatus(ShutdownAtUtc.IsEmpty() ? Announcement :
			FString::Printf(TEXT("%s (서버 종료 예정: %s)"), *Announcement, *ShutdownAtUtc), false);
	}
}

void UProjectProject01LobbyWidget::HandleLobbyRequestResult(
	const EProjectProject01LobbyOperation Operation,
	const bool bSuccess,
	const FString& Message)
{
	if (Operation != EProjectProject01LobbyOperation::RefreshRooms &&
		Operation != EProjectProject01LobbyOperation::RefreshCurrentRoom)
	{
		SetStatus(Message, !bSuccess);
	}
	if (bSuccess && Operation == EProjectProject01LobbyOperation::SendChat && IsValid(ChatInput))
	{
		ChatInput->SetText(FText::GetEmpty());
	}
	if (bSuccess && Operation == EProjectProject01LobbyOperation::RequestGameTicket)
	{
		UGameInstance* GameInstance = GetGameInstance();
		const UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
			? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
		if (IsValid(Lobby) && Lobby->HasCurrentRoom())
		{
			TravelToStartedGame(Lobby->GetCurrentRoom());
		}
	}
	if (Operation == EProjectProject01LobbyOperation::LeaveRoom)
	{
		if (bSuccess && bLogoutAfterLeave)
		{
			bLogoutAfterLeave = false;
			BeginLogout();
			return;
		}
		if (!bSuccess)
		{
			bLogoutAfterLeave = false;
			if (IsValid(LogoutButton)) LogoutButton->SetIsEnabled(true);
		}
	}
	if (bSuccess && (Operation == EProjectProject01LobbyOperation::CreateRoom ||
		Operation == EProjectProject01LobbyOperation::JoinRoom ||
		Operation == EProjectProject01LobbyOperation::LeaveRoom ||
		Operation == EProjectProject01LobbyOperation::TransferHost ||
		Operation == EProjectProject01LobbyOperation::DeleteRoom))
	{
		if (Operation == EProjectProject01LobbyOperation::CreateRoom ||
			Operation == EProjectProject01LobbyOperation::JoinRoom ||
			Operation == EProjectProject01LobbyOperation::LeaveRoom ||
			Operation == EProjectProject01LobbyOperation::DeleteRoom)
		{
			bShowingCreateRoom = false;
		}
		RefreshLobbyState();
	}
}

void UProjectProject01LobbyWidget::HandleRoomListChanged()
{
	RefreshRoomListView();
}

void UProjectProject01LobbyWidget::HandleCurrentRoomChanged()
{
	RefreshCurrentRoomView();
}

void UProjectProject01LobbyWidget::RefreshLobbyState()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (IsValid(GameInstance) && ++ServiceStatusPollCounter >= 5)
	{
		ServiceStatusPollCounter = 0;
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->CheckMultiplayerServiceStatus();
		}
	}
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || Lobby->IsRequestInFlight() || bTravelRequested)
	{
		return;
	}
	if (Lobby->HasCurrentRoom()) Lobby->RefreshCurrentRoom();
	else Lobby->RefreshRooms();
}

void UProjectProject01LobbyWidget::RefreshRoomListView()
{
	if (!IsValid(RoomListComboBox))
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby))
	{
		return;
	}
	const FString PreviousSelection = RoomListComboBox->GetSelectedOption();
	RoomListComboBox->ClearOptions();
	JoinCodeByDisplayOption.Reset();
	const FString Search = IsValid(RoomSearchInput)
		? RoomSearchInput->GetText().ToString().TrimStartAndEnd() : FString();
	for (const FProjectProject01RoomSummary& Room : Lobby->GetRooms())
	{
		if (!Search.IsEmpty() && !Room.Name.Contains(Search, ESearchCase::IgnoreCase) &&
			!Room.JoinCode.Contains(Search, ESearchCase::IgnoreCase) &&
			!Room.HostDisplayName.Contains(Search, ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FString LockText = Room.bHasPassword ? TEXT(" [비밀번호]") : FString();
		const FString Option = FString::Printf(TEXT("%s (%d/%d) - %s%s"),
			*Room.Name, Room.MemberCount, Room.MaxPlayers, *Room.HostDisplayName, *LockText);
		RoomListComboBox->AddOption(Option);
		JoinCodeByDisplayOption.Add(Option, Room.JoinCode);
	}
	if (JoinCodeByDisplayOption.Contains(PreviousSelection)) RoomListComboBox->SetSelectedOption(PreviousSelection);
	else if (RoomListComboBox->GetOptionCount() > 0) RoomListComboBox->SetSelectedIndex(0);
}

void UProjectProject01LobbyWidget::RefreshCurrentRoomView()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby))
	{
		return;
	}
	const bool bInRoom = Lobby->HasCurrentRoom();
	ShowLobbyView(bInRoom ? 2 : (bShowingCreateRoom ? 1 : 0));
	if (IsValid(CreateRoomButton)) CreateRoomButton->SetIsEnabled(!bInRoom);
	if (IsValid(JoinRoomButton)) JoinRoomButton->SetIsEnabled(!bInRoom);
	if (IsValid(ReadyButton)) ReadyButton->SetVisibility(bInRoom && !Lobby->GetCurrentRoom().bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(StartRoomButton)) StartRoomButton->SetVisibility(bInRoom && Lobby->GetCurrentRoom().bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(LeaveRoomButton)) LeaveRoomButton->SetVisibility(bInRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(CopyJoinCodeButton)) CopyJoinCodeButton->SetVisibility(bInRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	const bool bCanManageRoom = bInRoom && Lobby->GetCurrentRoom().bIsHost;
	if (IsValid(HostTransferComboBox)) HostTransferComboBox->SetVisibility(bCanManageRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(TransferHostButton)) TransferHostButton->SetVisibility(bCanManageRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(DeleteRoomButton)) DeleteRoomButton->SetVisibility(bCanManageRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(SendChatButton)) SendChatButton->SetIsEnabled(bInRoom);
	if (!bInRoom)
	{
		if (IsValid(CurrentRoomText)) CurrentRoomText->SetText(FProjectProject01Localization::Text(TEXT("참가 중인 방 없음")));
		if (IsValid(MemberListText)) MemberListText->SetText(FText::GetEmpty());
		if (IsValid(ChatLogText)) ChatLogText->SetText(FText::GetEmpty());
		if (IsValid(HostTransferComboBox)) HostTransferComboBox->ClearOptions();
		UserIdByTransferOption.Reset();
		return;
	}

	const FProjectProject01RoomState& Room = Lobby->GetCurrentRoom();
	if (IsValid(CurrentRoomText))
	{
		CurrentRoomText->SetText(FText::FromString(FString::Printf(
			TEXT("방: %s\n참가 코드: %s\n인원: %d/3"), *Room.Name, *Room.JoinCode, Room.Members.Num())));
	}
	FString MembersText;
	for (const FProjectProject01RoomMember& Member : Room.Members)
	{
		const FString RoleText = Member.AssignedRole.IsEmpty()
			? FString()
			: FString::Printf(TEXT(" [%s]"), *Member.AssignedRole);
		const FString ReturnText = Member.bReturnedToRoom ? FString(TEXT(" [방 복귀]")) : FString();
		MembersText += FString::Printf(TEXT("%s%s - %s%s%s\n"), *Member.DisplayName,
			Member.bIsHost ? TEXT(" [방장]") : TEXT(""),
			Member.bIsHost ? TEXT("시작 대기") : (Member.bIsReady ? TEXT("준비") : TEXT("대기")),
			*RoleText, *ReturnText);
	}
	if (IsValid(MemberListText)) MemberListText->SetText(FText::FromString(MembersText));
	if (IsValid(HostTransferComboBox))
	{
		const FString PreviousSelection = HostTransferComboBox->GetSelectedOption();
		HostTransferComboBox->ClearOptions();
		UserIdByTransferOption.Reset();
		if (Room.bIsHost)
		{
			for (const FProjectProject01RoomMember& Member : Room.Members)
			{
				if (Member.bIsHost || Member.UserId.IsEmpty())
				{
					continue;
				}
				const FString Option = FString::Printf(TEXT("%s (%s)"), *Member.DisplayName, *Member.UserId);
				HostTransferComboBox->AddOption(Option);
				UserIdByTransferOption.Add(Option, Member.UserId);
			}
			if (UserIdByTransferOption.Contains(PreviousSelection))
			{
				HostTransferComboBox->SetSelectedOption(PreviousSelection);
			}
			else if (HostTransferComboBox->GetOptionCount() > 0)
			{
				HostTransferComboBox->SetSelectedIndex(0);
			}
		}
	}
	if (IsValid(TransferHostButton))
	{
		TransferHostButton->SetIsEnabled(Room.bIsHost && UserIdByTransferOption.Num() > 0);
	}
	if (IsValid(ReadyButton))
	{
		if (UTextBlock* ButtonText = Cast<UTextBlock>(ReadyButton->GetChildAt(0)))
		{
			ButtonText->SetText(FProjectProject01Localization::Text(Room.bIsReady ? TEXT("준비 취소") : TEXT("준비")));
		}
	}
	if (IsValid(StartRoomButton)) StartRoomButton->SetIsEnabled(Room.bCanStart);

	FString ChatText;
	for (const FProjectProject01RoomChatMessage& Chat : Room.ChatMessages)
	{
		ChatText += FString::Printf(TEXT("%s: %s\n"), *Chat.DisplayName, *Chat.Message);
	}
	if (IsValid(ChatLogText)) ChatLogText->SetText(FText::FromString(ChatText));
	if (Room.bStarted && !Room.bReturnedToRoom)
	{
		if (Lobby->HasPendingGameJoinTicket())
		{
			TravelToStartedGame(Room);
		}
		else if (!Lobby->IsRequestInFlight())
		{
			SetStatus(TEXT("일회용 게임 접속 티켓을 발급받는 중입니다..."), false);
			Lobby->RequestGameJoinTicket();
		}
	}
	else if (Room.bStarted && Room.bReturnedToRoom)
	{
		SetStatus(TEXT("진행 중인 경기에서 방으로 복귀했습니다. 남은 플레이어를 기다립니다."), false);
	}
}

void UProjectProject01LobbyWidget::ShowLobbyView(const int32 ViewIndex)
{
	if (IsValid(LobbyViewSwitcher) && ViewIndex >= 0 && ViewIndex < LobbyViewSwitcher->GetNumWidgets())
	{
		LobbyViewSwitcher->SetActiveWidgetIndex(ViewIndex);
	}
}

void UProjectProject01LobbyWidget::HandleRoomSearchChanged(const FText& SearchText)
{
	(void)SearchText;
	RefreshRoomListView();
}

void UProjectProject01LobbyWidget::TravelToStartedGame(const FProjectProject01RoomState& RoomState)
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (bTravelRequested || !IsValid(Lobby) || !Lobby->HasPendingGameJoinTicket())
	{
		if (!bTravelRequested && RoomState.bStarted)
		{
			SetStatus(TEXT("검증된 게임 접속 티켓 또는 AES-GCM 키가 없습니다."), true);
		}
		return;
	}
	APlayerController* PlayerController = GetOwningPlayer();
	if (!IsValid(PlayerController))
	{
		SetStatus(TEXT("게임 서버로 이동할 PlayerController를 찾지 못했습니다."), true);
		return;
	}
	bTravelRequested = true;
	const FString TravelUrl = FString::Printf(TEXT("%s?EncryptionToken=%s?GameTicket=%s?LobbyRoomId=%s?MatchId=%s"),
		*Lobby->GetGameTicketTravelUrl(), *Lobby->GetGameJoinTicket(), *Lobby->GetGameJoinTicket(),
		*Lobby->GetGameTicketRoomId(), *Lobby->GetGameTicketMatchId());
	SetStatus(FString::Printf(TEXT("검증된 MultiplayTest 서버로 이동합니다. 역할: %s"),
		*Lobby->GetGameTicketRole()), false);
	PlayerController->ClientTravel(TravelUrl, TRAVEL_Absolute);
}

void UProjectProject01LobbyWidget::BuildWidgetTree()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Root;
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("ProjectProject01 Lobby"), 30.0f);
	LobbyStatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("세션 확인 중..."), 14.0f);
	LobbyViewSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	if (UVerticalBoxSlot* SwitcherSlot = Root->AddChildToVerticalBox(LobbyViewSwitcher))
	{
		SwitcherSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	auto MakeScrollablePanel = [this]() -> UVerticalBox*
	{
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		UVerticalBox* Panel = WidgetTree->ConstructWidget<UVerticalBox>();
		Scroll->AddChild(Panel);
		LobbyViewSwitcher->AddChild(Scroll);
		return Panel;
	};

	// 0: 처음 보이는 방 검색/참가 화면
	UVerticalBox* BrowserPanel = MakeScrollablePanel();
	ProjectProject01LoginUI::AddLabel(WidgetTree, BrowserPanel, TEXT("방 목록 / 참가"), 20.0f);
	OpenCreateRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, BrowserPanel, TEXT("새 방 만들기"));
	RoomSearchInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	RoomSearchInput->SetHintText(FProjectProject01Localization::Text(TEXT("방 이름 / 참가 코드 / 방장 이름 검색")));
	RoomSearchInput->SetForegroundColor(FLinearColor::Black);
	BrowserPanel->AddChildToVerticalBox(RoomSearchInput)->SetPadding(FMargin(6.0f));
	RefreshRoomsButton = ProjectProject01LoginUI::AddButton(WidgetTree, BrowserPanel, TEXT("공개방 목록 새로고침"));
	RoomListComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
	ProjectProject01LoginUI::SetComboBoxTextBlack(RoomListComboBox);
	BrowserPanel->AddChildToVerticalBox(RoomListComboBox)->SetPadding(FMargin(6.0f));
	DirectRoomCodeInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	DirectRoomCodeInput->SetHintText(FProjectProject01Localization::Text(TEXT("6~8자리 참가 코드 (비공개방 또는 직접 참가)")));
	DirectRoomCodeInput->SetForegroundColor(FLinearColor::Black);
	BrowserPanel->AddChildToVerticalBox(DirectRoomCodeInput)->SetPadding(FMargin(6.0f));
	JoinPasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	JoinPasswordInput->SetHintText(FProjectProject01Localization::Text(TEXT("참가 비밀번호 (비밀번호방만)")));
	JoinPasswordInput->SetForegroundColor(FLinearColor::Black);
	JoinPasswordInput->SetIsPassword(true);
	BrowserPanel->AddChildToVerticalBox(JoinPasswordInput)->SetPadding(FMargin(6.0f));
	JoinRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, BrowserPanel, TEXT("선택/참가 코드로 방 참가"));
	OpenLeaderboardTestButton = ProjectProject01LoginUI::AddButton(WidgetTree, BrowserPanel, TEXT("리더보드 열기"));
	LogoutButton = ProjectProject01LoginUI::AddButton(WidgetTree, BrowserPanel, TEXT("로그아웃 / 로그인 화면으로"));

	// 1: 사용자가 '새 방 만들기'를 눌렀을 때만 보이는 생성 화면
	UVerticalBox* CreatePanel = MakeScrollablePanel();
	ProjectProject01LoginUI::AddLabel(WidgetTree, CreatePanel, TEXT("새 방 만들기"), 20.0f);
	RoomNameInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	RoomNameInput->SetHintText(FProjectProject01Localization::Text(TEXT("방 이름 (1~48자)")));
	RoomNameInput->SetForegroundColor(FLinearColor::Black);
	CreatePanel->AddChildToVerticalBox(RoomNameInput)->SetPadding(FMargin(6.0f));
	PublicRoomCheckBox = WidgetTree->ConstructWidget<UCheckBox>();
	PublicRoomCheckBox->SetIsChecked(true);
	UBorder* PublicModeBackground = WidgetTree->ConstructWidget<UBorder>();
	PublicModeBackground->SetBrushColor(FLinearColor(0.82f, 0.82f, 0.82f, 1.0f));
	PublicModeBackground->SetPadding(FMargin(8.0f));
	UHorizontalBox* PublicModeRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	PublicModeRow->AddChild(PublicRoomCheckBox);
	UTextBlock* PublicLabel = WidgetTree->ConstructWidget<UTextBlock>();
	PublicLabel->SetText(FProjectProject01Localization::Text(TEXT("공개방: 체크 / 비공개방: 체크 해제 (참가 코드를 공유해 입장)")));
	PublicLabel->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	PublicModeRow->AddChild(PublicLabel);
	PublicModeBackground->AddChild(PublicModeRow);
	CreatePanel->AddChildToVerticalBox(PublicModeBackground)->SetPadding(FMargin(6.0f));
	CreatePasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	CreatePasswordInput->SetHintText(FProjectProject01Localization::Text(TEXT("방 비밀번호 (선택, 사용 시 4~64자)")));
	CreatePasswordInput->SetForegroundColor(FLinearColor::Black);
	CreatePasswordInput->SetIsPassword(true);
	CreatePanel->AddChildToVerticalBox(CreatePasswordInput)->SetPadding(FMargin(6.0f));
	CreateRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, CreatePanel, TEXT("방 만들기"));
	CancelCreateRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, CreatePanel, TEXT("방 목록으로 돌아가기"));

	// 2: 방 생성 또는 참가에 성공한 사용자만 보는 방 정보 화면
	UVerticalBox* RoomPanel = MakeScrollablePanel();
	ProjectProject01LoginUI::AddLabel(WidgetTree, RoomPanel, TEXT("현재 방 정보"), 20.0f);
	CurrentRoomText = ProjectProject01LoginUI::AddLabel(WidgetTree, RoomPanel, TEXT("참가 중인 방 없음"), 16.0f);
	CopyJoinCodeButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("참가 코드 복사"));
	MemberListText = ProjectProject01LoginUI::AddLabel(WidgetTree, RoomPanel, TEXT(""), 14.0f);
	ReadyButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("준비"));
	StartRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("게임 시작"));
	LeaveRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("방 나가기"));
	HostTransferComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
	ProjectProject01LoginUI::SetComboBoxTextBlack(HostTransferComboBox);
	RoomPanel->AddChildToVerticalBox(HostTransferComboBox)->SetPadding(FMargin(6.0f));
	TransferHostButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("선택한 플레이어에게 방장 넘기기"));
	DeleteRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("방 삭제 (모두 로비로)"));
	ReadyButton->SetVisibility(ESlateVisibility::Collapsed);
	StartRoomButton->SetVisibility(ESlateVisibility::Collapsed);
	LeaveRoomButton->SetVisibility(ESlateVisibility::Collapsed);
	CopyJoinCodeButton->SetVisibility(ESlateVisibility::Collapsed);
	HostTransferComboBox->SetVisibility(ESlateVisibility::Collapsed);
	TransferHostButton->SetVisibility(ESlateVisibility::Collapsed);
	DeleteRoomButton->SetVisibility(ESlateVisibility::Collapsed);

	ChatLogText = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
	ChatLogText->SetIsReadOnly(true);
	ChatLogText->SetHintText(FProjectProject01Localization::Text(TEXT("방 채팅")));
	ChatLogText->SetForegroundColor(FLinearColor::Black);
	RoomPanel->AddChildToVerticalBox(ChatLogText)->SetPadding(FMargin(6.0f));
	ChatInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	ChatInput->SetHintText(FProjectProject01Localization::Text(TEXT("채팅 입력 (최대 300자)")));
	ChatInput->SetForegroundColor(FLinearColor::Black);
	RoomPanel->AddChildToVerticalBox(ChatInput)->SetPadding(FMargin(6.0f));
	SendChatButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("채팅 보내기"));
	SendChatButton->SetIsEnabled(false);
	ShowLobbyView(0);
}

void UProjectProject01LobbyWidget::SetStatus(const FString& Message, const bool bIsError)
{
	if (!IsValid(LobbyStatusText))
	{
		return;
	}
	LobbyStatusText->SetText(FProjectProject01Localization::Text(Message));
	LobbyStatusText->SetColorAndOpacity(bIsError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::Black));
}

bool UProjectProject01LeaderboardWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	return true;
}

void UProjectProject01LeaderboardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(RefreshButton)) RefreshButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleRefreshClicked);
	if (IsValid(RoleComboBox)) RoleComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleRoleSelectionChanged);
	if (IsValid(SortComboBox)) SortComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleSortSelectionChanged);
	if (IsValid(SubmitRecordButton)) SubmitRecordButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleSubmitRecordClicked);
	if (IsValid(ReturnToLobbyButton)) ReturnToLobbyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleReturnToLobbyClicked);

	PendingTestMatchId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	RefreshSortOptions();

	UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Auth) || !Auth->IsSignedIn() || !IsValid(Lobby))
	{
		SetStatus(TEXT("로그인 세션 또는 리더보드 시스템이 없습니다."), true);
		return;
	}

	Lobby->OnRequestCompleted.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleLeaderboardRequestResult);
	Lobby->OnLeaderboardChanged.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleLeaderboardChanged);
	Lobby->RefreshLeaderboard(GetSelectedRole(), GetSelectedSort());
}

void UProjectProject01LeaderboardWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->OnRequestCompleted.RemoveDynamic(this, &UProjectProject01LeaderboardWidget::HandleLeaderboardRequestResult);
			Lobby->OnLeaderboardChanged.RemoveDynamic(this, &UProjectProject01LeaderboardWidget::HandleLeaderboardChanged);
		}
	}
	Super::NativeDestruct();
}

void UProjectProject01LeaderboardWidget::HandleRefreshClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->RefreshLeaderboard(GetSelectedRole(), GetSelectedSort());
		}
	}
}

void UProjectProject01LeaderboardWidget::HandleRoleSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	(void)SelectedItem;
	(void)SelectionType;
	RefreshSortOptions();
	HandleRefreshClicked();
}

void UProjectProject01LeaderboardWidget::HandleSortSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	(void)SelectedItem;
	(void)SelectionType;
	if (bUpdatingSortOptions)
	{
		return;
	}
	HandleRefreshClicked();
}

void UProjectProject01LeaderboardWidget::HandleSubmitRecordClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby))
	{
		SetStatus(TEXT("리더보드 시스템을 찾지 못했습니다."), true);
		return;
	}

	const int32 CaptureCount = IsValid(CaptureCountInput) ? FCString::Atoi(*CaptureCountInput->GetText().ToString()) : 0;
	const double FirstCaptureSeconds = IsValid(FirstCaptureSecondsInput) ? FCString::Atod(*FirstCaptureSecondsInput->GetText().ToString()) : 0.0;
	const double AllCapturedSeconds = IsValid(AllCapturedSecondsInput) ? FCString::Atod(*AllCapturedSecondsInput->GetText().ToString()) : 0.0;
	const int32 RescueCount = IsValid(RescueCountInput) ? FCString::Atoi(*RescueCountInput->GetText().ToString()) : 0;
	const double EscapeSeconds = IsValid(EscapeSecondsInput) ? FCString::Atod(*EscapeSecondsInput->GetText().ToString()) : 0.0;

	SetStatus(TEXT("테스트 성공 기록을 등록하는 중..."), false);
	Lobby->SubmitLeaderboardRecord(PendingTestMatchId, GetSelectedRole(), true,
		CaptureCount, FirstCaptureSeconds, AllCapturedSeconds, RescueCount, EscapeSeconds);
}

void UProjectProject01LeaderboardWidget::HandleReturnToLobbyClicked()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LobbyLevel")));
}

void UProjectProject01LeaderboardWidget::HandleLeaderboardChanged()
{
	RefreshLeaderboardView();
}

void UProjectProject01LeaderboardWidget::HandleLeaderboardRequestResult(
	const EProjectProject01LobbyOperation Operation,
	const bool bSuccess,
	const FString& Message)
{
	if (Operation != EProjectProject01LobbyOperation::RefreshLeaderboard &&
		Operation != EProjectProject01LobbyOperation::SubmitLeaderboardRecord)
	{
		return;
	}
	SetStatus(Message, !bSuccess);
	if (bSuccess && Operation == EProjectProject01LobbyOperation::SubmitLeaderboardRecord)
	{
		PendingTestMatchId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
			{
				Lobby->RefreshLeaderboard(GetSelectedRole(), GetSelectedSort());
			}
		}
	}
}

void UProjectProject01LeaderboardWidget::RefreshLeaderboardView()
{
	if (!IsValid(LeaderboardText))
	{
		return;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby))
	{
		return;
	}

	FString Text = GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin
		? TEXT("순위 | 이름 | 역할 | 종합점수 | 포획 | 첫 포획(초) | 전원 포획(초)\n")
		: TEXT("순위 | 이름 | 역할 | 종합점수 | 구출 | 탈출(초)\n");
	for (const FProjectProject01LeaderboardEntry& Entry : Lobby->GetLeaderboardEntries())
	{
		if (GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin)
		{
			Text += FString::Printf(TEXT("%d | %s | %s | %.1f | %d | %.2f | %.2f\n"),
				Entry.Rank, *Entry.DisplayName, *Entry.Role, Entry.OverallScore, Entry.CaptureCount,
				Entry.FirstCaptureSeconds, Entry.AllCapturedSeconds);
		}
		else
		{
			Text += FString::Printf(TEXT("%d | %s | %s | %.1f | %d | %.2f\n"),
				Entry.Rank, *Entry.DisplayName, *Entry.Role, Entry.OverallScore,
				Entry.RescueCount, Entry.EscapeSeconds);
		}
	}
	if (Lobby->GetLeaderboardEntries().IsEmpty())
	{
		Text += TEXT("등록된 성공 기록이 없습니다.\n");
	}
	LeaderboardText->SetText(FText::FromString(Text));
}

void UProjectProject01LeaderboardWidget::RefreshSortOptions()
{
	if (!IsValid(SortComboBox))
	{
		return;
	}
	bUpdatingSortOptions = true;
	SortComboBox->ClearOptions();
	SortComboBox->AddOption(TEXT("종합 점수"));
	const bool bMannequin = GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin;
	if (bMannequin)
	{
		SortComboBox->AddOption(TEXT("붙잡은 횟수"));
		SortComboBox->AddOption(TEXT("첫 포획 시간"));
		SortComboBox->AddOption(TEXT("전원 포획 시간"));
	}
	else
	{
		SortComboBox->AddOption(TEXT("구출 횟수"));
		SortComboBox->AddOption(TEXT("탈출 시간"));
	}
	SortComboBox->SetSelectedOption(TEXT("종합 점수"));
	if (IsValid(CaptureCountInput)) CaptureCountInput->SetVisibility(bMannequin ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(FirstCaptureSecondsInput)) FirstCaptureSecondsInput->SetVisibility(bMannequin ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(AllCapturedSecondsInput)) AllCapturedSecondsInput->SetVisibility(bMannequin ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(RescueCountInput)) RescueCountInput->SetVisibility(bMannequin ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (IsValid(EscapeSecondsInput)) EscapeSecondsInput->SetVisibility(bMannequin ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	bUpdatingSortOptions = false;
}

EProjectProject01LeaderboardRole UProjectProject01LeaderboardWidget::GetSelectedRole() const
{
	return IsValid(RoleComboBox) && RoleComboBox->GetSelectedOption() == TEXT("생존자")
		? EProjectProject01LeaderboardRole::Survivor : EProjectProject01LeaderboardRole::Mannequin;
}

EProjectProject01LeaderboardSort UProjectProject01LeaderboardWidget::GetSelectedSort() const
{
	const FString Option = IsValid(SortComboBox) ? SortComboBox->GetSelectedOption() : FString();
	if (Option == TEXT("붙잡은 횟수")) return EProjectProject01LeaderboardSort::Captures;
	if (Option == TEXT("첫 포획 시간")) return EProjectProject01LeaderboardSort::FirstCapture;
	if (Option == TEXT("전원 포획 시간")) return EProjectProject01LeaderboardSort::AllCaptured;
	if (Option == TEXT("구출 횟수")) return EProjectProject01LeaderboardSort::Rescues;
	if (Option == TEXT("탈출 시간")) return EProjectProject01LeaderboardSort::Escape;
	return EProjectProject01LeaderboardSort::Overall;
}

void UProjectProject01LeaderboardWidget::BuildWidgetTree()
{
	UScrollBox* ScrollRoot = WidgetTree->ConstructWidget<UScrollBox>();
	WidgetTree->RootWidget = ScrollRoot;
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	ScrollRoot->AddChild(Root);

	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("ProjectProject01 LeaderBoardTest"), 24.0f);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root,
		TEXT("성공한 경기만 등록하는 리더보드 테스트입니다. 미지정 정렬은 역할별 평균 순위 기반 종합점수입니다."), 14.0f);
	LeaderboardStatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("리더보드를 불러오는 중..."), 14.0f);

	RoleComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
	ProjectProject01LoginUI::SetComboBoxTextBlack(RoleComboBox);
	RoleComboBox->AddOption(TEXT("마네킹"));
	RoleComboBox->AddOption(TEXT("생존자"));
	RoleComboBox->SetSelectedOption(TEXT("마네킹"));
	Root->AddChildToVerticalBox(RoleComboBox)->SetPadding(FMargin(6.0f));

	SortComboBox = WidgetTree->ConstructWidget<UComboBoxString>();
	ProjectProject01LoginUI::SetComboBoxTextBlack(SortComboBox);
	Root->AddChildToVerticalBox(SortComboBox)->SetPadding(FMargin(6.0f));
	RefreshButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("리더보드 새로고침"));

	LeaderboardText = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
	LeaderboardText->SetIsReadOnly(true);
	LeaderboardText->SetForegroundColor(FLinearColor::Black);
	Root->AddChildToVerticalBox(LeaderboardText)->SetPadding(FMargin(6.0f));

	ProjectProject01LoginUI::AddLabel(WidgetTree, Root,
		TEXT("테스트 성공 결과 입력 (실제 게임 종료 연결 전 수동 검증용)"), 16.0f);
	auto AddInput = [this, Root](const FString& Hint, const FString& DefaultValue)
	{
		UEditableTextBox* Input = WidgetTree->ConstructWidget<UEditableTextBox>();
		Input->SetHintText(FProjectProject01Localization::Text(Hint));
		Input->SetText(FText::FromString(DefaultValue));
		Input->SetForegroundColor(FLinearColor::Black);
		Root->AddChildToVerticalBox(Input)->SetPadding(FMargin(6.0f));
		return Input;
	};
	CaptureCountInput = AddInput(TEXT("붙잡은 횟수"), TEXT("2"));
	FirstCaptureSecondsInput = AddInput(TEXT("첫 포획 시간(초)"), TEXT("30"));
	AllCapturedSecondsInput = AddInput(TEXT("전원 포획 시간(초)"), TEXT("120"));
	RescueCountInput = AddInput(TEXT("구출 횟수"), TEXT("1"));
	EscapeSecondsInput = AddInput(TEXT("탈출 시간(초)"), TEXT("180"));
	SubmitRecordButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("현재 성공 기록을 리더보드에 등록"));
	ReturnToLobbyButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("등록하지 않고 로비로 돌아가기"));
}

void UProjectProject01LeaderboardWidget::SetStatus(const FString& Message, const bool bIsError)
{
	if (!IsValid(LeaderboardStatusText))
	{
		return;
	}
	LeaderboardStatusText->SetText(FProjectProject01Localization::Text(Message));
	LeaderboardStatusText->SetColorAndOpacity(
		bIsError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::Black));
}

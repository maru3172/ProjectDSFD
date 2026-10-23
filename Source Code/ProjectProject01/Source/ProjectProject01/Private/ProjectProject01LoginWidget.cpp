// File: Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp
// Target: ProjectProject01 Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01LoginWidget.h"
#include "ProjectLoadingScreenSubsystem.h"
#include "ProjectDFSDBranding.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectProject01SocialSubsystem.h"
#include "ProjectProject01SocialWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01Localization.h"
#include "ProjectProject01SaveSubsystem.h"
#include "ProjectProject01VoiceChatSubsystem.h"
#include "ProjectProject01VoiceSettingsWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"
#include "Misc/PackageName.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "ImageUtils.h"
#endif

namespace
{
	// Widgets can tick, receive input or be destroyed while detached from a GameInstance.
	template <typename TSubsystem>
	TSubsystem* GameInstanceSubsystemOf(const UUserWidget* Widget)
	{
		const UGameInstance* Instance = IsValid(Widget) ? Widget->GetGameInstance() : nullptr;
		return IsValid(Instance) ? Instance->GetSubsystem<TSubsystem>() : nullptr;
	}
}

namespace ProjectProject01LoginUI
{
	bool IsSupportedServerIpv4(FString Address)
	{
		Address.TrimStartAndEndInline();
		TArray<FString> Parts;
		Address.ParseIntoArray(Parts, TEXT("."), false);
		if (Parts.Num() != 4) return false;
		for (const FString& Part : Parts)
		{
			if (Part.IsEmpty() || !Part.IsNumeric()) return false;
			const int32 Octet = FCString::Atoi(*Part);
			if (Octet < 0 || Octet > 255) return false;
		}
		const int32 A = FCString::Atoi(*Parts[0]);
		const int32 B = FCString::Atoi(*Parts[1]);
		return A == 25 || A == 10 || (A == 172 && B >= 16 && B <= 31) ||
			(A == 192 && B == 168) || A == 127;
	}

	UTextBlock* AddLabel(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& Text, const float FontSize)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FProjectProject01Localization::Text(Text));
		Label->SetColorAndOpacity(FSlateColor(ProjectDFSDUI::Text()));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor::Transparent);
		Background->SetPadding(FMargin(6.0f));
		Background->AddChild(Label);
		if (UVerticalBoxSlot* Slot = Root->AddChildToVerticalBox(Background))
		{
			Slot->SetPadding(FMargin(2.0f, 4.0f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Label;
	}

	UTextBlock* AddSaveSlotSummary(UWidgetTree* Tree, UVerticalBox* Parent, UImage*& OutImage)
	{
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(2,4));
		USizeBox* Frame = Tree->ConstructWidget<USizeBox>();
		Frame->SetWidthOverride(160); Frame->SetHeightOverride(90);
		Frame->SetClipping(EWidgetClipping::ClipToBounds);
		Row->AddChildToHorizontalBox(Frame)->SetPadding(FMargin(0,0,10,0));
		UBorder* Background=Tree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor(0.08f,0.08f,0.08f,1));
		Background->SetPadding(FMargin(0)); Frame->AddChild(Background);
		UOverlay* Layers=Tree->ConstructWidget<UOverlay>(); Background->AddChild(Layers);
		UTextBlock* Missing=Tree->ConstructWidget<UTextBlock>();
		Missing->SetText(NSLOCTEXT("CheckpointPreview","Missing","미리보기 없음"));
		Missing->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		FSlateFontInfo MissingFont=Missing->GetFont(); MissingFont.Size=12; Missing->SetFont(MissingFont);
		Missing->SetJustification(ETextJustify::Center);
		Missing->SetWrapTextAt(148);
		UOverlaySlot* MissingSlot=Layers->AddChildToOverlay(Missing);
		MissingSlot->SetHorizontalAlignment(HAlign_Fill); MissingSlot->SetVerticalAlignment(VAlign_Center);
		OutImage=Tree->ConstructWidget<UImage>();
		UOverlaySlot* ImageSlot=Layers->AddChildToOverlay(OutImage);
		ImageSlot->SetHorizontalAlignment(HAlign_Fill);
		ImageSlot->SetVerticalAlignment(VAlign_Fill);
		OutImage->SetVisibility(ESlateVisibility::Collapsed);
		UVerticalBox* Description=Tree->ConstructWidget<UVerticalBox>();
		Row->AddChildToHorizontalBox(Description)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UTextBlock* Label=AddLabel(Tree,Description,TEXT(""),14);
		Label->SetAutoWrapText(true);
		return Label;
	}

	void RefreshSavePreview(const TArray<TObjectPtr<UImage>>& Images, int32 Index, UTexture2D* Texture)
	{
		if (!Images.IsValidIndex(Index) || !IsValid(Images[Index])) return;
		Images[Index]->SetBrushFromTexture(Texture, true);
		Images[Index]->SetVisibility(IsValid(Texture) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (UOverlay* Layers=Cast<UOverlay>(Images[Index]->GetParent()))
			if (UWidget* Missing=Layers->GetChildAt(0))
				Missing->SetVisibility(IsValid(Texture) ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	FString SaveSlotDescription(const FProjectProject01SaveSlotInfo& Info, const FString& CurrentMark)
	{
		if (!Info.bHasSave) return FString::Printf(TEXT("저장소 %d%s | 빈 저장소"),Info.SlotIndex,*CurrentMark);
		return FString::Printf(TEXT("저장소 %d%s | %s\n맵: %s | 저장: %s (한국 시간) | 데스카운트 %d"),
			Info.SlotIndex,*CurrentMark,Info.CheckpointId.IsNone() ? TEXT("저장된 시점") : *Info.CheckpointId.ToString(),
			*FPackageName::GetShortName(Info.SavedMapPackageName),
			*(Info.SavedAtUtc+FTimespan::FromHours(9)).ToString(TEXT("%Y-%m-%d %H:%M")),Info.RemainingDeathCount);
	}

	UButton* AddButton(UWidgetTree* WidgetTree, UVerticalBox* Root, const FString& Text)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		ProjectDFSDUI::Apply(Button);
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>();
		ButtonText->SetText(FProjectProject01Localization::Text(Text));
		ButtonText->SetColorAndOpacity(FSlateColor(ProjectDFSDUI::Text()));
		ButtonText->SetJustification(ETextJustify::Center);
		FSlateFontInfo Font=ButtonText->GetFont();Font.Size=20;ButtonText->SetFont(Font);
		Button->AddChild(ButtonText);
		if (UVerticalBoxSlot* Slot = Root->AddChildToVerticalBox(Button))
		{
			Slot->SetPadding(FMargin(6.0f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Button;
	}

	void StyleComboBox(UComboBoxString* ComboBox)
	{
		ProjectDFSDUI::Apply(ComboBox);
	}

	void StyleTextBox(UEditableTextBox* TextBox)
	{
		ProjectDFSDUI::Apply(TextBox);
	}

	void StyleTextBox(UMultiLineEditableTextBox* TextBox)
	{
		ProjectDFSDUI::Apply(TextBox);
	}

	void EnlargeInput(UEditableTextBox* Input)
	{
		FEditableTextBoxStyle Style=Input->GetWidgetStyle();
		FSlateFontInfo Font=Style.TextStyle.Font;Font.Size=20;
		Style.TextStyle.SetFont(Font);Style.SetPadding(FMargin(12,10));Input->SetWidgetStyle(Style);
	}
	void EnlargeInput(UMultiLineEditableTextBox* Input)
	{
		FSlateFontInfo Font=Input->WidgetStyle.TextStyle.Font;Font.Size=20;
		Input->WidgetStyle.TextStyle.SetFont(Font);Input->WidgetStyle.SetPadding(FMargin(12,10));
		Input->SetTextStyle(Input->WidgetStyle.TextStyle);
	}
	void AddLargeField(UWidgetTree* Tree,UVerticalBox* Parent,UWidget* Input,float Height)
	{
		auto* Frame=Tree->ConstructWidget<USizeBox>();Frame->SetMinDesiredHeight(Height);Frame->AddChild(Input);
		Parent->AddChildToVerticalBox(Frame)->SetPadding(FMargin(6));
	}

	UButton* AddTabButton(UWidgetTree* WidgetTree, UHorizontalBox* Root, const FString& Text)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		ProjectDFSDUI::Apply(Button);
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>();
		ButtonText->SetText(FProjectProject01Localization::Text(Text));
		ButtonText->SetColorAndOpacity(FSlateColor(ProjectDFSDUI::Text()));
		ButtonText->SetJustification(ETextJustify::Center);
		FSlateFontInfo Font=ButtonText->GetFont();Font.Size=18;ButtonText->SetFont(Font);
		ButtonText->SetAutoWrapText(true);
		Button->AddChild(ButtonText);
		if (auto* ContentSlot=Cast<UButtonSlot>(ButtonText->Slot)) ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		Root->AddChild(Button);
		if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Button->Slot))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetPadding(FMargin(3.0f));
		}
		return Button;
	}

	UVerticalBox* AddSettingsPage(UWidgetTree* WidgetTree, UWidgetSwitcher* Switcher)
	{
		UScrollBox* PageScroll = WidgetTree->ConstructWidget<UScrollBox>();
		Switcher->AddChild(PageScroll);
		UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
		PageScroll->AddChild(Page);
		return Page;
	}

	UTextBlock* AddPlainText(UWidgetTree* WidgetTree, UPanelWidget* Parent, const FString& Text, const float FontSize = 14.0f)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FProjectProject01Localization::Text(Text));
		Label->SetColorAndOpacity(FSlateColor(ProjectDFSDUI::Text()));
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
		UComboBoxString* Combo = WidgetTree->ConstructWidget<UProjectDFSDComboBox>();
		StyleComboBox(Combo);
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
		ButtonText->SetColorAndOpacity(FSlateColor(ProjectDFSDUI::Text()));
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

bool UProjectProject01TransientNoticeWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	ProjectDFSDUI::ApplyTree(WidgetTree);
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return true;
}

void UProjectProject01TransientNoticeWidget::ShowNotice(
	const FString& Message,
	const float HoldSeconds,
	const float FadeSeconds)
{
	ElapsedSeconds = 0.0f;
	HoldDurationSeconds = FMath::Max(0.0f, HoldSeconds);
	FadeDurationSeconds = FMath::Max(0.01f, FadeSeconds);
	SetRenderOpacity(1.0f);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (IsValid(NoticeText))
	{
		NoticeText->SetText(FProjectProject01Localization::Text(Message));
	}
}

void UProjectProject01TransientNoticeWidget::NativeTick(
	const FGeometry& MyGeometry,
	const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ElapsedSeconds += FMath::Max(0.0f, InDeltaTime);
	if (ElapsedSeconds <= HoldDurationSeconds)
	{
		SetRenderOpacity(1.0f);
		return;
	}
	const float FadeAlpha = (ElapsedSeconds - HoldDurationSeconds) / FadeDurationSeconds;
	SetRenderOpacity(1.0f - FMath::Clamp(FadeAlpha, 0.0f, 1.0f));
	if (FadeAlpha >= 1.0f)
	{
		RemoveFromParent();
	}
}

void UProjectProject01TransientNoticeWidget::BuildWidgetTree()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;
	NoticeBackground = WidgetTree->ConstructWidget<UBorder>();
	NoticeBackground->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.025f, 0.84f));
	NoticeBackground->SetPadding(FMargin(14.0f, 9.0f));
	if (UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(NoticeBackground))
	{
		CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		CanvasSlot->SetPosition(FVector2D(28.0f, 30.0f));
		CanvasSlot->SetAutoSize(true);
	}
	NoticeText = WidgetTree->ConstructWidget<UTextBlock>();
	NoticeText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	FSlateFontInfo Font = NoticeText->GetFont();
	Font.Size = 18;
	NoticeText->SetFont(Font);
	NoticeBackground->AddChild(NoticeText);
}

float UProjectProject01MannequinStatusWidget::GetContentBottom()
{
	if (!StatusText || !StatusText->GetParent()) return 92.f;
	auto* Background=StatusText->GetParent();
	Background->ForceLayoutPrepass();
	const auto* ContentSlot=Cast<UCanvasPanelSlot>(Background->Slot);
	const float Height=Background->GetDesiredSize().Y;
	return Height>0 ? (ContentSlot ? ContentSlot->GetPosition().Y : 92.f)+Height : 240.f;
}
bool UProjectProject01MannequinStatusWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	ProjectDFSDUI::ApplyTree(WidgetTree);
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshMannequinStates();
	return true;
}

void UProjectProject01MannequinStatusWidget::NativeTick(
	const FGeometry& MyGeometry,
	const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshAccumulatorSeconds += FMath::Max(0.0f, InDeltaTime);
	if (RefreshAccumulatorSeconds >= 0.2f)
	{
		RefreshAccumulatorSeconds = 0.0f;
		RefreshMannequinStates();
	}
}

void UProjectProject01MannequinStatusWidget::BuildWidgetTree()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrushColor(FLinearColor(0.08f, 0.015f, 0.015f, 0.84f));
	Background->SetPadding(FMargin(14.0f, 10.0f));
	if (UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Background))
	{
		CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		CanvasSlot->SetPosition(FVector2D(24.0f, 92.0f));
		CanvasSlot->SetAutoSize(true);
	}
	StatusText = WidgetTree->ConstructWidget<UTextBlock>();
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	StatusText->SetWrapTextAt(560.0f);
	FSlateFontInfo Font = StatusText->GetFont();
	Font.Size = 16;
	StatusText->SetFont(Font);
	Background->AddChild(StatusText);
}

void UProjectProject01MannequinStatusWidget::RefreshMannequinStates()
{
	if (!IsValid(StatusText))
	{
		return;
	}
	const AMultiplayTestPlayerController* Controller =
		Cast<AMultiplayTestPlayerController>(GetOwningPlayer());
	// This tactical information belongs only to the local mannequin player.
	if (!IsValid(Controller) || !Controller->IsLocalMannequinPlayer())
	{
		SetStatusSource(FString());
		if (UPanelWidget* Background = StatusText->GetParent()) Background->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	TArray<int32> ChasingSlots;
	TArray<int32> FrozenSlots;
	for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It)
	{
		if (!IsValid(*It) || It->GetControlSlot() < 0) continue;
		if (It->IsFrozenBySurvivorVision() || It->GetMultiplayActivity() == EMultiplayMannequinActivity::FrozenByVision)
			FrozenSlots.AddUnique(It->GetControlSlot());
		else if (It->GetMultiplayActivity() == EMultiplayMannequinActivity::Chasing)
			ChasingSlots.AddUnique(It->GetControlSlot());
	}
	auto FormatSlots = [](TArray<int32>& Slots)
	{
		Slots.Sort();
		TArray<FString> Labels;
		for (const int32 Slot : Slots) Labels.Add(FString::Printf(TEXT("%d번"), Slot));
		return Labels.IsEmpty() ? FString(TEXT("없음")) : FString::Join(Labels, TEXT(", "));
	};
	FString Alerts = FString::Printf(TEXT("생존자 추격 중: %s\n생존자 시야로 정지: %s"),
		*FormatSlots(ChasingSlots), *FormatSlots(FrozenSlots));
	if (Controller && !Controller->GetMannequinCommandNotice().IsEmpty()) Alerts+=TEXT("\n")+Controller->GetMannequinCommandNotice();
	// Display the body actually supplying this local camera. RPC/property arrival
	// order must not replace its identity with an empty or older selection.
	const AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(Controller->GetViewTarget());
	if (!IsValid(Mannequin) || Mannequin->GetWorld()!=GetWorld()) Mannequin = Controller->GetViewedMannequin();
	if (IsValid(Mannequin) && Mannequin->GetWorld()!=GetWorld()) Mannequin = nullptr;
	if (!IsValid(Mannequin))
	{
		SetStatusSource(TEXT("현재 빙의한 마네킹: 정보 동기화 중"));
		if (UPanelWidget* Background = StatusText->GetParent()) Background->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		return;
	}
	if (UPanelWidget* Background=StatusText->GetParent()) Background->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	FString Activity;
	switch (Mannequin->GetMultiplayActivity())
	{
	case EMultiplayMannequinActivity::Patrolling: Activity = TEXT("정찰"); break;
	case EMultiplayMannequinActivity::Observing: Activity = TEXT("주변 동향 관찰"); break;
	case EMultiplayMannequinActivity::Chasing: Activity = TEXT("생존자 추격"); break;
	case EMultiplayMannequinActivity::Following: Activity = TEXT("마네킹 플레이어 추종"); break;
	case EMultiplayMannequinActivity::Holding: Activity = TEXT("정지"); break;
	case EMultiplayMannequinActivity::ManualControl: Activity = TEXT("직접 조작"); break;
	case EMultiplayMannequinActivity::FrozenByVision: Activity = TEXT("생존자 시야로 정지"); break;
	case EMultiplayMannequinActivity::Searching: Activity = TEXT("마지막 목격 지점 수색"); break;
	default: Activity = TEXT("확인 중"); break;
	}
	if (Mannequin->IsManualControlEnabled()) Activity = TEXT("직접 조작");
	const bool bCatchDamageEnabled = Mannequin->IsCatchDamageEnabled();
	// A fresh manual session with no order returns to hold, not the body's old AI order.
	const auto DisplayCommand=Mannequin->IsManualControlEnabled() && Controller && !Controller->HasQueuedManualMannequinCommand()
		? EMultiplayMannequinCommand::HoldPosition : Mannequin->GetMultiplayCommand();
	const TCHAR* Command=DisplayCommand==EMultiplayMannequinCommand::Patrol?TEXT("정찰"):
		DisplayCommand==EMultiplayMannequinCommand::ChaseInDetectionRange?TEXT("감지 범위 추격"):
		DisplayCommand==EMultiplayMannequinCommand::FollowMannequinPlayer?TEXT("마네킹 플레이어 추종"):TEXT("정지");
	const FString CatchDamageState = bCatchDamageEnabled ? TEXT("활성") : TEXT("비활성");
	const FString Identity = Mannequin->GetControlSlot()>=0
		? FString::Printf(TEXT("%d번"),Mannequin->GetControlSlot()) : TEXT("번호 수신 중");
	SetStatusSource(FString::Printf(
		TEXT("현재 빙의한 마네킹: %s\n")
		TEXT("현재 상태: %s\n")
		TEXT("명령 상태: %s\n")
		TEXT("전체 AI 데스카운트 소모: %s\n\n%s"),
		*Identity, *Activity, Command, *CatchDamageState, *Alerts));
}

void UProjectProject01MannequinStatusWidget::SetStatusSource(const FString& Source)
{
	// FText::FromString makes a new text every call, which forces a relayout even for identical words.
	if (Source.Equals(LastStatusSource, ESearchCase::CaseSensitive) && !(Source.IsEmpty() && !StatusText->GetText().IsEmpty())) return;
	LastStatusSource = Source;
	StatusText->SetText(Source.IsEmpty() ? FText::GetEmpty() : FProjectProject01Localization::Text(Source));
}

bool UProjectProject01VoiceStatusWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	ProjectDFSDUI::ApplyTree(WidgetTree);
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshVoiceStatus();
	return true;
}

void UProjectProject01VoiceStatusWidget::NativeTick(
	const FGeometry& MyGeometry,
	const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshSeconds+=InDeltaTime;
	if(RefreshSeconds<0.1f)return;
	RefreshSeconds=0;
	RefreshVoiceStatus();
}

void UProjectProject01VoiceStatusWidget::BuildWidgetTree()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;
	StatusBackground = WidgetTree->ConstructWidget<UBorder>();
	StatusBackground->SetBrushColor(FLinearColor(0.03f, 0.03f, 0.04f, 0.88f));
	StatusBackground->SetPadding(FMargin(14.0f, 10.0f));
	if (UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(StatusBackground))
	{
		CanvasSlot->SetAnchors(FAnchors(1.0f, 0.0f));
		CanvasSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		CanvasSlot->SetPosition(FVector2D(-24.0f, 92.0f));
		CanvasSlot->SetAutoSize(true);
	}

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	StatusBackground->AddChild(Root);
	VoiceStatusText = WidgetTree->ConstructWidget<UTextBlock>();
	VoiceStatusText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	FSlateFontInfo StatusFont = VoiceStatusText->GetFont();
	StatusFont.Size = 16;
	VoiceStatusText->SetFont(StatusFont);
	Root->AddChildToVerticalBox(VoiceStatusText);
	TeammateVoiceText=WidgetTree->ConstructWidget<UTextBlock>();
	TeammateVoiceText->SetColorAndOpacity(FLinearColor::White);
	FSlateFontInfo PeerFont=StatusFont;PeerFont.Size=14;TeammateVoiceText->SetFont(PeerFont);
	TeammateVoiceText->SetWrapTextAt(350);
	Root->AddChildToVerticalBox(TeammateVoiceText);
}

void UProjectProject01VoiceStatusWidget::RefreshVoiceStatus()
{
	if (!IsValid(VoiceStatusText))
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01VoiceChatSubsystem* Voice = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01VoiceChatSubsystem>() : nullptr;
	const bool bConfigured = IsValid(Voice) && Voice->IsConfiguredForSurvivor();
	if(IsValid(StatusBackground))StatusBackground->SetVisibility(bConfigured?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
	if(IsValid(TeammateVoiceText))
	{
		TArray<FString> Lines;
		if(bConfigured)
			for(const auto& Peer:Voice->GetVoiceParticipantStates())
			{
				Lines.Add(FString::Printf(TEXT("%s: %s"),*Peer.DisplayName,*Peer.GetStatusLabel()));
			}
		const FString TeammateText=FString::Join(Lines,TEXT("\n"));
		if(!TeammateText.Equals(LastTeammateVoiceText,ESearchCase::CaseSensitive)){LastTeammateVoiceText=TeammateText;TeammateVoiceText->SetText(FText::FromString(TeammateText));}
		TeammateVoiceText->SetVisibility(Lines.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
	}
	const bool bRequestedActive = bConfigured &&
		(Voice->IsOpenMicEnabled() || Voice->IsPushToTalkHeld());
	const bool bReady = bConfigured && Voice->IsVoiceReady();
	const FString Status = bReady
		? (bRequestedActive?TEXT("보이스: 활성"):TEXT("보이스: 비활성"))
		: FString(TEXT("보이스: 비활성\n"))+(bConfigured?Voice->GetConnectionStatusText():FString());
	const FString Key=Status+(bRequestedActive?TEXT("|A"):TEXT("|-"))+(bReady?TEXT("R"):TEXT("-"));
	if(Key.Equals(LastVoiceStatusKey,ESearchCase::CaseSensitive))return; // text, colour and brush all derive from these
	LastVoiceStatusKey=Key;
	VoiceStatusText->SetText(FProjectProject01Localization::Text(Status));
	VoiceStatusText->SetColorAndOpacity(FSlateColor(
		bRequestedActive && bReady ? FLinearColor(0.30f, 0.95f, 0.45f, 1.0f)
		: bRequestedActive ? FLinearColor(1.0f, 0.75f, 0.20f, 1.0f)
		: FLinearColor(0.70f, 0.70f, 0.70f, 1.0f)));
	if (IsValid(StatusBackground))
	{
		StatusBackground->SetBrushColor(bRequestedActive
			? (bReady ? FLinearColor(0.03f, 0.20f, 0.07f, 0.90f)
				: FLinearColor(0.25f, 0.14f, 0.02f, 0.90f))
			: FLinearColor(0.03f, 0.03f, 0.04f, 0.88f));
	}
}

bool UProjectProject01ServiceNoticeWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	ProjectDFSDUI::ApplyTree(WidgetTree);
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

void UProjectProject01SurveyWidget::ConfigureSurvey(
	const FProjectProject01SurveyDefinition& InSurvey,
	UUserWidget* InReturnWidget,
	const bool bInReturnToTitleAfterSurvey)
{
	Survey = InSurvey;
	ReturnWidget = InReturnWidget;
	bReturnToTitleAfterSurvey = bInReturnToTitleAfterSurvey;
	bSubmitted = false;
	bAcknowledgementVisible = false;
	if (IsValid(AcknowledgementPanel)) AcknowledgementPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (IsValid(SurveyTitleText)) SurveyTitleText->SetText(FProjectProject01Localization::Text(
		Survey.Title.IsEmpty() ? TEXT("피드백/설문조사") : Survey.Title));
	if (IsValid(SurveyDescriptionText)) SurveyDescriptionText->SetText(FProjectProject01Localization::Text(Survey.Description));
	if (!IsValid(QuestionPanel)) return;
	QuestionPanel->ClearChildren();
	ChoiceInputs.Reset();
	OptionBindings.Reset();
	SelectedOptions.SetNum(Survey.Questions.Num());
	for (TArray<int32>& Selected : SelectedOptions) Selected.Reset();
	TextInputs.Reset();
	for (int32 Index = 0; Index < Survey.Questions.Num(); ++Index)
	{
		const FProjectProject01SurveyQuestion& Question = Survey.Questions[Index];
		const FString RequiredMark = Question.bRequired ? TEXT(" *") : TEXT("");
		ProjectProject01LoginUI::AddLabel(WidgetTree, QuestionPanel,
			FString::Printf(TEXT("%d. %s%s"), Index + 1, *Question.Prompt, *RequiredMark), 20.0f);
		if (Question.Type == TEXT("Choice") || Question.Type == TEXT("OX"))
		{
			if (Question.bAllowMultiple)
				ProjectProject01LoginUI::AddLabel(WidgetTree, QuestionPanel,
					FString::Printf(TEXT("최대 %d개 선택 가능 · 초과 시 오래된 선택이 해제됩니다."), Question.MaxSelections), 14.0f);
			for (int32 OptionIndex = 0; OptionIndex < Question.Options.Num(); ++OptionIndex)
			{
				UCheckBox* Check = WidgetTree->ConstructWidget<UCheckBox>();
				ProjectDFSDUI::Apply(Check);
				UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
				Label->SetText(FText::FromString(Question.Options[OptionIndex]));
				Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
				FSlateFontInfo AnswerFont=Label->GetFont();AnswerFont.Size=20;Label->SetFont(AnswerFont);Label->SetAutoWrapText(true);
				Check->AddChild(Label);
				QuestionPanel->AddChildToVerticalBox(Check)->SetPadding(FMargin(6.0f));
				UProjectProject01SurveyOptionBinding* Binding = NewObject<UProjectProject01SurveyOptionBinding>(this);
				Binding->Owner = this; Binding->QuestionIndex = Index; Binding->OptionIndex = OptionIndex;
				Check->OnCheckStateChanged.AddDynamic(Binding, &UProjectProject01SurveyOptionBinding::HandleChanged);
				ChoiceInputs.Add(Check);
				OptionBindings.Add(Binding);
			}
			TextInputs.Add(nullptr);
		}
		else
		{
			UMultiLineEditableTextBox* TextInput = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
			ProjectDFSDUI::Apply(TextInput);
			TextInput->SetHintText(FProjectProject01Localization::Text(TEXT("답변을 입력해주세요.")));
			// Survey answers use white text on a dark field, including the focused state.
			TextInput->WidgetStyle.SetForegroundColor(FLinearColor::White);
			TextInput->WidgetStyle.SetFocusedForegroundColor(FLinearColor::White);
			TextInput->WidgetStyle.SetReadOnlyForegroundColor(FLinearColor::White);
			TextInput->WidgetStyle.TextStyle.SetColorAndOpacity(FSlateColor(FLinearColor::White));
			TextInput->WidgetStyle.SetBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f, 1.0f));
			TextInput->SetForegroundColor(FLinearColor::White);
			ProjectProject01LoginUI::EnlargeInput(TextInput);
			TextInput->SetAutoWrapText(true);
			ProjectProject01LoginUI::AddLargeField(WidgetTree,QuestionPanel,TextInput,160);
			TextInputs.Add(TextInput);
		}
	}
}

void UProjectProject01SurveyOptionBinding::HandleChanged(const bool bChecked)
{
	if (IsValid(Owner)) Owner->HandleOptionChanged(QuestionIndex, OptionIndex, bChecked);
}

void UProjectProject01SurveyWidget::HandleOptionChanged(const int32 QuestionIndex, const int32 OptionIndex, const bool bChecked)
{
	if (bUpdatingChoices || !Survey.Questions.IsValidIndex(QuestionIndex)) return;
	TArray<int32>& Selected = SelectedOptions[QuestionIndex];
	const FProjectProject01SurveyQuestion& Question = Survey.Questions[QuestionIndex];
	Question.ApplySelection(Selected, OptionIndex, bChecked);
	bUpdatingChoices = true;
	for (int32 Index = 0; Index < OptionBindings.Num(); ++Index)
	{
		const UProjectProject01SurveyOptionBinding* Binding = OptionBindings[Index];
		if (Binding->QuestionIndex == QuestionIndex)
			ChoiceInputs[Index]->SetIsChecked(Selected.Contains(Binding->OptionIndex));
	}
	bUpdatingChoices = false;
}

bool UProjectProject01SurveyWidget::Initialize()
{
	if (!Super::Initialize()) return false;
	SetIsFocusable(true);
	BuildWidgetTree();
	ProjectDFSDUI::ApplyTree(WidgetTree);
	return true;
}

void UProjectProject01SurveyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(SubmitButton)) SubmitButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SurveyWidget::HandleSubmitClicked);
	if (IsValid(BackButton)) BackButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SurveyWidget::HandleBackClicked);
	if (IsValid(AcknowledgementButton)) AcknowledgementButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SurveyWidget::HandleAcknowledgementClicked);
	SetBusy(false);
	SetKeyboardFocus();
}

void UProjectProject01SurveyWidget::NativeDestruct()
{
	if (IsValid(SubmitButton)) SubmitButton->OnClicked.RemoveDynamic(this, &UProjectProject01SurveyWidget::HandleSubmitClicked);
	if (IsValid(BackButton)) BackButton->OnClicked.RemoveDynamic(this, &UProjectProject01SurveyWidget::HandleBackClicked);
	if (IsValid(AcknowledgementButton)) AcknowledgementButton->OnClicked.RemoveDynamic(this, &UProjectProject01SurveyWidget::HandleAcknowledgementClicked);
	if (const UGameInstance* Instance = GetGameInstance(); IsValid(Instance))
		if (UProjectProject01AuthSubsystem* Auth = Instance->GetSubsystem<UProjectProject01AuthSubsystem>())
			Auth->OnLogoutCompleted.RemoveDynamic(this, &UProjectProject01SurveyWidget::HandleSurveyLogoutCompleted);
	Super::NativeDestruct();
}

FReply UProjectProject01SurveyWidget::NativeOnKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && !bBusy)
	{
		if (!bAcknowledgementVisible) HandleBackClicked();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UProjectProject01SurveyWidget::HandleSubmitClicked()
{
	if (bBusy || bSubmitted || bAcknowledgementVisible || !Survey.IsValid()) return;
	TArray<FProjectProject01SurveyAnswer> Answers;
	for (int32 Index = 0; Index < Survey.Questions.Num(); ++Index)
	{
		const FProjectProject01SurveyQuestion& Question = Survey.Questions[Index];
		FString Value;
		TArray<FString> Values;
		if (Question.Type == TEXT("Choice") || Question.Type == TEXT("OX"))
		{
			for (int32 OptionIndex : SelectedOptions[Index])
				if (Question.Options.IsValidIndex(OptionIndex)) Values.Add(Question.Options[OptionIndex]);
			Value = FString::Join(Values, TEXT(", "));
		}
		else if (TextInputs.IsValidIndex(Index) && IsValid(TextInputs[Index]))
		{
			Value = TextInputs[Index]->GetText().ToString().TrimStartAndEnd();
		}
		if (Question.bRequired && Value.IsEmpty())
		{
			SetBusy(false, FString::Printf(TEXT("필수 문항 %d에 응답해주세요."), Index + 1), true);
			return;
		}
		if (Value.Len() > 2000)
		{
			SetBusy(false, FString::Printf(TEXT("%d번 답변은 2000자 이하로 입력해주세요."), Index + 1), true);
			return;
		}
		FProjectProject01SurveyAnswer& Answer = Answers.AddDefaulted_GetRef();
		Answer.QuestionId = Question.Id;
		Answer.Value = MoveTemp(Value);
		Answer.Values = MoveTemp(Values);
	}
	UProjectProject01GameInstance* GameInstance = Cast<UProjectProject01GameInstance>(GetGameInstance());
	if (!IsValid(GameInstance))
	{
		SetBusy(false, TEXT("설문 전송 시스템을 찾지 못했습니다."), true);
		return;
	}
	SetBusy(true, TEXT("설문 응답을 전송하는 중..."));
	const TWeakObjectPtr<UProjectProject01SurveyWidget> WeakThis(this);
	GameInstance->SubmitPlayerSurvey(Survey, Answers,
		[WeakThis](const bool bSucceeded, const FString& ResponseMessage)
		{
			if (UProjectProject01SurveyWidget* Widget = WeakThis.Get(); IsValid(Widget) && Widget->IsInViewport())
			{
				if (bSucceeded) Widget->ShowSurveyNotice(TEXT("설문에 감사드립니다."), true);
				else Widget->SetBusy(false, ResponseMessage, true);
			}
		});
}

void UProjectProject01SurveyWidget::HandleBackClicked()
{
	if (bBusy || bAcknowledgementVisible) return;
	CloseSurvey();
}

void UProjectProject01SurveyWidget::ShowSurveyNotice(const FString& Message, const bool bSubmissionSucceeded)
{
	bSubmitted = bSubmissionSucceeded;
	bAcknowledgementVisible = true;
	if (IsValid(AcknowledgementText)) AcknowledgementText->SetText(FProjectProject01Localization::Text(Message));
	if (IsValid(AcknowledgementPanel)) AcknowledgementPanel->SetVisibility(ESlateVisibility::Visible);
	SetBusy(false);
	if (IsInViewport() && IsValid(AcknowledgementButton)) AcknowledgementButton->SetKeyboardFocus();
}

void UProjectProject01SurveyWidget::HandleAcknowledgementClicked()
{
	if (bBusy || !bAcknowledgementVisible) return;
	bAcknowledgementVisible = false;
	if (IsValid(AcknowledgementPanel)) AcknowledgementPanel->SetVisibility(ESlateVisibility::Collapsed);
	CloseSurvey();
}

void UProjectProject01SurveyWidget::CloseSurvey()
{
	if (bReturnToTitleAfterSurvey)
	{
		SetBusy(true, TEXT("로그아웃 후 타이틀로 돌아가는 중..."));
		if (UProjectProject01LobbyWidget* LobbyWidget = Cast<UProjectProject01LobbyWidget>(ReturnWidget))
			LobbyWidget->PrepareTitleSurveyLogout();
		UGameInstance* Instance = GetGameInstance();
		UProjectProject01AuthSubsystem* Auth = IsValid(Instance) ? Instance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
		if (!IsValid(Auth)) { FinishSurveyLogout(); return; }
		Auth->OnLogoutCompleted.AddUniqueDynamic(this, &UProjectProject01SurveyWidget::HandleSurveyLogoutCompleted);
		Auth->Logout();
		return;
	}
	if (IsValid(ReturnWidget))
	{
		ReturnWidget->SetVisibility(ESlateVisibility::Visible);
		ReturnWidget->SetKeyboardFocus();
	}
	RemoveFromParent();
}

void UProjectProject01SurveyWidget::HandleSurveyLogoutCompleted(bool, const FString&, const FString&)
{
	FinishSurveyLogout();
}

void UProjectProject01SurveyWidget::FinishSurveyLogout()
{
	UGameInstance* Instance = GetGameInstance();
	if (IsValid(Instance))
	{
		if (UProjectProject01AuthSubsystem* Auth = Instance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnLogoutCompleted.RemoveDynamic(this, &UProjectProject01SurveyWidget::HandleSurveyLogoutCompleted);
			Auth->ForceClearLocalSession();
		}
		if (UProjectProject01LobbySubsystem* Lobby = Instance->GetSubsystem<UProjectProject01LobbySubsystem>()) Lobby->ForceClearLocalSession();
		if (UProjectProject01GameInstance* ProjectInstance = Cast<UProjectProject01GameInstance>(Instance)) ProjectInstance->SetSurveyLoginRequested(false);
	}
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/TitleLevel")), true);
}

void UProjectProject01SurveyWidget::SetBusy(
	const bool bInBusy,
	const FString& Message,
	const bool bIsError)
{
	bBusy = bInBusy;
	if (IsValid(SubmitButton)) SubmitButton->SetIsEnabled(!bBusy && !bSubmitted && !bAcknowledgementVisible && Survey.IsValid());
	if (IsValid(BackButton)) BackButton->SetIsEnabled(!bBusy && !bAcknowledgementVisible);
	if (IsValid(AcknowledgementButton)) AcknowledgementButton->SetIsEnabled(!bBusy);
	for (UCheckBox* Input : ChoiceInputs) if (IsValid(Input)) Input->SetIsEnabled(!bBusy && !bSubmitted && !bAcknowledgementVisible);
	for (UMultiLineEditableTextBox* Input : TextInputs) if (IsValid(Input)) Input->SetIsReadOnly(bBusy || bSubmitted || bAcknowledgementVisible);
	if (IsValid(StatusText))
	{
		StatusText->SetText(FProjectProject01Localization::Text(Message));
		StatusText->SetColorAndOpacity(bIsError
			? FSlateColor(ProjectDFSDUI::Danger()) : FSlateColor(ProjectDFSDUI::Text()));
	}
}

void UProjectProject01SurveyWidget::BuildWidgetTree()
{
	if (!ensureMsgf(IsValid(WidgetTree), TEXT("Survey widget has no WidgetTree."))) return;
	UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Overlay;
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>();
	Dim->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.78f));
	if (UOverlaySlot* DimSlot = Overlay->AddChildToOverlay(Dim))
	{
		DimSlot->SetHorizontalAlignment(HAlign_Fill);
		DimSlot->SetVerticalAlignment(VAlign_Fill);
	}
	USizeBox* PanelFrame = WidgetTree->ConstructWidget<USizeBox>();
	PanelFrame->SetWidthOverride(760.0f);
	PanelFrame->SetHeightOverride(720.0f);
	if (UOverlaySlot* PanelSlot = Overlay->AddChildToOverlay(PanelFrame))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
		PanelSlot->SetPadding(FMargin(48.0f));
	}
	UVerticalBox* Panel = WidgetTree->ConstructWidget<UVerticalBox>();
	PanelFrame->AddChild(Panel);
	SurveyTitleText = ProjectProject01LoginUI::AddLabel(WidgetTree, Panel, TEXT("피드백/설문조사"), 30.0f);
	SurveyDescriptionText = ProjectProject01LoginUI::AddLabel(WidgetTree, Panel, TEXT(""), 14.0f);
	UScrollBox* QuestionScroll = WidgetTree->ConstructWidget<UScrollBox>();
	QuestionPanel = WidgetTree->ConstructWidget<UVerticalBox>();
	QuestionScroll->AddChild(QuestionPanel);
	if (UVerticalBoxSlot* QuestionSlot = Panel->AddChildToVerticalBox(QuestionScroll))
	{
		QuestionSlot->SetPadding(FMargin(4.0f));
		QuestionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	StatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Panel, TEXT(""), 14.0f);
	SubmitButton = ProjectProject01LoginUI::AddButton(WidgetTree, Panel, TEXT("설문 제출"));
	BackButton = ProjectProject01LoginUI::AddButton(WidgetTree, Panel, TEXT("돌아가기"));
	AcknowledgementPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SurveyAcknowledgement"));
	AcknowledgementPanel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	AcknowledgementPanel->SetHorizontalAlignment(HAlign_Center);
	AcknowledgementPanel->SetVerticalAlignment(VAlign_Center);
	if (UOverlaySlot* NoticeSlot = Overlay->AddChildToOverlay(AcknowledgementPanel))
	{
		NoticeSlot->SetHorizontalAlignment(HAlign_Fill);
		NoticeSlot->SetVerticalAlignment(VAlign_Fill);
	}
	UVerticalBox* Notice = WidgetTree->ConstructWidget<UVerticalBox>();
	AcknowledgementPanel->AddChild(Notice);
	AcknowledgementText = ProjectProject01LoginUI::AddLabel(WidgetTree, Notice, TEXT("설문에 감사드립니다."), 24.0f);
	AcknowledgementButton = ProjectProject01LoginUI::AddButton(WidgetTree, Notice, TEXT("확인"));
	AcknowledgementPanel->SetVisibility(ESlateVisibility::Collapsed);
}

bool UProjectProject01TitleWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	SetIsFocusable(true);
	BuildWidgetTree();
	ProjectDFSDUI::ApplyTree(WidgetTree);
	// Only the home menu keeps its original subtle surface; save-slot and other windows use readable panels.
	for (UButton* Button : {SinglePlayerButton.Get(), ContinueSinglePlayerButton.Get(), MultiplayerButton.Get(),
		SettingsButton.Get(), SurveyButton.Get(), QuitButton.Get()})
		if (IsValid(Button)) Button->SetStyle(ProjectDFSDUI::TitleButtonStyle());
	return true;
}

void UProjectProject01TitleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UProjectProject01GameInstance* Instance = Cast<UProjectProject01GameInstance>(GetGameInstance()))
		Instance->SetSurveyLoginRequested(false);
	bNavigationRequested = false;
	bMultiplayerServiceAvailable = false;
	bSinglePlayerFadeActive = false;
	SinglePlayerFadeElapsedSeconds = 0.0f;
	PendingSinglePlayerLaunchAction = ESinglePlayerLaunchAction::None;
	PendingSinglePlayerLaunchSlot = INDEX_NONE;
	if (IsValid(SinglePlayerFadeOverlay))
	{
		SinglePlayerFadeOverlay->SetRenderOpacity(0.0f);
		SinglePlayerFadeOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

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
				ContinueSinglePlayerButton->SetIsEnabled(Saves->HasAnyRecoverableSave());
			}
		}
	}
	if (SaveSlotPlayButtons.Num() == UProjectProject01SaveSubsystem::SaveSlotCount)
	{
		SaveSlotPlayButtons[0]->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot1PlayClicked);
		SaveSlotPlayButtons[1]->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot2PlayClicked);
		SaveSlotPlayButtons[2]->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot3PlayClicked);
		SaveSlotNewGameButtons[0]->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot1NewGameClicked);
		SaveSlotNewGameButtons[1]->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot2NewGameClicked);
		SaveSlotNewGameButtons[2]->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot3NewGameClicked);
		SaveSlotDeleteButtons[0]->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot1DeleteClicked);
		SaveSlotDeleteButtons[1]->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot2DeleteClicked);
		SaveSlotDeleteButtons[2]->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot3DeleteClicked);
	}
	if (IsValid(SaveSlotsBackButton))
	{
		SaveSlotsBackButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlotsBackClicked);
	}
	if (IsValid(SaveSlotConfirmationYesButton))
	{
		SaveSlotConfirmationYesButton->OnClicked.AddUniqueDynamic(
			this, &UProjectProject01TitleWidget::HandleSaveSlotConfirmationYesClicked);
	}
	if (IsValid(SaveSlotConfirmationNoButton))
	{
		SaveSlotConfirmationNoButton->OnClicked.AddUniqueDynamic(
			this, &UProjectProject01TitleWidget::HandleSaveSlotConfirmationNoClicked);
	}
	if (IsValid(MultiplayerButton))
	{
		MultiplayerButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleMultiplayerClicked);
	}
	if (IsValid(SettingsButton))
	{
		SettingsButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSettingsClicked);
	}
	if (IsValid(SurveyButton)) SurveyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleSurveyClicked);
	if (IsValid(QuitButton))
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01TitleWidget::HandleQuitClicked);
	}
	if (IsValid(TitlePageSwitcher)) TitlePageSwitcher->SetActiveWidgetIndex(0);
	HideSaveSlotConfirmation();
	if (UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnServiceStatusChanged.AddUniqueDynamic(
				this, &UProjectProject01TitleWidget::HandleServiceStatusChanged);
		}
	}
	if (IsValid(MultiplayerButton)) MultiplayerButton->SetIsEnabled(false);
	if (IsValid(SurveyButton)) SurveyButton->SetIsEnabled(false);
	if (IsValid(StatusText))
	{
		StatusText->SetText(FProjectProject01Localization::Text(TEXT("멀티플레이 서버 상태 확인 중...")));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(.65f, .68f, .72f)));
	}
	PollMultiplayerService();
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().SetTimer(
			ServiceStatusTimer, this, &UProjectProject01TitleWidget::PollMultiplayerService,
			5.0f, true, 5.0f);
	}
}

void UProjectProject01TitleWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bSinglePlayerFadeActive || !IsValid(SinglePlayerFadeOverlay))
	{
		return;
	}

	SinglePlayerFadeElapsedSeconds = FMath::Min(
		SinglePlayerFadeElapsedSeconds + InDeltaTime, SinglePlayerFadeDurationSeconds);
	const float FadeAlpha = SinglePlayerFadeDurationSeconds > UE_SMALL_NUMBER
		? SinglePlayerFadeElapsedSeconds / SinglePlayerFadeDurationSeconds
		: 1.0f;
	SinglePlayerFadeOverlay->SetRenderOpacity(FMath::Clamp(FadeAlpha, 0.0f, 1.0f));
}

namespace ProjectProject01TitleRoutes
{
	const FName SinglePlayerLevel(TEXT("/Game/MyProject/Level/LevelTest01"));
	const FName MultiplayerLoginLevel(TEXT("/Game/MyProject/Level/LoginLevel"));
	const FName TitleLevel(TEXT("/Game/MyProject/Level/TitleLevel"));
}

void UProjectProject01TitleWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(ServiceStatusTimer);
		World->GetTimerManager().ClearTimer(SinglePlayerLaunchTimer);
	}
	if (UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->OnServiceStatusChanged.RemoveDynamic(
				this, &UProjectProject01TitleWidget::HandleServiceStatusChanged);
		}
	}
	if (IsValid(SinglePlayerButton))
	{
		SinglePlayerButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSinglePlayerClicked);
	}
	if (IsValid(ContinueSinglePlayerButton))
	{
		ContinueSinglePlayerButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleContinueSinglePlayerClicked);
	}
	if (SaveSlotPlayButtons.Num() == UProjectProject01SaveSubsystem::SaveSlotCount)
	{
		SaveSlotPlayButtons[0]->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot1PlayClicked);
		SaveSlotPlayButtons[1]->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot2PlayClicked);
		SaveSlotPlayButtons[2]->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot3PlayClicked);
		SaveSlotNewGameButtons[0]->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot1NewGameClicked);
		SaveSlotNewGameButtons[1]->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot2NewGameClicked);
		SaveSlotNewGameButtons[2]->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot3NewGameClicked);
		SaveSlotDeleteButtons[0]->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot1DeleteClicked);
		SaveSlotDeleteButtons[1]->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot2DeleteClicked);
		SaveSlotDeleteButtons[2]->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlot3DeleteClicked);
	}
	if (IsValid(SaveSlotsBackButton))
	{
		SaveSlotsBackButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSaveSlotsBackClicked);
	}
	if (IsValid(SaveSlotConfirmationYesButton))
	{
		SaveSlotConfirmationYesButton->OnClicked.RemoveDynamic(
			this, &UProjectProject01TitleWidget::HandleSaveSlotConfirmationYesClicked);
	}
	if (IsValid(SaveSlotConfirmationNoButton))
	{
		SaveSlotConfirmationNoButton->OnClicked.RemoveDynamic(
			this, &UProjectProject01TitleWidget::HandleSaveSlotConfirmationNoClicked);
	}
	if (IsValid(MultiplayerButton))
	{
		MultiplayerButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleMultiplayerClicked);
	}
	if (IsValid(SettingsButton))
	{
		SettingsButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSettingsClicked);
	}
	if (IsValid(SurveyButton)) SurveyButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleSurveyClicked);
	if (IsValid(QuitButton))
	{
		QuitButton->OnClicked.RemoveDynamic(this, &UProjectProject01TitleWidget::HandleQuitClicked);
	}
	Super::NativeDestruct();
}

void UProjectProject01TitleWidget::PollMultiplayerService()
{
	if (bNavigationRequested) return;
	if (UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->CheckMultiplayerServiceStatus();
		}
	}
}

void UProjectProject01TitleWidget::HandleServiceStatusChanged(
	const bool bMaintenanceEnabled,
	const FString& Announcement,
	const FString& ShutdownAtUtc,
	const FString& Message)
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	const bool bReachable = IsValid(Auth) && Auth->IsMultiplayerServiceReachable();
	bMultiplayerServiceAvailable = bReachable && !bMaintenanceEnabled;
	if (IsValid(MultiplayerButton))
	{
		MultiplayerButton->SetIsEnabled(!bNavigationRequested && bMultiplayerServiceAvailable);
	}
	if (IsValid(SurveyButton)) SurveyButton->SetIsEnabled(!bNavigationRequested && bMultiplayerServiceAvailable);
	if (!IsValid(StatusText)) return;
	if (!bReachable)
	{
		StatusText->SetText(FProjectProject01Localization::Text(TEXT("서버가 꺼져 있습니다.")));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, .32f, .22f)));
	}
	else if (bMaintenanceEnabled)
	{
		StatusText->SetText(FProjectProject01Localization::Text(Message.IsEmpty()
			? TEXT("현재 멀티플레이 서버를 점검 중입니다.") : Message));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, .32f, .22f)));
	}
	else if (!Announcement.IsEmpty())
	{
		StatusText->SetText(FProjectProject01Localization::Text(ShutdownAtUtc.IsEmpty() ? Announcement :
			FString::Printf(TEXT("%s (점검 자동 해제 예정: %s)"), *Announcement, *ShutdownAtUtc)));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(.65f, .68f, .72f)));
	}
	else
	{
		StatusText->SetText(FProjectProject01Localization::Text(TEXT("멀티플레이 서버에 연결할 수 있습니다.")));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(.65f, .68f, .72f)));
	}
}

void UProjectProject01TitleWidget::HandleSinglePlayerClicked()
{
	if (bNavigationRequested || !IsValid(TitlePageSwitcher))
	{
		return;
	}
	RefreshSaveSlotPage();
	TitlePageSwitcher->SetActiveWidgetIndex(1);
}

void UProjectProject01TitleWidget::HandleContinueSinglePlayerClicked()
{
	if (bNavigationRequested) return;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (!IsValid(Saves) || !Saves->HasAnyRecoverableSave()) return;
	QueueSinglePlayerLaunch(ESinglePlayerLaunchAction::ContinueRecent);
}

void UProjectProject01TitleWidget::HandleSaveSlot1PlayClicked() { PlaySaveSlot(1); }
void UProjectProject01TitleWidget::HandleSaveSlot2PlayClicked() { PlaySaveSlot(2); }
void UProjectProject01TitleWidget::HandleSaveSlot3PlayClicked() { PlaySaveSlot(3); }
void UProjectProject01TitleWidget::HandleSaveSlot1NewGameClicked() { RequestStartNewGameInSaveSlot(1); }
void UProjectProject01TitleWidget::HandleSaveSlot2NewGameClicked() { RequestStartNewGameInSaveSlot(2); }
void UProjectProject01TitleWidget::HandleSaveSlot3NewGameClicked() { RequestStartNewGameInSaveSlot(3); }
void UProjectProject01TitleWidget::HandleSaveSlot1DeleteClicked() { RequestDeleteSaveSlot(1); }
void UProjectProject01TitleWidget::HandleSaveSlot2DeleteClicked() { RequestDeleteSaveSlot(2); }
void UProjectProject01TitleWidget::HandleSaveSlot3DeleteClicked() { RequestDeleteSaveSlot(3); }

void UProjectProject01TitleWidget::HandleSaveSlotsBackClicked()
{
	if (!bNavigationRequested && IsValid(TitlePageSwitcher))
	{
		TitlePageSwitcher->SetActiveWidgetIndex(0);
	}
}

void UProjectProject01TitleWidget::RefreshSaveSlotPage()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (!IsValid(Saves)) return;
	for (int32 Index = 0; Index < UProjectProject01SaveSubsystem::SaveSlotCount; ++Index)
	{
		const int32 SlotIndex = Index + 1;
		const FProjectProject01SaveSlotInfo Info = Saves->GetSaveSlotInfo(SlotIndex, true);
		ProjectProject01LoginUI::RefreshSavePreview(SaveSlotPreviewImages, Index, Info.PreviewTexture);
		const FString CurrentMark = Saves->GetCurrentSlotIndex() == SlotIndex ? TEXT(" (현재)") : TEXT("");
		if (SaveSlotStatusTexts.IsValidIndex(Index) && IsValid(SaveSlotStatusTexts[Index]))
		{
			const FString Description = ProjectProject01LoginUI::SaveSlotDescription(Info, CurrentMark);
			SaveSlotStatusTexts[Index]->SetText(FProjectProject01Localization::Text(Description));
		}
		if (SaveSlotPlayButtons.IsValidIndex(Index) && IsValid(SaveSlotPlayButtons[Index]))
		{
			SaveSlotPlayButtons[Index]->SetIsEnabled(Info.bHasSave && !bNavigationRequested);
		}
		if (SaveSlotDeleteButtons.IsValidIndex(Index) && IsValid(SaveSlotDeleteButtons[Index]))
		{
			SaveSlotDeleteButtons[Index]->SetIsEnabled(Info.bHasSave && !bNavigationRequested);
		}
		if (SaveSlotNewGameButtons.IsValidIndex(Index) && IsValid(SaveSlotNewGameButtons[Index]))
		{
			SaveSlotNewGameButtons[Index]->SetIsEnabled(!bNavigationRequested);
		}
	}
	if (IsValid(SaveSlotsMessageText))
	{
		SaveSlotsMessageText->SetText(FProjectProject01Localization::Text(
			TEXT("플레이는 저장 시점을 이어하고, 새 게임은 해당 저장소를 초기화합니다.")));
		SaveSlotsMessageText->SetColorAndOpacity(FSlateColor(ProjectDFSDUI::Text()));
	}
}

void UProjectProject01TitleWidget::BeginSinglePlayerNavigation()
{
	bNavigationRequested = true;
	bSinglePlayerFadeActive = true;
	SinglePlayerFadeElapsedSeconds = 0.0f;
	SetMenuEnabled(false);
	if (IsValid(SinglePlayerFadeOverlay))
	{
		SinglePlayerFadeOverlay->SetRenderOpacity(0.0f);
		SinglePlayerFadeOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		PlayerController->bShowMouseCursor = false;
		PlayerController->SetInputMode(FInputModeGameOnly());
	}
}

void UProjectProject01TitleWidget::QueueSinglePlayerLaunch(
	const ESinglePlayerLaunchAction Action,
	const int32 SlotIndex)
{
	if (bNavigationRequested || Action == ESinglePlayerLaunchAction::None || !IsValid(GetWorld()))
	{
		return;
	}
	PendingSinglePlayerLaunchAction = Action;
	PendingSinglePlayerLaunchSlot = SlotIndex;
	BeginSinglePlayerNavigation();
	GetWorld()->GetTimerManager().SetTimer(
		SinglePlayerLaunchTimer, this, &UProjectProject01TitleWidget::CompleteSinglePlayerLaunch,
		SinglePlayerFadeDurationSeconds, false);
}

void UProjectProject01TitleWidget::CompleteSinglePlayerLaunch()
{
	SinglePlayerFadeElapsedSeconds = SinglePlayerFadeDurationSeconds;
	if (IsValid(SinglePlayerFadeOverlay))
	{
		SinglePlayerFadeOverlay->SetRenderOpacity(1.0f);
	}
	if (UProjectProject01GameInstance* ProjectGameInstance =
		Cast<UProjectProject01GameInstance>(GetGameInstance()))
	{
		ProjectGameInstance->HoldEntireScreenBlack();
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (!IsValid(Saves))
	{
		CancelSinglePlayerLaunch(TEXT("싱글플레이 저장 시스템을 찾지 못했습니다."));
		return;
	}

	bool bStarted = false;
	switch (PendingSinglePlayerLaunchAction)
	{
	case ESinglePlayerLaunchAction::ContinueRecent:
		bStarted = Saves->LoadMostRecentlyPlayedCheckpoint();
		break;
	case ESinglePlayerLaunchAction::PlaySlot:
		bStarted = Saves->LoadCheckpointFromSlot(PendingSinglePlayerLaunchSlot);
		break;
	case ESinglePlayerLaunchAction::NewGameSlot:
		bStarted = Saves->StartNewGameInSlot(PendingSinglePlayerLaunchSlot);
		if (bStarted)
		{
			// 싱글 새 게임은 백엔드·로비·데디케이티드 서버에 접속하지 않는다.
			UGameplayStatics::OpenLevel(this, ProjectProject01TitleRoutes::SinglePlayerLevel, true);
		}
		break;
	default:
		break;
	}

	if (!bStarted)
	{
		CancelSinglePlayerLaunch(TEXT("선택한 싱글플레이를 시작하지 못했습니다."));
	}
}

void UProjectProject01TitleWidget::CancelSinglePlayerLaunch(const FString& ErrorMessage)
{
	bSinglePlayerFadeActive = false;
	SinglePlayerFadeElapsedSeconds = 0.0f;
	PendingSinglePlayerLaunchAction = ESinglePlayerLaunchAction::None;
	PendingSinglePlayerLaunchSlot = INDEX_NONE;
	bNavigationRequested = false;
	SetMenuEnabled(true);
	RefreshSaveSlotPage();
	if (IsValid(SaveSlotsMessageText))
	{
		SaveSlotsMessageText->SetText(FProjectProject01Localization::Text(ErrorMessage));
		SaveSlotsMessageText->SetColorAndOpacity(FSlateColor(ProjectDFSDUI::Danger()));
	}
	if (IsValid(SinglePlayerFadeOverlay))
	{
		SinglePlayerFadeOverlay->SetRenderOpacity(0.0f);
		SinglePlayerFadeOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (UProjectProject01GameInstance* ProjectGameInstance =
		Cast<UProjectProject01GameInstance>(GetGameInstance()))
	{
		ProjectGameInstance->ClearEntireScreenFade();
	}
	if (APlayerController* PlayerController = GetOwningPlayer(); IsValid(PlayerController))
	{
		if (IsValid(PlayerController->PlayerCameraManager))
		{
			PlayerController->PlayerCameraManager->StopCameraFade();
		}
		PlayerController->bShowMouseCursor = true;
		PlayerController->SetInputMode(FInputModeUIOnly());
	}
}

void UProjectProject01TitleWidget::PlaySaveSlot(const int32 SlotIndex)
{
	if (bNavigationRequested) return;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (!IsValid(Saves) || !Saves->HasRecoverableSaveInSlot(SlotIndex)) return;
	QueueSinglePlayerLaunch(ESinglePlayerLaunchAction::PlaySlot, SlotIndex);
}

void UProjectProject01TitleWidget::RequestStartNewGameInSaveSlot(const int32 SlotIndex)
{
	if (bNavigationRequested) return;
	UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (IsValid(Saves) && Saves->HasRecoverableSaveInSlot(SlotIndex))
	{
		ShowSaveSlotConfirmation(ESaveSlotConfirmationAction::StartNewGame, SlotIndex);
		return;
	}
	StartNewGameInSaveSlot(SlotIndex);
}

void UProjectProject01TitleWidget::RequestDeleteSaveSlot(const int32 SlotIndex)
{
	if (bNavigationRequested) return;
	UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (IsValid(Saves) && Saves->HasRecoverableSaveInSlot(SlotIndex))
	{
		ShowSaveSlotConfirmation(ESaveSlotConfirmationAction::Delete, SlotIndex);
	}
}

void UProjectProject01TitleWidget::ShowSaveSlotConfirmation(
	const ESaveSlotConfirmationAction Action,
	const int32 SlotIndex)
{
	if (Action == ESaveSlotConfirmationAction::None ||
		SlotIndex < 1 || SlotIndex > UProjectProject01SaveSubsystem::SaveSlotCount)
	{
		return;
	}
	PendingSaveSlotConfirmationAction = Action;
	PendingSaveSlotConfirmationSlot = SlotIndex;
	if (IsValid(SaveSlotConfirmationText))
	{
		SaveSlotConfirmationText->SetText(FProjectProject01Localization::Text(
			Action == ESaveSlotConfirmationAction::Delete
				? TEXT("정말로 이 저장소를 삭제하시겠습니까?")
				: TEXT("정말로 새로 시작하시겠습니까?")));
	}
	if (IsValid(TitlePageSwitcher)) TitlePageSwitcher->SetIsEnabled(false);
	if (IsValid(SaveSlotConfirmationBlur))
	{
		SaveSlotConfirmationBlur->SetVisibility(ESlateVisibility::Visible);
	}
	if (IsValid(SaveSlotConfirmationNoButton)) SaveSlotConfirmationNoButton->SetKeyboardFocus();
}

void UProjectProject01TitleWidget::HideSaveSlotConfirmation()
{
	PendingSaveSlotConfirmationAction = ESaveSlotConfirmationAction::None;
	PendingSaveSlotConfirmationSlot = INDEX_NONE;
	if (IsValid(SaveSlotConfirmationBlur))
	{
		SaveSlotConfirmationBlur->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IsValid(TitlePageSwitcher)) TitlePageSwitcher->SetIsEnabled(true);
}

void UProjectProject01TitleWidget::HandleSaveSlotConfirmationYesClicked()
{
	const ESaveSlotConfirmationAction Action = PendingSaveSlotConfirmationAction;
	const int32 SlotIndex = PendingSaveSlotConfirmationSlot;
	HideSaveSlotConfirmation();
	if (Action == ESaveSlotConfirmationAction::StartNewGame)
	{
		StartNewGameInSaveSlot(SlotIndex);
	}
	else if (Action == ESaveSlotConfirmationAction::Delete)
	{
		DeleteSaveSlot(SlotIndex);
	}
}

void UProjectProject01TitleWidget::HandleSaveSlotConfirmationNoClicked()
{
	HideSaveSlotConfirmation();
}

void UProjectProject01TitleWidget::StartNewGameInSaveSlot(const int32 SlotIndex)
{
	if (bNavigationRequested || !IsValid(GetWorld())) return;
	QueueSinglePlayerLaunch(ESinglePlayerLaunchAction::NewGameSlot, SlotIndex);
}

void UProjectProject01TitleWidget::DeleteSaveSlot(const int32 SlotIndex)
{
	if (bNavigationRequested) return;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	const bool bDeleted = IsValid(Saves) && Saves->DeleteSaveSlot(SlotIndex);
	RefreshSaveSlotPage();
	if (IsValid(ContinueSinglePlayerButton))
	{
		ContinueSinglePlayerButton->SetIsEnabled(IsValid(Saves) && Saves->HasAnyRecoverableSave());
	}
	if (IsValid(SaveSlotsMessageText))
	{
		SaveSlotsMessageText->SetText(FProjectProject01Localization::Text(bDeleted
			? FString::Printf(TEXT("저장소 %d을(를) 삭제했습니다."), SlotIndex)
			: FString::Printf(TEXT("저장소 %d을(를) 삭제하지 못했습니다."), SlotIndex)));
		SaveSlotsMessageText->SetColorAndOpacity(bDeleted
			? FSlateColor(ProjectDFSDUI::Text()) : FSlateColor(ProjectDFSDUI::Danger()));
	}
}

void UProjectProject01TitleWidget::HandleMultiplayerClicked()
{
	NavigateToMultiplayer(false);
}

void UProjectProject01TitleWidget::HandleSurveyClicked()
{
	NavigateToMultiplayer(true);
}

void UProjectProject01TitleWidget::NavigateToMultiplayer(const bool bForSurvey)
{
	if (bNavigationRequested || !IsValid(GetWorld()))
	{
		return;
	}
	if (!bMultiplayerServiceAvailable)
	{
		if (IsValid(StatusText))
		{
			StatusText->SetText(FProjectProject01Localization::Text(TEXT("서버가 꺼져 있습니다.")));
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, .32f, .22f)));
		}
		return;
	}
	UProjectProject01AuthSubsystem* Auth = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
	{
		Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>();
	}
	if (IsValid(Auth) && Auth->IsSignedIn())
	{
		if (UProjectProject01GameInstance* Instance = Cast<UProjectProject01GameInstance>(GetGameInstance()))
			Instance->SetSurveyLoginRequested(bForSurvey);
		bNavigationRequested = true;
		SetMenuEnabled(false);
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LobbyLevel")), true);
		return;
	}
	if (IsValid(Auth) && Auth->GetAuthState() != EProjectProject01AuthState::SignedOut)
	{
		if (IsValid(StatusText))
		{
			StatusText->SetText(FProjectProject01Localization::Text(
				TEXT("이전 로그인 요청을 처리 중입니다. 서버가 응답하지 않으면 5초 안에 해제됩니다.")));
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, .32f, .22f)));
		}
		return;
	}
	const UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get();
	const FString ServerAddress = IsValid(Settings) ? Settings->GetDirectServerAddress() : FString();
	if (!ProjectProject01LoginUI::IsSupportedServerIpv4(ServerAddress))
	{
		if (IsValid(StatusText))
		{
			StatusText->SetText(FProjectProject01Localization::Text(
				TEXT("환경설정에서 접속할 서버 PC의 IPv4 주소를 먼저 입력하세요.")));
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, .32f, .22f)));
		}
		return;
	}
	if (UProjectProject01GameInstance* ProjectGameInstance = Cast<UProjectProject01GameInstance>(GetGameInstance());
		IsValid(ProjectGameInstance))
	{
		FString ConnectionError;
		if (!ProjectGameInstance->ConfigureMultiplayerServerAddress(ServerAddress, ConnectionError))
		{
			if (IsValid(StatusText))
			{
				StatusText->SetText(FProjectProject01Localization::Text(ConnectionError));
				StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, .32f, .22f)));
			}
			return;
		}
	}

	bNavigationRequested = true;
	SetMenuEnabled(false);
	if (UProjectProject01GameInstance* Instance = Cast<UProjectProject01GameInstance>(GetGameInstance()))
		Instance->SetSurveyLoginRequested(bForSurvey);
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

	// Place the readability gradient UNDER the controls (NativePaint would cover the text).
	auto* ScrimTexture = UTexture2D::CreateTransient(256, 1, PF_B8G8R8A8);
	ScrimTexture->SRGB = true; ScrimTexture->Filter = TF_Bilinear;
	ScrimTexture->AddressX = TA_Clamp; ScrimTexture->AddressY = TA_Clamp;
	auto* Pixels = static_cast<FColor*>(ScrimTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 X = 0; X < 256; ++X)
	{
		const float Alpha = .92f * (1 - FMath::SmoothStep(.136f, .68f, X / 255.0f));
		Pixels[X] = FColor(3, 4, 5, FMath::RoundToInt(Alpha * 255));
	}
	ScrimTexture->GetPlatformData()->Mips[0].BulkData.Unlock(); ScrimTexture->UpdateResource();
	UImage* Background = WidgetTree->ConstructWidget<UImage>();
	Background->SetBrushFromTexture(ScrimTexture); Background->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* BackgroundSlot = Overlay->AddChildToOverlay(Background))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	TitlePageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	if (UOverlaySlot* MenuSlot = Overlay->AddChildToOverlay(TitlePageSwitcher))
	{
		MenuSlot->SetHorizontalAlignment(HAlign_Fill);
		MenuSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// Camera and home UI share a centered 16:9 frame: fit/letterbox, never stretch or crop.
	UScaleBox* HomeFit = WidgetTree->ConstructWidget<UScaleBox>(); HomeFit->SetStretch(EStretch::ScaleToFit);
	TitlePageSwitcher->AddChild(HomeFit);
	USizeBox* HomeSize = WidgetTree->ConstructWidget<USizeBox>();
	HomeSize->SetWidthOverride(1600); HomeSize->SetHeightOverride(900); HomeFit->AddChild(HomeSize);
	UCanvasPanel* Home = WidgetTree->ConstructWidget<UCanvasPanel>(); HomeSize->AddChild(Home);
	const auto TitleLabel = [&](const FString& Text, FVector2D Position, FVector2D Extent, int32 FontSize, FLinearColor Color)
	{
		auto* Label = WidgetTree->ConstructWidget<UTextBlock>(); Label->SetText(FProjectProject01Localization::Text(Text));
		FSlateFontInfo Font = Label->GetFont(); Font.Size = FontSize; Label->SetFont(Font);
		Label->SetColorAndOpacity(FSlateColor(Color)); Label->SetAutoWrapText(true);
		auto* Slot = Home->AddChildToCanvas(Label); Slot->SetPosition(Position); Slot->SetSize(Extent); return Label;
	};
	TitleLabel(TEXT("Team DSFD"), FVector2D(110, 116), FVector2D(520, 35), 17, FLinearColor(.55f, .58f, .63f));
	TitleLabel(ProjectDFSDBranding::GameTitle(), FVector2D(105, 165), FVector2D(650, 100), 51, FLinearColor(.94f, .92f, .87f));
	auto* Accent = WidgetTree->ConstructWidget<UBorder>(); Accent->SetBrushColor(FLinearColor(.45f, .12f, .08f));
	auto* AccentSlot = Home->AddChildToCanvas(Accent); AccentSlot->SetPosition(FVector2D(112, 280)); AccentSlot->SetSize(FVector2D(90, 2));
	UVerticalBox* Menu = WidgetTree->ConstructWidget<UVerticalBox>();
	auto* MenuPosition = Home->AddChildToCanvas(Menu); MenuPosition->SetPosition(FVector2D(105, 328)); MenuPosition->SetSize(FVector2D(470, 440));
	const auto MenuButton = [&](const FString& Text)
	{
		auto* Box = WidgetTree->ConstructWidget<USizeBox>(); Box->SetHeightOverride(55);
		Menu->AddChildToVerticalBox(Box)->SetPadding(FMargin(0, 3));
		auto* Button = WidgetTree->ConstructWidget<UButton>(); Box->AddChild(Button);
		FButtonStyle ButtonStyle = Button->GetStyle();
		ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(.01f, .014f, .02f, .25f)));
		ButtonStyle.SetHovered(FSlateColorBrush(FLinearColor(.18f, .12f, .10f, .85f)));
		ButtonStyle.SetPressed(FSlateColorBrush(FLinearColor(.27f, .08f, .05f, .92f)));
		ButtonStyle.SetDisabled(FSlateColorBrush(FLinearColor(.01f, .014f, .02f, .1f)));
		ButtonStyle.SetNormalPadding(FMargin(16, 8)); ButtonStyle.SetPressedPadding(FMargin(18, 9)); Button->SetStyle(ButtonStyle);
		auto* Label = WidgetTree->ConstructWidget<UTextBlock>(); Label->SetText(FProjectProject01Localization::Text(Text));
		FSlateFontInfo Font = Label->GetFont(); Font.Size = 24; Label->SetFont(Font);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor(.86f, .87f, .89f))); Button->AddChild(Label);
		if (auto* ContentSlot = Cast<UButtonSlot>(Label->Slot)) ContentSlot->SetHorizontalAlignment(HAlign_Left);
		return Button;
	};
	SinglePlayerButton = MenuButton(TEXT("싱글 플레이"));
	ContinueSinglePlayerButton = MenuButton(TEXT("이어하기"));
	MultiplayerButton = MenuButton(TEXT("멀티플레이"));
	SettingsButton = MenuButton(TEXT("환경설정"));
	SurveyButton = MenuButton(TEXT("피드백/설문조사"));
	QuitButton = MenuButton(TEXT("게임 종료"));
	StatusText = TitleLabel(TEXT(""), FVector2D(112, 785), FVector2D(560, 80), 14, FLinearColor(.65f, .68f, .72f));

	UOverlay* SavesPage = WidgetTree->ConstructWidget<UOverlay>(); TitlePageSwitcher->AddChild(SavesPage);
	auto* SavesDim = WidgetTree->ConstructWidget<UBorder>(); SavesDim->SetBrushColor(FLinearColor(.006f, .008f, .012f, .9f));
	auto* SavesDimSlot = SavesPage->AddChildToOverlay(SavesDim);
	SavesDimSlot->SetHorizontalAlignment(HAlign_Fill); SavesDimSlot->SetVerticalAlignment(VAlign_Fill);
	auto* SavesFit = WidgetTree->ConstructWidget<UScaleBox>(); SavesFit->SetStretch(EStretch::ScaleToFit);
	auto* SavesFitSlot = SavesPage->AddChildToOverlay(SavesFit);
	SavesFitSlot->SetHorizontalAlignment(HAlign_Fill); SavesFitSlot->SetVerticalAlignment(VAlign_Fill); SavesFitSlot->SetPadding(FMargin(35));
	auto* SavesSize = WidgetTree->ConstructWidget<USizeBox>(); SavesSize->SetWidthOverride(900); SavesSize->SetHeightOverride(800); SavesFit->AddChild(SavesSize);
	UVerticalBox* SaveSlotMenu = WidgetTree->ConstructWidget<UVerticalBox>();
	SavesSize->AddChild(SaveSlotMenu);
	ProjectProject01LoginUI::AddLabel(WidgetTree, SaveSlotMenu, TEXT("싱글플레이 저장소"), 30.0f);
	SaveSlotsMessageText = ProjectProject01LoginUI::AddLabel(
		WidgetTree, SaveSlotMenu,
		TEXT("플레이는 저장 시점을 이어하고, 새 게임은 해당 저장소를 초기화합니다."), 14.0f);
	SaveSlotStatusTexts.Reset();
	SaveSlotPreviewImages.Reset();
	SaveSlotPlayButtons.Reset();
	SaveSlotNewGameButtons.Reset();
	SaveSlotDeleteButtons.Reset();
	for (int32 SlotIndex = 1; SlotIndex <= UProjectProject01SaveSubsystem::SaveSlotCount; ++SlotIndex)
	{
		UImage* Preview = nullptr;
		SaveSlotStatusTexts.Add(ProjectProject01LoginUI::AddSaveSlotSummary(WidgetTree, SaveSlotMenu, Preview));
		SaveSlotPreviewImages.Add(Preview);
		UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (UVerticalBoxSlot* RowSlot = SaveSlotMenu->AddChildToVerticalBox(Actions))
		{
			RowSlot->SetPadding(FMargin(2.0f, 0.0f, 2.0f, 8.0f));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		SaveSlotPlayButtons.Add(ProjectProject01LoginUI::AddTabButton(WidgetTree, Actions, TEXT("플레이")));
		SaveSlotNewGameButtons.Add(ProjectProject01LoginUI::AddTabButton(WidgetTree, Actions, TEXT("새 게임")));
		SaveSlotDeleteButtons.Add(ProjectProject01LoginUI::AddTabButton(WidgetTree, Actions, TEXT("삭제")));
	}
	SaveSlotsBackButton = ProjectProject01LoginUI::AddButton(WidgetTree, SaveSlotMenu, TEXT("돌아가기"));
	TitlePageSwitcher->SetActiveWidgetIndex(0);

	SaveSlotConfirmationBlur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	SaveSlotConfirmationBlur->SetBlurRadius(16);
	SaveSlotConfirmationBlur->SetBlurStrength(12.0f);
	SaveSlotConfirmationBlur->SetVisibility(ESlateVisibility::Collapsed);
	if (UOverlaySlot* ConfirmationSlot = Overlay->AddChildToOverlay(SaveSlotConfirmationBlur))
	{
		ConfirmationSlot->SetHorizontalAlignment(HAlign_Fill);
		ConfirmationSlot->SetVerticalAlignment(VAlign_Fill);
	}
	UBorder* ConfirmationDim = WidgetTree->ConstructWidget<UBorder>();
	ConfirmationDim->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f));
	ConfirmationDim->SetHorizontalAlignment(HAlign_Center);
	ConfirmationDim->SetVerticalAlignment(VAlign_Center);
	SaveSlotConfirmationBlur->AddChild(ConfirmationDim);
	UVerticalBox* ConfirmationPanel = WidgetTree->ConstructWidget<UVerticalBox>();
	ConfirmationDim->AddChild(ConfirmationPanel);
	SaveSlotConfirmationText = ProjectProject01LoginUI::AddLabel(
		WidgetTree, ConfirmationPanel, TEXT("정말로 진행하시겠습니까?"), 20.0f);
	UHorizontalBox* ConfirmationActions = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* ActionsSlot = ConfirmationPanel->AddChildToVerticalBox(ConfirmationActions))
	{
		ActionsSlot->SetPadding(FMargin(2.0f, 8.0f, 2.0f, 2.0f));
		ActionsSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	SaveSlotConfirmationYesButton = ProjectProject01LoginUI::AddTabButton(
		WidgetTree, ConfirmationActions, TEXT("예"));
	SaveSlotConfirmationNoButton = ProjectProject01LoginUI::AddTabButton(
		WidgetTree, ConfirmationActions, TEXT("아니오"));

	SinglePlayerFadeOverlay = WidgetTree->ConstructWidget<UBorder>();
	SinglePlayerFadeOverlay->SetBrushColor(FLinearColor::Black);
	SinglePlayerFadeOverlay->SetRenderOpacity(0.0f);
	SinglePlayerFadeOverlay->SetVisibility(ESlateVisibility::Collapsed);
	if (UOverlaySlot* FadeSlot = Overlay->AddChildToOverlay(SinglePlayerFadeOverlay))
	{
		FadeSlot->SetHorizontalAlignment(HAlign_Fill);
		FadeSlot->SetVerticalAlignment(VAlign_Fill);
	}
}

void UProjectProject01TitleWidget::SetMenuEnabled(const bool bEnabled)
{
	const UProjectProject01SaveSubsystem* Saves = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		Saves = GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>();
	}
	if (IsValid(SinglePlayerButton))
	{
		SinglePlayerButton->SetIsEnabled(bEnabled);
	}
	if (IsValid(ContinueSinglePlayerButton))
	{
		ContinueSinglePlayerButton->SetIsEnabled(bEnabled && IsValid(Saves) && Saves->HasAnyRecoverableSave());
	}
	if (IsValid(MultiplayerButton))
	{
		MultiplayerButton->SetIsEnabled(bEnabled && bMultiplayerServiceAvailable);
	}
	if (IsValid(SettingsButton))
	{
		SettingsButton->SetIsEnabled(bEnabled);
	}
	if (IsValid(SurveyButton)) SurveyButton->SetIsEnabled(bEnabled && bMultiplayerServiceAvailable);
	if (IsValid(QuitButton))
	{
		QuitButton->SetIsEnabled(bEnabled);
	}
	for (int32 Index = 0; Index < SaveSlotPlayButtons.Num(); ++Index)
	{
		if (UButton* Button = SaveSlotPlayButtons[Index]; IsValid(Button))
		{
			Button->SetIsEnabled(bEnabled && IsValid(Saves) && Saves->HasRecoverableSaveInSlot(Index + 1));
		}
	}
	for (UButton* Button : SaveSlotNewGameButtons)
	{
		if (IsValid(Button)) Button->SetIsEnabled(bEnabled);
	}
	for (int32 Index = 0; Index < SaveSlotDeleteButtons.Num(); ++Index)
	{
		if (UButton* Button = SaveSlotDeleteButtons[Index]; IsValid(Button))
		{
			Button->SetIsEnabled(bEnabled && IsValid(Saves) && Saves->HasRecoverableSaveInSlot(Index + 1));
		}
	}
	if (IsValid(SaveSlotsBackButton)) SaveSlotsBackButton->SetIsEnabled(bEnabled);
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
	ProjectDFSDUI::ApplyTree(WidgetTree);
	ProjectDFSDUI::FramePage(WidgetTree,620);
	return true;
}

void UProjectProject01LoginWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bMultiplayerServiceAvailable = false;
	bAuthenticationRequestPending = false;

	if (IsValid(LoginButton))
	{
		LoginButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleLoginClicked);
	}
	if (IsValid(RegisterButton))
	{
		RegisterButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LoginWidget::HandleOpenRegistrationClicked);
	}
	if (RegistrationSubmitButton) RegistrationSubmitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRegisterClicked);
	if (ReturnToLoginButton) ReturnToLoginButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleReturnToLoginClicked);
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
		}
	}
	SetRequestControlsEnabled(false);
	const UProjectProject01GameInstance* Instance = Cast<UProjectProject01GameInstance>(GetGameInstance());
	SetStatus(IsValid(Instance) && Instance->IsSurveyLoginRequested()
		? TEXT("설문 참여를 위해 로그인해주세요. 로그인 후 설문이 열립니다.")
		: TEXT("멀티플레이 서버 상태 확인 중..."), false);
	PollMultiplayerService();
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().SetTimer(
			ServiceStatusTimer, this, &UProjectProject01LoginWidget::PollMultiplayerService,
			5.0f, true, 5.0f);
	}
}

void UProjectProject01LoginWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(ServiceStatusTimer);
	}
	if (IsValid(LoginButton))
	{
		LoginButton->OnClicked.RemoveDynamic(this, &UProjectProject01LoginWidget::HandleLoginClicked);
	}
	if (IsValid(RegisterButton))
	{
		RegisterButton->OnClicked.RemoveDynamic(this, &UProjectProject01LoginWidget::HandleOpenRegistrationClicked);
	}
	if (RegistrationSubmitButton) RegistrationSubmitButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleRegisterClicked);
	if (ReturnToLoginButton) ReturnToLoginButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleReturnToLoginClicked);
	if (PasswordInput) PasswordInput->SetText(FText::GetEmpty());
	if (RegistrationPasswordInput) RegistrationPasswordInput->SetText(FText::GetEmpty());
	bAuthenticationRequestPending = false;
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
			Auth->CancelPendingAuthentication();
		}
	}
	Super::NativeDestruct();
}

void UProjectProject01LoginWidget::PollMultiplayerService()
{
	if (UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->CheckMultiplayerServiceStatus();
		}
	}
}

void UProjectProject01LoginWidget::HandleReturnToTitleClicked()
{
	if (UProjectProject01GameInstance* Instance = Cast<UProjectProject01GameInstance>(GetGameInstance()))
		Instance->SetSurveyLoginRequested(false);
	if (!IsValid(GetWorld()))
	{
		SetStatus(TEXT("타이틀 화면을 열 수 없습니다."), true);
		return;
	}
	if (UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
	{
		if (UProjectProject01AuthSubsystem* Auth = GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			Auth->CancelPendingAuthentication();
		}
	}

	SetRequestControlsEnabled(false);
	UGameplayStatics::OpenLevel(this, ProjectProject01TitleRoutes::TitleLevel, true);
}

void UProjectProject01LoginWidget::HandleLoginClicked()
{
	if (bAuthenticationRequestPending || (AuthenticationPages && AuthenticationPages->GetActiveWidgetIndex()!=0)) return;
	if (!bMultiplayerServiceAvailable)
	{
		SetStatus(TEXT("서버가 꺼져 있습니다."), true);
		return;
	}
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

	bAuthenticationRequestPending = true;
	SetRequestControlsEnabled(false);
	SetStatus(TEXT("로그인 중..."), false);
	Auth->Login(AccountIdInput->GetText().ToString(), PasswordInput->GetText().ToString());
}

void UProjectProject01LoginWidget::HandleRegisterClicked()
{
	if (bAuthenticationRequestPending || !AuthenticationPages || AuthenticationPages->GetActiveWidgetIndex()!=1) return;
	if (!bMultiplayerServiceAvailable)
	{
		SetStatus(TEXT("서버가 꺼져 있습니다."), true);
		return;
	}
	if (!IsValid(RegistrationAccountIdInput) || !IsValid(RegistrationPasswordInput) || !IsValid(DisplayNameInput))
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

	bAuthenticationRequestPending = true;
	SetRequestControlsEnabled(false);
	SetStatus(TEXT("계정 생성 중..."), false);
	Auth->RegisterAccount(
		RegistrationAccountIdInput->GetText().ToString(),
		RegistrationPasswordInput->GetText().ToString(),
		DisplayNameInput->GetText().ToString());
}

void UProjectProject01LoginWidget::HandleOpenRegistrationClicked()
{
	ShowRegistrationPage(true);
}

void UProjectProject01LoginWidget::HandleReturnToLoginClicked()
{
	ShowRegistrationPage(false);
}

void UProjectProject01LoginWidget::ShowRegistrationPage(bool bShow)
{
	if (bAuthenticationRequestPending || !AuthenticationPages) return;
	if (PasswordInput) PasswordInput->SetText(FText::GetEmpty());
	if (RegistrationPasswordInput) RegistrationPasswordInput->SetText(FText::GetEmpty());
	if (DisplayNameInput) DisplayNameInput->SetText(FText::GetEmpty());
	if (RegistrationAccountIdInput) RegistrationAccountIdInput->SetText(bShow && AccountIdInput ? AccountIdInput->GetText() : FText::GetEmpty());
	AuthenticationPages->SetActiveWidgetIndex(bShow ? 1 : 0);
	SetStatus(bShow ? TEXT("아이디, 비밀번호, 닉네임을 입력해주세요.") : TEXT("아이디와 비밀번호를 입력해주세요."), false);
	SetRequestControlsEnabled(true);
}

void UProjectProject01LoginWidget::HandleLoginResult(
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	bAuthenticationRequestPending = false;
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
	bAuthenticationRequestPending = false;
	SetRequestControlsEnabled(true);
	SetStatus(Message, !bSuccess);
	if (bSuccess)
	{
		if (AccountIdInput && RegistrationAccountIdInput)
			AccountIdInput->SetText(FText::FromString(RegistrationAccountIdInput->GetText().ToString().TrimStartAndEnd()));
		ShowRegistrationPage(false);
		SetStatus(TEXT("회원가입이 완료되었습니다. 로그인해주세요."), false);
	}
}

void UProjectProject01LoginWidget::HandleServiceStatusChanged(
	const bool bMaintenanceEnabled,
	const FString& Announcement,
	const FString& ShutdownAtUtc,
	const FString& Message)
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	bMultiplayerServiceAvailable = IsValid(Auth) && Auth->IsMultiplayerServiceReachable();
	SetRequestControlsEnabled(bMultiplayerServiceAvailable && !bMaintenanceEnabled);
	if (IsValid(ReturnToTitleButton)) ReturnToTitleButton->SetIsEnabled(true);
	if (!bMultiplayerServiceAvailable)
	{
		SetStatus(TEXT("서버가 꺼져 있습니다."), true);
	}
	else if (bMaintenanceEnabled)
	{
		SetStatus(Message.IsEmpty()
			? TEXT("현재 멀티플레이 서버를 점검 중입니다. 싱글플레이는 타이틀에서 이용할 수 있습니다.")
			: Message, true);
	}
	else if (!Announcement.IsEmpty())
	{
		SetStatus(ShutdownAtUtc.IsEmpty() ? Announcement :
			FString::Printf(TEXT("%s (점검 자동 해제 예정: %s)"), *Announcement, *ShutdownAtUtc), false);
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
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, ProjectDFSDBranding::GameTitle(), 30.0f);
	AuthenticationPages = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	Root->AddChildToVerticalBox(AuthenticationPages);
	UVerticalBox* LoginPage = WidgetTree->ConstructWidget<UVerticalBox>();
	UVerticalBox* RegistrationPage = WidgetTree->ConstructWidget<UVerticalBox>();
	AuthenticationPages->AddChild(LoginPage);
	AuthenticationPages->AddChild(RegistrationPage);
	ProjectProject01LoginUI::AddLabel(WidgetTree, LoginPage, TEXT("로그인"), 20.0f);

	AccountIdInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	AccountIdInput->SetHintText(FProjectProject01Localization::Text(TEXT("계정 ID (영문/숫자/밑줄 3~32자)")));
	ProjectProject01LoginUI::StyleTextBox(AccountIdInput);
	ProjectProject01LoginUI::EnlargeInput(AccountIdInput);
	ProjectProject01LoginUI::AddLargeField(WidgetTree,LoginPage,AccountIdInput,54);

	PasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	PasswordInput->SetHintText(FProjectProject01Localization::Text(TEXT("비밀번호 (10~128자)")));
	ProjectProject01LoginUI::StyleTextBox(PasswordInput);
	PasswordInput->SetIsPassword(true);
	ProjectProject01LoginUI::EnlargeInput(PasswordInput);
	ProjectProject01LoginUI::AddLargeField(WidgetTree,LoginPage,PasswordInput,54);
	LoginButton = ProjectProject01LoginUI::AddButton(WidgetTree, LoginPage, TEXT("로그인"));
	RegisterButton = ProjectProject01LoginUI::AddButton(WidgetTree, LoginPage, TEXT("회원가입"));
	ReturnToTitleButton = ProjectProject01LoginUI::AddButton(WidgetTree, LoginPage, TEXT("타이틀로 돌아가기"));

	ProjectProject01LoginUI::AddLabel(WidgetTree, RegistrationPage, TEXT("회원가입"), 20.0f);
	RegistrationAccountIdInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	RegistrationAccountIdInput->SetHintText(FProjectProject01Localization::Text(TEXT("계정 ID (영문/숫자/밑줄 3~32자)")));
	ProjectProject01LoginUI::StyleTextBox(RegistrationAccountIdInput);
	ProjectProject01LoginUI::EnlargeInput(RegistrationAccountIdInput);
	ProjectProject01LoginUI::AddLargeField(WidgetTree,RegistrationPage,RegistrationAccountIdInput,54);
	RegistrationPasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	RegistrationPasswordInput->SetHintText(FProjectProject01Localization::Text(TEXT("비밀번호 (10~128자)")));
	RegistrationPasswordInput->SetIsPassword(true);
	ProjectProject01LoginUI::StyleTextBox(RegistrationPasswordInput);
	ProjectProject01LoginUI::EnlargeInput(RegistrationPasswordInput);
	ProjectProject01LoginUI::AddLargeField(WidgetTree,RegistrationPage,RegistrationPasswordInput,54);

	DisplayNameInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	DisplayNameInput->SetHintText(FProjectProject01Localization::Text(TEXT("닉네임 (2~32자)")));
	ProjectProject01LoginUI::StyleTextBox(DisplayNameInput);
	ProjectProject01LoginUI::EnlargeInput(DisplayNameInput);
	ProjectProject01LoginUI::AddLargeField(WidgetTree,RegistrationPage,DisplayNameInput,54);
	RegistrationSubmitButton = ProjectProject01LoginUI::AddButton(WidgetTree, RegistrationPage, TEXT("회원가입"));
	ReturnToLoginButton = ProjectProject01LoginUI::AddButton(WidgetTree, RegistrationPage, TEXT("로그인으로 돌아가기"));
	AuthenticationPages->SetActiveWidgetIndex(0);
	StatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("인증 서버에 연결할 준비가 되었습니다."), 14.0f);
}

void UProjectProject01LoginWidget::SetRequestControlsEnabled(const bool bEnabled)
{
	for (UEditableTextBox* Input : {AccountIdInput.Get(),PasswordInput.Get(),RegistrationAccountIdInput.Get(),RegistrationPasswordInput.Get(),DisplayNameInput.Get()})
		if (Input) Input->SetIsEnabled(!bAuthenticationRequestPending);
	bool bAuthenticationEnabled = bEnabled && !bAuthenticationRequestPending;
	if (const UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
	{
		if (const UProjectProject01AuthSubsystem* Auth =
			GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			bAuthenticationEnabled = bAuthenticationEnabled && bMultiplayerServiceAvailable &&
				Auth->IsMultiplayerServiceReachable() && !Auth->IsMultiplayerMaintenanceActive() &&
				Auth->GetAuthState() == EProjectProject01AuthState::SignedOut;
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
	if (RegistrationSubmitButton) RegistrationSubmitButton->SetIsEnabled(bAuthenticationEnabled);
	if (ReturnToLoginButton) ReturnToLoginButton->SetIsEnabled(!bAuthenticationRequestPending);
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
	StatusText->SetColorAndOpacity(bIsError ? FSlateColor(ProjectDFSDUI::Danger()) : FSlateColor(ProjectDFSDUI::Text()));
}

void UProjectProject01LoginWidget::EnterLobby()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LobbyLevel")));
}

bool UProjectProject01GameGuideWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	SetIsFocusable(true);
	BuildWidgetTree();
	ProjectDFSDUI::ApplyTree(WidgetTree);
	return true;
}

void UProjectProject01GameGuideWidget::Configure(
	const bool bInMannequinRole,
	const bool bInMultiplayer,
	const bool bInReturnToSessionMenu,
	UProjectProject01SessionMenuWidget* InReturnMenu)
{
	bMannequinRole = bInMannequinRole;
	bMultiplayer = bInMultiplayer;
	bReturnToSessionMenu = bInReturnToSessionMenu;
	ReturnMenu = InReturnMenu;
	RefreshGuideText();
}

void UProjectProject01GameGuideWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(ScreenButton))
	{
		ScreenButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01GameGuideWidget::HandleScreenClicked);
	}
	RefreshGuideText();
	SetKeyboardFocus();
}

void UProjectProject01GameGuideWidget::NativeDestruct()
{
	if (IsValid(ScreenButton))
	{
		ScreenButton->OnClicked.RemoveDynamic(this, &UProjectProject01GameGuideWidget::HandleScreenClicked);
	}
	Super::NativeDestruct();
}

FReply UProjectProject01GameGuideWidget::NativeOnKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		CloseGuide();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UProjectProject01GameGuideWidget::HandleScreenClicked()
{
	CloseGuide();
}

void UProjectProject01GameGuideWidget::CloseGuide()
{
	APlayerController* PlayerController = GetOwningPlayer();
	if (bReturnToSessionMenu && IsValid(ReturnMenu))
	{
		ReturnMenu->SetVisibility(ESlateVisibility::Visible);
		if (IsValid(PlayerController))
		{
			PlayerController->bShowMouseCursor = true;
			FInputModeGameAndUI InputMode;
			InputMode.SetWidgetToFocus(ReturnMenu->TakeWidget());
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			InputMode.SetHideCursorDuringCapture(false);
			PlayerController->SetInputMode(InputMode);
			ReturnMenu->SetKeyboardFocus();
		}
	}
	else
	{
		if (!bMultiplayer)
		{
			UGameplayStatics::SetGamePaused(this, false);
		}
		if (IsValid(PlayerController))
		{
			PlayerController->bShowMouseCursor = false;
			PlayerController->SetInputMode(FInputModeGameOnly());
		}
	}
	RemoveFromParent();
}

void UProjectProject01GameGuideWidget::RefreshGuideText()
{
	if (!IsValid(TitleText) || !IsValid(GoalText) || !IsValid(ControlsText))
	{
		return;
	}

	const UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get();
	const auto KeyName = [](const FKey& Key)
	{
		return Key.IsValid() ? Key.GetDisplayName().ToString() : FString(TEXT("미지정"));
	};

	if (bMannequinRole)
	{
		TitleText->SetText(FProjectProject01Localization::Text(TEXT("마네킹 플레이어 게임 규칙")));
		GoalText->SetText(FProjectProject01Localization::Text(
			TEXT("목표: 생존자 전원을 붙잡으세요. 생존자가 보고 있는 마네킹은 움직이지 못합니다. 각 마네킹은 자기 감지 범위에서 생존자를 발견하면 저장된 명령보다 추적을 우선하며, 놓치면 기존 명령으로 복귀합니다.")));
		ControlsText->SetText(FProjectProject01Localization::Text(
			TEXT("조작키\n"
				"0~9 : 머리 위 번호와 같은 마네킹 시점 선택\n"
				"V : 선택한 마네킹 직접 조작\n"
				"Z : 선택한 마네킹 정찰\n"
				"X : 선택한 마네킹 감지 범위 추격\n"
				"C : 선택한 마네킹 플레이어 추종\n"
				"Q : 추종 중인 마네킹을 정찰 상태로 초기화\n"
				"B : 전체 AI 마네킹 접촉 데스카운트 소모 켜기/끄기\n"
				"마우스 좌클릭 : 직접 조작 중 공격 (직접 조작 공격·접촉은 항상 피해 적용)\n"
				"W/A/S/D : 직접 조작 이동 | 마우스 : 시점\n"
				"ESC : 게임 메뉴")));
	}
	else
	{
		const FKey Sprint = IsValid(Settings) ? Settings->GetSprintKey() : EKeys::LeftShift;
		const FKey Help = IsValid(Settings) ? Settings->GetHelpPingKey() : EKeys::E;
		const FKey Danger = IsValid(Settings) ? Settings->GetDangerPingKey() : EKeys::R;
		const FKey Location = IsValid(Settings) ? Settings->GetLocationPingKey() : EKeys::T;
		const FKey PushToTalk = IsValid(Settings) ? Settings->GetVoicePushToTalkKey() : EKeys::F2;
		const FKey VoiceToggle = IsValid(Settings) ? Settings->GetVoiceToggleKey() : EKeys::U;
		TitleText->SetText(FProjectProject01Localization::Text(TEXT("생존자 플레이어 게임 규칙")));
		GoalText->SetText(FProjectProject01Localization::Text(
			TEXT("목표: 살아 있는 생존자들과 협력해 탈출하세요. 마네킹을 시야에 두면 움직임을 멈출 수 있습니다. 먼저 탈출하거나 탈락하면 살아 있는 생존자를 관전합니다.")));
		ControlsText->SetText(FProjectProject01Localization::Text(FString::Printf(
			TEXT("조작키\n"
				"W/A/S/D : 이동 | 마우스 : 시점\n"
				"%s : 달리기\n"
				"%s : 도움 핑 | %s : 위험 핑 | %s : 위치 핑\n"
				"%s : 누르는 동안 음성 송신 | %s : 음성 송신 고정 전환\n"
				"좌/우 방향키 : 관전 대상 전환\n"
				"ESC : 게임 메뉴"),
			*KeyName(Sprint), *KeyName(Help), *KeyName(Danger), *KeyName(Location),
			*KeyName(PushToTalk), *KeyName(VoiceToggle))));
	}
    if(IsValid(Settings))
    {
        const FString Movement=KeyName(Settings->GetMoveForwardKey())+TEXT("/")+KeyName(Settings->GetMoveLeftKey())+TEXT("/")+KeyName(Settings->GetMoveBackwardKey())+TEXT("/")+KeyName(Settings->GetMoveRightKey());
        ControlsText->SetText(FText::FromString(ControlsText->GetText().ToString().Replace(TEXT("W/A/S/D"),*Movement)));
    }
}

void UProjectProject01GameGuideWidget::BuildWidgetTree()
{
	if (!ensureMsgf(IsValid(WidgetTree), TEXT("Game guide widget has no WidgetTree.")))
	{
		return;
	}

	UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Overlay;
	ScreenButton = WidgetTree->ConstructWidget<UButton>();
	ScreenButton->SetBackgroundColor(FLinearColor(0.01f, 0.01f, 0.015f, 0.94f));
	if (UOverlaySlot* ScreenSlot = Overlay->AddChildToOverlay(ScreenButton))
	{
		ScreenSlot->SetHorizontalAlignment(HAlign_Fill);
		ScreenSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UBorder* GuidePanel = WidgetTree->ConstructWidget<UBorder>();
	GuidePanel->SetBrushColor(FLinearColor(0.88f, 0.88f, 0.88f, 0.98f));
	GuidePanel->SetPadding(FMargin(24.0f));
	GuidePanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* PanelSlot = Overlay->AddChildToOverlay(GuidePanel))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
		PanelSlot->SetPadding(FMargin(60.0f));
	}

	UVerticalBox* Guide = WidgetTree->ConstructWidget<UVerticalBox>();
	GuidePanel->AddChild(Guide);
	TitleText = ProjectProject01LoginUI::AddLabel(WidgetTree, Guide, TEXT("게임 규칙"), 30.0f);
	GoalText = ProjectProject01LoginUI::AddLabel(WidgetTree, Guide, TEXT("목표"), 18.0f);
	GoalText->SetAutoWrapText(true);
	ControlsText = ProjectProject01LoginUI::AddLabel(WidgetTree, Guide, TEXT("조작키"), 17.0f);
	ControlsText->SetAutoWrapText(true);
	UTextBlock* CloseHint = ProjectProject01LoginUI::AddLabel(
		WidgetTree, Guide, TEXT("화면 아무 곳이나 클릭하면 닫힙니다."), 15.0f);
	CloseHint->SetJustification(ETextJustify::Center);
}

bool UProjectProject01SessionMenuWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	SetIsFocusable(true);
	BuildWidgetTree();
	ProjectDFSDUI::ApplyTree(WidgetTree);
	return true;
}

void UProjectProject01SessionMenuWidget::ConfigureForSession(const bool bInMultiplayer)
{
	bMultiplayer = bInMultiplayer;
	if (IsValid(ReturnToRoomButton))
	{
		ReturnToRoomButton->SetVisibility(bMultiplayer ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(GameRulesButton))
	{
		GameRulesButton->SetVisibility(bMultiplayer ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(SaveCheckpointButton))
	{
		SaveCheckpointButton->SetVisibility(bMultiplayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	ShowManualSaveSlots(false);
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
	if (IsValid(LoadOtherCheckpointButton))
	{
		LoadOtherCheckpointButton->SetVisibility(bMultiplayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		if (!bMultiplayer)
		{
			if (UGameInstance* GameInstance = GetGameInstance())
			{
				if (const UProjectProject01SaveSubsystem* Saves = GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>())
				{
					LoadOtherCheckpointButton->SetIsEnabled(Saves->HasAnyRecoverableSave());
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
	bActiveSurveyAvailable = false;
	ActiveSurvey = FProjectProject01SurveyDefinition();
	if (IsValid(SurveyButton)) SurveyButton->SetIsEnabled(false);
	if (IsValid(ContinueButton)) ContinueButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleContinueClicked);
	if (IsValid(SaveCheckpointButton)) SaveCheckpointButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleSaveCheckpointClicked);
	if (ManualSaveSlotButtons.Num() == UProjectProject01SaveSubsystem::SaveSlotCount)
	{
		ManualSaveSlotButtons[0]->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleManualSaveSlot1Clicked);
		ManualSaveSlotButtons[1]->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleManualSaveSlot2Clicked);
		ManualSaveSlotButtons[2]->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleManualSaveSlot3Clicked);
	}
	if (IsValid(ManualSaveCancelButton))
	{
		ManualSaveCancelButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleManualSaveCancelClicked);
	}
	if (IsValid(LoadCheckpointButton)) LoadCheckpointButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadCheckpointClicked);
	if (IsValid(LoadOtherCheckpointButton)) LoadOtherCheckpointButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadOtherCheckpointClicked);
	if (LoadSaveSlotButtons.Num() == UProjectProject01SaveSubsystem::SaveSlotCount)
	{
		LoadSaveSlotButtons[0]->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadSaveSlot1Clicked);
		LoadSaveSlotButtons[1]->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadSaveSlot2Clicked);
		LoadSaveSlotButtons[2]->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadSaveSlot3Clicked);
	}
	if (IsValid(LoadSaveCancelButton))
	{
		LoadSaveCancelButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadSaveCancelClicked);
	}
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleReturnToRoomClicked);
	if (IsValid(SettingsButton)) SettingsButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleSettingsClicked);
	if (IsValid(GameRulesButton)) GameRulesButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleGameRulesClicked);
	if (IsValid(SurveyButton)) SurveyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SessionMenuWidget::HandleSurveyClicked);
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
	RefreshActiveSurvey();
	SetKeyboardFocus();
}

void UProjectProject01SessionMenuWidget::NativeDestruct()
{
	if (IsValid(ContinueButton)) ContinueButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleContinueClicked);
	if (IsValid(SaveCheckpointButton)) SaveCheckpointButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleSaveCheckpointClicked);
	if (ManualSaveSlotButtons.Num() == UProjectProject01SaveSubsystem::SaveSlotCount)
	{
		ManualSaveSlotButtons[0]->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleManualSaveSlot1Clicked);
		ManualSaveSlotButtons[1]->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleManualSaveSlot2Clicked);
		ManualSaveSlotButtons[2]->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleManualSaveSlot3Clicked);
	}
	if (IsValid(ManualSaveCancelButton))
	{
		ManualSaveCancelButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleManualSaveCancelClicked);
	}
	if (IsValid(LoadCheckpointButton)) LoadCheckpointButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadCheckpointClicked);
	if (IsValid(LoadOtherCheckpointButton)) LoadOtherCheckpointButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadOtherCheckpointClicked);
	if (LoadSaveSlotButtons.Num() == UProjectProject01SaveSubsystem::SaveSlotCount)
	{
		LoadSaveSlotButtons[0]->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadSaveSlot1Clicked);
		LoadSaveSlotButtons[1]->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadSaveSlot2Clicked);
		LoadSaveSlotButtons[2]->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadSaveSlot3Clicked);
	}
	if (IsValid(LoadSaveCancelButton))
	{
		LoadSaveCancelButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleLoadSaveCancelClicked);
	}
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleReturnToRoomClicked);
	if (IsValid(SettingsButton)) SettingsButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleSettingsClicked);
	if (IsValid(GameRulesButton)) GameRulesButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleGameRulesClicked);
	if (IsValid(SurveyButton)) SurveyButton->OnClicked.RemoveDynamic(this, &UProjectProject01SessionMenuWidget::HandleSurveyClicked);
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
		if (IsValid(SessionPageSwitcher) && SessionPageSwitcher->GetActiveWidgetIndex() != 0)
		{
			ShowManualSaveSlots(false);
			return FReply::Handled();
		}
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
	RefreshManualSaveSlots();
	ShowManualSaveSlots(true);
	if (IsValid(StatusText))
	{
		StatusText->SetText(FProjectProject01Localization::Text(TEXT("저장할 저장소를 선택하세요.")));
		StatusText->SetColorAndOpacity(FSlateColor(ProjectDFSDUI::Text()));
	}
}

void UProjectProject01SessionMenuWidget::HandleManualSaveSlot1Clicked() { SaveCheckpointToSlot(1); }
void UProjectProject01SessionMenuWidget::HandleManualSaveSlot2Clicked() { SaveCheckpointToSlot(2); }
void UProjectProject01SessionMenuWidget::HandleManualSaveSlot3Clicked() { SaveCheckpointToSlot(3); }

void UProjectProject01SessionMenuWidget::HandleManualSaveCancelClicked()
{
	if (!bBusy) ShowManualSaveSlots(false);
}

void UProjectProject01SessionMenuWidget::SaveCheckpointToSlot(const int32 SlotIndex)
{
	if (bMultiplayer || bBusy) return;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	APlayerCharacter* Player = GetOwningPlayerPawn<APlayerCharacter>();
	if (!IsValid(Saves) || !IsValid(Player))
	{
		SetBusy(false, TEXT("저장할 싱글플레이 상태를 찾지 못했습니다."), true);
		return;
	}
	ShowManualSaveSlots(false);
	SetBusy(true, TEXT("체크포인트를 저장하는 중..."));
	Saves->SaveCheckpointToSlot(Player, SlotIndex, TEXT("ManualPauseMenu"));
}

void UProjectProject01SessionMenuWidget::ShowManualSaveSlots(const bool bShow)
{
	if (IsValid(SessionPageSwitcher))
	{
		SessionPageSwitcher->SetActiveWidgetIndex(bShow && !bMultiplayer ? 1 : 0);
	}
}

void UProjectProject01SessionMenuWidget::RefreshManualSaveSlots()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (!IsValid(Saves)) return;
	for (int32 Index = 0; Index < UProjectProject01SaveSubsystem::SaveSlotCount; ++Index)
	{
		const int32 SlotIndex = Index + 1;
		const FProjectProject01SaveSlotInfo Info = Saves->GetSaveSlotInfo(SlotIndex, true);
		ProjectProject01LoginUI::RefreshSavePreview(ManualSaveSlotPreviewImages, Index, Info.PreviewTexture);
		const FString CurrentMark = Saves->GetCurrentSlotIndex() == SlotIndex ? TEXT(" (현재)") : TEXT("");
		if (ManualSaveSlotStatusTexts.IsValidIndex(Index) && IsValid(ManualSaveSlotStatusTexts[Index]))
		{
			const FString Description = ProjectProject01LoginUI::SaveSlotDescription(Info, CurrentMark);
			ManualSaveSlotStatusTexts[Index]->SetText(FProjectProject01Localization::Text(Description));
		}
	}
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
		SetBusy(false);
		CloseMenu();
	}
}

void UProjectProject01SessionMenuWidget::HandleLoadOtherCheckpointClicked()
{
	if (bMultiplayer || bBusy) return;
	RefreshLoadSaveSlots();
	ShowLoadSaveSlots(true);
	if (IsValid(StatusText))
	{
		StatusText->SetText(FProjectProject01Localization::Text(TEXT("불러올 저장소를 선택하세요.")));
		StatusText->SetColorAndOpacity(FSlateColor(ProjectDFSDUI::Text()));
	}
}

void UProjectProject01SessionMenuWidget::HandleLoadSaveSlot1Clicked() { LoadCheckpointFromSlot(1); }
void UProjectProject01SessionMenuWidget::HandleLoadSaveSlot2Clicked() { LoadCheckpointFromSlot(2); }
void UProjectProject01SessionMenuWidget::HandleLoadSaveSlot3Clicked() { LoadCheckpointFromSlot(3); }

void UProjectProject01SessionMenuWidget::HandleLoadSaveCancelClicked()
{
	if (!bBusy) ShowLoadSaveSlots(false);
}

void UProjectProject01SessionMenuWidget::LoadCheckpointFromSlot(const int32 SlotIndex)
{
	if (bMultiplayer || bBusy) return;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (!IsValid(Saves) || !Saves->HasRecoverableSaveInSlot(SlotIndex))
	{
		SetBusy(false, TEXT("불러올 저장 지점을 찾지 못했습니다."), true);
		return;
	}
	SetBusy(true, FString::Printf(TEXT("저장소 %d을(를) 불러오는 중..."), SlotIndex));
	if (Saves->LoadCheckpointFromSlot(SlotIndex))
	{
		SetBusy(false);
		CloseMenu();
	}
}

void UProjectProject01SessionMenuWidget::ShowLoadSaveSlots(const bool bShow)
{
	if (IsValid(SessionPageSwitcher))
	{
		SessionPageSwitcher->SetActiveWidgetIndex(bShow && !bMultiplayer ? 2 : 0);
	}
}

void UProjectProject01SessionMenuWidget::RefreshLoadSaveSlots()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (!IsValid(Saves)) return;
	for (int32 Index = 0; Index < UProjectProject01SaveSubsystem::SaveSlotCount; ++Index)
	{
		const int32 SlotIndex = Index + 1;
		const FProjectProject01SaveSlotInfo Info = Saves->GetSaveSlotInfo(SlotIndex, true);
		ProjectProject01LoginUI::RefreshSavePreview(LoadSaveSlotPreviewImages, Index, Info.PreviewTexture);
		const FString CurrentMark = Saves->GetCurrentSlotIndex() == SlotIndex ? TEXT(" (현재 플레이 중)") : TEXT("");
		if (LoadSaveSlotStatusTexts.IsValidIndex(Index) && IsValid(LoadSaveSlotStatusTexts[Index]))
		{
			const FString Description = ProjectProject01LoginUI::SaveSlotDescription(Info, CurrentMark);
			LoadSaveSlotStatusTexts[Index]->SetText(FProjectProject01Localization::Text(Description));
		}
		if (LoadSaveSlotButtons.IsValidIndex(Index) && IsValid(LoadSaveSlotButtons[Index]))
		{
			LoadSaveSlotButtons[Index]->SetIsEnabled(Info.bHasSave && !bBusy);
		}
	}
}

void UProjectProject01SessionMenuWidget::HandleSaveOperationCompleted(
	const bool bSucceeded,
	const FString& Message)
{
	SetBusy(false, Message, !bSucceeded);
	RefreshManualSaveSlots();
	RefreshLoadSaveSlots();
	if (bSucceeded && IsValid(LoadCheckpointButton))
	{
		LoadCheckpointButton->SetIsEnabled(true);
	}
	if (IsValid(LoadOtherCheckpointButton))
	{
		UGameInstance* GameInstance = GetGameInstance();
		const UProjectProject01SaveSubsystem* Saves = IsValid(GameInstance)
			? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
		LoadOtherCheckpointButton->SetIsEnabled(IsValid(Saves) && Saves->HasAnyRecoverableSave());
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

void UProjectProject01SessionMenuWidget::HandleGameRulesClicked()
{
	if (!bMultiplayer || bBusy || !IsValid(GetOwningPlayer()))
	{
		return;
	}

	const AMultiplayTestPlayerController* MultiplayerController =
		Cast<AMultiplayTestPlayerController>(GetOwningPlayer());
	const bool bShowMannequinGuide = IsValid(MultiplayerController) &&
		IsValid(MultiplayerController->GetViewedMannequin());
	GameGuideWidget = CreateWidget<UProjectProject01GameGuideWidget>(
		GetOwningPlayer(), UProjectProject01GameGuideWidget::StaticClass());
	if (!ensureMsgf(IsValid(GameGuideWidget), TEXT("Session menu could not create game guide widget.")))
	{
		return;
	}

	GameGuideWidget->Configure(bShowMannequinGuide, true, true, this);
	SetVisibility(ESlateVisibility::Collapsed);
	GameGuideWidget->AddToViewport(600);
	GetOwningPlayer()->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(GameGuideWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	GetOwningPlayer()->SetInputMode(InputMode);
}

void UProjectProject01SessionMenuWidget::HandleSurveyClicked()
{
	if (bBusy || !bActiveSurveyAvailable || !ActiveSurvey.IsValid() || !IsValid(GetOwningPlayer())) return;
	SurveyWidget = CreateWidget<UProjectProject01SurveyWidget>(
		GetOwningPlayer(), UProjectProject01SurveyWidget::StaticClass());
	if (!ensureMsgf(IsValid(SurveyWidget), TEXT("Session menu could not create survey widget."))) return;
	SurveyWidget->ConfigureSurvey(ActiveSurvey, this);
	SetVisibility(ESlateVisibility::Collapsed);
	SurveyWidget->AddToViewport(600);
}

void UProjectProject01SessionMenuWidget::RefreshActiveSurvey()
{
	UProjectProject01GameInstance* GameInstance = Cast<UProjectProject01GameInstance>(GetGameInstance());
	if (!IsValid(GameInstance)) return;
	const TWeakObjectPtr<UProjectProject01SessionMenuWidget> WeakThis(this);
	GameInstance->FetchActivePlayerSurvey(
		[WeakThis](const bool bRequestSucceeded, const bool bHasSurvey,
			const FProjectProject01SurveyDefinition& Survey, const FString&)
		{
			if (UProjectProject01SessionMenuWidget* Widget = WeakThis.Get(); IsValid(Widget))
			{
				Widget->bActiveSurveyAvailable = bRequestSucceeded && bHasSurvey;
				Widget->ActiveSurvey = Widget->bActiveSurveyAvailable
					? Survey : FProjectProject01SurveyDefinition();
				if (IsValid(Widget->SurveyButton))
				{
					Widget->SurveyButton->SetIsEnabled(!Widget->bBusy && Widget->bActiveSurveyAvailable);
				}
			}
		});
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
	for (UButton* Button : ManualSaveSlotButtons)
	{
		if (IsValid(Button)) Button->SetIsEnabled(!bBusy);
	}
	if (IsValid(ManualSaveCancelButton)) ManualSaveCancelButton->SetIsEnabled(!bBusy);
	const UProjectProject01SaveSubsystem* Saves = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		Saves = GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>();
	}
	if (IsValid(LoadCheckpointButton))
	{
		LoadCheckpointButton->SetIsEnabled(!bBusy && !bMultiplayer && IsValid(Saves) && Saves->HasRecoverableSave());
	}
	if (IsValid(LoadOtherCheckpointButton))
	{
		LoadOtherCheckpointButton->SetIsEnabled(!bBusy && !bMultiplayer && IsValid(Saves) && Saves->HasAnyRecoverableSave());
	}
	for (int32 Index = 0; Index < LoadSaveSlotButtons.Num(); ++Index)
	{
		if (UButton* Button = LoadSaveSlotButtons[Index]; IsValid(Button))
		{
			Button->SetIsEnabled(!bBusy && IsValid(Saves) && Saves->HasRecoverableSaveInSlot(Index + 1));
		}
	}
	if (IsValid(LoadSaveCancelButton)) LoadSaveCancelButton->SetIsEnabled(!bBusy);
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->SetIsEnabled(!bBusy);
	if (IsValid(SettingsButton)) SettingsButton->SetIsEnabled(!bBusy);
	if (IsValid(GameRulesButton)) GameRulesButton->SetIsEnabled(!bBusy);
	if (IsValid(SurveyButton)) SurveyButton->SetIsEnabled(!bBusy && bActiveSurveyAvailable);
	if (IsValid(ReturnToTitleButton)) ReturnToTitleButton->SetIsEnabled(!bBusy);
	if (IsValid(QuitGameButton)) QuitGameButton->SetIsEnabled(!bBusy);
	if (IsValid(StatusText) && !Message.IsEmpty())
	{
		StatusText->SetText(FProjectProject01Localization::Text(Message));
		StatusText->SetColorAndOpacity(bIsError
			? FSlateColor(ProjectDFSDUI::Danger())
			: FSlateColor(ProjectDFSDUI::Text()));
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

	SessionPageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	if (UOverlaySlot* MenuSlot = Overlay->AddChildToOverlay(SessionPageSwitcher))
	{
		MenuSlot->SetHorizontalAlignment(HAlign_Center);
		MenuSlot->SetVerticalAlignment(VAlign_Fill);
		MenuSlot->SetPadding(FMargin(40.0f));
	}
	// Long menus must remain reachable on short windows; keep the full-screen fade outside this scroll area.
	UScrollBox* MenuScroll = WidgetTree->ConstructWidget<UScrollBox>();
	SessionPageSwitcher->AddChild(MenuScroll);
	UVerticalBox* Menu = WidgetTree->ConstructWidget<UVerticalBox>();
	MenuScroll->AddChild(Menu);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Menu, TEXT("게임 메뉴"), 30.0f);
	StatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Menu, TEXT("ESC를 다시 누르면 게임으로 돌아갑니다."), 14.0f);
	ContinueButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("게임 계속"));
	SaveCheckpointButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("현재 체크포인트 저장"));
	LoadCheckpointButton = ProjectProject01LoginUI::AddButton(
		WidgetTree, Menu, TEXT("현재 저장소의 마지막 저장지점 불러오기"));
	LoadOtherCheckpointButton = ProjectProject01LoginUI::AddButton(
		WidgetTree, Menu, TEXT("다른 저장소 불러오기"));
	ReturnToRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("방으로 돌아가기"));
	SettingsButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("환경설정"));
	GameRulesButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("게임 규칙"));
	SurveyButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("피드백/설문조사"));
	ReturnToTitleButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("타이틀로 돌아가기"));
	QuitGameButton = ProjectProject01LoginUI::AddButton(WidgetTree, Menu, TEXT("게임 종료"));

	ManualSaveSlotPanel = WidgetTree->ConstructWidget<UVerticalBox>();
	SessionPageSwitcher->AddChild(ManualSaveSlotPanel);
	ProjectProject01LoginUI::AddLabel(WidgetTree, ManualSaveSlotPanel, TEXT("수동 저장소 선택"), 18.0f);
	ManualSaveSlotButtons.Reset();
	ManualSaveSlotStatusTexts.Reset();
	ManualSaveSlotPreviewImages.Reset();
	for (int32 SlotIndex = 1; SlotIndex <= UProjectProject01SaveSubsystem::SaveSlotCount; ++SlotIndex)
	{
		UImage* Preview = nullptr;
		ManualSaveSlotStatusTexts.Add(ProjectProject01LoginUI::AddSaveSlotSummary(WidgetTree, ManualSaveSlotPanel, Preview));
		ManualSaveSlotPreviewImages.Add(Preview);
		ManualSaveSlotButtons.Add(ProjectProject01LoginUI::AddButton(
			WidgetTree, ManualSaveSlotPanel,
			FString::Printf(TEXT("저장소 %d에 저장"), SlotIndex)));
	}
	ManualSaveCancelButton = ProjectProject01LoginUI::AddButton(WidgetTree, ManualSaveSlotPanel, TEXT("돌아가기"));

	LoadSaveSlotPanel = WidgetTree->ConstructWidget<UVerticalBox>();
	SessionPageSwitcher->AddChild(LoadSaveSlotPanel);
	ProjectProject01LoginUI::AddLabel(WidgetTree, LoadSaveSlotPanel, TEXT("다른 저장소 불러오기"), 18.0f);
	LoadSaveSlotButtons.Reset();
	LoadSaveSlotStatusTexts.Reset();
	LoadSaveSlotPreviewImages.Reset();
	for (int32 SlotIndex = 1; SlotIndex <= UProjectProject01SaveSubsystem::SaveSlotCount; ++SlotIndex)
	{
		UImage* Preview = nullptr;
		LoadSaveSlotStatusTexts.Add(ProjectProject01LoginUI::AddSaveSlotSummary(WidgetTree, LoadSaveSlotPanel, Preview));
		LoadSaveSlotPreviewImages.Add(Preview);
		LoadSaveSlotButtons.Add(ProjectProject01LoginUI::AddButton(
			WidgetTree, LoadSaveSlotPanel,
			FString::Printf(TEXT("저장소 %d 불러오기"), SlotIndex)));
	}
	LoadSaveCancelButton = ProjectProject01LoginUI::AddButton(WidgetTree, LoadSaveSlotPanel, TEXT("돌아가기"));
	SessionPageSwitcher->SetActiveWidgetIndex(0);

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
	ProjectDFSDUI::ApplyTree(WidgetTree);
	return true;
}

void UProjectProject01MatchResultWidget::ConfigureResult(const FMultiplayTestMatchResult& InResult)
{
	Result = InResult;
	bSinglePlayer = false;
	bConfigured = true;
	if (!IsInViewport()) InitializeResultPresentation();
	if (!Result.bLeaderboardVerificationPending &&
		Result.bSuccess && Result.bLeaderboardVerificationReady)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UProjectProject01LobbySubsystem* Lobby =
				GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
			{
				const EProjectProject01LeaderboardRole Role =
					Result.Role.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase)
					? EProjectProject01LeaderboardRole::Mannequin
					: EProjectProject01LeaderboardRole::Survivor;
				Lobby->CachePendingLeaderboardRecord(
					Result.MatchId, Role, Result.CaptureCount, Result.FirstCaptureSeconds,
					Result.AllCapturedSeconds, Result.RescueCount, Result.EscapeSeconds);
			}
		}
	}
	RefreshResultText();
}

void UProjectProject01MatchResultWidget::ConfigureSinglePlayerResult(const bool bEscaped)
{
	bShowingTimeline = false;
	Result = FMultiplayTestMatchResult();
	Result.Role = TEXT("SinglePlayer");
	Result.Outcome = bEscaped ? TEXT("Escaped") : TEXT("Eliminated");
	Result.bSuccess = bEscaped;
	bSinglePlayer = true;
	bConfigured = true;
	InitializeResultPresentation();
	RefreshResultText();
}

void UProjectProject01MatchResultWidget::NativeConstruct()
{
	if (IsValid(TimelineButton)) TimelineButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01MatchResultWidget::HandleTimelineClicked);
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
	if (IsValid(LoadLastCheckpointButton))
	{
		LoadLastCheckpointButton->OnClicked.AddUniqueDynamic(
			this, &UProjectProject01MatchResultWidget::HandleLoadLastCheckpointClicked);
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
	InitializeResultPresentation();
	SetBusy(false);
	RefreshResultText();
	SetKeyboardFocus();
}

void UProjectProject01MatchResultWidget::InitializeResultPresentation()
{
	// Multiplayer death/escape already handed over a black screen. A transparent
	// result background followed by a second fade exposed the world for 1.5 seconds.
	// Establish opaque black BEFORE AddToViewport; only the single escape retains
	// its original fade from gameplay when it has not already blacked out.
	const bool bImmediate = !bSinglePlayer || !Result.bSuccess;
	FadeElapsedSeconds = bImmediate ? FadeDurationSeconds : 0.0f;
	if (IsValid(FadeBackground))
	{
		FadeBackground->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, bImmediate ? 1.0f : 0.0f));
	}
	if (IsValid(ResultMenu))
	{
		ResultMenu->SetRenderOpacity(bImmediate ? 1.0f : 0.0f);
		ResultMenu->SetVisibility(bImmediate
			? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	}
}

void UProjectProject01MatchResultWidget::NativeDestruct()
{
	if (IsValid(TimelineButton)) TimelineButton->OnClicked.RemoveDynamic(this, &UProjectProject01MatchResultWidget::HandleTimelineClicked);
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
	if (IsValid(LoadLastCheckpointButton))
	{
		LoadLastCheckpointButton->OnClicked.RemoveDynamic(
			this, &UProjectProject01MatchResultWidget::HandleLoadLastCheckpointClicked);
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
	if (IsValid(Lobby))
	{
		Lobby->SetLeaderboardOpenedFromMatchResult(true);
	}
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

void UProjectProject01MatchResultWidget::HandleLoadLastCheckpointClicked()
{
	if (!bSinglePlayer || Result.bSuccess || bBusy || FadeElapsedSeconds < FadeDurationSeconds)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01SaveSubsystem* SaveSubsystem = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
	if (!IsValid(SaveSubsystem) || !SaveSubsystem->HasRecoverableSave())
	{
		SetBusy(false, TEXT("현재 저장소에 불러올 체크포인트가 없습니다."), true);
		if (IsValid(StatusText))
		{
			StatusText->SetVisibility(ESlateVisibility::Visible);
		}
		return;
	}

	SetBusy(true, TEXT("현재 저장소의 마지막 체크포인트를 불러오는 중..."));
	if (!SaveSubsystem->LoadLatestCheckpoint())
	{
		SetBusy(false, TEXT("마지막 체크포인트를 불러오지 못했습니다."), true);
		if (IsValid(StatusText))
		{
			StatusText->SetVisibility(ESlateVisibility::Visible);
		}
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
		if (bOpenLeaderboardAfterReturn)
		{
			if (UGameInstance* GameInstance = GetGameInstance())
			{
				if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
				{
					Lobby->SetLeaderboardOpenedFromMatchResult(false);
				}
			}
			bOpenLeaderboardAfterReturn = false;
		}
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
	const bool bHasTimeline = !bSinglePlayer && !Result.Timeline.IsEmpty();
	if (!bHasTimeline) bShowingTimeline = false;
	if (IsValid(TimelineButton))
	{
		TimelineButton->SetVisibility(bHasTimeline ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (UTextBlock* Label = Cast<UTextBlock>(TimelineButton->GetChildAt(0)))
			Label->SetText(bShowingTimeline ? NSLOCTEXT("MatchTimeline","Summary","결과 요약") : NSLOCTEXT("MatchTimeline","Title","경기 타임라인"));
	}
	ResultText->SetVisibility(bShowingTimeline ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (IsValid(TimelineFrame)) TimelineFrame->SetVisibility(bShowingTimeline ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(TimelineRows))
	{
		TimelineRows->ClearChildren();
		if (Result.OmittedTimelineEvents > 0)
		{
			UTextBlock* Note = WidgetTree->ConstructWidget<UTextBlock>();
			Note->SetText(FText::Format(NSLOCTEXT("MatchTimeline","Omitted","사건이 많아 중간 기록 {0}개를 생략했습니다."), FText::AsNumber(Result.OmittedTimelineEvents)));
			Note->SetColorAndOpacity(FSlateColor(FLinearColor::Yellow));
			Note->SetAutoWrapText(true); TimelineRows->AddChildToVerticalBox(Note);
		}
		for (const FProjectMatchTimelineEvent& Event : Result.Timeline)
		{
			UTextBlock* Row = WidgetTree->ConstructWidget<UTextBlock>();
			Row->SetText(ProjectProject01Timeline::FormatEvent(Event));
			Row->SetColorAndOpacity(FSlateColor(FLinearColor::White)); Row->SetAutoWrapText(true);
			FSlateFontInfo Font = Row->GetFont(); Font.Size = 18; Row->SetFont(Font);
			TimelineRows->AddChildToVerticalBox(Row)->SetPadding(FMargin(6,4));
		}
	}

	const bool bCanRegister = !bSinglePlayer && !Result.bLeaderboardVerificationPending &&
		Result.bSuccess && Result.bLeaderboardVerificationReady;
	if (IsValid(RegisterButton))
	{
		RegisterButton->SetVisibility(!bSinglePlayer && Result.bSuccess
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		RegisterButton->SetIsEnabled(bCanRegister && !bBusy);
	}
	if (IsValid(LeaderboardButton)) LeaderboardButton->SetVisibility(bSinglePlayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->SetVisibility(bSinglePlayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (IsValid(LogoutButton)) LogoutButton->SetVisibility(bSinglePlayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (IsValid(LoadLastCheckpointButton))
	{
		const bool bShowCheckpointButton = bSinglePlayer && !Result.bSuccess;
		UProjectProject01SaveSubsystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
		LoadLastCheckpointButton->SetVisibility(
			bShowCheckpointButton ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		LoadLastCheckpointButton->SetIsEnabled(
			bShowCheckpointButton && !bBusy && IsValid(SaveSubsystem) && SaveSubsystem->HasRecoverableSave());
	}
	if (IsValid(StatusText))
	{
		const FString Status = bSinglePlayer
			? FString()
			: Result.bLeaderboardVerificationPending
			? TEXT("경기 결과를 표시했습니다. 서버 기록을 확인하는 중입니다.")
			: !Result.bSuccess
			? TEXT("실패 기록은 리더보드에 등록되지 않습니다.")
			: Result.bLeaderboardVerificationReady
				? TEXT("리더보드를 열어 이번 경기 기록을 확인하고 등록할 수 있습니다.")
				: TEXT("서버의 리더보드 검증 기록에 실패했습니다. 방 복귀만 가능합니다.");
		StatusText->SetText(FProjectProject01Localization::Text(Status));
		StatusText->SetVisibility(bSinglePlayer ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		StatusText->SetColorAndOpacity(!Result.bLeaderboardVerificationPending &&
			Result.bSuccess && !Result.bLeaderboardVerificationReady
			? FSlateColor(ProjectDFSDUI::Danger()) : FSlateColor(FLinearColor::White));
	}
}

void UProjectProject01MatchResultWidget::SetBusy(
	const bool bInBusy,
	const FString& Message,
	const bool bIsError)
{
	bBusy = bInBusy;
	if (IsValid(TimelineButton)) TimelineButton->SetIsEnabled(!bBusy && FadeElapsedSeconds >= FadeDurationSeconds);
	const bool bFadeFinished = FadeElapsedSeconds >= FadeDurationSeconds;
	if (IsValid(RegisterButton))
	{
		RegisterButton->SetIsEnabled(bFadeFinished && !bBusy && !bSinglePlayer &&
			Result.bSuccess && Result.bLeaderboardVerificationReady);
	}
	if (IsValid(LeaderboardButton)) LeaderboardButton->SetIsEnabled(bFadeFinished && !bBusy);
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->SetIsEnabled(bFadeFinished && !bBusy);
	if (IsValid(ReturnToTitleButton)) ReturnToTitleButton->SetIsEnabled(bFadeFinished && !bBusy);
	if (IsValid(LoadLastCheckpointButton))
	{
		UProjectProject01SaveSubsystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UProjectProject01SaveSubsystem>() : nullptr;
		LoadLastCheckpointButton->SetIsEnabled(bFadeFinished && !bBusy && bSinglePlayer &&
			!Result.bSuccess && IsValid(SaveSubsystem) && SaveSubsystem->HasRecoverableSave());
	}
	if (IsValid(LogoutButton)) LogoutButton->SetIsEnabled(bFadeFinished && !bBusy);
	if (IsValid(StatusText) && !Message.IsEmpty())
	{
		StatusText->SetText(FProjectProject01Localization::Text(Message));
		StatusText->SetColorAndOpacity(bIsError
			? FSlateColor(ProjectDFSDUI::Danger()) : FSlateColor(FLinearColor::White));
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
	TimelineButton = ProjectProject01LoginUI::AddButton(WidgetTree, ResultMenu, TEXT("경기 타임라인"));
	TimelineButton->SetVisibility(ESlateVisibility::Collapsed);
	TimelineFrame = WidgetTree->ConstructWidget<USizeBox>();
	TimelineFrame->SetWidthOverride(760); TimelineFrame->SetHeightOverride(240);
	TimelineFrame->SetVisibility(ESlateVisibility::Collapsed);
	UScrollBox* TimelineScroll = WidgetTree->ConstructWidget<UScrollBox>();
	TimelineFrame->AddChild(TimelineScroll);
	TimelineRows = WidgetTree->ConstructWidget<UVerticalBox>(); TimelineScroll->AddChild(TimelineRows);
	ResultMenu->AddChildToVerticalBox(TimelineFrame);

	LeaderboardButton = ProjectProject01LoginUI::AddButton(WidgetTree, ResultMenu, TEXT("리더보드"));
	ReturnToRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, ResultMenu, TEXT("방으로 돌아가기"));
	LogoutButton = ProjectProject01LoginUI::AddButton(WidgetTree, ResultMenu, TEXT("로그아웃"));
	LoadLastCheckpointButton = ProjectProject01LoginUI::AddButton(
		WidgetTree, ResultMenu, TEXT("마지막 체크포인트에서 시작"));
	ReturnToTitleButton = ProjectProject01LoginUI::AddButton(WidgetTree, ResultMenu, TEXT("타이틀로 돌아가기"));
}

void UProjectProject01MatchResultWidget::HandleTimelineClicked()
{
	if (bSinglePlayer || bBusy || FadeElapsedSeconds < FadeDurationSeconds || Result.Timeline.IsEmpty()) return;
	bShowingTimeline = !bShowingTimeline;
	RefreshResultText();
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
	ProjectDFSDUI::ApplyTree(WidgetTree);
	ProjectDFSDUI::FramePage(WidgetTree,1120);
	return true;
}

void UProjectProject01SettingsWidget::NativeConstruct()
{
	if (IsValid(VoiceSettingsButton)) VoiceSettingsButton->OnClicked.AddUniqueDynamic(this,&UProjectProject01SettingsWidget::HandleVoiceSettingsClicked);
	Super::NativeConstruct();
	LoadPendingFromSettings();
	RefreshAllControls();
	SetActiveSettingsPage(0);

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
	CameraShakeCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleCameraShakeChanged);
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
	RevertVideoButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleRevertVideoClicked);
	ServerSettingsTabButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleServerSettingsTabClicked);
	AudioSettingsTabButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleAudioSettingsTabClicked);
	VideoSettingsTabButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleVideoSettingsTabClicked);
	AccessibilitySettingsTabButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleAccessibilitySettingsTabClicked);
	ControlsSettingsTabButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01SettingsWidget::HandleControlsSettingsTabClicked);
	RestoreSettingsInputFocus();
}

void UProjectProject01SettingsWidget::NativeDestruct()
{
	if (IsValid(VoiceSettingsWidget)) VoiceSettingsWidget->RemoveFromParent();
	if (IsValid(VoiceSettingsButton)) VoiceSettingsButton->OnClicked.RemoveDynamic(this,&UProjectProject01SettingsWidget::HandleVoiceSettingsClicked);
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
	if (IsValid(CameraShakeCheckBox)) CameraShakeCheckBox->OnCheckStateChanged.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleCameraShakeChanged);
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
	if (IsValid(RevertVideoButton)) RevertVideoButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleRevertVideoClicked);
	if (IsValid(ServerSettingsTabButton)) ServerSettingsTabButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleServerSettingsTabClicked);
	if (IsValid(AudioSettingsTabButton)) AudioSettingsTabButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleAudioSettingsTabClicked);
	if (IsValid(VideoSettingsTabButton)) VideoSettingsTabButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleVideoSettingsTabClicked);
	if (IsValid(AccessibilitySettingsTabButton)) AccessibilitySettingsTabButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleAccessibilitySettingsTabClicked);
	if (IsValid(ControlsSettingsTabButton)) ControlsSettingsTabButton->OnClicked.RemoveDynamic(this, &UProjectProject01SettingsWidget::HandleControlsSettingsTabClicked);
	Super::NativeDestruct();
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
void UProjectProject01SettingsWidget::HandleCameraShakeChanged(const bool bChecked) { if (!bRefreshingControls) { bPendingCameraShake = bChecked; ScreenShakeSlider->SetIsEnabled(bChecked); } }
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
	if (bAwaitingVideoConfirmation) ConfirmPendingVideoMode();
	CloseToReturnWidget();
}

void UProjectProject01SettingsWidget::HandleDefaultsClicked()
{
	if (GetGameInstance()) if (auto* Voice = GetGameInstance()->GetSubsystem<UProjectProject01VoiceChatSubsystem>())
		Voice->SetAudioPreferences(TEXT(""),TEXT(""),1.0f);
	SetPendingDefaults();
	RefreshAllControls();
	ApplyPendingSettings();
}

void UProjectProject01SettingsWidget::HandleRevertVideoClicked() { RevertPendingVideoMode(); }
void UProjectProject01SettingsWidget::HandleServerSettingsTabClicked() { SetActiveSettingsPage(0); }
void UProjectProject01SettingsWidget::HandleAudioSettingsTabClicked() { SetActiveSettingsPage(1); }

void UProjectProject01SettingsWidget::HandleVoiceSettingsClicked()
{
	if (!IsValid(GetOwningPlayer()) || (IsValid(VoiceSettingsWidget) && VoiceSettingsWidget->IsInViewport())) return;
	VoiceSettingsWidget=CreateWidget<UProjectProject01VoiceSettingsWidget>(GetOwningPlayer());
	if (!IsValid(VoiceSettingsWidget)) return;
	VoiceSettingsWidget->ConfigureReturn(this);SetVisibility(ESlateVisibility::Collapsed);
	VoiceSettingsWidget->AddToViewport(650);
}
void UProjectProject01SettingsWidget::HandleVideoSettingsTabClicked() { SetActiveSettingsPage(2); }
void UProjectProject01SettingsWidget::HandleAccessibilitySettingsTabClicked() { SetActiveSettingsPage(3); }
void UProjectProject01SettingsWidget::HandleControlsSettingsTabClicked() { SetActiveSettingsPage(4); }

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
	PendingScreenShakeScale = Settings->GetConfiguredScreenShakeScale();
	bPendingCameraShake = Settings->IsCameraShakeEnabled();
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
	PendingDirectServerAddress = Settings->GetDirectServerAddress();
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
	bPendingCameraShake = true;
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
	PendingHelpPingKey = EKeys::E;
	PendingDangerPingKey = EKeys::R;
	PendingLocationPingKey = EKeys::T;
	PendingDirectServerAddress.Reset();
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
	CameraShakeCheckBox->SetIsChecked(bPendingCameraShake);
	ScreenShakeSlider->SetIsEnabled(bPendingCameraShake);
	UIReadableScaleSlider->SetValue(PendingUIReadableScale);
	MuteAllCheckBox->SetIsChecked(bPendingMuteAll);
	MuteWhenUnfocusedCheckBox->SetIsChecked(bPendingMuteWhenUnfocused);
	VSyncCheckBox->SetIsChecked(bPendingVSync);
	MotionBlurCheckBox->SetIsChecked(bPendingMotionBlur);
	SubtitlesCheckBox->SetIsChecked(bPendingSubtitles);
	EnhancedVisualCuesCheckBox->SetIsChecked(bPendingEnhancedVisualCues);
	DirectServerAddressInput->SetText(FText::FromString(PendingDirectServerAddress));
    const auto* Auth=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>():nullptr;
    DirectServerAddressInput->SetIsEnabled(!Auth||Auth->GetAuthState()==EProjectProject01AuthState::SignedOut);
	DirectServerAddressInput->SetVisibility(ESlateVisibility::Visible);
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
	// 이미 적용한 화면 설정을 다시 편집하는 경우 현재 화면을 다음 되돌리기의 기준으로 확정한다.
	if (bAwaitingVideoConfirmation)
	{
		ConfirmPendingVideoMode();
	}
	PendingDirectServerAddress = IsValid(DirectServerAddressInput)
		? DirectServerAddressInput->GetText().ToString().TrimStartAndEnd()
		: PendingDirectServerAddress;
	if (!PendingDirectServerAddress.IsEmpty() &&
		!ProjectProject01LoginUI::IsSupportedServerIpv4(PendingDirectServerAddress))
	{
		SetStatus(TEXT("IP 직접 입력에는 서버 PC의 올바른 25.x 또는 사설 IPv4 주소를 입력하세요."), true);
		return;
	}
	const bool bVideoModeChanged = Settings->GetScreenResolution() != PendingResolution ||
		Settings->GetFullscreenMode() != PendingWindowMode;
    if(PendingDirectServerAddress!=Settings->GetDirectServerAddress())
    {
        FString ConnectionError;
        auto* Project=Cast<UProjectProject01GameInstance>(GetGameInstance());
        if(PendingDirectServerAddress.IsEmpty()||!Project||!Project->ConfigureMultiplayerServerAddress(PendingDirectServerAddress,ConnectionError))
        {SetStatus(ConnectionError.IsEmpty()?TEXT("서버 IP를 입력하고 로그인 전에 적용하세요."):ConnectionError,true);return;}
    }
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
	Settings->SetCameraShakeEnabled(bPendingCameraShake);
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
	Settings->SetDirectServerAddress(PendingDirectServerAddress);
	Settings->SetScreenResolution(PendingResolution);
	Settings->SetFullscreenMode(PendingWindowMode);
	Settings->ApplyNonResolutionSettings();
	RefreshLocalPlayerInput();
	if (bVideoModeChanged)
	{
		Settings->ApplyResolutionSettings(false);
		Settings->SaveSettings();
		bAwaitingVideoConfirmation = true;
		VideoConfirmPanel->SetVisibility(ESlateVisibility::Visible);
		if (IsValid(VideoConfirmText))
		{
			VideoConfirmText->SetText(FProjectProject01Localization::Text(
				TEXT("화면 설정을 적용했습니다. 이전 설정으로 돌아가려면 되돌리기를 누르세요.")));
		}
		SetStatus(TEXT("환경설정을 적용하고 저장했습니다. 화면 설정은 되돌리기를 누르기 전까지 유지됩니다."));
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
		Settings->ConfirmVideoMode();
		Settings->SaveSettings();
		PendingResolution = Settings->GetScreenResolution();
		PendingWindowMode = Settings->GetFullscreenMode();
	}
	bAwaitingVideoConfirmation = false;
	VideoConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
	RefreshAllControls();
	SetStatus(TEXT("적용 전 화면 설정으로 되돌렸습니다."));
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
		StatusText->SetColorAndOpacity(bIsError ? FSlateColor(ProjectDFSDUI::Danger()) : FSlateColor(ProjectDFSDUI::Text()));
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

void UProjectProject01SettingsWidget::SetActiveSettingsPage(const int32 PageIndex)
{
	if (!IsValid(SettingsPageSwitcher))
	{
		return;
	}
	const int32 SafePageIndex = FMath::Clamp(PageIndex, 0, SettingsPageSwitcher->GetNumWidgets() - 1);
	SettingsPageSwitcher->SetActiveWidgetIndex(SafePageIndex);

	const TArray<TPair<UButton*, FString>> Tabs = {
		{ ServerSettingsTabButton, TEXT("서버 연결") },
		{ AudioSettingsTabButton, TEXT("사운드") },
		{ VideoSettingsTabButton, TEXT("화면·그래픽") },
		{ AccessibilitySettingsTabButton, TEXT("접근성") },
		{ ControlsSettingsTabButton, TEXT("조작") }
	};
	for (int32 Index = 0; Index < Tabs.Num(); ++Index)
	{
		UButton* Button = Tabs[Index].Key;
		if (!IsValid(Button))
		{
			continue;
		}
		ProjectProject01LoginUI::SetButtonText(
			Button,
			(Index == SafePageIndex ? TEXT("● ") : TEXT("")) + Tabs[Index].Value);
		ProjectDFSDUI::Apply(Button,Index == SafePageIndex);
	}
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
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	Background->AddChild(Root);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("환경설정"), 30.0f);
	StatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("변경 후 적용을 눌러 저장하세요."), 14.0f);

	UHorizontalBox* TabBar = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* TabBarSlot = Root->AddChildToVerticalBox(TabBar))
	{
		TabBarSlot->SetPadding(FMargin(4.0f, 2.0f));
		TabBarSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	ServerSettingsTabButton = ProjectProject01LoginUI::AddTabButton(WidgetTree, TabBar, TEXT("서버 연결"));
	AudioSettingsTabButton = ProjectProject01LoginUI::AddTabButton(WidgetTree, TabBar, TEXT("사운드"));
	VideoSettingsTabButton = ProjectProject01LoginUI::AddTabButton(WidgetTree, TabBar, TEXT("화면·그래픽"));
	AccessibilitySettingsTabButton = ProjectProject01LoginUI::AddTabButton(WidgetTree, TabBar, TEXT("접근성"));
	ControlsSettingsTabButton = ProjectProject01LoginUI::AddTabButton(WidgetTree, TabBar, TEXT("조작"));

	SettingsPageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	if (UVerticalBoxSlot* SwitcherSlot = Root->AddChildToVerticalBox(SettingsPageSwitcher))
	{
		SwitcherSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SwitcherSlot->SetPadding(FMargin(4.0f));
		SwitcherSlot->SetHorizontalAlignment(HAlign_Fill);
		SwitcherSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* ServerPage = ProjectProject01LoginUI::AddSettingsPage(WidgetTree, SettingsPageSwitcher);
	ProjectProject01LoginUI::AddLabel(WidgetTree, ServerPage, TEXT("멀티플레이 서버 연결"), 20.0f);
    if (IsValid(GetOwningPlayer())) if (auto* Blocks=CreateWidget<UProjectProject01SocialWidget>(GetOwningPlayer()))
    {
        Blocks->ConfigureBlockList();
        ServerPage->AddChildToVerticalBox(Blocks);
    }
	DirectServerAddressInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	DirectServerAddressInput->SetHintText(FProjectProject01Localization::Text(
		TEXT("서버 PC IPv4 주소 (예: 192.168.0.10)")));
	ProjectProject01LoginUI::StyleTextBox(DirectServerAddressInput);
	ServerPage->AddChildToVerticalBox(DirectServerAddressInput)->SetPadding(FMargin(6.0f));
	ProjectProject01LoginUI::AddLabel(WidgetTree, ServerPage,
		TEXT("접속할 서버 PC의 IPv4 주소를 직접 입력하세요. 입력한 주소는 이 PC에 저장됩니다."), 12.0f);

	UVerticalBox* AudioPage = ProjectProject01LoginUI::AddSettingsPage(WidgetTree, SettingsPageSwitcher);
	ProjectProject01LoginUI::AddLabel(WidgetTree, AudioPage, TEXT("사운드"), 20.0f);
	MasterVolumeSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, AudioPage, TEXT("전체 음량"), 0.0f, 1.0f, 0.01f, MasterVolumeValueText);
	SFXVolumeSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, AudioPage, TEXT("효과음 음량"), 0.0f, 1.0f, 0.01f, SFXVolumeValueText);
	MusicVolumeSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, AudioPage, TEXT("배경음악 음량"), 0.0f, 1.0f, 0.01f, MusicVolumeValueText);
	UIVolumeSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, AudioPage, TEXT("UI 음량"), 0.0f, 1.0f, 0.01f, UIVolumeValueText);
	VoiceSettingsButton = ProjectProject01LoginUI::AddButton(WidgetTree, AudioPage, TEXT("음성 채팅 장치·음량·마이크 테스트"));
	MuteAllCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, AudioPage, TEXT("전체 음소거"));
	MuteWhenUnfocusedCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, AudioPage, TEXT("게임이 백그라운드일 때 음소거"));

	UVerticalBox* VideoPage = ProjectProject01LoginUI::AddSettingsPage(WidgetTree, SettingsPageSwitcher);
	ProjectProject01LoginUI::AddLabel(WidgetTree, VideoPage, TEXT("화면 및 그래픽"), 20.0f);
	WindowModeComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, VideoPage, TEXT("화면 모드"));
	WindowModeComboBox->AddOption(TEXT("전체화면")); WindowModeComboBox->AddOption(TEXT("테두리 없는 창")); WindowModeComboBox->AddOption(TEXT("창모드"));
	ResolutionComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, VideoPage, TEXT("해상도"));
	TArray<FIntPoint> Resolutions;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
	for (const FIntPoint Resolution : Resolutions)
	{
		const FString Text = FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y);
		if (ResolutionComboBox->FindOptionIndex(Text) == INDEX_NONE) ResolutionComboBox->AddOption(Text);
	}
	VSyncCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, VideoPage, TEXT("수직동기화"));
	FrameRateSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, VideoPage, TEXT("최대 FPS"), 30.0f, 240.0f, 1.0f, FrameRateValueText);
	ResolutionScaleSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, VideoPage, TEXT("해상도 스케일"), 50.0f, 100.0f, 1.0f, ResolutionScaleValueText);
	OverallQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, VideoPage, TEXT("전체 그래픽 프리셋"));
	const TArray<FString> OverallOptions = {
		TEXT("낮음"), TEXT("중간"), TEXT("높음"), TEXT("매우 높음"), TEXT("사용자 지정") };
	for (const FString& Option : OverallOptions) OverallQualityComboBox->AddOption(Option);
	auto PopulateQuality = [](UComboBoxString* Combo)
	{
		Combo->AddOption(TEXT("낮음")); Combo->AddOption(TEXT("중간")); Combo->AddOption(TEXT("높음")); Combo->AddOption(TEXT("매우 높음"));
	};
	AAQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, VideoPage, TEXT("안티앨리어싱 품질")); PopulateQuality(AAQualityComboBox);
	ShadowQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, VideoPage, TEXT("그림자 품질")); PopulateQuality(ShadowQualityComboBox);
	TextureQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, VideoPage, TEXT("텍스처 품질")); PopulateQuality(TextureQualityComboBox);
	EffectsQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, VideoPage, TEXT("이펙트 품질")); PopulateQuality(EffectsQualityComboBox);
	PostProcessQualityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, VideoPage, TEXT("후처리 품질")); PopulateQuality(PostProcessQualityComboBox);
	MotionBlurCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, VideoPage, TEXT("모션 블러"));
	BrightnessSlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, VideoPage, TEXT("밝기 (안전 범위)"), 1.8f, 2.6f, 0.01f, BrightnessValueText);
	VFXIntensityComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, VideoPage, TEXT("화면 번쩍임·일렁임"));
	VFXIntensityComboBox->AddOption(TEXT("기본")); VFXIntensityComboBox->AddOption(TEXT("약하게"));

	UVerticalBox* AccessibilityPage = ProjectProject01LoginUI::AddSettingsPage(WidgetTree, SettingsPageSwitcher);
	ProjectProject01LoginUI::AddLabel(WidgetTree, AccessibilityPage, TEXT("접근성"), 20.0f);
	SubtitlesCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, AccessibilityPage, TEXT("자막 표시"));
	EnhancedVisualCuesCheckBox = ProjectProject01LoginUI::AddCheckRow(
		WidgetTree, AccessibilityPage, TEXT("색상 외 형태·문구로 정보 표시"));
	ColorVisionModeComboBox = ProjectProject01LoginUI::AddComboRow(WidgetTree, AccessibilityPage, TEXT("색각 보정"));
	ColorVisionModeComboBox->AddOption(TEXT("보정 없음"));
	ColorVisionModeComboBox->AddOption(TEXT("녹색약 보정"));
	ColorVisionModeComboBox->AddOption(TEXT("적색약 보정"));
	ColorVisionModeComboBox->AddOption(TEXT("청황색약 보정"));
	ColorVisionSeveritySlider = ProjectProject01LoginUI::AddSliderRow(
		WidgetTree, AccessibilityPage, TEXT("색각 보정 강도"), 0.0f, 10.0f, 1.0f, ColorVisionSeverityValueText);
	ScreenFlashSlider = ProjectProject01LoginUI::AddSliderRow(
		WidgetTree, AccessibilityPage, TEXT("화면 번쩍임 강도"), 0.0f, 1.0f, 0.05f, ScreenFlashValueText);
	ScreenDistortionSlider = ProjectProject01LoginUI::AddSliderRow(
		WidgetTree, AccessibilityPage, TEXT("화면 일렁임 강도"), 0.0f, 1.0f, 0.05f, ScreenDistortionValueText);
	CameraShakeCheckBox = ProjectProject01LoginUI::AddCheckRow(WidgetTree, AccessibilityPage, TEXT("카메라 셰이킹 활성화"));
	ScreenShakeSlider = ProjectProject01LoginUI::AddSliderRow(
		WidgetTree, AccessibilityPage, TEXT("화면 흔들림 강도"), 0.0f, 1.0f, 0.05f, ScreenShakeValueText);
	UIReadableScaleSlider = ProjectProject01LoginUI::AddSliderRow(
		WidgetTree, AccessibilityPage, TEXT("UI 크기·가독성"), 0.8f, 1.3f, 0.05f, UIReadableScaleValueText);
	ProjectProject01LoginUI::AddLabel(WidgetTree, AccessibilityPage,
		TEXT("자막은 자막 데이터가 있는 음성에 적용됩니다. 화면 흔들림 0%는 현재 흔들림을 즉시 중지하고 이후 효과의 기준값으로 저장합니다."), 12.0f);

	UVerticalBox* ControlsPage = ProjectProject01LoginUI::AddSettingsPage(WidgetTree, SettingsPageSwitcher);
	ProjectProject01LoginUI::AddLabel(WidgetTree, ControlsPage, TEXT("조작"), 20.0f);
	SensitivitySlider = ProjectProject01LoginUI::AddSliderRow(WidgetTree, ControlsPage, TEXT("마우스 감도"), 0.1f, 3.0f, 0.01f, SensitivityValueText);
	ForwardKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, ControlsPage, TEXT("앞으로 이동"));
	BackwardKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, ControlsPage, TEXT("뒤로 이동"));
	LeftKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, ControlsPage, TEXT("왼쪽 이동"));
	RightKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, ControlsPage, TEXT("오른쪽 이동"));
	SprintKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, ControlsPage, TEXT("달리기"));
	VoicePushToTalkKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, ControlsPage, TEXT("음성 채팅 누르고 말하기"));
	VoiceToggleKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, ControlsPage, TEXT("음성 채팅 상시 송신 전환"));
	HelpPingKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, ControlsPage, TEXT("도움 요청 핑"));
	DangerPingKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, ControlsPage, TEXT("위험 알림 핑"));
	LocationPingKeyButton = ProjectProject01LoginUI::AddKeyRow(WidgetTree, ControlsPage, TEXT("위치 표시 핑"));
	ProjectProject01LoginUI::AddLabel(WidgetTree, ControlsPage, TEXT("상호작용: 현재 공용 Input Action이 없어 아직 재설정할 수 없습니다. ESC 게임 메뉴는 고정입니다."), 12.0f);

	DefaultsButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("설정 초기화"));
	ApplyButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("적용"));
	CancelButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("돌아가기"));
	VideoConfirmPanel = WidgetTree->ConstructWidget<UVerticalBox>();
	Root->AddChildToVerticalBox(VideoConfirmPanel);
	VideoConfirmText = ProjectProject01LoginUI::AddLabel(
		WidgetTree, VideoConfirmPanel,
		TEXT("화면 설정을 적용했습니다. 이전 설정으로 돌아가려면 되돌리기를 누르세요."), 14.0f);
	RevertVideoButton = ProjectProject01LoginUI::AddButton(WidgetTree, VideoConfirmPanel, TEXT("되돌리기"));
	VideoConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSurveyEntryPointsTest, "ProjectProject01.Surveys.AuthenticatedLobbyEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectSurveyEntryPointsTest::RunTest(const FString&)
{
	auto CountSurveyButtons = [](UUserWidget* Widget)
	{
		TArray<UWidget*> Widgets;
		Widget->WidgetTree->GetAllWidgets(Widgets);
		int32 Count = 0;
		for (UWidget* Child : Widgets)
		{
			const UButton* Button = Cast<UButton>(Child);
			const UTextBlock* Text = IsValid(Button) ? Cast<UTextBlock>(Button->GetChildAt(0)) : nullptr;
			if (IsValid(Text) && Text->GetText().EqualTo(FProjectProject01Localization::Text(TEXT("피드백/설문조사")))) ++Count;
		}
		return Count;
	};
	UUserWidget* Title = NewObject<UProjectProject01TitleWidget>();
	UUserWidget* Lobby = NewObject<UProjectProject01LobbyWidget>();
	if (!TestTrue(TEXT("Title widget initializes"), Title->Initialize()) ||
		!TestTrue(TEXT("Lobby widget initializes"), Lobby->Initialize())) return false;
	TestEqual(TEXT("Title has one login-gated survey entry"), CountSurveyButtons(Title), 1);
	TestEqual(TEXT("Lobby has one survey entry"), CountSurveyButtons(Lobby), 1);
	TArray<UWidget*> Widgets;
	Lobby->WidgetTree->GetAllWidgets(Widgets);
	for (UWidget* Child : Widgets)
	{
		const UButton* Button = Cast<UButton>(Child);
		const UTextBlock* Text = IsValid(Button) ? Cast<UTextBlock>(Button->GetChildAt(0)) : nullptr;
		if (IsValid(Text) && Text->GetText().EqualTo(FProjectProject01Localization::Text(TEXT("피드백/설문조사"))))
			TestFalse(TEXT("Survey starts disabled until authenticated active-survey lookup"), Button->GetIsEnabled());
	}
	UProjectProject01GameInstance* Instance = NewObject<UProjectProject01GameInstance>();
	TestFalse(TEXT("Normal login does not auto-open a survey"), Instance->IsSurveyLoginRequested());
	Instance->SetSurveyLoginRequested(true);
	TestTrue(TEXT("Title intent survives login navigation"), Instance->ConsumeSurveyLoginRequest());
	TestFalse(TEXT("Title survey intent is consumed exactly once"), Instance->ConsumeSurveyLoginRequest());
	Instance->SetSurveyLoginRequested(true);
	Instance->SetSurveyLoginRequested(false);
	TestFalse(TEXT("Cancelling login clears title survey intent"), Instance->ConsumeSurveyLoginRequest());
	UUserWidget* SurveyBase = NewObject<UProjectProject01SurveyWidget>();
	if (!TestTrue(TEXT("Survey widget initializes"), SurveyBase->Initialize())) return false;
	UProjectProject01SurveyWidget* SurveyWidget = CastChecked<UProjectProject01SurveyWidget>(SurveyBase);
	SurveyWidget->ConfigureSurvey(FProjectProject01SurveyDefinition(), Lobby, true);
	TestTrue(TEXT("Title entry logs out on completion or cancellation"), SurveyWidget->ShouldReturnToTitleAfterSurvey());
	SurveyWidget->ConfigureSurvey(FProjectProject01SurveyDefinition(), Lobby);
	TestFalse(TEXT("Lobby entry preserves login"), SurveyWidget->ShouldReturnToTitleAfterSurvey());
	UWidget* Popup = SurveyWidget->GetWidgetFromName(TEXT("SurveyAcknowledgement"));
	if (TestNotNull(TEXT("Submission confirmation popup exists"), Popup))
	{
		TestEqual(TEXT("Popup is hidden before submission"), Popup->GetVisibility(), ESlateVisibility::Collapsed);
		SurveyWidget->ShowSurveyNotice(TEXT("설문에 감사드립니다."), true);
		TestEqual(TEXT("Successful submission shows acknowledgement"), Popup->GetVisibility(), ESlateVisibility::Visible);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSurveySelectionTest, "ProjectProject01.Surveys.SelectionReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectSurveySelectionTest::RunTest(const FString&)
{
	FProjectProject01SurveyQuestion Question;
	Question.Options = { TEXT("A"), TEXT("B"), TEXT("C") };
	TArray<int32> Selected;
	Question.ApplySelection(Selected, 0, true);
	Question.ApplySelection(Selected, 1, true);
	TestTrue(TEXT("Single choice keeps only the newest option"), Selected == TArray<int32>({ 1 }));
	Question.bAllowMultiple = true; Question.MaxSelections = 2;
	Selected.Reset();
	Question.ApplySelection(Selected, 0, true); Question.ApplySelection(Selected, 1, true); Question.ApplySelection(Selected, 2, true);
	TestTrue(TEXT("Overflow removes the oldest option"), Selected == TArray<int32>({ 1, 2 }));
	Question.ApplySelection(Selected, 2, false);
	TestTrue(TEXT("Checked options can be unchecked"), Selected == TArray<int32>({ 1 }));
	Question.MaxSelections = 0; Selected.Reset();
	for (int32 Index = 0; Index < 3; ++Index) Question.ApplySelection(Selected, Index, true);
	TestEqual(TEXT("Unset limit allows all choices"), Selected.Num(), 3);
	return true;
}

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
	Settings->SetScreenShakeScale(.65f);
	Settings->SetCameraShakeEnabled(false);
	TestEqual(TEXT("Disabled camera shaking has zero effective intensity"),Settings->GetScreenShakeScale(),0.f);
	TestEqual(TEXT("Disabling preserves the configured intensity"),Settings->GetConfiguredScreenShakeScale(),.65f);
	Settings->SetCameraShakeEnabled(true);
	TestEqual(TEXT("Re-enabling restores configured intensity"),Settings->GetScreenShakeScale(),.65f);
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
	SetIsFocusable(true);
	BuildWidgetTree();
	ProjectDFSDUI::ApplyTree(WidgetTree);
	ProjectDFSDUI::FramePage(WidgetTree,1120);
	return true;
}

void UProjectProject01LobbyWidget::NativeConstruct()
{
    if(auto* Social=GameInstanceSubsystemOf<UProjectProject01SocialSubsystem>(this))Social->SetLobbyVisible(true);
    if(IsValid(LobbySettingsButton))LobbySettingsButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleLobbySettingsClicked);
	Super::NativeConstruct();
	if (IsValid(LobbySurveyButton)) LobbySurveyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleLobbySurveyClicked);
	LobbySurvey = FProjectProject01SurveyDefinition();
	LobbySurveyPollCounter = 0;
	bLobbySurveyRequestPending = false;
	UProjectProject01GameInstance* SurveyInstance = Cast<UProjectProject01GameInstance>(GetGameInstance());
	bOpenTitleSurveyOnRefresh = IsValid(SurveyInstance) && SurveyInstance->ConsumeSurveyLoginRequest();
	if (IsValid(LobbySurveyButton)) LobbySurveyButton->SetIsEnabled(false);
	bTravelRequested = false;
	bGameStartCountdownActive = false;
	bGameStartCountdownComplete = false;
	bGameStartFadeActive = false;
	bGameStartFadeComplete = false;
	bStartConfirmationRequested = false;
	GameStartCountdownNumber = 0;
	GameStartFadeElapsedSeconds = 0.0f;
	if (IsValid(StartCountdownPanel))
	{
		StartCountdownPanel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		StartCountdownPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IsValid(StartCountdownText))
	{
		StartCountdownText->SetText(FText::GetEmpty());
	}
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
	if (IsValid(ChatInput)) ChatInput->OnTextCommitted.AddUniqueDynamic(this, &UProjectProject01LobbyWidget::HandleChatTextCommitted);
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

	RefreshLobbySurvey();
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().SetTimer(
			LobbyRefreshTimer, this, &UProjectProject01LobbyWidget::RefreshLobbyState, 1.0f, true, 1.0f);
	}
}

void UProjectProject01LobbyWidget::NativeDestruct()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(TutorialTravelTimer);
    if(auto* Social=GameInstanceSubsystemOf<UProjectProject01SocialSubsystem>(this))Social->SetLobbyVisible(false);
    if(IsValid(LobbySettingsWidget))LobbySettingsWidget->RemoveFromParent();
    LobbySettingsWidget=nullptr;
	if (IsValid(LobbySurveyButton)) LobbySurveyButton->OnClicked.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleLobbySurveyClicked);
	if (IsValid(LobbySurveyWidget)) LobbySurveyWidget->RemoveFromParent();
	LobbySurveyWidget = nullptr;
	bOpenTitleSurveyOnRefresh = false;
	LobbySurvey = FProjectProject01SurveyDefinition();
	if (IsValid(LobbySurveyButton)) LobbySurveyButton->SetIsEnabled(false);
	if (IsValid(OpenCreateRoomButton)) OpenCreateRoomButton->OnClicked.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleOpenCreateRoomClicked);
	if (IsValid(CancelCreateRoomButton)) CancelCreateRoomButton->OnClicked.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleCancelCreateRoomClicked);
	if (IsValid(RoomSearchInput)) RoomSearchInput->OnTextChanged.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleRoomSearchChanged);
	if (IsValid(ChatInput)) ChatInput->OnTextCommitted.RemoveDynamic(this, &UProjectProject01LobbyWidget::HandleChatTextCommitted);
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(LobbyRefreshTimer);
		World->GetTimerManager().ClearTimer(GameStartCountdownTimer);
		World->GetTimerManager().ClearTimer(GameStartFadeTimer);
	}
	if (!bTravelRequested)
	{
		CancelGameStartCountdown();
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

void UProjectProject01LobbyWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
    ContinueGameAdmission(InDeltaTime);
    if(IsValid(LobbyViewSwitcher) && LobbyViewSwitcher->GetActiveWidgetIndex()==3)
        if(auto* Lobby=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
            Lobby && Lobby->IsMatchmaking() && DisplayedMatchmakingElapsedSeconds!=Lobby->GetMatchmakingElapsedSeconds())
            RefreshMatchmakingView();
	if (!bGameStartFadeActive || !IsValid(StartCountdownPanel))
	{
		return;
	}

	GameStartFadeElapsedSeconds = FMath::Min(
		GameStartFadeElapsedSeconds + InDeltaTime, GameStartFadeDurationSeconds);
	const float FadeAlpha = GameStartFadeDurationSeconds > UE_SMALL_NUMBER
		? GameStartFadeElapsedSeconds / GameStartFadeDurationSeconds
		: 1.0f;
	StartCountdownPanel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f,
		FMath::Clamp(FadeAlpha, 0.0f, 1.0f)));
}

void UProjectProject01LobbyWidget::HandleOpenCreateRoomClicked()
{
	HandleCloseCodeJoinClicked();
	bShowingCreateRoom = true;
	ShowLobbyView(1);
}
void UProjectProject01LobbyWidget::HandleOpenMatchClicked()
{
    HandleCloseCodeJoinClicked();
    bShowingOpenMatching=true;bShowingMatchmaking=false;bShowingCreateRoom=false;ShowLobbyView(0);
}
void UProjectProject01LobbyWidget::HandleTutorialClicked()
{
	auto* Lobby=GameInstanceSubsystemOf<UProjectProject01LobbySubsystem>(this);
	if (!Lobby || Lobby->HasCurrentRoom() || Lobby->IsMatchmaking() || bTravelRequested || bGameStartFadeActive) return;
	bShowingTutorial=true; ShowLobbyView(6);
}
void UProjectProject01LobbyWidget::HandleSurvivorTutorialClicked() { StartTutorial(false); }
void UProjectProject01LobbyWidget::HandleMannequinTutorialClicked() { StartTutorial(true); }
void UProjectProject01LobbyWidget::HandleSurvivorPracticeClicked() { StartTutorial(false,true); }
void UProjectProject01LobbyWidget::HandleMannequinPracticeClicked() { StartTutorial(true,true); }
void UProjectProject01LobbyWidget::StartTutorial(bool bMannequin,bool bFreePractice)
{
	auto* Lobby=GameInstanceSubsystemOf<UProjectProject01LobbySubsystem>(this);
	if (!Lobby || Lobby->HasCurrentRoom() || Lobby->IsMatchmaking() || Lobby->IsRequestInFlight() || bTravelRequested || bGameStartFadeActive) return;
	UWorld* World=GetWorld();
	if (!IsValid(World)) return;
	const FName TutorialMap(TEXT("/Game/MyProject/Level/MultiplayerTutorial"));
	if (!FPackageName::DoesPackageExist(TutorialMap.ToString()))
	{ SetStatus(TEXT("튜토리얼 맵이 없습니다. MultiplayerTutorial 맵을 준비하세요."),true); return; }
	bTravelRequested=true; SetIsEnabled(false);
	if (auto* GI=GetGameInstance<UProjectProject01GameInstance>()) GI->FadeEntireScreenToBlack(0.6f);
	const TWeakObjectPtr<UProjectProject01LobbyWidget> Weak(this);
	World->GetTimerManager().SetTimer(TutorialTravelTimer,[Weak,bMannequin,bFreePractice,TutorialMap]()
	{
		FString Options=bMannequin ? TEXT("TutorialRole=Mannequin") : TEXT("TutorialRole=Survivor");
		if (bFreePractice) Options+=TEXT("?FreePractice=1");
		if (auto* Self=Weak.Get()) UGameplayStatics::OpenLevel(Self,TutorialMap,true,Options);
	},0.65f,false);
}
void UProjectProject01LobbyWidget::HandleRandomMatchClicked()
{
    HandleCloseCodeJoinClicked();
    bShowingMatchmaking=true;bShowingOpenMatching=false;bShowingCreateRoom=false;bPartyCreationRequested=false;ShowLobbyView(3);RefreshMatchmakingView();RefreshLobbyState();
}
void UProjectProject01LobbyWidget::HandleLobbyModesClicked()
{
    bShowingTutorial=false;
    auto* Lobby=GameInstanceSubsystemOf<UProjectProject01LobbySubsystem>(this);
    if(!Lobby)return;
    if(Lobby->IsMatchmaking()||Lobby->IsRequestInFlight()||(Lobby->HasCurrentRoom()&&Lobby->GetCurrentRoom().bRandomMatch))return;
    if(Lobby->HasCurrentRoom()&&!Lobby->GetCurrentRoom().bRandomParty)return;
    HandleCloseCodeJoinClicked();
    bShowingMatchmaking=false;bShowingOpenMatching=false;bShowingCreateRoom=false;
    if(Lobby->HasCurrentRoom())Lobby->LeaveRoom();else ShowLobbyView(4);
}
void UProjectProject01LobbyWidget::HandleMatchStartClicked()
{
    if(auto* Lobby=GameInstanceSubsystemOf<UProjectProject01LobbySubsystem>(this))Lobby->JoinMatchmaking();
    RefreshMatchmakingView();
}
void UProjectProject01LobbyWidget::HandleMatchCancelClicked()
{
    if(auto* Lobby=GameInstanceSubsystemOf<UProjectProject01LobbySubsystem>(this))Lobby->CancelMatchmaking();
    RefreshMatchmakingView();
}
void UProjectProject01LobbyWidget::RefreshMatchmakingView()
{
    auto* Lobby=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;if(!Lobby)return;
    bool Party=Lobby->HasCurrentRoom()&&Lobby->GetCurrentRoom().bRandomParty;
    bool Matched=Party&&Lobby->GetCurrentRoom().bRandomMatch,Leader=Party&&Lobby->GetCurrentRoom().bIsHost,Waiting=Lobby->IsMatchmaking(),Busy=Lobby->IsRequestInFlight();
    for(UButton* Button : {LogoutButton.Get(),MatchLogoutButton.Get(),ModesLogoutButton.Get()})
        if(Button)Button->SetIsEnabled(!bLobbyLogoutInProgress&&!bTravelRequested&&!bGameStartFadeActive&&(!Matched||Lobby->GetCurrentRoom().bReturnedToRoom));
    if(MatchStartButton)MatchStartButton->SetIsEnabled(Party&&Leader&&Lobby->GetCurrentRoom().Members.Num()==1&&!Matched&&!Waiting&&!Busy);
    if(MatchCancelButton)MatchCancelButton->SetIsEnabled(!Matched&&Leader&&Waiting&&!Lobby->IsCancellingMatchmaking());
    if(MatchBackButton)MatchBackButton->SetIsEnabled(!Matched&&!Waiting&&!Busy);
    FString Text=Matched?(Lobby->GetCurrentRoom().bReturnedToRoom?TEXT("경기 종료 정리 중 — 개인 대기 상태로 돌아갑니다."):TEXT("매칭 완료 (3/3) — 매칭 취소 불가")):Waiting
        ? (Lobby->MatchmakingWaiting>=3?TEXT("매칭 확정 대기 중 — 방 또는 서버 공간을 기다리고 있습니다.")
           :FString::Printf(TEXT("인원 모집 중 (%d/3)"),FMath::Clamp(Lobby->MatchmakingWaiting,1,2)))
        :!Party?TEXT("랜덤 매칭을 준비 중입니다. 실패했다면 뒤로가기 후 다시 시도하세요."):Lobby->GetCurrentRoom().Members.Num()!=1?TEXT("솔로 전용입니다. 뒤로가기 후 다시 입장하세요."):TEXT("시작을 누르면 개인 신청자 3명으로 랜덤 매칭합니다.");
    DisplayedMatchmakingElapsedSeconds=Waiting?Lobby->GetMatchmakingElapsedSeconds():INDEX_NONE;
    if(Waiting)Text+=FString::Printf(TEXT("\n매칭 경과 시간: %d분 %02d초"),DisplayedMatchmakingElapsedSeconds/60,DisplayedMatchmakingElapsedSeconds%60);
    if(!Waiting&&!Matched&&!Lobby->MatchmakingMessage.IsEmpty())Text+=TEXT("\n")+Lobby->MatchmakingMessage;
    if(Party)
    {
        if(Matched)for(const auto& M:Lobby->GetCurrentRoom().Members)Text+=TEXT("\n")+M.DisplayName;
    }
    if(MatchStatusText)MatchStatusText->SetText(FText::FromString(Text));
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
    auto* Lobby=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
    const FString Code=PublicRoomLabels.Contains(SelectedPublicRoomCode)?SelectedPublicRoomCode:FString();
    if(!Lobby||Code.IsEmpty()){SetStatus(TEXT("참가할 공개방을 먼저 선택하세요."),true);return;}
    Lobby->JoinRoom(Code,PublicJoinPasswordInput?PublicJoinPasswordInput->GetText().ToString():FString());
}

void UProjectProject01LobbyWidget::HandleOpenCodeJoinClicked()
{
    auto* Lobby=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
    if(!Lobby||Lobby->HasCurrentRoom()||Lobby->IsRequestInFlight())return;
    if(CodeJoinPanel)CodeJoinPanel->SetVisibility(ESlateVisibility::Visible);
    ShowLobbyView(5);
    if(DirectRoomCodeInput)DirectRoomCodeInput->SetKeyboardFocus();
}

void UProjectProject01LobbyWidget::HandleCloseCodeJoinClicked()
{
    if(CodeJoinPanel)CodeJoinPanel->SetVisibility(ESlateVisibility::Collapsed);
    if(DirectRoomCodeInput)DirectRoomCodeInput->SetText(FText::GetEmpty());
    if(JoinPasswordInput)JoinPasswordInput->SetText(FText::GetEmpty());
    if(LobbyViewSwitcher&&LobbyViewSwitcher->GetActiveWidgetIndex()==5)ShowLobbyView(0);
}

void UProjectProject01LobbyWidget::HandleJoinByCodeClicked()
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
	if(JoinCode.IsEmpty()){SetStatus(TEXT("참가 코드를 입력하세요."),true);return;}
	if(!CodeJoinPanel||CodeJoinPanel->GetVisibility()==ESlateVisibility::Collapsed)return;
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
			if (Lobby->HasCurrentRoom() && Lobby->GetCurrentRoom().bServerPreparing)
			{
				if (Lobby->GetCurrentRoom().bIsHost) Lobby->CancelStartRoom();
				return;
			}
			if (Lobby->HasCurrentRoom() && Lobby->GetCurrentRoom().bStarting)
			{
				if (Lobby->GetCurrentRoom().bIsHost && bGameStartCountdownActive &&
					!bGameStartCountdownComplete && !bStartConfirmationRequested)
				{
					Lobby->CancelStartRoom();
				}
				return;
			}
			if (!Lobby->HasCurrentRoom() || !Lobby->GetCurrentRoom().bGameServerOnline)
			{
				SetStatus(TEXT("경기 세션 운영이 비활성 상태입니다. 운영자가 활성화하면 게임 시작 버튼이 자동으로 활성화됩니다."), true);
				return;
			}
			Lobby->StartRoom();
		}
	}
}

void UProjectProject01LobbyWidget::HandleLeaveRoomClicked()
{
	CancelGameStartCountdown();
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
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || !Lobby->HasCurrentRoom() || !Lobby->GetCurrentRoom().Members.ContainsByPredicate([this](const FProjectProject01RoomMember& M){return !M.bIsHost&&M.UserId==SelectedHostTransferUserId;}))
	{
		SetStatus(TEXT("방장을 넘길 플레이어를 선택하세요."), true);
		return;
	}
	Lobby->TransferHost(SelectedHostTransferUserId);
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
	const FString Message = ChatInput->GetText().ToString().TrimStartAndEnd();
	if (Message.IsEmpty())
	{
		return;
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->SendChat(Message);
		}
	}
}

void UProjectProject01LobbyWidget::HandleChatTextCommitted(const FText&, const ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleSendChatClicked();
	}
}

void UProjectProject01LobbyWidget::HandleLogoutClicked()
{
	if (bLobbyLogoutInProgress || bTravelRequested || bGameStartFadeActive) return;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	// A confirmed random match keeps the existing no-cancellation policy until gameplay starts.
	if (IsValid(Lobby) && Lobby->HasCurrentRoom() && Lobby->GetCurrentRoom().bRandomMatch &&
		!Lobby->GetCurrentRoom().bReturnedToRoom) return;
	bLobbyLogoutInProgress = true;
	SetIsEnabled(false);
	if (IsValid(LogoutButton))
	{
		LogoutButton->SetIsEnabled(false);
	}
	if (IsValid(Lobby) && (bShowingMatchmaking || Lobby->IsMatchmaking() ||
		(Lobby->HasCurrentRoom() && Lobby->GetCurrentRoom().bRandomParty)))
	{
		// Auth logout removes the queue/party on the server in the same transaction as session revocation.
		// Cancel the local poll first: a delayed match response must not start a countdown or recreate a party.
		bShowingMatchmaking = false;
		bPartyCreationRequested = true;
		CancelGameStartCountdown();
		Lobby->ForceClearLocalSession();
		BeginLogout();
		return;
	}
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
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->SetLeaderboardOpenedFromMatchResult(false);
		}
	}
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LeaderBoardTest")));
}

void UProjectProject01LobbyWidget::BeginLogout()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	if (!IsValid(Auth))
	{
		bLobbyLogoutInProgress = false;
		SetIsEnabled(true);
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
	// 타이틀에서 시작한 설문은 설문 위젯이 로그아웃 완료 후 타이틀 이동을 소유합니다.
	if (IsValid(LobbySurveyWidget) && LobbySurveyWidget->ShouldReturnToTitleAfterSurvey()) return;
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
	bLobbyLogoutInProgress = false;
	SetIsEnabled(true);
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
			FString::Printf(TEXT("%s (점검 자동 해제 예정: %s)"), *Announcement, *ShutdownAtUtc), false);
	}
}

void UProjectProject01LobbyWidget::HandleLobbyRequestResult(
	const EProjectProject01LobbyOperation Operation,
	const bool bSuccess,
	const FString& Message)
{
	if (bLobbyLogoutInProgress && Operation != EProjectProject01LobbyOperation::LeaveRoom) return;
	if (!bSuccess &&
		(Operation == EProjectProject01LobbyOperation::StartRoom ||
			Operation == EProjectProject01LobbyOperation::RequestGameTicket) &&
		(Message.Contains(TEXT("게임 서버가 실행 중이 아닙니다")) ||
			Message.Contains(TEXT("게임 서버 연결이 끊겼습니다"))))
	{
		CancelGameStartCountdown();
		SetStatus(TEXT("게임 서버가 꺼져 있어 방과 로그인 세션을 정리했습니다. 로그인 화면으로 돌아갑니다..."), true);
		BeginLogout();
		return;
	}
	if (!bSuccess && Operation == EProjectProject01LobbyOperation::ConfirmStartRoom)
	{
		CancelGameStartCountdown();
		SetStatus(Message.IsEmpty()
			? TEXT("게임 시작을 확정하지 못했습니다. 준비 상태로 돌아갑니다.") : Message, true);
		RefreshLobbyState();
		return;
	}
	if (!bSuccess && Operation == EProjectProject01LobbyOperation::CancelStartRoom)
	{
		bGameStartCountdownActive = false;
		bGameStartCountdownComplete = true;
		bStartConfirmationRequested = false;
		if (UWorld* World = GetWorld(); IsValid(World))
		{
			World->GetTimerManager().ClearTimer(GameStartCountdownTimer);
		}
		SetStatus(Message.IsEmpty() ? TEXT("게임 시작 취소 시간이 지났습니다.") : Message, true);
		RefreshLobbyState();
		return;
	}
	if (Operation != EProjectProject01LobbyOperation::RefreshRooms &&
		Operation != EProjectProject01LobbyOperation::RefreshCurrentRoom &&
		!(bGameStartCountdownActive &&
			(Operation == EProjectProject01LobbyOperation::StartRoom ||
				Operation == EProjectProject01LobbyOperation::CancelStartRoom ||
				Operation == EProjectProject01LobbyOperation::ConfirmStartRoom ||
				Operation == EProjectProject01LobbyOperation::RequestGameTicket)))
	{
		SetStatus(Message, !bSuccess);
	}
	if (!bSuccess && (Operation == EProjectProject01LobbyOperation::StartRoom ||
		Operation == EProjectProject01LobbyOperation::RequestGameTicket))
	{
		CancelGameStartCountdown();
	}
	if (bSuccess && Operation == EProjectProject01LobbyOperation::CancelStartRoom)
	{
		CancelGameStartCountdown();
		SetStatus(TEXT("게임 시작이 취소되었습니다."), false);
	}
	if (bSuccess && Operation == EProjectProject01LobbyOperation::ConfirmStartRoom)
	{
		RefreshLobbyState();
	}
	if (bSuccess && Operation == EProjectProject01LobbyOperation::SendChat && IsValid(ChatInput))
	{
		ChatInput->SetText(FText::GetEmpty());
		ChatInput->SetKeyboardFocus();
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
			bLobbyLogoutInProgress = false;
			SetIsEnabled(true);
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
	if (bLobbyLogoutInProgress) return;
	if (++LobbySurveyPollCounter >= 10) { LobbySurveyPollCounter = 0; RefreshLobbySurvey(); }
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
	if (Lobby->IsMatchmaking()) Lobby->PollMatchmaking();
    else if (Lobby->HasCurrentRoom()) Lobby->RefreshCurrentRoom();
    else if(bShowingMatchmaking&&!bPartyCreationRequested){bPartyCreationRequested=true;Lobby->CreateMatchParty();}
	else Lobby->RefreshRooms();
}

void UProjectProject01LobbyWidget::RefreshRoomListView()
{
	if (!IsValid(PublicRoomList))
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
	PopulatePublicRooms(Lobby->GetRooms());
}

void UProjectProject01LobbyWidget::PopulatePublicRooms(const TArray<FProjectProject01RoomSummary>& Rooms)
{
    if(!PublicRoomList)return;
    PublicRoomLabels.Reset();TArray<TPair<FString,FString>> Entries;
    PasswordPublicRoomCodes.Reset();
	const FString Search = IsValid(RoomSearchInput)
		? RoomSearchInput->GetText().ToString().TrimStartAndEnd() : FString();
	for (const FProjectProject01RoomSummary& Room : Rooms)
	{
		if (!Search.IsEmpty() && !Room.Name.Contains(Search, ESearchCase::IgnoreCase) &&
			!Room.JoinCode.Contains(Search, ESearchCase::IgnoreCase) &&
			!Room.HostDisplayName.Contains(Search, ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FString LockText = Room.bHasPassword ? TEXT(" [비밀번호]") : FString();
		const FString Option = FString::Printf(TEXT("%s (%d/%d) - %s%s [%s]"),
			*Room.Name, Room.MemberCount, Room.MaxPlayers, *Room.HostDisplayName, *LockText,*Room.JoinCode);
        PublicRoomLabels.Add(Room.JoinCode,Option);Entries.Emplace(Room.JoinCode,Option);
        if(Room.bHasPassword)PasswordPublicRoomCodes.Add(Room.JoinCode);
	}
    if(!PublicRoomLabels.Contains(SelectedPublicRoomCode))
    {
        SelectedPublicRoomCode.Reset();
        if(PublicJoinPasswordInput)PublicJoinPasswordInput->SetText(FText::GetEmpty());
    }
    PopulateLobbyChoices(PublicRoomList,Entries,PublicRoomBindings,false);
	UpdatePublicRoomSelection();
}

void UProjectProject01LobbySelectionBinding::HandleClicked()
{
    if(IsValid(Owner))Owner->SelectLobbyEntry(Key,bHost);
}

void UProjectProject01LobbyWidget::PopulateLobbyChoices(UVerticalBox* Panel,const TArray<TPair<FString,FString>>& Entries,TArray<TObjectPtr<UProjectProject01LobbySelectionBinding>>& Bindings,bool bHost)
{
    if(!Panel)return;
    bool Rebuild=Bindings.Num()!=Entries.Num()||Panel->GetChildrenCount()==0;
    if(!Rebuild)for(int32 I=0;I<Entries.Num();++I)if(Bindings[I]->Key!=Entries[I].Key){Rebuild=true;break;}
    if(Rebuild){Panel->ClearChildren();Bindings.Reset();}
    for(int32 I=0;I<Entries.Num();++I)
    {
        UProjectProject01LobbySelectionBinding* Binding=nullptr;
        if(Rebuild)
        {
            Binding=NewObject<UProjectProject01LobbySelectionBinding>(this);Binding->Owner=this;Binding->Key=Entries[I].Key;Binding->bHost=bHost;
            Binding->Button=ProjectProject01LoginUI::AddButton(WidgetTree,Panel,Entries[I].Value);
            Binding->Label=CastChecked<UTextBlock>(Binding->Button->GetChildAt(0));
            FSlateFontInfo Font=Binding->Label->GetFont();Font.Size=22;Binding->Label->SetFont(Font);Binding->Label->SetAutoWrapText(true);
            Binding->Button->OnClicked.AddUniqueDynamic(Binding,&UProjectProject01LobbySelectionBinding::HandleClicked);Bindings.Add(Binding);
        }
        else Binding=Bindings[I];
        Binding->Caption=Entries[I].Value;
    }
    if(Rebuild&&Entries.IsEmpty())ProjectProject01LoginUI::AddLabel(WidgetTree,Panel,bHost?TEXT("위임할 참가자 없음"):TEXT("조건에 맞는 공개방 없음"),20);
    UpdateLobbyChoiceHighlight(Bindings,bHost?SelectedHostTransferUserId:SelectedPublicRoomCode);
}

void UProjectProject01LobbyWidget::UpdateLobbyChoiceHighlight(const TArray<TObjectPtr<UProjectProject01LobbySelectionBinding>>& Bindings,const FString& Selected)
{
    for(const auto& B:Bindings)
    {
        const bool Chosen=B->Key==Selected;
        B->Label->SetText(FText::FromString((Chosen?TEXT("✓ 선택됨: "):TEXT(""))+B->Caption));
        ProjectDFSDUI::Apply(B->Button,Chosen);
    }
}

void UProjectProject01LobbyWidget::SelectLobbyEntry(const FString& Key,bool bHost)
{
    if(bHost)
    {
        if(!HostTransferBindings.ContainsByPredicate([&](const auto& B){return B->Key==Key;}))return;
        SelectedHostTransferUserId=Key;UpdateLobbyChoiceHighlight(HostTransferBindings,Key);RefreshCurrentRoomView();
    }
    else
    {
        if(!PublicRoomLabels.Contains(Key))return;
        SelectedPublicRoomCode=Key;HandleCloseCodeJoinClicked();
        if(PublicJoinPasswordInput)PublicJoinPasswordInput->SetText(FText::GetEmpty());
        UpdatePublicRoomSelection();
    }
}

void UProjectProject01LobbyWidget::UpdatePublicRoomSelection()
{
    if(!SelectedPublicRoomText)return;
    const bool Selected=PublicRoomLabels.Contains(SelectedPublicRoomCode);const FString Option=PublicRoomLabels.FindRef(SelectedPublicRoomCode);
    UpdateLobbyChoiceHighlight(PublicRoomBindings,SelectedPublicRoomCode);
    if(PublicJoinPasswordInput)PublicJoinPasswordInput->SetVisibility(Selected&&PasswordPublicRoomCodes.Contains(SelectedPublicRoomCode)?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    const auto* Lobby=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
    if(JoinRoomButton)JoinRoomButton->SetIsEnabled(Selected&&Lobby&&!Lobby->HasCurrentRoom()&&!Lobby->IsRequestInFlight());
	SelectedPublicRoomText->SetText(FProjectProject01Localization::Text(Selected
		?TEXT("✓ 선택됨: ")+Option+TEXT("\n아래 방 참가 버튼을 눌러야 입장합니다.")
		:TEXT("선택한 공개방 없음 — 위 목록을 눌러 방을 선택하세요.")));
	SelectedPublicRoomBanner->SetBrushColor(Selected?FLinearColor(.11f,.04f,.03f,1):ProjectDFSDUI::Panel());
}

FEventReply UProjectProject01RoomProfileBinding::HandleMouseButtonDown(FGeometry Geometry,const FPointerEvent& MouseEvent)
{
	return FEventReply(MouseEvent.GetEffectingButton()==EKeys::RightMouseButton&&IsValid(Owner)&&Owner->ShowRoomMemberMenu(UserId,MouseEvent));
}

bool UProjectProject01LobbyWidget::ShowRoomMemberMenu(const FString& UserId,const FPointerEvent& MouseEvent)
{
	const auto* Lobby=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
	if(!Lobby||!Lobby->HasCurrentRoom()||bTravelRequested||bGameStartFadeActive||!FSlateApplication::IsInitialized()||
		!Lobby->GetCurrentRoom().Members.ContainsByPredicate([&](const FProjectProject01RoomMember& M){return M.UserId==UserId;}))return false;
	return IsValid(SocialOverlay)&&SocialOverlay->ShowRoomMemberMenu(UserId,MouseEvent.GetScreenSpacePosition());
}

void UProjectProject01LobbyWidget::OpenRoomMemberProfile(const FString& UserId)
{
	const auto* Lobby=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
	if(!Lobby||!Lobby->HasCurrentRoom()||bTravelRequested||bGameStartFadeActive)return;
	if(!Lobby->GetCurrentRoom().Members.ContainsByPredicate([&](const FProjectProject01RoomMember& M){return M.UserId==UserId;}))
	{
		SetStatus(TEXT("이 참가자가 방을 나갔습니다. 참가자 목록을 다시 확인하세요."),true);return;
	}
	if(!IsValid(SocialOverlay)||!SocialOverlay->ShowRoomMemberProfile(UserId))
		SetStatus(TEXT("열린 확인창을 먼저 닫거나 로그인 상태를 확인한 뒤 프로필을 다시 열어주세요."),true);
}

void UProjectProject01LobbyWidget::PopulateRoomMemberProfiles(const TArray<FProjectProject01RoomMember>& Members,bool bRevealRoles)
{
	if(!IsValid(MemberProfilePanel))return;
	TArray<FString> Keys;
	for(const auto& M:Members)Keys.Add(FString::Printf(TEXT("%s:%d:%s:%d%d%d:%d:%s"),*M.UserId,M.DisplayName.Len(),*M.DisplayName,
		M.bIsHost,M.bIsReady,M.bReturnedToRoom,bRevealRoles,*M.AssignedRole));
	// Keep live buttons stable across the room's periodic refresh (including mouse-down/up).
	if(Keys==MemberProfileRowKeys)return;
	MemberProfileRowKeys=MoveTemp(Keys);MemberProfilePanel->ClearChildren();MemberProfileBindings.Reset();
	for(const auto& M:Members)
	{
		auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
		auto* Label=WidgetTree->ConstructWidget<UTextBlock>();
		const FString Role=bRevealRoles&&!M.AssignedRole.IsEmpty()?TEXT(" [")+M.AssignedRole+TEXT("]"):FString();
		Label->SetText(FText::FromString(M.DisplayName+(M.bIsHost?TEXT(" [방장]"):TEXT(""))+FString(TEXT(" — "))+
			(M.bIsHost?TEXT("시작 대기"):M.bIsReady?TEXT("준비"):TEXT("대기"))+Role+(M.bReturnedToRoom?TEXT(" [방 복귀]"):TEXT(""))));
		FSlateFontInfo Font=Label->GetFont();Font.Size=20;Label->SetFont(Font);Label->SetAutoWrapText(true);Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		auto* LabelSlot=Row->AddChildToHorizontalBox(Label);LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LabelSlot->SetVerticalAlignment(VAlign_Center);
		auto* Binding=NewObject<UProjectProject01RoomProfileBinding>(this);Binding->Owner=this;Binding->UserId=M.UserId;
		MemberProfileBindings.Add(Binding);
		auto* Background=WidgetTree->ConstructWidget<UBorder>();Background->SetBrushColor(FLinearColor(.06f,.09f,.13f,1));Background->SetPadding(FMargin(12,8));Background->AddChild(Row);
		Background->SetToolTipText(FProjectProject01Localization::Text(TEXT("우클릭하여 프로필·친구·귓속말·차단·신고")));
		Background->OnMouseButtonDownEvent.BindDynamic(Binding,&UProjectProject01RoomProfileBinding::HandleMouseButtonDown);
		MemberProfilePanel->AddChildToVerticalBox(Background)->SetPadding(FMargin(6,3));
	}
}

void UProjectProject01LobbyWidget::RefreshCurrentRoomView()
{
	if (bLobbyLogoutInProgress) return;
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby))
	{
		return;
	}
	const bool bInRoom = Lobby->HasCurrentRoom();
    if(bInRoom)HandleCloseCodeJoinClicked();
	if (!bInRoom)
	{
		bServerPreparationPending = false;
		CancelGameStartCountdown();
	}
    RefreshMatchmakingView();
    const bool RandomStart=bInRoom&&Lobby->GetCurrentRoom().bRandomMatch;
	const bool bCodeJoinOpen=CodeJoinPanel&&CodeJoinPanel->GetVisibility()!=ESlateVisibility::Collapsed;
	ShowLobbyView(bInRoom&&Lobby->GetCurrentRoom().bRandomParty?3:bInRoom?2:bShowingMatchmaking?3:bShowingCreateRoom?1:bCodeJoinOpen?5:bShowingTutorial?6:bShowingOpenMatching?0:4);
	if (IsValid(CreateRoomButton)) CreateRoomButton->SetIsEnabled(!bInRoom);
	if (IsValid(JoinRoomButton)) JoinRoomButton->SetIsEnabled(!bInRoom&&!Lobby->IsRequestInFlight()&&PublicRoomLabels.Contains(SelectedPublicRoomCode));
    if(OpenCodeJoinButton)OpenCodeJoinButton->SetIsEnabled(!bInRoom&&!Lobby->IsRequestInFlight());
    if(CodeJoinButton)CodeJoinButton->SetIsEnabled(!bInRoom&&!Lobby->IsRequestInFlight());
	if (IsValid(ReadyButton)) ReadyButton->SetVisibility(bInRoom && !Lobby->GetCurrentRoom().bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(StartRoomButton)) StartRoomButton->SetVisibility(bInRoom && !RandomStart && Lobby->GetCurrentRoom().bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(LeaveRoomButton)) LeaveRoomButton->SetVisibility(bInRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(CopyJoinCodeButton)) CopyJoinCodeButton->SetVisibility(bInRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	const bool bCanManageRoom = bInRoom && Lobby->GetCurrentRoom().bIsHost;
	if (HostTransferFrame) HostTransferFrame->SetVisibility(bCanManageRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(TransferHostButton)) TransferHostButton->SetVisibility(bCanManageRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(DeleteRoomButton)) DeleteRoomButton->SetVisibility(bCanManageRoom ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(SendChatButton)) SendChatButton->SetIsEnabled(bInRoom);
	if (!bInRoom)
	{
		if (IsValid(CurrentRoomText)) CurrentRoomText->SetText(FProjectProject01Localization::Text(TEXT("참가 중인 방 없음")));
		PopulateRoomMemberProfiles({},false);
		if(IsValid(SocialOverlay))SocialOverlay->CloseRoomMemberProfile();
		if (IsValid(ChatLogText)) ChatLogText->SetText(FText::GetEmpty());
        SelectedHostTransferUserId.Reset();PopulateLobbyChoices(HostTransferList,{},HostTransferBindings,true);
		return;
	}

	const FProjectProject01RoomState& Room = Lobby->GetCurrentRoom();
	if (IsValid(CurrentRoomText))
	{
		CurrentRoomText->SetText(FText::FromString(FString::Printf(
			TEXT("방: %s\n참가 코드: %s\n인원: %d/3\n경기 세션: %s"),
			*Room.Name, *Room.JoinCode, Room.Members.Num(),
			Room.bServerPreparing ? (Room.SessionWaitPhase==TEXT("Preparing") ? TEXT("서버 준비 중…") : TEXT("빈자리 대기 중…")) : Room.bStarted ? TEXT("진행 중") : Room.bGameServerOnline ? TEXT("시작 가능") : TEXT("운영 비활성"))));
	}
	PopulateRoomMemberProfiles(Room.Members,bGameStartFadeActive||bGameStartFadeComplete);
    TArray<TPair<FString,FString>> HostEntries;
    if(Room.bIsHost)for(const auto& Member:Room.Members)if(!Member.bIsHost&&!Member.UserId.IsEmpty())HostEntries.Emplace(Member.UserId,Member.DisplayName+TEXT(" (")+Member.UserId+TEXT(")"));
    if(!HostEntries.ContainsByPredicate([this](const auto& E){return E.Key==SelectedHostTransferUserId;}))SelectedHostTransferUserId.Reset();
    PopulateLobbyChoices(HostTransferList,HostEntries,HostTransferBindings,true);
	if (IsValid(TransferHostButton))
	{
		TransferHostButton->SetIsEnabled(Room.bIsHost&&!SelectedHostTransferUserId.IsEmpty()&&!Room.bStarting&&!Room.bStarted&&!Room.bServerPreparing&&!Lobby->IsRequestInFlight());
	}
	if (IsValid(ReadyButton))
	{
		ReadyButton->SetIsEnabled(!Room.bServerPreparing && !Room.bStarting && !Room.bStarted);
		if (UTextBlock* ButtonText = Cast<UTextBlock>(ReadyButton->GetChildAt(0)))
		{
			ButtonText->SetText(FProjectProject01Localization::Text(Room.bIsReady ? TEXT("준비 취소") : TEXT("준비")));
		}
	}
	if (IsValid(StartRoomButton))
	{
		const bool bCanCancelStart = Room.bStarting && Room.bIsHost &&
			bGameStartCountdownActive && !bGameStartCountdownComplete &&
			!bStartConfirmationRequested;
		StartRoomButton->SetIsEnabled((Room.bServerPreparing && Room.bIsHost) || bCanCancelStart ||
			(Room.bCanStart && Room.bGameServerOnline && !bGameStartCountdownActive &&
				!bGameStartCountdownComplete && !bGameStartFadeActive));
		if (UTextBlock* ButtonText = Cast<UTextBlock>(StartRoomButton->GetChildAt(0)))
		{
			const TCHAR* ButtonLabel = Room.bServerPreparing ? TEXT("세션 대기 취소") : Room.bStarting
				? (bCanCancelStart ? TEXT("게임 시작 취소") : TEXT("게임 시작 확정 중"))
				: (Room.bGameServerOnline ? TEXT("게임 시작") : TEXT("게임 서버 대기 중"));
			ButtonText->SetText(FProjectProject01Localization::Text(ButtonLabel));
		}
	}

	FString ChatText;
	for (const FProjectProject01RoomChatMessage& Chat : Room.ChatMessages)
	{
		ChatText += FString::Printf(TEXT("%s: %s\n"), *Chat.DisplayName, *Chat.Message);
	}
	if (IsValid(ChatLogText)) ChatLogText->SetText(FText::FromString(ChatText));
	if (Room.bServerPreparing)
	{
		bServerPreparationPending = true;
		// A late-created UI may first see allocation, after the shared countdown.
		if(!bGameStartCountdownActive && !bGameStartCountdownComplete)bGameStartCountdownComplete=true;
		if(bGameStartCountdownComplete)BeginGameStartFade(Room);
		ShowGameSessionSearch(&Room);
	}
	else if (Room.bStarting)
	{
		bServerPreparationPending = false;
		BeginGameStartCountdown();
		if(bGameStartCountdownComplete)BeginGameStartFade(Room);
		if (bGameStartCountdownComplete && Room.bIsHost && !bStartConfirmationRequested &&
			!Lobby->IsRequestInFlight())
		{
			bStartConfirmationRequested = true;
			if (IsValid(StartRoomButton)) StartRoomButton->SetIsEnabled(false);
			Lobby->ConfirmStartRoom();
		}
	}
	else if (Room.bStarted && !Room.bReturnedToRoom)
	{
		bServerPreparationPending = false;
		if (!bGameStartCountdownActive && !bGameStartCountdownComplete)
		{
			// UI가 늦게 만들어진 클라이언트도 확정된 시작 흐름에는 안전하게 합류한다.
			bGameStartCountdownComplete = true;
		}
		if (bGameStartCountdownComplete)
		{
			BeginGameStartFade(Room);
            ContinueGameAdmission(0);
		}
	}
	else if (Room.bStarted && Room.bReturnedToRoom)
	{
		CancelGameStartCountdown();
		SetStatus(TEXT("진행 중인 경기에서 방으로 복귀했습니다. 남은 플레이어를 기다립니다."), false);
	}
	else
	{
		const bool bWasPreparing = bServerPreparationPending;
		bServerPreparationPending = false;
		const bool bWasPendingStart = bWasPreparing || bGameStartCountdownActive || bGameStartCountdownComplete ||
			bGameStartFadeActive || bStartConfirmationRequested;
		CancelGameStartCountdown();
		if (bWasPendingStart)
		{
			SetStatus(bWasPreparing ? TEXT("세션 대기 시간이 초과되었거나 시작이 취소되었습니다. 방을 유지합니다. 다시 준비해 시작하세요.") : TEXT("게임 시작이 취소되었습니다."), false);
		}
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
	if (bTravelRequested || !IsValid(Lobby))
	{
		return;
	}
	if (!bGameStartFadeComplete || !Lobby->HasGameJoinTicketForCurrentRoom() ||
		RoomState.RoomId != AdmissionRoomId || RoomState.MatchId != AdmissionMatchId)
	{
		return;
	}
	APlayerController* PlayerController = GetOwningPlayer();
	if (!IsValid(PlayerController))
	{
		SetStatus(TEXT("게임 서버로 이동할 PlayerController를 찾지 못했습니다."), true);
		return;
	}
	bTravelRequested = true;
	if (UProjectProject01GameInstance* ProjectGameInstance =
		Cast<UProjectProject01GameInstance>(GameInstance))
	{
		ProjectGameInstance->HoldEntireScreenBlack();
	}
	const FString TravelUrl = FString::Printf(TEXT("%s?EncryptionToken=%s?GameTicket=%s?LobbyRoomId=%s?MatchId=%s"),
		*Lobby->GetGameTicketTravelUrl(), *Lobby->GetGameJoinTicket(), *Lobby->GetGameJoinTicket(),
		*Lobby->GetGameTicketRoomId(), *Lobby->GetGameTicketMatchId());
	SetStatus(FString::Printf(TEXT("검증된 MultiplayTest 서버로 이동합니다. 역할: %s"),
		*Lobby->GetGameTicketRole()), false);
	if(auto* Loading=GameInstance->GetSubsystem<UProjectLoadingScreenSubsystem>())Loading->BeginServerTravel();
	PlayerController->ClientTravel(TravelUrl, TRAVEL_Absolute);
}

void UProjectProject01LobbyWidget::ShowGameSessionSearch(const FProjectProject01RoomState* RoomState)
{
	if(!bGameStartFadeComplete || bTravelRequested)return;
	const auto* Lobby=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
	const FProjectProject01RoomState Room=RoomState?*RoomState:Lobby?Lobby->GetCurrentRoom():FProjectProject01RoomState();
	FString Label=TEXT("세션 접속 중…");uint8 Progress=2;
	if (Room.SessionWaitPhase==TEXT("Preparing"))
	{
		Label=TEXT("게임 서버 준비 중…");Progress=10;
	}
	else if (Room.SessionWaitPhase==TEXT("Queued"))
	{
		Label=TEXT("세션 접속 중… 빈 경기 세션 대기");Progress=5;
	}
	if(Room.bStarted){Label=TEXT("게임 서버 접속 허가 확인 중…");Progress=15;}
	if(auto* Instance=GetGameInstance())if(auto* Loading=Instance->GetSubsystem<UProjectLoadingScreenSubsystem>())
		Loading->ShowSessionPreparation(Label,Progress,Room.bServerPreparing && Room.bIsHost && !Room.bRandomMatch && Lobby && !Lobby->IsRequestInFlight(),
			FSimpleDelegate::CreateUObject(this,&ThisClass::HandleStartRoomClicked));
}

void UProjectProject01LobbyWidget::BeginGameStartCountdown()
{
	if (bTravelRequested || bGameStartCountdownActive || bGameStartCountdownComplete)
	{
		return;
	}
	bGameStartCountdownActive = true;
    if(IsValid(LobbySettingsWidget)&&LobbySettingsWidget->IsInViewport())
    {
        LobbySettingsWidget->RemoveFromParent();LobbySettingsWidget=nullptr;SetVisibility(ESlateVisibility::Visible);
    }
	GameStartFadeElapsedSeconds = 0.0f;
	GameStartCountdownNumber = 5;
	if (IsValid(StartCountdownPanel))
	{
		StartCountdownPanel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		StartCountdownPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (IsValid(StartCountdownText))
	{
        UpdateStartCountdownText();
	}
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().SetTimer(
			GameStartCountdownTimer, this,
			&UProjectProject01LobbyWidget::AdvanceGameStartCountdown,
			1.0f, true, 1.0f);
	}
}

void UProjectProject01LobbyWidget::AdvanceGameStartCountdown()
{
	if (!bGameStartCountdownActive)
	{
		return;
	}
	if (GameStartCountdownNumber > 1)
	{
		--GameStartCountdownNumber;
		if (IsValid(StartCountdownText))
		{
            UpdateStartCountdownText();
		}
		return;
	}

	bGameStartCountdownActive = false;
	bGameStartCountdownComplete = true;
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(GameStartCountdownTimer);
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>()
		: nullptr;
	if (!IsValid(Lobby) || !Lobby->HasCurrentRoom())
	{
		CancelGameStartCountdown();
		return;
	}
	const FProjectProject01RoomState& Room = Lobby->GetCurrentRoom();
	BeginGameStartFade(Room);
	if (Room.bStarted)
	{
		ContinueGameAdmission(0);
	}
	else if (Room.bStarting && Room.bIsHost && !bStartConfirmationRequested && !Lobby->IsRequestInFlight())
	{
		bStartConfirmationRequested = true;
		if (IsValid(StartRoomButton))
		{
			StartRoomButton->SetIsEnabled(false);
		}
		Lobby->ConfirmStartRoom();
	}
}

void UProjectProject01LobbyWidget::BeginGameStartFade(const FProjectProject01RoomState& RoomState)
{
	if (bTravelRequested || bGameStartFadeActive || bGameStartFadeComplete ||
		!bGameStartCountdownComplete || (!RoomState.bStarting && !RoomState.bServerPreparing && !RoomState.bStarted))
	{
		return;
	}
	bGameStartFadeActive = true;
    AdmissionRoomId=RoomState.RoomId;AdmissionMatchId=RoomState.MatchId;AdmissionWaitSeconds=0;AdmissionRetrySeconds=0;AdmissionTicketAttempts=0;
	GameStartFadeElapsedSeconds = 0.0f;
	if(RoomState.bStarted)bStartConfirmationRequested = true;
	const FString RoleText = RoomState.AssignedRole.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase)
		? TEXT("마네킹") : TEXT("생존자");
	if (IsValid(StartCountdownPanel))
	{
		StartCountdownPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (IsValid(StartCountdownText))
	{
		StartCountdownText->SetText(FText::FromString(RoomState.AssignedRole.IsEmpty()?FString(TEXT("게임을 시작합니다")):
			FString::Printf(TEXT("게임을 시작합니다\n역할: %s"), *RoleText)));
	}
	if (IsValid(StartRoomButton))
	{
		StartRoomButton->SetIsEnabled(false);
	}
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		if(auto* Instance=GetGameInstance<UProjectProject01GameInstance>())Instance->FadeEntireScreenToBlack(GameStartFadeDurationSeconds);
		World->GetTimerManager().SetTimer(
			GameStartFadeTimer, this, &UProjectProject01LobbyWidget::CompleteGameStartFade,
			GameStartFadeDurationSeconds, false);
	}
}
void UProjectProject01LobbyWidget::UpdateStartCountdownText()
{
    if(!StartCountdownText)return;
    const auto* L=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
    const bool Random=L&&L->HasCurrentRoom()&&L->GetCurrentRoom().bRandomMatch;
    const FString Role=Random?(L->GetCurrentRoom().AssignedRole==TEXT("Mannequin")?TEXT("마네킹"):TEXT("생존자")):FString();
    StartCountdownText->SetText(FText::FromString(Random?FString::Printf(TEXT("게임이 시작됩니다\n역할: %s\n%d..."),*Role,GameStartCountdownNumber)
        :FString::Printf(TEXT("게임을 시작합니다\n%d..."),GameStartCountdownNumber)));
}

void UProjectProject01LobbyWidget::CompleteGameStartFade()
{
	// The full-viewport fade ticks conservatively through hitches. A world timer
	// finishing must not expose loading before the last black frame is reached.
	if(auto* Instance=GetGameInstance<UProjectProject01GameInstance>();bGameStartFadeActive && Instance && !Instance->IsEntireScreenBlack())
	{
		if(auto* World=GetWorld())World->GetTimerManager().SetTimer(GameStartFadeTimer,this,&ThisClass::CompleteGameStartFade,.05f,false);
		return;
	}
	bGameStartFadeActive = false;
	bGameStartFadeComplete = true;
	GameStartFadeElapsedSeconds = GameStartFadeDurationSeconds;
	if (UProjectProject01GameInstance* ProjectGameInstance =
		Cast<UProjectProject01GameInstance>(GetGameInstance()))
	{
		ProjectGameInstance->HoldEntireScreenBlack();
	}
	if (IsValid(StartCountdownPanel))
	{
		StartCountdownPanel->SetBrushColor(FLinearColor::Black);
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (!IsValid(Lobby) || !Lobby->HasCurrentRoom() ||
		(!Lobby->GetCurrentRoom().bStarting && !Lobby->GetCurrentRoom().bServerPreparing && !Lobby->GetCurrentRoom().bStarted))
	{
		CancelGameStartCountdown();
		return;
	}
	if (IsValid(StartCountdownPanel))StartCountdownPanel->SetVisibility(ESlateVisibility::Collapsed);
	ShowGameSessionSearch();
	if (Lobby->HasGameJoinTicketForCurrentRoom())
	{
		TravelToStartedGame(Lobby->GetCurrentRoom());
	}
    else ContinueGameAdmission(0);
}

void UProjectProject01LobbyWidget::ContinueGameAdmission(float DeltaSeconds)
{
	if (bLobbyLogoutInProgress) return;
    if(!bGameStartFadeComplete||bTravelRequested)return;
    auto* Lobby=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
    if(!Lobby||!Lobby->HasCurrentRoom()||Lobby->GetCurrentRoom().bReturnedToRoom||Lobby->GetCurrentRoom().RoomId!=AdmissionRoomId)
    {CancelGameStartCountdown();return;}
	const auto& Room=Lobby->GetCurrentRoom();
	if(!Room.bStarted)
	{
		if(Room.bStarting || Room.bServerPreparing){ShowGameSessionSearch(&Room);return;}
		CancelGameStartCountdown();return;
	}
	if(AdmissionMatchId.IsEmpty())AdmissionMatchId=Room.MatchId;
	else if(Room.MatchId!=AdmissionMatchId){CancelGameStartCountdown();return;}
	ShowGameSessionSearch(&Room);
    AdmissionWaitSeconds+=FMath::Max(0.f,DeltaSeconds);
	AdmissionRetrySeconds=FMath::Max(0.f,AdmissionRetrySeconds-FMath::Max(0.f,DeltaSeconds));
    if(AdmissionWaitSeconds>=15 || (!Lobby->IsRequestInFlight() && AdmissionTicketAttempts>=3 && !Lobby->HasGameJoinTicketForCurrentRoom()))
    {
        CancelGameStartCountdown();
        if(auto* Auth=GameInstanceSubsystemOf<UProjectProject01AuthSubsystem>(this))Auth->InvalidateSessionFromServerFailure(TEXT("경기 접속 허가 대기 시간이 초과되었습니다. 다시 로그인하세요."));
        return;
    }
    if(Lobby->HasGameJoinTicketForCurrentRoom())TravelToStartedGame(Lobby->GetCurrentRoom());
    else if(!Lobby->IsRequestInFlight() && AdmissionRetrySeconds<=0)
	{++AdmissionTicketAttempts;AdmissionRetrySeconds=1.0f;Lobby->RequestGameJoinTicket();}
}

void UProjectProject01LobbyWidget::CancelGameStartCountdown()
{
	const bool bHadCountdown = bGameStartCountdownActive || bGameStartCountdownComplete ||
		bGameStartFadeActive || bGameStartFadeComplete;
	bGameStartCountdownActive = false;
	bGameStartCountdownComplete = false;
	bGameStartFadeActive = false;
	bGameStartFadeComplete = false;
    AdmissionWaitSeconds=0;AdmissionRoomId.Reset();AdmissionMatchId.Reset();AdmissionRetrySeconds=0;AdmissionTicketAttempts=0;
	bStartConfirmationRequested = false;
	GameStartFadeElapsedSeconds = 0.0f;
	GameStartCountdownNumber = 0;
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(GameStartCountdownTimer);
		World->GetTimerManager().ClearTimer(GameStartFadeTimer);
	}
	if (IsValid(StartCountdownPanel))
	{
		StartCountdownPanel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		StartCountdownPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IsValid(StartCountdownText))
	{
		StartCountdownText->SetText(FText::GetEmpty());
	}
	if (bHadCountdown && !bTravelRequested)
	{
		if(auto* Instance=GetGameInstance())if(auto* Loading=Instance->GetSubsystem<UProjectLoadingScreenSubsystem>())Loading->CancelLoading();
		if (UProjectProject01GameInstance* ProjectGameInstance =
			Cast<UProjectProject01GameInstance>(GetGameInstance()))
		{
			ProjectGameInstance->ClearEntireScreenFade();
		}
		if (APlayerController* PlayerController = GetOwningPlayer();
			IsValid(PlayerController) && IsValid(PlayerController->PlayerCameraManager))
		{
			PlayerController->PlayerCameraManager->StopCameraFade();
		}
	}
}

void UProjectProject01LobbyWidget::BuildWidgetTree()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}

	UOverlay* RootOverlay = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = RootOverlay;
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UOverlaySlot* RootSlot = RootOverlay->AddChildToOverlay(Root))
	{
		RootSlot->SetHorizontalAlignment(HAlign_Fill);
		RootSlot->SetVerticalAlignment(VAlign_Fill);
	}
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("멀티플레이"), 30.0f);
    if (IsValid(GetOwningPlayer())) if (auto* Social=CreateWidget<UProjectProject01SocialWidget>(GetOwningPlayer()))
    {
        SocialOverlay=Social;
        if (auto* SocialSlot=RootOverlay->AddChildToOverlay(Social))
        {
            SocialSlot->SetHorizontalAlignment(HAlign_Fill);
            SocialSlot->SetVerticalAlignment(VAlign_Fill);
        }
    }
	auto* LobbyToolbar=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(LobbyToolbar);
	LobbySurveyButton = ProjectProject01LoginUI::AddTabButton(WidgetTree, LobbyToolbar, TEXT("피드백/설문조사"));
    LobbySettingsButton=ProjectProject01LoginUI::AddTabButton(WidgetTree,LobbyToolbar,TEXT("환경설정"));
	LobbySurveyButton->SetIsEnabled(false);
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

	// 0: open matching browser; code entry uses its own page (5).
	UVerticalBox* BrowserPanel = MakeScrollablePanel();
	ProjectProject01LoginUI::AddLabel(WidgetTree, BrowserPanel, TEXT("방 목록 / 참가"), 20.0f);
	RoomSearchInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	RoomSearchInput->SetHintText(FProjectProject01Localization::Text(TEXT("방 이름 / 참가 코드 / 방장 이름 검색")));
	ProjectProject01LoginUI::StyleTextBox(RoomSearchInput);
	ProjectProject01LoginUI::EnlargeInput(RoomSearchInput);
	ProjectProject01LoginUI::AddLargeField(WidgetTree,BrowserPanel,RoomSearchInput,54);
	RefreshRoomsButton = ProjectProject01LoginUI::AddButton(WidgetTree, BrowserPanel, TEXT("공개방 목록 새로고침"));
    auto* RoomListScroll=WidgetTree->ConstructWidget<UScrollBox>();PublicRoomList=WidgetTree->ConstructWidget<UVerticalBox>();RoomListScroll->AddChild(PublicRoomList);
    auto* RoomListFrame=WidgetTree->ConstructWidget<USizeBox>();RoomListFrame->SetHeightOverride(200);RoomListFrame->AddChild(RoomListScroll);
    BrowserPanel->AddChildToVerticalBox(RoomListFrame)->SetPadding(FMargin(6));
	SelectedPublicRoomBanner=WidgetTree->ConstructWidget<UBorder>();SelectedPublicRoomBanner->SetPadding(FMargin(12));
	SelectedPublicRoomText=WidgetTree->ConstructWidget<UTextBlock>();FSlateFontInfo SelectionFont=SelectedPublicRoomText->GetFont();SelectionFont.Size=20;SelectedPublicRoomText->SetFont(SelectionFont);
	SelectedPublicRoomText->SetColorAndOpacity(FSlateColor(FLinearColor::White));SelectedPublicRoomText->SetAutoWrapText(true);
	SelectedPublicRoomBanner->AddChild(SelectedPublicRoomText);BrowserPanel->AddChildToVerticalBox(SelectedPublicRoomBanner)->SetPadding(FMargin(6));
    PublicJoinPasswordInput=WidgetTree->ConstructWidget<UEditableTextBox>();
    PublicJoinPasswordInput->SetHintText(FProjectProject01Localization::Text(TEXT("선택한 공개방 비밀번호")));
    PublicJoinPasswordInput->SetIsPassword(true);ProjectProject01LoginUI::StyleTextBox(PublicJoinPasswordInput);
    ProjectProject01LoginUI::EnlargeInput(PublicJoinPasswordInput);
    BrowserPanel->AddChildToVerticalBox(PublicJoinPasswordInput)->SetPadding(FMargin(6));
    JoinRoomButton=ProjectProject01LoginUI::AddButton(WidgetTree,BrowserPanel,TEXT("선택한 방 참가"));
	UpdatePublicRoomSelection();
    OpenCodeJoinButton=ProjectProject01LoginUI::AddButton(WidgetTree,BrowserPanel,TEXT("참가코드로 참가"));
    OpenCodeJoinButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleOpenCodeJoinClicked);
	OpenCreateRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, BrowserPanel, TEXT("새 방 만들기"));
    auto* BrowserBack=ProjectProject01LoginUI::AddButton(WidgetTree,BrowserPanel,TEXT("뒤로가기"));
    BrowserBack->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleLobbyModesClicked);
	LogoutButton = ProjectProject01LoginUI::AddButton(WidgetTree, BrowserPanel, TEXT("로그아웃"));

	// 1: 사용자가 '새 방 만들기'를 눌렀을 때만 보이는 생성 화면
	UVerticalBox* CreatePanel = MakeScrollablePanel();
	ProjectProject01LoginUI::AddLabel(WidgetTree, CreatePanel, TEXT("새 방 만들기"), 20.0f);
	RoomNameInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	RoomNameInput->SetHintText(FProjectProject01Localization::Text(TEXT("방 이름 (1~48자)")));
	ProjectProject01LoginUI::StyleTextBox(RoomNameInput);
	CreatePanel->AddChildToVerticalBox(RoomNameInput)->SetPadding(FMargin(6.0f));
	PublicRoomCheckBox = WidgetTree->ConstructWidget<UCheckBox>();
	PublicRoomCheckBox->SetIsChecked(true);
	UBorder* PublicModeBackground = WidgetTree->ConstructWidget<UBorder>();
	PublicModeBackground->SetBrushColor(ProjectDFSDUI::Panel());
	PublicModeBackground->SetPadding(FMargin(8.0f));
	UHorizontalBox* PublicModeRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	PublicModeRow->AddChild(PublicRoomCheckBox);
	UTextBlock* PublicLabel = WidgetTree->ConstructWidget<UTextBlock>();
	PublicLabel->SetText(FProjectProject01Localization::Text(TEXT("공개방: 체크 / 비공개방: 체크 해제 (참가 코드를 공유해 입장)")));
	PublicLabel->SetColorAndOpacity(FSlateColor(ProjectDFSDUI::Text()));
	PublicModeRow->AddChild(PublicLabel);
	PublicModeBackground->AddChild(PublicModeRow);
	CreatePanel->AddChildToVerticalBox(PublicModeBackground)->SetPadding(FMargin(6.0f));
	CreatePasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	CreatePasswordInput->SetHintText(FProjectProject01Localization::Text(TEXT("방 비밀번호 (선택, 사용 시 4~64자)")));
	ProjectProject01LoginUI::StyleTextBox(CreatePasswordInput);
	CreatePasswordInput->SetIsPassword(true);
	CreatePanel->AddChildToVerticalBox(CreatePasswordInput)->SetPadding(FMargin(6.0f));
	CreateRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, CreatePanel, TEXT("방 만들기"));
	CancelCreateRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, CreatePanel, TEXT("방 목록으로 돌아가기"));

	// 2: 방 생성 또는 참가에 성공한 사용자만 보는 방 정보 화면
	UVerticalBox* RoomPanel = MakeScrollablePanel();
	ProjectProject01LoginUI::AddLabel(WidgetTree, RoomPanel, TEXT("현재 방 정보"), 20.0f);
	CurrentRoomText = ProjectProject01LoginUI::AddLabel(WidgetTree, RoomPanel, TEXT("참가 중인 방 없음"), 16.0f);
	CopyJoinCodeButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("참가 코드 복사"));
	MemberProfilePanel=WidgetTree->ConstructWidget<UVerticalBox>();RoomPanel->AddChildToVerticalBox(MemberProfilePanel);
	ReadyButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("준비"));
	StartRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("게임 시작"));
	LeaveRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("방 나가기"));
    HostTransferList=WidgetTree->ConstructWidget<UVerticalBox>();auto* HostScroll=WidgetTree->ConstructWidget<UScrollBox>();HostScroll->AddChild(HostTransferList);
    HostTransferFrame=WidgetTree->ConstructWidget<USizeBox>();HostTransferFrame->SetHeightOverride(140);HostTransferFrame->AddChild(HostScroll);
    RoomPanel->AddChildToVerticalBox(HostTransferFrame)->SetPadding(FMargin(6));
	TransferHostButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("선택한 플레이어에게 방장 넘기기"));
	DeleteRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("방 삭제 (모두 로비로)"));
	ReadyButton->SetVisibility(ESlateVisibility::Collapsed);
	StartRoomButton->SetVisibility(ESlateVisibility::Collapsed);
	LeaveRoomButton->SetVisibility(ESlateVisibility::Collapsed);
	CopyJoinCodeButton->SetVisibility(ESlateVisibility::Collapsed);
	HostTransferFrame->SetVisibility(ESlateVisibility::Collapsed);
	TransferHostButton->SetVisibility(ESlateVisibility::Collapsed);
	DeleteRoomButton->SetVisibility(ESlateVisibility::Collapsed);

	ChatLogText = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
	ChatLogText->SetIsReadOnly(true);
	ChatLogText->SetHintText(FProjectProject01Localization::Text(TEXT("방 채팅")));
	ProjectProject01LoginUI::StyleTextBox(ChatLogText);
	ProjectProject01LoginUI::EnlargeInput(ChatLogText);
	ProjectProject01LoginUI::AddLargeField(WidgetTree,RoomPanel,ChatLogText,180);
	ChatInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	ChatInput->SetHintText(FProjectProject01Localization::Text(TEXT("채팅 입력 (최대 300자)")));
	ProjectProject01LoginUI::StyleTextBox(ChatInput);
	ProjectProject01LoginUI::EnlargeInput(ChatInput);
	ProjectProject01LoginUI::AddLargeField(WidgetTree,RoomPanel,ChatInput,54);
	SendChatButton = ProjectProject01LoginUI::AddButton(WidgetTree, RoomPanel, TEXT("채팅 보내기"));
	SendChatButton->SetIsEnabled(false);

    UVerticalBox* Matching=MakeScrollablePanel(); // 3: explicit random-match consent, readiness and queue.
    ProjectProject01LoginUI::AddLabel(WidgetTree,Matching,TEXT("랜덤 매칭 (솔로 전용)"),26);
    MatchStatusText=ProjectProject01LoginUI::AddLabel(WidgetTree,Matching,TEXT("시작 → 매칭 중…"),22);
    MatchStartButton=ProjectProject01LoginUI::AddButton(WidgetTree,Matching,TEXT("시작"));MatchStartButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleMatchStartClicked);
    MatchCancelButton=ProjectProject01LoginUI::AddButton(WidgetTree,Matching,TEXT("매칭 취소"));MatchCancelButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleMatchCancelClicked);
    MatchBackButton=ProjectProject01LoginUI::AddButton(WidgetTree,Matching,TEXT("뒤로가기"));MatchBackButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleLobbyModesClicked);
    MatchLogoutButton=ProjectProject01LoginUI::AddButton(WidgetTree,Matching,TEXT("로그아웃 / 로그인 화면으로"));
    MatchLogoutButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleLogoutClicked);
    auto* Modes=MakeScrollablePanel(); // 4: lobby entry; existing room browser is only under Open Matching.
    ProjectProject01LoginUI::AddLabel(WidgetTree,Modes,TEXT("매칭 방식 선택"),26);
    auto* Random=ProjectProject01LoginUI::AddButton(WidgetTree,Modes,TEXT("랜덤 매칭"));Random->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleRandomMatchClicked);
    auto* Open=ProjectProject01LoginUI::AddButton(WidgetTree,Modes,TEXT("오픈 매칭"));Open->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleOpenMatchClicked);
    auto* Tutorial=ProjectProject01LoginUI::AddButton(WidgetTree,Modes,TEXT("튜토리얼"));Tutorial->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleTutorialClicked);
	OpenLeaderboardTestButton = ProjectProject01LoginUI::AddButton(WidgetTree, Modes, TEXT("리더보드 열기"));
    ModesLogoutButton=ProjectProject01LoginUI::AddButton(WidgetTree,Modes,TEXT("로그아웃 / 로그인 화면으로"));
    ModesLogoutButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleLogoutClicked);

    // 5: direct code entry only. The public room browser is an inactive sibling page.
    CodeJoinPanel=MakeScrollablePanel();
    CodeJoinPanel->SetVisibility(ESlateVisibility::Collapsed);
    ProjectProject01LoginUI::AddLabel(WidgetTree,CodeJoinPanel,TEXT("참가 코드로 방 참가"),22);
    DirectRoomCodeInput = WidgetTree->ConstructWidget<UEditableTextBox>();
    DirectRoomCodeInput->SetHintText(FProjectProject01Localization::Text(TEXT("6~8자리 참가 코드 (비공개방 또는 직접 참가)")));
    ProjectProject01LoginUI::StyleTextBox(DirectRoomCodeInput);
    ProjectProject01LoginUI::EnlargeInput(DirectRoomCodeInput);
    ProjectProject01LoginUI::AddLargeField(WidgetTree,CodeJoinPanel,DirectRoomCodeInput,54);
    JoinPasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>();
    JoinPasswordInput->SetHintText(FProjectProject01Localization::Text(TEXT("참가 비밀번호 (비밀번호방만)")));
    ProjectProject01LoginUI::StyleTextBox(JoinPasswordInput);
    JoinPasswordInput->SetIsPassword(true);
    ProjectProject01LoginUI::EnlargeInput(JoinPasswordInput);
    ProjectProject01LoginUI::AddLargeField(WidgetTree,CodeJoinPanel,JoinPasswordInput,54);
    CodeJoinButton=ProjectProject01LoginUI::AddButton(WidgetTree,CodeJoinPanel,TEXT("방 참가"));
    CodeJoinButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleJoinByCodeClicked);
    auto* CloseCodeJoin=ProjectProject01LoginUI::AddButton(WidgetTree,CodeJoinPanel,TEXT("뒤로가기"));
    CloseCodeJoin->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleCloseCodeJoinClicked);

    auto* TutorialRoles=MakeScrollablePanel(); // 6: practice is outside both matchmaking queues.
    ProjectProject01LoginUI::AddLabel(WidgetTree,TutorialRoles,TEXT("멀티 튜토리얼 · 역할 선택"),26);
    ProjectProject01LoginUI::AddLabel(WidgetTree,TutorialRoles,TEXT("별도 연습장에서 목표와 조작을 배우고 직접 연습합니다. 실제 경기 기록에는 반영하지 않습니다."),20);
    auto* SurvivorTutorial=ProjectProject01LoginUI::AddButton(WidgetTree,TutorialRoles,TEXT("생존자 튜토리얼"));
    SurvivorTutorial->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleSurvivorTutorialClicked);
    auto* MannequinTutorial=ProjectProject01LoginUI::AddButton(WidgetTree,TutorialRoles,TEXT("마네킹 튜토리얼"));
    MannequinTutorial->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleMannequinTutorialClicked);
    auto* SurvivorPractice=ProjectProject01LoginUI::AddButton(WidgetTree,TutorialRoles,TEXT("생존자 연습모드"));
    SurvivorPractice->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleSurvivorPracticeClicked);
    auto* MannequinPractice=ProjectProject01LoginUI::AddButton(WidgetTree,TutorialRoles,TEXT("마네킹 연습모드"));
    MannequinPractice->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleMannequinPracticeClicked);
    auto* TutorialBack=ProjectProject01LoginUI::AddButton(WidgetTree,TutorialRoles,TEXT("뒤로가기"));
    TutorialBack->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleLobbyModesClicked);

    // Compact only the matching panels; social overlays and other game menus retain their layout.
    for(UVerticalBox* Panel : {BrowserPanel,Matching,Modes,CodeJoinPanel.Get()})
        for(UWidget* Child : Panel->GetAllChildren())
            if(auto* PanelChildSlot=Cast<UVerticalBoxSlot>(Child->Slot))PanelChildSlot->SetPadding(FMargin(6.0f,4.0f));

	StartCountdownPanel = WidgetTree->ConstructWidget<UBorder>();
	StartCountdownPanel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	StartCountdownPanel->SetPadding(FMargin(36.0f, 24.0f));
	StartCountdownPanel->SetHorizontalAlignment(HAlign_Center);
	StartCountdownPanel->SetVerticalAlignment(VAlign_Center);
	StartCountdownPanel->SetVisibility(ESlateVisibility::Collapsed);
	StartCountdownText = WidgetTree->ConstructWidget<UTextBlock>();
	StartCountdownText->SetText(FText::FromString(TEXT("게임을 시작합니다\n5...")));
	StartCountdownText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	StartCountdownText->SetJustification(ETextJustify::Center);
	FSlateFontInfo CountdownFont = StartCountdownText->GetFont();
	CountdownFont.Size = 42;
	StartCountdownText->SetFont(CountdownFont);
	StartCountdownPanel->AddChild(StartCountdownText);
	if (UOverlaySlot* CountdownSlot = RootOverlay->AddChildToOverlay(StartCountdownPanel))
	{
		CountdownSlot->SetHorizontalAlignment(HAlign_Fill);
		CountdownSlot->SetVerticalAlignment(VAlign_Fill);
	}
    RefreshMatchmakingView();
	ShowLobbyView(4);
}

void UProjectProject01LobbyWidget::SetStatus(const FString& Message, const bool bIsError)
{
	if (!IsValid(LobbyStatusText))
	{
		return;
	}
	LobbyStatusText->SetText(FProjectProject01Localization::Text(Message));
	LobbyStatusText->SetColorAndOpacity(bIsError ? FSlateColor(ProjectDFSDUI::Danger()) : FSlateColor(ProjectDFSDUI::Text()));
}

void UProjectProject01LobbyWidget::RefreshLobbySurvey()
{
	if (bLobbySurveyRequestPending) return;
	UProjectProject01GameInstance* Instance = Cast<UProjectProject01GameInstance>(GetGameInstance());
	const UProjectProject01AuthSubsystem* Auth = IsValid(Instance)
		? Instance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	if (!IsValid(Auth) || !Auth->IsSignedIn() || bTravelRequested || bLogoutAfterLeave)
	{
		LobbySurvey = FProjectProject01SurveyDefinition();
		if (IsValid(LobbySurveyButton)) LobbySurveyButton->SetIsEnabled(false);
		return;
	}
	bLobbySurveyRequestPending = true;
	const TWeakObjectPtr<UProjectProject01LobbyWidget> WeakThis(this);
	Instance->FetchActivePlayerSurvey([WeakThis](bool bSuccess, bool bHasSurvey,
		const FProjectProject01SurveyDefinition& Survey, const FString& Message)
	{
		if (UProjectProject01LobbyWidget* Widget = WeakThis.Get(); IsValid(Widget))
		{
			Widget->bLobbySurveyRequestPending = false;
			const UGameInstance* GameInstance = Widget->GetGameInstance();
			const UProjectProject01AuthSubsystem* CurrentAuth = IsValid(GameInstance)
				? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
			const bool bCanParticipate = Widget->IsInViewport() && IsValid(CurrentAuth) && CurrentAuth->IsSignedIn() &&
				!Widget->bTravelRequested && !Widget->bLogoutAfterLeave;
			Widget->LobbySurvey = bCanParticipate && bSuccess && bHasSurvey
				? Survey : FProjectProject01SurveyDefinition();
			if (IsValid(Widget->LobbySurveyButton)) Widget->LobbySurveyButton->SetIsEnabled(
				Widget->LobbySurvey.IsValid() && !Widget->bServerPreparationPending && !Widget->bGameStartCountdownActive && !Widget->bGameStartFadeActive);
			if (Widget->bOpenTitleSurveyOnRefresh)
			{
				Widget->bOpenTitleSurveyOnRefresh = false;
				if (bCanParticipate)
				{
					Widget->OpenLobbySurvey(true);
					if (!Widget->LobbySurvey.IsValid() && IsValid(Widget->LobbySurveyWidget))
						Widget->LobbySurveyWidget->ShowSurveyNotice(Message.IsEmpty()
							? TEXT("현재 진행 중인 설문이 없습니다.") : Message, false);
				}
			}
		}
	});
}

void UProjectProject01LobbyWidget::PrepareTitleSurveyLogout()
{
	if (!IsValid(LobbySurveyWidget) || !LobbySurveyWidget->ShouldReturnToTitleAfterSurvey()) return;
	bTravelRequested = true;
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(LobbyRefreshTimer);
	if (UGameInstance* Instance = GetGameInstance())
		if (UProjectProject01LobbySubsystem* Lobby = Instance->GetSubsystem<UProjectProject01LobbySubsystem>())
			Lobby->ForceClearLocalSession();
}

void UProjectProject01LobbyWidget::HandleLobbySurveyClicked()
{
	OpenLobbySurvey();
}

void UProjectProject01LobbyWidget::HandleLobbySettingsClicked()
{
    if(!IsValid(GetOwningPlayer())||bServerPreparationPending||bGameStartCountdownActive||bGameStartFadeActive||bTravelRequested)return;
    if(IsValid(LobbySettingsWidget)&&LobbySettingsWidget->IsInViewport())return;
    LobbySettingsWidget=CreateWidget<UProjectProject01SettingsWidget>(GetOwningPlayer());
    if(IsValid(LobbySettingsWidget)){LobbySettingsWidget->ConfigureReturnWidget(this);LobbySettingsWidget->AddToViewport(650);SetVisibility(ESlateVisibility::Collapsed);}
}

void UProjectProject01LobbyWidget::OpenLobbySurvey(const bool bFromTitle)
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	if (bTravelRequested || bLogoutAfterLeave || bServerPreparationPending || bGameStartCountdownActive || bGameStartFadeActive ||
		(!LobbySurvey.IsValid() && !bFromTitle) || !IsValid(Auth) || !Auth->IsSignedIn() || !IsValid(GetOwningPlayer()) ||
		(IsValid(LobbySurveyWidget) && LobbySurveyWidget->IsInViewport())) return;
	LobbySurveyWidget = CreateWidget<UProjectProject01SurveyWidget>(GetOwningPlayer(), UProjectProject01SurveyWidget::StaticClass());
	if (!IsValid(LobbySurveyWidget)) return;
	LobbySurveyWidget->ConfigureSurvey(LobbySurvey, this, bFromTitle);
	SetVisibility(ESlateVisibility::Collapsed);
	LobbySurveyWidget->AddToViewport(600);
}

bool UProjectProject01LeaderboardWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	BuildWidgetTree();
	ProjectDFSDUI::ApplyTree(WidgetTree);
	ProjectDFSDUI::FramePage(WidgetTree,1120);
	return true;
}

void UProjectProject01LeaderboardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(RefreshButton)) RefreshButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleRefreshClicked);
	if (IsValid(MannequinTabButton)) MannequinTabButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleMannequinTabClicked);
	if (IsValid(SurvivorTabButton)) SurvivorTabButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleSurvivorTabClicked);
	if (IsValid(IndividualScopeButton)) IndividualScopeButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleIndividualScopeClicked);
	if (IsValid(TeamScopeButton)) TeamScopeButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleTeamScopeClicked);
	if (IsValid(SortComboBox)) SortComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleSortSelectionChanged);
	if (IsValid(SubmitRecordButton)) SubmitRecordButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleSubmitRecordClicked);
	if (IsValid(ReturnToLobbyButton)) ReturnToLobbyButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleReturnToLobbyClicked);
	if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->OnClicked.AddUniqueDynamic(this, &UProjectProject01LeaderboardWidget::HandleReturnToRoomClicked);

	SetSelectedRole(SelectedRole, false);

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
	bOpenedFromMatchResult = Lobby->IsLeaderboardOpenedFromMatchResult() && Lobby->HasCurrentRoom();
	if (IsValid(ReturnToRoomButton))
	{
		ReturnToRoomButton->SetVisibility(bOpenedFromMatchResult
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (Lobby->HasPendingLeaderboardRecord())
	{
		const FProjectProject01PendingLeaderboardRecord& Pending = Lobby->GetPendingLeaderboardRecord();
		SetSelectedRole(Pending.Role, false);
		if (IsValid(SubmitRecordButton)) SubmitRecordButton->SetIsEnabled(true);
		SetStatus(TEXT("방금 종료된 서버 검증 성공 기록을 등록할 수 있습니다."), false);
	}
	else
	{
		if (IsValid(SubmitRecordButton)) SubmitRecordButton->SetIsEnabled(false);
		SetStatus(TEXT("조회 전용입니다. 등록할 서버 검증 경기 기록이 없습니다."), false);
	}
	Lobby->RefreshLeaderboard(GetSelectedRole(), GetSelectedSort(), GetSelectedScope());
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
			Lobby->RefreshLeaderboard(GetSelectedRole(), GetSelectedSort(), GetSelectedScope());
		}
	}
}

void UProjectProject01LeaderboardWidget::HandleMannequinTabClicked()
{
	SetSelectedRole(EProjectProject01LeaderboardRole::Mannequin, true);
}

void UProjectProject01LeaderboardWidget::HandleSurvivorTabClicked()
{
	SetSelectedRole(EProjectProject01LeaderboardRole::Survivor, true);
}

void UProjectProject01LeaderboardWidget::HandleIndividualScopeClicked()
{
	SetSelectedScope(EProjectProject01LeaderboardScope::Individual, true);
}

void UProjectProject01LeaderboardWidget::HandleTeamScopeClicked()
{
	SetSelectedScope(EProjectProject01LeaderboardScope::Team, true);
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

	if (!Lobby->HasPendingLeaderboardRecord())
	{
		SetStatus(TEXT("등록할 서버 검증 경기 기록이 없습니다."), true);
		return;
	}

	SetStatus(TEXT("방금 경기의 서버 검증 성공 기록을 등록하는 중..."), false);
	Lobby->SubmitPendingLeaderboardRecord();
}

void UProjectProject01LeaderboardWidget::HandleReturnToLobbyClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01LobbySubsystem* Lobby = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>() : nullptr;
	if (IsValid(Lobby))
	{
		Lobby->ClearPendingLeaderboardRecord();
		Lobby->SetLeaderboardOpenedFromMatchResult(false);
		if (bOpenedFromMatchResult && Lobby->HasCurrentRoom())
		{
			bReturnToLobbyAfterLeave = true;
			if (IsValid(ReturnToLobbyButton)) ReturnToLobbyButton->SetIsEnabled(false);
			if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->SetIsEnabled(false);
			SetStatus(TEXT("방을 나간 뒤 로비로 돌아가는 중..."), false);
			Lobby->LeaveRoom();
			return;
		}
	}
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LobbyLevel")));
}

void UProjectProject01LeaderboardWidget::HandleReturnToRoomClicked()
{
	if (!bOpenedFromMatchResult)
	{
		return;
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
		{
			Lobby->ClearPendingLeaderboardRecord();
			Lobby->SetLeaderboardOpenedFromMatchResult(false);
		}
	}
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
	if (Operation == EProjectProject01LobbyOperation::LeaveRoom && bReturnToLobbyAfterLeave)
	{
		bReturnToLobbyAfterLeave = false;
		if (!bSuccess)
		{
			if (IsValid(ReturnToLobbyButton)) ReturnToLobbyButton->SetIsEnabled(true);
			if (IsValid(ReturnToRoomButton)) ReturnToRoomButton->SetIsEnabled(true);
			SetStatus(Message, true);
			return;
		}
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LobbyLevel")));
		return;
	}
	if (Operation != EProjectProject01LobbyOperation::RefreshLeaderboard &&
		Operation != EProjectProject01LobbyOperation::SubmitLeaderboardRecord)
	{
		return;
	}
	SetStatus(Message, !bSuccess);
	if (bSuccess && Operation == EProjectProject01LobbyOperation::SubmitLeaderboardRecord)
	{
		if (IsValid(SubmitRecordButton)) SubmitRecordButton->SetIsEnabled(false);
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UProjectProject01LobbySubsystem* Lobby = GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>())
			{
				Lobby->RefreshLeaderboard(GetSelectedRole(), GetSelectedSort(), GetSelectedScope());
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

	const bool bTeam = GetSelectedRole() == EProjectProject01LeaderboardRole::Survivor &&
		GetSelectedScope() == EProjectProject01LeaderboardScope::Team;
	FString Text = GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin
		? TEXT("순위 | 이름 | 방 이름 | 역할 | 개인 종합점수 | 포획 | 첫 포획(초) | 전원 포획(초)\n")
		: bTeam
			? TEXT("순위 | 방 이름 | 인원 | 팀 종합점수 | 팀 전체 구출 | 마지막 탈출(초)\n")
			: TEXT("순위 | 이름 | 방 이름 | 역할 | 개인 종합점수 | 구출 | 탈출(초)\n");
	for (const FProjectProject01LeaderboardEntry& Entry : Lobby->GetLeaderboardEntries())
	{
		if (GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin)
		{
			Text += FString::Printf(TEXT("%d | %s | %s | %s | %.1f | %d | %.2f | %.2f\n"),
				Entry.Rank, *Entry.DisplayName, *Entry.RoomName, *Entry.Role, Entry.OverallScore, Entry.CaptureCount,
				Entry.FirstCaptureSeconds, Entry.AllCapturedSeconds);
		}
		else if (bTeam)
		{
			Text += FString::Printf(TEXT("%d | %s | %d | %.1f | %.2f | %.2f\n"),
				Entry.Rank, *Entry.RoomName, Entry.SurvivorCount, Entry.OverallScore,
				Entry.TeamRescueCount, Entry.LastEscapeSeconds);
		}
		else
		{
			Text += FString::Printf(TEXT("%d | %s | %s | %s | %.1f | %d | %.2f\n"),
				Entry.Rank, *Entry.DisplayName, *Entry.RoomName, *Entry.Role, Entry.OverallScore,
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
	const bool bMannequin = GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin;
	const bool bTeam = !bMannequin && GetSelectedScope() == EProjectProject01LeaderboardScope::Team;
	SortComboBox->AddOption(bTeam ? TEXT("팀 종합 평균 점수") : TEXT("개별 플레이어 점수"));
	if (bMannequin)
	{
		SortComboBox->AddOption(TEXT("붙잡은 횟수"));
		SortComboBox->AddOption(TEXT("첫 포획 시간"));
		SortComboBox->AddOption(TEXT("전원 포획 시간"));
	}
	else
	{
		SortComboBox->AddOption(bTeam ? TEXT("팀 전체 구출 횟수") : TEXT("구출 횟수"));
		SortComboBox->AddOption(bTeam ? TEXT("마지막 탈출 시간") : TEXT("탈출 시간"));
	}
	SortComboBox->SetSelectedOption(bTeam ? TEXT("팀 종합 평균 점수") : TEXT("개별 플레이어 점수"));
	bUpdatingSortOptions = false;
}

void UProjectProject01LeaderboardWidget::SetSelectedRole(
	const EProjectProject01LeaderboardRole NewRole,
	const bool bRefreshLeaderboard)
{
	SelectedRole = NewRole;
	const bool bMannequin = SelectedRole == EProjectProject01LeaderboardRole::Mannequin;
	if (bMannequin)
	{
		SelectedScope = EProjectProject01LeaderboardScope::Individual;
	}
	if (IsValid(MannequinTabButton))
	{
		ProjectProject01LoginUI::SetButtonText(
			MannequinTabButton, bMannequin ? TEXT("● 마네킹") : TEXT("마네킹"));
		ProjectDFSDUI::Apply(MannequinTabButton,bMannequin);
	}
	if (IsValid(SurvivorTabButton))
	{
		ProjectProject01LoginUI::SetButtonText(
			SurvivorTabButton, bMannequin ? TEXT("생존자") : TEXT("● 생존자"));
		ProjectDFSDUI::Apply(SurvivorTabButton,!bMannequin);
	}
	if (IsValid(SurvivorScopeBar))
	{
		SurvivorScopeBar->SetVisibility(bMannequin ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	SetSelectedScope(SelectedScope, false);
	RefreshSortOptions();
	if (bRefreshLeaderboard)
	{
		HandleRefreshClicked();
	}
}

void UProjectProject01LeaderboardWidget::SetSelectedScope(
	const EProjectProject01LeaderboardScope NewScope,
	const bool bRefreshLeaderboard)
{
	SelectedScope = GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin
		? EProjectProject01LeaderboardScope::Individual
		: NewScope;
	const bool bIndividual = SelectedScope == EProjectProject01LeaderboardScope::Individual;
	if (IsValid(IndividualScopeButton))
	{
		ProjectProject01LoginUI::SetButtonText(
			IndividualScopeButton, bIndividual ? TEXT("● 개별") : TEXT("개별"));
		ProjectDFSDUI::Apply(IndividualScopeButton,bIndividual);
	}
	if (IsValid(TeamScopeButton))
	{
		ProjectProject01LoginUI::SetButtonText(
			TeamScopeButton, bIndividual ? TEXT("팀 점수") : TEXT("● 팀 점수"));
		ProjectDFSDUI::Apply(TeamScopeButton,!bIndividual);
	}
	RefreshSortOptions();
	if (bRefreshLeaderboard)
	{
		HandleRefreshClicked();
	}
}

EProjectProject01LeaderboardRole UProjectProject01LeaderboardWidget::GetSelectedRole() const
{
	return SelectedRole;
}

EProjectProject01LeaderboardSort UProjectProject01LeaderboardWidget::GetSelectedSort() const
{
	const FString Option = IsValid(SortComboBox) ? SortComboBox->GetSelectedOption() : FString();
	if (Option == TEXT("붙잡은 횟수")) return EProjectProject01LeaderboardSort::Captures;
	if (Option == TEXT("첫 포획 시간")) return EProjectProject01LeaderboardSort::FirstCapture;
	if (Option == TEXT("전원 포획 시간")) return EProjectProject01LeaderboardSort::AllCaptured;
	if (Option == TEXT("구출 횟수") || Option == TEXT("팀 전체 구출 횟수")) return EProjectProject01LeaderboardSort::Rescues;
	if (Option == TEXT("탈출 시간") || Option == TEXT("마지막 탈출 시간")) return EProjectProject01LeaderboardSort::Escape;
	return EProjectProject01LeaderboardSort::Overall;
}

EProjectProject01LeaderboardScope UProjectProject01LeaderboardWidget::GetSelectedScope() const
{
	return GetSelectedRole() == EProjectProject01LeaderboardRole::Mannequin
		? EProjectProject01LeaderboardScope::Individual
		: SelectedScope;
}

void UProjectProject01LeaderboardWidget::BuildWidgetTree()
{
	UScrollBox* ScrollRoot = WidgetTree->ConstructWidget<UScrollBox>();
	WidgetTree->RootWidget = ScrollRoot;
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	ScrollRoot->AddChild(Root);

	ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("리더보드"), 24.0f);
	ProjectProject01LoginUI::AddLabel(WidgetTree, Root,
		TEXT("서버가 검증한 성공 경기만 등록됩니다. 개인 점수와 생존자 팀 평균 점수에는 경기 당시 방 이름이 함께 표시됩니다."), 14.0f);
	LeaderboardStatusText = ProjectProject01LoginUI::AddLabel(WidgetTree, Root, TEXT("리더보드를 불러오는 중..."), 14.0f);

	UHorizontalBox* RoleTabBar = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* RoleTabBarSlot = Root->AddChildToVerticalBox(RoleTabBar))
	{
		RoleTabBarSlot->SetPadding(FMargin(6.0f, 4.0f));
		RoleTabBarSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	MannequinTabButton = ProjectProject01LoginUI::AddTabButton(WidgetTree, RoleTabBar, TEXT("마네킹"));
	SurvivorTabButton = ProjectProject01LoginUI::AddTabButton(WidgetTree, RoleTabBar, TEXT("생존자"));

	SurvivorScopeBar = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* ScopeBarSlot = Root->AddChildToVerticalBox(SurvivorScopeBar))
	{
		ScopeBarSlot->SetPadding(FMargin(6.0f, 2.0f));
		ScopeBarSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	IndividualScopeButton = ProjectProject01LoginUI::AddTabButton(WidgetTree, SurvivorScopeBar, TEXT("개별"));
	TeamScopeButton = ProjectProject01LoginUI::AddTabButton(WidgetTree, SurvivorScopeBar, TEXT("팀 점수"));

	SortComboBox = WidgetTree->ConstructWidget<UProjectDFSDComboBox>();
	ProjectProject01LoginUI::StyleComboBox(SortComboBox);
	Root->AddChildToVerticalBox(SortComboBox)->SetPadding(FMargin(6.0f));
	RefreshButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("리더보드 새로고침"));

	LeaderboardText = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
	LeaderboardText->SetIsReadOnly(true);
	ProjectProject01LoginUI::StyleTextBox(LeaderboardText);
	Root->AddChildToVerticalBox(LeaderboardText)->SetPadding(FMargin(6.0f));

	SubmitRecordButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("방금 경기의 성공 기록을 리더보드에 등록"));
	SubmitRecordButton->SetIsEnabled(false);
	ReturnToLobbyButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("로비로 돌아가기"));
	ReturnToRoomButton = ProjectProject01LoginUI::AddButton(WidgetTree, Root, TEXT("방으로 돌아가기"));
	ReturnToRoomButton->SetVisibility(ESlateVisibility::Collapsed);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCheckpointSlotPreviewWidgetTest,"ProjectProject01.SinglePlayer.CheckpointPreviewWidgets",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCheckpointSlotPreviewWidgetTest::RunTest(const FString&)
{
	UUserWidget* TitleBase=NewObject<UProjectProject01TitleWidget>();
	UUserWidget* MenuBase=NewObject<UProjectProject01SessionMenuWidget>();
	if (!TestTrue(TEXT("Title initializes"),TitleBase->Initialize()) || !TestTrue(TEXT("Pause menu initializes"),MenuBase->Initialize())) return false;
	auto* Title=CastChecked<UProjectProject01TitleWidget>(TitleBase);
	auto* Menu=CastChecked<UProjectProject01SessionMenuWidget>(MenuBase);
	TestEqual(TEXT("Title has three preview slots"),Title->SaveSlotPreviewImages.Num(),3);
	TestEqual(TEXT("Manual save has three preview slots"),Menu->ManualSaveSlotPreviewImages.Num(),3);
	TestEqual(TEXT("In-game loading has three preview slots"),Menu->LoadSaveSlotPreviewImages.Num(),3);
	UTexture2D* Texture=UTexture2D::CreateTransient(320,180);
	ProjectProject01LoginUI::RefreshSavePreview(Title->SaveSlotPreviewImages,0,Texture);
	TestTrue(TEXT("Available image displayed"),Title->SaveSlotPreviewImages[0]->GetVisibility()==ESlateVisibility::HitTestInvisible);
	TestTrue(TEXT("Image brush retains texture"),Title->SaveSlotPreviewImages[0]->GetBrush().GetResourceObject()==Texture);
	UImage* Preview=Title->SaveSlotPreviewImages[0];
	auto* ImageSlot=CastChecked<UOverlaySlot>(Preview->Slot);
	TestTrue(TEXT("Image fills the complete preview frame instead of a 32px corner"),
		ImageSlot->GetHorizontalAlignment()==HAlign_Fill && ImageSlot->GetVerticalAlignment()==VAlign_Fill);
	auto* Layers=CastChecked<UOverlay>(Preview->GetParent());
	TestTrue(TEXT("Valid image hides missing-image text"),Layers->GetChildAt(0)->GetVisibility()==ESlateVisibility::Collapsed);
	ProjectProject01LoginUI::RefreshSavePreview(Title->SaveSlotPreviewImages,0,nullptr);
	TestTrue(TEXT("Deleted/legacy slot clears previous texture"),Title->SaveSlotPreviewImages[0]->GetBrush().GetResourceObject()==nullptr);
	TestTrue(TEXT("Missing image reveals placeholder"),Title->SaveSlotPreviewImages[0]->GetVisibility()==ESlateVisibility::Collapsed);
	TestTrue(TEXT("Empty slot restores missing-image text"),Layers->GetChildAt(0)->GetVisibility()==ESlateVisibility::HitTestInvisible);
	FProjectProject01SaveSlotInfo Info; Info.SlotIndex=2; Info.bHasSave=true; Info.SavedMapPackageName=TEXT("/Game/Maps/TestMap");
	Info.SavedAtUtc=FDateTime(2026,10,16,1,2); Info.RemainingDeathCount=3;
	const FString Description=ProjectProject01LoginUI::SaveSlotDescription(Info,TEXT(" (현재)"));
	TestTrue(TEXT("Slot description contains map and Korean save time"),Description.Contains(TEXT("TestMap")) && Description.Contains(TEXT("2026-10-16 10:02")));
	TestTrue(TEXT("Slot description retains current marker and death count"),Description.Contains(TEXT("(현재)")) && Description.Contains(TEXT("데스카운트 3")));
	if (FApp::CanEverRender())
	{
		// Exercise real Slate layout and paint, not only UWidget properties.
		TArray<FColor> ThumbnailPixels;ThumbnailPixels.Init(FColor(35,100,165,255),320*180);
		for (int32 Y=25;Y<155;++Y) for (int32 X=40;X<280;++X) ThumbnailPixels[Y*320+X]=FColor(185,210,230,255);
		void* Data=Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data,ThumbnailPixels.GetData(),ThumbnailPixels.Num()*sizeof(FColor));
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();Texture->UpdateResource();
		ProjectProject01LoginUI::RefreshSavePreview(Title->SaveSlotPreviewImages,0,Texture);
		Info.SlotIndex=1;
		Title->SaveSlotStatusTexts[0]->SetText(FText::FromString(ProjectProject01LoginUI::SaveSlotDescription(Info,TEXT(" (현재)"))));
		for (int32 Index=1;Index<3;++Index)
		{
			FProjectProject01SaveSlotInfo Empty;Empty.SlotIndex=Index+1;
			Title->SaveSlotStatusTexts[Index]->SetText(FText::FromString(ProjectProject01LoginUI::SaveSlotDescription(Empty,TEXT(""))));
		}
		Title->TitlePageSwitcher->SetActiveWidgetIndex(1);
		FWidgetRenderer Renderer(true);
		const TSharedRef<SWidget> Slate=Title->WidgetTree->RootWidget->TakeWidget();
		const FString Directory=FPaths::ProjectSavedDir()/TEXT("CheckpointPreviewTests");
		IFileManager::Get().MakeDirectory(*Directory,true);
		for (const FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
		{
			UTextureRenderTarget2D* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,true);
			TArray<FColor> Pixels;
			if (!TestNotNull(TEXT("Slot UI render target"),Target)) continue;
			const float DpiScale=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
			// Match viewport DPI and allow auto-wrapped labels a second layout pass.
			Renderer.DrawWidget(Target,Slate,DpiScale,FVector2D(Size),0.016f);
			Renderer.DrawWidget(Target,Slate,DpiScale,FVector2D(Size),0.016f);
			TestTrue(TEXT("Slot UI readback"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
			const FVector2D Actual=Preview->GetCachedGeometry().GetLocalSize();
			TestTrue(TEXT("Painted image occupies the 160x90 frame"),FMath::IsNearlyEqual(Actual.X,160.0,0.1)&&FMath::IsNearlyEqual(Actual.Y,90.0,0.1));
			const FGeometry& Back=Title->SaveSlotsBackButton->GetCachedGeometry();
			TestTrue(TEXT("Return button remains inside viewport at project DPI"),Back.GetAbsolutePosition().Y+Back.GetAbsoluteSize().Y<=Size.Y+0.1f);
			TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);
			FFileHelper::SaveArrayToFile(Png,*(Directory/FString::Printf(TEXT("SlotLayout-%dx%d.png"),Size.X,Size.Y)));
		}
	}
	return true;
}
#endif

void UProjectProject01LeaderboardWidget::SetStatus(const FString& Message, const bool bIsError)
{
	if (!IsValid(LeaderboardStatusText))
	{
		return;
	}
	LeaderboardStatusText->SetText(FProjectProject01Localization::Text(Message));
	LeaderboardStatusText->SetColorAndOpacity(
		bIsError ? FSlateColor(ProjectDFSDUI::Danger()) : FSlateColor(ProjectDFSDUI::Text()));
}

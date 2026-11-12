#include "ProjectPadSettingsPanel.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectDFSDUITheme.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    FSlateFontInfo PanelFont(int32 Size = 14) { return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size); }
    TSharedRef<STextBlock> PanelText(const FString& Value, int32 Size = 14)
    {
        return SNew(STextBlock).Font(PanelFont(Size)).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(Value));
    }
    const TArray<TSharedPtr<FString>>& GlyphNames()
    {
        static const TArray<TSharedPtr<FString>> Names = {MakeShared<FString>(TEXT("자동 (Xbox · Steam Deck)")), MakeShared<FString>(TEXT("Xbox")), MakeShared<FString>(TEXT("PlayStation"))};
        return Names;
    }
}

void UProjectPadSettingsPanel::Load(const UProjectProject01GameUserSettings* S)
{
    Bindings.SetNum(ProjectGamepad::ActionCount);
    for (int32 I = 0; I < ProjectGamepad::ActionCount; ++I) Bindings[I] = S ? S->GetPadBinding(EProjectPadAction(I)) : ProjectGamepad::DefaultKey(EProjectPadAction(I));
    if (S)
    {
        LookX = S->GetPadLookSpeedX(); LookY = S->GetPadLookSpeedY(); Cursor = S->GetPadCursorSpeed(); DeadZone = S->GetPadDeadZone();
        LookSmoothing = S->GetPadLookSmoothing(); LookCurve = S->GetPadLookCurve();
        bInvertY = S->GetPadInvertY(); Glyphs = int32(S->GetPadGlyphs()); bVibration = S->GetPadVibration(); Vibration = S->GetPadVibrationStrength();
    }
    Capturing = INDEX_NONE;
}
void UProjectPadSettingsPanel::Defaults()
{
    Bindings.SetNum(ProjectGamepad::ActionCount);
    for (int32 I = 0; I < ProjectGamepad::ActionCount; ++I) Bindings[I] = ProjectGamepad::DefaultKey(EProjectPadAction(I));
    LookX = LookY = Cursor = 1.f; DeadZone = .2f; bInvertY = false; Glyphs = 0; bVibration = true; Vibration = .8f; Capturing = INDEX_NONE;
    LookSmoothing = .5f; LookCurve = 2.f;
}
void UProjectPadSettingsPanel::Store(UProjectProject01GameUserSettings* S) const
{
    if (!S) return;
    for (int32 I = 0; I < Bindings.Num(); ++I) S->SetPadBinding(EProjectPadAction(I), Bindings[I]);
    S->SetPadLookSpeed(LookX, LookY); S->SetPadInvertY(bInvertY); S->SetPadCursorSpeed(Cursor); S->SetPadDeadZone(DeadZone);
    S->SetPadLookFeel(LookSmoothing, LookCurve);
    S->SetPadGlyphs(EProjectPadGlyphs(Glyphs)); S->SetPadVibration(bVibration, Vibration);
}
FString UProjectPadSettingsPanel::ConflictWarning() const
{
    for (int32 A = 0; A < Bindings.Num(); ++A)
        for (int32 B = A + 1; B < Bindings.Num(); ++B)
            if (Bindings[A] == Bindings[B] && ProjectGamepad::SameScreen(EProjectPadAction(A), EProjectPadAction(B)))
                return FString::Printf(TEXT("패드 %s 버튼이 '%s'와 '%s'에 함께 쓰입니다. 하나를 바꾸세요."),
                    *ProjectGamepad::FixedLabel(Bindings[A]), ProjectGamepad::ActionName(EProjectPadAction(A)), ProjectGamepad::ActionName(EProjectPadAction(B)));
    return FString();
}
void UProjectPadSettingsPanel::BeginCapture(int32 Action)
{
    UProjectGamepadSubsystem* Pad = UProjectGamepadSubsystem::Get(this);
    if (!Pad) return;
    Capturing = Action;
    const TWeakObjectPtr<UProjectPadSettingsPanel> Weak(this);
    Pad->BeginPadCapture([Weak](FKey Key)
    {
        if (UProjectPadSettingsPanel* Self = Weak.Get(); Self && Self->Bindings.IsValidIndex(Self->Capturing)) { Self->Bindings[Self->Capturing] = Key; Self->Capturing = INDEX_NONE; }
    }, [Weak] { if (UProjectPadSettingsPanel* Self = Weak.Get()) Self->Capturing = INDEX_NONE; });
}

TSharedRef<SWidget> UProjectPadSettingsPanel::RebuildWidget()
{
    if (Bindings.Num() != ProjectGamepad::ActionCount) Load(UProjectProject01GameUserSettings::Get());
    auto Box = SNew(SVerticalBox);
    auto Row = [&Box](const FString& Label, TSharedRef<SWidget> Control)
    {
        Box->AddSlot().AutoHeight().Padding(8, 3)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[PanelText(Label)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(260)[Control]]];
    };
    auto Slider = [this, &Row](const FString& Label, float* Value, float Min, float Max, bool bPercent)
    {
        Row(Label, SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SSlider).MinValue(Min).MaxValue(Max).StepSize(.05f)
                .Value_Lambda([Value] { return *Value; }).OnValueChanged_Lambda([Value](float V) { *Value = V; })]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[SNew(SBox).WidthOverride(52)
                [SNew(STextBlock).Font(PanelFont()).ColorAndOpacity(ProjectDFSDUI::Text())
                    .Text_Lambda([Value, bPercent] { return FText::FromString(bPercent ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(*Value * 100)) : FString::Printf(TEXT("%.2f"), *Value)); })]]);
    };
    auto Check = [&Row](const FString& Label, bool* Value)
    {
        Row(Label, SNew(SCheckBox).IsChecked_Lambda([Value] { return *Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([Value](ECheckBoxState S) { *Value = S == ECheckBoxState::Checked; }));
    };
    Box->AddSlot().AutoHeight().Padding(4, 16, 4, 4)[PanelText(TEXT("게임패드"), 20)];
    Box->AddSlot().AutoHeight().Padding(8, 0, 8, 6)[SNew(STextBlock).Font(PanelFont(12)).ColorAndOpacity(ProjectDFSDUI::Muted()).AutoWrapText(true)
        .Text(FText::FromString(TEXT("Xbox 패드와, 스팀·DS4Windows를 거친 PlayStation·스팀 패드를 지원합니다. 메뉴에서는 왼쪽 스틱이 커서, A 클릭, B 뒤로입니다.")))];
    Slider(TEXT("시점 감도 (가로)"), &LookX, .2f, 3.f, false);
    Slider(TEXT("시점 감도 (세로)"), &LookY, .2f, 3.f, false);
    Slider(TEXT("시점 부드러움"), &LookSmoothing, 0.f, 1.f, true);
    Slider(TEXT("시점 반응 곡선"), &LookCurve, 1.f, 3.f, false);
    Box->AddSlot().AutoHeight().Padding(8, 0, 8, 6)[SNew(STextBlock).Font(PanelFont(12)).ColorAndOpacity(ProjectDFSDUI::Muted()).AutoWrapText(true)
        .Text(FText::FromString(TEXT("부드러움을 올리면 스틱을 끝까지 밀어도 시점이 천천히 빨라집니다(0%는 바로 반응). 곡선을 올리면 살짝 밀 때 더 천천히 돕니다.")))];
    Check(TEXT("시점 세로 반전"), &bInvertY);
    Slider(TEXT("메뉴 커서 속도"), &Cursor, .3f, 3.f, false);
    Slider(TEXT("스틱 데드존"), &DeadZone, .05f, .45f, true);
    Row(TEXT("버튼 표기"), SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&GlyphNames()).ComboBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>("ComboBox"))
        .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return PanelText(Item.IsValid() ? *Item : FString()); })
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type) { const int32 I = GlyphNames().IndexOfByKey(Item); if (I != INDEX_NONE) Glyphs = I; })
        [SNew(STextBlock).Font(PanelFont()).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([this] { return FText::FromString(*GlyphNames()[FMath::Clamp(Glyphs, 0, 2)]); })]);
    Check(TEXT("진동"), &bVibration);
    Slider(TEXT("진동 세기"), &Vibration, 0.f, 1.f, true);
    Box->AddSlot().AutoHeight().Padding(8, 10, 8, 2)[SNew(STextBlock).Font(PanelFont(12)).ColorAndOpacity(ProjectDFSDUI::Muted()).AutoWrapText(true)
        .Text(FText::FromString(TEXT("버튼을 누른 뒤 패드에서 쓸 버튼을 누르세요. Esc로 취소합니다.")))];
    for (int32 I = 0; I < ProjectGamepad::ActionCount; ++I)
        Row(ProjectGamepad::ActionName(EProjectPadAction(I)), SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).HAlign(HAlign_Center)
            .OnClicked_Lambda([this, I] { BeginCapture(I); return FReply::Handled(); })
            [SNew(STextBlock).Font(PanelFont()).ColorAndOpacity(ProjectDFSDUI::Text())
                .Text_Lambda([this, I] { return FText::FromString(Capturing == I ? FString(TEXT("패드 버튼을 누르세요")) : Bindings.IsValidIndex(I) ? ProjectGamepad::FixedLabel(Bindings[I]) : FString()); })]);
    Box->AddSlot().AutoHeight().Padding(8, 6)[SAssignNew(WarningText, STextBlock).Font(PanelFont(13)).ColorAndOpacity(ProjectDFSDUI::Danger()).AutoWrapText(true)
        .Text_Lambda([this] { return FText::FromString(ConflictWarning()); })];
    return Box;
}
void UProjectPadSettingsPanel::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    WarningText.Reset();
    if (Capturing != INDEX_NONE) if (UProjectGamepadSubsystem* Pad = UProjectGamepadSubsystem::Get(this)) Pad->CancelPadCapture();
}

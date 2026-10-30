#include "ProjectConfirmDialog.h"
#include "ProjectDFSDUITheme.h"
#include "InputCoreTypes.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
    const TCHAR* SkipSection=TEXT("ProjectDFSD.ConfirmationSkips");
    FString Today(){return FDateTime::Now().ToString(TEXT("%Y-%m-%d"));}
}

bool UProjectConfirmDialogWidget::IsSkippedToday(const FString& SkipKey)
{
    FString Day;
    return !SkipKey.IsEmpty()&&GConfig&&GConfig->GetString(SkipSection,*SkipKey,Day,GGameUserSettingsIni)&&Day==Today();
}

void UProjectConfirmDialogWidget::SkipToday(const FString& SkipKey)
{
    if(SkipKey.IsEmpty()||!GConfig)return;
    GConfig->SetString(SkipSection,*SkipKey,*Today(),GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);
}

void UProjectConfirmDialogWidget::Ask(const FString& Question,TFunction<void()> OnYes,const FString& InSkipKey)
{
    if(IsSkippedToday(InSkipKey)){if(OnYes)OnYes();return;}
    Text=Question;SkipKey=InSkipKey;bSkipChecked=false;bQuestion=true;PendingYes=MoveTemp(OnYes);
    SetVisibility(ESlateVisibility::Visible);SetIsFocusable(true);SetKeyboardFocus();
}

void UProjectConfirmDialogWidget::Notify(const FString& Message)
{
    Text=Message;SkipKey.Reset();bSkipChecked=false;bQuestion=false;PendingYes=nullptr;
    SetVisibility(ESlateVisibility::Visible);SetIsFocusable(true);SetKeyboardFocus();
}

void UProjectConfirmDialogWidget::Close(bool bYes)
{
    // Hidden first: the action may open another dialog through this same widget.
    SetVisibility(ESlateVisibility::Collapsed);
    if(ReturnFocusTo.IsValid())ReturnFocusTo->SetKeyboardFocus();
    // The opt-out counts only with "예": a "아니오" does nothing, so it is asked again next time.
    if(bYes&&bSkipChecked)SkipToday(SkipKey);
    TFunction<void()> Action=MoveTemp(PendingYes);PendingYes=nullptr;
    if(bYes&&Action)Action();
}

TSharedRef<SWidget> UProjectConfirmDialogWidget::RebuildWidget()
{
    auto Label=[](const TAttribute<FText>& Value,int32 Size)
    {return SNew(STextBlock).Text(Value).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(true).Justification(ETextJustify::Center);};
    auto Button=[this,Label](const FString& Caption,bool bYes,TAttribute<EVisibility> Shown)
    {
        return SNew(SBox).Padding(8,0).Visibility(Shown)
        [
            SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ContentPadding(FMargin(40,10))
            .OnClicked_Lambda([this,bYes](){Close(bYes);return FReply::Handled();})
            [Label(FText::FromString(Caption),18)]
        ];
    };
    const TAttribute<EVisibility> QuestionOnly=TAttribute<EVisibility>::CreateWeakLambda(this,[this](){return bQuestion?EVisibility::Visible:EVisibility::Collapsed;});
    const TAttribute<EVisibility> NoticeOnly=TAttribute<EVisibility>::CreateWeakLambda(this,[this](){return bQuestion?EVisibility::Collapsed:EVisibility::Visible;});
    const TAttribute<EVisibility> SkipShown=TAttribute<EVisibility>::CreateWeakLambda(this,[this](){return bQuestion&&!SkipKey.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;});
    return SNew(SOverlay)
        +SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,.7f))]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [
            SNew(SBox).WidthOverride(520)
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(32)
                [
                    SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,24)[Label(TAttribute<FText>::CreateWeakLambda(this,[this](){return FText::FromString(Text);}),20)]
                    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,20)
                    [
                        SNew(SCheckBox).Visibility(SkipShown)
                        .IsChecked_Lambda([this](){return bSkipChecked?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bSkipChecked=State==ECheckBoxState::Checked;})
                        [Label(FText::FromString(TEXT("오늘 하루 다시 묻지 않기")),16)]
                    ]
                    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [
                        SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("예"),true,QuestionOnly)]
                        +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("아니오"),false,QuestionOnly)]
                        +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("확인"),false,NoticeOnly)]
                    ]
                ]
            ]
        ];
}

FReply UProjectConfirmDialogWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(IsOpen()&&Event.GetKey()==EKeys::Escape){Close(false);return FReply::Handled();}
    return Super::NativeOnKeyDown(Geometry,Event);
}

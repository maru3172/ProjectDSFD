// File: Source/ProjectProject01/Private/ProjectRuleVote.cpp
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#include "ProjectRuleVote.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectCustomMatchRules.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

namespace ProjectRuleVoteUI
{
    const FLinearColor Ink(.9f,.88f,.84f),Faint(.55f,.53f,.5f),Paper(.035f,.033f,.03f,1.f),Mark(.86f,.72f,.42f);
}

/** Full-screen vote panel: the same dark blackout as the item card pick, three rule cards with live vote counts. */
class SProjectRuleVotePanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectRuleVotePanel){}
        SLATE_ARGUMENT(FProjectRuleVoteView,View)
        SLATE_ARGUMENT(TWeakObjectPtr<UProjectRuleVoteComponent>,Owner)
        SLATE_ATTRIBUTE(float,SecondsLeft)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args){Owner=Args._Owner;SecondsLeft=Args._SecondsLeft;Rebuild(Args._View);}
    void Rebuild(const FProjectRuleVoteView& InView)
    {
        View=InView;
        TSharedRef<SHorizontalBox> Cards=SNew(SHorizontalBox);
        for(int32 I=0;I<View.Cards.Num();++I)Cards->AddSlot().FillWidth(1.f).Padding(10,0)[MakeCard(I)];
        auto Text=[](const TAttribute<FText>& T,int32 Size,const FLinearColor& Color,const TCHAR* Face=TEXT("Regular"))
        {return SNew(STextBlock).Text(T).Font(FCoreStyle::GetDefaultFontStyle(Face,Size)).ColorAndOpacity(Color).AutoWrapText(true);};
        ChildSlot
        [
            SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(40,96,40,32)) // Top clear for match notices.
            [
                SNew(SBox).WidthOverride_Lambda([this]{const float W=GetCachedGeometry().GetLocalSize().X;return FOptionalSize(FMath::Clamp(W-80.f,560.f,1080.f));})
                [
                    SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().FillWidth(1.f)[Text(FText::FromString(TEXT("규칙 카드 투표")),26,ProjectRuleVoteUI::Ink,TEXT("Bold"))]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(STextBlock).Text(TAttribute<FText>::CreateSP(this,&SProjectRuleVotePanel::StatusText)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ColorAndOpacity(ProjectRuleVoteUI::Faint)]
                    ]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,22)
                    [Text(FText::FromString(View.MyVote>=0?TEXT("투표했습니다. 시간이 끝나기 전에는 다른 카드로 바꿀 수 있습니다."):
                        TEXT("이번 경기에 더할 규칙 하나를 고르세요. 가장 많이 받은 카드가 켜지고, 같으면 무작위입니다.")),16,ProjectRuleVoteUI::Faint)]
                    +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(240.f)[Cards]]
                ]
            ]
        ];
    }
    virtual bool SupportsKeyboardFocus()const override{return true;}
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& Key)override
    {
        const FKey K=Key.GetKey();
        const int32 Index=K==EKeys::One||K==EKeys::NumPadOne?0:K==EKeys::Two||K==EKeys::NumPadTwo?1:K==EKeys::Three||K==EKeys::NumPadThree?2:INDEX_NONE;
        if(auto* O=Owner.Get();O&&Index!=INDEX_NONE)O->VoteLocal(Index);
        return FReply::Handled(); // The game's controls wait for the vote.
    }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&)override{return FReply::Handled();}
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled)const override
    {
        const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        const FLinearColor Tint=Style.GetColorAndOpacityTint();
        FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),White,ESlateDrawEffect::None,ProjectRuleVoteUI::Paper*Tint);
        const FVector2D Size=G.GetLocalSize();
        ProjectDFSDUI::PaintTape(Out,G,Layer+1,FVector2D::ZeroVector,Size,1.f,Tint,733,.6f);
        ProjectDFSDUI::PaintVignette(Out,G,Layer+1,Size,Tint);
        return SCompoundWidget::OnPaint(Args,G,Clip,Out,Layer+2,Style,bParentEnabled);
    }
private:
    FProjectRuleVoteView View;
    TWeakObjectPtr<UProjectRuleVoteComponent> Owner;
    TAttribute<float> SecondsLeft;

    FText StatusText()const
    {
        const int32 Left=FMath::CeilToInt(FMath::Max(0.f,SecondsLeft.Get()));
        return FText::FromString(FString::Printf(TEXT("남은 시간 %d초 · 투표 %d/%d"),Left,View.Voted,View.Voters));
    }
    TSharedRef<SWidget> MakeCard(int32 Index)
    {
        const ProjectPlaygroundVote::FCard* Card=ProjectPlaygroundVote::Find(View.Cards[Index]);
        const FString Name=Card?Card->Name:View.Cards[Index],Explain=Card?Card->Explain:FString();
        const int32 Count=View.Counts.IsValidIndex(Index)?View.Counts[Index]:0;
        const bool bMine=View.MyVote==Index;
        auto Line=[](const FString& S,int32 Size,const FLinearColor& C,const TCHAR* Face=TEXT("Regular"))
        {return SNew(STextBlock).Text(FText::FromString(S)).Font(FCoreStyle::GetDefaultFontStyle(Face,Size)).ColorAndOpacity(C).AutoWrapText(true);};
        return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle(bMine)).ContentPadding(FMargin(18,16)).IsFocusable(false)
            .OnClicked_Lambda([this,Index]{if(auto* O=Owner.Get())O->VoteLocal(Index);return FReply::Handled();})
            [
                SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1.f)[Line(FString::Printf(TEXT("%d표"),Count),18,ProjectRuleVoteUI::Mark,TEXT("Bold"))]
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Line(bMine?FString(TEXT("내 표")):FString::Printf(TEXT("[%d]"),Index+1),14,ProjectRuleVoteUI::Faint)]
                ]
                +SVerticalBox::Slot().AutoHeight().Padding(0,16,0,0)[Line(Name,22,ProjectRuleVoteUI::Ink,TEXT("Bold"))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[Line(Explain,16,ProjectRuleVoteUI::Ink)]
                +SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer)]
            ];
    }
};

UProjectRuleVoteComponent::UProjectRuleVoteComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;
}

void UProjectRuleVoteComponent::ClientShowVote_Implementation(const FProjectRuleVoteView& InView)
{
    if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
    View=InView;ViewReceivedAt=FPlatformTime::Seconds();
    OpenPanel();
}

void UProjectRuleVoteComponent::ClientCloseVote_Implementation(){ClosePanel();}

bool UProjectRuleVoteComponent::ServerVote_Validate(int32 Serial,int32 Index){return Serial>=0&&Index>=0&&Index<ProjectPlaygroundRules::VoteCards;}
void UProjectRuleVoteComponent::ServerVote_Implementation(int32 Serial,int32 Index)
{
    auto* PC=Cast<AMultiplayTestPlayerController>(GetOwner());auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>():nullptr;
    if(PC&&GM)GM->VotePlaygroundRuleCard(PC,Serial,Index);
}

void UProjectRuleVoteComponent::VoteLocal(int32 Index)
{
    // Changing a vote is allowed until the vote closes; the server answers with the updated counts.
    if(!View.Cards.IsValidIndex(Index)||View.MyVote==Index)return;
    ServerVote(View.Serial,Index);
}

void UProjectRuleVoteComponent::OpenPanel()
{
    auto* PC=Cast<APlayerController>(GetOwner());if(!PC||!PC->IsLocalController())return;
    UGameViewportClient* V=GetWorld()?GetWorld()->GetGameViewport():nullptr;if(!V)return;
    TWeakObjectPtr<UProjectRuleVoteComponent> Self(this);
    if(Panel.IsValid())Panel->Rebuild(View);
    else
    {
        Panel=SNew(SProjectRuleVotePanel).View(View).Owner(Self)
            .SecondsLeft_Lambda([Self]{const auto* C=Self.Get();return C?float(C->View.SecondsLeft-(FPlatformTime::Seconds()-C->ViewReceivedAt)):0.f;});
        // Above the loading screen (same layer, added later): the vote is part of the blackout.
        V->AddViewportWidgetContent(Panel.ToSharedRef(),1000001);Viewport=V;
    }
    HoldInput(true);SetComponentTickEnabled(true);
}

void UProjectRuleVoteComponent::ClosePanel()
{
    if(auto* V=Viewport.Get();V&&Panel.IsValid())V->RemoveViewportWidgetContent(Panel.ToSharedRef());
    Panel.Reset();Viewport.Reset();HoldInput(false);SetComponentTickEnabled(false);
}

void UProjectRuleVoteComponent::HoldInput(bool bHold)
{
    auto* PC=Cast<APlayerController>(GetOwner());if(!PC||bHold==bInputHeld)return;
    bInputHeld=bHold;
    if(bHold)
    {
        bCursorWasShown=PC->bShowMouseCursor;PC->bShowMouseCursor=true;
        PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);
        FInputModeUIOnly Mode;if(Panel.IsValid())Mode.SetWidgetToFocus(Panel);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);PC->SetInputMode(Mode);
        if(Panel.IsValid()&&FSlateApplication::IsInitialized())FSlateApplication::Get().SetAllUserFocus(Panel,EFocusCause::SetDirectly);
    }
    else
    {
        PC->SetIgnoreMoveInput(false);PC->SetIgnoreLookInput(false);
        PC->bShowMouseCursor=bCursorWasShown;PC->SetInputMode(FInputModeGameOnly());
    }
}

void UProjectRuleVoteComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTick)
{
    Super::TickComponent(Delta,TickType,ThisTick);
    if(Panel.IsValid()&&FSlateApplication::IsInitialized()&&!Panel->HasAnyUserFocus())FSlateApplication::Get().SetAllUserFocus(Panel,EFocusCause::SetDirectly);
}

void UProjectRuleVoteComponent::EndPlay(const EEndPlayReason::Type Reason){ClosePanel();Super::EndPlay(Reason);}

TSharedRef<SWidget> UProjectRuleVoteComponent::MakePanelForTest(const FProjectRuleVoteView& InView)
{
    const float Seconds=InView.SecondsLeft;
    return SNew(SProjectRuleVotePanel).View(InView).SecondsLeft_Lambda([Seconds]{return Seconds;});
}

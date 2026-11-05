// File: Source/ProjectProject01/Private/ProjectRoguePick.cpp
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#include "ProjectRoguePick.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectItemRoguelikeRuntime.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

namespace ProjectRogueUI
{
    const FLinearColor ItemInk(.86f,.72f,.42f),BuffInk(.62f,.78f,.92f),Ink(.9f,.88f,.84f),Faint(.55f,.53f,.5f),Paper(.035f,.033f,.03f,1.f);
    bool PickerIsSurvivor(const FProjectRogueOffer& O,const FString& Role)
    {
        if(!Role.IsEmpty())return ProjectItemRoguelike::IsSurvivorRole(Role);
        TArray<FString> Ids=O.Cards;Ids.Append(O.Owned);
        for(const FString& Id:Ids)if(ProjectItemRoguelike::IsCardFor(Id,true))return true;
        return false;
    }
    FString KindLabel(const FString& Id,bool bSurvivor)
    {
        if(ProjectItemRoguelike::FindItem(Id))return TEXT("내 아이템");
        return bSurvivor?TEXT("마네킹에게 주는 버프"):TEXT("생존자에게 주는 버프");
    }
    FString FloorTitle(int32 Floor){return Floor==0?FString(TEXT("경기 시작")):FString::Printf(TEXT("%d번째 층"),Floor+1);}
    FString OwnedLine(const TArray<FString>& Owned)
    {
        if(Owned.IsEmpty())return TEXT("아직 고른 카드가 없습니다.");
        TArray<FString> Names;
        for(const FString& Id:Owned){FString N,E,D,G;bool bBuff=false;if(ProjectItemRoguelike::Describe(Id,N,E,D,bBuff,G))Names.Add(bBuff?FString::Printf(TEXT("%s(버프)"),*N):N);}
        return FString(TEXT("지금까지 고른 카드: "))+FString::Join(Names,TEXT(", "));
    }
}

/** Full-screen pick panel: a dark sheet with fine static ruled lines, three cards and one reroll. */
class SProjectRoguePickPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectRoguePickPanel){}
        SLATE_ARGUMENT(FProjectRogueOffer,Offer)
        SLATE_ARGUMENT(FString,Role)
        SLATE_ARGUMENT(TWeakObjectPtr<UProjectRoguePickComponent>,Owner)
        SLATE_ATTRIBUTE(float,SecondsLeft)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Owner=Args._Owner;SecondsLeft=Args._SecondsLeft;Role=Args._Role;Rebuild(Args._Offer);
    }
    void Rebuild(const FProjectRogueOffer& InOffer)
    {
        Offer=InOffer;const bool bSurvivor=ProjectRogueUI::PickerIsSurvivor(Offer,Role);
        TSharedRef<SHorizontalBox> Cards=SNew(SHorizontalBox);
        for(int32 I=0;I<Offer.Cards.Num();++I)Cards->AddSlot().FillWidth(1.f).Padding(10,0)[MakeCard(I,bSurvivor)];
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
                        +SHorizontalBox::Slot().FillWidth(1.f)[Text(FText::FromString(FString::Printf(TEXT("카드 선택 · %s"),*ProjectRogueUI::FloorTitle(Offer.Floor))),26,ProjectRogueUI::Ink,TEXT("Bold"))]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [Text(TAttribute<FText>::CreateSP(this,&SProjectRoguePickPanel::StatusText),16,ProjectRogueUI::Faint)]
                    ]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,22)
                    [Text(FText::FromString(Offer.bPicked?TEXT("선택을 마쳤습니다. 다른 플레이어가 고르면 경기가 이어집니다."):
                        TEXT("내 아이템을 고르거나 상대 진영에 줄 버프를 고르세요. 고른 효과는 경기가 끝날 때까지 이어집니다.")),16,ProjectRogueUI::Faint)]
                    +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(300.f)[Cards]]
                    +SVerticalBox::Slot().AutoHeight().Padding(10,22,10,0)
                    [
                        SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Text(FText::FromString(ProjectRogueUI::OwnedLine(Offer.Owned)),14,ProjectRogueUI::Faint)]
                        +SHorizontalBox::Slot().AutoWidth().Padding(16,0,0,0)
                        [
                            SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ContentPadding(FMargin(18,8))
                            .IsEnabled(!Offer.bRerollUsed&&!Offer.bPicked).IsFocusable(false)
                            .OnClicked_Lambda([this]{if(auto* O=Owner.Get())O->RerollLocal();return FReply::Handled();})
                            [Text(FText::FromString(Offer.bRerollUsed?TEXT("새로고침 사용함"):TEXT("새로고침 (R) · 1회")),15,ProjectDFSDUI::Text())]
                        ]
                    ]
                ]
            ]
        ];
    }
    virtual bool SupportsKeyboardFocus()const override{return true;}
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& Key)override
    {
        const FKey K=Key.GetKey();auto* O=Owner.Get();
        const int32 Index=K==EKeys::One||K==EKeys::NumPadOne?0:K==EKeys::Two||K==EKeys::NumPadTwo?1:K==EKeys::Three||K==EKeys::NumPadThree?2:INDEX_NONE;
        if(O&&Index!=INDEX_NONE)O->ChooseLocal(Index);
        else if(O&&K==EKeys::R)O->RerollLocal();
        return FReply::Handled(); // Every other key is swallowed: the game's controls wait for the pick.
    }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&)override{return FReply::Handled();}
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled)const override
    {
        // The blackout itself as a paused tape frame: fixed scanlines/grain, darker edges and a
        // REC line in the bottom padding band, clear of the cards and the top notice area (static).
        const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        const FLinearColor Tint=Style.GetColorAndOpacityTint();
        FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),White,ESlateDrawEffect::None,ProjectRogueUI::Paper*Tint);
        const FVector2D Size=G.GetLocalSize();
        ProjectDFSDUI::PaintTape(Out,G,Layer+1,FVector2D::ZeroVector,Size,1.f,Tint,733,.6f); // near-black blackout: softer rules
        ProjectDFSDUI::PaintVignette(Out,G,Layer+1,Size,Tint);
        ProjectDFSDUI::PaintRecordMarks(Out,G,Layer+1,40.f,float(Size.X)-40.f,float(Size.Y)-24.f,1.f,Tint,7);
        return SCompoundWidget::OnPaint(Args,G,Clip,Out,Layer+2,Style,bParentEnabled);
    }
private:
    FProjectRogueOffer Offer;FString Role;
    TWeakObjectPtr<UProjectRoguePickComponent> Owner;
    TAttribute<float> SecondsLeft;

    FText StatusText()const
    {
        const int32 Left=FMath::CeilToInt(FMath::Max(0.f,SecondsLeft.Get()));
        return FText::FromString(Offer.bPicked?FString::Printf(TEXT("기다리는 중 %d명 · %d초"),Offer.Waiting,Left):
            FString::Printf(TEXT("남은 시간 %d초 · 지나면 무작위로 고릅니다"),Left));
    }
    TSharedRef<SWidget> MakeCard(int32 Index,bool bSurvivor)
    {
        const FString Id=Offer.Cards[Index];FString Name,Effect,Detail,Glyph;bool bBuff=false;
        ProjectItemRoguelike::Describe(Id,Name,Effect,Detail,bBuff,Glyph);
        const FLinearColor Tone=bBuff?ProjectRogueUI::BuffInk:ProjectRogueUI::ItemInk;
        const bool bChosen=Offer.bPicked&&Offer.PickedId==Id;
        auto Line=[](const FString& S,int32 Size,const FLinearColor& C,const TCHAR* Face=TEXT("Regular"))
        {return SNew(STextBlock).Text(FText::FromString(S)).Font(FCoreStyle::GetDefaultFontStyle(Face,Size)).ColorAndOpacity(C).AutoWrapText(true);};
        return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle(bChosen)).ContentPadding(FMargin(18,16)).IsFocusable(false)
            .IsEnabled(!Offer.bPicked||bChosen)
            .OnClicked_Lambda([this,Index]{if(auto* O=Owner.Get())O->ChooseLocal(Index);return FReply::Handled();})
            [
                SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(Tone*FLinearColor(1,1,1,.85f)).Padding(FMargin(10,4))
                        [SNew(STextBlock).Text(FText::FromString(Glyph)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18)).ColorAndOpacity(FLinearColor(.05f,.04f,.03f))]
                    ]
                    +SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(12,0,0,0)[Line(ProjectRogueUI::KindLabel(Id,bSurvivor),14,Tone,TEXT("Bold"))]
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Line(bChosen?FString(TEXT("선택함")):FString::Printf(TEXT("[%d]"),Index+1),14,ProjectRogueUI::Faint)]
                ]
                +SVerticalBox::Slot().AutoHeight().Padding(0,16,0,0)[Line(Name,22,ProjectRogueUI::Ink,TEXT("Bold"))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[Line(Effect,16,ProjectRogueUI::Ink)]
                +SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer)]
                +SVerticalBox::Slot().AutoHeight()[Line(Detail,13,ProjectRogueUI::Faint)]
            ];
    }
};

UProjectRoguePickComponent::UProjectRoguePickComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;
}

FString UProjectRoguePickComponent::LocalRole()const
{
    if(const auto* PC=Cast<AMultiplayTestPlayerController>(GetOwner()))return PC->IsLocalMannequinPlayer()?TEXT("Mannequin"):FString();
    return FString();
}

void UProjectRoguePickComponent::ClientShowOffer_Implementation(const FProjectRogueOffer& InOffer)
{
    if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
    const bool bNewSerial=InOffer.Serial!=Offer.Serial||!Panel.IsValid();
    Offer=InOffer;OfferReceivedAt=FPlatformTime::Seconds();
    if(bNewSerial)bRequestSent=false;
    OpenPanel();
}

void UProjectRoguePickComponent::ClientClosePick_Implementation(){ClosePanel();}

bool UProjectRoguePickComponent::ServerPickCard_Validate(int32 Serial,int32 Index){return Serial>=0&&Index>=0&&Index<ProjectItemRoguelike::OfferSize;}
void UProjectRoguePickComponent::ServerPickCard_Implementation(int32 Serial,int32 Index)
{
    auto* PC=Cast<AMultiplayTestPlayerController>(GetOwner());auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>():nullptr;
    if(PC&&GM&&GM->GetItemRoguelike())GM->GetItemRoguelike()->Pick(GM,PC,Serial,Index);
}
bool UProjectRoguePickComponent::ServerRerollCards_Validate(int32 Serial){return Serial>=0;}
void UProjectRoguePickComponent::ServerRerollCards_Implementation(int32 Serial)
{
    auto* PC=Cast<AMultiplayTestPlayerController>(GetOwner());auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>():nullptr;
    if(PC&&GM&&GM->GetItemRoguelike())GM->GetItemRoguelike()->Reroll(GM,PC,Serial);
}

void UProjectRoguePickComponent::ChooseLocal(int32 Index)
{
    // One request per offer; the server answers with the updated offer (or closes the pick).
    if(bRequestSent||Offer.bPicked||!Offer.Cards.IsValidIndex(Index))return;
    bRequestSent=true;ServerPickCard(Offer.Serial,Index);
}
void UProjectRoguePickComponent::RerollLocal()
{
    if(bRequestSent||Offer.bPicked||Offer.bRerollUsed)return;
    bRequestSent=true;ServerRerollCards(Offer.Serial);
}

void UProjectRoguePickComponent::OpenPanel()
{
    auto* PC=Cast<APlayerController>(GetOwner());if(!PC||!PC->IsLocalController())return;
    UGameViewportClient* V=GetWorld()?GetWorld()->GetGameViewport():nullptr;if(!V)return;
    TWeakObjectPtr<UProjectRoguePickComponent> Self(this);
    if(Panel.IsValid())Panel->Rebuild(Offer);
    else
    {
        Panel=SNew(SProjectRoguePickPanel).Offer(Offer).Role(LocalRole()).Owner(Self)
            .SecondsLeft_Lambda([Self]{const auto* C=Self.Get();return C?float(C->Offer.SecondsLeft-(FPlatformTime::Seconds()-C->OfferReceivedAt)):0.f;});
        // Above the loading screen (same layer, added later): the pick is the blackout until everyone has chosen.
        V->AddViewportWidgetContent(Panel.ToSharedRef(),1000001);Viewport=V;
    }
    HoldInput(true);SetComponentTickEnabled(true);
}

void UProjectRoguePickComponent::ClosePanel()
{
    if(auto* V=Viewport.Get();V&&Panel.IsValid())V->RemoveViewportWidgetContent(Panel.ToSharedRef());
    Panel.Reset();Viewport.Reset();HoldInput(false);SetComponentTickEnabled(false);bRequestSent=false;
}

void UProjectRoguePickComponent::HoldInput(bool bHold)
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

void UProjectRoguePickComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTick)
{
    Super::TickComponent(Delta,TickType,ThisTick);
    // Keep the keys with the panel even if something else took focus (a viewport click, a window switch).
    if(Panel.IsValid()&&FSlateApplication::IsInitialized()&&!Panel->HasAnyUserFocus())FSlateApplication::Get().SetAllUserFocus(Panel,EFocusCause::SetDirectly);
}

void UProjectRoguePickComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ClosePanel();Super::EndPlay(Reason);
}

TSharedRef<SWidget> UProjectRoguePickComponent::MakePanelForTest(const FProjectRogueOffer& InOffer,const FString& Role)
{
    const float Seconds=InOffer.SecondsLeft;
    return SNew(SProjectRoguePickPanel).Offer(InOffer).Role(Role).SecondsLeft_Lambda([Seconds]{return Seconds;});
}

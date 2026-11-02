#include "ProjectPhoneMessenger.h"
#include "ProjectMiniGameWidgets.h"
#include "ProjectMiniGames.h"
#include "Framework/Application/SlateApplication.h"
#include "ProjectCaseRecords.h"
#include "ProjectDFSDUITheme.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    FString JStr(const TSharedPtr<FJsonObject>& J,const TCHAR* K){FString V;if(J)J->TryGetStringField(K,V);return V;}
    bool JFlag(const TSharedPtr<FJsonObject>& J,const TCHAR* K){bool V=false;if(J)J->TryGetBoolField(K,V);return V;}
    TArray<TSharedPtr<FJsonValue>> JRows(const TSharedPtr<FJsonObject>& J,const TCHAR* K){const TArray<TSharedPtr<FJsonValue>>* V=nullptr;return J&&J->TryGetArrayField(K,V)?*V:TArray<TSharedPtr<FJsonValue>>{};}
    TSharedPtr<FJsonObject> JObj(const TSharedPtr<FJsonObject>& J,const TCHAR* K){const TSharedPtr<FJsonObject>* V=nullptr;return J&&J->TryGetObjectField(K,V)?*V:nullptr;}
    FSlateFontInfo Font(int32 Size,const TCHAR* Weight=TEXT("Regular")){return FCoreStyle::GetDefaultFontStyle(Weight,Size);}
}

bool FProjectTalkScript::Parse(const TSharedPtr<FJsonObject>& Step,FProjectTalkScript& Out)
{
    const auto Talk=JObj(Step,TEXT("talk"));if(!Talk)return false;
    Out=FProjectTalkScript();Out.StepId=JStr(Step,TEXT("id"));Out.Npc=JStr(Talk,TEXT("npc"));Out.bTalked=JFlag(Step,TEXT("talked"));Out.bActive=JFlag(Step,TEXT("active"));
    for(const auto& V:JRows(Step,TEXT("choices"))){double N=0;if(V.IsValid()&&V->TryGetNumber(N))Out.SavedChoices.Add(int32(N));}
    for(const auto& V:JRows(Talk,TEXT("lines")))
    {
        const auto L=V.IsValid()?V->AsObject():nullptr;if(!L)continue;FLine Line;Line.Text=JStr(L,TEXT("text"));
        for(const auto& O:JRows(L,TEXT("options")))
        {
            const auto Option=O.IsValid()?O->AsObject():nullptr;if(!Option)continue;FOption Item;Item.Text=JStr(Option,TEXT("text"));
            for(const auto& R:JRows(Option,TEXT("replies"))){FString Reply;if(R.IsValid()&&R->TryGetString(Reply)&&!Reply.IsEmpty())Item.Replies.Add(Reply);}
            if(!Item.Text.IsEmpty())Line.Options.Add(MoveTemp(Item));
        }
        if(Line.IsChoice()||!Line.Text.IsEmpty())Out.Lines.Add(MoveTemp(Line));
    }
    return !Out.StepId.IsEmpty()&&!Out.Npc.IsEmpty()&&Out.Lines.Num()>0;
}
TArray<FProjectTalkScript> FProjectTalkScript::FromSnapshot(const TSharedPtr<FJsonObject>& Snapshot)
{
    TArray<FProjectTalkScript> Result;
    for(const auto& V:JRows(JObj(Snapshot,TEXT("mainQuest")),TEXT("steps"))){FProjectTalkScript Script;if(V.IsValid()&&Parse(V->AsObject(),Script))Result.Add(MoveTemp(Script));}
    return Result;
}

void FProjectTalkPlayback::Start(const FProjectTalkScript& InScript){Script=InScript;Shown.Reset();Replies.Reset();Picked.Reset();Cursor=0;bDone=false;Wait=0;Schedule();}
bool FProjectTalkPlayback::NextMessage(FString& Out)const
{
    if(Replies.Num()>0){Out=Replies[0];return true;}
    if(Script.Lines.IsValidIndex(Cursor)&&!Script.Lines[Cursor].IsChoice()){Out=Script.Lines[Cursor].Text;return true;}
    return false;
}
bool FProjectTalkPlayback::HasPendingMessage()const{FString Unused;return NextMessage(Unused);}
void FProjectTalkPlayback::Schedule()
{
    FString Next;
    if(NextMessage(Next))Wait=TypingSeconds(Next);
    else if(!Script.Lines.IsValidIndex(Cursor))bDone=true;
    else Wait=0; // A choice waits for the player.
}
const TArray<FProjectTalkScript::FOption>* FProjectTalkPlayback::Choices()const
{
    return !bDone&&Replies.Num()==0&&Script.Lines.IsValidIndex(Cursor)&&Script.Lines[Cursor].IsChoice()?&Script.Lines[Cursor].Options:nullptr;
}
bool FProjectTalkPlayback::Tick(double Seconds)
{
    if(bDone||Choices())return false;
    Wait-=FMath::Max(0.,Seconds);if(Wait>0)return false;
    FString Message;if(!NextMessage(Message)){Schedule();return false;}
    Shown.Add({Message,false});
    if(Replies.Num()>0)Replies.RemoveAt(0);else ++Cursor;
    Schedule();return true;
}
bool FProjectTalkPlayback::Choose(int32 Option)
{
    const auto* Options=Choices();if(!Options||!Options->IsValidIndex(Option))return false;
    const auto Picked_=(*Options)[Option];Shown.Add({Picked_.Text,true});Replies=Picked_.Replies;Picked.Add(Option);++Cursor;Schedule();return true;
}
void FProjectTalkPlayback::Replay(const FProjectTalkScript& InScript,const TArray<int32>& Saved)
{
    Start(InScript);
    for(int32 Guard=0;!bDone&&Guard<1000;++Guard)
    {
        if(const auto* Options=Choices()){const int32 Pick=Saved.IsValidIndex(Picked.Num())?FMath::Clamp(Saved[Picked.Num()],0,Options->Num()-1):0;Choose(Pick);}
        else{Wait=0;if(!Tick(0)&&!HasPendingMessage()&&!Choices())break;}
    }
}

/**
 * Wear drawn over the whole phone (372x720 with a 10px body around the screen): rust patches eating into the body,
 * and a crack spreading from a chip in the upper-right of the glass. It does not change over time.
 */
class SProjectPhoneWear final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectPhoneWear){}SLATE_END_ARGS()
    void Construct(const FArguments&){SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(1,1);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool)const override
    {
        const FVector2D S=G.GetLocalSize();const FLinearColor Tint=Style.GetColorAndOpacityTint();
        static const FSlateRoundedBoxBrush Patch(FLinearColor::White,4.f);
        static const FSlateRoundedBoxBrush Blob(FLinearColor::White,FVector4(12.f,12.f,12.f,12.f));
        // Rust stains: clusters of soft, overlapping blobs where the body wears through (corners and an edge), in the
        // phone's 372x720 layout units. Centre x, centre y, diameter, then which of the three rust tones and its strength.
        struct FRust{float X,Y,D;int32 Tone;float A;};
        static const FLinearColor Tones[]={{.30f,.14f,.05f,1.f},{.52f,.27f,.10f,1.f},{.19f,.09f,.04f,1.f}};
        static const FRust Rust[]={
            {14,22,22,0,.55f},{8,34,14,1,.5f},{24,12,12,2,.6f},{6,52,9,0,.45f},{30,7,8,1,.4f},              // top-left corner
            {360,690,24,0,.55f},{350,706,15,1,.5f},{366,672,11,2,.55f},{340,712,9,0,.45f},{368,650,7,1,.35f}, // bottom-right corner
            {4,404,10,1,.45f},{5,418,16,0,.5f},{4,436,8,2,.5f},                                              // left edge
            {368,210,9,0,.4f},{366,222,6,1,.4f},                                                             // right edge
            {120,716,10,0,.4f},{132,714,7,1,.35f}};                                                          // bottom edge
        const FVector2D Unit(S.X/372.,S.Y/720.);
        for(const auto& R:Rust)
        {
            const float Grain=FMath::Min(Unit.X,Unit.Y); // the same blob size upright or sideways
            const FVector2D Size(R.D*Grain,R.D*Grain);
            FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(Size,FSlateLayoutTransform(FVector2D(R.X*Unit.X,R.Y*Unit.Y)-Size*.5)),&Blob,ESlateDrawEffect::None,Tones[R.Tone].CopyWithNewOpacity(R.A)*Tint);
        }
        // Cracked glass, kept in the status bar between the clock and the notch: the one strip that is empty on every
        // screen, so it never lies over messages, names or buttons. Layout units like the rust.
        const FVector2D Hit(112,22);
        const TArray<TArray<FVector2D>> Cracks={{Hit,{100,28},{88,30},{76,35}},{Hit,{106,14},{98,11}},{Hit,{124,16},{134,13}},{Hit,{120,30},{128,35}},{{100,28},{96,35}}};
        for(const auto& Crack:Cracks)
        {
            TArray<FVector2D> Line;for(const auto& P:Crack)Line.Add(P*Unit);
            FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Line,ESlateDrawEffect::None,FLinearColor(1,1,1,.24f)*Tint,true,1.f);
        }
        FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(7,7)*Unit.X,FSlateLayoutTransform(Hit*Unit-FVector2D(3.5,3.5)*Unit.X)),&Patch,ESlateDrawEffect::None,FLinearColor(1,1,1,.22f)*Tint);
        return Layer+1;
    }
};

/** The phone itself: slides up over the screen, contact list ↔ chat, messenger-style bubbles and answer buttons. */
class SProjectPhone final:public SCompoundWidget
{
    friend class FProjectPhoneRenderTest;
public:
    SLATE_BEGIN_ARGS(SProjectPhone){}
        SLATE_ARGUMENT(TWeakObjectPtr<UGameInstance>,GameInstance)
        SLATE_ARGUMENT(FString,Step)
        SLATE_EVENT(FSimpleDelegate,OnClose)
        SLATE_ARGUMENT(TSharedPtr<FJsonObject>,Snapshot) // Used when there is no game instance (render test).
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Game=Args._GameInstance;OnClose=Args._OnClose;FixedSnapshot=Args._Snapshot;
        // The phone opens the way it was left, unless a match or a sign-out has put it back upright since.
        if(auto* Games=MiniGames())bLandscape=Games->bPhoneLandscape;
        // An old phone found in the store: dull, rust-stained body, cracked glass, no service.
        const FLinearColor Ink(.035f,.04f,.045f,1.f),Body(.11f,.095f,.08f,1.f),Screen=ProjectDFSDUI::Canvas(),Npc=ProjectDFSDUI::Panel().CopyWithNewOpacity(1.f),Player=ProjectDFSDUI::Accent();
        FrameBrush=FSlateRoundedBoxBrush(Body,34.f,FLinearColor(.34f,.2f,.11f,1.f),2.f);ScreenBrush=FSlateRoundedBoxBrush(Screen,26.f);
        NpcBrush=FSlateRoundedBoxBrush(Npc,14.f,FLinearColor(1,1,1,.06f),1.f);PlayerBrush=FSlateRoundedBoxBrush(Player,14.f);
        AvatarBrush=FSlateRoundedBoxBrush(ProjectDFSDUI::Muted().CopyWithNewOpacity(.55f),20.f);DotBrush=FSlateRoundedBoxBrush(FLinearColor(.86f,.16f,.14f,1.f),5.f);
        NotchBrush=FSlateRoundedBoxBrush(Ink,8.f);RowBrush=FSlateRoundedBoxBrush(FLinearColor(1,1,1,.04f),12.f);
        ChildSlot
        [
            SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            // Tapping outside tucks the phone small into the bottom-right corner; the dimming goes and the screen behind
            // is usable again until the small phone is tapped.
            +SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor_Lambda([this]{return FLinearColor(0,0,0,.55f*Ease()*(1-MinimizeEase()));})
                .Visibility_Lambda([this]{return bMinimized?EVisibility::Collapsed:EVisibility::Visible;})
                .OnMouseButtonDown_Lambda([this](const FGeometry&,const FPointerEvent&){SetMinimized(true);return FReply::Handled();})]
            // Laid out in the middle in its final orientation; the transform turns, scales and shifts it there, so the
            // portrait phone keeps its place on the right and a turn looks like the phone being rotated in the hand.
            +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride_Lambda([this]{return FOptionalSize(bLandscape?LandscapeSize().X:PortraitSize.X);}).HeightOverride_Lambda([this]{return FOptionalSize(bLandscape?LandscapeSize().Y:PortraitSize.Y);})
                    .RenderTransformPivot(FVector2D(.5f,.5f)).RenderTransform_Lambda([this]{return TOptional<FSlateRenderTransform>(PhoneTransform());})
                [
                    SNew(SOverlay)
                    +SOverlay::Slot()
                    [
                        SNew(SBorder).BorderImage(&FrameBrush).Padding(10).OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})
                        [
                            SNew(SBorder).BorderImage(&ScreenBrush).Padding(0)
                            [
                                SNew(SVerticalBox)
                                +SVerticalBox::Slot().AutoHeight()[StatusBar()]
                                +SVerticalBox::Slot().FillHeight(1)[SAssignNew(ScreenBox,SBox)]
                            ]
                        ]
                    ]
                    +SOverlay::Slot()[SNew(SProjectPhoneWear)] // Rust and cracks; clicks pass through.
                    // While tucked away the whole small phone is one button that brings it back.
                    +SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("NoBorder")))
                        .Visibility_Lambda([this]{return bMinimized?EVisibility::Visible:EVisibility::Collapsed;}).Cursor(EMouseCursor::Hand)
                        .OnMouseButtonDown_Lambda([this](const FGeometry&,const FPointerEvent&){SetMinimized(false);return FReply::Handled();})]
                ]
            ]
        ];
        if(auto* R=Records())Changed=R->OnChanged.AddSP(this,&SProjectPhone::OnRecordsChanged);
        if(Args._Step.IsEmpty()||!OpenChat(Args._Step))ShowContacts();
    }
    virtual ~SProjectPhone() override{if(auto* R=Records())R->OnChanged.Remove(Changed);}
    void RequestClose(){if(!bClosing){bClosing=true;OpenTime=FMath::Min(OpenTime,SlideSeconds);}}
    virtual void Tick(const FGeometry& G,const double Time,const float Delta) override
    {
        SCompoundWidget::Tick(G,Time,Delta);const float Step=FMath::Clamp(Delta,0.f,.1f);
        OpenTime=bClosing?OpenTime-Step:FMath::Min(SlideSeconds,OpenTime+Step);
        ViewSize=G.GetLocalSize();TurnTime=FMath::Min(TurnSeconds,TurnTime+Step);
        MinimizeTime=bMinimized?FMath::Min(MinimizeSeconds,MinimizeTime+Step):FMath::Max(0.f,MinimizeTime-Step);
        if(bClosing&&OpenTime<=0){bClosing=false;OnClose.ExecuteIfBound();return;}
        if(bBackToList){bBackToList=false;bInGames=false;ShowContacts();}
        if(bOpenGames){bOpenGames=false;bInChat=false;bInGames=true;ScreenBox->SetContent(ProjectMiniGameUI::MakeFolder(Game,FSimpleDelegate::CreateLambda([this]{bBackToList=true;}),FSimpleDelegate::CreateLambda([this]{SetLandscape(true);})));}
        if(!PendingChat.IsEmpty()){const FString Step_=MoveTemp(PendingChat);PendingChat.Reset();OpenChat(Step_);}
        if(!bInChat)return;
        if(bRestart){bRestart=false;Script.bTalked=false;Playback.Start(Script);ResetMessages();}
        if(Playback.Tick(Step)){AddBubble(Playback.Bubbles().Last());bChoicesDirty=true;}
        if(Playback.IsFinished()&&!bReported)
        {
            bReported=true;bChoicesDirty=true;
            if(auto* R=Records())R->MainQuestTalk(Script.StepId,Playback.Chosen()); // Saves the choices; finishes the quest step if this is the current one.
        }
        if(bChoicesDirty){bChoicesDirty=false;RebuildChoices();}
    }
private:
    UProjectCaseRecords* Records()const{auto* GI=Game.Get();return GI?GI->GetSubsystem<UProjectCaseRecords>():nullptr;}
    UProjectMiniGames* MiniGames()const{auto* GI=Game.Get();return GI?GI->GetSubsystem<UProjectMiniGames>():nullptr;}
    float Ease()const{const float T=FMath::Clamp(OpenTime/SlideSeconds,0.f,1.f);return 1.f-FMath::Pow(1.f-T,3.f);}
    // ---- orientation: upright (messages) or sideways and larger (games) ----
    inline static const FVector2D PortraitSize{372,720};
    FVector2D LandscapeSize()const{return FVector2D(FMath::Clamp(ViewSize.X-80,PortraitSize.X,1120.),FMath::Clamp(ViewSize.Y-56,PortraitSize.X,620.));}
    /** Where the upright phone sits, from the centre: on the right with a 96 margin, as it always was. */
    float PortraitShift()const{return FMath::Max(0.f,float(ViewSize.X*.5-96-PortraitSize.X*.5));}
    float TurnEase()const{const float T=FMath::Clamp(TurnTime/TurnSeconds,0.f,1.f);return T<.5f?4*T*T*T:1-FMath::Pow(-2*T+2,3.f)/2;}
    FSlateRenderTransform PhoneTransform()const
    {
        // Turning: the new layout starts drawn at the old orientation and size (rotated a quarter turn, scaled to the old
        // outline) and eases into place; to landscape it grows, back to portrait it shrinks.
        const float E=TurnEase();const FVector2D L=LandscapeSize();
        const float Start=bLandscape?FMath::Min(PortraitSize.X/L.Y,PortraitSize.Y/L.X):FMath::Min(L.X/PortraitSize.Y,L.Y/PortraitSize.X);
        const float Angle=(1-E)*(bLandscape?HALF_PI:-HALF_PI),Scale=FMath::Lerp(Start,1.f,E);
        const float Shift=bLandscape?FMath::Lerp(PortraitShift(),0.f,E):FMath::Lerp(0.f,PortraitShift(),E);
        FVector2f Move(Shift,(1.f-Ease())*760.f);float Size=Scale;
        // Tucked away: shrunk to a third and slid into the bottom-right corner, 24 from the edges.
        if(const float M=MinimizeEase();M>0)
        {
            const FVector2D Box=bLandscape?LandscapeSize():PortraitSize;
            const FVector2f Corner(float(ViewSize.X*.5-24-Box.X*MinimizedScale*.5),float(ViewSize.Y*.5-24-Box.Y*MinimizedScale*.5));
            Size=FMath::Lerp(Scale,MinimizedScale,M);Move=FMath::Lerp(Move,Corner,M);
        }
        return FSlateRenderTransform(::Concatenate(FScale2f(Size),FQuat2f(Angle)),Move);
    }
    static constexpr float MinimizedScale=.3f,MinimizeSeconds=.3f;
    float MinimizeEase()const{const float T=FMath::Clamp(MinimizeTime/MinimizeSeconds,0.f,1.f);return T*T*(3-2*T);}
    void SetMinimized(bool bOn)
    {
        if(bOn==bMinimized)return;
        bMinimized=bOn;
        if(!FSlateApplication::IsInitialized())return;
        // Small: the keyboard goes back to the screen behind (a game in the phone pauses as it loses focus). Back: the
        // phone takes it again.
        if(bOn)FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);else if(Refocus)Refocus();
    }
    void SetLandscape(bool bOn)
    {
        if(bOn==bLandscape)return;
        bLandscape=bOn;TurnTime=0;if(auto* Games=MiniGames())Games->bPhoneLandscape=bOn;
    }
    TSharedRef<SWidget> StatusBar()
    {
        return SNew(SBox).HeightOverride(34).Padding(FMargin(16,6,14,0))[SNew(SOverlay)
            // The clock lost its time once in a while; the store has no signal at all.
            +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center)[SNew(STextBlock).Font(Font(11,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([]{return FText::FromString(FMath::Fmod(FPlatformTime::Seconds(),23.0)<.6?TEXT("--:--"):FDateTime::Now().ToString(TEXT("%H:%M")));})]
            +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top)[SNew(SBox).WidthOverride(64).HeightOverride(18)[SNew(SBorder).BorderImage(&NotchBrush)]]
            +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center)[SNew(SHorizontalBox)
                // Turn the phone: sideways and larger, or back upright. Only a match or a sign-out turns it back by itself.
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)[SNew(SButton).IsFocusable(false).ButtonStyle(&FCoreStyle::Get(),TEXT("NoBorder")).ContentPadding(FMargin(2,0))
                    .OnClicked_Lambda([this]{SetLandscape(!bLandscape);return FReply::Handled();})
                    .ToolTipText_Lambda([this]{return FText::FromString(bLandscape?TEXT("세로로 작게 되돌리기"):TEXT("가로로 크게 보기"));})
                    // Two letters: the upright status bar has room for no more beside the notch.
                    [SNew(STextBlock).Font(Font(10,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Accent()).Text_Lambda([this]{return FText::FromString(bLandscape?TEXT("세로"):TEXT("가로"));})]]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)[SNew(STextBlock).Font(Font(10)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text(FText::FromString(TEXT("서비스 없음")))]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Font(Font(10,TEXT("Bold"))).ColorAndOpacity(FLinearColor(.86f,.3f,.2f,1.f)).Text(FText::FromString(TEXT("12%")))]]];
    }
    TSharedRef<SWidget> Avatar(const FString& Name,float Size)
    {
        return SNew(SBox).WidthOverride(Size).HeightOverride(Size)[SNew(SBorder).BorderImage(&AvatarBrush).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(STextBlock).Font(Font(int32(Size*.42f),TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(Name.Left(1)))]];
    }
    TSharedRef<SWidget> Dot(){return SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SBorder).BorderImage(&DotBrush)];}
    void OnRecordsChanged(){if(!bInChat&&!bInGames)ShowContacts();}
    void ShowContacts()
    {
        bInChat=false;auto* R=Records();const auto Talks=FProjectTalkScript::FromSnapshot(R?R->Snapshot:FixedSnapshot);
        auto List=SNew(SVerticalBox);
        for(const auto& Talk:Talks)
        {
            const FString Step=Talk.StepId;FProjectTalkPlayback Preview;Preview.Replay(Talk,Talk.SavedChoices);
            const FString Last=!Talk.bTalked?TEXT("새 메시지가 있습니다."):Preview.Bubbles().Num()?Preview.Bubbles().Last().Text:FString();
            List->AddSlot().AutoHeight().Padding(10,3)[SNew(SButton).ButtonStyle(&FCoreStyle::Get(),TEXT("NoBorder")).OnClicked_Lambda([this,Step]{PendingChat=Step;return FReply::Handled();})
                [SNew(SBorder).BorderImage(&RowBrush).Padding(FMargin(12,10))[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)[Avatar(Talk.Npc,44)]
                    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(14,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(Talk.Npc))]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,2,0,0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(ProjectDFSDUI::Muted()).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text(FText::FromString(Last))]]
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Talk.bTalked?SNullWidget::NullWidget:Dot()]]]];
        }
        if(Talks.IsEmpty())List->AddSlot().AutoHeight().Padding(24,30)[SNew(STextBlock).AutoWrapText(true).Justification(ETextJustify::Center).Font(Font(12)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text(FText::FromString(TEXT("아직 연락할 수 있는 사람이 없습니다.")))];
        ScreenBox->SetContent(SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(20,12,12,10)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Font(Font(20,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(TEXT("메시지")))]
                // The offline games folder (no signal needed).
                +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle(true)).ContentPadding(FMargin(10,4)).OnClicked_Lambda([this]{bOpenGames=true;return FReply::Handled();})
                    [SNew(STextBlock).Font(Font(11,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(TEXT("오프라인 게임")))]]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ContentPadding(FMargin(10,4)).OnClicked_Lambda([this]{RequestClose();return FReply::Handled();})
                    [SNew(STextBlock).Font(Font(11)).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(TEXT("닫기")))]]]
            +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[List]]);
    }
    bool OpenChat(const FString& Step)
    {
        auto* R=Records();for(const auto& Talk:FProjectTalkScript::FromSnapshot(R?R->Snapshot:FixedSnapshot))if(Talk.StepId==Step)
        {
            Script=Talk;bInChat=true;bReported=Talk.bTalked;
            ScreenBox->SetContent(SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(10,6,14,8)[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SButton).ButtonStyle(&FCoreStyle::Get(),TEXT("NoBorder")).ContentPadding(FMargin(8,4)).OnClicked_Lambda([this]{bBackToList=true;return FReply::Handled();})
                        [SNew(STextBlock).Font(Font(18)).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(TEXT("<")))]]
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0,10,0)[Avatar(Talk.Npc,32)]
                    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(14,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(Talk.Npc))]
                        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(10)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]{return FText::FromString(Playback.IsTyping()?TEXT("입력 중..."):TEXT("알 수 없는 연결"));})]]]
                +SVerticalBox::Slot().FillHeight(1)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("NoBorder"))).Padding(0)
                    .OnMouseButtonDown_Lambda([this](const FGeometry&,const FPointerEvent&){Playback.Hurry();return FReply::Unhandled();}) // Click the chat to skip the typing pause.
                    [SAssignNew(Messages,SScrollBox)]]
                +SVerticalBox::Slot().AutoHeight()[SAssignNew(Answers,SVerticalBox)]);
            if(Talk.bTalked)Playback.Replay(Talk,Talk.SavedChoices);else Playback.Start(Talk);
            ResetMessages();return true;
        }
        return false;
    }
    /** Empties the chat, then shows the bubbles so far and the typing dots, which always stay last. */
    void ResetMessages()
    {
        if(!Messages)return;
        Messages->ClearChildren();BubbleRows=0;
        Messages->AddSlot().Padding(14,4)[SNew(SBox).Visibility_Lambda([this]{return Playback.IsTyping()?EVisibility::Visible:EVisibility::Collapsed;})
            [SNew(STextBlock).Font(Font(16,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([]{static const TCHAR* Frames[]={TEXT("·"),TEXT("· ·"),TEXT("· · ·")};return FText::FromString(Frames[int32(FPlatformTime::Seconds()*3)%3]);})]];
        for(const auto& Bubble:Playback.Bubbles())AddBubble(Bubble);
        bReported=Playback.IsFinished()&&Script.bTalked;bChoicesDirty=true;
    }
    void AddBubble(const FProjectTalkPlayback::FBubble& Bubble)
    {
        if(!Messages)return;
        const bool Mine=Bubble.bPlayer;
        auto Text=SNew(STextBlock).AutoWrapText(true).Font(Font(13)).ColorAndOpacity(Mine?FLinearColor(1,1,1,1):ProjectDFSDUI::Text()).Text(FText::FromString(Bubble.Text));
        auto Row=SNew(SHorizontalBox);
        if(!Mine)Row->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0,0,8,0)[Avatar(Script.Npc,28)];
        Row->AddSlot().AutoWidth()[SNew(SBox).MaxDesiredWidth(236)[SNew(SBorder).BorderImage(Mine?&PlayerBrush:&NpcBrush).Padding(FMargin(12,8))[Text]]];
        Messages->InsertSlot(BubbleRows++).Padding(12,4)[SNew(SBox).HAlign(Mine?HAlign_Right:HAlign_Left)[Row]];
        Messages->ScrollToEnd();
    }
    void RebuildChoices()
    {
        if(!Answers)return;Answers->ClearChildren();
        auto Button=[this](const FString& Caption,TFunction<void()> Run,bool Primary=false)
        {
            return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle(Primary)).ContentPadding(FMargin(12,8)).OnClicked_Lambda([Run]{Run();return FReply::Handled();})
                [SNew(STextBlock).AutoWrapText(true).Font(Font(13)).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(Caption))];
        };
        if(const auto* Options=Playback.Choices())
        {
            // The answer buttons are rebuilt on the next tick, not inside their own click.
            for(int32 I=0;I<Options->Num();++I)Answers->AddSlot().AutoHeight().Padding(12,3)[Button((*Options)[I].Text,[this,I]{if(Playback.Choose(I)){AddBubble(Playback.Bubbles().Last());bChoicesDirty=true;}},true)];
            Answers->AddSlot().AutoHeight().Padding(0,0,0,10)[SNullWidget::NullWidget];
        }
        else if(Playback.IsFinished())
        {
            Answers->AddSlot().AutoHeight().Padding(16,8,16,4)[SNew(STextBlock).Justification(ETextJustify::Center).Font(Font(11)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text(FText::FromString(TEXT("대화가 끝났습니다.")))];
            Answers->AddSlot().AutoHeight().Padding(12,3,12,12)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[Button(TEXT("처음부터 다시 보기"),[this]{bRestart=true;})]
                +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("목록으로"),[this]{bBackToList=true;})]];
        }
    }
    static constexpr float SlideSeconds=.28f,TurnSeconds=.5f;
    FVector2D ViewSize{1280,720};
    float TurnTime=TurnSeconds; // a finished turn: opening the phone does not replay it
    bool bLandscape=false;
    float MinimizeTime=0;
    bool bMinimized=false;
public:
    /** Gives the keyboard back to the phone widget when it comes back from the corner. */
    TFunction<void()> Refocus;
private:
    TWeakObjectPtr<UGameInstance> Game;
    TSharedPtr<FJsonObject> FixedSnapshot;
    FSimpleDelegate OnClose;
    FDelegateHandle Changed;
    FSlateRoundedBoxBrush FrameBrush{FLinearColor::Black,0.f},ScreenBrush{FLinearColor::Black,0.f},NpcBrush{FLinearColor::Black,0.f},PlayerBrush{FLinearColor::Black,0.f},AvatarBrush{FLinearColor::Black,0.f},DotBrush{FLinearColor::Black,0.f},NotchBrush{FLinearColor::Black,0.f},RowBrush{FLinearColor::Black,0.f};
    TSharedPtr<SBox> ScreenBox;
    TSharedPtr<SScrollBox> Messages;
    TSharedPtr<SVerticalBox> Answers;
    FProjectTalkScript Script;
    FProjectTalkPlayback Playback;
    int32 BubbleRows=0;
    FString PendingChat;
    float OpenTime=0;
    bool bClosing=false,bInChat=false,bReported=false,bChoicesDirty=false,bRestart=false,bBackToList=false,bOpenGames=false,bInGames=false; // Button actions run on the next tick, outside the click.
};

UProjectPhoneWidget* UProjectPhoneWidget::Open(APlayerController* Player,const FString& StepId)
{
    if(!IsValid(Player))return nullptr;
    auto* Widget=CreateWidget<UProjectPhoneWidget>(Player);if(!Widget)return nullptr;
    Widget->InitialStep=StepId;Widget->OwnerController=Player;Widget->SetIsFocusable(true);
    // Only the phone and the dimming take clicks, so the screen behind works while the phone is tucked away.
    Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Widget->AddToViewport(ViewportZOrder);Widget->SetKeyboardFocus();Player->bShowMouseCursor=true;
    return Widget;
}
void UProjectPhoneWidget::Close(){if(Phone)Phone->RequestClose();else RemoveFromParent();}
TSharedRef<SWidget> UProjectPhoneWidget::RebuildWidget()
{
    const TWeakObjectPtr<UProjectPhoneWidget> Weak(this);
    SAssignNew(Phone,SProjectPhone).GameInstance(TWeakObjectPtr<UGameInstance>(GetGameInstance())).Step(InitialStep)
        .OnClose(FSimpleDelegate::CreateLambda([Weak]{if(auto* W=Weak.Get())W->RemoveFromParent();}));
    Phone->Refocus=[Weak]{if(auto* W=Weak.Get())W->SetKeyboardFocus();};
    return Phone.ToSharedRef();
}
FReply UProjectPhoneWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Escape){Close();return FReply::Handled();}
    return Super::NativeOnKeyDown(Geometry,Event);
}
void UProjectPhoneWidget::NativeDestruct(){Super::NativeDestruct();}
void UProjectPhoneWidget::ReleaseSlateResources(bool bReleaseChildren){Super::ReleaseSlateResources(bReleaseChildren);Phone.Reset();}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPhoneTalkPlaybackTest,"ProjectProject01.CaseRecords.PhoneTalkPlayback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectPhoneTalkPlaybackTest::RunTest(const FString&)
{
    const FString Json=TEXT(R"({"mainQuest":{"steps":[
        {"id":"kalin_talk","active":true,"talked":false,"choices":[],"talk":{"npc":"캘린","lines":[
            {"text":"보여?","options":null},
            {"text":null,"options":[{"text":"누구세요?","replies":["캘린이야.","반가워."]},{"text":"여기가 어디죠?","replies":[]}]},
            {"text":"조심해.","options":null},
            {"text":null,"options":[{"text":"알겠어요.","replies":["살아서 보자."]}]}]}},
        {"id":"survivor_tutorial","talk":null},
        {"id":"atea_talk","talked":true,"choices":[1,0],"talk":{"npc":"아테아","lines":[{"text":null,"options":[{"text":"A","replies":["a"]},{"text":"B","replies":["b"]}]},{"text":null,"options":[{"text":"C","replies":[]}]}]}}]}})");
    TSharedPtr<FJsonObject> Snapshot;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Snapshot);
    const auto Talks=FProjectTalkScript::FromSnapshot(Snapshot);
    TestEqual(TEXT("Only steps with a conversation become contacts, in quest order"),Talks.Num(),2);
    if(Talks.Num()!=2)return false;
    TestTrue(TEXT("Contact data keeps the NPC, unread state and saved choices"),Talks[0].Npc==TEXT("캘린")&&!Talks[0].bTalked&&Talks[1].bTalked&&Talks[1].SavedChoices==TArray<int32>({1,0}));
    FProjectTalkPlayback P;P.Start(Talks[0]);
    TestTrue(TEXT("The first NPC message waits behind a typing pause"),P.IsTyping()&&P.Bubbles().Num()==0&&!P.Tick(.1));
    TestTrue(TEXT("It arrives after the pause"),P.Tick(FProjectTalkPlayback::TypingSeconds(TEXT("보여?")))&&P.Bubbles().Num()==1);
    TestTrue(TEXT("A choice stops the conversation until the player answers"),P.Choices()&&P.Choices()->Num()==2&&!P.Tick(10)&&!P.IsTyping());
    TestFalse(TEXT("An option that does not exist is ignored"),P.Choose(5));
    TestTrue(TEXT("The chosen answer shows as the player's message"),P.Choose(0)&&P.Bubbles().Last().bPlayer&&P.Bubbles().Last().Text==TEXT("누구세요?"));
    P.Hurry();P.Tick(0);P.Hurry();P.Tick(0);
    TestTrue(TEXT("The NPC's replies to that answer come before the next line"),P.Bubbles().Num()==4&&P.Bubbles()[2].Text==TEXT("캘린이야.")&&P.Bubbles()[3].Text==TEXT("반가워."));
    P.Hurry();P.Tick(0);
    TestTrue(TEXT("A single-option choice is still a choice"),P.Choices()&&P.Choices()->Num()==1&&P.Choose(0));
    P.Hurry();P.Tick(0);P.Tick(0);
    TestTrue(TEXT("After the last reply the conversation is finished with every choice recorded"),P.IsFinished()&&P.Chosen()==TArray<int32>({0,0})&&P.Bubbles().Last().Text==TEXT("살아서 보자."));
    FProjectTalkPlayback Read;Read.Replay(Talks[1],Talks[1].SavedChoices);
    TestTrue(TEXT("A finished talk is rebuilt instantly from the saved choices"),Read.IsFinished()&&Read.Bubbles().Num()==3&&Read.Bubbles()[0].Text==TEXT("B")&&Read.Bubbles()[1].Text==TEXT("b")&&Read.Bubbles()[2].Text==TEXT("C"));
    FProjectTalkPlayback Empty;Empty.Replay(Talks[0],{});
    TestTrue(TEXT("Missing saved choices fall back to the first option instead of stopping"),Empty.IsFinished()&&Empty.Chosen()==TArray<int32>({0,0}));
    return true;
}

#include "Slate/WidgetRenderer.h"
#include "ProjectUIRenderCapture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
// Draws the phone (contacts with an unread dot, and a chat waiting on a choice) to Saved/CaseRecordUIQA for review.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPhoneRenderTest,"ProjectProject01.CaseRecords.PhoneRender",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectPhoneRenderTest::RunTest(const FString&)
{
    const FString Json=TEXT(R"({"mainQuest":{"steps":[
        {"id":"kalin_talk","active":true,"talked":false,"choices":[],"talk":{"npc":"캘린","lines":[
            {"text":"...이 메시지 보여? 보이면 아무거나 대답해 줘.","options":null},
            {"text":null,"options":[{"text":"누구세요?","replies":["캘린. 너보다 조금 먼저 여기서 눈을 뜬 사람이야."]},{"text":"여기가 어디예요?","replies":["나도 정확히는 몰라."]}]}]}},
        {"id":"atea_talk","talked":true,"choices":[0],"talk":{"npc":"아테아","lines":[{"text":"또 처음으로 돌아왔지?","options":null},{"text":null,"options":[{"text":"당신은 누구죠?","replies":["아테아."]}]}]}}]}})");
    TSharedPtr<FJsonObject> Snapshot;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Snapshot);
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("CaseRecordUIQA");IFileManager::Get().MakeDirectory(*Directory,true);
    FWidgetRenderer Renderer(false);const FIntPoint Size(1280,720);const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
    bool bSaved=true;
    auto Capture=[&](const TSharedRef<SProjectPhone>& Phone,const FString& Name,int32 Ticks=40)
    {
        for(int32 I=0;I<Ticks;++I)Phone->Tick(FGeometry::MakeRoot(FVector2D(Size),FSlateLayoutTransform()),I*.1,.1f); // Finish the slide-in and let the first message arrive.
        auto* Target=ProjectUIRenderCapture::Create(Size);
        for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,Phone,Dpi,FVector2D(Size),0);
        FlushRenderingCommands();TArray<FColor> Pixels;
        if(ProjectUIRenderCapture::Read(Target,Pixels)){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);bSaved&=FFileHelper::SaveArrayToFile(PNG,*(Directory/Name));}else bSaved=false;
        Target->ReleaseResource();
    };
    auto Contacts=SNew(SProjectPhone).Snapshot(Snapshot);Capture(Contacts,TEXT("PhoneContacts_1280x720.png"));
    auto Chat=SNew(SProjectPhone).Snapshot(Snapshot).Step(TEXT("kalin_talk"));Capture(Chat,TEXT("PhoneChat_1280x720.png"));
    // Turning sideways: halfway through the turn, then settled large in the middle; and back upright on the right.
    Chat->SetLandscape(true);Capture(Chat,TEXT("PhoneTurning_1280x720.png"),2);Capture(Chat,TEXT("PhoneLandscape_1280x720.png"),10);
    TestTrue(TEXT("Sideways the phone is wider than tall"),Chat->bLandscape&&Chat->LandscapeSize().X>Chat->LandscapeSize().Y);
    Chat->SetLandscape(false);Capture(Chat,TEXT("PhoneBackUpright_1280x720.png"),10);
    // Tapping outside: tucked small into the bottom-right corner, the screen behind no longer dimmed.
    Chat->SetMinimized(true);Capture(Chat,TEXT("PhoneMinimized_1280x720.png"),6);
    TestTrue(TEXT("Tucked away, the dimming is gone"),Chat->bMinimized&&Chat->MinimizeEase()==1.f);
    Chat->SetMinimized(false);Capture(Chat,TEXT("PhoneRestored_1280x720.png"),6);
    TestTrue(TEXT("Phone screens render to images"),bSaved);
    return true;
}
#endif

#include "ProjectMiniGameWidgets.h"
#include "ProjectGamepad.h"
#include "ProjectMiniGameRules.h"
#include "ProjectMiniGames.h"
#include "ProjectMiniGameSound.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectStoreBackdrop.h"
#include "ProjectProject01GameInstance.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

using namespace ProjectMiniGame;

namespace
{
FSlateFontInfo Font(int32 Size,const TCHAR* Weight=TEXT("Regular")){return FCoreStyle::GetDefaultFontStyle(Weight,Size);}
const FSlateBrush* White(){return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));}
/** A circle needs a corner radius of exactly half its size, so brushes are kept per radius. */
const FSlateBrush* Round(float Radius)
{
    static TMap<int32,TSharedPtr<FSlateRoundedBoxBrush>> Brushes;
    const int32 Key=FMath::Max(1,FMath::RoundToInt(Radius*2));
    if(auto* Found=Brushes.Find(Key))return Found->Get();
    return Brushes.Add(Key,MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White,Key*.5f)).Get();
}
FLinearColor Ink(float A=1){return FLinearColor(.012f,.012f,.014f,A);}

struct FDraw
{
    FSlateWindowElementList& Out;const FGeometry& G;int32 Layer;
    void Box(FVector2D P,FVector2D S,const FLinearColor& C,const FSlateBrush* Brush=nullptr)const
    {if(S.X>0&&S.Y>0)FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(S,FSlateLayoutTransform(P)),Brush?Brush:White(),ESlateDrawEffect::None,C);}
    void Circle(FVector2D Center,float R,const FLinearColor& C)const{Box(Center-FVector2D(R,R),FVector2D(R*2,R*2),C,Round(R));}
    /** A filled circle made of thin slices: exact for very dark colours, which the rounded brush draws too light. */
    void Disc(FVector2D Center,float R,const FLinearColor& C)const
    {const int32 Slices=FMath::Clamp(FMath::RoundToInt(R),6,40);for(int32 I=0;I<Slices;++I){const float Y0=-R+2*R*I/Slices,Mid=Y0+R/Slices,W=2*FMath::Sqrt(FMath::Max(0.f,R*R-Mid*Mid));Box(Center+FVector2D(-W*.5f,Y0),FVector2D(W,2*R/Slices+.5f),C);}}
    void Line(TArray<FVector2D> Points,const FLinearColor& C,float Thickness=1.f)const
    {FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,C,true,Thickness);}
    void Ring(FVector2D Center,float R,const FLinearColor& C,float Thickness=2.f)const
    {TArray<FVector2D> P;for(int32 I=0;I<=24;++I){const float A=I/24.f*2*PI;P.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*R);}Line(P,C,Thickness);}
    void Text(FVector2D P,const FString& S,int32 Size,const FLinearColor& C,bool bBold=false)const
    {FSlateDrawElement::MakeText(Out,Layer+1,G.ToPaintGeometry(FVector2D(400,Size*1.6f),FSlateLayoutTransform(P)),S,Font(Size,bBold?TEXT("Bold"):TEXT("Regular")),ESlateDrawEffect::None,C);}
};

/** A store mannequin standing with its feet at Foot. Head 0/1/2: facing front/left/right. Arm 0/1/2: down/raised/held out. */
/**
 * A store mannequin standing with its feet at Foot. Head 0/1/2: facing front (two eyes) / turned left / turned right
 * (a profile with the nose out). Arm 0/1/2: hanging down / both raised / the right one held out. Garment, when given,
 * colours the torso and hips so each figure can be told apart and remembered.
 */
void Mannequin(const FDraw& D,FVector2D Foot,float H,int32 Head,int32 Arm,const FLinearColor& Body,const FLinearColor& Edge,const FLinearColor* Garment=nullptr)
{
    const float Top=Foot.Y-H,W=H*.22f;const FLinearColor Cloth=Garment?*Garment:Body;
    const FVector2D HeadCenter(Foot.X+(Head==1?-H*.012f:Head==2?H*.012f:0.f),Top+H*.08f);const float R=H*.075f;
    D.Disc(HeadCenter,R,Body);
    if(Head==0)
    {
        D.Box(HeadCenter+FVector2D(-R*.55f,-R*.15f),FVector2D(R*.28f,R*.28f),Edge);D.Box(HeadCenter+FVector2D(R*.27f,-R*.15f),FVector2D(R*.28f,R*.28f),Edge);
    }
    else
    {
        // Profile: the nose sticks out on the side it looks to, one eye on that side.
        const float Side=Head==1?-1.f:1.f;
        D.Line({HeadCenter+FVector2D(Side*R*.85f,-R*.2f),HeadCenter+FVector2D(Side*R*1.35f,R*.1f),HeadCenter+FVector2D(Side*R*.85f,R*.3f)},Body,FMath::Max(2.f,R*.45f));
        D.Box(HeadCenter+FVector2D(Side*R*.45f-R*.14f,-R*.2f),FVector2D(R*.28f,R*.28f),Edge);
    }
    D.Box(FVector2D(Foot.X-H*.025f,Top+H*.15f),FVector2D(H*.05f,H*.04f),Body);
    D.Box(FVector2D(Foot.X-W*.5f,Top+H*.19f),FVector2D(W,H*.3f),Cloth);
    D.Box(FVector2D(Foot.X-W*.42f,Top+H*.48f),FVector2D(W*.84f,H*.08f),Cloth);
    D.Box(FVector2D(Foot.X-W*.4f,Top+H*.55f),FVector2D(W*.32f,H*.45f),Body);
    D.Box(FVector2D(Foot.X+W*.08f,Top+H*.55f),FVector2D(W*.32f,H*.45f),Body);
    const FVector2D L(Foot.X-W*.55f,Top+H*.21f),Rt(Foot.X+W*.55f,Top+H*.21f);const float T=FMath::Max(2.f,H*.065f);
    if(Arm==1){D.Line({L,L+FVector2D(-H*.1f,-H*.24f)},Body,T);D.Line({Rt,Rt+FVector2D(H*.1f,-H*.24f)},Body,T);}
    else if(Arm==2){D.Line({L,L+FVector2D(-H*.03f,H*.3f)},Body,T);D.Line({Rt,Rt+FVector2D(H*.32f,0)},Body,T);}
    else{D.Line({L,L+FVector2D(-H*.02f,H*.3f)},Body,T);D.Line({Rt,Rt+FVector2D(H*.02f,H*.3f)},Body,T);}
    D.Line({FVector2D(Foot.X+W*.5f,Top+H*.2f),FVector2D(Foot.X+W*.5f,Top+H*.5f)},Edge,1.f);
}

/** The play area: paints with the owner's function and passes the pointer on. */
class SProjectGameBoard final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectGameBoard){}
    SLATE_END_ARGS()
    TFunction<int32(const FDraw&)> Paint;
    TFunction<FReply(const FGeometry&,const FPointerEvent&,bool bDown)> Press;
    TFunction<void(const FGeometry&,const FPointerEvent&)> Move;
    // Clipped: the store picture is drawn to cover the area and must not spill over the title or the buttons.
    void Construct(const FArguments&){SetClipping(EWidgetClipping::ClipToBounds);}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(320,420);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool)const override
    {return Paint?Paint(FDraw{Out,G,Layer}):Layer;}
    virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)override{return Press?Press(G,E,true):FReply::Unhandled();}
    virtual FReply OnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)override{return Press?Press(G,E,false):FReply::Unhandled();}
    virtual FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E)override{if(Move)Move(G,E);return FReply::Unhandled();}
};

TSharedRef<SWidget> Label(const FString& S,int32 Size=12,bool bBold=false,FLinearColor C=FLinearColor::Transparent)
{
    return SNew(STextBlock).AutoWrapText(true).Font(Font(Size,bBold?TEXT("Bold"):TEXT("Regular"))).ColorAndOpacity(C.A>0?C:ProjectDFSDUI::Text()).Text(FText::FromString(S));
}
TSharedRef<SButton> Button(const FString& Caption,TFunction<void()> Run,bool bPrimary=false,int32 Size=12)
{
    // Not focusable: pressing a game button must not take the keyboard away from the game (which would pause it).
    return SNew(SButton).IsFocusable(false).ButtonStyle(&ProjectDFSDUI::ButtonStyle(bPrimary)).ContentPadding(FMargin(10,6)).HAlign(HAlign_Center)
        .OnClicked_Lambda([Run]{Run();return FReply::Handled();})[SNew(STextBlock).Font(Font(Size,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(Caption))];
}
FString GameName(const FString& Game)
{
    return Game==TEXT("spot")?TEXT("움직였다"):Game==TEXT("blackout")?TEXT("정전 복구"):Game==TEXT("endless")?TEXT("정전 복구 · 무한"):Game==TEXT("listen")?TEXT("어디서 났지?")
        :Game==TEXT("approach")?TEXT("다가가기"):Game==TEXT("shutter")?TEXT("셔터 내려"):Game==TEXT("mimic")?TEXT("흉내 내기"):TEXT("한 칸씩");
}
FString Record(const FString& Game,const FString& Board,int32 P,int32 S)
{
    if(Game==TEXT("spot")||Game==TEXT("listen"))return FString::Printf(TEXT("%d라운드 · %d점"),P,S);
    if(Game==TEXT("approach"))return FString::Printf(TEXT("%.1f초 · 들킴 %d번"),P/1000.f,S);
    if(Game==TEXT("blackout"))return FString::Printf(TEXT("%.1f초 · 배터리 %d%%"),P/1000.f,S);
    if(Game==TEXT("endless"))return FString::Printf(TEXT("퓨즈 %d개 · %.1f초"),P,S/1000.f);
    if(Game==TEXT("shutter"))return FString::Printf(TEXT("%d명 막음 · %.1f초"),P,S/1000.f);
    if(Game==TEXT("mimic"))return FString::Printf(TEXT("%d번 버팀 · %.1f초"),P,S/1000.f);
    if(Board==TEXT("stars"))return FString::Printf(TEXT("별 %d개 · %d수"),P,S);
    return FString::Printf(TEXT("%d수"),P);
}
}

// ======================================================================== shared game frame
/**
 * What every game shares: keyboard focus, a start card with how to play (new runs), pause (Esc or losing focus) with the
 * board hidden and the same how-to, a 3-second countdown when play resumes, active play time, saving on pause, and the pause-and-save hook for sign-out and map changes.
 */
class SProjectMiniGameView:public SCompoundWidget
{
public:
    enum class EPhase:uint8{Intro,Countdown,Running,Paused,Busy,Over};
    struct FHost{TWeakObjectPtr<UGameInstance> Game;TFunction<void()> ToList;TFunction<void(const FString&,const FString&)> Restart;TFunction<void(const FString&)> Board;};
    void Init(const FHost& InHost,const FString& InGame,const FString& InRunId,int32 InActiveMs)
    {
        Host=InHost;GameId=InGame;RunId=InRunId;ActiveSeconds=InActiveMs/1000.0;
        ChildSlot
        [
            SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(10,4,10,4)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Font(Font(14,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([this]{return FText::FromString(Title());})]
                +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("일시정지"),[this]{Pause();})]]
            +SVerticalBox::Slot().AutoHeight().Padding(12,0,12,4)[SNew(STextBlock).AutoWrapText(true).Font(Font(11)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]{return FText::FromString(Hud());})]
            +SVerticalBox::Slot().FillHeight(1).Padding(8,0)
            [
                SNew(SOverlay)
                +SOverlay::Slot()[SAssignNew(Board,SProjectGameBoard)]
                +SOverlay::Slot()[SAssignNew(Cover,SBox).Visibility_Lambda([this]{return Phase==EPhase::Running?EVisibility::Collapsed:EVisibility::Visible;})]
            ]
            +SVerticalBox::Slot().AutoHeight().Padding(8,6,8,10)[SAssignNew(ControlsBox,SBox)]
        ];
        Board->Paint=[this](const FDraw& D){return PaintBoard(D);};
        Board->Press=[this](const FGeometry& G,const FPointerEvent& E,bool bDown)
        {
            if(Phase==EPhase::Running)BoardPress(G,E,bDown);
            return FReply::Handled().SetUserFocus(SharedThis(this),EFocusCause::Mouse);
        };
        Board->Move=[this](const FGeometry& G,const FPointerEvent& E){if(Phase==EPhase::Running)BoardMove(G,E);};
        ControlsBox->SetContent(Controls());
        if(auto* Games=Subsystem())Games->SetActiveGame(this,[this]{Pause();});
        BeginCountdown();
    }
    virtual ~SProjectMiniGameView() override{if(auto* Games=Subsystem())Games->ClearActiveGame(this);}
    /** Called by the folder before it drops this view (back to the list, phone closed). */
    /** A new run: what to do and how, then 시작 (or Space/Enter) starts the countdown. */
    void ShowIntro()
    {
        Phase=EPhase::Intro;
        ShowCover(SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,10)[Label(Title(),18,true)]
            +SVerticalBox::Slot().AutoHeight().Padding(24,0,24,14)[HowToCard()]
            +SVerticalBox::Slot().AutoHeight().Padding(40,3)[Button(TEXT("시작 (스페이스)"),[this]{if(Phase==EPhase::Intro)BeginCountdown();},true,13)]);
    }
    void Leave(){if(Phase==EPhase::Running||Phase==EPhase::Countdown)Pause();else if(Phase==EPhase::Paused)SaveNow();if(auto* Games=Subsystem())Games->ClearActiveGame(this);}
    virtual bool SupportsKeyboardFocus()const override{return true;}
    virtual void OnFocusLost(const FFocusEvent& Event)override{SCompoundWidget::OnFocusLost(Event);if(Phase==EPhase::Running)Pause();}
    virtual FReply OnKeyDown(const FGeometry& G,const FKeyEvent& E)override
    {
        if(E.GetKey()==EKeys::Escape){if(Phase==EPhase::Running||Phase==EPhase::Countdown){Pause();return FReply::Handled();}return FReply::Unhandled();}
        if(Phase==EPhase::Intro&&(E.GetKey()==EKeys::SpaceBar||E.GetKey()==EKeys::Enter)){BeginCountdown();return FReply::Handled();}
        if(Phase==EPhase::Running&&Key(E,true))return FReply::Handled();
        // Game keys never fall through to the lobby or the character, even while paused.
        return IsGameKey(E.GetKey())?FReply::Handled():FReply::Unhandled();
    }
    virtual FReply OnKeyUp(const FGeometry& G,const FKeyEvent& E)override{if(Phase==EPhase::Running)Key(E,false);return IsGameKey(E.GetKey())?FReply::Handled():FReply::Unhandled();}
    virtual void Tick(const FGeometry& G,const double Time,const float Delta)override
    {
        SCompoundWidget::Tick(G,Time,Delta);
        const float Dt=FMath::Clamp(Delta,0.f,.1f);
        if(!bFocusedOnce&&FSlateApplication::IsInitialized()){bFocusedOnce=true;FSlateApplication::Get().SetKeyboardFocus(SharedThis(this),EFocusCause::SetDirectly);}
        if(Phase==EPhase::Countdown){CountdownLeft-=Dt;if(CountdownLeft<=0)Phase=EPhase::Running;}
        if(Phase==EPhase::Running){ActiveSeconds+=Dt;TickGame(Dt);}
    }
protected:
    UProjectMiniGames* Subsystem()const{auto* GI=Host.Game.Get();return GI?GI->GetSubsystem<UProjectMiniGames>():nullptr;}
    int32 ActiveMs()const{return int32(ActiveSeconds*1000);}
    void BeginCountdown(){Phase=EPhase::Countdown;CountdownLeft=3.f;ShowCover(CountdownCover());}
    void Pause()
    {
        if(Phase!=EPhase::Running&&Phase!=EPhase::Countdown)return;
        Phase=EPhase::Paused;OnPaused();SaveNow();
        ShowCover(SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,6)[Label(TEXT("일시정지"),18,true)]
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(16,0,16,10)[Label(TEXT("멈춘 동안 화면은 가려지고, 시간은 기록에 들어가지 않습니다. 진행 상황은 저장되어 다른 PC에서도 이어할 수 있습니다."),11,false,ProjectDFSDUI::Muted())]
            +SVerticalBox::Slot().AutoHeight().Padding(24,0,24,12)[HowToCard()]
            +SVerticalBox::Slot().AutoHeight().Padding(40,3)[Button(TEXT("계속하기"),[this]{BeginCountdown();},true,13)]
            +SVerticalBox::Slot().AutoHeight().Padding(40,3)[Button(TEXT("처음부터"),[this]{Host.Restart(GameId,RestartStage());})]
            +SVerticalBox::Slot().AutoHeight().Padding(40,3)[Button(TEXT("목록으로"),[this]{Host.ToList();})]);
    }
    void SaveNow(){if(auto* Games=Subsystem();Games&&!RunId.IsEmpty()&&!bFinished)Games->Save(RunId,ActiveMs(),SaveState());}
    void ShowCover(TSharedRef<SWidget> Content)
    {
        Cover->SetContent(SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(.015f,.015f,.018f,.96f)).HAlign(HAlign_Fill).VAlign(VAlign_Center)[Content]);
    }
    TSharedRef<SWidget> CountdownCover()
    {
        return SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Font(Font(40,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([this]{return FText::AsNumber(FMath::Max(1,FMath::CeilToInt(CountdownLeft)));})]
            +SVerticalBox::Slot().AutoHeight().Padding(24,10)[HowToCard()];
    }
    /** Three short lines: 목표 (what wins), 조작 (the keys and clicks), 요령 (the tell to watch and how to beat it). */
    TSharedRef<SWidget> HowToCard()const
    {
        const FHowTo H=HowTo();
        auto Row=[](const TCHAR* Name,const FString& Text)
        {
            return SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)[SNew(SBox).WidthOverride(34)[Label(Name,11,true,ProjectDFSDUI::Accent())]]
                +SHorizontalBox::Slot().FillWidth(1)[Label(Text,11,false,ProjectDFSDUI::Muted())];
        };
        return SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(0,2)[Row(TEXT("목표"),H.Goal)]
            +SVerticalBox::Slot().AutoHeight().Padding(0,2)[Row(TEXT("조작"),H.Play)]
            +SVerticalBox::Slot().AutoHeight().Padding(0,2)[Row(TEXT("요령"),H.Tip)];
    }
    /** The run is over: result text and what to do next. */
    void ShowOver(const FString& Headline,const FString& Detail,TArray<TPair<FString,TFunction<void()>>> Extra={})
    {
        Phase=EPhase::Over;bFinished=true;if(auto* Games=Subsystem())Games->ClearActiveGame(this);
        auto Box=SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,6)[Label(Headline,18,true)]
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(16,0,16,14)[Label(Detail,12,false,ProjectDFSDUI::Muted())];
        for(auto& E:Extra)Box->AddSlot().AutoHeight().Padding(40,3)[Button(E.Key,E.Value,true,13)];
        Box->AddSlot().AutoHeight().Padding(40,3)[Button(TEXT("다시 하기"),[this]{Host.Restart(GameId,RestartStage());},Extra.IsEmpty(),13)];
        Box->AddSlot().AutoHeight().Padding(40,3)[Button(TEXT("순위 보기"),[this]{Host.Board(GameId);})];
        Box->AddSlot().AutoHeight().Padding(40,3)[Button(TEXT("목록으로"),[this]{Host.ToList();})];
        ShowCover(Box);
    }
    void ShowError(const FString& Message){ShowOver(TEXT("연결 문제"),Message);}
    static FVector2D Local(const FGeometry& G,const FPointerEvent& E){return G.AbsoluteToLocal(E.GetScreenSpacePosition());}
    // ---- per game ----
    virtual FString Title()const{return GameName(GameId);}
    virtual FString Hud()const=0;
    struct FHowTo{FString Goal,Play,Tip;};
    virtual FHowTo HowTo()const=0;
    virtual int32 PaintBoard(const FDraw& D)const=0;
    virtual TSharedRef<SWidget> Controls(){return SNullWidget::NullWidget;}
    virtual TSharedRef<FJsonObject> SaveState()const=0;
    virtual bool Key(const FKeyEvent& E,bool bDown){return false;}
    virtual bool IsGameKey(const FKey& K)const{return false;}
    virtual void BoardPress(const FGeometry& G,const FPointerEvent& E,bool bDown){}
    virtual void BoardMove(const FGeometry& G,const FPointerEvent& E){}
    virtual void TickGame(float Dt){}
    virtual void OnPaused(){}
    virtual FString RestartStage()const{return FString();}

    FHost Host;FString GameId,RunId;
    EPhase Phase=EPhase::Countdown;
    double ActiveSeconds=0;float CountdownLeft=3;bool bFocusedOnce=false,bFinished=false;
    TSharedPtr<SProjectGameBoard> Board;TSharedPtr<SBox> Cover,ControlsBox;
};

// ======================================================================== 움직였다
class SProjectSpotGame final:public SProjectMiniGameView
{
public:
    SLATE_BEGIN_ARGS(SProjectSpotGame){}
    SLATE_END_ARGS()
    void Construct(const FArguments&,const FHost& InHost,const FString& Run,int32 Ms,const FSpotRound& InRound,const TSharedPtr<FJsonObject>& Saved)
    {
        Round=InRound;
        // Continuing: the picture to remember comes back with the time that was left, never more.
        double Left=Round.MemorizeSeconds();FString Stage;
        if(Saved&&Saved->TryGetStringField(TEXT("phase"),Stage)&&Stage==TEXT("answer"))Step=EStep::Answer;
        else if(Saved&&Saved->TryGetNumberField(TEXT("left"),Left))Left=FMath::Clamp(Left,0.,double(Round.MemorizeSeconds()));
        StepTime=Step==EStep::Memorize?float(Left):0.f;
        Init(InHost,TEXT("spot"),Run,Ms);
    }
private:
    enum class EStep:uint8{Memorize,Dark,Answer,Waiting,Reveal};
    FSpotRound Round;EStep Step=EStep::Memorize;float StepTime=0;TSet<int32> Picks;TArray<int32> Changed;bool bCorrect=false;TSharedPtr<FJsonObject> NextRound;
    static constexpr float DarkSeconds=2.f,RevealSeconds=1.8f;
    FString Hud()const override
    {
        const FString Lives=FString::ChrN(Round.Lives,TEXT('●'))+FString::ChrN(FMath::Max(0,3-Round.Lives),TEXT('○'));
        const FString What=Step==EStep::Memorize?FString::Printf(TEXT("기억하세요 %.0f초"),FMath::Max(0.f,StepTime)):Step==EStep::Dark?TEXT("불이 꺼졌습니다"):Step==EStep::Answer?FString::Printf(TEXT("움직인 마네킹 %d개를 고르세요"),Round.Changes):TEXT("");
        return FString::Printf(TEXT("%d라운드 · %d점 · 목숨 %s   %s"),Round.Round,Round.Score,*Lives,*What);
    }
    FHowTo HowTo()const override
    {
        return {TEXT("불이 꺼진 사이 움직인 마네킹을 모두 찾으세요."),
            TEXT("마네킹을 눌러 고르기(다시 누르면 취소) → 확인 또는 스페이스."),
            TEXT("옷 색으로 하나씩 외워 두세요. 바뀌는 것은 셋 중 하나: 옆으로 한 걸음, 고개 방향, 팔 모양. 세 번 틀리면 끝납니다.")};
    }
    bool IsGameKey(const FKey& K)const override{return K==EKeys::SpaceBar||K==EKeys::Enter;}
    bool Key(const FKeyEvent& E,bool bDown)override{if(bDown&&IsGameKey(E.GetKey())){Submit();return true;}return false;}
    TSharedRef<SWidget> Controls()override
    {
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("고른 것 지우기"),[this]{if(Step==EStep::Answer)Picks.Reset();})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("확인"),[this]{Submit();},true)];
    }
    void TickGame(float Dt)override
    {
        StepTime-=Dt;
        if(Step==EStep::Memorize&&StepTime<=0){Step=EStep::Dark;StepTime=DarkSeconds;UProjectMiniGameSound::Play(EProjectMiniGameSound::LightsOut);}
        else if(Step==EStep::Dark&&StepTime<=0){Step=EStep::Answer;StepTime=0;UProjectMiniGameSound::Play(EProjectMiniGameSound::LightsOn);SaveNow();}
        else if(Step==EStep::Reveal&&StepTime<=0)
        {
            if(NextRound&&FSpotRound::Parse(NextRound,Round)){Picks.Reset();Changed.Reset();Step=EStep::Memorize;StepTime=Round.MemorizeSeconds();}
        }
    }
    void OnPaused()override{if(Step==EStep::Dark){Step=EStep::Answer;StepTime=0;}}
    TSharedRef<FJsonObject> SaveState()const override
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("phase"),Step==EStep::Memorize?TEXT("memorize"):TEXT("answer"));J->SetNumberField(TEXT("left"),FMath::Max(0.f,StepTime));return J;
    }
    void Submit()
    {
        if(Phase!=EPhase::Running||Step!=EStep::Answer)return;
        auto* Games=Subsystem();if(!Games)return;
        Step=EStep::Waiting;TArray<int32> List=Picks.Array();List.Sort();
        const TWeakPtr<SProjectSpotGame> Weak=StaticCastSharedRef<SProjectSpotGame>(AsShared());
        Games->SpotAnswer(RunId,Round.Round,List,ActiveMs(),[Weak](TSharedPtr<FJsonObject> Json,const FString& Error)
        {
            auto Self=Weak.Pin();if(!Self)return;
            if(!Error.IsEmpty()||!Json){Self->Step=EStep::Answer;Self->ShowError(Error);return;}
            Self->Changed.Reset();const TArray<TSharedPtr<FJsonValue>>* List=nullptr;
            if(Json->TryGetArrayField(TEXT("changed"),List))for(const auto& V:*List)Self->Changed.Add(int32(V->AsNumber()));
            Json->TryGetBoolField(TEXT("correct"),Self->bCorrect);
            bool bOver=false;Json->TryGetBoolField(TEXT("over"),bOver);
            double Cleared=0,Score=0,Lives=0;Json->TryGetNumberField(TEXT("cleared"),Cleared);Json->TryGetNumberField(TEXT("score"),Score);Json->TryGetNumberField(TEXT("lives"),Lives);
            Self->Round.Cleared=int32(Cleared);Self->Round.Score=int32(Score);Self->Round.Lives=int32(Lives);
            const TSharedPtr<FJsonObject>* Next=nullptr;Self->NextRound=Json->TryGetObjectField(TEXT("spot"),Next)?*Next:nullptr;
            Self->Step=EStep::Reveal;Self->StepTime=RevealSeconds;UProjectMiniGameSound::Play(Self->bCorrect?EProjectMiniGameSound::Good:EProjectMiniGameSound::Bad);
            if(bOver)Self->ShowOver(TEXT("게임 끝"),FString::Printf(TEXT("%d라운드 통과 · %d점"),int32(Cleared),int32(Score)));
        });
    }
    // The picture: the closed store behind, mannequins in front, nearer ones bigger and drawn last.
    FBox2D Picture(const FGeometry& G)const{const FVector2D S=G.GetLocalSize();return FBox2D(FVector2D(0,0),S);}
    FVector2D FootOf(const FSpotFigure& F,const FBox2D& R,float& Height)const
    {
        const FVector2D S=R.GetSize();const float Depth=(F.Y-260)/640.f;
        Height=S.Y*(.2f+.16f*Depth);
        return R.Min+FVector2D(F.X/1000.f*S.X,S.Y*(.38f+.58f*Depth));
    }
    int32 Hit(const FGeometry& G,FVector2D P)const
    {
        const auto& List=Round.After;const FBox2D R=Picture(G);int32 Best=INDEX_NONE;float BestY=-1;
        for(int32 I=0;I<List.Num();++I)
        {
            float H=0;const FVector2D Foot=FootOf(List[I],R,H);
            if(FMath::Abs(P.X-Foot.X)<=H*.22f&&P.Y<=Foot.Y&&P.Y>=Foot.Y-H&&Foot.Y>BestY){Best=I;BestY=Foot.Y;}
        }
        return Best;
    }
    void BoardPress(const FGeometry& G,const FPointerEvent& E,bool bDown)override
    {
        if(!bDown||Step!=EStep::Answer)return;
        const int32 I=Hit(G,Local(G,E));if(I==INDEX_NONE)return;
        if(Picks.Contains(I))Picks.Remove(I);else if(Picks.Num()<12)Picks.Add(I);
        UProjectMiniGameSound::Play(EProjectMiniGameSound::Pick);
    }
    int32 PaintBoard(const FDraw& D)const override
    {
        const FBox2D R=Picture(D.G);
        ProjectDFSDUI::PaintStoreBackdrop(D.Out,D.G,D.Layer,FLinearColor(1.f,1.f,1.f,1.f)); // kept dark so the figures stand out
        const FDraw Top{D.Out,D.G,D.Layer+3};
        const bool bAfter=Step!=EStep::Memorize;
        const auto& List=bAfter?Round.After:Round.Before;
        TArray<int32> Order;for(int32 I=0;I<List.Num();++I)Order.Add(I);
        Order.Sort([&](int32 A,int32 B){return List[A].Y<List[B].Y;});
        for(const int32 I:Order)
        {
            float H=0;const FVector2D Foot=FootOf(List[I],R,H);
            // Each figure keeps its own garment colour before and after the lights go out.
            static const FLinearColor Garments[]={{.32f,.05f,.04f,1.f},{.04f,.07f,.24f,1.f},{.16f,.19f,.04f,1.f},{.42f,.27f,.03f,1.f},{.2f,.05f,.24f,1.f},{.03f,.2f,.17f,1.f},
                {.26f,.11f,.04f,1.f},{.12f,.12f,.14f,1.f},{.36f,.12f,.2f,1.f},{.05f,.16f,.06f,1.f},{.4f,.15f,.05f,1.f},{.1f,.16f,.3f,1.f}};
            Mannequin(Top,Foot,H,List[I].Head,List[I].Arm,FLinearColor(.36f,.34f,.31f,1.f),FLinearColor(.03f,.028f,.025f,1.f),&Garments[I%UE_ARRAY_COUNT(Garments)]);
            const FDraw Mark{D.Out,D.G,D.Layer+5};
            const FVector2D Mid=Foot-FVector2D(0,H*.5f);
            if(Step==EStep::Reveal)
            {
                const bool bChanged=Changed.Contains(I),bPicked=Picks.Contains(I);
                if(bChanged)Mark.Ring(Mid,H*.42f,bPicked?FLinearColor(.3f,.85f,.45f,1.f):FLinearColor(.95f,.75f,.2f,1.f),3.f);
                else if(bPicked)Mark.Ring(Mid,H*.42f,FLinearColor(.9f,.25f,.2f,1.f),3.f);
            }
            else if(Picks.Contains(I))Mark.Ring(Mid,H*.42f,ProjectDFSDUI::Accent(),3.f);
        }
        // Lights out: one smooth fade to black and back, never a flicker.
        if(Step==EStep::Dark)
        {
            const float Fade=FMath::Min(1.f,FMath::Min((DarkSeconds-StepTime)/.3f,StepTime/.3f));
            D.Box(FVector2D::ZeroVector,D.G.GetLocalSize(),FLinearColor(0,0,0,FMath::Clamp(Fade,0.f,1.f)*.97f+.0f));
        }
        if(Step==EStep::Memorize)
        {
            const float Part=FMath::Clamp(StepTime/Round.MemorizeSeconds(),0.f,1.f);
            D.Box(FVector2D(0,D.G.GetLocalSize().Y-4),FVector2D(D.G.GetLocalSize().X*Part,4),ProjectDFSDUI::Accent());
        }
        if(Step==EStep::Reveal){const FDraw Note{D.Out,D.G,D.Layer+6};Note.Text(FVector2D(12,10),bCorrect?TEXT("정답"):TEXT("틀렸습니다"),16,bCorrect?FLinearColor(.3f,.85f,.45f,1.f):FLinearColor(.95f,.4f,.3f,1.f),true);}
        return D.Layer+7;
    }
};

// ======================================================================== 정전 복구
class SProjectBlackoutGame final:public SProjectMiniGameView
{
public:
    SLATE_BEGIN_ARGS(SProjectBlackoutGame){}
    SLATE_END_ARGS()
    void Construct(const FArguments&,const FHost& InHost,const FString& Run,int32 Ms,const TSharedPtr<FJsonObject>& Saved,bool bInEndless=false)
    {
        if(!Sim.Load(Saved))Sim.Start(uint32(GetTypeHash(Run)),bInEndless);
        Sim.bEndless=bInEndless;
        if(const auto* S=UProjectProject01GameUserSettings::Get())
        {Keys[0]=S->GetMoveForwardKey();Keys[1]=S->GetMoveBackwardKey();Keys[2]=S->GetMoveLeftKey();Keys[3]=S->GetMoveRightKey();}
        Init(InHost,bInEndless?TEXT("endless"):TEXT("blackout"),Run,Ms);
    }
private:
    FBlackoutSim Sim;FBlackoutInput Input;bool Held[4]={},bToggle=false;FKey Keys[4]={EKeys::W,EKeys::S,EKeys::A,EKeys::D};
    float Scale=1,CreakWait=0;FVector2D Offset=FVector2D::ZeroVector;
    FString Hud()const override
    {
        if(Sim.bEndless)return FString::Printf(TEXT("배터리 %d%% · 퓨즈 %d개 (들고 있음 %d) · 마네킹 최대 %d · %.1f초"),FMath::RoundToInt(Sim.Battery),Sim.Installed,Sim.Carried,Sim.MannequinLimit(),ActiveSeconds);
        return FString::Printf(TEXT("배터리 %d%% · 퓨즈 %d/3 (들고 있음 %d) · %.1f초"),FMath::RoundToInt(Sim.Battery),Sim.Installed,Sim.Carried,ActiveSeconds);
    }
    FHowTo HowTo()const override
    {
        return {Sim.bEndless?TEXT("퓨즈가 끝없이 나옵니다. 주워서 배전반에 끼울 때마다 배터리가 25% 찹니다. 붙잡힐 때까지 몇 개를 끼울 수 있을까요?"):TEXT("바닥의 노란 퓨즈 3개를 밟아 줍고, 위쪽 배전반 앞에 서 있으면 저절로 끼워집니다."),
            ProjectGamepad::UsingPad()?FString::Printf(TEXT("십자 키로 이동 · 왼쪽 스틱으로 손전등 비추기 · %s로 손전등 끄고 켜기"),*ProjectGamepad::FixedLabel(EKeys::Gamepad_RightTrigger)):
            FString::Printf(TEXT("%s/%s/%s/%s 또는 방향키로 이동 · 마우스로 손전등 비추기 · F 또는 오른쪽 클릭으로 손전등 끄고 켜기"),
                *Keys[0].GetDisplayName().ToString(),*Keys[2].GetDisplayName().ToString(),*Keys[1].GetDisplayName().ToString(),*Keys[3].GetDisplayName().ToString()),
            TEXT("빛에 닿은 마네킹은 멈춥니다. 어둠 속 하얀 발자국 = 마네킹이 다가오는 중, 그쪽을 비추세요. 배터리가 닳으면 다시 못 켜니 아껴 쓰세요.")};
    }
    int32 Dir(const FKey& K)const
    {
        for(int32 I=0;I<4;++I)if(K==Keys[I])return I;
        return K==EKeys::Up?0:K==EKeys::Down?1:K==EKeys::Left?2:K==EKeys::Right?3:INDEX_NONE;
    }
    bool IsGameKey(const FKey& K)const override{return Dir(K)!=INDEX_NONE||K==EKeys::E||K==EKeys::F;}
    bool Key(const FKeyEvent& E,bool bDown)override
    {
        const FKey K=E.GetKey();
        if(const int32 D=Dir(K);D!=INDEX_NONE){Held[D]=bDown;return true;}
        if(K==EKeys::E)return true; // fuses seat on their own now; E stays the game's so it never reaches the lobby
        if(K==EKeys::F){if(bDown&&!E.IsRepeat())bToggle=true;return true;}
        return false;
    }
    void OnPaused()override{for(bool& H:Held)H=false;}
    void Layout(const FGeometry& G){const FVector2D S=G.GetLocalSize();Scale=FMath::Min(S.X/FBlackoutSim::Width,S.Y/FBlackoutSim::Height);Offset=(S-FVector2D(FBlackoutSim::Width,FBlackoutSim::Height)*Scale)*.5f;}
    FVector2D ToRoom(const FGeometry& G,FVector2D P){Layout(G);return (P-Offset)/FMath::Max(Scale,.01f);}
    void BoardMove(const FGeometry& G,const FPointerEvent& E)override{Input.Aim=ToRoom(G,Local(G,E));}
    void BoardPress(const FGeometry& G,const FPointerEvent& E,bool bDown)override
    {
        Input.Aim=ToRoom(G,Local(G,E));
        if(bDown&&E.GetEffectingButton()==EKeys::RightMouseButton)bToggle=true;
    }
    void TickGame(float Dt)override
    {
        Input.Move=FVector2D((Held[3]?1:0)-(Held[2]?1:0),(Held[1]?1:0)-(Held[0]?1:0));
        Input.bHoldInstall=true;Input.bToggleLight=bToggle;bToggle=false; // standing at the board with a fuse is enough
        const bool bLightBefore=Sim.bLight;const int32 InstalledBefore=Sim.Installed,PrintsBefore=Sim.Prints.Num();
        const auto Outcome=Sim.Tick(Dt,Input);
        // Sounds: the torch switch, a fuse seating, and a creak whenever a mannequin moves in the dark (at most every 0.4 s).
        CreakWait-=Dt;
        if(Sim.bLight!=bLightBefore)UProjectMiniGameSound::Play(EProjectMiniGameSound::Switch);
        if(Sim.Installed>InstalledBefore)UProjectMiniGameSound::Play(EProjectMiniGameSound::Fuse);
        if(Sim.Prints.Num()>PrintsBefore&&CreakWait<=0){CreakWait=.4f;UProjectMiniGameSound::Play(EProjectMiniGameSound::Creak);}
        if(Outcome!=FBlackoutSim::EOutcome::Playing)UProjectMiniGameSound::Play(Outcome==FBlackoutSim::EOutcome::Cleared?EProjectMiniGameSound::Good:EProjectMiniGameSound::Caught);
        if(Outcome==FBlackoutSim::EOutcome::Playing)return;
        auto* Games=Subsystem();Phase=EPhase::Busy;
        const bool bCleared=Outcome==FBlackoutSim::EOutcome::Cleared;
        const TWeakPtr<SProjectBlackoutGame> Weak=StaticCastSharedRef<SProjectBlackoutGame>(AsShared());
        const int32 Ms=ActiveMs(),Battery=FMath::RoundToInt(Sim.Battery);
        if(Sim.bEndless)
        {
            // Endless ends only when caught; the record is the fuses seated.
            const int32 Fuses=Sim.Installed;
            auto Show=[Weak,Fuses,Ms](const FString& Error){if(auto Self=Weak.Pin())Self->ShowOver(TEXT("붙잡혔습니다"),FString::Printf(TEXT("퓨즈 %d개 · %.1f초"),Fuses,Ms/1000.f)+(Error.IsEmpty()?FString():TEXT("\n")+Error));};
            if(!Games){Show(FString());return;}
            Games->Finish(RunId,false,Ms,Battery,{},[Show](TSharedPtr<FJsonObject>,const FString& Error){Show(Error);},0,0,Fuses);return;
        }
        if(!Games){ShowOver(bCleared?TEXT("전기가 들어왔습니다"):TEXT("붙잡혔습니다"),TEXT(""));return;}
        Games->Finish(RunId,bCleared,Ms,Battery,{},[Weak,bCleared,Ms,Battery](TSharedPtr<FJsonObject>,const FString& Error)
        {
            auto Self=Weak.Pin();if(!Self)return;
            if(!bCleared){Self->ShowOver(TEXT("붙잡혔습니다"),TEXT("빛을 돌린 사이 마네킹이 다가왔습니다. 발자국이 남는 쪽을 먼저 비춰 보세요."));return;}
            Self->ShowOver(TEXT("전기가 들어왔습니다"),Error.IsEmpty()?FString::Printf(TEXT("%.1f초 · 배터리 %d%% 남김"),Ms/1000.f,Battery):Error);
        });
    }
    TSharedRef<FJsonObject> SaveState()const override{return Sim.Save();}
    int32 PaintBoard(const FDraw& D)const override
    {
        const_cast<SProjectBlackoutGame*>(this)->Layout(D.G);
        auto P=[&](FVector2D Room){return Offset+Room*Scale;};
        const float S=Scale;
        D.Box(Offset,FVector2D(FBlackoutSim::Width,FBlackoutSim::Height)*S,FLinearColor(.018f,.017f,.016f,1.f));
        // Walls and the four doorways.
        D.Line({P(FVector2D(0,0)),P(FVector2D(FBlackoutSim::Width,0)),P(FVector2D(FBlackoutSim::Width,FBlackoutSim::Height)),P(FVector2D(0,FBlackoutSim::Height)),P(FVector2D(0,0))},FLinearColor(.12f,.11f,.1f,1.f),3.f);
        for(const FVector2D Door:{FVector2D(0,FBlackoutSim::Height*.5),FVector2D(FBlackoutSim::Width,FBlackoutSim::Height*.5)})D.Box(P(Door)-FVector2D(3,18*S),FVector2D(6,36*S),FLinearColor(.006f,.006f,.006f,1.f));
        // Torch: a fan of faint lines from the player (lit area), drawn under everything else.
        if(Sim.bLight&&Sim.Battery>0)
        {
            const FVector2D Dir=Sim.TorchDirection();const float Base=FMath::Atan2(Dir.Y,Dir.X);
            for(float A=-FBlackoutSim::TorchHalfAngle;A<=FBlackoutSim::TorchHalfAngle;A+=1.5f)
            {
                const float R=Base+FMath::DegreesToRadians(A);const float Edge=1.f-FMath::Abs(A)/FBlackoutSim::TorchHalfAngle;
                D.Line({P(Sim.Player),P(Sim.Player+FVector2D(FMath::Cos(R),FMath::Sin(R))*FBlackoutSim::TorchRange)},FLinearColor(1.f,.9f,.62f,.05f+.06f*Edge),FMath::Max(2.f,6*S));
            }
        }
        const FDraw Top{D.Out,D.G,D.Layer+2};
        // Fuse board with its three slots.
        Top.Box(P(FBlackoutSim::Board-FVector2D(34,14)),FVector2D(68,22)*S,FLinearColor(.09f,.08f,.07f,1.f));
        for(int32 I=0;I<3;++I)Top.Box(P(FBlackoutSim::Board+FVector2D(-26+I*20,-9)),FVector2D(12,12)*S,I<(Sim.bEndless?Sim.Installed%3:Sim.Installed)?FLinearColor(.95f,.75f,.25f,1.f):FLinearColor(.02f,.02f,.02f,1.f));
        Top.Text(P(FBlackoutSim::Board+FVector2D(-17,12)),TEXT("배전반"),FMath::Max(9,int32(11*S)),FLinearColor(.75f,.7f,.6f,1.f),true);
        if(Sim.bInstalling)Top.Ring(P(FBlackoutSim::Board),20*S,FLinearColor(.95f,.75f,.25f,.4f+.6f*float(Sim.InstallProgress/FBlackoutSim::InstallSeconds)),3.f);
        // Fuses glow faintly; mannequins only show in the light or right next to the player.
        for(const FVector2D& F:Sim.Fuses)
        {
            const bool bLit=Sim.Lit(F);
            Top.Circle(P(F),5*S,FLinearColor(.95f,.7f,.2f,bLit?1.f:.18f));
            Top.Text(P(F)+FVector2D(-9,-9*S-14),TEXT("퓨즈"),FMath::Max(8,int32(9*S)),FLinearColor(.95f,.75f,.3f,bLit?.9f:.3f));
        }
        for(const auto& Print:Sim.Prints){const float A=FMath::Clamp(1.f-float(Print.Age/2.5),0.f,1.f)*.5f;Top.Circle(P(Print.Pos),2.5f*S,FLinearColor(.75f,.72f,.68f,A));}
        for(const auto& M:Sim.Mannequins)
        {
            const bool bLit=Sim.Lit(M.Pos),bNear=FVector2D::Distance(M.Pos,Sim.Player)<46;
            if(!bLit&&!bNear)continue;
            Mannequin(Top,P(M.Pos+FVector2D(0,10)),36*S,0,bLit?0:2,bLit?FLinearColor(.62f,.58f,.52f,1.f):FLinearColor(.16f,.15f,.14f,1.f),Ink());
        }
        Top.Circle(P(Sim.Player),9*S,FLinearColor(.55f,.75f,.85f,1.f));
        Top.Line({P(Sim.Player),P(Sim.Player+Sim.TorchDirection()*14)},FLinearColor(.9f,.9f,.8f,1.f),2.f);
        // What to do right now, in the room's bottom-left corner.
        const FString Task=Sim.bInstalling?TEXT("퓨즈를 끼우는 중 · 배전반 앞에 머무르세요"):Sim.Carried>0?FString::Printf(TEXT("퓨즈 %d개를 들었습니다 · 위쪽 배전반 앞으로"),Sim.Carried):TEXT("노란 퓨즈를 밟아 주우세요");
        Top.Text(P(FVector2D(10,FBlackoutSim::Height-24)),(Sim.bEndless?FString::Printf(TEXT("할 일: %s (%d개째)"),*Task,Sim.Installed+1):FString::Printf(TEXT("할 일: %s (%d/3)"),*Task,Sim.Installed)),FMath::Max(9,int32(12*S)),FLinearColor(.85f,.82f,.75f,.9f),true);
        return D.Layer+4;
    }
};

// ======================================================================== 한 칸씩
class SProjectStepsGame final:public SProjectMiniGameView
{
public:
    SLATE_BEGIN_ARGS(SProjectStepsGame){}
    SLATE_END_ARGS()
    /** The stage, the saved moves and what "다음 스테이지" does (one argument: Slate passes at most five). */
    struct FSetup{FStepStage Stage;TSharedPtr<FJsonObject> Saved;TFunction<void(const FString&)> Next;int32 StageCount=13;};
    void Construct(const FArguments&,const FHost& InHost,const FString& Run,int32 Ms,const FSetup& Setup)
    {
        Stage=Setup.Stage;NextStage=Setup.Next;const TSharedPtr<FJsonObject>& Saved=Setup.Saved;
        const TArray<TSharedPtr<FJsonValue>>* List=nullptr;
        if(Saved&&Saved->TryGetArrayField(TEXT("moves"),List))for(const auto& V:*List)Moves.Add(V->AsString());
        double Saved_Hints=0,Saved_Level=0;
        if(Saved){Saved->TryGetNumberField(TEXT("hints"),Saved_Hints);Saved->TryGetNumberField(TEXT("hintLevel"),Saved_Level);}
        HintsUsed=int32(Saved_Hints);HintLevel=FMath::Clamp(int32(Saved_Level),0,3);StageCount=Setup.StageCount;
        // A stage already lost once this session offers its hint at once; otherwise after a while of trying.
        NextHintAt=Failures().FindRef(Stage.Id)>0||HintLevel>0?0.f:FirstHintSeconds;
        Rebuild();
        if(const auto* S=UProjectProject01GameUserSettings::Get())
        {Keys[0]=S->GetMoveForwardKey();Keys[1]=S->GetMoveBackwardKey();Keys[2]=S->GetMoveLeftKey();Keys[3]=S->GetMoveRightKey();}
        Init(InHost,TEXT("steps"),Run,Ms);
    }
private:
    FStepStage Stage;FStepState State;TArray<FString> Moves;EStepResult Last=EStepResult::Continue;TFunction<void(const FString&)> NextStage;int32 StageCount=13;
    // Hints come in three steps, each a bit more of the answer: 1 shows where the mannequins would go for the cell under
    // the mouse, 2 marks one cell the answer passes through, 3 shows the next move (once per press). Any hint caps the
    // stage at two stars.
    static constexpr float FirstHintSeconds=25.f,HintGapSeconds=10.f;
    int32 HintLevel=0,HintsUsed=0;float NextHintAt=FirstHintSeconds;FIntPoint KeyCell=FIntPoint(-1,-1),Hover=FIntPoint(-1,-1);FString NextMove;
    static TMap<FString,int32>& Failures(){static TMap<FString,int32> Map;return Map;} // stages lost this session
    FKey Keys[4]={EKeys::W,EKeys::S,EKeys::A,EKeys::D};
    FString Title()const override{return FString::Printf(TEXT("한 칸씩 %s. %s"),*Stage.Id,*Stage.Name);}
    FString Hud()const override
    {
        return FString::Printf(TEXT("행동 %d · 기준 %d · 셔터 %d\n"),State.Actions,Stage.Par,State.ShuttersLeft)+Hint();
    }
    /** One line on what matters right now: being caught, a door to shut, or why the yellow-ringed mannequin stays put. */
    FString Hint()const
    {
        if(!NextMove.IsEmpty())return TEXT("힌트: 빛나는 화살표 쪽이 다음 한 수입니다.");
        if(HintLevel>=2&&KeyCell.X>=0)return TEXT("힌트: 정답은 별표 칸을 지나갑니다. 그 칸까지 어떻게 갈지 생각해 보세요.");
        if(HintLevel>=1)return TEXT("힌트: 옆 칸에 마우스를 올리면, 그리로 갔을 때 마네킹이 어디로 오는지 보입니다.");
        FStepState Probe=State;
        if(State.ShuttersLeft>0&&Probe.Apply(TEXT("S"))!=EStepResult::Invalid)return TEXT("바로 앞이 문입니다. 스페이스로 셔터를 내리면 벽이 되어 막힙니다.");
        if(State.ShuttersLeft>0)return TEXT("문(빨간 줄)을 바라보고 스페이스: 셔터로 길을 막을 수 있습니다.");
        if(State.Watched()!=INDEX_NONE)return TEXT("노란 테두리 마네킹은 내 시선에 묶여 멈춥니다. 나머지는 내가 움직일 때마다 한 칸 다가옵니다.");
        return TEXT("내가 움직일 때마다 마네킹도 한 칸 다가옵니다. Q/E로 돌아서 바라보면(공짜) 멈춥니다.");
    }
    FHowTo HowTo()const override
    {
        return {TEXT("초록색 출구 칸까지 가세요. 적은 행동일수록 별이 많습니다."),
            ProjectGamepad::UsingPad()?FString::Printf(TEXT("십자 키: 한 칸 이동 · %s/%s: 제자리 돌기(공짜) · %s: 앞 문에 셔터 · %s 되돌리기 · %s 처음부터"),
                *ProjectGamepad::FixedLabel(EKeys::Gamepad_LeftShoulder),*ProjectGamepad::FixedLabel(EKeys::Gamepad_RightShoulder),*ProjectGamepad::FixedLabel(EKeys::Gamepad_FaceButton_Left),
                *ProjectGamepad::FixedLabel(EKeys::Gamepad_Special_Left),*ProjectGamepad::FixedLabel(EKeys::Gamepad_FaceButton_Top)):
            FString::Printf(TEXT("%s/%s/%s/%s·방향키 또는 옆 칸 클릭: 한 칸 이동 · Q/E 또는 먼 칸 클릭: 그쪽 보기(공짜) · 스페이스: 앞 문에 셔터 · Z 되돌리기 · R 처음부터"),
                *Keys[0].GetDisplayName().ToString(),*Keys[2].GetDisplayName().ToString(),*Keys[1].GetDisplayName().ToString(),*Keys[3].GetDisplayName().ToString()),
            TEXT("내가 걷거나 셔터를 내리면 마네킹이 모두 한 칸 다가옵니다. 내가 바라보는 줄의 첫 마네킹(노란 테두리)만 멈춥니다. 붙잡히면 처음부터입니다. 막히면 잠시 뒤 힌트가 열립니다.")};
    }
    FString RestartStage()const override{return Stage.Id;}
    void Rebuild(){Last=Replay(Stage,Moves,State);}
    void Do(const FString& Action)
    {
        if(Last!=EStepResult::Continue)return;
        FStepState Next=State;const EStepResult R=Next.Apply(Action);
        if(R==EStepResult::Invalid)return;
        const bool bMannequinsMoved=Next.Mannequins!=State.Mannequins;
        Moves.Add(Action);State=Next;Last=R;
        UProjectMiniGameSound::Play(Action==TEXT("S")?EProjectMiniGameSound::Shutter:R==EStepResult::Won?EProjectMiniGameSound::Good:R==EStepResult::Caught?EProjectMiniGameSound::Caught:EProjectMiniGameSound::Step);
        if(bMannequinsMoved&&R==EStepResult::Continue)UProjectMiniGameSound::Play(EProjectMiniGameSound::Creak);
        if(Action[0]!=TEXT('t'))NextMove.Reset(); // a shown next move is used up by the move after it
        if(HintLevel>=2)KeyCell=FindKeyCell();
        if(Moves.Num()%5==0&&R==EStepResult::Continue)SaveNow();
        if(R==EStepResult::Won)Win();
        if(R==EStepResult::Caught)Lose();
    }
    /** Caught: the stage is over. Start it again or go back to the list. */
    void Lose()
    {
        ++Failures().FindOrAdd(Stage.Id);Phase=EPhase::Busy;
        if(auto* Games=Subsystem())Games->Abandon(TEXT("steps"));
        ShowOver(TEXT("붙잡혔습니다"),FString::Printf(TEXT("%d번째 행동에서 마네킹에게 닿았습니다. 처음부터 다시 하면 힌트를 바로 쓸 수 있습니다."),State.Actions));
    }
    bool HintReady()const{return HintLevel<3||NextMove.IsEmpty();}
    float HintWait()const{return FMath::Max(0.f,NextHintAt-float(ActiveSeconds));}
    void UseHint()
    {
        if(Phase!=EPhase::Running||HintWait()>0||!HintReady())return;
        if(HintLevel<3)++HintLevel;
        if(HintLevel==2)KeyCell=FindKeyCell();
        if(HintLevel==3)
        {
            // The next move from here; a turn only ever matters in front of a shutter, so show it as "turn, then shutter".
            const TArray<FString> Path=SolveSteps(State);NextMove.Reset();
            int32 I=0;while(I<Path.Num()&&Path[I][0]==TEXT('t')&&!(I+1<Path.Num()&&Path[I+1]==TEXT("S")))++I;
            if(I<Path.Num())NextMove=Path[I][0]==TEXT('t')?Path[I].Mid(1)+TEXT("S"):Path[I]==TEXT("S")?FString(1,&State.Facing)+TEXT("S"):Path[I];
        }
        ++HintsUsed;NextHintAt=float(ActiveSeconds)+HintGapSeconds;SaveNow();
        UProjectMiniGameSound::Play(EProjectMiniGameSound::Pick);
    }
    /** A cell midway along the answer from here (or the shutter's doorway when the answer lowers one). */
    FIntPoint FindKeyCell()const
    {
        const TArray<FString> Path=SolveSteps(State);if(Path.IsEmpty())return FIntPoint(-1,-1);
        FStepState Walk=State;TArray<FIntPoint> Cells;
        for(const FString& M:Path)
        {
            if(M==TEXT("S"))return Walk.Player+FStepState::Delta(Walk.Facing);
            Walk.Apply(M);if(M[0]!=TEXT('t'))Cells.Add(Walk.Player);
        }
        return Cells.Num()>=3?Cells[Cells.Num()/2]:FIntPoint(-1,-1); // two steps from the exit is no puzzle left: no mark
    }
    void Undo(){if(!Moves.IsEmpty()){Moves.Pop();Rebuild();NextMove.Reset();if(HintLevel>=2)KeyCell=FindKeyCell();}}
    void Restart(){Moves.Reset();Rebuild();NextMove.Reset();if(HintLevel>=2)KeyCell=FindKeyCell();}
    static TCHAR TurnLeft(TCHAR F){return F==TEXT('U')?TEXT('L'):F==TEXT('L')?TEXT('D'):F==TEXT('D')?TEXT('R'):TEXT('U');}
    static TCHAR TurnRight(TCHAR F){return F==TEXT('U')?TEXT('R'):F==TEXT('R')?TEXT('D'):F==TEXT('D')?TEXT('L'):TEXT('U');}
    void Turn(TCHAR To){if(To!=State.Facing)Do(FString(TEXT("t"))+To);}
    void Win()
    {
        auto* Games=Subsystem();Phase=EPhase::Busy;const int32 Actions=State.Actions,Stars=Stage.Stars(Actions,HintsUsed);
        const TWeakPtr<SProjectStepsGame> Weak=StaticCastSharedRef<SProjectStepsGame>(AsShared());
        const FString Detail=FString::Printf(TEXT("행동 %d · 기준 %d · 별 %s"),Actions,Stage.Par,*(FString::ChrN(Stars,TEXT('★'))+FString::ChrN(3-Stars,TEXT('☆'))))+(HintsUsed>0?FString::Printf(TEXT(" · 힌트 %d번"),HintsUsed):FString());
        auto Finish=[Weak,Detail](const FString& Error)
        {
            auto Self=Weak.Pin();if(!Self)return;
            TArray<TPair<FString,TFunction<void()>>> Extra;
            const int32 Index=FCString::Atoi(*Self->Stage.Id);
            if(Index<Self->StageCount&&Self->NextStage){auto Next=Self->NextStage;const FString Id=FString::FromInt(Index+1);Extra.Add({TEXT("다음 스테이지"),[Next,Id]{Next(Id);}});}
            Self->ShowOver(TEXT("출구에 닿았습니다"),Error.IsEmpty()?Detail:Detail+TEXT("\n")+Error,Extra);
        };
        if(!Games){Finish(FString());return;}
        Games->Finish(RunId,true,ActiveMs(),0,Moves,[Finish](TSharedPtr<FJsonObject>,const FString& Error){Finish(Error);},0,HintsUsed);
    }
    int32 Dir(const FKey& K)const
    {
        for(int32 I=0;I<4;++I)if(K==Keys[I])return I;
        return K==EKeys::Up?0:K==EKeys::Down?1:K==EKeys::Left?2:K==EKeys::Right?3:INDEX_NONE;
    }
    bool IsGameKey(const FKey& K)const override{return Dir(K)!=INDEX_NONE||K==EKeys::Q||K==EKeys::E||K==EKeys::SpaceBar||K==EKeys::Z||K==EKeys::BackSpace||K==EKeys::R||K==EKeys::H;}
    bool Key(const FKeyEvent& E,bool bDown)override
    {
        if(!bDown)return IsGameKey(E.GetKey());
        const FKey K=E.GetKey();static const TCHAR Names[]={TEXT('U'),TEXT('D'),TEXT('L'),TEXT('R')};
        if(const int32 D=Dir(K);D!=INDEX_NONE){Do(FString(1,&Names[D]));return true;}
        if(K==EKeys::Q){Turn(TurnLeft(State.Facing));return true;}
        if(K==EKeys::E){Turn(TurnRight(State.Facing));return true;}
        if(K==EKeys::SpaceBar){Do(TEXT("S"));return true;}
        if(K==EKeys::Z||K==EKeys::BackSpace){Undo();return true;}
        if(K==EKeys::R){Restart();return true;}
        if(K==EKeys::H){UseHint();return true;}
        return false;
    }
    TSharedRef<SWidget> Controls()override
    {
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("왼쪽 보기"),[this]{if(Phase==EPhase::Running)Turn(TurnLeft(State.Facing));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("오른쪽 보기"),[this]{if(Phase==EPhase::Running)Turn(TurnRight(State.Facing));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("셔터"),[this]{if(Phase==EPhase::Running)Do(TEXT("S"));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("되돌리기"),[this]{if(Phase==EPhase::Running)Undo();})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[SNew(SButton).IsFocusable(false).ButtonStyle(&ProjectDFSDUI::ButtonStyle(false)).ContentPadding(FMargin(10,6)).HAlign(HAlign_Center)
                .IsEnabled_Lambda([this]{return Phase==EPhase::Running&&HintWait()<=0&&HintReady();})
                .OnClicked_Lambda([this]{UseHint();return FReply::Handled();})
                [SNew(STextBlock).Font(Font(12,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([this]
                {
                    if(HintWait()>0)return FText::FromString(FString::Printf(TEXT("힌트 %d초"),FMath::CeilToInt(HintWait())));
                    static const TCHAR* Names[]={TEXT("힌트 1 · 예고 (H)"),TEXT("힌트 2 · 열쇠 칸 (H)"),TEXT("힌트 3 · 다음 한 수 (H)"),TEXT("다음 한 수 (H)")};
                    return FText::FromString(Names[FMath::Clamp(HintLevel,0,3)]);
                })]];
    }
    TSharedRef<FJsonObject> SaveState()const override
    {
        auto J=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> List;for(const FString& M:Moves)List.Add(MakeShared<FJsonValueString>(M));J->SetArrayField(TEXT("moves"),List);
        J->SetNumberField(TEXT("hints"),HintsUsed);J->SetNumberField(TEXT("hintLevel"),HintLevel);return J;
    }
    void Cell(const FGeometry& G,float& Size,FVector2D& Origin)const
    {
        const int32 W=State.Grid.Num()?State.Grid[0].Len():1,H=FMath::Max(1,State.Grid.Num());const FVector2D S=G.GetLocalSize();
        Size=FMath::Min(S.X/W,S.Y/H);Origin=(S-FVector2D(W,H)*Size)*.5f;
    }
    void BoardPress(const FGeometry& G,const FPointerEvent& E,bool bDown)override
    {
        if(!bDown)return;
        float Size;FVector2D Origin;Cell(G,Size,Origin);const FVector2D P=(Local(G,E)-Origin)/Size;
        const FIntPoint C(FMath::FloorToInt(P.X),FMath::FloorToInt(P.Y)),D=C-State.Player;
        const TCHAR To=D.X==0?(D.Y<0?TEXT('U'):TEXT('D')):D.Y==0?(D.X<0?TEXT('L'):TEXT('R')):0;
        if(!To||D==FIntPoint::ZeroValue)return;
        if(FMath::Abs(D.X)+FMath::Abs(D.Y)==1)Do(FString(1,&To));else Turn(To); // next cell: step; further in a line: look that way
    }
    void BoardMove(const FGeometry& G,const FPointerEvent& E)override
    {
        float Size;FVector2D Origin;Cell(G,Size,Origin);const FVector2D P=(Local(G,E)-Origin)/Size;
        Hover=FIntPoint(FMath::FloorToInt(P.X),FMath::FloorToInt(P.Y));
    }
    int32 PaintBoard(const FDraw& D)const override
    {
        float Size;FVector2D Origin;Cell(D.G,Size,Origin);
        auto At=[&](FIntPoint C){return Origin+FVector2D(C.X,C.Y)*Size;};
        const TArray<FIntPoint> Sight=State.SightLine();const int32 Watched=State.Watched();
        for(int32 Y=0;Y<State.Grid.Num();++Y)for(int32 X=0;X<State.Grid[Y].Len();++X)
        {
            const TCHAR C=State.Grid[Y][X];const FVector2D P=At(FIntPoint(X,Y));
            FLinearColor Fill=C==TEXT('#')?FLinearColor(.035f,.033f,.03f,1.f):FLinearColor(.1f,.095f,.085f,1.f);
            if(C==TEXT('E'))Fill=FLinearColor(.12f,.3f,.2f,1.f);
            if(Sight.Contains(FIntPoint(X,Y)))Fill=Fill+FLinearColor(.08f,.075f,.04f,0.f);
            D.Box(P+FVector2D(1,1),FVector2D(Size-2,Size-2),Fill);
            if(C==TEXT('X'))
            {
                D.Line({P+FVector2D(Size*.15f,Size*.5f),P+FVector2D(Size*.85f,Size*.5f)},FLinearColor(.7f,.25f,.15f,.7f),2.f);
                D.Text(P+FVector2D(Size*.38f,Size*.12f),TEXT("문"),FMath::Max(8,int32(Size*.2f)),FLinearColor(.85f,.45f,.35f,.9f));
            }
            if(C==TEXT('C'))for(int32 I=0;I<4;++I)D.Box(P+FVector2D(2,2+I*Size*.24f),FVector2D(Size-4,Size*.12f),FLinearColor(.55f,.2f,.12f,1.f));
            if(C==TEXT('E'))D.Text(P+FVector2D(Size*.12f,Size*.25f),TEXT("출구"),FMath::Max(8,int32(Size*.26f)),FLinearColor(.75f,1.f,.85f,1.f),true);
        }
        const FDraw Top{D.Out,D.G,D.Layer+2};
        for(int32 I=0;I<State.Mannequins.Num();++I)
        {
            const FVector2D P=At(State.Mannequins[I]);
            Mannequin(Top,P+FVector2D(Size*.5f,Size*.95f),Size*.88f,0,I==Watched?0:2,I==Watched?FLinearColor(.55f,.52f,.47f,1.f):FLinearColor(.3f,.28f,.26f,1.f),Ink());
            if(I==Watched)Top.Ring(P+FVector2D(Size*.5f,Size*.5f),Size*.46f,FLinearColor(.95f,.85f,.5f,.9f),3.f); // frozen by the player's look (the tip line names the ring)
        }
        // Hint 1: for the neighbouring cell under the mouse, where each mannequin would step (red cross: caught there).
        if(HintLevel>=1&&Phase==EPhase::Running&&FMath::Abs(Hover.X-State.Player.X)+FMath::Abs(Hover.Y-State.Player.Y)==1)
        {
            const FIntPoint Step=Hover-State.Player;const TCHAR To=Step.X>0?TEXT('R'):Step.X<0?TEXT('L'):Step.Y>0?TEXT('D'):TEXT('U');
            FStepState Ahead=State;const EStepResult R=Ahead.Apply(FString(1,&To));
            if(R!=EStepResult::Invalid)
            {
                const FDraw Ghost{D.Out,D.G,D.Layer+3};
                for(int32 I=0;I<State.Mannequins.Num()&&I<Ahead.Mannequins.Num();++I)if(Ahead.Mannequins[I]!=State.Mannequins[I])
                {
                    const FVector2D From=At(State.Mannequins[I])+FVector2D(Size*.5f,Size*.5f),Into=At(Ahead.Mannequins[I])+FVector2D(Size*.5f,Size*.5f);
                    Ghost.Line({From,Into},FLinearColor(.95f,.55f,.35f,.8f),3.f);Ghost.Ring(Into,Size*.22f,FLinearColor(.95f,.55f,.35f,.8f),2.f);
                }
                const FVector2D C=At(Hover)+FVector2D(Size*.5f,Size*.5f);
                if(R==EStepResult::Caught){const float A=Size*.25f;Ghost.Line({C-FVector2D(A,A),C+FVector2D(A,A)},FLinearColor(.95f,.3f,.25f,1.f),4.f);Ghost.Line({C+FVector2D(-A,A),C+FVector2D(A,-A)},FLinearColor(.95f,.3f,.25f,1.f),4.f);}
                else Ghost.Ring(C,Size*.3f,FLinearColor(.6f,.85f,1.f,.7f),2.f);
            }
        }
        // Hint 2: one cell the answer passes through. Hint 3: the next move as an arrow from the player.
        if(HintLevel>=2&&KeyCell.X>=0){const FVector2D C=At(KeyCell)+FVector2D(Size*.5f,Size*.5f);Top.Ring(C,Size*.4f,FLinearColor(.55f,.95f,.65f,.9f),3.f);Top.Text(C-FVector2D(Size*.12f,Size*.2f),TEXT("★"),FMath::Max(10,int32(Size*.3f)),FLinearColor(.55f,.95f,.65f,1.f),true);}
        if(!NextMove.IsEmpty())
        {
            const FIntPoint Dir=FStepState::Delta(NextMove[0]);const FVector2D C=At(State.Player)+FVector2D(Size*.5f,Size*.5f),Tip=C+FVector2D(Dir.X,Dir.Y)*Size*.85f;
            Top.Line({C,Tip},FLinearColor(.55f,.95f,.65f,1.f),5.f);Top.Ring(Tip,Size*.12f,FLinearColor(.55f,.95f,.65f,1.f),4.f);
            if(NextMove.Len()>1)Top.Text(Tip+FVector2D(Size*.15f,-Size*.4f),TEXT("셔터"),FMath::Max(9,int32(Size*.2f)),FLinearColor(.55f,.95f,.65f,1.f),true);
        }
        const FVector2D Me=At(State.Player)+FVector2D(Size*.5f,Size*.5f);const FIntPoint F=FStepState::Delta(State.Facing);
        const FVector2D Fwd(F.X,F.Y),Side(-F.Y,F.X);
        Top.Circle(Me,Size*.3f,FLinearColor(.55f,.75f,.85f,1.f));
        Top.Line({Me+Fwd*Size*.42f,Me+Side*Size*.16f,Me-Side*Size*.16f,Me+Fwd*Size*.42f},FLinearColor(.9f,.95f,1.f,1.f),2.f);
        return D.Layer+4;
    }
};

// ======================================================================== 어디서 났지?
class SProjectListenGame final:public SProjectMiniGameView
{
public:
    SLATE_BEGIN_ARGS(SProjectListenGame){}
    SLATE_END_ARGS()
    void Construct(const FArguments&,const FHost& InHost,const FString& Run,int32 Ms,const FListenRound& InRound,const TSharedPtr<FJsonObject>& Saved)
    {
        Round=InRound;FString Stage;
        if(Saved&&Saved->TryGetStringField(TEXT("phase"),Stage)&&Stage==TEXT("answer")){Step=EStep::Answer;Saved->TryGetBoolField(TEXT("replayed"),bReplayed);}
        Init(InHost,TEXT("listen"),Run,Ms);
    }
private:
    enum class EStep:uint8{Ready,Playing,Answer,Waiting,Reveal};
    FListenRound Round;EStep Step=EStep::Ready;float StepTime=1.f,Clock=0;TSet<int32> Played;int32 Chosen=INDEX_NONE,Correct=INDEX_NONE,Hover=INDEX_NONE;
    bool bReplayed=false,bRight=false;TSharedPtr<FJsonObject> NextRound;
    static constexpr float RevealSeconds=1.6f;
    FString Hud()const override
    {
        const FString Lives=FString::ChrN(Round.Lives,TEXT('●'))+FString::ChrN(FMath::Max(0,3-Round.Lives),TEXT('○'));
        const FString What=Step==EStep::Ready?TEXT("잘 들으세요"):Step==EStep::Playing?TEXT("듣는 중..."):Step==EStep::Answer?TEXT("삐걱 소리가 난 쪽을 고르세요"):TEXT("");
        return FString::Printf(TEXT("%d라운드 · %d점 · 목숨 %s   %s"),Round.Round,Round.Score,*Lives,*What);
    }
    FHowTo HowTo()const override
    {
        return {TEXT("마네킹 관절이 삐걱거린 방향을 맞히세요(가운데가 나, 위쪽이 앞)."),
            TEXT("8방향 칸 클릭 또는 W 앞 · E 오른쪽 앞 · D 오른쪽 · C 오른쪽 뒤 · X 뒤 · Z 왼쪽 뒤 · A 왼쪽 · Q 왼쪽 앞 · R 다시 듣기(라운드마다 한 번)"),
            TEXT("이어폰을 권합니다. 뒤에서 난 소리는 더 먹먹합니다. 3라운드부터 배관·셔터 소리가 섞이니 삐걱 소리만 쫓으세요.")};
    }
    int32 KeyDirection(const FKey& K)const
    {
        static const FKey Keys[8]={EKeys::W,EKeys::E,EKeys::D,EKeys::C,EKeys::X,EKeys::Z,EKeys::A,EKeys::Q};
        static const FKey Pad[8]={EKeys::NumPadEight,EKeys::NumPadNine,EKeys::NumPadSix,EKeys::NumPadThree,EKeys::NumPadTwo,EKeys::NumPadOne,EKeys::NumPadFour,EKeys::NumPadSeven};
        for(int32 I=0;I<8;++I)if(K==Keys[I]||K==Pad[I])return I;
        return INDEX_NONE;
    }
    bool IsGameKey(const FKey& K)const override{return KeyDirection(K)!=INDEX_NONE||K==EKeys::R;}
    bool Key(const FKeyEvent& E,bool bDown)override
    {
        if(!bDown)return IsGameKey(E.GetKey());
        if(E.GetKey()==EKeys::R){Replay();return true;}
        if(const int32 D=KeyDirection(E.GetKey());D!=INDEX_NONE){Answer(D);return true;}
        return false;
    }
    TSharedRef<SWidget> Controls()override{return SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("다시 듣기 (라운드마다 한 번)"),[this]{Replay();})];}
    void Replay(){if(Step==EStep::Answer&&!bReplayed){bReplayed=true;BeginPlaying();}}
    void BeginPlaying(){Step=EStep::Playing;Clock=0;Played.Reset();}
    float PlayLength()const
    {
        float End=0;
        for(const auto& S:Round.Sounds)End=FMath::Max(End,S.Delay+UProjectMiniGameSound::Length(Kind(S))*Round.Length/.9f);
        return End+.25f;
    }
    static EProjectMiniGameSound Kind(const FListenSound& S){return S.Kind==TEXT("knock")?EProjectMiniGameSound::Knock:S.Kind==TEXT("rattle")?EProjectMiniGameSound::Rattle:EProjectMiniGameSound::Creak;}
    void TickGame(float Dt)override
    {
        if(Step==EStep::Ready){StepTime-=Dt;if(StepTime<=0)BeginPlaying();}
        else if(Step==EStep::Playing)
        {
            Clock+=Dt;
            for(int32 I=0;I<Round.Sounds.Num();++I)if(!Played.Contains(I)&&Clock>=Round.Sounds[I].Delay)
            {
                Played.Add(I);const auto& S=Round.Sounds[I];
                // Decoys are a touch louder than the creak, so loudness alone does not give the answer away.
                UProjectMiniGameSound::PlayFrom(Kind(S),S.Direction*45.f,Kind(S)==EProjectMiniGameSound::Creak?Round.Gain:FMath::Min(1.f,Round.Gain*1.15f),Round.Length/.9f);
            }
            if(Clock>=PlayLength()){Step=EStep::Answer;SaveNow();}
        }
        else if(Step==EStep::Reveal)
        {
            StepTime-=Dt;
            if(StepTime<=0&&NextRound&&FListenRound::Parse(NextRound,Round)){Chosen=Correct=INDEX_NONE;bReplayed=false;Step=EStep::Ready;StepTime=1.f;}
        }
    }
    void OnPaused()override{if(Step==EStep::Playing||Step==EStep::Ready){Step=EStep::Answer;bReplayed=false;}} // the replay is still there after a pause
    TSharedRef<FJsonObject> SaveState()const override{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("phase"),TEXT("answer"));J->SetBoolField(TEXT("replayed"),bReplayed);return J;}
    void Answer(int32 Direction)
    {
        if(Phase!=EPhase::Running||Step!=EStep::Answer)return;
        auto* Games=Subsystem();if(!Games)return;
        Step=EStep::Waiting;Chosen=Direction;UProjectMiniGameSound::Play(EProjectMiniGameSound::Pick);
        const TWeakPtr<SProjectListenGame> Weak=StaticCastSharedRef<SProjectListenGame>(AsShared());
        Games->ListenAnswer(RunId,Round.Round,Direction,ActiveMs(),[Weak](TSharedPtr<FJsonObject> Json,const FString& Error)
        {
            auto Self=Weak.Pin();if(!Self)return;
            if(!Error.IsEmpty()||!Json){Self->Step=EStep::Answer;Self->ShowError(Error);return;}
            double Dir=0,Cleared=0,Score=0,Lives=0;bool bOver=false;
            Json->TryGetBoolField(TEXT("correct"),Self->bRight);Json->TryGetNumberField(TEXT("direction"),Dir);Json->TryGetBoolField(TEXT("over"),bOver);
            Json->TryGetNumberField(TEXT("cleared"),Cleared);Json->TryGetNumberField(TEXT("score"),Score);Json->TryGetNumberField(TEXT("lives"),Lives);
            Self->Correct=int32(Dir);Self->Round.Cleared=int32(Cleared);Self->Round.Score=int32(Score);Self->Round.Lives=int32(Lives);
            const TSharedPtr<FJsonObject>* Next=nullptr;Self->NextRound=Json->TryGetObjectField(TEXT("listen"),Next)?*Next:nullptr;
            Self->Step=EStep::Reveal;Self->StepTime=RevealSeconds;
            UProjectMiniGameSound::Play(Self->bRight?EProjectMiniGameSound::Good:EProjectMiniGameSound::Bad);
            if(bOver)Self->ShowOver(TEXT("게임 끝"),FString::Printf(TEXT("%d라운드 통과 · %d점"),int32(Cleared),int32(Score)));
        });
    }
    // Eight pads around the listener; pad 0 straight up (ahead), clockwise.
    void Ring(const FGeometry& G,FVector2D& Center,float& Radius,float& Pad)const
    {const FVector2D S=G.GetLocalSize();Center=S*.5f;Radius=FMath::Min(S.X,S.Y)*.36f;Pad=FMath::Min(S.X,S.Y)*.075f;}
    static FVector2D PadAt(FVector2D C,float R,int32 I){const float A=FMath::DegreesToRadians(I*45.f);return C+FVector2D(FMath::Sin(A),-FMath::Cos(A))*R;}
    int32 HitPad(const FGeometry& G,FVector2D P)const
    {
        FVector2D C;float R,Pad;Ring(G,C,R,Pad);
        for(int32 I=0;I<8;++I)if(FVector2D::Distance(P,PadAt(C,R,I))<=Pad*1.15f)return I;
        return INDEX_NONE;
    }
    void BoardMove(const FGeometry& G,const FPointerEvent& E)override{Hover=HitPad(G,Local(G,E));}
    void BoardPress(const FGeometry& G,const FPointerEvent& E,bool bDown)override{if(bDown)if(const int32 I=HitPad(G,Local(G,E));I!=INDEX_NONE)Answer(I);}
    int32 PaintBoard(const FDraw& D)const override
    {
        FVector2D C;float R,Pad;Ring(D.G,C,R,Pad);
        D.Box(FVector2D::ZeroVector,D.G.GetLocalSize(),FLinearColor(.012f,.012f,.013f,1.f));
        D.Ring(C,R,FLinearColor(1,1,1,.06f),1.f);
        const FDraw Top{D.Out,D.G,D.Layer+2};
        for(int32 I=0;I<8;++I)
        {
            const FVector2D P=PadAt(C,R,I);FLinearColor Fill(1,1,1,Step==EStep::Answer?(I==Hover?.16f:.07f):.04f);
            if(Step==EStep::Reveal||Step==EStep::Waiting)
            {
                if(I==Correct)Fill=FLinearColor(.3f,.85f,.45f,.75f);
                else if(I==Chosen)Fill=Step==EStep::Waiting?ProjectDFSDUI::Accent().CopyWithNewOpacity(.6f):FLinearColor(.9f,.3f,.25f,.7f);
            }
            Top.Circle(P,Pad,Fill);
            Top.Text(P+FVector2D(-Pad*.9f,Pad*1.05f),FListenRound::DirectionName(I),FMath::Max(8,int32(Pad*.32f)),ProjectDFSDUI::Muted());
        }
        // The listener seen from above: head with a nose toward "ahead".
        Top.Circle(C,Pad*.7f,FLinearColor(.55f,.75f,.85f,1.f));
        Top.Line({C+FVector2D(0,-Pad*1.0f),C+FVector2D(-Pad*.25f,-Pad*.55f),C+FVector2D(Pad*.25f,-Pad*.55f),C+FVector2D(0,-Pad*1.0f)},FLinearColor(.9f,.95f,1.f,1.f),2.f);
        if(Step==EStep::Playing||Step==EStep::Ready)Top.Text(C+FVector2D(-Pad*1.6f,Pad*1.1f),Step==EStep::Ready?TEXT("곧 소리가 납니다"):TEXT("듣는 중..."),FMath::Max(9,int32(Pad*.34f)),ProjectDFSDUI::Text());
        if(Step==EStep::Reveal)Top.Text(FVector2D(12,10),bRight?TEXT("맞았습니다"):TEXT("틀렸습니다 · 초록이 정답"),15,bRight?FLinearColor(.3f,.85f,.45f,1.f):FLinearColor(.95f,.4f,.3f,1.f),true);
        return D.Layer+4;
    }
};

// ======================================================================== 다가가기 (the mannequin's game)
class SProjectApproachGame final:public SProjectMiniGameView
{
public:
    SLATE_BEGIN_ARGS(SProjectApproachGame){}
    SLATE_END_ARGS()
    void Construct(const FArguments&,const FHost& InHost,const FString& Run,int32 Ms,const TSharedPtr<FJsonObject>& Saved)
    {
        if(!Sim.Load(Saved))Sim.Start(uint32(GetTypeHash(Run)));
        if(const auto* S=UProjectProject01GameUserSettings::Get())Forward=S->GetMoveForwardKey();
        Init(InHost,TEXT("approach"),Run,Ms);
    }
private:
    FApproachSim Sim;FKey Forward=EKeys::W;bool bKey=false,bMouse=false;float StepWait=0,RedFade=0,LastPhaseTime=0;FApproachSim::EPhase LastPhase=FApproachSim::EPhase::Away;
    FString Hud()const override{return FString::Printf(TEXT("거리 %d%% · 들킴 %d/%d · %.1f초"),FMath::RoundToInt(Sim.Distance),Sim.Strikes,FApproachSim::MaxStrikes,ActiveSeconds);}
    FHowTo HowTo()const override
    {
        return {TEXT("당신은 마네킹입니다. 등을 돌린 생존자에게 들키지 않고 닿으세요."),
            ProjectGamepad::UsingPad()?FString::Printf(TEXT("십자 위나 %s를 누르고 있으면 다가가고, 떼면 멈춥니다."),*ProjectGamepad::FixedLabel(EKeys::Gamepad_FaceButton_Left)):
            FString::Printf(TEXT("%s(또는 위 방향키·스페이스·마우스)를 누르고 있으면 다가가고, 떼면 멈춥니다."),*Forward.GetDisplayName().ToString()),
            TEXT("어깨가 움찔하고 숨을 들이쉬면 곧 돌아봅니다. 바로 멈추세요. 빛이 비출 때 움직이면 밀려나고, 세 번이면 끝. 돌아보는 척도 합니다.")};
    }
    bool IsGameKey(const FKey& K)const override{return K==Forward||K==EKeys::Up||K==EKeys::SpaceBar;}
    bool Key(const FKeyEvent& E,bool bDown)override{if(IsGameKey(E.GetKey())){bKey=bDown;return true;}return false;}
    void OnPaused()override{bKey=bMouse=false;}
    void BoardPress(const FGeometry&,const FPointerEvent& E,bool bDown)override{if(E.GetEffectingButton()==EKeys::LeftMouseButton)bMouse=bDown;}
    void TickGame(float Dt)override
    {
        bool bSpotted=false;const auto Before=Sim.Phase;
        const auto Outcome=Sim.Tick(Dt,bKey||bMouse,bSpotted);
        RedFade=FMath::Max(0.f,RedFade-Dt);
        // Sounds: the tell (a breath, also on a feint), the torch clicking on as it turns, steps while walking.
        if(Sim.Phase!=Before)
        {
            if(Sim.Phase==FApproachSim::EPhase::Tell)UProjectMiniGameSound::Play(EProjectMiniGameSound::Breath);
            if(Sim.Phase==FApproachSim::EPhase::Turning)UProjectMiniGameSound::Play(EProjectMiniGameSound::Switch);
        }
        if(bSpotted){RedFade=.8f;UProjectMiniGameSound::Play(EProjectMiniGameSound::Bad);}
        StepWait-=Dt;if(Sim.bMoving&&!Sim.Watching()&&StepWait<=0){StepWait=.45f;UProjectMiniGameSound::Play(EProjectMiniGameSound::Step);}
        if(Outcome==FApproachSim::EOutcome::Playing)return;
        const bool bReached=Outcome==FApproachSim::EOutcome::Reached;
        UProjectMiniGameSound::Play(bReached?EProjectMiniGameSound::Good:EProjectMiniGameSound::Caught);
        Phase=EPhase::Busy;const int32 Ms=ActiveMs(),Strikes=Sim.Strikes;
        const TWeakPtr<SProjectApproachGame> Weak=StaticCastSharedRef<SProjectApproachGame>(AsShared());
        auto Show=[Weak,bReached,Ms,Strikes](const FString& Error)
        {
            auto Self=Weak.Pin();if(!Self)return;
            if(!bReached){Self->ShowOver(TEXT("세 번 들켰습니다"),TEXT("어깨가 움찔하고 숨소리가 들리면 멈추세요. 가끔은 돌아보는 척만 합니다."));return;}
            Self->ShowOver(TEXT("생존자 뒤에 닿았습니다"),Error.IsEmpty()?FString::Printf(TEXT("%.1f초 · 들킴 %d번"),Ms/1000.f,Strikes):Error);
        };
        auto* Games=Subsystem();if(!Games){Show(FString());return;}
        Games->Finish(RunId,bReached,Ms,0,{},[Show](TSharedPtr<FJsonObject>,const FString& Error){Show(Error);},Strikes);
    }
    TSharedRef<FJsonObject> SaveState()const override{return Sim.Save();}
    int32 PaintBoard(const FDraw& D)const override
    {
        const FVector2D S=D.G.GetLocalSize();
        ProjectDFSDUI::PaintStoreBackdrop(D.Out,D.G,D.Layer,FLinearColor(1.f,1.f,1.f,1.f));
        const FDraw Top{D.Out,D.G,D.Layer+3};
        const float Near=float(Sim.Distance/FApproachSim::Goal),H=S.Y*FMath::Lerp(.22f,.95f,Near*Near);
        const FVector2D Foot(S.X*.5f,S.Y*FMath::Lerp(.62f,1.18f,Near));
        const bool bFacing=Sim.Phase==FApproachSim::EPhase::Looking;
        // The tell: the shoulders twitch (a small sway, not a flash).
        float Sway=0;if(Sim.Phase==FApproachSim::EPhase::Tell)Sway=FMath::Sin(float(Sim.PhaseLength-Sim.PhaseLeft)*28.f)*H*.025f;
        const float Turn=Sim.Phase==FApproachSim::EPhase::Turning?float(1-Sim.PhaseLeft/Sim.PhaseLength):bFacing?1.f:0.f;
        // The survivor: a dark coat seen from behind, narrower while turning; facing, a pale face and the torch.
        const FLinearColor Coat(.018f,.02f,.024f,1.f),Skin(.24f,.16f,.11f,1.f); // dark, like everything in the closed store
        const float Width=H*.26f*(1-.45f*FMath::Sin(Turn*PI));
        Top.Disc(Foot-FVector2D(-Sway,H*.9f),H*.075f,bFacing?Skin:Coat); // back of the head: as dark as the coat
        Top.Box(Foot-FVector2D(Width*.5f-Sway,H*.8f),FVector2D(Width,H*.42f),Coat);
        Top.Box(Foot-FVector2D(Width*.38f,H*.4f),FVector2D(Width*.32f,H*.4f),Coat);Top.Box(Foot-FVector2D(-Width*.06f,H*.4f),FVector2D(Width*.32f,H*.4f),Coat);
        if(Turn>0)
        {
            // Torch light toward the viewer, fading in as it turns round.
            const float Glow=FMath::Clamp(Turn,0.f,1.f);const FVector2D Lamp=Foot-FVector2D(-Width*.45f,H*.55f);
            for(int32 Ring=6;Ring>=1;--Ring)Top.Circle(Lamp,H*.08f*Ring,FLinearColor(1.f,.92f,.7f,.05f*Glow));
            D.Box(FVector2D::ZeroVector,S,FLinearColor(1.f,.95f,.8f,.08f*Glow*(.4f+Near)));
        }
        // The mannequin's own pale hands at the bottom edge, swaying while it walks.
        const float Bob=Sim.bMoving&&!Sim.Watching()?FMath::Sin(float(Sim.Elapsed)*9.f)*S.Y*.012f:0.f;
        const FDraw Hands{D.Out,D.G,D.Layer+5};const FLinearColor Plastic(.2f,.19f,.18f,1.f),Joint(.08f,.075f,.07f,1.f);
        for(const float Side:{-1.f,1.f})
        {
            // A forearm rising from the bottom edge, a jointed wrist and a flat, fingerless plastic hand.
            const FVector2D Elbow(S.X*(.5f+Side*.34f),S.Y+6),Wrist(S.X*(.5f+Side*.25f),S.Y*.86f+Bob*Side);
            Hands.Line({Elbow,Wrist},Plastic,S.Y*.045f);Hands.Circle(Wrist,S.Y*.024f,Joint);
            Hands.Line({Wrist,Wrist+FVector2D(-Side*S.Y*.03f,-S.Y*.07f)},Plastic,S.Y*.05f);
        }
        // Distance to the survivor, and a red wash when seen moving.
        Hands.Box(FVector2D(S.X*.1f,8),FVector2D(S.X*.8f,5),FLinearColor(1,1,1,.1f));
        Hands.Box(FVector2D(S.X*.1f,8),FVector2D(S.X*.8f*Near,5),ProjectDFSDUI::Accent());
        if(RedFade>0)Hands.Box(FVector2D::ZeroVector,S,FLinearColor(.6f,.05f,.04f,.3f*RedFade/.8f));
        if(Sim.Watching())Hands.Text(FVector2D(12,18),TEXT("보고 있습니다 · 멈추세요"),14,FLinearColor(1.f,.85f,.6f,1.f),true);
        return D.Layer+7;
    }
};

// ======================================================================== the folder
// ======================================================================== 셔터 내려
/**
 * Three aisles. Mannequins run from the dark end toward the player; dropping an aisle's fire shutter (1/2/3 or a click)
 * stops a runner that hits it while it is down. Tell: a creak as each runner sets off, and the runner itself. Counter:
 * the shutter, held down for a moment. Weak spot: after it lifts, the motor needs a second before it can drop again, so
 * dropping early wastes it. Runners come faster and closer together, later in pairs.
 */
class SProjectShutterGame final:public SProjectMiniGameView
{
public:
    SLATE_BEGIN_ARGS(SProjectShutterGame){}
    SLATE_END_ARGS()
    void Construct(const FArguments&,const FHost& InHost,const FString& Run,int32 Ms,const TSharedPtr<FJsonObject>& Saved)
    {
        Random=uint32(GetTypeHash(Run))|1u;
        if(Saved)
        {
            double V=0;
            if(Saved->TryGetNumberField(TEXT("score"),V))Score=int32(V);
            if(Saved->TryGetNumberField(TEXT("lives"),V))Lives=FMath::Clamp(int32(V),1,MaxLives);
            if(Saved->TryGetNumberField(TEXT("random"),V))Random=uint32(V)|1u;
        }
        Init(InHost,TEXT("shutter"),Run,Ms);
    }
private:
    struct FRunner{int32 Lane=0;float X=1.f;bool bStopped=false;float Fade=0;};
    static constexpr int32 Lanes=3,MaxLives=3;
    static constexpr float ShutterX=.24f,ClosedSeconds=.8f,MotorSeconds=1.f,ReachX=.04f;
    TArray<FRunner> Runners;float Closed[Lanes]={},Motor[Lanes]={},SpawnIn=1.2f,Hurt=0;int32 Score=0,Lives=MaxLives;uint32 Random=1;
    float Speed()const{return FMath::Min(.75f,.3f+Score*.012f);}
    float Gap()const{return FMath::Max(.5f,1.5f-Score*.03f);}
    float Next01(){Random=Random*1664525u+1013904223u;return float(Random>>8)/float(1u<<24);}
    FString Hud()const override
    {
        return FString::Printf(TEXT("막음 %d · 목숨 %s · %.1f초"),Score,*(FString::ChrN(Lives,TEXT('●'))+FString::ChrN(MaxLives-Lives,TEXT('○'))),ActiveSeconds);
    }
    FHowTo HowTo()const override
    {
        return {TEXT("통로 세 곳으로 마네킹이 달려옵니다. 셔터에 닿는 순간 셔터가 내려가 있으면 막습니다. 세 명이 지나가면 끝입니다."),
            TEXT("1 · 2 · 3 (위·가운데·아래 통로) 또는 통로를 눌러 셔터 내리기"),
            TEXT("셔터는 잠깐만 내려가 있고, 올라간 뒤 1초 동안 다시 못 내립니다. 너무 일찍 내리면 놓칩니다. 출발할 때 삐걱 소리가 납니다.")};
    }
    int32 LaneOf(const FKey& K)const
    {
        return K==EKeys::One||K==EKeys::NumPadOne?0:K==EKeys::Two||K==EKeys::NumPadTwo?1:K==EKeys::Three||K==EKeys::NumPadThree?2:INDEX_NONE;
    }
    bool IsGameKey(const FKey& K)const override{return LaneOf(K)!=INDEX_NONE;}
    bool Key(const FKeyEvent& E,bool bDown)override{const int32 L=LaneOf(E.GetKey());if(L==INDEX_NONE)return false;if(bDown&&!E.IsRepeat())Drop(L);return true;}
    void Drop(int32 Lane)
    {
        if(Closed[Lane]>0||Motor[Lane]>0)return;
        Closed[Lane]=ClosedSeconds;UProjectMiniGameSound::Play(EProjectMiniGameSound::Shutter);
    }
    void Area(const FGeometry& G,FVector2D& Origin,FVector2D& Size,float& LaneH)const
    {
        const FVector2D S=G.GetLocalSize();Origin=FVector2D(8,8);Size=S-FVector2D(16,16);LaneH=Size.Y/Lanes;
    }
    void BoardPress(const FGeometry& G,const FPointerEvent& E,bool bDown)override
    {
        if(!bDown)return;FVector2D O,S;float H;Area(G,O,S,H);
        const int32 Lane=FMath::FloorToInt((Local(G,E).Y-O.Y)/H);if(Lane>=0&&Lane<Lanes)Drop(Lane);
    }
    void TickGame(float Dt)override
    {
        Hurt=FMath::Max(0.f,Hurt-Dt);
        for(int32 L=0;L<Lanes;++L)
        {
            if(Closed[L]>0){Closed[L]-=Dt;if(Closed[L]<=0){Closed[L]=0;Motor[L]=MotorSeconds;}}
            else Motor[L]=FMath::Max(0.f,Motor[L]-Dt);
        }
        SpawnIn-=Dt;
        if(SpawnIn<=0)
        {
            SpawnIn=Gap()*(.8f+.4f*Next01());const int32 Lane=FMath::Min(Lanes-1,int32(Next01()*Lanes));
            Runners.Add({Lane,1.f});
            if(Score>=12&&Next01()<.35f)Runners.Add({Lane,1.13f}); // a second one close behind: one long drop covers both
            UProjectMiniGameSound::PlayFrom(EProjectMiniGameSound::Creak,0.f,.5f+.25f*Lane,1.f);
        }
        for(FRunner& R:Runners)
        {
            if(R.bStopped){R.Fade+=Dt;continue;}
            const float Before=R.X;R.X-=Speed()*Dt;
            if(Before>ShutterX&&R.X<=ShutterX&&Closed[R.Lane]>0){R.X=ShutterX+.01f;R.bStopped=true;++Score;UProjectMiniGameSound::Play(EProjectMiniGameSound::Knock);}
            else if(R.X<=ReachX){R.bStopped=true;R.Fade=1.f;--Lives;Hurt=.6f;UProjectMiniGameSound::Play(EProjectMiniGameSound::Bad);}
        }
        Runners.RemoveAll([](const FRunner& R){return R.bStopped&&R.Fade>=.6f;});
        if(Lives>0)return;
        UProjectMiniGameSound::Play(EProjectMiniGameSound::Caught);
        Phase=EPhase::Busy;const int32 Ms=ActiveMs(),Stopped=Score;
        const TWeakPtr<SProjectShutterGame> Weak=StaticCastSharedRef<SProjectShutterGame>(AsShared());
        auto Show=[Weak,Stopped,Ms](const FString& Error){if(auto Self=Weak.Pin())Self->ShowOver(TEXT("세 명이 지나갔습니다"),FString::Printf(TEXT("%d명 막음 · %.1f초"),Stopped,Ms/1000.f)+(Error.IsEmpty()?FString():TEXT("\n")+Error));};
        auto* Games=Subsystem();if(!Games){Show(FString());return;}
        Games->Finish(RunId,false,Ms,0,{},[Show](TSharedPtr<FJsonObject>,const FString& Error){Show(Error);},0,0,Stopped);
    }
    // A pause keeps the score and lives; the aisles start empty again after the countdown.
    void OnPaused()override{Runners.Reset();SpawnIn=1.2f;for(int32 L=0;L<Lanes;++L){Closed[L]=0;Motor[L]=0;}}
    TSharedRef<FJsonObject> SaveState()const override
    {
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("score"),Score);J->SetNumberField(TEXT("lives"),Lives);J->SetNumberField(TEXT("random"),double(Random));return J;
    }
    int32 PaintBoard(const FDraw& D)const override
    {
        FVector2D O,S;float H;Area(D.G,O,S,H);
        D.Box(O,S,FLinearColor(.02f,.019f,.018f,1.f));
        const FDraw Top{D.Out,D.G,D.Layer+2};
        for(int32 L=0;L<Lanes;++L)
        {
            const FVector2D LaneO=O+FVector2D(0,H*L);
            // The aisle fades into the dark toward where the runners come from.
            for(int32 I=0;I<8;++I)D.Box(LaneO+FVector2D(S.X*I/8.f,3),FVector2D(S.X/8.f+1,H-6),FLinearColor(.09f,.085f,.075f,1.f)*(1.f-I*.1f)+FLinearColor(0,0,0,1.f)*(I*.1f));
            const float Sx=O.X+S.X*ShutterX;
            Top.Text(FVector2D(Sx+10,LaneO.Y+6),FString::FromInt(L+1),16,FLinearColor(.85f,.82f,.75f,.9f),true); // the key that drops this shutter
            if(Closed[L]>0)for(int32 I=0;I<5;++I)Top.Box(FVector2D(Sx-6,LaneO.Y+4+I*(H-8)/5.f),FVector2D(12,(H-8)/5.f-3),FLinearColor(.6f,.22f,.12f,1.f));
            else
            {
                Top.Line({FVector2D(Sx,LaneO.Y+4),FVector2D(Sx,LaneO.Y+H-4)},FLinearColor(.6f,.22f,.12f,Motor[L]>0?.25f:.7f),2.f);
                if(Motor[L]>0)Top.Box(FVector2D(Sx-14,LaneO.Y+H-10),FVector2D(28*(1.f-Motor[L]/MotorSeconds),4),FLinearColor(.75f,.7f,.6f,.7f)); // motor recharging
            }
        }
        for(const FRunner& R:Runners)
        {
            const float A=R.bStopped?FMath::Clamp(1.f-R.Fade/.6f,0.f,1.f):1.f;
            const FVector2D Foot(O.X+S.X*R.X,O.Y+H*(R.Lane+.92f));
            Mannequin(Top,Foot,H*.8f,0,1,FLinearColor(.5f,.47f,.42f,A),FLinearColor(.03f,.028f,.025f,A)); // arms up, reaching
        }
        // The player at the near end of the aisles; a red wash for a moment when one gets through (no flashing).
        Top.Disc(FVector2D(O.X+S.X*.03f,O.Y+S.Y*.5f),FMath::Min(14.f,H*.2f),FLinearColor(.55f,.75f,.85f,1.f));
        if(Hurt>0)Top.Box(O,S,FLinearColor(.6f,.05f,.03f,.25f*Hurt/.6f));
        return D.Layer+4;
    }
};

// ======================================================================== 흉내 내기
/**
 * The player hides as one of five mannequins in a dark shop window. A torch sweeps the row from the left; every
 * mannequin stands in the same pose, and the player must stand in it too by the time the light reaches them. While it is
 * dark again the mannequins take a new pose. Tell: the light shows each pose as it passes the others first. Counter: set
 * head (Q/W/E) and arms (A/S/D). Weak spot: the sweep speeds up and, later, the dark hides the poses until lit.
 */
class SProjectMimicGame final:public SProjectMiniGameView
{
public:
    SLATE_BEGIN_ARGS(SProjectMimicGame){}
    SLATE_END_ARGS()
    void Construct(const FArguments&,const FHost& InHost,const FString& Run,int32 Ms,const TSharedPtr<FJsonObject>& Saved)
    {
        Random=uint32(GetTypeHash(Run))|1u;
        if(Saved)
        {
            double V=0;
            if(Saved->TryGetNumberField(TEXT("score"),V))Score=int32(V);
            if(Saved->TryGetNumberField(TEXT("lives"),V))Lives=FMath::Clamp(int32(V),1,MaxLives);
            if(Saved->TryGetNumberField(TEXT("random"),V))Random=uint32(V)|1u;
        }
        NewPose();
        Init(InHost,TEXT("mimic"),Run,Ms);
    }
private:
    static constexpr int32 Count=5,Me=3,MaxLives=3;
    static constexpr float DarkSeconds=.6f;
    int32 Head=0,Arm=0,MyHead=0,MyArm=0,Score=0,Lives=MaxLives;uint32 Random=1;
    float Beam=-.15f,Dark=DarkSeconds,Hurt=0;bool bChecked=false,bLastGood=true;
    float SweepSeconds()const{return FMath::Max(1.3f,4.f-Score*.15f);}
    float DarkAlpha()const{return Score<6?.5f:Score<12?.25f:.1f;} // later the dark hides the poses until the light passes
    float Next01(){Random=Random*1664525u+1013904223u;return float(Random>>8)/float(1u<<24);}
    void NewPose()
    {
        const int32 OldHead=Head,OldArm=Arm;
        do{Head=FMath::Min(2,int32(Next01()*3));Arm=FMath::Min(2,int32(Next01()*3));}while(Head==OldHead&&Arm==OldArm);
    }
    static float SlotX(int32 I){return .12f+I*.19f;}
    FString Hud()const override
    {
        return FString::Printf(TEXT("버팀 %d · 목숨 %s · %.1f초"),Score,*(FString::ChrN(Lives,TEXT('●'))+FString::ChrN(MaxLives-Lives,TEXT('○'))),ActiveSeconds);
    }
    FHowTo HowTo()const override
    {
        return {TEXT("파란 옷이 나입니다. 손전등이 나에게 오기 전에 다른 마네킹과 같은 자세를 하세요. 세 번 들키면 끝입니다."),
            TEXT("고개: Q 왼쪽 · W 정면 · E 오른쪽 / 팔: A 내림 · S 만세 · D 한쪽 뻗기 (내 머리·몸을 눌러도 바뀜)"),
            TEXT("불이 꺼진 사이 마네킹들이 자세를 바꿉니다. 빛이 왼쪽 마네킹들을 먼저 비추니 그때 보고 따라 하세요. 점점 빨라지고 어두워집니다.")};
    }
    bool IsGameKey(const FKey& K)const override{return K==EKeys::Q||K==EKeys::W||K==EKeys::E||K==EKeys::A||K==EKeys::S||K==EKeys::D;}
    bool Key(const FKeyEvent& E,bool bDown)override
    {
        const FKey K=E.GetKey();if(!IsGameKey(K))return false;if(!bDown||E.IsRepeat())return true;
        if(K==EKeys::Q)MyHead=1;else if(K==EKeys::W)MyHead=0;else if(K==EKeys::E)MyHead=2;
        else if(K==EKeys::A)MyArm=0;else if(K==EKeys::S)MyArm=1;else MyArm=2;
        UProjectMiniGameSound::Play(EProjectMiniGameSound::Pick);return true;
    }
    void Window(const FGeometry& G,FVector2D& O,FVector2D& S)const{S=G.GetLocalSize()-FVector2D(16,16);O=FVector2D(8,8);}
    void BoardPress(const FGeometry& G,const FPointerEvent& E,bool bDown)override
    {
        if(!bDown)return;FVector2D O,S;Window(G,O,S);const FVector2D P=Local(G,E);const float H=S.Y*.62f,Foot=O.Y+S.Y*.86f;
        if(FMath::Abs(P.X-(O.X+S.X*SlotX(Me)))>H*.25f||P.Y>Foot||P.Y<Foot-H)return;
        if(P.Y<Foot-H*.82f)MyHead=(MyHead+1)%3;else MyArm=(MyArm+1)%3; // the head cycles its way; the body cycles the arms
        UProjectMiniGameSound::Play(EProjectMiniGameSound::Pick);
    }
    void TickGame(float Dt)override
    {
        Hurt=FMath::Max(0.f,Hurt-Dt);
        if(Dark>0){Dark-=Dt;return;}
        Beam+=Dt/SweepSeconds();
        if(!bChecked&&Beam>=SlotX(Me))
        {
            bChecked=true;bLastGood=MyHead==Head&&MyArm==Arm;
            if(bLastGood){++Score;UProjectMiniGameSound::Play(EProjectMiniGameSound::Good);}
            else{--Lives;Hurt=.8f;UProjectMiniGameSound::Play(EProjectMiniGameSound::Bad);}
        }
        if(Lives<=0)
        {
            UProjectMiniGameSound::Play(EProjectMiniGameSound::Caught);
            Phase=EPhase::Busy;const int32 Ms=ActiveMs(),Held=Score;
            const TWeakPtr<SProjectMimicGame> Weak=StaticCastSharedRef<SProjectMimicGame>(AsShared());
            auto Show=[Weak,Held,Ms](const FString& Error){if(auto Self=Weak.Pin())Self->ShowOver(TEXT("들켰습니다"),FString::Printf(TEXT("%d번 버팀 · %.1f초"),Held,Ms/1000.f)+(Error.IsEmpty()?FString():TEXT("\n")+Error));};
            auto* Games=Subsystem();if(!Games){Show(FString());return;}
            Games->Finish(RunId,false,Ms,0,{},[Show](TSharedPtr<FJsonObject>,const FString& Error){Show(Error);},0,0,Held);return;
        }
        if(Beam>=1.15f){Beam=-.15f;Dark=DarkSeconds;bChecked=false;NewPose();UProjectMiniGameSound::Play(EProjectMiniGameSound::Creak);}
    }
    // After a pause the sweep starts over from the dark (a new pose), so a paused screen is never studied for free.
    void OnPaused()override{Beam=-.15f;Dark=DarkSeconds;bChecked=false;NewPose();}
    TSharedRef<FJsonObject> SaveState()const override
    {
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("score"),Score);J->SetNumberField(TEXT("lives"),Lives);J->SetNumberField(TEXT("random"),double(Random));return J;
    }
    int32 PaintBoard(const FDraw& D)const override
    {
        FVector2D O,S;Window(D.G,O,S);
        D.Box(O,S,FLinearColor(.015f,.014f,.013f,1.f));
        D.Box(O+FVector2D(0,S.Y*.86f),FVector2D(S.X,S.Y*.14f),FLinearColor(.05f,.045f,.04f,1.f)); // the window's floor
        const float BeamX=O.X+S.X*Beam,BeamW=S.X*.16f;
        const FDraw Light{D.Out,D.G,D.Layer+1};
        if(Dark<=0)for(int32 I=0;I<12;++I)
        {
            const float T=(I+.5f)/12.f,Edge=1.f-FMath::Abs(T-.5f)*2.f;
            Light.Box(FVector2D(BeamX-BeamW*.5f+BeamW*I/12.f,O.Y),FVector2D(BeamW/12.f+1,S.Y),FLinearColor(1.f,.9f,.62f,.12f*Edge));
        }
        const FDraw Top{D.Out,D.G,D.Layer+3};
        static const FLinearColor Garments[]={{.32f,.05f,.04f,1.f},{.16f,.19f,.04f,1.f},{.42f,.27f,.03f,1.f},{.04f,.07f,.24f,1.f},{.2f,.05f,.24f,1.f}};
        const float H=S.Y*.62f,Foot=O.Y+S.Y*.86f;
        for(int32 I=0;I<Count;++I)
        {
            const float X=O.X+S.X*SlotX(I);
            const bool bLit=Dark<=0&&FMath::Abs(X-BeamX)<BeamW*.5f;
            const float A=I==Me?1.f:bLit?1.f:DarkAlpha();
            const bool bMine=I==Me;
            FLinearColor Body(.42f,.4f,.37f,A),Edge(.03f,.028f,.025f,A),Cloth=bMine?FLinearColor(.1f,.3f,.6f,1.f):Garments[I]*FLinearColor(1,1,1,A);
            if(Dark>0&&!bMine){Body.A=Edge.A=Cloth.A=.06f;} // the change itself happens unseen
            Mannequin(Top,FVector2D(X,Foot),H,bMine?MyHead:Head,bMine?MyArm:Arm,Body,Edge,&Cloth);
            if(bMine)Top.Text(FVector2D(X-8,Foot+6),TEXT("나"),12,FLinearColor(.6f,.8f,1.f,1.f),true);
        }
        if(bChecked&&Beam<1.15f)Top.Text(FVector2D(O.X+12,O.Y+10),bLastGood?TEXT("통과"):TEXT("들켰습니다"),16,bLastGood?FLinearColor(.3f,.85f,.45f,1.f):FLinearColor(.95f,.4f,.3f,1.f),true);
        if(Hurt>0)Top.Box(O,S,FLinearColor(.6f,.05f,.03f,.25f*Hurt/.8f));
        return D.Layer+5;
    }
};

class SProjectMiniGameFolder final:public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectMiniGameFolder){}
    SLATE_END_ARGS()
    void Construct(const FArguments&,TWeakObjectPtr<UGameInstance> InGame,FSimpleDelegate InBack,FSimpleDelegate InPlay)
    {
        Game=InGame;Back=InBack;OnPlay=InPlay;
        ChildSlot[SAssignNew(Screen,SBox)];
        ShowList();Reload();
    }
    virtual ~SProjectMiniGameFolder() override{if(Playing)Playing->Leave();}
private:
    TWeakObjectPtr<UGameInstance> Game;FSimpleDelegate Back,OnPlay;TSharedPtr<SBox> Screen;TSharedPtr<SProjectMiniGameView> Playing;
    FString Status;TArray<FStepStage> Stages;
    UProjectMiniGames* Games()const{auto* GI=Game.Get();return GI?GI->GetSubsystem<UProjectMiniGames>():nullptr;}
    TSharedPtr<FJsonObject> Home()const{auto* G=Games();return G?G->Home:nullptr;}
    void Reload()
    {
        auto* G=Games();if(!G){Status=TEXT("미니게임을 불러올 수 없습니다.");return;}
        Status=TEXT("불러오는 중...");
        const TWeakPtr<SProjectMiniGameFolder> Weak=StaticCastSharedRef<SProjectMiniGameFolder>(AsShared());
        G->Refresh([Weak](TSharedPtr<FJsonObject> Json,const FString& Error)
        {
            auto Self=Weak.Pin();if(!Self)return;
            Self->Status=Error;Self->Stages.Reset();
            const TArray<TSharedPtr<FJsonValue>>* List=nullptr;
            if(Json&&Json->TryGetArrayField(TEXT("stages"),List))for(const auto& V:*List){FStepStage S;const TSharedPtr<FJsonObject>* O=nullptr;if(V->TryGetObject(O)&&FStepStage::Parse(*O,S))Self->Stages.Add(S);}
            if(!Self->Playing)Self->ShowList();
        });
    }
    TSharedPtr<FJsonObject> OpenRun(const FString& GameId,const FString& Stage=FString())const
    {
        const TArray<TSharedPtr<FJsonValue>>* Runs=nullptr;if(!Home()||!Home()->TryGetArrayField(TEXT("runs"),Runs))return nullptr;
        for(const auto& V:*Runs){const TSharedPtr<FJsonObject>* O=nullptr;FString G,S;if(V->TryGetObject(O)&&(*O)->TryGetStringField(TEXT("game"),G)&&G==GameId&&(Stage.IsEmpty()||((*O)->TryGetStringField(TEXT("stage"),S)&&S==Stage)))return *O;}
        return nullptr;
    }
    FString Best(const FString& GameId,const FString& Board,bool bWeekly=false)const
    {
        const TArray<TSharedPtr<FJsonValue>>* List=nullptr;if(!Home()||!Home()->TryGetArrayField(TEXT("best"),List))return FString();
        for(const auto& V:*List)
        {
            const TSharedPtr<FJsonObject>* O=nullptr;if(!V->TryGetObject(O))continue;
            FString G,B;bool W=false;double P=0,S=0;(*O)->TryGetStringField(TEXT("game"),G);(*O)->TryGetStringField(TEXT("board"),B);(*O)->TryGetBoolField(TEXT("weekly"),W);
            if(G==GameId&&B==Board&&W==bWeekly){(*O)->TryGetNumberField(TEXT("primary"),P);(*O)->TryGetNumberField(TEXT("secondary"),S);return Record(G,B,int32(P),int32(S));}
        }
        return FString();
    }
    TSharedRef<SWidget> Header(const FString& Title,TFunction<void()> OnBack)
    {
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SButton).IsFocusable(false).ButtonStyle(&FCoreStyle::Get(),TEXT("NoBorder")).ContentPadding(FMargin(8,4)).OnClicked_Lambda([OnBack]{OnBack();return FReply::Handled();})
                [SNew(STextBlock).Font(Font(18)).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(TEXT("<")))]]
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Font(Font(18,TEXT("Bold"))).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(Title))];
    }
    void Stop(){if(Playing){Playing->Leave();Playing.Reset();}}
    void ShowList()
    {
        Stop();
        auto List=SNew(SVerticalBox);
        struct FCard{FString Id,Desc;};
        for(const FCard& Card:{FCard{TEXT("spot"),TEXT("불이 꺼진 사이 움직인 마네킹을 찾으세요.")},FCard{TEXT("blackout"),TEXT("손전등으로 마네킹을 멈춰 두고 퓨즈 3개를 끼우세요.")},FCard{TEXT("steps"),TEXT("한 칸씩, 보고 있는 마네킹은 멈춥니다. 출구까지 가세요.")},
            FCard{TEXT("listen"),TEXT("어둠 속 삐걱 소리, 어느 쪽에서 났을까요? 이어폰을 권합니다.")},FCard{TEXT("approach"),TEXT("마네킹이 되어, 생존자가 돌아보지 않을 때만 다가가세요.")},
            FCard{TEXT("shutter"),TEXT("마네킹이 통로를 달려옵니다. 닿기 직전에 방화 셔터를 내리세요.")},FCard{TEXT("mimic"),TEXT("마네킹 사이에 숨었습니다. 손전등이 오기 전에 같은 자세를 하세요.")}})
        {
            const FString Id=Card.Id;const bool bOpen=Id!=TEXT("steps")&&OpenRun(Id).IsValid();
            const FString Mine=Best(Id,Id==TEXT("steps")?TEXT("stars"):TEXT("all"));
            auto Buttons=SNew(SHorizontalBox);
            if(Id==TEXT("steps"))Buttons->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("스테이지"),[this]{ShowStages();},true)];
            else
            {
                if(bOpen)Buttons->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("이어하기"),[this,Id]{Resume(Id);},true)];
                Buttons->AddSlot().FillWidth(1).Padding(2)[Button(bOpen?TEXT("새로 시작"):TEXT("시작"),[this,Id]{Begin(Id,FString());},!bOpen)];
            }
            Buttons->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("순위"),[this,Id]{ShowBoard(Id,Id==TEXT("steps")?TEXT("stars"):TEXT("all"),true);})];
            auto Box=SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[Label(GameName(Id),15,true)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,2)[Label(Card.Desc,11,false,ProjectDFSDUI::Muted())]
                +SVerticalBox::Slot().AutoHeight().Padding(0,2,0,6)[Label(Mine.IsEmpty()?TEXT("최고 기록 없음"):TEXT("최고 기록 ")+Mine,11,false,ProjectDFSDUI::Accent())]
                +SVerticalBox::Slot().AutoHeight()[Buttons];
            if(Id==TEXT("blackout"))
            {
                // 무한 모드: its own saved run and board, so it never ends the normal run.
                const bool bEndlessOpen=OpenRun(TEXT("endless")).IsValid();const FString EndlessBest=Best(TEXT("endless"),TEXT("all"));
                Box->AddSlot().AutoHeight().Padding(0,8,0,2)[Label(EndlessBest.IsEmpty()?TEXT("무한 모드 · 기록 없음"):TEXT("무한 모드 최고 ")+EndlessBest,11,false,ProjectDFSDUI::Accent())];
                Box->AddSlot().AutoHeight()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(bEndlessOpen?TEXT("무한 이어하기"):TEXT("무한 모드"),[this]{Resume(TEXT("endless"));})]
                    +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("무한 순위"),[this]{ShowBoard(TEXT("endless"),TEXT("all"),true);})]];
            }
            List->AddSlot().AutoHeight().Padding(10,4)[SNew(SBorder).BorderImage(Round(12)).BorderBackgroundColor(FLinearColor(1,1,1,.05f)).Padding(FMargin(12,10))[Box]];
        }
        Screen->SetContent(SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(8,6,12,6)[Header(TEXT("오프라인 게임"),[this]{Back.ExecuteIfBound();})]
            +SVerticalBox::Slot().AutoHeight().Padding(18,0,18,4)[SNew(STextBlock).AutoWrapText(true).Font(Font(11)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]{return FText::FromString(Status.IsEmpty()?TEXT("통신이 안 돼도 할 수 있는 게임. 경기가 시작되면 멈추고 저장됩니다."):Status);})]
            +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[List]]);
    }
    void ShowStages()
    {
        Stop();
        auto List=SNew(SVerticalBox);
        for(const FStepStage& S:Stages)
        {
            const FString Id=S.Id,Mine=Best(TEXT("steps"),TEXT("stage-")+Id);const bool bOpen=OpenRun(TEXT("steps"),Id).IsValid();
            int32 Stars=0;if(!Mine.IsEmpty())Stars=S.Stars(FCString::Atoi(*Mine));
            List->AddSlot().AutoHeight().Padding(10,3)[SNew(SBorder).BorderImage(Round(10)).BorderBackgroundColor(FLinearColor(1,1,1,.05f)).Padding(FMargin(10,6))[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[Label(FString::Printf(TEXT("%s. %s"),*S.Id,*S.Name),13,true)]
                    +SVerticalBox::Slot().AutoHeight()[Label(FString::Printf(TEXT("%s  기준 %d수%s"),*(FString::ChrN(Stars,TEXT('★'))+FString::ChrN(3-Stars,TEXT('☆'))),S.Par,Mine.IsEmpty()?TEXT(""):*(TEXT(" · 최고 ")+Mine)),11,false,ProjectDFSDUI::Muted())]]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0)[Button(bOpen?TEXT("이어하기"):TEXT("시작"),[this,Id,bOpen]{if(bOpen)Resume(TEXT("steps"),Id);else Begin(TEXT("steps"),Id);},true)]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(TEXT("순위"),[this,Id]{ShowBoard(TEXT("steps"),TEXT("stage-")+Id,true);})]]];
        }
        if(Stages.IsEmpty())List->AddSlot().AutoHeight().Padding(20)[Label(Status.IsEmpty()?TEXT("스테이지를 불러오는 중입니다."):Status,12,false,ProjectDFSDUI::Muted())];
        Screen->SetContent(SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(8,6,12,6)[Header(TEXT("한 칸씩"),[this]{ShowList();})]
            +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[List]]);
    }
    SProjectMiniGameView::FHost Host()
    {
        const TWeakPtr<SProjectMiniGameFolder> Weak=StaticCastSharedRef<SProjectMiniGameFolder>(AsShared());
        SProjectMiniGameView::FHost H;H.Game=Game;
        // These run from the game's own buttons, so the switch waits for the next tick (outside the click).
        H.ToList=[Weak]{if(auto S=Weak.Pin())S->Pending=[S]{S->Reload();S->ShowList();};};
        H.Restart=[Weak](const FString& G,const FString& Stage){if(auto S=Weak.Pin())S->Pending=[S,G,Stage]{S->Begin(G,Stage);};};
        H.Board=[Weak](const FString& G){if(auto S=Weak.Pin())S->Pending=[S,G]{S->ShowBoard(G,G==TEXT("steps")?TEXT("stars"):TEXT("all"),true);};};
        return H;
    }
    TFunction<void()> Pending;
    virtual void Tick(const FGeometry& G,const double Time,const float Delta)override{SCompoundWidget::Tick(G,Time,Delta);if(auto Run=MoveTemp(Pending)){Pending=nullptr;Run();}}
    void Begin(const FString& GameId,const FString& Stage)
    {
        Stop();auto* G=Games();if(!G)return;
        Screen->SetContent(SNew(SBox).Padding(24).VAlign(VAlign_Center)[Label(TEXT("시작하는 중..."),12,false,ProjectDFSDUI::Muted())]);
        const TWeakPtr<SProjectMiniGameFolder> Weak=StaticCastSharedRef<SProjectMiniGameFolder>(AsShared());
        G->Start(GameId,Stage,[Weak](TSharedPtr<FJsonObject> Json,const FString& Error)
        {
            auto Self=Weak.Pin();if(!Self)return;
            const TSharedPtr<FJsonObject>* Run=nullptr;
            if(!Error.IsEmpty()||!Json||!Json->TryGetObjectField(TEXT("run"),Run)){Self->Status=Error.IsEmpty()?TEXT("시작하지 못했습니다."):Error;Self->ShowList();return;}
            Self->Play(*Run);
        });
    }
    void Resume(const FString& GameId,const FString& Stage=FString()){if(auto Run=OpenRun(GameId,Stage))Play(Run);else Begin(GameId,Stage);}
    void Play(const TSharedPtr<FJsonObject>& Run)
    {
        Stop();
        FString Id,GameId,Stage;double Ms=0;Run->TryGetStringField(TEXT("id"),Id);Run->TryGetStringField(TEXT("game"),GameId);Run->TryGetStringField(TEXT("stage"),Stage);Run->TryGetNumberField(TEXT("activeMs"),Ms);
        const TSharedPtr<FJsonObject>* StateObj=nullptr;TSharedPtr<FJsonObject> Saved=Run->TryGetObjectField(TEXT("state"),StateObj)?*StateObj:nullptr;
        // A newer save made in this session but not yet on the server wins.
        if(auto* G=Games())if(const auto* Local=G->LocalSaves.Find(Id);Local&&Local->Key>=int32(Ms)){Saved=Local->Value;Ms=Local->Key;}
        const auto H=Host();
        if(GameId==TEXT("spot"))
        {
            const TSharedPtr<FJsonObject>* Spot=nullptr;FSpotRound Round;
            if(!Run->TryGetObjectField(TEXT("spot"),Spot)||!FSpotRound::Parse(*Spot,Round)){Status=TEXT("판을 불러오지 못했습니다.");ShowList();return;}
            Playing=SNew(SProjectSpotGame,H,Id,int32(Ms),Round,Saved);
        }
        else if(GameId==TEXT("blackout")||GameId==TEXT("endless"))Playing=SNew(SProjectBlackoutGame,H,Id,int32(Ms),Saved,GameId==TEXT("endless"));
        else if(GameId==TEXT("shutter"))Playing=SNew(SProjectShutterGame,H,Id,int32(Ms),Saved);
        else if(GameId==TEXT("mimic"))Playing=SNew(SProjectMimicGame,H,Id,int32(Ms),Saved);
        else if(GameId==TEXT("approach"))Playing=SNew(SProjectApproachGame,H,Id,int32(Ms),Saved);
        else if(GameId==TEXT("listen"))
        {
            const TSharedPtr<FJsonObject>* View=nullptr;FListenRound Round;
            if(!Run->TryGetObjectField(TEXT("listen"),View)||!FListenRound::Parse(*View,Round)){Status=TEXT("판을 불러오지 못했습니다.");ShowList();return;}
            Playing=SNew(SProjectListenGame,H,Id,int32(Ms),Round,Saved);
        }
        else
        {
            const FStepStage* S=Stages.FindByPredicate([&](const FStepStage& X){return X.Id==Stage;});
            if(!S){Status=TEXT("스테이지를 불러오지 못했습니다.");ShowList();return;}
            const TWeakPtr<SProjectMiniGameFolder> Weak=StaticCastSharedRef<SProjectMiniGameFolder>(AsShared());
            Playing=SNew(SProjectStepsGame,H,Id,int32(Ms),SProjectStepsGame::FSetup{*S,Saved,[Weak](const FString& Next){if(auto F=Weak.Pin())F->Pending=[F,Next]{F->Begin(TEXT("steps"),Next);};},Stages.Num()});
        }
        // A fresh run opens on the how-to card (한 칸씩: only its first stage; later stages show their tip line instead).
        if(Ms<=0&&!Saved&&(GameId!=TEXT("steps")||Stage==TEXT("1")))Playing->ShowIntro();
        Screen->SetContent(Playing.ToSharedRef());OnPlay.ExecuteIfBound();
    }
    void ShowBoard(const FString& GameId,const FString& BoardId,bool bWeekly)
    {
        Stop();
        auto Rows=SNew(SVerticalBox);TSharedPtr<SVerticalBox> RowsPtr=Rows;
        auto Tabs=SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("주간"),[this,GameId,BoardId]{Pending=[this,GameId,BoardId]{ShowBoard(GameId,BoardId,true);};},bWeekly)]
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("전체"),[this,GameId,BoardId]{Pending=[this,GameId,BoardId]{ShowBoard(GameId,BoardId,false);};},!bWeekly)];
        FString Name=GameName(GameId);
        if(GameId==TEXT("steps"))Name+=BoardId==TEXT("stars")?TEXT(" · 별 합계"):TEXT(" · 스테이지 ")+BoardId.RightChop(6);
        Screen->SetContent(SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(8,6,12,2)[Header(Name+TEXT(" 순위"),[this,GameId]{if(GameId==TEXT("steps"))ShowStages();else ShowList();})]
            +SVerticalBox::Slot().AutoHeight().Padding(10,4)[Tabs]
            +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Rows]]);
        Rows->AddSlot().AutoHeight().Padding(18,8)[Label(TEXT("불러오는 중..."),11,false,ProjectDFSDUI::Muted())];
        auto* G=Games();if(!G)return;
        G->Leaderboard(GameId,BoardId,bWeekly,[RowsPtr,GameId,BoardId](TSharedPtr<FJsonObject> Json,const FString& Error)
        {
            if(!RowsPtr)return;RowsPtr->ClearChildren();
            if(!Error.IsEmpty()||!Json){RowsPtr->AddSlot().AutoHeight().Padding(18,8)[Label(Error,11,false,ProjectDFSDUI::Muted())];return;}
            auto Row=[&](int32 Rank,const FString& Who,const FString& Value,bool bMine)
            {
                RowsPtr->AddSlot().AutoHeight().Padding(10,2)[SNew(SBorder).BorderImage(Round(8)).BorderBackgroundColor(bMine?ProjectDFSDUI::Accent().CopyWithNewOpacity(.25f):FLinearColor(1,1,1,.04f)).Padding(FMargin(10,6))[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)[SNew(SBox).WidthOverride(30)[Label(FString::FromInt(Rank),12,true)]]
                    +SHorizontalBox::Slot().FillWidth(1)[Label(Who,12)]
                    +SHorizontalBox::Slot().AutoWidth()[Label(Value,12,false,ProjectDFSDUI::Muted())]]];
            };
            const TArray<TSharedPtr<FJsonValue>>* Top=nullptr;
            if(Json->TryGetArrayField(TEXT("top"),Top))for(const auto& V:*Top)
            {
                const TSharedPtr<FJsonObject>* O=nullptr;if(!V->TryGetObject(O))continue;
                double Rank=0,P=0,S=0;FString Name;bool bMine=false;(*O)->TryGetNumberField(TEXT("rank"),Rank);(*O)->TryGetStringField(TEXT("name"),Name);(*O)->TryGetNumberField(TEXT("primary"),P);(*O)->TryGetNumberField(TEXT("secondary"),S);(*O)->TryGetBoolField(TEXT("mine"),bMine);
                Row(int32(Rank),Name,Record(GameId,BoardId,int32(P),int32(S)),bMine);
            }
            if(!Top||Top->IsEmpty())RowsPtr->AddSlot().AutoHeight().Padding(18,8)[Label(TEXT("아직 기록이 없습니다."),11,false,ProjectDFSDUI::Muted())];
            const TSharedPtr<FJsonObject>* Me=nullptr;
            RowsPtr->AddSlot().AutoHeight().Padding(18,10,18,2)[Label(TEXT("내 순위"),11,true)];
            if(Json->TryGetObjectField(TEXT("me"),Me)&&Me->IsValid()){double Rank=0,P=0,S=0;(*Me)->TryGetNumberField(TEXT("rank"),Rank);(*Me)->TryGetNumberField(TEXT("primary"),P);(*Me)->TryGetNumberField(TEXT("secondary"),S);Row(int32(Rank),TEXT("나"),Record(GameId,BoardId,int32(P),int32(S)),true);}
            else RowsPtr->AddSlot().AutoHeight().Padding(18,2)[Label(TEXT("이 순위표에 아직 기록이 없습니다."),11,false,ProjectDFSDUI::Muted())];
        });
    }
};

TSharedRef<SWidget> ProjectMiniGameUI::MakeFolder(TWeakObjectPtr<UGameInstance> Game,FSimpleDelegate OnBack,FSimpleDelegate OnPlay)
{
    return SNew(SProjectMiniGameFolder,Game,OnBack,OnPlay);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Slate/WidgetRenderer.h"
#include "ProjectUIRenderCapture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
// Draws each game on a phone-sized screen to Saved/MiniGameQA for review (no server needed).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMiniGameRenderTest,"ProjectProject01.MiniGames.Render",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectMiniGameRenderTest::RunTest(const FString&)
{
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("MiniGameQA");IFileManager::Get().MakeDirectory(*Directory,true);
    FWidgetRenderer Renderer(false);FIntPoint Size(1100,560);bool bSaved=true;
    SProjectMiniGameView::FHost Host;Host.ToList=[]{};Host.Restart=[](const FString&,const FString&){};Host.Board=[](const FString&){};
    auto Capture=[&](const TSharedRef<SWidget>& Widget,const FString& Name,float Seconds)
    {
        for(float T=0;T<Seconds;T+=.1f)Widget->Tick(FGeometry::MakeRoot(FVector2D(Size),FSlateLayoutTransform()),T,.1f);
        auto* Target=ProjectUIRenderCapture::Create(Size);
        for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,Widget,1.f,FVector2D(Size),0);
        FlushRenderingCommands();TArray<FColor> Pixels;
        if(ProjectUIRenderCapture::Read(Target,Pixels)){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);bSaved&=FFileHelper::SaveArrayToFile(PNG,*(Directory/Name));}else bSaved=false;
        Target->ReleaseResource();
    };
    FSpotRound Round;Round.Round=4;Round.Score=375;Round.Lives=2;Round.Changes=2;
    for(int32 I=0;I<8;++I){FSpotFigure F;F.X=110+I*110;F.Y=300+(I*137)%560;F.Head=I%3;F.Arm=(I*2)%3;Round.Before.Add(F);if(I==2)F.X+=70;if(I==5)F.Arm=(F.Arm+1)%3;Round.After.Add(F);}
    Capture(SNew(SProjectSpotGame,Host,TEXT("qa"),0,Round,nullptr),TEXT("Spot_Memorize.png"),3.5f);
    auto Answer=MakeShared<FJsonObject>();Answer->SetStringField(TEXT("phase"),TEXT("answer"));
    Capture(SNew(SProjectSpotGame,Host,TEXT("qa"),0,Round,Answer),TEXT("Spot_Answer.png"),3.5f);
    Capture(SNew(SProjectBlackoutGame,Host,TEXT("qa-blackout"),0,nullptr),TEXT("Blackout.png"),5.f);
    {const TSharedRef<SProjectBlackoutGame> Intro=SNew(SProjectBlackoutGame,Host,TEXT("qa-blackout"),0,nullptr);Intro->ShowIntro();Capture(Intro,TEXT("Blackout_Intro.png"),.5f);}
    FStepStage Stage;Stage.Id=TEXT("10");Stage.Name=TEXT("폐점");Stage.Map={TEXT("########"),TEXT("#ME.M.M#"),TEXT("#...#..#"),TEXT("##.#...#"),TEXT("##.#...#"),TEXT("#XSX...#"),TEXT("########")};Stage.Facing=TEXT('R');Stage.Shutters=2;Stage.Par=15;
    auto Moves=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> List;for(const TCHAR* M:{TEXT("tL"),TEXT("S"),TEXT("U")})List.Add(MakeShared<FJsonValueString>(M));Moves->SetArrayField(TEXT("moves"),List);
    Capture(SNew(SProjectStepsGame,Host,TEXT("qa"),0,SProjectStepsGame::FSetup{Stage,Moves,nullptr}),TEXT("Steps.png"),3.5f);
    Capture(SNew(SProjectStepsGame,Host,TEXT("qa"),0,SProjectStepsGame::FSetup{Stage,nullptr,nullptr}),TEXT("Steps_Countdown.png"),.5f);
    FListenRound Listen;Listen.Round=3;Listen.Lives=2;Listen.Score=240;Listen.Sounds={{2,TEXT("creak"),.2f},{5,TEXT("knock"),.5f}};
    auto ListenAnswer=MakeShared<FJsonObject>();ListenAnswer->SetStringField(TEXT("phase"),TEXT("answer"));
    Capture(SNew(SProjectListenGame,Host,TEXT("qa"),0,Listen,ListenAnswer),TEXT("Listen.png"),3.5f);
    Capture(SNew(SProjectApproachGame,Host,TEXT("qa-approach"),0,nullptr),TEXT("Approach_Start.png"),3.5f);
    Capture(SNew(SProjectBlackoutGame,Host,TEXT("qa-endless"),0,nullptr,true),TEXT("Blackout_Endless.png"),5.f);
    Capture(SNew(SProjectShutterGame,Host,TEXT("qa-shutter"),0,nullptr),TEXT("Shutter.png"),6.f);
    Capture(SNew(SProjectMimicGame,Host,TEXT("qa-mimic"),0,nullptr),TEXT("Mimic.png"),5.2f);
    auto Close=MakeShared<FJsonObject>();Close->SetNumberField(TEXT("distance"),70);Close->SetNumberField(TEXT("elapsed"),12);Close->SetNumberField(TEXT("strikes"),1);
    Capture(SNew(SProjectApproachGame,Host,TEXT("qa-approach"),12000,Close),TEXT("Approach_Close.png"),3.5f);
    Size=FIntPoint(352,640); // upright again (the player turned the phone back): still playable
    Capture(SNew(SProjectBlackoutGame,Host,TEXT("qa-blackout"),0,nullptr),TEXT("Blackout_Portrait.png"),5.f);
    TestTrue(TEXT("Mini game screens render to images"),bSaved);
    return true;
}
#endif

#include "ProjectMiniGameWidgets.h"
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
    void Line(TArray<FVector2D> Points,const FLinearColor& C,float Thickness=1.f)const
    {FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,C,true,Thickness);}
    void Ring(FVector2D Center,float R,const FLinearColor& C,float Thickness=2.f)const
    {TArray<FVector2D> P;for(int32 I=0;I<=24;++I){const float A=I/24.f*2*PI;P.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*R);}Line(P,C,Thickness);}
    void Text(FVector2D P,const FString& S,int32 Size,const FLinearColor& C,bool bBold=false)const
    {FSlateDrawElement::MakeText(Out,Layer+1,G.ToPaintGeometry(FVector2D(400,Size*1.6f),FSlateLayoutTransform(P)),S,Font(Size,bBold?TEXT("Bold"):TEXT("Regular")),ESlateDrawEffect::None,C);}
};

/** A store mannequin standing with its feet at Foot. Head 0/1/2: facing front/left/right. Arm 0/1/2: down/raised/held out. */
void Mannequin(const FDraw& D,FVector2D Foot,float H,int32 Head,int32 Arm,const FLinearColor& Body,const FLinearColor& Edge)
{
    const float Top=Foot.Y-H,W=H*.22f;
    D.Circle(FVector2D(Foot.X,Top+H*.08f),H*.075f,Body);
    const float Face=Head==1?-H*.04f:Head==2?H*.04f:0.f; // where the face points
    D.Box(FVector2D(Foot.X+Face-H*.02f,Top+H*.07f),FVector2D(H*.04f,H*.025f),Edge);
    D.Box(FVector2D(Foot.X-H*.025f,Top+H*.15f),FVector2D(H*.05f,H*.04f),Body);
    D.Box(FVector2D(Foot.X-W*.5f,Top+H*.19f),FVector2D(W,H*.3f),Body);
    D.Box(FVector2D(Foot.X-W*.42f,Top+H*.48f),FVector2D(W*.84f,H*.08f),Body);
    D.Box(FVector2D(Foot.X-W*.4f,Top+H*.55f),FVector2D(W*.32f,H*.45f),Body);
    D.Box(FVector2D(Foot.X+W*.08f,Top+H*.55f),FVector2D(W*.32f,H*.45f),Body);
    const FVector2D L(Foot.X-W*.55f,Top+H*.21f),R(Foot.X+W*.55f,Top+H*.21f);const float T=FMath::Max(2.f,H*.055f);
    if(Arm==1){D.Line({L,L+FVector2D(-H*.08f,-H*.2f)},Body,T);D.Line({R,R+FVector2D(H*.08f,-H*.2f)},Body,T);}
    else if(Arm==2){D.Line({L,L+FVector2D(-H*.1f,H*.26f)},Body,T);D.Line({R,R+FVector2D(H*.28f,H*.02f)},Body,T);}
    else{D.Line({L,L+FVector2D(-H*.02f,H*.3f)},Body,T);D.Line({R,R+FVector2D(H*.02f,H*.3f)},Body,T);}
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
FString GameName(const FString& Game){return Game==TEXT("spot")?TEXT("움직였다"):Game==TEXT("blackout")?TEXT("정전 복구"):TEXT("한 칸씩");}
FString Record(const FString& Game,const FString& Board,int32 P,int32 S)
{
    if(Game==TEXT("spot"))return FString::Printf(TEXT("%d라운드 · %d점"),P,S);
    if(Game==TEXT("blackout"))return FString::Printf(TEXT("%.1f초 · 배터리 %d%%"),P/1000.f,S);
    if(Board==TEXT("stars"))return FString::Printf(TEXT("별 %d개 · %d수"),P,S);
    return FString::Printf(TEXT("%d수"),P);
}
}

// ======================================================================== shared game frame
/**
 * What every game shares: keyboard focus, pause (Esc or losing focus) with the board hidden, a 3-second countdown when
 * play resumes, active play time, saving on pause, and the pause-and-save hook for sign-out and map changes.
 */
class SProjectMiniGameView:public SCompoundWidget
{
public:
    enum class EPhase:uint8{Countdown,Running,Paused,Busy,Over};
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
            +SVerticalBox::Slot().AutoHeight().Padding(12,0,12,4)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]{return FText::FromString(Hud());})]
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
    void Leave(){if(Phase==EPhase::Running||Phase==EPhase::Countdown)Pause();else if(Phase==EPhase::Paused)SaveNow();if(auto* Games=Subsystem())Games->ClearActiveGame(this);}
    virtual bool SupportsKeyboardFocus()const override{return true;}
    virtual void OnFocusLost(const FFocusEvent& Event)override{SCompoundWidget::OnFocusLost(Event);if(Phase==EPhase::Running)Pause();}
    virtual FReply OnKeyDown(const FGeometry& G,const FKeyEvent& E)override
    {
        if(E.GetKey()==EKeys::Escape){if(Phase==EPhase::Running||Phase==EPhase::Countdown){Pause();return FReply::Handled();}return FReply::Unhandled();}
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
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(16,0,16,14)[Label(TEXT("멈춘 동안 화면은 가려지고, 시간은 기록에 들어가지 않습니다. 진행 상황은 저장되어 다른 PC에서도 이어할 수 있습니다."),11,false,ProjectDFSDUI::Muted())]
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
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(16,10)[Label(Help(),11,false,ProjectDFSDUI::Muted())];
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
    virtual FString Help()const=0;
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
    FString Help()const override{return TEXT("사진을 기억하세요. 불이 꺼졌다 켜지면 자리·고개·팔이 바뀐 마네킹을 모두 고르고 확인(스페이스)을 누릅니다. 세 번 틀리면 끝납니다.");}
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
        ProjectDFSDUI::PaintStoreBackdrop(D.Out,D.G,D.Layer,FLinearColor(1.6f,1.6f,1.6f,1.f));
        const FDraw Top{D.Out,D.G,D.Layer+3};
        const bool bAfter=Step!=EStep::Memorize;
        const auto& List=bAfter?Round.After:Round.Before;
        TArray<int32> Order;for(int32 I=0;I<List.Num();++I)Order.Add(I);
        Order.Sort([&](int32 A,int32 B){return List[A].Y<List[B].Y;});
        for(const int32 I:Order)
        {
            float H=0;const FVector2D Foot=FootOf(List[I],R,H);
            Mannequin(Top,Foot,H,List[I].Head,List[I].Arm,FLinearColor(.2f,.19f,.18f,1.f),FLinearColor(.05f,.045f,.04f,1.f));
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
    void Construct(const FArguments&,const FHost& InHost,const FString& Run,int32 Ms,const TSharedPtr<FJsonObject>& Saved)
    {
        if(!Sim.Load(Saved))Sim.Start(uint32(GetTypeHash(Run)));
        if(const auto* S=UProjectProject01GameUserSettings::Get())
        {Keys[0]=S->GetMoveForwardKey();Keys[1]=S->GetMoveBackwardKey();Keys[2]=S->GetMoveLeftKey();Keys[3]=S->GetMoveRightKey();}
        Init(InHost,TEXT("blackout"),Run,Ms);
    }
private:
    FBlackoutSim Sim;FBlackoutInput Input;bool Held[4]={},bInstallKey=false,bToggle=false;FKey Keys[4]={EKeys::W,EKeys::S,EKeys::A,EKeys::D};
    float Scale=1,CreakWait=0;FVector2D Offset=FVector2D::ZeroVector;
    FString Hud()const override
    {
        return FString::Printf(TEXT("배터리 %d%% · 퓨즈 %d/3 (들고 있음 %d) · %.1f초"),FMath::RoundToInt(Sim.Battery),Sim.Installed,Sim.Carried,ActiveSeconds);
    }
    FString Help()const override
    {
        return FString::Printf(TEXT("%s/%s/%s/%s 이동 · 마우스로 손전등 · F 또는 오른쪽 클릭 켜고 끄기 · 배전반에서 E를 누르고 있으면 퓨즈를 끼웁니다. 빛에 닿은 마네킹은 멈춥니다. 어둠 속 발자국을 조심하세요."),
            *Keys[0].GetDisplayName().ToString(),*Keys[2].GetDisplayName().ToString(),*Keys[1].GetDisplayName().ToString(),*Keys[3].GetDisplayName().ToString());
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
        if(K==EKeys::E){bInstallKey=bDown;return true;}
        if(K==EKeys::F){if(bDown&&!E.IsRepeat())bToggle=true;return true;}
        return false;
    }
    void OnPaused()override{for(bool& H:Held)H=false;bInstallKey=false;}
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
        Input.bHoldInstall=bInstallKey;Input.bToggleLight=bToggle;bToggle=false;
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
        for(int32 I=0;I<3;++I)Top.Box(P(FBlackoutSim::Board+FVector2D(-26+I*20,-9)),FVector2D(12,12)*S,I<Sim.Installed?FLinearColor(.95f,.75f,.25f,1.f):FLinearColor(.02f,.02f,.02f,1.f));
        if(Sim.bInstalling)Top.Ring(P(FBlackoutSim::Board),20*S,FLinearColor(.95f,.75f,.25f,.4f+.6f*float(Sim.InstallProgress/FBlackoutSim::InstallSeconds)),3.f);
        // Fuses glow faintly; mannequins only show in the light or right next to the player.
        for(const FVector2D& F:Sim.Fuses)Top.Circle(P(F),5*S,FLinearColor(.95f,.7f,.2f,Sim.Lit(F)?1.f:.18f));
        for(const auto& Print:Sim.Prints){const float A=FMath::Clamp(1.f-float(Print.Age/2.5),0.f,1.f)*.5f;Top.Circle(P(Print.Pos),2.5f*S,FLinearColor(.75f,.72f,.68f,A));}
        for(const auto& M:Sim.Mannequins)
        {
            const bool bLit=Sim.Lit(M.Pos),bNear=FVector2D::Distance(M.Pos,Sim.Player)<46;
            if(!bLit&&!bNear)continue;
            Mannequin(Top,P(M.Pos+FVector2D(0,10)),36*S,0,bLit?0:2,bLit?FLinearColor(.62f,.58f,.52f,1.f):FLinearColor(.16f,.15f,.14f,1.f),Ink());
        }
        Top.Circle(P(Sim.Player),9*S,FLinearColor(.55f,.75f,.85f,1.f));
        Top.Line({P(Sim.Player),P(Sim.Player+Sim.TorchDirection()*14)},FLinearColor(.9f,.9f,.8f,1.f),2.f);
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
    struct FSetup{FStepStage Stage;TSharedPtr<FJsonObject> Saved;TFunction<void(const FString&)> Next;};
    void Construct(const FArguments&,const FHost& InHost,const FString& Run,int32 Ms,const FSetup& Setup)
    {
        Stage=Setup.Stage;NextStage=Setup.Next;const TSharedPtr<FJsonObject>& Saved=Setup.Saved;
        const TArray<TSharedPtr<FJsonValue>>* List=nullptr;
        if(Saved&&Saved->TryGetArrayField(TEXT("moves"),List))for(const auto& V:*List)Moves.Add(V->AsString());
        Rebuild();
        if(const auto* S=UProjectProject01GameUserSettings::Get())
        {Keys[0]=S->GetMoveForwardKey();Keys[1]=S->GetMoveBackwardKey();Keys[2]=S->GetMoveLeftKey();Keys[3]=S->GetMoveRightKey();}
        Init(InHost,TEXT("steps"),Run,Ms);
    }
private:
    FStepStage Stage;FStepState State;TArray<FString> Moves;EStepResult Last=EStepResult::Continue;TFunction<void(const FString&)> NextStage;
    FKey Keys[4]={EKeys::W,EKeys::S,EKeys::A,EKeys::D};
    FString Title()const override{return FString::Printf(TEXT("한 칸씩 %s. %s"),*Stage.Id,*Stage.Name);}
    FString Hud()const override
    {
        return FString::Printf(TEXT("행동 %d · 기준 %d · 셔터 %d"),State.Actions,Stage.Par,State.ShuttersLeft)+(Last==EStepResult::Caught?TEXT(" · 붙잡혔습니다 (되돌리기 가능)"):TEXT(""));
    }
    FString Help()const override
    {
        return FString::Printf(TEXT("%s/%s/%s/%s 또는 방향키로 한 칸 이동(그쪽을 보게 됨) · Q/E 제자리에서 돌기(무료) · 스페이스 앞의 셔터 내리기 · Z 되돌리기 · R 처음부터. 내가 보고 있는 줄의 첫 마네킹은 움직이지 못합니다. 칸을 눌러도 이동하거나 돌 수 있습니다."),
            *Keys[0].GetDisplayName().ToString(),*Keys[2].GetDisplayName().ToString(),*Keys[1].GetDisplayName().ToString(),*Keys[3].GetDisplayName().ToString());
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
        if(Moves.Num()%5==0&&R==EStepResult::Continue)SaveNow();
        if(R==EStepResult::Won)Win();
    }
    void Undo(){if(!Moves.IsEmpty()){Moves.Pop();Rebuild();}}
    void Restart(){Moves.Reset();Rebuild();}
    static TCHAR TurnLeft(TCHAR F){return F==TEXT('U')?TEXT('L'):F==TEXT('L')?TEXT('D'):F==TEXT('D')?TEXT('R'):TEXT('U');}
    static TCHAR TurnRight(TCHAR F){return F==TEXT('U')?TEXT('R'):F==TEXT('R')?TEXT('D'):F==TEXT('D')?TEXT('L'):TEXT('U');}
    void Turn(TCHAR To){if(To!=State.Facing)Do(FString(TEXT("t"))+To);}
    void Win()
    {
        auto* Games=Subsystem();Phase=EPhase::Busy;const int32 Actions=State.Actions,Stars=Stage.Stars(Actions);
        const TWeakPtr<SProjectStepsGame> Weak=StaticCastSharedRef<SProjectStepsGame>(AsShared());
        const FString Detail=FString::Printf(TEXT("행동 %d · 기준 %d · 별 %s"),Actions,Stage.Par,*(FString::ChrN(Stars,TEXT('★'))+FString::ChrN(3-Stars,TEXT('☆'))));
        auto Finish=[Weak,Detail](const FString& Error)
        {
            auto Self=Weak.Pin();if(!Self)return;
            TArray<TPair<FString,TFunction<void()>>> Extra;
            const int32 Index=FCString::Atoi(*Self->Stage.Id);
            if(Index<10&&Self->NextStage){auto Next=Self->NextStage;const FString Id=FString::FromInt(Index+1);Extra.Add({TEXT("다음 스테이지"),[Next,Id]{Next(Id);}});}
            Self->ShowOver(TEXT("출구에 닿았습니다"),Error.IsEmpty()?Detail:Detail+TEXT("\n")+Error,Extra);
        };
        if(!Games){Finish(FString());return;}
        Games->Finish(RunId,true,ActiveMs(),0,Moves,[Finish](TSharedPtr<FJsonObject>,const FString& Error){Finish(Error);});
    }
    int32 Dir(const FKey& K)const
    {
        for(int32 I=0;I<4;++I)if(K==Keys[I])return I;
        return K==EKeys::Up?0:K==EKeys::Down?1:K==EKeys::Left?2:K==EKeys::Right?3:INDEX_NONE;
    }
    bool IsGameKey(const FKey& K)const override{return Dir(K)!=INDEX_NONE||K==EKeys::Q||K==EKeys::E||K==EKeys::SpaceBar||K==EKeys::Z||K==EKeys::BackSpace||K==EKeys::R;}
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
        return false;
    }
    TSharedRef<SWidget> Controls()override
    {
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("왼쪽 보기"),[this]{if(Phase==EPhase::Running)Turn(TurnLeft(State.Facing));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("오른쪽 보기"),[this]{if(Phase==EPhase::Running)Turn(TurnRight(State.Facing));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("셔터"),[this]{if(Phase==EPhase::Running)Do(TEXT("S"));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("되돌리기"),[this]{if(Phase==EPhase::Running)Undo();})];
    }
    TSharedRef<FJsonObject> SaveState()const override
    {
        auto J=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> List;for(const FString& M:Moves)List.Add(MakeShared<FJsonValueString>(M));J->SetArrayField(TEXT("moves"),List);return J;
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
            if(C==TEXT('X'))D.Line({P+FVector2D(Size*.15f,Size*.5f),P+FVector2D(Size*.85f,Size*.5f)},FLinearColor(.7f,.25f,.15f,.7f),2.f);
            if(C==TEXT('C'))for(int32 I=0;I<4;++I)D.Box(P+FVector2D(2,2+I*Size*.24f),FVector2D(Size-4,Size*.12f),FLinearColor(.55f,.2f,.12f,1.f));
            if(C==TEXT('E'))D.Text(P+FVector2D(Size*.12f,Size*.25f),TEXT("출구"),FMath::Max(8,int32(Size*.26f)),FLinearColor(.75f,1.f,.85f,1.f),true);
        }
        const FDraw Top{D.Out,D.G,D.Layer+2};
        for(int32 I=0;I<State.Mannequins.Num();++I)
        {
            const FVector2D P=At(State.Mannequins[I]);
            Mannequin(Top,P+FVector2D(Size*.5f,Size*.95f),Size*.88f,0,I==Watched?0:2,I==Watched?FLinearColor(.55f,.52f,.47f,1.f):FLinearColor(.3f,.28f,.26f,1.f),Ink());
            if(I==Watched)Top.Ring(P+FVector2D(Size*.5f,Size*.5f),Size*.46f,FLinearColor(.95f,.85f,.5f,.8f),2.f); // frozen by the player's look
        }
        const FVector2D Me=At(State.Player)+FVector2D(Size*.5f,Size*.5f);const FIntPoint F=FStepState::Delta(State.Facing);
        const FVector2D Fwd(F.X,F.Y),Side(-F.Y,F.X);
        Top.Circle(Me,Size*.3f,FLinearColor(.55f,.75f,.85f,1.f));
        Top.Line({Me+Fwd*Size*.42f,Me+Side*Size*.16f,Me-Side*Size*.16f,Me+Fwd*Size*.42f},FLinearColor(.9f,.95f,1.f,1.f),2.f);
        if(Last==EStepResult::Caught)Top.Text(FVector2D(10,8),TEXT("붙잡혔습니다 · Z로 되돌리기"),13,FLinearColor(.95f,.4f,.3f,1.f),true);
        return D.Layer+4;
    }
};

// ======================================================================== the folder
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
        for(const FCard& Card:{FCard{TEXT("spot"),TEXT("불이 꺼진 사이 움직인 마네킹을 찾으세요.")},FCard{TEXT("blackout"),TEXT("손전등으로 마네킹을 멈춰 두고 퓨즈 3개를 끼우세요.")},FCard{TEXT("steps"),TEXT("한 칸씩, 보고 있는 마네킹은 멈춥니다. 출구까지 가세요.")}})
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
            List->AddSlot().AutoHeight().Padding(10,4)[SNew(SBorder).BorderImage(Round(12)).BorderBackgroundColor(FLinearColor(1,1,1,.05f)).Padding(FMargin(12,10))[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[Label(GameName(Id),15,true)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,2)[Label(Card.Desc,11,false,ProjectDFSDUI::Muted())]
                +SVerticalBox::Slot().AutoHeight().Padding(0,2,0,6)[Label(Mine.IsEmpty()?TEXT("최고 기록 없음"):TEXT("최고 기록 ")+Mine,11,false,ProjectDFSDUI::Accent())]
                +SVerticalBox::Slot().AutoHeight()[Buttons]]];
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
        else if(GameId==TEXT("blackout"))Playing=SNew(SProjectBlackoutGame,H,Id,int32(Ms),Saved);
        else
        {
            const FStepStage* S=Stages.FindByPredicate([&](const FStepStage& X){return X.Id==Stage;});
            if(!S){Status=TEXT("스테이지를 불러오지 못했습니다.");ShowList();return;}
            const TWeakPtr<SProjectMiniGameFolder> Weak=StaticCastSharedRef<SProjectMiniGameFolder>(AsShared());
            Playing=SNew(SProjectStepsGame,H,Id,int32(Ms),SProjectStepsGame::FSetup{*S,Saved,[Weak](const FString& Next){if(auto F=Weak.Pin())F->Pending=[F,Next]{F->Begin(TEXT("steps"),Next);};}});
        }
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
    FStepStage Stage;Stage.Id=TEXT("10");Stage.Name=TEXT("폐점");Stage.Map={TEXT("########"),TEXT("#ME.M.M#"),TEXT("#...#..#"),TEXT("##.#...#"),TEXT("##.#...#"),TEXT("#XSX...#"),TEXT("########")};Stage.Facing=TEXT('R');Stage.Shutters=2;Stage.Par=15;
    auto Moves=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> List;for(const TCHAR* M:{TEXT("tL"),TEXT("S"),TEXT("U")})List.Add(MakeShared<FJsonValueString>(M));Moves->SetArrayField(TEXT("moves"),List);
    Capture(SNew(SProjectStepsGame,Host,TEXT("qa"),0,SProjectStepsGame::FSetup{Stage,Moves,nullptr}),TEXT("Steps.png"),3.5f);
    Capture(SNew(SProjectStepsGame,Host,TEXT("qa"),0,SProjectStepsGame::FSetup{Stage,nullptr,nullptr}),TEXT("Steps_Countdown.png"),.5f);
    Size=FIntPoint(352,640); // upright again (the player turned the phone back): still playable
    Capture(SNew(SProjectBlackoutGame,Host,TEXT("qa-blackout"),0,nullptr),TEXT("Blackout_Portrait.png"),5.f);
    TestTrue(TEXT("Mini game screens render to images"),bSaved);
    return true;
}
#endif

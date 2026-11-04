// File: Source/ProjectProject01/Private/ProjectMatchWait.cpp

#include "ProjectMatchWait.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectDFSDUITheme.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Misc/App.h"

namespace
{
const FName LobbyMap(TEXT("/Game/MyProject/Level/LobbyLevel"));
FSlateFontInfo PanelFont(int32 Size,bool bBold=false){return FCoreStyle::GetDefaultFontStyle(bBold?TEXT("Bold"):TEXT("Regular"),Size);}

/** The left-side panel. Rebuilds only when what it shows changes, so buttons keep their hover and press states. */
class SProjectMatchWaitPanel final:public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectMatchWaitPanel){}
    SLATE_END_ARGS()
    void Construct(const FArguments&,UProjectMatchWait* InOwner)
    {
        Owner=InOwner;
        // The panel lies over the whole screen: only its card takes the mouse, the rest lets clicks reach the lobby below.
        SetVisibility(EVisibility::SelfHitTestInvisible);
        ChildSlot
        [
            SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            // Left, a little below the middle of the screen.
            +SOverlay::Slot()[SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
                +SVerticalBox::Slot().FillHeight(.56f)[SNullWidget::NullWidget]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(24,0)[SAssignNew(Card,SBox).WidthOverride(300)]
                +SVerticalBox::Slot().FillHeight(.44f)[SNullWidget::NullWidget]]
            // The start notice, in the middle while the screen goes dark.
            +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SAssignNew(Notice,SBorder).Visibility(EVisibility::Collapsed)
                .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(0,0,0,.75f)).Padding(FMargin(40,18))
                [SNew(STextBlock).Font(PanelFont(28,true)).ColorAndOpacity(ProjectDFSDUI::Text()).Justification(ETextJustify::Center)
                    .Text_Lambda([this]{const auto* O=Owner.Get();return FText::FromString(O?O->StartNoticeText():FString(TEXT("게임이 시작됩니다")));})]]
        ];
    }
    void Refresh(bool bShow,bool bNotice,bool bHint)
    {
        Notice->SetVisibility(bNotice?EVisibility::HitTestInvisible:EVisibility::Collapsed);
        Card->SetVisibility(bShow?EVisibility::Visible:EVisibility::Collapsed);
        if(!bShow)return;
        auto* O=Owner.Get();auto* Lobby=O?O->GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;if(!Lobby)return;
        const auto& Room=Lobby->GetCurrentRoom();const bool bRandom=Lobby->IsMatchmaking()||(Lobby->HasCurrentRoom()&&Room.bRandomParty);
        // Not the clock or a request in flight: those change every second and a rebuilt button loses the click being made.
        FString Key=FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%s|%d|%d"),bRandom,Lobby->MatchmakingWaiting,Room.bIsReady,Room.bCanStart,Room.bIsHost,
            Room.bStarting||Room.bServerPreparing,*Room.Name,bHint,O->Place()==UProjectMatchWait::EPlace::Lobby);
        for(const auto& M:Room.Members)Key+=FString::Printf(TEXT("|%s%d%d"),*M.DisplayName,M.bIsReady,M.bIsHost);
        if(Key==Shown)return;Shown=Key;
        const TWeakObjectPtr<UProjectProject01LobbySubsystem> WeakLobby(Lobby);
        // Only a change being sent disables a button; during a plain refresh a press waits for it (RunWhenFree).
        auto Line=[](const FString& S,int32 Size,const FLinearColor& C,bool bBold=false){return SNew(STextBlock).AutoWrapText(true).Font(PanelFont(Size,bBold)).ColorAndOpacity(C).Text(FText::FromString(S));};
        auto Button=[](const FString& Caption,TFunction<void()> Run,TAttribute<bool> bEnabled=true,bool bPrimary=false)
        {
            return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle(bPrimary)).ContentPadding(FMargin(8,4)).HAlign(HAlign_Center).IsEnabled(bEnabled)
                .OnClicked_Lambda([Run]{Run();return FReply::Handled();})[SNew(STextBlock).Font(PanelFont(12,true)).ColorAndOpacity(ProjectDFSDUI::Text()).Text(FText::FromString(Caption))];
        };
        const TWeakObjectPtr<UProjectMatchWait> Weak=O;
        auto Body=SNew(SVerticalBox);auto Actions=SNew(SHorizontalBox);
        const FString Where=O->Place()==UProjectMatchWait::EPlace::Lobby?TEXT("방 확인"):TEXT("로비로");
        if(bRandom)
        {
            Body->AddSlot().AutoHeight()[Line(TEXT("랜덤 매칭 중"),15,ProjectDFSDUI::Text(),true)];
            Body->AddSlot().AutoHeight().Padding(0,4,0,0)[SNew(STextBlock).Font(PanelFont(13)).ColorAndOpacity(ProjectDFSDUI::Muted())
                .Text_Lambda([WeakLobby]{const auto* L=WeakLobby.Get();const int32 S=L?L->GetMatchmakingElapsedSeconds():0;
                    return FText::FromString(FString::Printf(TEXT("인원 %d/3 · %d분 %02d초"),L?FMath::Clamp(L->MatchmakingWaiting,1,3):1,S/60,S%60));})];
            Actions->AddSlot().FillWidth(1).Padding(2)[Button(Where==TEXT("방 확인")?TEXT("매칭 화면"):Where,[Weak]{if(auto* S=Weak.Get())S->OpenMatchView();})];
            Actions->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("매칭 취소"),[Weak]{if(auto* S=Weak.Get())S->LeaveOrCancel();},TAttribute<bool>::CreateLambda([WeakLobby]{const auto* L=WeakLobby.Get();return L&&!L->IsStateChangeInFlight()&&L->IsMatchmaking()&&!L->IsCancellingMatchmaking();}))];
        }
        else
        {
            const int32 Players=Room.Members.Num();
            Body->AddSlot().AutoHeight()[Line(FString::Printf(TEXT("오픈 매칭 · %s (%d/3)"),*Room.Name,FMath::Min(Players,3)),15,ProjectDFSDUI::Text(),true)];
            for(const auto& M:Room.Members)
                Body->AddSlot().AutoHeight().Padding(0,2,0,0)[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(PanelFont(12)).ColorAndOpacity(ProjectDFSDUI::Text()).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text(FText::FromString(M.DisplayName))]
                    +SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)[Line(M.bIsHost?TEXT("방장"):M.bIsReady?TEXT("준비"):TEXT("대기"),12,M.bIsHost||M.bIsReady?ProjectDFSDUI::Accent():ProjectDFSDUI::Muted())]];
            if(Room.bStarting||Room.bServerPreparing)Body->AddSlot().AutoHeight().Padding(0,4,0,0)[Line(TEXT("곧 경기가 시작됩니다"),12,ProjectDFSDUI::Accent())];
            Actions->AddSlot().FillWidth(1).Padding(2)[Button(Where,[Weak]{if(auto* S=Weak.Get())S->OpenMatchView();})];
            if(Room.bIsHost)Actions->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("시작"),[Weak]{if(auto* S=Weak.Get())S->StartRoom();},TAttribute<bool>::CreateLambda([WeakLobby]{const auto* L=WeakLobby.Get();return L&&!L->IsStateChangeInFlight()&&L->GetCurrentRoom().bCanStart&&!L->GetCurrentRoom().bStarting;}),true)];
            else Actions->AddSlot().FillWidth(1).Padding(2)[Button(Room.bIsReady?TEXT("준비 취소"):TEXT("준비"),[Weak]{if(auto* S=Weak.Get())S->ToggleReady();},TAttribute<bool>::CreateLambda([WeakLobby]{const auto* L=WeakLobby.Get();return L&&!L->IsStateChangeInFlight()&&!L->GetCurrentRoom().bStarting;}),!Room.bIsReady)];
            Actions->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("나가기"),[Weak]{if(auto* S=Weak.Get())S->LeaveOrCancel();},TAttribute<bool>::CreateLambda([WeakLobby]{const auto* L=WeakLobby.Get();return L&&!L->IsStateChangeInFlight()&&!L->GetCurrentRoom().bStarting;}))];
        }
        Body->AddSlot().AutoHeight().Padding(0,8,0,0)[Actions];
        if(bHint)Body->AddSlot().AutoHeight().Padding(0,6,0,0)[Line(TEXT("Alt를 누르고 있으면 마우스로 누를 수 있습니다."),11,ProjectDFSDUI::Muted())];
        Card->SetContent(SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel().CopyWithNewOpacity(.94f)).Padding(FMargin(14,10))[Body]);
    }
private:
    TWeakObjectPtr<UProjectMatchWait> Owner;TSharedPtr<SBox> Card;TSharedPtr<SBorder> Notice;FString Shown;
};
}

bool UProjectMatchWait::ShouldCreateSubsystem(UObject* Outer)const{return !IsRunningDedicatedServer()&&!IsRunningCommandlet()&&Super::ShouldCreateSubsystem(Outer);}
void UProjectMatchWait::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);Collection.InitializeDependency<UProjectProject01LobbySubsystem>();Collection.InitializeDependency<UProjectProject01AuthSubsystem>();
    if(FApp::IsUnattended())return;
    TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::Tick),.2f);
}
void UProjectMatchWait::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);TickHandle.Reset();
    if(Viewport.IsValid()&&Panel)Viewport->RemoveViewportWidgetContent(Panel.ToSharedRef());
    Panel.Reset();Viewport.Reset();Super::Deinitialize();
}
UProjectProject01LobbySubsystem* UProjectMatchWait::Lobby()const{return GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>();}
bool UProjectMatchWait::IsWaiting()const
{
    const auto* Auth=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();const auto* L=Lobby();
    if(!Auth||!Auth->IsSignedIn()||!L)return false;
    if(L->IsMatchmaking())return true;
    const auto& Room=L->GetCurrentRoom();
    // A random party that is not queued has nothing to wait for; bot rooms start at once and never wait here.
    return L->HasCurrentRoom()&&!Room.bBotMatch&&(!Room.bRandomParty||Room.bRandomMatch);
}
bool UProjectMatchWait::IsStarting()const
{
    const auto* L=Lobby();if(!L||!L->HasCurrentRoom()||L->GetCurrentRoom().bBotMatch)return false;
    const auto& Room=L->GetCurrentRoom();
    return (Room.bRandomMatch&&!Room.bReturnedToRoom)||Room.bStarting||Room.bServerPreparing||(Room.bStarted&&!Room.bReturnedToRoom);
}
UProjectMatchWait::EPlace UProjectMatchWait::Place()const
{
    const UWorld* World=GetGameInstance()->GetWorld();if(!World)return EPlace::Elsewhere;
    const FString Map=World->GetMapName();
    if(Map.Contains(TEXT("LobbyLevel")))return EPlace::Lobby;
    if(Map.Contains(TEXT("MultiplayerTutorial")))return EPlace::Practice;
    if(Map.Contains(TEXT("MultiplayTest")))return EPlace::Match;
    return EPlace::Elsewhere;
}
void UProjectMatchWait::EnsurePanel()
{
    auto* V=GetGameInstance()->GetGameViewportClient();if(!V)return;
    if(Viewport.Get()!=V){if(Viewport.IsValid()&&Panel)Viewport->RemoveViewportWidgetContent(Panel.ToSharedRef());Viewport=V;}
    if(!Panel)Panel=SNew(SProjectMatchWaitPanel,this);
    if(!Panel->GetParentWidget().IsValid())V->AddViewportWidgetContent(Panel.ToSharedRef(),1000001);
}
bool UProjectMatchWait::Tick(float)
{
    const EPlace Where=Place();const bool bWaiting=IsWaiting();auto* L=Lobby();
    const double Now=FPlatformTime::Seconds();
    // Back in the lobby after "로비로": lift the dark we laid on the way (the lobby shows the room or matching screen).
    if(bRevealOnLobby&&Where==EPlace::Lobby)
    {bRevealOnLobby=false;if(auto* GI=Cast<UProjectProject01GameInstance>(GetGameInstance()))GI->FadeEntireScreenFromBlack(.5f);}
    else if(bRevealOnLobby&&Where==EPlace::Match)bRevealOnLobby=false; // The match was starting instead: its own arrival reveals.
    // After counting away the lobby goes straight into the match under the dark; only if that start fell through
    // (someone left, the start was cancelled) is the lobby shown again.
    if(bHoldDarkInLobby&&Where==EPlace::Match)bHoldDarkInLobby=false;
    else if(bHoldDarkInLobby&&Where==EPlace::Lobby&&!IsStarting())
    {bHoldDarkInLobby=false;if(auto* GI=Cast<UProjectProject01GameInstance>(GetGameInstance()))GI->FadeEntireScreenFromBlack(.5f);}
    if(PendingAction&&(Now>PendingActionUntil||!L))PendingAction.Reset();
    else if(PendingAction&&!L->IsRequestInFlight()){TFunction<void()> Run=MoveTemp(PendingAction);PendingAction.Reset();Run();}
    if(bTransition)
    {
        // "게임이 시작됩니다" counting 5..1 right here, then dark, then the lobby (which goes straight into the match).
        if(Where==EPlace::Lobby||Where==EPlace::Match){bTransition=false;TransitionStage=0;}
        else if(TransitionStage==0&&Now>=TransitionAt){TransitionStage=1;TransitionAt=Now+.7;if(auto* GI=Cast<UProjectProject01GameInstance>(GetGameInstance()))GI->FadeEntireScreenToBlack(.6f);}
        else if(TransitionStage==1&&Now>=TransitionAt){TransitionStage=2;CountedAwayAt=Now;bHoldDarkInLobby=true;if(UWorld* World=GetGameInstance()->GetWorld())UGameplayStatics::OpenLevel(World,LobbyMap,true);}
    }
    else if(bWaiting&&L&&Where!=EPlace::Lobby&&Where!=EPlace::Match)
    {
        // Away from the lobby nobody else asks the server, so the wait is kept fresh from here.
        if(Now>=NextPollAt&&!L->IsRequestInFlight()){NextPollAt=Now+2.0;if(L->IsMatchmaking())L->PollMatchmaking();else L->RefreshCurrentRoom();}
        if(IsStarting())BeginStartTransition();
    }
    const bool bShow=!bTransition&&bWaiting&&Where!=EPlace::Match&&!(Where==EPlace::Lobby&&bLobbyMatchViewShown);
    const bool bAway=bShow&&Where!=EPlace::Lobby;
    if(bShow||bTransition||Panel)EnsurePanel();
    if(Panel)StaticCastSharedPtr<SProjectMatchWaitPanel>(Panel)->Refresh(bShow,bTransition&&TransitionStage<2,bAway);
    // Away from the lobby the mouse belongs to the game; holding Alt lends it to the panel.
    APlayerController* PC=GetGameInstance()->GetFirstLocalPlayerController();
    const bool bAlt=bAway&&PC&&(PC->IsInputKeyDown(EKeys::LeftAlt)||PC->IsInputKeyDown(EKeys::RightAlt));
    if(bAlt&&!bCursorHeld&&PC)
    {
        bCursorHeld=true;bCursorWasShown=PC->bShowMouseCursor;PC->bShowMouseCursor=true;
        FInputModeGameAndUI Mode;Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);Mode.SetHideCursorDuringCapture(false);PC->SetInputMode(Mode);
    }
    else if(!bAlt&&bCursorHeld)ReleaseCursor();
    return true;
}
void UProjectMatchWait::ReleaseCursor()
{
    if(!bCursorHeld)return;bCursorHeld=false;
    if(APlayerController* PC=GetGameInstance()->GetFirstLocalPlayerController()){PC->bShowMouseCursor=bCursorWasShown;if(!bCursorWasShown)PC->SetInputMode(FInputModeGameOnly());}
}
void UProjectMatchWait::BeginStartTransition()
{
    if(bTransition)return;
    ReleaseCursor();bTransition=true;TransitionStage=0;TransitionAt=FPlatformTime::Seconds()+5.0; // 5, 4, 3, 2, 1 here.
}
FString UProjectMatchWait::StartNoticeText()const
{
    if(!bTransition||TransitionStage!=0)return TEXT("게임이 시작됩니다");
    const int32 Left=FMath::Clamp(FMath::CeilToInt(TransitionAt-FPlatformTime::Seconds()),1,5);
    return FString::Printf(TEXT("게임이 시작됩니다\n%d"),Left);
}
bool UProjectMatchWait::ConsumeCountedDownAway()
{
    // Only right after the count (the lobby loads in well under this); a later lobby visit counts as usual.
    const bool bRecent=FPlatformTime::Seconds()-CountedAwayAt<20.0;CountedAwayAt=-100;return bRecent;
}
void UProjectMatchWait::OpenMatchView()
{
    if(Place()==EPlace::Lobby){OnOpenMatchView.Broadcast();return;}
    // From practice: back to the lobby, which shows the room or the matching screen again.
    ReleaseCursor();bRevealOnLobby=true;
    if(auto* GI=Cast<UProjectProject01GameInstance>(GetGameInstance()))GI->FadeEntireScreenToBlack(.4f);
    const TWeakObjectPtr<UProjectMatchWait> Weak(this);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float){if(auto* S=Weak.Get())if(UWorld* World=S->GetGameInstance()->GetWorld())UGameplayStatics::OpenLevel(World,LobbyMap,true);return false;}),.45f);
}
void UProjectMatchWait::RunWhenFree(TFunction<void()> Action)
{
    auto* L=Lobby();if(!L)return;
    if(!L->IsRequestInFlight()){PendingAction.Reset();Action();return;}
    PendingAction=MoveTemp(Action);PendingActionUntil=FPlatformTime::Seconds()+4.0;
}
void UProjectMatchWait::ToggleReady()
{
    const TWeakObjectPtr<UProjectMatchWait> Weak(this);
    RunWhenFree([Weak]{auto* S=Weak.Get();auto* L=S?S->Lobby():nullptr;if(L&&L->HasCurrentRoom())L->SetReady(!L->GetCurrentRoom().bIsReady);});
}
void UProjectMatchWait::StartRoom()
{
    const TWeakObjectPtr<UProjectMatchWait> Weak(this);
    RunWhenFree([Weak]{auto* S=Weak.Get();auto* L=S?S->Lobby():nullptr;if(L&&L->HasCurrentRoom()&&L->GetCurrentRoom().bIsHost)L->StartRoom();});
}
void UProjectMatchWait::LeaveOrCancel()
{
    const TWeakObjectPtr<UProjectMatchWait> Weak(this);
    RunWhenFree([Weak]{auto* S=Weak.Get();auto* L=S?S->Lobby():nullptr;if(!L)return;
        if(L->IsMatchmaking())L->CancelMatchmaking();else if(L->HasCurrentRoom())L->LeaveRoom();});
}

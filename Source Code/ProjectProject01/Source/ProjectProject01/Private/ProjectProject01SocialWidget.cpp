#include "ProjectProject01SocialWidget.h"
#include "ProjectProject01SocialSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01Localization.h"
#include "Dom/JsonObject.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Algo/Reverse.h"

namespace
{
FText Txt(const FString& S){return FProjectProject01Localization::Text(S);}
FString Str(const TSharedPtr<FJsonObject>& J,const TCHAR* Key){FString S;if(J)J->TryGetStringField(Key,S);return S;}
bool Flag(const TSharedPtr<FJsonObject>& J,const TCHAR* Key){bool B=false;double N=0;return J&&(J->TryGetBoolField(Key,B)?B:J->TryGetNumberField(Key,N)&&N!=0);}
TArray<TSharedPtr<FJsonValue>> Rows(const TSharedPtr<FJsonObject>& J,const TCHAR* Key)
{const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(J)J->TryGetArrayField(Key,A);return A?*A:TArray<TSharedPtr<FJsonValue>>{};}
const FEditableTextBoxStyle& SocialInputStyle()
{
    static const FEditableTextBoxStyle Style=[]
    {
        auto V=FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
        V.SetBackgroundImageNormal(FSlateColorBrush(FLinearColor(.94f,.94f,.94f))).SetBackgroundImageHovered(FSlateColorBrush(FLinearColor::White))
         .SetBackgroundImageFocused(FSlateColorBrush(FLinearColor::White)).SetBackgroundImageReadOnly(FSlateColorBrush(FLinearColor(.8f,.8f,.8f)))
         .SetForegroundColor(FLinearColor::Black).SetFocusedForegroundColor(FLinearColor::Black).SetReadOnlyForegroundColor(FLinearColor::Black).SetPadding(FMargin(10,10));
        V.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18));return V;
    }();return Style;
}
TSharedRef<SWidget> Label(const FString& S){return SNew(STextBlock).Text(Txt(S)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).AutoWrapText(true).ColorAndOpacity(FLinearColor::White);}
TSharedRef<SWidget> Btn(const FString& S,TFunction<void()> Action)
{return SNew(SButton).OnClicked_Lambda([Action]{Action();return FReply::Handled();})[Label(S)];}
FString PresenceName(const FString& V)
{return V==TEXT("Online")?TEXT("온라인"):V==TEXT("Away")?TEXT("자리비움"):V==TEXT("DoNotDisturb")?TEXT("방해금지"):TEXT("오프라인");}
FString TitleName(const FString& V)
{return V==TEXT("Survivor")?TEXT("생존자"):V==TEXT("Mannequin")?TEXT("마네킹"):V==TEXT("Explorer")?TEXT("탐험가"):TEXT("새내기");}
FLinearColor AccentColor(const FString& V)
{return V==TEXT("Crimson")?FLinearColor(.45f,.06f,.09f):V==TEXT("Ocean")?FLinearColor(.03f,.25f,.45f):V==TEXT("Forest")?FLinearColor(.04f,.3f,.14f):V==TEXT("Gold")?FLinearColor(.4f,.27f,.02f):FLinearColor(.12f,.15f,.2f);}
class SCommunityBell:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCommunityBell){} SLATE_END_ARGS()
    void Construct(const FArguments&){}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(24,24);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool)const override
    {
        FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),TArray<FVector2D>{{4,18},{7,14},{7,9},{8,6},{12,4},{16,6},{17,9},{17,14},{20,18},{4,18}},ESlateDrawEffect::None,FLinearColor::White,true,2);
        FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),TArray<FVector2D>{{10,21},{14,21}},ESlateDrawEffect::None,FLinearColor::White,true,2);return Layer;
    }
};
}

TSharedRef<SWidget> UProjectProject01SocialWidget::RebuildWidget()
{
    auto Bar=SNew(SHorizontalBox);
    if(bBlocksOnly)Bar->AddSlot().AutoWidth()[Btn(TEXT("차단 목록"),[this]{Open(TEXT("Blocks"));})];
    else
    {
        Bar->AddSlot().FillWidth(1)[Btn(TEXT("친구"),[this]{Open(TEXT("Friends"));})];
        Bar->AddSlot().FillWidth(1)[Btn(TEXT("귓속말"),[this]{Open(TEXT("Whispers"));})];
        Bar->AddSlot().AutoWidth()[Btn(TEXT("신고"),[this]
        {
            if(!ModalKind.IsEmpty())return;
            GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Status.Reset();
            Page=TEXT("RoomActions");BuildPanel();ShowModal(TEXT("AccountReport"));
        })];
        Bar->AddSlot().AutoWidth()[SNew(SButton).ToolTipText(Txt(TEXT("알림"))).OnClicked_Lambda([this]{Open(TEXT("Notifications"));GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Act(TEXT("Read"));return FReply::Handled();})
          [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[SNew(SCommunityBell)]+SHorizontalBox::Slot().AutoWidth()[SAssignNew(BellDot,STextBlock).Text(FText::FromString(TEXT("●"))).ColorAndOpacity(FLinearColor::Red)]]];
    }
    return SNew(SOverlay).Visibility_Lambda([this]{auto* GI=GetGameInstance();auto* S=GI?GI->GetSubsystem<UProjectProject01SocialSubsystem>():nullptr;return S&&S->IsAuthenticated()?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;})
      +SOverlay::Slot().HAlign(HAlign_Right).VAlign(bBlocksOnly?VAlign_Top:VAlign_Bottom).Padding(12)
      [SNew(SBox).WidthOverride(440)[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[SNew(SOverlay)
          +SOverlay::Slot()[SAssignNew(Panel,SVerticalBox)]
          +SOverlay::Slot().VAlign(VAlign_Center)[SAssignNew(Modal,SVerticalBox)]]
        +SVerticalBox::Slot().AutoHeight()[Bar]]];
}
void UProjectProject01SocialWidget::NativeConstruct()
{
    Super::NativeConstruct();if(!GetGameInstance())return;auto* S=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>();if(!S)return;
    S->OnChanged.AddUObject(this,&ThisClass::RefreshView);
    S->OnRoomContextReady.AddUObject(this,&ThisClass::RoomContextReady);
    S->OnWhisperSent.AddWeakLambda(this,[this](const FString& Text){if(MessageInput&&Page==TEXT("Thread")&&MessageInput->GetText().ToString()==Text)MessageInput->SetText(FText::GetEmpty());});
    S->OnActionCompleted.AddWeakLambda(this,[this](const FString& Op,bool Ok)
    {
        if(Ok&&((Op==TEXT("Request")&&ModalKind==TEXT("Add"))||(Op==TEXT("Report")&&(ModalKind==TEXT("Report")||ModalKind==TEXT("AccountReport")))||(Op==TEXT("SaveProfile")&&ModalKind==TEXT("Edit"))||((Op==TEXT("Accept")||Op==TEXT("Decline"))&&ModalKind==TEXT("FriendRequest"))||((Op==TEXT("AcceptInvite")||Op==TEXT("DeclineInvite"))&&ModalKind==TEXT("Invite"))))CloseModal();
    });
    S->Refresh();RefreshView();
}
void UProjectProject01SocialWidget::NativeDestruct()
{
    if(GetGameInstance())if(auto* S=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>())
    {S->OnChanged.RemoveAll(this);S->OnRoomContextReady.RemoveAll(this);S->OnWhisperSent.RemoveAll(this);S->OnActionCompleted.RemoveAll(this);if(!bBlocksOnly)S->OpenConversation(FString());}
    Page.Reset();ModalKind.Reset();bRoomMemberProfile=false;Super::NativeDestruct();
}
void UProjectProject01SocialWidget::ReleaseSlateResources(bool Children)
{
    Super::ReleaseSlateResources(Children);Panel.Reset();Modal.Reset();TargetInput.Reset();MessageInput.Reset();Feedback.Reset();MessageLog.Reset();BellDot.Reset();List.Reset();MessageScroll.Reset();
}
void UProjectProject01SocialWidget::Open(const FString& Name)
{
    const bool LeavingReport=ModalKind==TEXT("Report")||ModalKind==TEXT("AccountReport");
    CloseModal();PendingRoomMenu.Reset();
    if(LeavingReport&&GetGameInstance())if(auto* S=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>())S->Status.Reset();
    if(FSlateApplication::IsInitialized())FSlateApplication::Get().DismissAllMenus();
    bRoomMemberProfile=false;
    Page=Page==Name?FString():Name;ThreadAccount.Reset();LastMessageId.Reset();
    if(!bBlocksOnly)if(auto* S=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>())S->OpenConversation(FString());
    BuildPanel();
}
void UProjectProject01SocialWidget::OpenThread(const FString& Account)
{
    const bool LeavingReport=ModalKind==TEXT("Report")||ModalKind==TEXT("AccountReport");
    CloseModal();PendingRoomMenu.Reset();
    if(LeavingReport&&GetGameInstance())if(auto* S=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>())S->Status.Reset();
    bRoomMemberProfile=false;
    ThreadAccount=Account.TrimStartAndEnd();if(ThreadAccount.Len()<3)return;
    Page=TEXT("Thread");LastMessageId.Reset();GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->OpenConversation(ThreadAccount);BuildPanel();
}
void UProjectProject01SocialWidget::ShowProfile(const FString& Account)
{
    bRoomMemberProfile=false;
    Page=TEXT("Profile");GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->LoadProfile(Account);BuildPanel();
}
bool UProjectProject01SocialWidget::ShowRoomMemberProfile(const FString& UserId)
{
    auto* S=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>():nullptr;
    if(!S||!S->IsAuthenticated()||!ModalKind.IsEmpty()||UserId.IsEmpty())return false;
    S->OpenConversation(FString());bRoomMemberProfile=true;Page=TEXT("Profile");
    S->LoadRoomProfile(UserId);BuildPanel();return true;
}
void UProjectProject01SocialWidget::CloseRoomMemberProfile()
{
    if(!PendingRoomMenu.IsEmpty()){PendingRoomMenu.Reset();if(FSlateApplication::IsInitialized())FSlateApplication::Get().DismissAllMenus();}
    if(ModalKind==TEXT("Report"))CloseModal();
    if(!bRoomMemberProfile)return;
    bRoomMemberProfile=false;Page.Reset();BuildPanel();
}
void UProjectProject01SocialWidget::BuildPanel()
{
    if(!Panel)return;Panel->ClearChildren();List.Reset();Feedback.Reset();MessageInput.Reset();MessageLog.Reset();MessageScroll.Reset();TargetInput.Reset();
    if(Page.IsEmpty())return;
    auto* S=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>();
    auto Body=SNew(SVerticalBox);
    FString Heading=Page==TEXT("RoomActions")?TEXT("방 참가자"):Page==TEXT("Friends")?TEXT("친구"):Page==TEXT("Whispers")?TEXT("귓속말"):Page==TEXT("Thread")?ThreadAccount+TEXT("님과의 대화"):Page==TEXT("Profile")?TEXT("프로필 / 전적"):Page==TEXT("Blocks")?TEXT("차단 목록"):TEXT("알림");
    Body->AddSlot().AutoHeight()[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Label(Heading)]
      +SHorizontalBox::Slot().AutoWidth()[Btn(TEXT("닫기"),[this]{Page.Reset();bRoomMemberProfile=false;if(!bBlocksOnly)GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->OpenConversation(FString());BuildPanel();})]];
    if(Page==TEXT("Friends"))
    {
        Body->AddSlot().AutoHeight().Padding(0,6)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(TargetInput,SEditableTextBox).Style(&SocialInputStyle()).Text(FText::FromString(Filter)).HintText(Txt(TEXT("이름·계정 검색"))).ForegroundColor(FLinearColor::Black)
            .OnTextChanged_Lambda([this](const FText& T){Filter=T.ToString();RefreshView();})
            .OnTextCommitted_Lambda([this](const FText& T,ETextCommit::Type C){if(C==ETextCommit::OnEnter)GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->SearchPlayers(T.ToString());})]
          +SHorizontalBox::Slot().AutoWidth()[Btn(TEXT("검색"),[this]{GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->SearchPlayers(Filter);})]];
        Body->AddSlot().AutoHeight()[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1)[Btn(TEXT("친구 추가"),[this]{ShowModal(TEXT("Add"));})]
          +SHorizontalBox::Slot().FillWidth(1)[Btn(TEXT("내 프로필 꾸미기"),[this]{ShowModal(TEXT("Edit"));})]];
    }
    if(Page==TEXT("Whispers"))
    {
        Body->AddSlot().AutoHeight()[Label(TEXT("로비·방에서만 대화할 수 있습니다."))];
        Body->AddSlot().AutoHeight().Padding(0,6)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(TargetInput,SEditableTextBox).Style(&SocialInputStyle()).HintText(Txt(TEXT("대화할 상대 계정 ID"))).ForegroundColor(FLinearColor::Black)
            .OnTextCommitted_Lambda([this](const FText& T,ETextCommit::Type C){if(C==ETextCommit::OnEnter)OpenThread(T.ToString());})]
          +SHorizontalBox::Slot().AutoWidth()[Btn(TEXT("대화 열기"),[this]{OpenThread(TargetInput->GetText().ToString());})]];
    }
    if(Page==TEXT("Thread"))
    {
        Body->AddSlot().AutoHeight()[Btn(TEXT("대화 목록으로"),[this]{Page=TEXT("Whispers");GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->OpenConversation(FString());BuildPanel();})];
        Body->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(300)[SAssignNew(MessageScroll,SScrollBox)+SScrollBox::Slot()[SAssignNew(MessageLog,STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).AutoWrapText(true).ColorAndOpacity(FLinearColor::White)]]];
        auto Send=[this]{if(MessageInput)GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Act(TEXT("Whisper"),ThreadAccount,MessageInput->GetText().ToString());};
        Body->AddSlot().AutoHeight()[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(MessageInput,SEditableTextBox).Style(&SocialInputStyle()).HintText(Txt(TEXT("메시지 (500자 이내)"))).ForegroundColor(FLinearColor::Black).OnTextCommitted_Lambda([Send](const FText&,ETextCommit::Type C){if(C==ETextCommit::OnEnter)Send();})]
          +SHorizontalBox::Slot().AutoWidth()[Btn(TEXT("보내기"),Send)]];
    }
    if(Page==TEXT("Profile"))Body->AddSlot().AutoHeight()[Btn(bRoomMemberProfile?TEXT("닫고 방으로 돌아가기"):TEXT("친구 목록으로"),[this]{if(bRoomMemberProfile)CloseRoomMemberProfile();else{Page=TEXT("Friends");BuildPanel();}})];
    if(Page!=TEXT("Thread"))Body->AddSlot().AutoHeight()[SNew(SBox).MaxDesiredHeight(340)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(List,SVerticalBox)]]];
    Body->AddSlot().AutoHeight().Padding(0,6)[SAssignNew(Feedback,STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor::White)];
    if(!bBlocksOnly)Body->AddSlot().AutoHeight()[SNew(SComboButton)
      .ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return Txt(TEXT("내 상태: ")+PresenceName(GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->GetPresence()));}).ColorAndOpacity(FLinearColor::White)]
      .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>
      {
          auto Choices=SNew(SVerticalBox);
          for(const FString& V:TArray<FString>{TEXT("Online"),TEXT("Offline"),TEXT("Away"),TEXT("DoNotDisturb")})Choices->AddSlot().AutoHeight()[Btn(PresenceName(V),[this,V]{FSlateApplication::Get().DismissAllMenus();GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->SetPresence(V);})];
          return Choices;
      })];
    Panel->AddSlot().AutoHeight()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.025f,.03f,.045f,1)).Padding(12)[Body]];
    RefreshView();S->Refresh();
}
void UProjectProject01SocialWidget::FriendMenu(const FString& Account,const FPointerEvent& Event)
{
    FMenuBuilder Menu(true,nullptr);
    for(const auto& Entry:TArray<TPair<FString,FString>>{{TEXT("내 방에 초대하기"),TEXT("Invite")},{TEXT("친구 삭제"),TEXT("Remove")},{TEXT("메시지 차단"),TEXT("Block")},{TEXT("프로필 확인"),TEXT("Profile")}})
    {
        const FString Op=Entry.Value;
        const auto* Lobby=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
        if(Op==TEXT("Invite")&&Lobby&&Lobby->HasCurrentRoom()&&Lobby->GetCurrentRoom().bRandomParty)continue;
        Menu.AddMenuEntry(Txt(Entry.Key),FText::GetEmpty(),FSlateIcon(),FUIAction(FExecuteAction::CreateWeakLambda(this,[this,Account,Op]
        {if(Op==TEXT("Profile"))ShowProfile(Account);else GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Act(Op,Account);})));
    }
    FSlateApplication::Get().PushMenu(TakeWidget(),FWidgetPath(),Menu.MakeWidget(),Event.GetScreenSpacePosition(),FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}
bool UProjectProject01SocialWidget::IsRoomMember(const FString& UserId) const
{
    const auto* S=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>():nullptr;
    const auto* L=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
    return S&&S->IsAuthenticated()&&L&&L->HasCurrentRoom()&&
        L->GetCurrentRoom().Members.ContainsByPredicate([&](const FProjectProject01RoomMember& M){return M.UserId==UserId;});
}
bool UProjectProject01SocialWidget::ShowRoomMemberMenu(const FString& UserId,const FVector2D& Position)
{
    if(!ModalKind.IsEmpty()||!IsRoomMember(UserId)||!FSlateApplication::IsInitialized())return false;
    PendingRoomMenu=UserId;RoomMenuPosition=Position;Page=TEXT("RoomActions");BuildPanel();
    auto* S=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>();S->Status=TEXT("참가자 정보를 확인하는 중입니다.");
    S->LoadRoomContext(UserId);RefreshView();return true;
}
void UProjectProject01SocialWidget::RoomContextReady(const FString& UserId,bool Ok)
{
    if(PendingRoomMenu!=UserId||!Ok||!ModalKind.IsEmpty()||!IsRoomMember(UserId)||!FSlateApplication::IsInitialized())return;
    auto* S=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>();const auto J=S->RoomContext;
    const TSharedPtr<FJsonObject>* Person=nullptr;if(!J||!J->TryGetObjectField(TEXT("profile"),Person))return;
    const FString Account=Str(*Person,TEXT("accountId")),Name=Str(*Person,TEXT("displayName"));if(Account.IsEmpty())return;
    S->Status.Reset();
    FMenuBuilder Menu(true,nullptr);
    auto Add=[&](const FString& LabelText,const FString& Op)
    {
        Menu.AddMenuEntry(Txt(LabelText),FText::GetEmpty(),FSlateIcon(),FUIAction(FExecuteAction::CreateWeakLambda(this,[this,UserId,Account,Name,Op]
        {
            if(!IsRoomMember(UserId)||!ModalKind.IsEmpty())return;
            auto* Social=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>();
            if(Op==TEXT("Profile")){ShowRoomMemberProfile(UserId);return;}
            if(Op==TEXT("Whisper")){OpenThread(Account);return;}
            Page=TEXT("RoomActions");bRoomMemberProfile=false;Social->Status.Reset();BuildPanel();
            if(Op==TEXT("Request")){ShowModal(TEXT("Add"),Account);return;}
            if(Op==TEXT("Report")){ShowModal(TEXT("Report"),UserId,Name+TEXT(" [")+Account+TEXT("]"));return;}
            Social->Act(Op,Account);
        })));
    };
    for(const auto& Action:RoomMenuActions(Flag(J,TEXT("isSelf")),Flag(J,TEXT("isFriend")),Flag(J,TEXT("isBlocked"))))Add(Action.Key,Action.Value);
    FSlateApplication::Get().PushMenu(TakeWidget(),FWidgetPath(),Menu.MakeWidget(),RoomMenuPosition,FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}
TArray<TPair<FString,FString>> UProjectProject01SocialWidget::RoomMenuActions(bool bSelf,bool bFriend,bool bBlocked)
{
    TArray<TPair<FString,FString>> Actions{{TEXT("프로필 확인"),TEXT("Profile")}};
    if(bSelf)return Actions;
    Actions.Emplace(bFriend?TEXT("친구 삭제"):TEXT("친구 추가"),bFriend?TEXT("Remove"):TEXT("Request"));
    Actions.Emplace(TEXT("귓속말"),TEXT("Whisper"));
    Actions.Emplace(bBlocked?TEXT("메시지 차단 해제"):TEXT("메시지 차단"),bBlocked?TEXT("Unblock"):TEXT("Block"));
    Actions.Emplace(TEXT("신고"),TEXT("Report"));return Actions;
}
void UProjectProject01SocialWidget::CloseModal(){ModalKind.Reset();ModalTarget.Reset();ModalRequestKey.Reset();if(Modal)Modal->ClearChildren();if(Panel)Panel->SetEnabled(true);}
void UProjectProject01SocialWidget::ShowModal(const FString& Kind,const FString& Target,const FString& Description)
{
    if(!Modal)return;ModalKind=Kind;ModalTarget=Target;Modal->ClearChildren();if(Panel)Panel->SetEnabled(false);
    auto* S=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>();auto Body=SNew(SVerticalBox);
    Body->AddSlot().AutoHeight()[Label(Kind==TEXT("Report")||Kind==TEXT("AccountReport")?TEXT("플레이어 신고"):Kind==TEXT("Add")?TEXT("친구 요청 보내기"):Kind==TEXT("Edit")?TEXT("내 프로필 꾸미기"):Kind==TEXT("FriendRequest")?TEXT("친구 요청 수신"):TEXT("방 초대"))];
    if(Kind==TEXT("Add"))
    {
        TSharedRef<SEditableTextBox> Input=SNew(SEditableTextBox).Style(&SocialInputStyle()).Text(FText::FromString(Target)).HintText(Txt(TEXT("상대 계정 ID"))).ForegroundColor(FLinearColor::Black);
        Body->AddSlot().AutoHeight().Padding(0,8)[Input];Body->AddSlot().AutoHeight()[Btn(TEXT("친구 요청 보내기"),[this,Input]{GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Act(TEXT("Request"),Input->GetText().ToString());})];
    }
    else if(Kind==TEXT("Report")||Kind==TEXT("AccountReport"))
    {
        ReportReason.Reset();TSharedPtr<SEditableTextBox> AccountInput;
        if(Kind==TEXT("AccountReport"))
        {
            Body->AddSlot().AutoHeight().Padding(0,6)[Label(TEXT("상대 계정 아이디 (닉네임이 아닌 로그인 아이디)"))];
            Body->AddSlot().AutoHeight()[SAssignNew(AccountInput,SEditableTextBox).Style(&SocialInputStyle()).ForegroundColor(FLinearColor::Black)];
        }
        else Body->AddSlot().AutoHeight().Padding(0,8)[Label(TEXT("신고 대상: ")+Description)];
        Body->AddSlot().AutoHeight()[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda([this]
        {return Txt(ReportReason==TEXT("Abuse")?TEXT("신고 사유: 욕설·괴롭힘"):ReportReason==TEXT("Cheating")?TEXT("신고 사유: 부정행위 의심"):ReportReason==TEXT("Griefing")?TEXT("신고 사유: 게임 진행 방해"):ReportReason==TEXT("Other")?TEXT("신고 사유: 기타"):TEXT("신고 사유 선택 (필수)"));}).ColorAndOpacity(FLinearColor::White)]
          .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>
          {
              auto L=SNew(SVerticalBox);
              for(const auto& E:TArray<TPair<FString,FString>>{{TEXT("욕설·괴롭힘"),TEXT("Abuse")},{TEXT("부정행위 의심"),TEXT("Cheating")},{TEXT("게임 진행 방해"),TEXT("Griefing")},{TEXT("기타"),TEXT("Other")}})
                  L->AddSlot().AutoHeight()[Btn(E.Key,[this,V=E.Value]{ReportReason=V;FSlateApplication::Get().DismissAllMenus();})];return L;
          })];
        Body->AddSlot().AutoHeight().Padding(0,6)[Label(TEXT("상세 신고 사유 (필수, 1000자 이내)"))];
        auto Input=SNew(SMultiLineEditableTextBox).Style(&SocialInputStyle()).AutoWrapText(true).ForegroundColor(FLinearColor::Black);
        Body->AddSlot().AutoHeight().Padding(0,8)[SNew(SBox).HeightOverride(150)[Input]];
        Body->AddSlot().AutoHeight()[Label(TEXT("신고 내용은 운영자만 확인합니다. 신고만으로 자동 제재하지 않습니다."))];
        Body->AddSlot().AutoHeight()[Btn(TEXT("신고 제출"),[this,Input,AccountInput]
        {
            auto* Social=GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>();
            if(AccountInput)Social->SubmitAccountReport(AccountInput->GetText().ToString(),ReportReason,Input->GetText().ToString());
            else Social->SubmitRoomReport(ModalTarget,ReportReason,Input->GetText().ToString());
        })];
    }
    else if(Kind==TEXT("Edit"))
    {
        const TSharedPtr<FJsonObject>* Me=nullptr;if(S->Snapshot)S->Snapshot->TryGetObjectField(TEXT("me"),Me);
        auto J=Me?*Me:TSharedPtr<FJsonObject>();DraftAccent=Str(J,TEXT("accent"));DraftTitle=Str(J,TEXT("title"));if(DraftAccent.IsEmpty())DraftAccent=TEXT("Slate");if(DraftTitle.IsEmpty())DraftTitle=TEXT("Rookie");
        Body->AddSlot().AutoHeight()[Label(TEXT("상태메시지는 모든 로그인 플레이어에게 공개됩니다. (160자)"))];
        auto StatusInput=SNew(SEditableTextBox).Style(&SocialInputStyle()).Text(FText::FromString(Str(J,TEXT("statusMessage")))).ForegroundColor(FLinearColor::Black);
        auto BioInput=SNew(SEditableTextBox).Style(&SocialInputStyle()).Text(FText::FromString(Str(J,TEXT("bio")))).HintText(Txt(TEXT("소개글 (500자)"))).ForegroundColor(FLinearColor::Black);
        Body->AddSlot().AutoHeight().Padding(0,6)[StatusInput];Body->AddSlot().AutoHeight().Padding(0,6)[BioInput];
        Body->AddSlot().AutoHeight()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor_Lambda([this]{return AccentColor(DraftAccent);}).Padding(8)[Label(TEXT("프로필 색상 미리보기"))]];
        Body->AddSlot().AutoHeight()[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return Txt(TEXT("색상: ")+DraftAccent);})]
          .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>{auto L=SNew(SVerticalBox);for(const FString& V:TArray<FString>{TEXT("Slate"),TEXT("Crimson"),TEXT("Ocean"),TEXT("Forest"),TEXT("Gold")})L->AddSlot().AutoHeight()[Btn(V,[this,V]{DraftAccent=V;FSlateApplication::Get().DismissAllMenus();})];return L;})];
        Body->AddSlot().AutoHeight()[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return Txt(TEXT("칭호: ")+TitleName(DraftTitle));})]
          .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>{auto L=SNew(SVerticalBox);for(const FString& V:TArray<FString>{TEXT("Rookie"),TEXT("Survivor"),TEXT("Mannequin"),TEXT("Explorer")})L->AddSlot().AutoHeight()[Btn(TitleName(V),[this,V]{DraftTitle=V;FSlateApplication::Get().DismissAllMenus();})];return L;})];
        Body->AddSlot().AutoHeight().Padding(0,6)[Btn(TEXT("프로필 저장"),[this,StatusInput,BioInput]{GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->SaveProfile(StatusInput->GetText().ToString(),BioInput->GetText().ToString(),DraftAccent,DraftTitle);})];
        const FString OwnAccount=Str(J,TEXT("accountId"));
        Body->AddSlot().AutoHeight()[Btn(TEXT("내 프로필·전적 보기"),[this,OwnAccount]{CloseModal();ShowProfile(OwnAccount);})];
    }
    else if(Kind==TEXT("Invite"))
    {
        Body->AddSlot().AutoHeight().Padding(0,8)[Label(Description+TEXT("\n수락한 경우에만 입장합니다. 초대는 2분 동안 유효합니다."))];
        Body->AddSlot().AutoHeight()[Btn(TEXT("수락하고 방에 입장"),[this]{GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Act(TEXT("AcceptInvite"),ModalTarget);})];
        Body->AddSlot().AutoHeight()[Btn(TEXT("거절"),[this]{GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Act(TEXT("DeclineInvite"),ModalTarget);})];
    }
    else if(Kind==TEXT("FriendRequest"))
    {
        Body->AddSlot().AutoHeight().Padding(0,8)[Label(Description+TEXT("\n친구 요청을 수락하시겠습니까? 수락해야만 친구로 추가됩니다."))];
        Body->AddSlot().AutoHeight()[Btn(TEXT("수락"),[this]{GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Act(TEXT("Accept"),ModalTarget);})];
        Body->AddSlot().AutoHeight()[Btn(TEXT("거절"),[this]{GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Act(TEXT("Decline"),ModalTarget);})];
    }
    Body->AddSlot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]{return Txt(GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Status);}).AutoWrapText(true).ColorAndOpacity(FLinearColor::White)];
    Body->AddSlot().AutoHeight()[Btn(TEXT("닫기"),[this]{CloseModal();RefreshView();})];
    Modal->AddSlot().AutoHeight()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.06f,.08f,.12f,1)).Padding(18)[Body]];
}
void UProjectProject01SocialWidget::RefreshView()
{
    auto* S=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>():nullptr;
    if(!S)return;SetVisibility(S->IsAuthenticated()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(!S->IsAuthenticated()){PendingRoomMenu.Reset();Page.Reset();ThreadAccount.Reset();Filter.Reset();SeenInvites.Reset();SeenFriendRequests.Reset();CloseModal();if(Panel)Panel->ClearChildren();if(FSlateApplication::IsInitialized())FSlateApplication::Get().DismissAllMenus();return;}
    if(Feedback)Feedback->SetText(Txt(S->Status));
    bool Unread=false;for(auto V:Rows(S->Snapshot,TEXT("notifications")))Unread|=Flag(V->AsObject(),TEXT("unread"));for(auto V:Rows(S->Snapshot,TEXT("conversations")))Unread|=Flag(V->AsObject(),TEXT("unread"));
    if(BellDot)BellDot->SetVisibility(Unread?EVisibility::Visible:EVisibility::Collapsed);
    if(!bBlocksOnly)
    {
        bool ActiveInvite=false;for(auto V:Rows(S->Snapshot,TEXT("invites")))if(Str(V->AsObject(),TEXT("inviteId"))==ModalTarget)ActiveInvite=true;
        if(ModalKind==TEXT("Invite")&&!ActiveInvite)CloseModal();
        if(ModalKind.IsEmpty())for(auto V:Rows(S->Snapshot,TEXT("invites")))
        {
            auto J=V->AsObject();FString Id=Str(J,TEXT("inviteId"));if(!SeenInvites.Contains(Id)){SeenInvites.Add(Id);ShowModal(TEXT("Invite"),Id,Str(J,TEXT("displayName"))+TEXT("님의 초대: ")+Str(J,TEXT("roomName")));break;}
        }
        bool ActiveFriendRequest=false;
        for(auto V:Rows(S->Snapshot,TEXT("requests"))){auto J=V->AsObject();if(Str(J,TEXT("accountId"))+TEXT("|")+Str(J,TEXT("createdAtUtc"))==ModalRequestKey)ActiveFriendRequest=true;}
        if(ModalKind==TEXT("FriendRequest")&&!ActiveFriendRequest)CloseModal();
        if(ModalKind.IsEmpty())for(auto V:Rows(S->Snapshot,TEXT("requests")))
        {
            auto J=V->AsObject();FString A=Str(J,TEXT("accountId")),Key=A+TEXT("|")+Str(J,TEXT("createdAtUtc"));
            if(!SeenFriendRequests.Contains(Key)){SeenFriendRequests.Add(Key);ShowModal(TEXT("FriendRequest"),A,Str(J,TEXT("displayName"))+TEXT(" [")+A+TEXT("]님이 친구를 요청했습니다."));ModalRequestKey=Key;break;}
        }
    }
    if(List)List->ClearChildren();
    auto Add=[this](const FString& T){if(List)List->AddSlot().AutoHeight().Padding(3)[Label(T)];};
    auto Action=[this](const FString& T,const FString& Op,const FString& A){if(List)List->AddSlot().AutoHeight().Padding(2)[Btn(T,[this,Op,A]{GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Act(Op,A);})];};
    if(Page==TEXT("Friends")&&List)
    {
        int32 Count=0;
        for(auto V:Rows(S->Snapshot,TEXT("friends")))
        {
            auto J=V->AsObject();FString A=Str(J,TEXT("accountId")),N=Str(J,TEXT("displayName"));if(!Filter.IsEmpty()&&!A.Contains(Filter)&&!N.Contains(Filter))continue;Count++;
            List->AddSlot().AutoHeight().Padding(0,4)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).Padding(8).BorderBackgroundColor(AccentColor(Str(J,TEXT("accent"))))
              .OnMouseButtonDown_Lambda([this,A](const FGeometry&,const FPointerEvent& E){if(E.GetEffectingButton()==EKeys::RightMouseButton)FriendMenu(A,E);else if(E.GetEffectingButton()==EKeys::LeftMouseButton)OpenThread(A);else return FReply::Unhandled();return FReply::Handled();})
              [Label(N+TEXT(" [")+A+TEXT("] / ")+TitleName(Str(J,TEXT("title")))+TEXT("\n")+PresenceName(Str(J,TEXT("presence")))+TEXT(" ")+Str(J,TEXT("activity"))+TEXT("\n")+Str(J,TEXT("statusMessage")))]];
        }
        if(Count==0)Add(TEXT("표시할 친구가 없습니다."));
        if(S->SearchResults&&!Filter.IsEmpty())
        {
            Add(TEXT("계정 검색 결과"));for(auto V:Rows(S->SearchResults,TEXT("players"))){auto J=V->AsObject();FString A=Str(J,TEXT("accountId"));Add(Str(J,TEXT("displayName"))+TEXT(" [")+A+TEXT("]"));List->AddSlot().AutoHeight()[Btn(TEXT("친구 추가"),[this,A]{ShowModal(TEXT("Add"),A);})];List->AddSlot().AutoHeight()[Btn(TEXT("프로필 확인"),[this,A]{ShowProfile(A);})];}
        }
        Add(TEXT("받은 친구 요청"));for(auto V:Rows(S->Snapshot,TEXT("requests"))){auto J=V->AsObject();FString A=Str(J,TEXT("accountId"));Add(Str(J,TEXT("displayName"))+TEXT(" [")+A+TEXT("]"));Action(TEXT("수락"),TEXT("Accept"),A);Action(TEXT("거절"),TEXT("Decline"),A);}
        Add(TEXT("전체 상태메시지 (최근 50개)"));for(auto V:Rows(S->Snapshot,TEXT("statusMessages"))){auto J=V->AsObject();Add(Str(J,TEXT("displayName"))+TEXT(" [")+Str(J,TEXT("accountId"))+TEXT("]\n")+Str(J,TEXT("statusMessage")));}
    }
    if(Page==TEXT("Blocks"))for(auto V:Rows(S->Snapshot,TEXT("blocks"))){auto J=V->AsObject();Action(Str(J,TEXT("displayName"))+TEXT(" [")+Str(J,TEXT("accountId"))+TEXT("] 차단 해제"),TEXT("Unblock"),Str(J,TEXT("accountId")));}
    if(Page==TEXT("Notifications")&&List)
    {
        for(auto V:Rows(S->Snapshot,TEXT("invites"))){auto J=V->AsObject();FString Id=Str(J,TEXT("inviteId"));List->AddSlot().AutoHeight()[Btn(Str(J,TEXT("displayName"))+TEXT("님의 방 초대: ")+Str(J,TEXT("roomName")),[this,Id,J]{ShowModal(TEXT("Invite"),Id,Str(J,TEXT("displayName"))+TEXT("님의 방: ")+Str(J,TEXT("roomName")));})];}
        for(auto V:Rows(S->Snapshot,TEXT("notifications")))Add(Str(V->AsObject(),TEXT("body")));
    }
    if(Page==TEXT("Whispers"))
    {
        auto C=Rows(S->Snapshot,TEXT("conversations"));if(C.IsEmpty())Add(TEXT("아직 대화가 없습니다. 상대 계정 ID로 대화를 열어보세요."));
        for(auto V:C){auto J=V->AsObject();FString A=Str(J,TEXT("accountId"));List->AddSlot().AutoHeight().Padding(0,5)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1)[Btn((Flag(J,TEXT("unread"))?TEXT("● "):TEXT(""))+Str(J,TEXT("displayName"))+TEXT(" [")+A+TEXT("]\n")+Str(J,TEXT("body")).Left(70),[this,A]{OpenThread(A);})]
          +SHorizontalBox::Slot().AutoWidth()[Btn(TEXT("대화 삭제"),[this,A]{GetGameInstance()->GetSubsystem<UProjectProject01SocialSubsystem>()->Act(TEXT("DeleteConversation"),A);})]];}
        Add(TEXT("삭제는 내 기록에만 적용됩니다. 새 메시지가 오면 대화가 다시 나타납니다."));
    }
    if(Page==TEXT("Thread")&&MessageLog)
    {
        if(!S->IsLobbyVisible()){MessageLog->SetText(Txt(TEXT("대화는 로비·방에서만 가능합니다.")));return;}
        auto M=Rows(S->Conversation,TEXT("messages"));FString Last=M.IsEmpty()?FString():Str(M[0]->AsObject(),TEXT("id"));Algo::Reverse(M);FString Text;
        for(auto V:M){auto J=V->AsObject();Text+=(Str(J,TEXT("direction"))==TEXT("Outgoing")?FString(TEXT("나")):ThreadAccount)+TEXT("  ")+Str(J,TEXT("createdAtUtc"))+TEXT("\n")+Str(J,TEXT("body"))+TEXT("\n\n");}
        if(!S->Conversation)Text=TEXT("대화를 불러오는 중입니다. 상대 계정이 올바른지 확인하세요.");else if(M.IsEmpty())Text=TEXT("대화를 시작해보세요.");
        MessageLog->SetText(FText::FromString(Text));if(Last!=LastMessageId){LastMessageId=Last;if(MessageScroll)MessageScroll->ScrollToEnd();S->MarkConversationRead(Last);}
    }
    if(Page==TEXT("Profile")&&List)
    {
        const TSharedPtr<FJsonObject>* P=nullptr;if(S->Profile)S->Profile->TryGetObjectField(TEXT("profile"),P);
        if(!P){Add(TEXT("프로필을 불러오는 중입니다."));return;}auto J=*P;
        List->AddSlot().AutoHeight().Padding(0,8)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).Padding(12).BorderBackgroundColor(AccentColor(Str(J,TEXT("accent"))))[Label(Str(J,TEXT("displayName"))+TEXT(" [")+Str(J,TEXT("accountId"))+TEXT("]\n")+TitleName(Str(J,TEXT("title")))+TEXT("\n")+Str(J,TEXT("statusMessage"))+TEXT("\n")+Str(J,TEXT("bio")))]];
        for(auto V:Rows(S->Profile,TEXT("statistics"))){auto R=V->AsObject();double Games=0,Wins=0,Captures=0,Rescues=0,Escape=0;R->TryGetNumberField(TEXT("matches"),Games);R->TryGetNumberField(TEXT("wins"),Wins);R->TryGetNumberField(TEXT("captures"),Captures);R->TryGetNumberField(TEXT("rescues"),Rescues);bool HasEscape=R->TryGetNumberField(TEXT("bestEscapeSeconds"),Escape);
          Add((Str(R,TEXT("role"))==TEXT("Mannequin")?FString(TEXT("마네킹")):FString(TEXT("생존자")))+FString::Printf(TEXT(" — %d경기 / %d승\n붙잡기 %d회 / 구출 %d회"),(int32)Games,(int32)Wins,(int32)Captures,(int32)Rescues)+(HasEscape?FString::Printf(TEXT("\n최단 탈출 %.1f초"),Escape):FString()));}
        if(Rows(S->Profile,TEXT("statistics")).IsEmpty())Add(TEXT("아직 서버가 보관한 경기 기록이 없습니다."));Add(Str(S->Profile,TEXT("statisticsNote")));
    }
}

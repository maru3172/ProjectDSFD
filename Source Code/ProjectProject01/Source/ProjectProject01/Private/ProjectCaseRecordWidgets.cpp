#include "ProjectCaseRecords.h"
#include "ProjectCasePresentation.h"
#include "ProjectCaseProfileSlate.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "ProjectCaseAchievementPresentation.h"
#include "ProjectMatchReplay.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01SocialSubsystem.h"
#include "ProjectProject01SocialWidget.h"
#include "ProjectProject01LobbySubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/CoreStyle.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

namespace
{
    TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& J,const TCHAR* Key){const TSharedPtr<FJsonObject>* O=nullptr;return J&&J->TryGetObjectField(Key,O)?*O:nullptr;}
    FString Str(const TSharedPtr<FJsonObject>& J,const TCHAR* Key){FString S;if(J)J->TryGetStringField(Key,S);return S;}
    double Num(const TSharedPtr<FJsonObject>& J,const TCHAR* Key){double N=0;if(J)J->TryGetNumberField(Key,N);return N;}
    bool Flag(const TSharedPtr<FJsonObject>& J,const TCHAR* Key){bool B=false;if(J)J->TryGetBoolField(Key,B);return B;}
    bool InsufficientPoints(const FString& Status){return Status==TEXT("포인트가 부족합니다")||Status==TEXT("포인트가 부족합니다.")||Status==TEXT("기록 포인트가 부족합니다.");}
    TArray<TSharedPtr<FJsonValue>> Rows(const TSharedPtr<FJsonObject>& J,const TCHAR* Key){const TArray<TSharedPtr<FJsonValue>>* A=nullptr;return J&&J->TryGetArrayField(Key,A)?*A:TArray<TSharedPtr<FJsonValue>>{};}
    UProjectCaseRecords* Records(const UUserWidget* W){auto* GI=W->GetGameInstance();return GI?GI->GetSubsystem<UProjectCaseRecords>():nullptr;}
    TSharedRef<STextBlock> Label(const FString& S,int32 Size=18){return SNew(STextBlock).Text(FText::FromString(S)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(true).LineHeightPercentage(1.3f);}
    TSharedRef<STextBlock> FixedLabel(const FString& S,int32 Size=18){auto Text=Label(S,Size);Text->SetAutoWrapText(false);return Text;}
    TSharedRef<SButton> Button(const FString& S,TFunction<void()> Click,bool Enabled=true)
    {auto Caption=Label(S);Caption->SetAutoWrapText(false);return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).IsEnabled(Enabled).OnClicked_Lambda([Click=MoveTemp(Click)]{Click();return FReply::Handled();})[Caption];}
    void Compact(const TSharedRef<SButton>& Control,int32 Size)
    {
        static const FButtonStyle Style=[] {auto S=ProjectDFSDUI::ButtonStyle();S.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));return S;}();
        Control->SetButtonStyle(&Style);Control->SetContentPadding(FMargin(8,3));
        auto Caption=StaticCastSharedRef<STextBlock>(Control->GetContent());Caption->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size));Caption->SetLineHeightPercentage(1.f);
    }
    const FProgressBarStyle& ExperienceStyle()
    {
        static const FProgressBarStyle Style=[] {auto S=FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>(TEXT("ProgressBar"));S.SetBackgroundImage(FSlateColorBrush(FLinearColor(.0165f,.0165f,.0165f))).SetFillImage(FSlateColorBrush(FLinearColor::White));return S;}();return Style;
    }
    FString AchievementName(const FString& Code)
    {
        if(Code==TEXT("campaign_checkpoint"))return TEXT("첫 기록 — 싱글 체크포인트 저장");
        if(Code==TEXT("campaign_escape"))return TEXT("출구를 찾다 — 싱글 탈출");
        if(Code==TEXT("campaign_complete"))return TEXT("사건의 끝 — 싱글 캠페인 완료");
        if(Code==TEXT("pvp_first"))return TEXT("대전 참가 — PvP 경기 완료");
        if(Code==TEXT("bot_first"))return TEXT("모의 조사 — 봇전 경기 완료");
        if(Code==TEXT("survivor_escape"))return TEXT("생존 기록 — 생존자 성공");
        if(Code==TEXT("mannequin_victory"))return TEXT("추적 기록 — 마네킹 성공");return Code;
    }

    class SCaseShopDisplay final:public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SCaseShopDisplay){} SLATE_ARGUMENT(FString,ItemId) SLATE_END_ARGS()
        void Construct(const FArguments& A){Id=A._ItemId;ChildSlot[SNew(SBox).HeightOverride(40)];}
        virtual int32 OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const override
        {
            const FVector2D S=G.GetLocalSize();const FLinearColor Tint=Style.GetColorAndOpacityTint();
            const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
            auto Box=[&](FVector2D P,FVector2D Extent,FLinearColor Color){FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(Extent,FSlateLayoutTransform(P)),White,ESlateDrawEffect::None,Color*Tint);};
            auto Line=[&](const TArray<FVector2D>& P,FLinearColor Color,float Width=1.f){FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color*Tint,true,Width);};
            Box({0,0},S,ProjectDFSDUI::Panel());
            Box({S.X*.15,4},{S.X*.7,2},FLinearColor(.21f,.21f,.21f,.5f));
            Box({0,S.Y-7},{S.X,7},FLinearColor(.072f,.072f,.072f));
            Line({{0,S.Y-7},{S.X,S.Y-7}},FLinearColor(.25f,.25f,.25f));
            // Rust and scratched glass belong to the shelf, never the item label or purchase control.
            FRandomStream Wear(int32(FCrc::StrCrc32(*Id)));
            const int32 Marks=Wear.RandRange(3,7);
            for(int32 I=0;I<Marks;++I)
            {
                const float X=S.X*Wear.FRandRange(.025f,.9f);
                Box({X,S.Y-Wear.FRandRange(3,6)},{Wear.FRandRange(4,16),Wear.FRandRange(.6f,2.f)},FLinearColor(.095f,.095f,.095f,Wear.FRandRange(.25f,.5f)));
                Line({{X,7},{X+Wear.FRandRange(-5,6),10},{X+Wear.FRandRange(-5,6),Wear.FRandRange(12,18)}},FLinearColor(.26f,.26f,.26f,Wear.FRandRange(.08f,.2f)),.7f);
            }
            const FVector2D C(S.X*.5,S.Y*.46);
            const FLinearColor Product=Id.Contains(TEXT("crimson"))?FLinearColor(.6f,.14f,.12f):FLinearColor(.58f,.67f,.65f);
            if(Id.StartsWith(TEXT("frame_")))
            {
                Line({C+FVector2D(-25,-16),C+FVector2D(25,-16),C+FVector2D(25,16),C+FVector2D(-25,16),C+FVector2D(-25,-16)},Product,3);
                Line({C+FVector2D(-19,-10),C+FVector2D(19,-10),C+FVector2D(19,10),C+FVector2D(-19,10),C+FVector2D(-19,-10)},Product*.5f);
            }
            else if(Id==TEXT("icon_seal"))
            {
                TArray<FVector2D> Ring;for(int32 I=0;I<=16;++I){const float Angle=I*2.f*PI/16;Ring.Add(C+FVector2D(FMath::Cos(Angle)*16,FMath::Sin(Angle)*16));}
                Line(Ring,Product,2);Line({C+FVector2D(-7,0),C+FVector2D(0,8),C+FVector2D(9,-9)},Product,2);
            }
            else if(Id==TEXT("icon_file"))
            {
                Line({C+FVector2D(-13,-17),C+FVector2D(7,-17),C+FVector2D(14,-10),C+FVector2D(14,17),C+FVector2D(-13,17),C+FVector2D(-13,-17)},Product,2);
                for(int32 I=0;I<3;++I)Line({C+FVector2D(-7,I*7-4),C+FVector2D(7,I*7-4)},Product);
            }
            else
            {
                Box(C+FVector2D(-35,-12),{70,24},Product*.35f);Line({C+FVector2D(-27,-4),C+FVector2D(27,-4)},Product,2);Line({C+FVector2D(-20,5),C+FVector2D(20,5)},Product);
            }
            Line({{S.X*.72,8},{S.X*.58,S.Y-10}},FLinearColor(.6f,.7f,.72f,.12f),4);
            return SCompoundWidget::OnPaint(A,G,Clip,Out,Layer+2,Style,Enabled);
        }
    private:FString Id;
    };

    // Glass storefront / directory-plan geometry, revealed as a nine-piece case puzzle.
    // Code-native drawing avoids a decorative image competing with controls or story text.
    class SCasePuzzleCell final:public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SCasePuzzleCell):_Found(false),_Selected(false),_Layout(0,0,1,1){}
            SLATE_ARGUMENT(bool,Found) SLATE_ARGUMENT(bool,Selected) SLATE_ARGUMENT(FVector4,Layout)
            SLATE_DEFAULT_SLOT(FArguments,Content)
        SLATE_END_ARGS()
        void Construct(const FArguments& A){Found=A._Found;Selected=A._Selected;Layout=A._Layout;ChildSlot[A._Content.Widget];}
        virtual int32 OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const override
        {
            const auto S=G.GetLocalSize();const auto Tint=Style.GetColorAndOpacityTint();
            const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
            FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),White,ESlateDrawEffect::None,(Found?FLinearColor(.035f,.06f,.065f):FLinearColor(.009f,.012f,.016f))*Tint);
            if(Found)
            {
                const TArray<TArray<FVector2D>> Plan{{{.15,.2},{2.85,.2},{2.85,2.8},{.15,2.8},{.15,.2}},{{.2,1.15},{1.25,1.15},{1.25,2.8}},{{1.75,.2},{1.75,1.8},{2.8,1.8}},{{.2,2.25},{1.25,2.25}},{{2.3,.2},{2.3,1.15},{1.75,1.15}}};
                for(const auto& Segment:Plan)
                {
                    TArray<FVector2D> Points;for(const auto& P:Segment)Points.Add(FVector2D((P.X/3-Layout.X)*S.X/Layout.Z,(P.Y/3-Layout.Y)*S.Y/Layout.W));
                    FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(.22f,.29f,.29f,.7f)*Tint,true,1.2f);
                }
            }
            const double W=S.X,H=S.Y,N=FMath::Min(W,H)*.12;
            const TArray<FVector2D> Seam{{0,0},{W*.5-N,0},{W*.5-N,N*.6},{W*.5,N},{W*.5+N,N*.6},{W*.5+N,0},{W,0},{W,H*.5-N},{W-N*.6,H*.5-N},{W-N,H*.5},{W-N*.6,H*.5+N},{W,H*.5+N},{W,H},{0,H},{0,0}};
            FSlateDrawElement::MakeLines(Out,Layer+2,G.ToPaintGeometry(),Seam,ESlateDrawEffect::None,(Selected?ProjectDFSDUI::Accent():FLinearColor(.16f,.19f,.2f,.8f))*Tint,true,Selected?2.f:1.f);
            return SCompoundWidget::OnPaint(A,G,Clip,Out,Layer+3,Style,Enabled);
        }
    private:bool Found=false,Selected=false;FVector4 Layout;
    };
}
UProjectCaseRecordWidget* UProjectCaseRecordWidget::OpenFor(APlayerController* Player,const FString& Tab,UUserWidget* Return)
{
    if(!Player)return nullptr;auto* Widget=CreateWidget<UProjectCaseRecordWidget>(Player);if(!Widget)return nullptr;
    Widget->Page=Tab;Widget->ReturnWidget=Return;if(Return)Return->SetVisibility(ESlateVisibility::Hidden);
    Widget->AddToViewport(1180);Player->bShowMouseCursor=true;FInputModeUIOnly Input;Input.SetWidgetToFocus(Widget->TakeWidget());Player->SetInputMode(Input);return Widget;
}
class SProjectShopFundsNotice final:public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectShopFundsNotice){}SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        SetCanTick(false);SetVisibility(EVisibility::HitTestInvisible);
        ChildSlot[SNew(STextBlock).Text(FText::FromString(TEXT("포인트가 부족합니다."))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16))
          .ColorAndOpacity_Lambda([this]{return ProjectDFSDUI::Danger().CopyWithNewOpacity(Opacity());})];
    }
    void Show(){Elapsed=0;SetCanTick(true);ForceVolatile(true);Invalidate(EInvalidateWidgetReason::Paint);}
    void Clear(){Elapsed=3.5f;SetCanTick(false);ForceVolatile(false);Invalidate(EInvalidateWidgetReason::Paint);}
    float Opacity()const{return 1.f-FMath::SmoothStep(1.f,3.5f,Elapsed);}
    virtual void Tick(const FGeometry& G,double Time,float Delta)override{SCompoundWidget::Tick(G,Time,Delta);Elapsed=FMath::Min(3.5f,Elapsed+FMath::Clamp(Delta,0.f,.1f));if(Elapsed>=3.5f)Clear();}
private:
    float Elapsed=3.5f;
};
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectShopFundsNoticeFadeTest,"ProjectProject01.CaseRecords.ShopFundsNoticeFade",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectShopFundsNoticeFadeTest::RunTest(const FString&)
{
    auto Notice=SNew(SProjectShopFundsNotice);TestEqual(TEXT("Funds feedback starts invisible"),Notice->Opacity(),0.f);Notice->Show();TestEqual(TEXT("A rejected purchase shows clear feedback"),Notice->Opacity(),1.f);
    for(int32 I=0;I<8;++I)Notice->Tick(FGeometry(),0,.1f);TestTrue(TEXT("Feedback stays clear briefly"),Notice->Opacity()>.99f);
    for(int32 I=0;I<15;++I)Notice->Tick(FGeometry(),0,.1f);TestTrue(TEXT("Feedback fades gradually rather than disappearing abruptly"),Notice->Opacity()>0&&Notice->Opacity()<1);
    for(int32 I=0;I<15;++I)Notice->Tick(FGeometry(),0,.1f);TestEqual(TEXT("Funds feedback finishes fading"),Notice->Opacity(),0.f);Notice->Show();TestEqual(TEXT("Another purchase attempt can show the feedback again"),Notice->Opacity(),1.f);return true;
}
#endif
TSharedRef<SWidget> UProjectCaseRecordWidget::RebuildWidget()
{
    ShopFundsNotice=SNew(SProjectShopFundsNotice);
    auto Tabs=SNew(SWrapBox).UseAllottedSize(true);
    auto NavigationButton=[this](const FString& Caption,const FString& Id)
    {
        auto Text=Label(Caption,16);Text->SetAutoWrapText(false);
        auto Control=SNew(SButton).ContentPadding(FMargin(10,6)).ButtonStyle(&ProjectDFSDUI::ButtonStyle())
          .OnClicked_Lambda([this,Id]{if(Page==TEXT("Draw")&&Id!=Page){bDrawResultOpen=false;bDrawRequestedHere=false;RareRevealQueue.Reset();if(RarePresentationLayer)RarePresentationLayer->ClearChildren();}Page=Id;if(Page==TEXT("Replays"))if(auto* R=GetGameInstance()->GetSubsystem<UProjectMatchReplay>())R->RefreshList();if(Page==TEXT("Archive"))if(auto* R=Records(this))R->RefreshStoryArchives();RefreshPanel();return FReply::Handled();})[Text];
        if(Page==TEXT("Track"))Compact(Control,16);return Control;
    };
    for(const auto& Entry:TArray<TPair<FString,FString>>{{TEXT("개인 기록"),TEXT("Profile")},{TEXT("컬렉션"),TEXT("Collection")},{TEXT("성장 기록"),TEXT("Achievements")},{TEXT("배틀패스"),TEXT("Track")},{TEXT("이벤트"),TEXT("Events")},{TEXT("리플레이"),TEXT("Replays")},{TEXT("사건 보관함"),TEXT("Archive")}})
        Tabs->AddSlot().Padding(3)[NavigationButton(Entry.Key,Entry.Value)];
    auto GrowthTabs=SNew(SWrapBox).UseAllottedSize(true).Visibility_Lambda([this]{return Page==TEXT("Achievements")||Page==TEXT("OnlineAchievements")||Page==TEXT("Quest")||Page==TEXT("Import")||Page==TEXT("Pvp")||Page==TEXT("Bot")||Page==TEXT("Shop")?EVisibility::Visible:EVisibility::Collapsed;});
    for(const auto& Entry:TArray<TPair<FString,FString>>{{TEXT("업적·도전과제"),TEXT("Achievements")},{TEXT("메인 퀘스트"),TEXT("Quest")},{TEXT("PvP 미션"),TEXT("Pvp")},{TEXT("봇전 미션"),TEXT("Bot")},{TEXT("보관소 상점"),TEXT("Shop")}})
        GrowthTabs->AddSlot().Padding(3)[NavigationButton(Entry.Key,Entry.Value)];
    auto Content=SNew(SBorder).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(22)
         [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
          [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Visibility_Lambda([this]{return Page==TEXT("Track")?EVisibility::Collapsed:EVisibility::Visible;}).Text(FText::FromString(TEXT("사건 기록 / 개인 조사 파일"))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),28)).ColorAndOpacity(ProjectDFSDUI::Text())]
           +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Visibility_Lambda([this]{return Page==TEXT("Track")?EVisibility::Visible:EVisibility::Collapsed;}).Text(FText::FromString(TEXT("[RECORD 0] 202X-XX-XX 18:24:01 _OMINOUS ENTRY"))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),22)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(true)]
           +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(STextBlock).Visibility_Lambda([this]{return Page==TEXT("Track")?EVisibility::Visible:EVisibility::Collapsed;}).Text(FText::FromString(TEXT("(~ 정식 출시 전까지)"))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ColorAndOpacity(ProjectDFSDUI::Muted())]]
          +SVerticalBox::Slot().AutoHeight()[Tabs]
          +SVerticalBox::Slot().AutoHeight()[GrowthTabs]
          +SVerticalBox::Slot().FillHeight(1).Padding(0,14)[SAssignNew(RecordsScroll,SScrollBox)+SScrollBox::Slot()[SAssignNew(Body,SVerticalBox)]]
          +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(20).Visibility_Lambda([this]{return Page==TEXT("Shop")?EVisibility::Collapsed:EVisibility::Visible;})[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
            .Text_Lambda([this]{const auto* R=Records(this);return FText::FromString(R&&!InsufficientPoints(R->Status)?R->Status:FString());}).ToolTipText_Lambda([this]{const auto* R=Records(this);return FText::FromString(R&&!InsufficientPoints(R->Status)?R->Status:FString());}).ColorAndOpacity(ProjectDFSDUI::Muted())]]
          +SVerticalBox::Slot().AutoHeight().Padding(0,8)[Button(TEXT("돌아가기"),[this]{Close();})]];
    return SNew(SBorder).Padding(0).OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})
        [SNew(SOverlay)+SOverlay::Slot()[ProjectDFSDUI::FrameSlate(Content,Page==TEXT("Track")?1180:1080)]
         +SOverlay::Slot()[SAssignNew(RarePresentationLayer,SOverlay).Visibility(EVisibility::SelfHitTestInvisible)]
         +SOverlay::Slot()[SAssignNew(TransactionLayer,SOverlay).Visibility(EVisibility::SelfHitTestInvisible)]];
}
void UProjectCaseRecordWidget::NativeConstruct(){Super::NativeConstruct();SetIsFocusable(true);if(auto* R=Records(this)){bOpenedOnline=R->IsAuthenticated();bShopFundsMessageHandled=InsufficientPoints(R->Status);R->OnChanged.AddUObject(this,&ThisClass::RefreshPanel);R->OnReceipt.AddUObject(this,&ThisClass::ShowReceipt);R->Refresh();R->RefreshStoryArchives();}if(auto* GI=GetGameInstance())if(auto* R=GI->GetSubsystem<UProjectMatchReplay>())R->OnChanged.AddUObject(this,&ThisClass::RefreshPanel);RefreshPanel();}
void UProjectCaseRecordWidget::NativeDestruct(){if(auto* R=Records(this)){R->OnChanged.RemoveAll(this);R->OnReceipt.RemoveAll(this);}ConfirmedAction=nullptr;if(auto* GI=GetGameInstance())if(auto* R=GI->GetSubsystem<UProjectMatchReplay>())R->OnChanged.RemoveAll(this);Super::NativeDestruct();}
void UProjectCaseRecordWidget::ReleaseSlateResources(bool Children){Super::ReleaseSlateResources(Children);Body.Reset();RecordsScroll.Reset();ShopFundsNotice.Reset();RarePresentationLayer.Reset();TransactionLayer.Reset();ConfirmedAction=nullptr;RenderedFingerprint.Reset();}
void UProjectCaseRecordWidget::Close()
{
    auto* Player=GetOwningPlayer();if(auto* Return=ReturnWidget.Get()){Return->SetVisibility(ESlateVisibility::Visible);if(Player){FInputModeGameAndUI Input;Input.SetWidgetToFocus(Return->TakeWidget());Player->SetInputMode(Input);}}
    else if(Player){FInputModeGameAndUI Input;Player->SetInputMode(Input);}RemoveFromParent();
}
void UProjectCaseRecordWidget::RefreshPanel()
{
    if(!Body)return;auto* R=Records(this);if(!R)return;
    if(Page==TEXT("Achievements")||Page==TEXT("OnlineAchievements"))R->RefreshAchievementRates();
    const bool FundsError=InsufficientPoints(R->Status);
    if(Page!=TEXT("Shop")){if(ShopFundsNotice)ShopFundsNotice->Clear();bShopFundsMessageHandled=FundsError;}
    else if(R->IsBusy()||!FundsError){bShopFundsMessageHandled=false;if(!FundsError&&!R->Status.IsEmpty()&&ShopFundsNotice)ShopFundsNotice->Clear();}
    else if(!bShopFundsMessageHandled){bShopFundsMessageHandled=true;if(ShopFundsNotice)ShopFundsNotice->Show();}
    if(bOpenedOnline&&!R->IsAuthenticated()){ReturnWidget.Reset();RemoveFromParent();return;}
    FString Encoded;if(R->Snapshot)FJsonSerializer::Serialize(R->Snapshot.ToSharedRef(),TJsonWriterFactory<>::Create(&Encoded));
    FString RateFingerprint;if(Page==TEXT("Achievements")||Page==TEXT("OnlineAchievements"))if(const auto Rates=R->GetAchievementRates())FJsonSerializer::Serialize(Rates.ToSharedRef(),TJsonWriterFactory<>::Create(&RateFingerprint));Encoded+=RateFingerprint;
    TArray<FString> Local=R->GetLocalAchievements().Array();Local.Sort();
    Encoded+=FString::Printf(TEXT("/%p/%d/%d/%d"),R->GetStoryArchives().Get(),InvestigationStage,InvestigationPiece,ArchiveIndex);
    if(R->DrawResult){FString ResultJson;FJsonSerializer::Serialize(R->DrawResult.ToSharedRef(),TJsonWriterFactory<>::Create(&ResultJson));Encoded+=ResultJson;}
    FString ReplayFingerprint;
    if(Page==TEXT("Replays"))if(const auto* Replays=GetGameInstance()->GetSubsystem<UProjectMatchReplay>())
    {ReplayFingerprint=Replays->GetStatus()+FString::FromInt(Replays->IsBusy()*2+Replays->IsRecordingEnabled());for(const auto& E:Replays->GetEntries())ReplayFingerprint+=E.Name;}
    const FString Fingerprint=Page+TEXT("|")+FString::FromInt(R->IsAuthenticated()*2+R->IsBusy())+TEXT("|")+FString::Join(Local,TEXT(","))+Encoded+ReplayFingerprint;
    if(Fingerprint==RenderedFingerprint)return;RenderedFingerprint=Fingerprint;Body->ClearChildren();
    auto Add=[&](const FString& Text,int32 Size=18){Body->AddSlot().AutoHeight().Padding(6,7)[Label(Text,Size)];};
    const auto State=R->Snapshot;const bool Online=R->IsAuthenticated(),Ready=Online&&!R->IsBusy();
    if(Page==TEXT("Quest")){BuildMainQuest(State);return;}
    if(Page==TEXT("Import"))
    {
        Add(TEXT("로컬 캠페인 업적·도전과제를 현재 로그인한 계정에 병합할까요?"),24);
        Body->AddSlot().AutoHeight()[Button(TEXT("이 계정에 병합"),[this,R]{R->ImportCampaign();Page=TEXT("Achievements");RefreshPanel();},Ready&&R->CanImport())];Body->AddSlot().AutoHeight()[Button(TEXT("아니오"),[this]{Page=TEXT("Achievements");RefreshPanel();})];return;
    }
    if(Page==TEXT("Achievements")||Page==TEXT("OnlineAchievements"))
    {
        const auto Earned=ProjectCaseAchievements::Earned(R->GetLocalAchievements(),State);
        const auto& Definitions=ProjectCaseAchievements::Definitions();
        const auto Rewards=Rows(State,TEXT("achievementRewards"));
        const auto RewardFor=[Rewards](const FString& Code){for(const auto& V:Rewards)if(Str(V->AsObject(),TEXT("code"))==Code)return V->AsObject();return TSharedPtr<FJsonObject>();};
        const auto RewardButton=[&](const FString& Code)
        {
            const auto Reward=RewardFor(Code);auto B=Button(Flag(Reward,TEXT("claimed"))?TEXT("수령 완료"):TEXT("보상 받기"),[R,Code]{R->Action(TEXT("ClaimAchievement"),Code);},Ready&&Flag(Reward,TEXT("earned"))&&!Flag(Reward,TEXT("claimed")));Compact(B,14);return B;
        };
        const bool Multi=Page==TEXT("OnlineAchievements")&&Online;
        if(Online&&!Rewards.IsEmpty())Body->AddSlot().AutoHeight().Padding(4,0,4,8)
            [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)[Button(TEXT("싱글 캠페인"),[this]{Page=TEXT("Achievements");RefreshPanel();})]
             +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("멀티플레이"),[this]{Page=TEXT("OnlineAchievements");RefreshPanel();})]];
        if(!Multi)
        {
        Body->AddSlot().AutoHeight().Padding(6,7)[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("싱글 캠페인 업적·도전과제"),23)]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Label(FString::Printf(TEXT("전체 %d/%d"),Earned.Num(),Definitions.Num()),18)]];
        auto Areas=SNew(SVerticalBox);
        for(const auto& Entry:Definitions)
        {
            const bool Achieved=Earned.Contains(Entry.Code);
            int32 AreaTotal=0,AreaEarned=0;for(const auto& Def:Definitions)if(Def.Area==Entry.Area){++AreaTotal;if(Earned.Contains(Def.Code))++AreaEarned;}
            Areas->AddSlot().AutoHeight().Padding(4,4)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.038f,.038f,.038f)).Padding(14,9)
                [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(170)[FixedLabel(FString::Printf(TEXT("%s %d/%d"),*Entry.Area,AreaEarned,AreaTotal),18)]]
                 +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(12,0)[Label(AchievementName(Entry.Code),16)]
                 +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10,0)[SNew(SBox).WidthOverride(170)[FixedLabel(ProjectCaseAchievements::GlobalRate(R->GetAchievementRates(),Entry.Code),14)]]
                 +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(62).HAlign(HAlign_Right)[FixedLabel(Achieved?TEXT("달성"):TEXT("미달성"),18)]]
                 +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12,0,0,0)[SNew(SBox).WidthOverride(106)[RewardButton(Entry.Code)]]]];
        }
        Body->AddSlot().AutoHeight()[Areas];
        }
        if(Multi)
        {
            const TArray<FString> Codes{TEXT("pvp_first"),TEXT("bot_first"),TEXT("survivor_escape"),TEXT("mannequin_victory")};
            int32 Completed=0;for(const auto& Code:Codes)Completed+=Flag(RewardFor(Code),TEXT("earned"));
            Body->AddSlot().AutoHeight().Padding(6,10,6,4)[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("멀티플레이 업적·도전과제"),21)]
                +SHorizontalBox::Slot().AutoWidth()[Label(FString::Printf(TEXT("전체 %d/4"),Completed),16)]];
            for(const auto& Code:Codes)
            {
                const bool Achieved=Flag(RewardFor(Code),TEXT("earned"));
                Body->AddSlot().AutoHeight().Padding(4,3)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.038f,.038f,.038f)).Padding(14,7)
                    [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(AchievementName(Code),16)]
                     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10,0)[SNew(SBox).WidthOverride(170)[FixedLabel(ProjectCaseAchievements::GlobalRate(R->GetAchievementRates(),Code),14)]]
                     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(62)[FixedLabel(Achieved?TEXT("달성"):TEXT("미달성"),16)]]
                     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12,0,0,0)[SNew(SBox).WidthOverride(106)[RewardButton(Code)]]]];
            }
        }
        if(Online&&!Multi)
        {
            Body->AddSlot().AutoHeight()[Button(TEXT("로컬 업적·도전과제 계정에 병합"),[this]{Page=TEXT("Import");RefreshPanel();},Ready&&R->CanImport())];
            if(!R->CanImport()&&!R->GetLocalAchievements().IsEmpty())Add(TEXT("이미 귀속한 계정과 현재 계정이 다르거나 계정 정보를 확인 중입니다."));
        }
        else if(!Online)Add(TEXT("로그인하면 로컬 업적·도전과제가 계정에 병합됩니다. 네트워크가 불안정하면 로그인 후 다시 병합할 수 있습니다."));return;
    }
    if(Page==TEXT("Replays"))
    {
        auto* Replays=GetGameInstance()->GetSubsystem<UProjectMatchReplay>();if(!Replays)return;
        Add(TEXT("저장된 경기 리플레이"),24);Add(TEXT("자동 녹화를 켠 뒤 진행한 경기를 로비에서 재생할 수 있습니다. 보관 한도 10개 / 2GB, 경기당 최대 512MB / 60분입니다."));
        Add(Replays->GetStatus());
        Body->AddSlot().AutoHeight()[Button(Replays->IsRecordingEnabled()?TEXT("경기 자동 녹화: 켜짐"):TEXT("경기 자동 녹화: 꺼짐"),[Replays]{Replays->SetRecordingEnabled(!Replays->IsRecordingEnabled());},!Replays->IsBusy())];
        Body->AddSlot().AutoHeight()[Button(TEXT("목록 새로고침"),[Replays]{Replays->RefreshList();},!Replays->IsBusy())];
        for(const auto& Entry:Replays->GetEntries())
        {
            Add(FString::Printf(TEXT("%s · %d분 %02d초 · %.1f MB"),*Entry.Description,Entry.DurationMs/60000,(Entry.DurationMs/1000)%60,Entry.Bytes/1048576.));
            auto Actions=SNew(SHorizontalBox);
            Actions->AddSlot().FillWidth(1)[Button(TEXT("재생"),[this,Replays,Name=Entry.Name]{if(Replays->Play(Name))RemoveFromParent();},!Replays->IsBusy()&&Entry.DurationMs>0)];
            Actions->AddSlot().AutoWidth()[Button(TEXT("삭제"),[Replays,Name=Entry.Name]{Replays->Delete(Name);},!Replays->IsBusy())];Body->AddSlot().AutoHeight()[Actions];
        }return;
    }
    if(Page==TEXT("Archive"))
    {
        const auto Archives=Rows(R->GetStoryArchives(),TEXT("archives"));
        if(Archives.IsEmpty()){Add(TEXT("공개된 시즌 기록이 없습니다."));return;}
        ArchiveIndex=FMath::Clamp(ArchiveIndex,0,Archives.Num()-1);const auto Archive=Archives[ArchiveIndex]->AsObject();
        auto Seasons=SNew(SHorizontalBox);
        Seasons->AddSlot().AutoWidth()[Button(TEXT("이전 시즌"),[this]{--ArchiveIndex;RefreshPanel();},ArchiveIndex>0)];
        Seasons->AddSlot().FillWidth(1).HAlign(HAlign_Center)[Label(Str(Archive,TEXT("season")),20)];
        Seasons->AddSlot().AutoWidth()[Button(TEXT("다음 시즌"),[this]{++ArchiveIndex;RefreshPanel();},ArchiveIndex+1<Archives.Num())];Body->AddSlot().AutoHeight().Padding(0,0,0,8)[Seasons];
        BuildStoryBook(Archive,true);return;
    }
    if(Page==TEXT("Events")){Add(TEXT("진행 중인 이벤트가 없습니다."));return;}
    if(!Online){Add(TEXT("이 항목은 로그인 후 이용할 수 있습니다. 로컬 업적·도전과제는 위의 업적·도전과제 탭에서 확인하세요."));return;}
    if(!State){Add(TEXT("계정 기록을 확인 중입니다…"));return;}
    if(Page==TEXT("Collection")){BuildCollection(State);return;}
    if(Page==TEXT("Draw")){BuildDraw(State);return;}
    if(Page==TEXT("Profile"))
    {
        const auto ProfileRoot=Body;auto IdentityColumn=SNew(SVerticalBox);auto DetailsColumn=SNew(SVerticalBox);
        if(bEditingProfile)ProfileRoot->AddSlot().AutoHeight()[IdentityColumn];
        else ProfileRoot->AddSlot().AutoHeight()[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(.46f).Padding(0,0,12,0)[IdentityColumn]+SHorizontalBox::Slot().FillWidth(.54f)[DetailsColumn]];Body=IdentityColumn;
        const auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();const auto Collection=Obj(State,TEXT("collection"));
        if(!bEditingProfile)Body->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(ProjectCaseProfile::SDossier).State(State).DisplayName(A?A->GetSignedInDisplayName():FString()).Bio(Str(Collection,TEXT("bio")))];
        auto ResetPanel=[this]{RenderedFingerprint.Reset();RefreshPanel();};
        Body->AddSlot().AutoHeight()[Button(bEditingProfile?TEXT("닫기"):TEXT("프로필 편집"),[this,Collection,ResetPanel]
        {bEditingProfile=!bEditingProfile;if(bEditingProfile){BioDraft=Str(Collection,TEXT("bio"));FeaturedDraft.Reset();for(auto V:Rows(Collection,TEXT("featured")))FeaturedDraft.Add(Str(V->AsObject(),TEXT("code")));}ResetPanel();},Ready)];
        if(bEditingProfile)
        {
            Body->AddSlot().AutoHeight().Padding(4,8)[SNew(SBox).HeightOverride(84)[SNew(SMultiLineEditableTextBox).Style(&ProjectDFSDUI::InputStyle()).Text(FText::FromString(BioDraft)).HintText(FText::FromString(TEXT("소개 글"))).ForegroundColor(ProjectDFSDUI::Text()).OnTextChanged_Lambda([this](const FText& Value){BioDraft=Value.ToString().Left(500);})]];
            Add(FString::Printf(TEXT("대표 업적·도전과제 %d / 3"),FeaturedDraft.Num()),18);auto Choices=SNew(SWrapBox).UseAllottedSize(true);
            for(auto V:Rows(State,TEXT("achievements"))){const FString Code=Str(V->AsObject(),TEXT("code"));const bool Selected=FeaturedDraft.Contains(Code);
                auto B=Button((Selected?TEXT("◆ "):TEXT("◇ "))+ProjectCaseProfile::Achievement(Code),[this,Code,ResetPanel]{if(FeaturedDraft.Contains(Code))FeaturedDraft.Remove(Code);else if(FeaturedDraft.Num()<3)FeaturedDraft.Add(Code);ResetPanel();},Ready&&(Selected||FeaturedDraft.Num()<3));Compact(B,14);Choices->AddSlot().Padding(3)[B];}
            Body->AddSlot().AutoHeight()[Choices];Body->AddSlot().AutoHeight().Padding(4,8)[Button(TEXT("저장"),[this,R,ResetPanel]{R->SaveFeatured(FeaturedDraft,BioDraft);bEditingProfile=false;ResetPanel();},Ready)];
            Body=ProfileRoot;return;
        }
        Body=DetailsColumn;const bool MaxLevel=Num(State,TEXT("level"))>=100;
        Add(FString::Printf(TEXT("계정 레벨 %d  |  %s"),int32(Num(State,TEXT("level"))),MaxLevel?TEXT("최고 레벨"):*FString::Printf(TEXT("경험치 %.0f / %.0f"),Num(State,TEXT("xp"))-Num(State,TEXT("levelStartXp")),Num(State,TEXT("nextLevelXp"))-Num(State,TEXT("levelStartXp")))));
        Body->AddSlot().AutoHeight().Padding(6,8)[SNew(SBox).HeightOverride(8)[SNew(SProgressBar).Style(&ExperienceStyle()).Percent(float(Num(State,TEXT("levelProgress")))).FillColorAndOpacity(ProjectDFSDUI::Accent())]];
        auto Rewards=Rows(State,TEXT("levelRewards"));
        Rewards.Sort([](const auto& A,const auto& B){return Num(A->AsObject(),TEXT("level"))<Num(B->AsObject(),TEXT("level"));});
        Body->AddSlot().AutoHeight().Padding(6,7)[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(TEXT("레벨 구간 보상"),22)]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("한 번에 받기"),[R]{R->Action(TEXT("ClaimLevelReward"),TEXT("All"));},Ready&&Rewards.ContainsByPredicate([](const auto& V){return Flag(V->AsObject(),TEXT("available"))&&!Flag(V->AsObject(),TEXT("claimed"));}))]];
        for(const auto& Value:Rewards)
        {
            auto Reward=Value->AsObject();const FString Id=FString::FromInt(int32(Num(Reward,TEXT("level"))));const FString Item=Str(Reward,TEXT("itemId"));
            if(Flag(Reward,TEXT("claimed")))continue;
            Add(FString::Printf(TEXT("레벨 %s · %.0f 포인트%s"),*Id,Num(Reward,TEXT("credits")),Item.IsEmpty()?TEXT(""):*(TEXT(" + ")+ProjectCasePresentation::ItemName(Item))));
            auto Actions=SNew(SHorizontalBox);Actions->AddSlot().FillWidth(1)[Button(Flag(Reward,TEXT("claimed"))?TEXT("수령 완료"):TEXT("레벨 보상 받기"),[R,Id]{R->Action(TEXT("ClaimLevelReward"),Id);},Ready&&Flag(Reward,TEXT("available")))];
            if(!Item.IsEmpty())Actions->AddSlot().AutoWidth()[Button(TEXT("테두리 장착"),[R,Item]{R->Action(TEXT("Equip"),Item);},Ready&&Flag(Reward,TEXT("claimed")))];Body->AddSlot().AutoHeight()[Actions];
            break;
        }
        Add(TEXT("현재 장착한 꾸미기"),22);
        const auto Loadout=Rows(State,TEXT("loadout"));
        bool HasCosmetic=false;
        for(const auto& Value:Loadout){const FString Id=Str(Value->AsObject(),TEXT("itemId"));if(!Id.IsEmpty()){Add(ProjectCaseProfile::Name(Id));HasCosmetic=true;}}
        if(!HasCosmetic)Add(TEXT("현재 아무것도 적용되지 않았습니다."));Body=ProfileRoot;return;
    }
    if(Page==TEXT("Bot")||Page==TEXT("Pvp"))
    {
        const bool AnyClaimable=Rows(State,TEXT("missions")).ContainsByPredicate([this](const auto& V){auto M=V->AsObject();return Str(M,TEXT("mode"))==Page&&!Flag(M,TEXT("claimed"))&&Num(M,TEXT("progress"))>=Num(M,TEXT("target"))&&(!M->HasField(TEXT("canClaim"))||Flag(M,TEXT("canClaim")));});
        auto ClaimAll=Button(TEXT("한 번에 받기"),[R,Mode=Page]{R->Action(TEXT("ClaimAllMissions"),TEXT("All"),Mode);},Ready&&AnyClaimable);Compact(ClaimAll,14);
        Body->AddSlot().AutoHeight().Padding(6,7)[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(Page==TEXT("Bot")?TEXT("봇전 일일 / 주간 미션"):TEXT("PvP 일일 / 주간 미션"),24)]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ClaimAll]];
        Add(FString::Printf(TEXT("주간 상한 %.0f/%.0f"),Num(State,TEXT("missionWeeklyEarned")),State&&State->HasField(TEXT("missionWeeklyLimit"))?Num(State,TEXT("missionWeeklyLimit")):2000.0),18);
        for(const auto& Value:Rows(State,TEXT("missions")))
        {
            auto M=Value->AsObject();if(Str(M,TEXT("mode"))!=Page)continue;
            auto Row=SNew(SHorizontalBox);
            auto Name=Label(FString::Printf(TEXT("[%s] %s"),Flag(M,TEXT("weekly"))?TEXT("주간"):TEXT("일일"),*Str(M,TEXT("name"))),16);Name->SetAutoWrapText(false);Name->SetLineHeightPercentage(1.f);Name->SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);Name->SetToolTipText(Name->GetText());
            auto Progress=Label(FString::Printf(TEXT("%.0f / %.0f · 보상 %.0f 포인트"),Num(M,TEXT("progress")),Num(M,TEXT("target")),Num(M,TEXT("reward"))),14);Progress->SetLineHeightPercentage(1.f);
            Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Name]
                +SVerticalBox::Slot().AutoHeight()[Progress]];
            auto Claim=Button(Flag(M,TEXT("claimed"))?TEXT("수령 완료"):TEXT("보상 받기"),[R,Id=Str(M,TEXT("id")),Mode=Page]{R->Action(TEXT("ClaimMission"),Id,Mode);},Ready&&!Flag(M,TEXT("claimed"))&&Num(M,TEXT("progress"))>=Num(M,TEXT("target"))&&(!M->HasField(TEXT("canClaim"))||Flag(M,TEXT("canClaim"))));
            auto Change=Button(FString::Printf(TEXT("변경 · %.0f P"),Num(M,TEXT("rerollCost"))),[R,Id=Str(M,TEXT("id")),Mode=Page,Count=int32(Num(M,TEXT("rerolls")))]{R->Action(TEXT("RerollMission"),Id,Mode,Count);},Ready&&Flag(M,TEXT("canReroll"))&&Num(State,TEXT("credits"))>=Num(M,TEXT("rerollCost")));
            for(auto Control:{Claim,Change})Compact(Control,14);
            Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6,0)[SNew(SBox).WidthOverride(92)[Claim]];
            Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(118)[Change]];
            Body->AddSlot().AutoHeight().Padding(4,3)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(8,4)[Row]];
        }return;
    }
    if(Page==TEXT("Shop"))
    {
        Add(FString::Printf(TEXT("보관소 상점 / 기록 포인트 %.0f"),Num(State,TEXT("credits"))),24);
        Body->AddSlot().AutoHeight().Padding(6,0,6,8)[SNew(SBox).HeightOverride(24)[SNew(SOverlay)+SOverlay::Slot()[ShopFundsNotice.ToSharedRef()]
          +SOverlay::Slot()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ColorAndOpacity(ProjectDFSDUI::Muted()).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
            .Text_Lambda([this]{auto* Record=Records(this);return FText::FromString(Record&&!InsufficientPoints(Record->Status)?Record->Status:FString());}).ToolTipText_Lambda([this]{auto* Record=Records(this);return FText::FromString(Record&&!InsufficientPoints(Record->Status)?Record->Status:FString());})]]];
        TSet<FString> Owned;for(const auto& V:Rows(State,TEXT("inventory")))Owned.Add(Str(V->AsObject(),TEXT("itemId")));
        auto Catalogue=Rows(State,TEXT("catalogue"));
        // Older level rewards leave the progression panel, but their owned cosmetics remain usable.
        TArray<FString> OwnedIds=Owned.Array();OwnedIds.Sort();
        for(const FString& Id:OwnedIds)if(!Catalogue.ContainsByPredicate([&](const auto& V){return Str(V->AsObject(),TEXT("id"))==Id;}))
        {auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("id"),Id);Item->SetStringField(TEXT("name"),ProjectCaseProfile::Name(Id));Item->SetNumberField(TEXT("price"),0);Catalogue.Add(MakeShared<FJsonValueObject>(Item));}
        auto Shelves=SNew(SUniformGridPanel).SlotPadding(FMargin(4));int32 Product=0;
        for(const auto& Value:Catalogue)
        {
            auto I=Value->AsObject();const FString Id=Str(I,TEXT("id"));
            auto Buy=Button(Owned.Contains(Id)?TEXT("장착"):TEXT("구매"),[this,R,Id,Has=Owned.Contains(Id)]{if(Has)R->Action(TEXT("Equip"),Id);else RequestPurchase(Id);},Ready);
            Compact(Buy,16);
            auto Name=Label(Str(I,TEXT("name")),16);Name->SetLineHeightPercentage(1.f);Name->SetAutoWrapText(false);Name->SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);Name->SetToolTipText(Name->GetText());
            auto Price=Label(Owned.Contains(Id)?FString(TEXT("보유 중")):FString::Printf(TEXT("%.0f 포인트"),Num(I,TEXT("price"))),14);Price->SetLineHeightPercentage(1.f);
            Shelves->AddSlot(Product%3,Product/3)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(6)
                [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(SCaseShopDisplay).ItemId(Id)]
                 +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[Name]
                 +SVerticalBox::Slot().AutoHeight()[Price]
                 +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[Buy]]];++Product;
        }
        Body->AddSlot().AutoHeight()[Shelves];return;
    }
    if(Page==TEXT("Track"))
    {
        const auto Track=Obj(State,TEXT("season"));
        const auto Investigation=Obj(Track,TEXT("investigation"));
        if(!Flag(Investigation,TEXT("enabled"))){Add(TEXT("조사 일정 준비 중입니다."));return;}
        if(Flag(Investigation,TEXT("ended")))
        {
            Body->AddSlot().AutoHeight()[Button(TEXT("종료된 시즌 이야기 열람"),[this,R]{Page=TEXT("Archive");R->RefreshStoryArchives();RefreshPanel();})];return;
        }
        BuildStoryBook(Investigation,false);return;
    }
}
void UProjectCaseRecordWidget::BuildStoryBook(const TSharedPtr<FJsonObject>& Investigation,bool Archived)
{
    auto* R=Records(this);if(!R||!Investigation)return;
    InvestigationStage=FMath::Clamp(InvestigationStage,1,13);InvestigationPiece=FMath::Clamp(InvestigationPiece,1,9);
    TSharedPtr<FJsonObject> Stage,Selected;TArray<TSharedPtr<FJsonObject>> Pieces;int32 Found=0;
    for(const auto& Value:Rows(Investigation,TEXT("stages")))if(int32(Num(Value->AsObject(),TEXT("stage")))==InvestigationStage){Stage=Value->AsObject();break;}
    for(const auto& Value:Rows(Investigation,TEXT("records")))
        if(int32(Num(Value->AsObject(),TEXT("stage")))==InvestigationStage){Pieces.Add(Value->AsObject());Found+=Flag(Value->AsObject(),TEXT("discovered"))?1:0;}
    auto Pager=SNew(SHorizontalBox);
    auto Previous=Button(TEXT("이전 파트"),[this]{--InvestigationStage;InvestigationPiece=1;RefreshPanel();},InvestigationStage>1);Compact(Previous,16);
    Pager->AddSlot().AutoWidth()[Previous];
    auto PageCaption=Label(FString::Printf(TEXT("%d / 13 파트%s"),InvestigationStage,Archived?TEXT(""):*FString::Printf(TEXT(" · %d / 9 조각 · 이번 주 %.0f/%.0f"),Found,Num(Investigation,TEXT("collectedThisWeek")),Num(Investigation,TEXT("weeklyLimit")))),18);PageCaption->SetAutoWrapText(false);
    Pager->AddSlot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)[PageCaption];
    auto Next=Button(TEXT("다음 파트"),[this]{++InvestigationStage;InvestigationPiece=1;RefreshPanel();},InvestigationStage<13);Compact(Next,16);Pager->AddSlot().AutoWidth()[Next];
    Body->AddSlot().AutoHeight().Padding(0,0,0,12)[Pager];
    auto Board=SNew(SGridPanel);
    static const FButtonStyle PuzzleButtonStyle=[] {auto Style=ProjectDFSDUI::ButtonStyle();Style.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));return Style;}();
    for(int32 I=0;I<14;++I)Board->SetColumnFill(I,1.f);for(int32 I=0;I<10;++I)Board->SetRowFill(I,1.f);
    const TArray<FIntRect> Layout{{0,0,5,4},{5,0,9,2},{9,0,14,2},{5,2,10,5},{10,2,14,5},{0,4,5,8},{5,5,9,10},{9,5,14,10},{0,8,5,10}};
    for(int32 Piece=1;Piece<=9;++Piece)
    {
        TSharedPtr<FJsonObject> Item;for(const auto& Candidate:Pieces)if(int32(Num(Candidate,TEXT("part")))==Piece){Item=Candidate;break;}
        if(Piece==InvestigationPiece)Selected=Item;
        const bool Collected=Archived||Flag(Item,TEXT("discovered"));const auto Cell=Layout[Piece-1];
        Board->AddSlot(Cell.Min.X,Cell.Min.Y).ColumnSpan(Cell.Width()).RowSpan(Cell.Height()).Padding(1)
          [SNew(SButton).ContentPadding(0).ButtonStyle(&PuzzleButtonStyle).OnClicked_Lambda([this,Piece]{InvestigationPiece=Piece;RefreshPanel();return FReply::Handled();})
            [SNew(SCasePuzzleCell).Found(Collected).Selected(Piece==InvestigationPiece).Layout(FVector4(Cell.Min.X/14.f,Cell.Min.Y/10.f,Cell.Width()/14.f,Cell.Height()/10.f)).Clipping(EWidgetClipping::ClipToBounds)
              [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::AsNumber(Piece)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).ColorAndOpacity(Collected?ProjectDFSDUI::Text():ProjectDFSDUI::Muted()).Justification(ETextJustify::Center)]
                +SVerticalBox::Slot().FillHeight(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Collected?Str(Item,TEXT("description")):FString())).ToolTipText(FText::FromString(Collected?Str(Item,TEXT("description")):FString())).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(true).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Justification(ETextJustify::Center)]]]];
    }
    const bool Available=Archived||Flag(Stage,TEXT("storyAvailable"));
    auto BoardHeight=[this,Archived]()->float
    {
        const float ViewHeight=RecordsScroll.IsValid()?RecordsScroll->GetCachedGeometry().GetLocalSize().Y:0;
        return FMath::Clamp(ViewHeight>0?ViewHeight-(Archived?185.f:150.f):300.f,180.f,420.f);
    };
    auto Spread=SNew(SHorizontalBox);
    Spread->AddSlot().AutoWidth()[SNew(SBox).WidthOverride_Lambda([BoardHeight]{return FOptionalSize(BoardHeight()*.9f);})
       [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6).HAlign(HAlign_Center)[FixedLabel(FString::Printf(TEXT("파트 완성 %d/7"),Archived?7:FMath::Min(Found,7)),18)]
        +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride_Lambda([BoardHeight]{return FOptionalSize(BoardHeight());})[Board]]]];
    Spread->AddSlot().FillWidth(1).Padding(12,0,0,0)
       [SNew(SBox).HeightOverride_Lambda([BoardHeight]{return FOptionalSize(BoardHeight()+30.f);})[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(18)
         [SNew(SVerticalBox)
          +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Label(Str(Stage,TEXT("title")),18)]
          +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Label(Available?Str(Stage,TEXT("story")):TEXT("이야기 잠김"),18)]]]]];
    Body->AddSlot().AutoHeight()[Spread];
    if(!Archived)
    {
        const bool Ready=R->IsAuthenticated()&&!R->IsBusy();const FString Id=Str(Selected,TEXT("id"));auto Rewards=SNew(SHorizontalBox);
        auto PieceReward=Button(Flag(Selected,TEXT("claimed"))?TEXT("조각 보상 수령 완료"):FString::Printf(TEXT("%d번 조각 · %.0f 포인트 받기"),InvestigationPiece,Num(Selected,TEXT("points"))),[R,Id]{R->Action(TEXT("ClaimRecord"),Id);},Ready&&Flag(Selected,TEXT("discovered"))&&!Flag(Selected,TEXT("claimed")));
        auto PartReward=Button(Flag(Stage,TEXT("claimed"))?TEXT("완성 봉인 장착"):TEXT("파트 완성 보상 받기"),[R,Stage,Part=InvestigationStage]{R->Action(Flag(Stage,TEXT("claimed"))?TEXT("Equip"):TEXT("ClaimInvestigationStage"),Flag(Stage,TEXT("claimed"))?Str(Stage,TEXT("rareItem")):FString::FromInt(Part));},Ready&&Flag(Stage,TEXT("complete")));
        Compact(PieceReward,16);Compact(PartReward,16);
        Rewards->AddSlot().FillWidth(1).Padding(0,10,10,0)[PieceReward];
        Rewards->AddSlot().FillWidth(1).Padding(10,10,0,0)[PartReward];
        Body->AddSlot().AutoHeight()[Rewards];
    }
}
TSharedRef<SWidget> UProjectCaseRecordDock::RebuildWidget()
{
    auto Party=[this]{auto* R=Records(this);return Obj(Obj(R?R->Snapshot:nullptr,TEXT("partyState")),TEXT("party"));};
    auto Other=[this,Party](int32 Index)->TSharedPtr<FJsonObject>
    {
        auto* R=Records(this);const FString Self=Str(R?R->Snapshot:nullptr,TEXT("userId"));int32 Found=0;
        for(const auto& V:Rows(Party(),TEXT("members"))){auto M=V->AsObject();if(Str(M,TEXT("userId"))!=Self&&Found++==Index)return M;}return nullptr;
    };
    static const FButtonStyle DockStyle=[] {auto S=ProjectDFSDUI::ButtonStyle();S.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));return S;}();
    auto OwnNameplate=SNew(SOverlay)
        +SOverlay::Slot()[SNew(ProjectCaseProfile::SInlineBanner).Background(true).ItemId_Lambda([this]{auto* R=Records(this);return ProjectCasePresentation::Equipped(R?R->Snapshot:nullptr,TEXT("Banner"));})]
        +SOverlay::Slot()[SNew(STextBlock).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([this]
        {auto* R=Records(this);auto* A=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>():nullptr;return FText::FromString(ProjectCasePresentation::Identity(R?R->Snapshot:nullptr,A?A->GetSignedInDisplayName():FString()));})];
    auto Profile=SNew(SButton).ButtonStyle(&DockStyle).ContentPadding(4).ToolTipText(FText::FromString(TEXT("개인 기록")))
      .OnClicked_Lambda([this]{UProjectCaseRecordWidget::OpenFor(GetOwningPlayer(),TEXT("Profile"));return FReply::Handled();})
      [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)[ProjectDFSDUI::Icon(ProjectDFSDUI::EIcon::Profile,28)]
        +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
          +SVerticalBox::Slot().AutoHeight()[OwnNameplate]
          +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]{auto* R=Records(this);return FText::FromString(FString::Printf(TEXT("Lv.%d"),R?int32(Num(R->Snapshot,TEXT("level"))):1));})]]]
        +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[SNew(SBox).HeightOverride(4)[SNew(SProgressBar).Style(&ExperienceStyle()).Percent_Lambda([this]{auto* R=Records(this);return float(Num(R?R->Snapshot:nullptr,TEXT("levelProgress")));}).FillColorAndOpacity(ProjectDFSDUI::Accent())]]];
    auto SelfColumn=SNew(SVerticalBox);
    SelfColumn->AddSlot().AutoHeight()[SNew(SBorder).Padding(0).BorderBackgroundColor(FLinearColor::Transparent).OnMouseButtonDown_Lambda([this](const FGeometry&,const FPointerEvent& E)
      {auto* R=Records(this);if(E.GetEffectingButton()==EKeys::RightMouseButton&&R&&SocialOverlay.IsValid()&&SocialOverlay->ShowPartyMemberMenu(Str(R->Snapshot,TEXT("userId")),E.GetScreenSpacePosition()))return FReply::Handled();return FReply::Unhandled();})[SNew(SBox).HeightOverride(64)[Profile]]];
    auto Leave=Button(TEXT("파티 나가기"),[this]{if(auto* R=Records(this))R->PartyAction(TEXT("Leave"));});Compact(Leave,13);Leave->SetContentPadding(FMargin(8,1));
    Leave->SetVisibility(TAttribute<EVisibility>::CreateLambda([Party]{return Party()?EVisibility::Visible:EVisibility::Collapsed;}));
    Leave->SetEnabled(TAttribute<bool>::CreateLambda([this]{auto* R=Records(this);return R&&!R->IsBusy();}));SelfColumn->AddSlot().AutoHeight().Padding(0,3,0,0)[Leave];
    auto Members=SNew(SHorizontalBox);
    Members->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0,0,8,0)[SNew(SBox).WidthOverride(185)[SelfColumn]];
    for(int32 Index=0;Index<2;++Index)
    {
        auto Member=SNew(SBorder).BorderImage(&DockStyle.Normal).Padding(6)
          [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)[ProjectDFSDUI::Icon(ProjectDFSDUI::EIcon::Profile,28)]
           +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
             +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([Other,Index]{return FText::FromString(Str(Other(Index),TEXT("displayName")));})]
             +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([Other,Index]{return FText::FromString(FString::Printf(TEXT("Lv.%d"),FMath::Max(1,int32(Num(Other(Index),TEXT("level"))))));})]]];
        auto Hit=SNew(SBorder).Padding(0).BorderBackgroundColor(FLinearColor::Transparent).Visibility_Lambda([Other,Index]{return Other(Index)?EVisibility::Visible:EVisibility::Collapsed;})
          .OnMouseButtonDown_Lambda([this,Other,Index](const FGeometry&,const FPointerEvent& E){auto M=Other(Index);if(E.GetEffectingButton()==EKeys::RightMouseButton&&M&&SocialOverlay.IsValid()&&SocialOverlay->ShowPartyMemberMenu(Str(M,TEXT("userId")),E.GetScreenSpacePosition()))return FReply::Handled();return FReply::Unhandled();})[Member];
        Members->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0,0,8,0)[SNew(SBox).WidthOverride(185).HeightOverride(64).Visibility_Lambda([Other,Index]{return Other(Index)?EVisibility::Visible:EVisibility::Collapsed;})[Hit]];
    }
    auto Invite=SNew(SButton).ButtonStyle(&DockStyle).ContentPadding(6).ToolTipText(FText::FromString(TEXT("파티 초대")))
      .IsEnabled_Lambda([this,Party]{auto* R=Records(this);auto P=Party();return R&&!R->IsBusy()&&(!P||(Flag(P,TEXT("isLeader"))&&Rows(P,TEXT("members")).Num()<3));})
      .OnClicked_Lambda([this]{if(SocialOverlay.IsValid())SocialOverlay->OpenPartyInvite();return FReply::Handled();})[ProjectDFSDUI::Icon(ProjectDFSDUI::EIcon::Add,24)];
    auto InviteVisibility=TAttribute<EVisibility>::CreateLambda([Party]{return Rows(Party(),TEXT("members")).Num()<3?EVisibility::Visible:EVisibility::Collapsed;});
    Invite->SetVisibility(InviteVisibility);
    Members->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0,0,8,0)[SNew(SBox).WidthOverride(40).HeightOverride(64).Visibility(InviteVisibility)[Invite]];
    Members->AddSlot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(118).HeightOverride(64).VAlign(VAlign_Center)[SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight()[Label(TEXT("보유 포인트"),14)]
      +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([this]{auto* R=Records(this);return FText::AsNumber(int64(Num(R?R->Snapshot:nullptr,TEXT("credits"))));})]]];
    auto Left=SNew(SBox).WidthOverride_Lambda([this]{return FMath::Max(200.f,FMath::Min(760.f,ProjectDFSDUI::PageMetrics(GetCachedGeometry()).Width-225.f));})[Members];
    return SNew(SOverlay).Visibility_Lambda([this]{auto* R=Records(this);return R&&R->IsAuthenticated()?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;})
      +SOverlay::Slot()[ProjectDFSDUI::FooterSlate(SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(12,8)
        [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Left]+SHorizontalBox::Slot().FillWidth(1)[SNullWidget::NullWidget]])];
}
void UProjectCaseRecordDock::NativeConstruct(){Super::NativeConstruct();if(auto* R=Records(this))R->Refresh();}
void UProjectCaseRecordDock::SetSocialOverlay(UProjectProject01SocialWidget* Social){SocialOverlay=Social;}
void UProjectCaseRecordDock::NativeDestruct(){Super::NativeDestruct();}
void UProjectCaseOpenButton::OpenRecords(){if(ReturnWidget)UProjectCaseRecordWidget::OpenFor(ReturnWidget->GetOwningPlayer(),TEXT("Achievements"),ReturnWidget);}

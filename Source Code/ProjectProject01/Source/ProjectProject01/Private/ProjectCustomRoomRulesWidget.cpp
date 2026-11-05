#include "ProjectCustomRoomRulesWidget.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectConfirmDialog.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Rendering/DrawElements.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ScopeExit.h"

namespace
{
    TSharedRef<STextBlock> Caption(const FString& Text,int Size=18)
    {return SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).ColorAndOpacity(ProjectDFSDUI::Text());}
    const FCheckBoxStyle& ToggleStyle()
    {
        static const FCheckBoxStyle Style=[]
        {
            auto S=FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("Checkbox"));
            S.SetUncheckedImage(FSlateRoundedBoxBrush(ProjectDFSDUI::Panel(),2.f,ProjectDFSDUI::Muted(),1.f,FVector2D(20,20)));
            S.SetUncheckedHoveredImage(S.UncheckedImage).SetUncheckedPressedImage(S.UncheckedImage);
            S.SetCheckedImage(FSlateRoundedBoxBrush(ProjectDFSDUI::Accent(),2.f,FVector2D(20,20)));
            S.SetCheckedHoveredImage(S.CheckedImage).SetCheckedPressedImage(S.CheckedImage);return S;
        }();return Style;
    }
    /** The theme button without its own padding, so a fill inside (picked affix, active tab) covers the whole button. */
    const FButtonStyle& TightButtonStyle()
    {
        static const FButtonStyle Style=[]{FButtonStyle S=ProjectDFSDUI::ButtonStyle();S.SetNormalPadding(FMargin(0));S.SetPressedPadding(FMargin(0));return S;}();
        return Style;
    }
    class SRulesChevron final:public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SRulesChevron){}SLATE_END_ARGS()
        void Construct(const FArguments&){ChildSlot[SNew(SBox).WidthOverride(14).HeightOverride(14)];}
        virtual int32 OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& O,int32 L,const FWidgetStyle& S,bool Enabled)const override
        {
            const auto Size=G.GetLocalSize();TArray<FVector2D> Points{{2,Size.Y*.35},{Size.X*.5,Size.Y*.7},{Size.X-2,Size.Y*.35}};
            FSlateDrawElement::MakeLines(O,L,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,ProjectDFSDUI::Text()*S.GetColorAndOpacityTint(),true,1.5f);
            return SCompoundWidget::OnPaint(A,G,C,O,L+1,S,Enabled);
        }
    };
}
UProjectCustomRoomRulesWidget* UProjectCustomRoomRulesWidget::OpenFor(APlayerController* Player,UUserWidget* Return)
{
    if(!Player||!Player->GetGameInstance())return nullptr;
    auto* Lobby=Player->GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>();
    if(!Lobby||!Lobby->GetCurrentRoom().bIsHost)return nullptr;
    auto* W=CreateWidget<UProjectCustomRoomRulesWidget>(Player);if(!W)return nullptr;
    W->SetIsFocusable(true);
    W->RoomId=Lobby->GetCurrentRoom().RoomId;W->ReturnWidget=Return;W->Draft=Lobby->GetCurrentRoom().CustomRules;
    W->AddToViewport(1250);Player->bShowMouseCursor=true;FInputModeUIOnly Input;Input.SetWidgetToFocus(W->TakeWidget());Player->SetInputMode(Input);return W;
}
bool UProjectCustomRoomRulesWidget::CanEdit()const
{
    auto* GI=GetGameInstance();auto* L=GI?GI->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
    if(!L)return false;const auto& R=L->GetCurrentRoom();
    return bEditable&&!Request&&R.RoomId==RoomId&&R.bIsHost&&!R.bBotMatch&&!R.bRandomMatch&&!R.bRandomParty&&!R.bStarting&&!R.bStarted&&!R.bServerPreparing;
}
FProjectCustomMatchRules UProjectCustomRoomRulesWidget::Outgoing()const
{
    FProjectCustomMatchRules R=Draft;
    if(!R.bEnabled){R.AffixIds.Reset();R.bItemCards=R.bInfiniteLives=R.bInfection=R.bGhosts=false;R.HideRevealSeconds=0;R.GazeLimitSeconds=R.Darkness=R.SoundOnlyMeters=R.EscapeMode=0;R.bSpreadingFire=false;R.Handicaps.Reset();R.bHideAndSeek=R.bWaves=R.bRuleVote=R.bRoleSwap=false;}
    return R;
}
bool UProjectCustomRoomRulesWidget::CanPickAffix(const FString& Id)const
{
    const auto* Info=ProjectAffixes::Find(Id);
    if(!Info||!Info->bPlayable||Draft.AffixIds.Contains(Id)||Draft.AffixIds.Num()>=ProjectPlaygroundRules::MaxAffixes)return false;
    int32 SameGroup=0;for(const FString& Other:Draft.AffixIds)if(const auto* O=ProjectAffixes::Find(Other);O&&O->Group==Info->Group)++SameGroup;
    return SameGroup<ProjectPlaygroundRules::MaxAffixesPerGroup;
}
void UProjectCustomRoomRulesWidget::ToggleAffix(const FString& Id)
{
    if(!CanEdit()||!Draft.bEnabled)return;
    if(Draft.AffixIds.Remove(Id)>0)return;
    if(CanPickAffix(Id))Draft.AffixIds.Add(Id);
}
FString UProjectCustomRoomRulesWidget::MixDetail()const
{
    if(const auto* Info=ProjectAffixes::Find(HoveredAffix))
        return FString::Printf(TEXT("%s · %s\n%s"),Info->Name,ProjectAffixes::GroupName(Info->Group),Info->Summary);
    return TEXT("어픽스에 마우스를 올리면 설명이 여기에 나옵니다.");
}
TSharedRef<SWidget> UProjectCustomRoomRulesWidget::RebuildWidget()
{
    auto Button=[this](const FString& Text,TFunction<void()> Click,TFunction<bool()> Enabled)
    {return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(FMargin(14,7))
        .IsEnabled_Lambda([Enabled]{return Enabled();}).OnClicked_Lambda([Click]{Click();return FReply::Handled();})[Caption(Text,17)];};

    // 기본: the custom-rules switch and the three numbers (saved presets live in 프리셋·코드).
    auto Basic=SNew(SVerticalBox);
    Basic->AddSlot().AutoHeight().Padding(0,0,0,8)
    [SNew(SCheckBox).Style(&ToggleStyle()).Padding(FMargin(8,0,0,0)).IsEnabled_Lambda([this]{return CanEdit();}).IsChecked_Lambda([this]{return Draft.bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
      .OnCheckStateChanged_Lambda([this](ECheckBoxState S){Draft.bEnabled=S==ECheckBoxState::Checked;})[Caption(TEXT("사용자 지정"))]];
    auto Number=[this,&Basic](const FString& Label,int32 FProjectCustomMatchRules::* Member,int Min,int Max,int Divisor=1)
    {
        Basic->AddSlot().AutoHeight().Padding(0,4)
        [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Caption(Label)]
          +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(180)
           [SNew(SSpinBox<int32>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).ContentPadding(FMargin(8,4)).MinValue(Min).MaxValue(Max).MinSliderValue(Min).MaxSliderValue(Max).Delta(1)
            .IsEnabled_Lambda([this]{return CanEdit();}).Value_Lambda([this,Member,Divisor]{return Draft.*Member/Divisor;})
            .OnValueChanged_Lambda([this,Member,Divisor](int32 V){Draft.*Member=V*Divisor;})]]];
    };
    Number(TEXT("데스카운트"),&FProjectCustomMatchRules::DeathCount,1,20);
    Number(TEXT("마네킹 수"),&FProjectCustomMatchRules::MannequinCount,1,10);
    Number(TEXT("제한 시간 (분)"),&FProjectCustomMatchRules::TimeLimitSeconds,0,120,60);
    // 개인 핸디캡 (12.1): pick a participant, then six numbers in a fixed 3 x 2 grid. A player whose numbers are all 0
    // has no entry. Everyone sees the result in the room summary and on the result screen.
    Basic->AddSlot().AutoHeight().Padding(0,12,0,4)[Caption(TEXT("개인 핸디캡"))];
    Basic->AddSlot().AutoHeight().Padding(0,2)
    [SAssignNew(HandicapCombo,SComboBox<TSharedPtr<FString>>).OptionsSource(&HandicapNames).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ItemStyle(&ProjectDFSDUI::RowStyle()).HasDownArrow(false)
      .IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled&&!HandicapNames.IsEmpty();})
      .OnGenerateWidget_Lambda([](TSharedPtr<FString> Name){return Caption(Name?*Name:FString(),16);})
      .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Name,ESelectInfo::Type){const int32 I=Name?HandicapNames.IndexOfByKey(Name):INDEX_NONE;HandicapUserId=HandicapUserIds.IsValidIndex(I)?HandicapUserIds[I]:FString();})
      [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),17)).ColorAndOpacity(ProjectDFSDUI::Text())
        .Text_Lambda([this]{const int32 I=HandicapUserIds.IndexOfByKey(HandicapUserId);return FText::FromString(HandicapNames.IsValidIndex(I)&&HandicapNames[I]?*HandicapNames[I]:FString(TEXT("참가자 선택")));})]
       +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0)[SNew(SRulesChevron)]]];
    {
        auto Grid=SNew(SVerticalBox);
        struct FField{const TCHAR* Label;int32 FProjectPlaygroundHandicap::* Member;int32 Max;};
        const FField Fields[]={
            {TEXT("데스카운트"),&FProjectPlaygroundHandicap::DeathCount,ProjectPlaygroundRules::HandicapDeathMax},
            {TEXT("이동 속도 %"),&FProjectPlaygroundHandicap::SpeedPct,ProjectPlaygroundRules::HandicapSpeedMax},
            {TEXT("스태미나 %"),&FProjectPlaygroundHandicap::StaminaPct,ProjectPlaygroundRules::HandicapStaminaMax},
            {TEXT("시선 각도 %"),&FProjectPlaygroundHandicap::GazePct,ProjectPlaygroundRules::HandicapGazeMax},
            {TEXT("몸체 속도 %"),&FProjectPlaygroundHandicap::BodySpeedPct,ProjectPlaygroundRules::HandicapBodySpeedMax},
            {TEXT("공격 사거리 %"),&FProjectPlaygroundHandicap::AttackRangePct,ProjectPlaygroundRules::HandicapAttackRangeMax}};
        for(int32 Row=0;Row<3;++Row)
        {
            auto Line=SNew(SHorizontalBox);
            for(int32 Col=0;Col<2;++Col)
            {
                const FField& F=Fields[Row*2+Col];const FString Label=F.Label;const auto Member=F.Member;const int32 Max=F.Max;
                Line->AddSlot().FillWidth(1).VAlign(VAlign_Center).Padding(Col==0?FMargin(0,0,6,0):FMargin(10,0,6,0))[Caption(Label,16)];
                Line->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(84)
                 [SNew(SSpinBox<int32>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ContentPadding(FMargin(6,3)).MinValue(-Max).MaxValue(Max).MinSliderValue(-Max).MaxSliderValue(Max).Delta(1)
                   .IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled&&!HandicapUserId.IsEmpty();})
                   .Value_Lambda([this,Member]{const auto* H=Draft.Handicaps.FindByPredicate([this](const auto& E){return E.UserId==HandicapUserId;});return H?(*H).*Member:0;})
                   .OnValueChanged_Lambda([this,Member,Max](int32 V){SetHandicapValue(Member,FMath::Clamp(V,-Max,Max));})]];
            }
            Grid->AddSlot().AutoHeight().Padding(0,3)[Line];
        }
        Basic->AddSlot().AutoHeight()[Grid];
    }
    // 프리셋·코드: recommended presets (five columns, one fixed summary line), my presets (with board 게시/내리기), share codes.
    auto Codes=SNew(SVerticalBox);
    Codes->AddSlot().AutoHeight().Padding(0,0,0,4)[Caption(TEXT("추천 프리셋"))];
    {
        const TArray<ProjectPlaygroundPresets::FPreset> Recommended=ProjectPlaygroundPresets::Available();
        constexpr int32 Columns=5;
        for(int32 I=0;I<Recommended.Num();I+=Columns)
        {
            auto Row=SNew(SHorizontalBox);
            for(int32 J=I;J<FMath::Min(I+Columns,Recommended.Num());++J)
            {
                const ProjectPlaygroundPresets::FPreset Preset=Recommended[J];
                Row->AddSlot().FillWidth(1).Padding(J==I?FMargin(0,0,3,0):FMargin(3,0,J==I+Columns-1?0:3,0))
                [SNew(SBox).HeightOverride(32)[SNew(SButton).ButtonStyle(&TightButtonStyle()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(FMargin(0))
                  .IsEnabled_Lambda([this]{return CanEdit();})
                  .OnHovered_Lambda([this,Preset]{HoveredPreset=Preset.Name+TEXT(" · ")+Preset.Summary;Status=HoveredPreset;}) // Shown in the fixed status line.
                  .OnClicked_Lambda([this,Preset]{{const auto Keep=Draft.Handicaps;Draft=Preset.Rules;Draft.Handicaps=Keep;}HoveredPreset=Preset.Name+TEXT(" · ")+Preset.Summary;
                      Status=FString::Printf(TEXT("'%s'을(를) 불러왔습니다. 적용을 눌러야 방에 반영됩니다."),*Preset.Name);return FReply::Handled();})
                  [Caption(Preset.Name,14)]]];
            }
            for(int32 Empty=FMath::Min(I+Columns,Recommended.Num())-I;Empty<Columns;++Empty)Row->AddSlot().FillWidth(1).Padding(3,0)[SNew(SBox)]; // Short last row keeps the same button width.
            Codes->AddSlot().AutoHeight().Padding(0,2)[Row];
        }
    }
    Codes->AddSlot().AutoHeight().Padding(0,8,0,4)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Caption(TEXT("내 프리셋"))]
     +SHorizontalBox::Slot().AutoWidth()
     [SNew(SBox).HeightOverride(28).WidthOverride(180)[SNew(SButton).ButtonStyle(&TightButtonStyle()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(FMargin(0))
       .IsEnabled_Lambda([this]{return !Request;}).OnClicked_Lambda([this]{bManageOpen=true;ManagePage=0;return FReply::Handled();})
       [Caption(TEXT("모아 보기 · 여러 개 관리"),14)]]]];
    // The saved-preset list and the name box share one row.
    Codes->AddSlot().AutoHeight().Padding(0,2)
    [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
    [SAssignNew(PresetCombo,SComboBox<TSharedPtr<FString>>).OptionsSource(&PresetNames).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ItemStyle(&ProjectDFSDUI::RowStyle()).HasDownArrow(false)
      .IsEnabled_Lambda([this]{return !Request;}).OnGenerateWidget_Lambda([](TSharedPtr<FString> Name){return Caption(Name?*Name:FString(),16);})
      .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Name,ESelectInfo::Type){SelectedName=Name?*Name:FString();if(NameInput)NameInput->SetText(FText::FromString(SelectedName));})
      [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).Text_Lambda([this]
        {
            if(SelectedName.IsEmpty())return FText::FromString(TEXT("프리셋 선택"));
            const FProjectCustomMatchRules* R=Presets.Find(SelectedName);const FMyPost* P=R?FindMyPost(*R):nullptr;
            return FText::FromString(P&&P->bPublished&&!P->bHidden?SelectedName+TEXT(" · 게시 중"):SelectedName);
        }).ColorAndOpacity(ProjectDFSDUI::Text())]
       +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0)[SNew(SRulesChevron)]]]
     +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)
    [SAssignNew(NameInput,SEditableTextBox).Style(&ProjectDFSDUI::InputStyle()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).HintText(FText::FromString(TEXT("프리셋 이름"))).IsEnabled_Lambda([this]{return !Request;})]];
    auto Actions=SNew(SHorizontalBox);
    Actions->AddSlot().FillWidth(1).Padding(0,0,4,0)[Button(TEXT("불러오기"),[this]{if(auto* R=Presets.Find(SelectedName)){{const auto Keep=Draft.Handicaps;Draft=*R;Draft.Handicaps=Keep;}Status.Reset();}},[this]{return CanEdit()&&Presets.Contains(SelectedName);})];
    Actions->AddSlot().FillWidth(1).Padding(4,0)[Button(TEXT("저장"),[this]{SavePresetAs(NameInput->GetText().ToString().TrimStartAndEnd(),Outgoing());},[this]{return CanEdit()&&NameInput&&!NameInput->GetText().IsEmpty()&&Outgoing().IsValid();})];
    Actions->AddSlot().FillWidth(1).Padding(4,0)[Button(TEXT("삭제"),[this]{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("action"),TEXT("Delete"));J->SetStringField(TEXT("name"),SelectedName);Send(TEXT("POST"),TEXT("/api/room-rule-presets"),J);},[this]{return CanEdit()&&Presets.Contains(SelectedName);})];
    // Any saved preset can go on the shared board or come off it at any time (a cleared one: Docs/CustomGamePlayground.md 13).
    Actions->AddSlot().FillWidth(1.4f).Padding(4,0,0,0)
    [SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(FMargin(6,7))
      .IsEnabled_Lambda([this]{const FProjectCustomMatchRules* R=Presets.Find(SelectedName);const FMyPost* P=R?FindMyPost(*R):nullptr;return CanEdit()&&R&&R->bEnabled&&!(P&&P->bHidden);})
      .OnClicked_Lambda([this]{if(const FProjectCustomMatchRules* R=Presets.Find(SelectedName))TogglePosted(SelectedName,*R);return FReply::Handled();})
      [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),17)).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([this]
       {const FProjectCustomMatchRules* R=Presets.Find(SelectedName);const FMyPost* P=R?FindMyPost(*R):nullptr;
        return FText::FromString(P&&P->bHidden?TEXT("운영자가 숨김"):P&&P->bPublished?TEXT("게시판에서 내리기"):TEXT("게시판에 게시"));})]];
    Codes->AddSlot().AutoHeight().Padding(0,4)[Actions];
    // Share codes: receive one (look first, then load or save), make one from the current rules, keep or revoke mine.
    Codes->AddSlot().AutoHeight().Padding(0,4,0,4)[Caption(TEXT("공유 코드"))];
    {
        auto Row=SNew(SHorizontalBox);
        Row->AddSlot().FillWidth(1).Padding(0,0,4,0)
        [SAssignNew(CodeInput,SEditableTextBox).Style(&ProjectDFSDUI::InputStyle()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).HintText(FText::FromString(TEXT("받은 공유 코드 (예: AB3CD-EF7GH)"))).IsEnabled_Lambda([this]{return !Request;})];
        Row->AddSlot().AutoWidth().Padding(4,0,0,0)[Button(TEXT("내용 보기"),[this]
        {
            FString Code=CodeInput?CodeInput->GetText().ToString().TrimStartAndEnd():FString();
            const FString Plain=Code.Replace(TEXT("-"),TEXT("")).Replace(TEXT(" "),TEXT(""));
            if(Plain.Len()==10)Send(TEXT("GET"),TEXT("/api/share-codes/")+FGenericPlatformHttp::UrlEncode(Plain),nullptr,[this](const TSharedPtr<FJsonObject>& J){ReadShareAnswer(J);});
            else Send(TEXT("GET"),TEXT("/api/rule-codes/")+FGenericPlatformHttp::UrlEncode(Plain),nullptr,[this](const TSharedPtr<FJsonObject>& J)
                {ReadCodeAnswer(J,false);Status=TEXT("규칙 코드는 기록만 볼 수 있습니다. 규칙을 받으려면 공유 코드가 필요합니다.");});
        },[this]{return !Request&&CodeInput&&CodeInput->GetText().ToString().TrimStartAndEnd().Len()>=8;})];
        Codes->AddSlot().AutoHeight().Padding(0,2)[Row];
    }
    {
        auto Row=SNew(SHorizontalBox);
        Row->AddSlot().AutoWidth().Padding(0,0,4,0)
        [SNew(SBox).WidthOverride(150)[SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(FMargin(6,7)).IsEnabled_Lambda([this]{return !Request;})
          .OnClicked_Lambda([this]{ShareHours=ShareHours==24?168:ShareHours==168?0:24;return FReply::Handled();})
          [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ColorAndOpacity(ProjectDFSDUI::Text())
            .Text_Lambda([this]{return FText::FromString(ShareHours==24?TEXT("기한: 24시간"):ShareHours==168?TEXT("기한: 7일"):TEXT("기한: 무기한"));})]]];
        Row->AddSlot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("지금 규칙으로 공유 코드 만들기"),[this]
        {
            FString Name=NameInput?NameInput->GetText().ToString().TrimStartAndEnd():FString();if(Name.IsEmpty())Name=SelectedName.IsEmpty()?FString(TEXT("공유 규칙")):SelectedName;
            auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),Name.Left(32));J->SetObjectField(TEXT("rules"),Outgoing().ToJson());J->SetNumberField(TEXT("hours"),ShareHours);
            Send(TEXT("POST"),TEXT("/api/share-codes"),J,[this](const TSharedPtr<FJsonObject>& A)
            {
                FString Code;A->TryGetStringField(TEXT("code"),Code);A->TryGetStringField(TEXT("message"),Status);
                if(!Code.IsEmpty()){FPlatformApplicationMisc::ClipboardCopy(*Code);CodeLine=FString::Printf(TEXT("새 공유 코드 %s · 클립보드에 복사했습니다."),*Code);}
                LoadShares();
            });
        },[this]{return CanEdit()&&Draft.bEnabled&&Outgoing().IsValid();})];
        Codes->AddSlot().AutoHeight().Padding(0,2)[Row];
    }
    {
        // My share codes: copy or revoke (a board post's code is fixed).
        auto Row=SNew(SHorizontalBox);
        Row->AddSlot().FillWidth(1).Padding(0,0,4,0)
        [SAssignNew(ShareCombo,SComboBox<TSharedPtr<FString>>).OptionsSource(&ShareNames).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ItemStyle(&ProjectDFSDUI::RowStyle()).HasDownArrow(false)
          .IsEnabled_Lambda([this]{return !Request&&!ShareNames.IsEmpty();}).OnGenerateWidget_Lambda([](TSharedPtr<FString> Name){return Caption(Name?*Name:FString(),15);})
          .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Name,ESelectInfo::Type){const int32 I=Name?ShareNames.IndexOfByKey(Name):INDEX_NONE;SelectedShare=ShareCodesList.IsValidIndex(I)?ShareCodesList[I]:FString();})
          [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).OverflowPolicy(ETextOverflowPolicy::Ellipsis).ColorAndOpacity(ProjectDFSDUI::Text())
             .Text_Lambda([this]{const int32 I=ShareCodesList.IndexOfByKey(SelectedShare);return FText::FromString(ShareNames.IsValidIndex(I)?*ShareNames[I]:ShareNames.IsEmpty()?FString(TEXT("내 공유 코드 없음")):FString(TEXT("내 공유 코드 선택")));})]
           +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0)[SNew(SRulesChevron)]]];
        Row->AddSlot().AutoWidth().Padding(4,0)[Button(TEXT("복사"),[this]{FPlatformApplicationMisc::ClipboardCopy(*SelectedShare);Status=FString::Printf(TEXT("%s을(를) 복사했습니다."),*SelectedShare);},[this]{return !SelectedShare.IsEmpty();})];
        Row->AddSlot().AutoWidth().Padding(4,0,0,0)[Button(TEXT("취소"),[this]
        {
            const FString Code=SelectedShare;
            AskThen(FString::Printf(TEXT("공유 코드 %s을(를) 취소하시겠습니까? 이 코드로는 더 이상 불러올 수 없습니다."),*Code),TEXT("ShareCodeRevoke"),[this,Code]
            {Send(TEXT("DELETE"),TEXT("/api/share-codes/")+FGenericPlatformHttp::UrlEncode(Code),nullptr,[this](const TSharedPtr<FJsonObject>& J){J->TryGetStringField(TEXT("message"),Status);SelectedShare.Reset();LoadShares();});});
        },[this]{return CanEdit()&&!SelectedShare.IsEmpty()&&!SelectedShareIsBoard();})];
        Codes->AddSlot().AutoHeight().Padding(0,2)[Row];
    }
    Codes->AddSlot().AutoHeight().Padding(0,4)
    [SNew(SBox).HeightOverride(44)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Text())
      .Text_Lambda([this]{return FText::FromString(CodeLine.IsEmpty()?TEXT("공유 코드는 보낼 때마다 새로 만들고 언제든 취소할 수 있습니다. 게시판 글의 코드는 고정입니다."):CodeLine);})]];

    // 섞기: affix picks (one row per group, fixed-size badges) and the item-card switch.
    auto Mix=SNew(SVerticalBox);
    Mix->AddSlot().AutoHeight().Padding(0,0,0,8)
    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]
     {return FText::FromString(FString::Printf(TEXT("어픽스 %d/%d · 그룹별 %d개까지"),Draft.AffixIds.Num(),ProjectPlaygroundRules::MaxAffixes,ProjectPlaygroundRules::MaxAffixesPerGroup));})];
    for(const ProjectAffixes::EGroup Group:{ProjectAffixes::EGroup::Survivor,ProjectAffixes::EGroup::Mannequin,ProjectAffixes::EGroup::Environment})
    {
        auto Row=SNew(SHorizontalBox);
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(96)[Caption(ProjectAffixes::GroupName(Group),15)]];
        for(const auto& Info:ProjectAffixes::All())
        {
            if(Info.Group!=Group||!Info.bPlayable)continue;
            const FString Id=Info.Id;const FLinearColor Color=ProjectAffixes::GroupColor(Group);
            Row->AddSlot().AutoWidth().Padding(2,0)
            [SNew(SBox).WidthOverride(42).HeightOverride(42)
             [SNew(SButton).ButtonStyle(&TightButtonStyle()).HAlign(HAlign_Fill).VAlign(VAlign_Fill).ContentPadding(FMargin(1))
              .IsEnabled_Lambda([this,Id]{return CanEdit()&&Draft.bEnabled&&(Draft.AffixIds.Contains(Id)||CanPickAffix(Id));})
              .OnHovered_Lambda([this,Id]{HoveredAffix=Id;})
              .OnClicked_Lambda([this,Id]{ToggleAffix(Id);HoveredAffix=Id;return FReply::Handled();})
              // A picked affix is filled with its group colour; the fill is a steady state, never a blink.
              [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).Padding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)
               .BorderBackgroundColor_Lambda([this,Id,Color]{return Draft.AffixIds.Contains(Id)?FSlateColor(Color.CopyWithNewOpacity(.85f)):FSlateColor(FLinearColor::Transparent);})
               [Caption(Info.Glyph,18)]]]];
        }
        Mix->AddSlot().AutoHeight().Padding(0,3)[Row];
    }
    // Fixed two-line detail: the hovered affix, so nothing pops over the panel.
    Mix->AddSlot().AutoHeight().Padding(0,8)
    [SNew(SBox).HeightOverride(48)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([this]{return FText::FromString(MixDetail());})]];
    Mix->AddSlot().AutoHeight().Padding(0,6)
    [SNew(SCheckBox).Style(&ToggleStyle()).Padding(FMargin(8,0,0,0)).IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled;})
      .IsChecked_Lambda([this]{return Draft.bItemCards?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
      .OnCheckStateChanged_Lambda([this](ECheckBoxState S){Draft.bItemCards=S==ECheckBoxState::Checked;})[Caption(TEXT("아이템 카드 (층마다 3장 중 1장)"))]];
    Mix->AddSlot().AutoHeight().Padding(0,4)
    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text_Lambda([this]{return FText::FromString(Draft.bEnabled?TEXT("고른 어픽스는 카드로 나오지 않습니다."):TEXT("기본 탭에서 사용자 지정을 켜면 고를 수 있습니다."));})];

    // 자유 규칙: rules that change who stays in the match. Each switch has one fixed explanation line below it.
    auto Free=SNew(SVerticalBox);
    auto FreeSwitch=[this,&Free](const FString& Label,const FString& Explain,bool FProjectCustomMatchRules::* Member)
    {
        Free->AddSlot().AutoHeight().Padding(0,4,0,2)
        [SNew(SCheckBox).Style(&ToggleStyle()).Padding(FMargin(8,0,0,0)).IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled;})
          .IsChecked_Lambda([this,Member]{return Draft.*Member?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
          .OnCheckStateChanged_Lambda([this,Member](ECheckBoxState S){Draft.*Member=S==ECheckBoxState::Checked;})[Caption(Label)]];
        Free->AddSlot().AutoHeight().Padding(28,0,0,10)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted()).Text(FText::FromString(Explain))];
    };
    FreeSwitch(TEXT("무한 목숨"),TEXT("잡혀도 탈락하지 않고 그 층의 시작 지점으로 돌아갑니다. 시간이 끝날 때까지 아무도 탈출하지 못하면 마네킹 쪽이 이깁니다."),&FProjectCustomMatchRules::bInfiniteLives);
    FreeSwitch(TEXT("감염"),TEXT("탈락한 생존자가 마네킹 몸체 하나를 직접 조종하며 마네킹 편이 됩니다. 보이면 멈추고, 생존자를 잡을 수 있습니다. 무한 목숨과 함께 켜면 첫 포획에 감염됩니다."),&FProjectCustomMatchRules::bInfection);
    FreeSwitch(TEXT("유령 생존자"),TEXT("탈락한 생존자가 동료를 따라다니며 20초마다 핑을, 말하기 키로 5초 동안 속삭임을 보냅니다. 마네킹 쪽은 보지도 듣지도 못합니다."),&FProjectCustomMatchRules::bGhosts);
    // 오래 숨으면 들킴: a switch and its seconds on one row (the number stays put; it is only disabled while off).
    Free->AddSlot().AutoHeight().Padding(0,4,0,2)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
     [SNew(SCheckBox).Style(&ToggleStyle()).Padding(FMargin(8,0,0,0)).IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled;})
       .IsChecked_Lambda([this]{return Draft.HideRevealSeconds>0?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
       .OnCheckStateChanged_Lambda([this](ECheckBoxState S){Draft.HideRevealSeconds=S==ECheckBoxState::Checked?ProjectPlaygroundRules::HideRevealDefaultOn:0;})[Caption(TEXT("오래 숨으면 들킴 (초)"))]]
     +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(140)
      [SNew(SSpinBox<int32>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).ContentPadding(FMargin(8,4))
        .MinValue(ProjectPlaygroundRules::HideRevealMin).MaxValue(ProjectPlaygroundRules::HideRevealMax).MinSliderValue(ProjectPlaygroundRules::HideRevealMin).MaxSliderValue(ProjectPlaygroundRules::HideRevealMax).Delta(ProjectPlaygroundRules::HideRevealStep)
        .IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled&&Draft.HideRevealSeconds>0;})
        .Value_Lambda([this]{return Draft.HideRevealSeconds>0?Draft.HideRevealSeconds:ProjectPlaygroundRules::HideRevealDefaultOn;})
        .OnValueChanged_Lambda([this](int32 V){if(Draft.HideRevealSeconds>0)Draft.HideRevealSeconds=FMath::Clamp(V-V%ProjectPlaygroundRules::HideRevealStep,ProjectPlaygroundRules::HideRevealMin,ProjectPlaygroundRules::HideRevealMax);})]]];
    Free->AddSlot().AutoHeight().Padding(28,0,0,10)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text(FText::FromString(TEXT("4m 안에 이 시간보다 오래 머무르면 그 자리가 마네킹 쪽에 5초 동안 드러납니다. 10초 전에 알려 줍니다.")))];
    // 탈출 방식 (12.1): one choice of four; the explanation line under it follows the choice.
    EscapeModeNames={MakeShared<FString>(TEXT("기본")),MakeShared<FString>(TEXT("함께 탈출")),MakeShared<FString>(TEXT("마지막 한 명이 모두를 구한다")),MakeShared<FString>(TEXT("먼저 나간 사람 승리"))};
    Free->AddSlot().AutoHeight().Padding(0,4,0,2)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(28,0,0,0)[Caption(TEXT("탈출 방식"))]
     +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(260)
      [SAssignNew(EscapeCombo,SComboBox<TSharedPtr<FString>>).OptionsSource(&EscapeModeNames).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ItemStyle(&ProjectDFSDUI::RowStyle()).HasDownArrow(false)
        .IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled;})
        .OnGenerateWidget_Lambda([](TSharedPtr<FString> Name){return Caption(Name?*Name:FString(),16);})
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Name,ESelectInfo::Type){const int32 I=Name?EscapeModeNames.IndexOfByKey(Name):INDEX_NONE;if(I!=INDEX_NONE&&CanEdit())Draft.EscapeMode=I;})
        [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ColorAndOpacity(ProjectDFSDUI::Text())
          .Text_Lambda([this]{return FText::FromString(EscapeModeNames.IsValidIndex(Draft.EscapeMode)?*EscapeModeNames[Draft.EscapeMode]:FString());})]
         +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0)[SNew(SRulesChevron)]]]]];
    Free->AddSlot().AutoHeight().Padding(28,0,0,10)
    [SNew(SBox).HeightOverride(40)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]
     {
         switch(Draft.EscapeMode)
         {
         case 1:return FText::FromString(TEXT("살아 있는 생존자가 모두 출구에 모여야 함께 나갑니다. 마네킹은 출구 하나만 지켜도 됩니다."));
         case 2:return FText::FromString(TEXT("혼자 남은 생존자가 탈출하면 먼저 잡힌 동료도 함께 이깁니다."));
         case 3:return FText::FromString(TEXT("경쟁형: 가장 먼저 탈출한 한 명만 이기고 경기가 바로 끝납니다."));
         default:return FText::FromString(TEXT("평소 규칙대로 탈출합니다."));
         }
     })]];
    Free->AddSlot().AutoHeight().Padding(0,4)
    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]
     {
         if(!Draft.bEnabled)return FText::FromString(TEXT("기본 탭에서 사용자 지정을 켜면 고를 수 있습니다."));
         if(Draft.bInfiniteLives&&Draft.TimeLimitSeconds<ProjectPlaygroundRules::InfiniteLivesMinTimeLimit)return FText::FromString(TEXT("무한 목숨은 제한 시간이 5분 이상이어야 합니다. 기본 탭에서 바꾸세요."));
         if(Draft.bGhosts&&(Draft.bInfiniteLives||Draft.bInfection))return FText::FromString(TEXT("유령 생존자는 무한 목숨·감염과 함께 켤 수 없습니다. 탈락자가 생기지 않기 때문입니다."));
         if(Draft.bInfection&&Draft.MannequinCount>ProjectPlaygroundRules::InfectionMaxMannequins)return FText::FromString(TEXT("감염을 켜면 마네킹 수는 8 이하여야 합니다. 기본 탭에서 바꾸세요."));
         return FText::GetEmpty();
     })];

    // 시야·소리 (phase 2): rules about seeing and hearing. Each is a switch with its number on the same row; the number
    // stays in place and is only disabled while the switch is off.
    auto Vision=SNew(SVerticalBox);
    auto NumberSwitch=[this,&Vision](const FString& Label,const FString& Explain,int32 FProjectCustomMatchRules::* Member,int32 Min,int32 Max,int32 DefaultOn)
    {
        Vision->AddSlot().AutoHeight().Padding(0,4,0,2)
        [SNew(SHorizontalBox)
         +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
         [SNew(SCheckBox).Style(&ToggleStyle()).Padding(FMargin(8,0,0,0)).IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled;})
           .IsChecked_Lambda([this,Member]{return Draft.*Member>0?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
           .OnCheckStateChanged_Lambda([this,Member,DefaultOn](ECheckBoxState S){Draft.*Member=S==ECheckBoxState::Checked?DefaultOn:0;})[Caption(Label)]]
         +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(140)
          [SNew(SSpinBox<int32>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).ContentPadding(FMargin(8,4)).MinValue(Min).MaxValue(Max).MinSliderValue(Min).MaxSliderValue(Max).Delta(1)
            .IsEnabled_Lambda([this,Member]{return CanEdit()&&Draft.bEnabled&&Draft.*Member>0;})
            .Value_Lambda([this,Member,DefaultOn]{return Draft.*Member>0?Draft.*Member:DefaultOn;})
            .OnValueChanged_Lambda([this,Member,Min,Max](int32 V){if(Draft.*Member>0)Draft.*Member=FMath::Clamp(V,Min,Max);})]]];
        Vision->AddSlot().AutoHeight().Padding(28,0,0,10)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted()).Text(FText::FromString(Explain))];
    };
    NumberSwitch(TEXT("바라보기 시간 제한 (초)"),TEXT("같은 몸체를 이 시간보다 오래 계속 보면 그 몸체가 시선을 무시하고 움직입니다. 눈을 떼거나 다른 몸체를 보면 다시 셉니다."),
        &FProjectCustomMatchRules::GazeLimitSeconds,ProjectPlaygroundRules::GazeLimitMin,ProjectPlaygroundRules::GazeLimitMax,ProjectPlaygroundRules::GazeLimitDefaultOn);
    // 어둠·연기: one number, 0 = off. Both sides' screens and the survivors' freezing gaze reach shrink with it.
    Vision->AddSlot().AutoHeight().Padding(0,4,0,2)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(28,0,0,0)[Caption(TEXT("어둠·연기 (0 끄기 ~ 3 아주 어두움)"))]
     +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(140)
      [SNew(SSpinBox<int32>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).ContentPadding(FMargin(8,4)).MinValue(0).MaxValue(ProjectPlaygroundRules::DarknessMax)
        .MinSliderValue(0).MaxSliderValue(ProjectPlaygroundRules::DarknessMax).Delta(1).IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled;})
        .Value_Lambda([this]{return Draft.Darkness;}).OnValueChanged_Lambda([this](int32 V){Draft.Darkness=FMath::Clamp(V,0,ProjectPlaygroundRules::DarknessMax);})]]];
    Vision->AddSlot().AutoHeight().Padding(28,0,0,10)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text(FText::FromString(TEXT("생존자와 마네킹 플레이어 화면이 모두 어두워집니다. 생존자 시선이 몸체를 멈추게 하는 거리도 30m / 20m / 12m로 줄어듭니다.")))];
    NumberSwitch(TEXT("소리만 들리는 마네킹 (m)"),TEXT("생존자 화면에서 이 거리보다 먼 몸체는 보이지 않고 발소리만 들립니다. 보이지 않는 몸체는 시선에 멈추지도 않습니다. 마네킹 쪽은 그대로 봅니다."),
        &FProjectCustomMatchRules::SoundOnlyMeters,ProjectPlaygroundRules::SoundOnlyMin,ProjectPlaygroundRules::SoundOnlyMax,ProjectPlaygroundRules::SoundOnlyDefaultOn);
    // 번지는 불: a plain switch; the numbers (2 min, 30 s, 6 m, 24 cells, 5 s) are the decided ones (12.1).
    Vision->AddSlot().AutoHeight().Padding(0,4,0,2)
    [SNew(SCheckBox).Style(&ToggleStyle()).Padding(FMargin(8,0,0,0)).IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled;})
      .IsChecked_Lambda([this]{return Draft.bSpreadingFire?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
      .OnCheckStateChanged_Lambda([this](ECheckBoxState S){Draft.bSpreadingFire=S==ECheckBoxState::Checked;})[Caption(TEXT("번지는 불"))]];
    Vision->AddSlot().AutoHeight().Padding(28,0,0,10)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text(FText::FromString(TEXT("2분 뒤 불이 나서 30초마다 번집니다. 불 속에서는 스태미나가 줄고, 바닥나면 데스카운트를 잃습니다. 마네킹은 불 속에서 느려집니다.")))];
    Vision->AddSlot().AutoHeight().Padding(0,4)
    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text_Lambda([this]{return FText::FromString(Draft.bEnabled?TEXT(""):TEXT("기본 탭에서 사용자 지정을 켜면 고를 수 있습니다."));})];

    // 놀이 방식 (phase 3): how a match (or a run of matches) is played. Waves fix the time limit at 12 minutes (기본).
    auto Modes=SNew(SVerticalBox);
    auto ModeSwitch=[this,&Modes](const FString& Label,const FString& Explain,bool FProjectCustomMatchRules::* Member,TFunction<void()> OnOn)
    {
        Modes->AddSlot().AutoHeight().Padding(0,4,0,2)
        [SNew(SCheckBox).Style(&ToggleStyle()).Padding(FMargin(8,0,0,0)).IsEnabled_Lambda([this]{return CanEdit()&&Draft.bEnabled;})
          .IsChecked_Lambda([this,Member]{return Draft.*Member?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
          .OnCheckStateChanged_Lambda([this,Member,OnOn](ECheckBoxState S){Draft.*Member=S==ECheckBoxState::Checked;if(Draft.*Member&&OnOn)OnOn();})[Caption(Label)]];
        Modes->AddSlot().AutoHeight().Padding(28,0,0,10)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted()).Text(FText::FromString(Explain))];
    };
    ModeSwitch(TEXT("숨바꼭질 (60초)"),TEXT("처음 60초 동안 마네킹은 보지도 움직이지도 못합니다. 그동안 생존자가 숨습니다."),&FProjectCustomMatchRules::bHideAndSeek,nullptr);
    ModeSwitch(TEXT("웨이브 생존 (12분)"),TEXT("마네킹 3개로 시작해 2분마다 하나씩 늘어 8개까지 갑니다. 12분을 버티면 생존자가 이깁니다."),&FProjectCustomMatchRules::bWaves,
        [this]{Draft.TimeLimitSeconds=ProjectPlaygroundRules::WaveSurviveSeconds;});
    ModeSwitch(TEXT("규칙 카드 투표"),TEXT("경기 전 암전 화면에서 아직 꺼진 규칙 3장을 보여 주고 15초 동안 투표합니다. 마네킹 플레이어도 한 표, 동점이면 무작위입니다."),&FProjectCustomMatchRules::bRuleVote,nullptr);
    ModeSwitch(TEXT("역할 바꾸기 (3판)"),TEXT("이 방에서 3판을 이어 하며 세 사람이 한 번씩 마네킹을 맡습니다. 방 화면에 누가 가장 빨리 전원을 잡았는지 보여 줍니다."),&FProjectCustomMatchRules::bRoleSwap,nullptr);
    Modes->AddSlot().AutoHeight().Padding(0,4)
    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text_Lambda([this]
      {
          if(!Draft.bEnabled)return FText::FromString(TEXT("기본 탭에서 사용자 지정을 켜면 고를 수 있습니다."));
          if(Draft.bWaves&&Draft.TimeLimitSeconds!=ProjectPlaygroundRules::WaveSurviveSeconds)return FText::FromString(TEXT("웨이브 생존은 제한 시간이 12분이어야 합니다. 기본 탭에서 바꾸세요."));
          return FText::GetEmpty();
      })];

    // 공유 게시판 (Docs/CustomGamePlayground.md 13): four fixed rows bound to Board, sort and page buttons, and the post row.
    // Rows never rebuild; an empty row is hidden but keeps its height, so the tab never changes size.
    auto BoardTab=SNew(SVerticalBox);
    {
        auto SortButton=[this](const FString& Text,const FString& Key)
        {return SNew(SButton).ButtonStyle(&TightButtonStyle()).ContentPadding(FMargin(1)).IsEnabled_Lambda([this]{return !Request;})
            .OnClicked_Lambda([this,Key]{BoardSort=Key;BoardPage=0;LoadBoard();return FReply::Handled();})
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).Padding(FMargin(10,4))
             .BorderBackgroundColor_Lambda([this,Key]{return BoardSort==Key?FSlateColor(ProjectDFSDUI::Accent().CopyWithNewOpacity(.6f)):FSlateColor(FLinearColor::Transparent);})
             [Caption(Text,15)]];};
        BoardTab->AddSlot().AutoHeight().Padding(0,0,0,6)
        [SNew(SHorizontalBox)
         +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SortButton(TEXT("최신순"),TEXT("new"))]
         +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SortButton(TEXT("많이 플레이"),TEXT("played"))]
         +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SortButton(TEXT("추천순"),TEXT("liked"))]
         +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SortButton(TEXT("내 글"),TEXT("mine"))]
         +SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right).VAlign(VAlign_Center)
         [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]
          {
              if(!bBoardLoaded)return FText::FromString(TEXT("불러오는 중..."));
              if(Board.IsEmpty())return FText::FromString(BoardPage>0?TEXT("더 없습니다."):TEXT("아직 올라온 규칙이 없습니다."));
              return FText::FromString(FString::Printf(TEXT("%d쪽"),BoardPage+1));
          })]];
        // One small fixed-width row button; Hidden (not Collapsed) when it does not apply, so rows keep the same layout.
        auto RowButton=[this](TFunction<const FBoardItem*()> Item,float Width,TFunction<FString(const FBoardItem&)> Label,TFunction<bool(const FBoardItem&)> Visible,
            TFunction<bool(const FBoardItem&)> Enabled,TFunction<void(const FBoardItem&)> Click,TFunction<bool(const FBoardItem&)> Chosen)->TSharedRef<SWidget>
        {
            return SNew(SBox).WidthOverride(Width).HeightOverride(32).Visibility_Lambda([Item,Visible]{const FBoardItem* I=Item();return I&&Visible(*I)?EVisibility::Visible:EVisibility::Hidden;})
                [SNew(SButton).ButtonStyle(&TightButtonStyle()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(FMargin(0))
                 .IsEnabled_Lambda([this,Item,Enabled]{const FBoardItem* I=Item();return !Request&&I&&Enabled(*I);})
                 .OnClicked_Lambda([Item,Click]{if(const FBoardItem* I=Item())Click(*I);return FReply::Handled();})
                 [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15))
                   .ColorAndOpacity_Lambda([Item,Chosen]{const FBoardItem* I=Item();return I&&Chosen(*I)?FSlateColor(ProjectDFSDUI::Accent()):FSlateColor(ProjectDFSDUI::Text());})
                   .Text_Lambda([Item,Label]{const FBoardItem* I=Item();return FText::FromString(I?Label(*I):FString());})]];
        };
        for(int32 Row=0;Row<4;++Row)
        {
            auto Item=[this,Row]()->const FBoardItem*{return Board.IsValidIndex(Row)?&Board[Row]:nullptr;};
            auto Line=[Item](int32 Size,const FLinearColor& Color,TFunction<FString(const FBoardItem&)> Read)
            {return SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).ColorAndOpacity(Color).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                .Text_Lambda([Item,Read]{const FBoardItem* I=Item();return FText::FromString(I?Read(*I):FString());});};
            BoardTab->AddSlot().AutoHeight().Padding(0,2)
            [SNew(SBox).HeightOverride(74).Visibility_Lambda([Item]{return Item()?EVisibility::Visible:EVisibility::Hidden;})
             [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(1,1,1,.04f)).Padding(FMargin(8,1))
              [SNew(SHorizontalBox)
               +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
               [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[Line(15,ProjectDFSDUI::Text(),[](const FBoardItem& I){return FString::Printf(TEXT("%s%s · %s · %s"),I.bHidden?TEXT("[운영자가 숨김] "):!I.bPublished?TEXT("[내림] "):TEXT(""),*I.Name,*I.Author,I.ShareCode.IsEmpty()?*I.Code:*I.ShareCode);})]
                +SVerticalBox::Slot().AutoHeight()[Line(13,ProjectDFSDUI::Muted(),[](const FBoardItem& I){return I.Summary;})]
                +SVerticalBox::Slot().AutoHeight()[Line(13,ProjectDFSDUI::Muted(),[](const FBoardItem& I){return I.Stats;})]]
               +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6,0,0,0)
               [SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ContentPadding(FMargin(8,4)).IsEnabled_Lambda([this]{return !Request;})
                 .OnClicked_Lambda([this,Item]{if(const FBoardItem* I=Item())
                     OpenPreview(I->Name,FString::Printf(TEXT("올린 사람 %s · 공유 코드 %s (고정)\n%s"),*I->Author,*I->ShareCode,*I->Stats),I->Name,I->Rules);return FReply::Handled();})[Caption(TEXT("보기"),15)]]
               +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0,0,0)
               [RowButton(Item,62,[](const FBoardItem& I){return I.bMine?FString(I.bPublished?TEXT("내리기"):TEXT("게시")):FString::Printf(TEXT("추천 %d"),I.Likes);},
                   [](const FBoardItem& I){return true;},[](const FBoardItem& I){return I.bMine?!I.bHidden:(I.bCleared||I.MyVote!=0);},
                   [this](const FBoardItem& I)
                   {
                       if(I.bMine){SetPublished(I.Id,!I.bPublished);return;} // 게시/내리기: the post, its votes and its fixed code stay.
                       Vote(I.Id,I.MyVote==1?0:1);
                   },[](const FBoardItem& I){return I.MyVote==1;})]
               +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0,0,0)
               [RowButton(Item,70,[](const FBoardItem& I){return I.bMine?FString(TEXT("삭제")):FString::Printf(TEXT("비추천 %d"),I.Dislikes);},
                   [](const FBoardItem& I){return true;},[](const FBoardItem& I){return I.bMine||I.bCleared||I.MyVote!=0;},
                   [this](const FBoardItem& I)
                   {
                       const int64 Id=I.Id;
                       if(I.bMine){AskThen(TEXT("글을 완전히 지우시겠습니까? 추천 기록과 고정 공유 코드도 함께 사라집니다."),TEXT("SharedPresetDelete"),[this,Id]
                           {Send(TEXT("DELETE"),FString::Printf(TEXT("/api/shared-presets/%lld"),Id),nullptr,[this](const TSharedPtr<FJsonObject>& J){J->TryGetStringField(TEXT("message"),Status);LoadBoard();LoadMine();});});return;}
                       Vote(Id,I.MyVote==-1?0:-1);
                   },[](const FBoardItem& I){return I.MyVote==-1;})]
               +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0,0,0)
               [RowButton(Item,60,[](const FBoardItem& I){return I.bReported?FString(TEXT("신고함")):FString(TEXT("신고"));},
                   [](const FBoardItem& I){return !I.bMine;},[](const FBoardItem& I){return !I.bReported;},
                   [this](const FBoardItem& I)
                   {
                       const int64 Id=I.Id;
                       AskThen(FString::Printf(TEXT("'%s'의 이름·설명을 신고하시겠습니까? 운영자가 확인합니다."),*I.Name),TEXT("SharedPresetReport"),[this,Id]
                       {
                           auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("reason"),TEXT("Name"));
                           Send(TEXT("POST"),FString::Printf(TEXT("/api/shared-presets/%lld/report"),Id),Body,[this](const TSharedPtr<FJsonObject>& J){J->TryGetStringField(TEXT("message"),Status);LoadBoard();});
                       });
                   },[](const FBoardItem&){return false;})]]]];
        }
        BoardTab->AddSlot().AutoHeight().Padding(0,2,0,0)
        [SNew(SHorizontalBox)
         +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpacer)]
         +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[Button(TEXT("이전 쪽"),[this]{BoardPage=FMath::Max(0,BoardPage-1);LoadBoard();},[this]{return !Request&&BoardPage>0;})]
         +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("다음 쪽"),[this]{++BoardPage;LoadBoard();},[this]{return !Request&&bBoardMore;})]];
        BoardTab->AddSlot().AutoHeight().Padding(0,8,0,4)[Caption(TEXT("지금 규칙 올리기"))];
        BoardTab->AddSlot().AutoHeight()
        [SNew(SHorizontalBox)
         +SHorizontalBox::Slot().FillWidth(2).Padding(0,0,4,0)
         [SAssignNew(BoardName,SEditableTextBox).Style(&ProjectDFSDUI::InputStyle()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).HintText(FText::FromString(TEXT("이름 (32자)")))]
         +SHorizontalBox::Slot().FillWidth(3).Padding(4,0)
         [SAssignNew(BoardSummary,SEditableTextBox).Style(&ProjectDFSDUI::InputStyle()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).HintText(FText::FromString(TEXT("한 줄 설명 (60자)")))]
         +SHorizontalBox::Slot().AutoWidth().Padding(4,0,0,0)[Button(TEXT("올리기"),[this]
         {
             auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),BoardName->GetText().ToString().TrimStartAndEnd());
             J->SetStringField(TEXT("summary"),BoardSummary->GetText().ToString().TrimStartAndEnd());J->SetObjectField(TEXT("rules"),Outgoing().ToJson());
             AskThen(TEXT("지금 규칙을 공유 게시판에 올리시겠습니까?"),TEXT("SharedPresetPost"),[this,J]{Send(TEXT("POST"),TEXT("/api/shared-presets"),J,[this](const TSharedPtr<FJsonObject>& A){A->TryGetStringField(TEXT("message"),Status);BoardSort=TEXT("new");BoardPage=0;LoadBoard();});});
         },[this]{return CanEdit()&&Draft.bEnabled&&Outgoing().IsValid()&&BoardName&&!BoardName->GetText().IsEmpty();})]];
        BoardTab->AddSlot().AutoHeight().Padding(0,4,0,0)
        [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
          .Text(FText::FromString(TEXT("이 규칙으로 한 판을 끝까지 마쳐야 올릴 수 있습니다(역할 무관, 기권 제외).")))];
    }

    auto TabButton=[this](const FString& Text,int32 Index)
    {return SNew(SButton).ButtonStyle(&TightButtonStyle()).HAlign(HAlign_Fill).VAlign(VAlign_Fill).ContentPadding(FMargin(1))
        .OnClicked_Lambda([this,Index]{Tab=Index;if(Index==6&&!bBoardLoaded)LoadBoard();return FReply::Handled();})
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).Padding(FMargin(6,5)).HAlign(HAlign_Center)
         .BorderBackgroundColor_Lambda([this,Index]{return Tab==Index?FSlateColor(ProjectDFSDUI::Accent().CopyWithNewOpacity(.6f)):FSlateColor(FLinearColor::Transparent);})
         [Caption(Text,16)]];};
    auto Tabs=SNew(SHorizontalBox);
    Tabs->AddSlot().AutoWidth().Padding(0,0,4,0)[TabButton(TEXT("기본"),0)];
    Tabs->AddSlot().AutoWidth().Padding(0,0,4,0)[TabButton(TEXT("섞기"),1)];
    Tabs->AddSlot().AutoWidth().Padding(0,0,4,0)[TabButton(TEXT("자유 규칙"),2)];
    Tabs->AddSlot().AutoWidth().Padding(0,0,4,0)[TabButton(TEXT("환경"),3)];
    Tabs->AddSlot().AutoWidth().Padding(0,0,4,0)[TabButton(TEXT("놀이 방식"),4)];
    Tabs->AddSlot().AutoWidth().Padding(0,0,4,0)[TabButton(TEXT("프리셋·코드"),5)];
    Tabs->AddSlot().AutoWidth()[TabButton(TEXT("공유 게시판"),6)];

    auto Body=SNew(SVerticalBox);
    Body->AddSlot().AutoHeight().Padding(0,0,0,12)[Caption(TEXT("커스텀 규칙"),25)];
    Body->AddSlot().AutoHeight().Padding(0,0,0,10)[Tabs];
    // Fixed height so switching tabs never resizes the panel.
    Body->AddSlot().AutoHeight()
    [SNew(SBox).HeightOverride(540)[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]{return Tab;})+SWidgetSwitcher::Slot()[Basic]+SWidgetSwitcher::Slot()[Mix]+SWidgetSwitcher::Slot()[Free]+SWidgetSwitcher::Slot()[Vision]+SWidgetSwitcher::Slot()[Modes]+SWidgetSwitcher::Slot()[Codes]+SWidgetSwitcher::Slot()[BoardTab]]];
    Body->AddSlot().AutoHeight().Padding(0,8)
    [SNew(SBox).HeightOverride(44)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text_Lambda([this]{return FText::FromString(Request?TEXT("불러오는 중…"):!Outgoing().IsValid()?TEXT("규칙 조합을 확인하세요."):Status);})]];
    auto Footer=SNew(SHorizontalBox);
    Footer->AddSlot().FillWidth(1).Padding(0,0,4,0)[Button(TEXT("적용"),[this]{AskThen(TEXT("적용하시겠습니까?"),TEXT("RoomRulesApply"),[this]{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("roomId"),RoomId);J->SetObjectField(TEXT("rules"),Outgoing().ToJson());Send(TEXT("PUT"),TEXT("/api/rooms/current/custom-rules"),J);});},[this]{return CanEdit()&&Outgoing().IsValid();})];
    Footer->AddSlot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("돌아가기"),[this]{Close();},[]{return true;})];
    Body->AddSlot().AutoHeight()[Footer];
    auto PreviewButton=[](const FString& Text,TFunction<void()> Click,TFunction<bool()> Enabled)
    {return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(FMargin(2,7))
        .IsEnabled_Lambda([Enabled]{return Enabled();}).OnClicked_Lambda([Click]{Click();return FReply::Handled();})[Caption(Text,16)];};
    // Preview of received rules (share code or board post): covers the whole panel until loaded, saved or closed.
    auto Preview=SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(0)
        .Visibility_Lambda([this]{return bPreviewOpen?EVisibility::Visible:EVisibility::Collapsed;})
        [SNew(SVerticalBox)
         +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),24)).ColorAndOpacity(ProjectDFSDUI::Text()).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text_Lambda([this]{return FText::FromString(PreviewTitle);})]
         +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]{return FText::FromString(PreviewInfo);})]
         +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[Caption(TEXT("규칙 내용"))]
         +SVerticalBox::Slot().FillHeight(1)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),17)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([this]{return FText::FromString(DescribeRules(PreviewRules));})]
         +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted()).Text_Lambda([this]{return FText::FromString(Request?TEXT("처리 중…"):Status);})]
         +SVerticalBox::Slot().AutoHeight()
         [SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[PreviewButton(TEXT("이 방에 불러오기"),[this]
           {{const auto Keep=Draft.Handicaps;Draft=PreviewRules;Draft.Handicaps=Keep;}bPreviewOpen=false;Status=FString::Printf(TEXT("'%s'을(를) 불러왔습니다. 적용을 눌러야 방에 반영됩니다."),*PreviewName);},[this]{return CanEdit();})]
          +SHorizontalBox::Slot().FillWidth(1).Padding(4,0)[PreviewButton(TEXT("내 프리셋에 저장"),[this]{SavePresetAs(PreviewName,PreviewRules);},[this]{return !Request&&!PreviewName.IsEmpty();})]
          +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[PreviewButton(TEXT("닫기"),[this]{bPreviewOpen=false;},[]{return true;})]]];
    // 내 프리셋 모아 보기: every saved preset at a glance (rules in one line, board state), ticked ones posted, taken down or
    // deleted together. Eight fixed rows per page, no scrolling; rows never rebuild.
    auto Manage=SNew(SVerticalBox);
    Manage->AddSlot().AutoHeight().Padding(0,0,0,8)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1)[Caption(TEXT("내 프리셋 모아 보기"),24)]
     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).ColorAndOpacity(ProjectDFSDUI::Muted())
       .Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("%d/20개 · 선택 %d개"),PresetNames.Num(),ManageSelected.Num()));})]];
    Manage->AddSlot().AutoHeight().Padding(0,0,0,6)
    [SNew(SCheckBox).Style(&ToggleStyle()).Padding(FMargin(8,0,0,0)).IsEnabled_Lambda([this]{return !Request&&!PresetNames.IsEmpty();})
      .IsChecked_Lambda([this]{const TArray<FString> Page=ManagePageNames();if(Page.IsEmpty())return ECheckBoxState::Unchecked;
          for(const FString& N:Page)if(!ManageSelected.Contains(N))return ECheckBoxState::Unchecked;return ECheckBoxState::Checked;})
      .OnCheckStateChanged_Lambda([this](ECheckBoxState S){for(const FString& N:ManagePageNames()){if(S==ECheckBoxState::Checked)ManageSelected.Add(N);else ManageSelected.Remove(N);}})
      [Caption(TEXT("이 쪽 모두 선택"),16)]];
    for(int32 Row=0;Row<ManageRows;++Row)
    {
        auto NameAt=[this,Row]()->FString{const int32 I=ManagePage*ManageRows+Row;return PresetNames.IsValidIndex(I)&&PresetNames[I]?*PresetNames[I]:FString();};
        Manage->AddSlot().AutoHeight().Padding(0,1)
        [SNew(SBox).HeightOverride(52).Visibility_Lambda([NameAt]{return NameAt().IsEmpty()?EVisibility::Hidden:EVisibility::Visible;})
         [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(1,1,1,.04f)).Padding(FMargin(8,2))
          [SNew(SHorizontalBox)
           +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)
           [SNew(SCheckBox).Style(&ToggleStyle()).IsEnabled_Lambda([this]{return !Request;})
             .IsChecked_Lambda([this,NameAt]{return ManageSelected.Contains(NameAt())?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
             .OnCheckStateChanged_Lambda([this,NameAt](ECheckBoxState S){const FString N=NameAt();if(N.IsEmpty())return;if(S==ECheckBoxState::Checked)ManageSelected.Add(N);else ManageSelected.Remove(N);})]
           +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
           [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()
            [SNew(SHorizontalBox)
             +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ColorAndOpacity(ProjectDFSDUI::Text()).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
               .Text_Lambda([NameAt]{return FText::FromString(NameAt());})]
             +SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14))
               .ColorAndOpacity_Lambda([this,NameAt]{return PresetBoardState(NameAt())==TEXT("게시 중")?FSlateColor(ProjectDFSDUI::Accent()):FSlateColor(ProjectDFSDUI::Muted());})
               .Text_Lambda([this,NameAt]{return FText::FromString(PresetBoardState(NameAt()));})]]
            +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),13)).ColorAndOpacity(ProjectDFSDUI::Muted()).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
              .Text_Lambda([this,NameAt]{const FProjectCustomMatchRules* R=Presets.Find(NameAt());return FText::FromString(R?DescribeRules(*R).Replace(TEXT("\n"),TEXT(" · ")):FString());})]]]]];
    }
    Manage->AddSlot().AutoHeight().Padding(0,6,0,0)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).ColorAndOpacity(ProjectDFSDUI::Muted())
       .Text_Lambda([this]{const int32 Pages=FMath::Max(1,FMath::DivideAndRoundUp(PresetNames.Num(),ManageRows));
           return FText::FromString(PresetNames.IsEmpty()?FString(TEXT("저장한 프리셋이 없습니다.")):FString::Printf(TEXT("%d / %d쪽"),FMath::Min(ManagePage+1,Pages),Pages));})]
     +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[PreviewButton(TEXT("이전 쪽"),[this]{ManagePage=FMath::Max(0,ManagePage-1);},[this]{return ManagePage>0;})]
     +SHorizontalBox::Slot().AutoWidth()[PreviewButton(TEXT("다음 쪽"),[this]{++ManagePage;},[this]{return (ManagePage+1)*ManageRows<PresetNames.Num();})]];
    Manage->AddSlot().FillHeight(1).Padding(0,6)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text_Lambda([this]{return FText::FromString(Request?TEXT("처리 중…"):Status.IsEmpty()?TEXT("게시는 그 규칙으로 한 판을 끝까지 마친 프리셋만 됩니다. 삭제는 내 프리셋만 지우고 게시판 글은 남깁니다."):Status);})];
    Manage->AddSlot().AutoHeight()
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[PreviewButton(TEXT("선택 게시"),[this]{BulkBoard(true);},[this]{return CanEdit()&&!ManageSelected.IsEmpty();})]
     +SHorizontalBox::Slot().FillWidth(1).Padding(4,0)[PreviewButton(TEXT("선택 내리기"),[this]{BulkBoard(false);},[this]{return CanEdit()&&!ManageSelected.IsEmpty();})]
     +SHorizontalBox::Slot().FillWidth(1).Padding(4,0)[PreviewButton(TEXT("선택 삭제"),[this]{BulkDelete();},[this]{return CanEdit()&&!ManageSelected.IsEmpty();})]
     +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[PreviewButton(TEXT("닫기"),[this]{bManageOpen=false;},[]{return true;})]];
    auto ManagePanel=SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(0)
        .Visibility_Lambda([this]{return bManageOpen?EVisibility::Visible:EVisibility::Collapsed;})[Manage];
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(0,0,0,.65f)).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(18)
      [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
       [SNew(SBox).WidthOverride(700)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(24)
        [SNew(SOverlay)+SOverlay::Slot()[Body]+SOverlay::Slot()[ManagePanel]+SOverlay::Slot()[Preview]]]]];
}
void UProjectCustomRoomRulesWidget::NativeConstruct()
{
    Super::NativeConstruct();RefreshHandicapMembers();
    Send(TEXT("GET"),TEXT("/api/rooms/current/custom-rules"));LoadMine();LoadShares(); // One at a time (queued).
}
TArray<FString> UProjectCustomRoomRulesWidget::ManagePageNames()const
{
    TArray<FString> Out;
    for(int32 I=ManagePage*ManageRows;I<FMath::Min((ManagePage+1)*ManageRows,PresetNames.Num());++I)if(PresetNames[I])Out.Add(*PresetNames[I]);
    return Out;
}
FString UProjectCustomRoomRulesWidget::PresetBoardState(const FString& Name)const
{
    const FProjectCustomMatchRules* R=Presets.Find(Name);if(!R)return FString();
    const FMyPost* P=FindMyPost(*R);
    return !P?FString(TEXT("안 올림")):P->bHidden?FString(TEXT("운영자가 숨김")):P->bPublished?FString(TEXT("게시 중")):FString(TEXT("내림"));
}
void UProjectCustomRoomRulesWidget::BulkBoard(bool bPublish)
{
    // One request for every ticked preset; the backend answers one line per preset.
    TArray<TSharedPtr<FJsonValue>> Items;
    for(const auto& Name:PresetNames)if(Name&&ManageSelected.Contains(*Name))if(const FProjectCustomMatchRules* R=Presets.Find(*Name))
    {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("name"),*Name);FProjectCustomMatchRules Clean=*R;Clean.Handicaps.Reset();O->SetObjectField(TEXT("rules"),Clean.ToJson());
        Items.Add(MakeShared<FJsonValueObject>(O));
    }
    if(Items.IsEmpty())return;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("action"),bPublish?TEXT("Publish"):TEXT("Unpublish"));J->SetArrayField(TEXT("presets"),Items);
    Send(TEXT("POST"),TEXT("/api/shared-presets/bulk"),J,[this](const TSharedPtr<FJsonObject>& A)
    {
        FString Message;A->TryGetStringField(TEXT("message"),Message);TArray<FString> Failed;
        const TArray<TSharedPtr<FJsonValue>>* Results=nullptr;
        if(A->TryGetArrayField(TEXT("results"),Results))for(const auto& V:*Results)
        {const TSharedPtr<FJsonObject>* O=nullptr;bool bOk=true;FString N,Why;if(V->TryGetObject(O)&&(*O)->TryGetBoolField(TEXT("ok"),bOk)&&!bOk){(*O)->TryGetStringField(TEXT("name"),N);(*O)->TryGetStringField(TEXT("message"),Why);Failed.Add(N+TEXT(": ")+Why);}}
        if(Failed.Num()>2){const int32 More=Failed.Num()-2;Failed.SetNum(2);Failed.Add(FString::Printf(TEXT("외 %d개"),More));}
        Status=Failed.IsEmpty()?Message:Message+TEXT("\n")+FString::Join(Failed,TEXT(" / "));
        LoadMine();if(bBoardLoaded)LoadBoard();LoadShares();
    });
}
void UProjectCustomRoomRulesWidget::BulkDelete()
{
    TArray<TSharedPtr<FJsonValue>> Names;for(const FString& N:ManageSelected)Names.Add(MakeShared<FJsonValueString>(N));
    if(Names.IsEmpty())return;
    AskThen(FString::Printf(TEXT("고른 프리셋 %d개를 삭제하시겠습니까? 게시판 글은 그대로 남습니다."),Names.Num()),TEXT("PresetBulkDelete"),[this,Names]
    {
        auto J=MakeShared<FJsonObject>();J->SetArrayField(TEXT("names"),Names);
        Send(TEXT("POST"),TEXT("/api/room-rule-presets/delete"),J,[this](const TSharedPtr<FJsonObject>& A)
        {
            ReadPresets(A);A->TryGetStringField(TEXT("message"),Status);ManageSelected.Reset();
            ManagePage=FMath::Clamp(ManagePage,0,FMath::Max(0,FMath::DivideAndRoundUp(PresetNames.Num(),ManageRows)-1));
        });
    });
}
FString UProjectCustomRoomRulesWidget::DescribeRules(const FProjectCustomMatchRules& R)
{
    if(!R.bEnabled)return TEXT("사용자 지정 꺼짐 (기본 규칙)");
    TArray<FString> Lines;
    Lines.Add(FString::Printf(TEXT("데스카운트 %d · 마네킹 %d · %s"),R.DeathCount,R.MannequinCount,R.TimeLimitSeconds==0?TEXT("시간 무제한"):*FString::Printf(TEXT("제한 %d분"),R.TimeLimitSeconds/60)));
    auto Group=[&Lines](const TCHAR* Title,const TArray<FString>& Items){if(!Items.IsEmpty())Lines.Add(FString(Title)+TEXT(": ")+FString::Join(Items,TEXT(", ")));};
    TArray<FString> Mix;for(const FString& Id:R.AffixIds)if(const auto* A=ProjectAffixes::Find(Id))Mix.Add(A->Name);if(R.bItemCards)Mix.Add(TEXT("아이템 카드"));
    TArray<FString> Free;if(R.bInfiniteLives)Free.Add(TEXT("무한 목숨"));if(R.bInfection)Free.Add(TEXT("감염"));if(R.bGhosts)Free.Add(TEXT("유령 생존자"));
    if(R.HideRevealSeconds>0)Free.Add(FString::Printf(TEXT("숨으면 들킴 %d초"),R.HideRevealSeconds));
    static const TCHAR* Escape[]={TEXT(""),TEXT("함께 탈출"),TEXT("마지막 한 명이 모두를 구한다"),TEXT("먼저 나간 사람 승리")};if(R.EscapeMode>0&&R.EscapeMode<4)Free.Add(Escape[R.EscapeMode]);
    TArray<FString> Env;if(R.GazeLimitSeconds>0)Env.Add(FString::Printf(TEXT("바라보기 %d초"),R.GazeLimitSeconds));if(R.Darkness>0)Env.Add(FString::Printf(TEXT("어둠 %d"),R.Darkness));
    if(R.SoundOnlyMeters>0)Env.Add(FString::Printf(TEXT("소리만 들리는 마네킹 %dm"),R.SoundOnlyMeters));if(R.bSpreadingFire)Env.Add(TEXT("번지는 불"));
    TArray<FString> Modes;if(R.bHideAndSeek)Modes.Add(TEXT("숨바꼭질"));if(R.bWaves)Modes.Add(TEXT("웨이브 생존"));if(R.bRuleVote)Modes.Add(TEXT("규칙 카드 투표"));if(R.bRoleSwap)Modes.Add(TEXT("역할 바꾸기"));
    Group(TEXT("섞기"),Mix);Group(TEXT("자유 규칙"),Free);Group(TEXT("환경"),Env);Group(TEXT("놀이 방식"),Modes);
    if(Lines.Num()==1)Lines.Add(TEXT("놀이터 규칙 없음"));
    return FString::Join(Lines,TEXT("\n"));
}
void UProjectCustomRoomRulesWidget::OpenPreview(const FString& Title,const FString& Info,const FString& Name,const FProjectCustomMatchRules& Rules)
{
    PreviewTitle=Title;PreviewInfo=Info;PreviewName=Name.Left(32);PreviewRules=Rules;PreviewRules.Handicaps.Reset();bPreviewOpen=true;Status.Reset();
}
void UProjectCustomRoomRulesWidget::ReadShareAnswer(const TSharedPtr<FJsonObject>& Json)
{
    FString Code,Name,Owner,Expires,RuleCode;bool bBoard=false;const TSharedPtr<FJsonObject>* R=nullptr;FProjectCustomMatchRules Rules;
    if(!Json->TryGetObjectField(TEXT("rules"),R)||!FProjectCustomMatchRules::Parse(*R,Rules)){Status=TEXT("이 공유 코드의 규칙을 이 버전에서 쓸 수 없습니다.");return;}
    Json->TryGetStringField(TEXT("code"),Code);Json->TryGetStringField(TEXT("name"),Name);Json->TryGetStringField(TEXT("owner"),Owner);
    Json->TryGetStringField(TEXT("expiresAtUtc"),Expires);Json->TryGetStringField(TEXT("ruleCode"),RuleCode);Json->TryGetBoolField(TEXT("board"),bBoard);
    FDateTime At;const FString When=bBoard?FString(TEXT("게시판 글 (고정)")):Expires.IsEmpty()?FString(TEXT("기한 없음")):
        FDateTime::ParseIso8601(*Expires,At)?FString::Printf(TEXT("%s까지"),*(At+(FDateTime::Now()-FDateTime::UtcNow())).ToString(TEXT("%m월 %d일 %H:%M"))):FString(TEXT("기한 있음"));
    OpenPreview(Name,FString::Printf(TEXT("보낸 사람 %s · 공유 코드 %s · %s\n기록용 규칙 코드 %s"),*Owner,*Code,*When,*RuleCode),Name,Rules);
}
void UProjectCustomRoomRulesWidget::SavePresetAs(const FString& Name,const FProjectCustomMatchRules& Rules)
{
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("action"),TEXT("Save"));J->SetStringField(TEXT("name"),Name.Left(32));
    FProjectCustomMatchRules Clean=Rules;Clean.Handicaps.Reset();J->SetObjectField(TEXT("rules"),Clean.ToJson());
    const FString Saved=Name.Left(32);
    Send(TEXT("POST"),TEXT("/api/room-rule-presets"),J,[this,Saved](const TSharedPtr<FJsonObject>& A)
    {ReadPresets(A);Status=FString::Printf(TEXT("'%s'을(를) 내 프리셋에 저장했습니다. 같은 이름이 있으면 덮어씁니다."),*Saved);});
}
void UProjectCustomRoomRulesWidget::LoadShares(){Send(TEXT("GET"),TEXT("/api/share-codes"),nullptr,[this](const TSharedPtr<FJsonObject>& J){ReadShares(J);});}
void UProjectCustomRoomRulesWidget::ReadShares(const TSharedPtr<FJsonObject>& Json)
{
    const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;if(!Json->TryGetArrayField(TEXT("items"),Items))return;
    ShareNames.Reset();ShareCodesList.Reset();ShareBoard.Reset();
    for(const auto& V:*Items)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;if(!V->TryGetObject(O))continue;FString Code,Name,Expires;bool bBoard=false;
        (*O)->TryGetStringField(TEXT("code"),Code);(*O)->TryGetStringField(TEXT("name"),Name);(*O)->TryGetStringField(TEXT("expiresAtUtc"),Expires);(*O)->TryGetBoolField(TEXT("board"),bBoard);
        FDateTime At;const FString When=bBoard?FString(TEXT("게시판 고정")):Expires.IsEmpty()?FString(TEXT("기한 없음")):
            FDateTime::ParseIso8601(*Expires,At)?(At+(FDateTime::Now()-FDateTime::UtcNow())).ToString(TEXT("%m/%d %H:%M까지")):FString();
        ShareNames.Add(MakeShared<FString>(FString::Printf(TEXT("%s · %s · %s"),*Code,*Name,*When)));ShareCodesList.Add(Code);ShareBoard.Add(bBoard);
    }
    if(!ShareCodesList.Contains(SelectedShare))SelectedShare.Reset();if(ShareCombo)ShareCombo->RefreshOptions();
}
bool UProjectCustomRoomRulesWidget::SelectedShareIsBoard()const{const int32 I=ShareCodesList.IndexOfByKey(SelectedShare);return ShareBoard.IsValidIndex(I)&&ShareBoard[I];}
void UProjectCustomRoomRulesWidget::LoadMine(){Send(TEXT("GET"),TEXT("/api/shared-presets/mine"),nullptr,[this](const TSharedPtr<FJsonObject>& J){ReadMine(J);});}
void UProjectCustomRoomRulesWidget::ReadMine(const TSharedPtr<FJsonObject>& Json)
{
    const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;if(!Json->TryGetArrayField(TEXT("items"),Items))return;
    MyPosts.Reset();
    for(const auto& V:*Items)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;const TSharedPtr<FJsonObject>* R=nullptr;if(!V->TryGetObject(O)||!(*O)->TryGetObjectField(TEXT("rules"),R))continue;
        FMyPost P;if(!FProjectCustomMatchRules::Parse(*R,P.Rules))continue;
        (*O)->TryGetNumberField(TEXT("id"),P.Id);(*O)->TryGetStringField(TEXT("name"),P.Name);(*O)->TryGetBoolField(TEXT("published"),P.bPublished);(*O)->TryGetBoolField(TEXT("hidden"),P.bHidden);
        MyPosts.Add(MoveTemp(P));
    }
}
const UProjectCustomRoomRulesWidget::FMyPost* UProjectCustomRoomRulesWidget::FindMyPost(const FProjectCustomMatchRules& Rules)const
{
    // A post is the same rules as a saved preset when they match ignoring affix order and handicaps (as rule codes do).
    auto Norm=[](FProjectCustomMatchRules R){R.Handicaps.Reset();R.AffixIds.Sort();return R;};
    const FProjectCustomMatchRules Want=Norm(Rules);
    return MyPosts.FindByPredicate([&](const FMyPost& P){return Norm(P.Rules)==Want;});
}
void UProjectCustomRoomRulesWidget::TogglePosted(const FString& Name,const FProjectCustomMatchRules& Rules)
{
    if(const FMyPost* P=FindMyPost(Rules)){SetPublished(P->Id,!P->bPublished);return;}
    // Not on the board yet: post it under the preset's name (the backend asks for a clear first).
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),Name.Left(32));J->SetStringField(TEXT("summary"),TEXT(""));
    FProjectCustomMatchRules Clean=Rules;Clean.Handicaps.Reset();J->SetObjectField(TEXT("rules"),Clean.ToJson());
    Send(TEXT("POST"),TEXT("/api/shared-presets"),J,[this](const TSharedPtr<FJsonObject>& A){A->TryGetStringField(TEXT("message"),Status);LoadMine();if(bBoardLoaded)LoadBoard();LoadShares();});
}
void UProjectCustomRoomRulesWidget::SetPublished(int64 Id,bool bPublish)
{
    auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("published"),bPublish);
    Send(TEXT("PUT"),FString::Printf(TEXT("/api/shared-presets/%lld"),Id),J,[this](const TSharedPtr<FJsonObject>& A){A->TryGetStringField(TEXT("message"),Status);LoadMine();if(bBoardLoaded)LoadBoard();});
}
void UProjectCustomRoomRulesWidget::ReadPresets(const TSharedPtr<FJsonObject>& Json)
{
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!Json->TryGetArrayField(TEXT("presets"),Rows)||Rows->Num()>20)return;
    Presets.Reset();PresetNames.Reset();
    for(const auto& V:*Rows)
    {auto J=V->Type==EJson::Object?V->AsObject():nullptr;FString N;const TSharedPtr<FJsonObject>* R=nullptr;FProjectCustomMatchRules Rules;
      if(J&&J->TryGetStringField(TEXT("name"),N)&&N.Len()<=32&&J->TryGetObjectField(TEXT("rules"),R)&&FProjectCustomMatchRules::Parse(*R,Rules)){Presets.Add(N,Rules);PresetNames.Add(MakeShared<FString>(N));}}
    if(!Presets.Contains(SelectedName))SelectedName.Reset();if(PresetCombo)PresetCombo->RefreshOptions();
}
void UProjectCustomRoomRulesWidget::Send(const FString& Verb,const FString& Path,const TSharedPtr<FJsonObject>& Body,TFunction<void(const TSharedPtr<FJsonObject>&)> OnJson)
{
    auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();if(!A||!A->IsSignedIn())return;
    // One request at a time; a few more wait their turn (e.g. opening loads rules, my posts and my share codes).
    if(Request){if(Queued.Num()<8)Queued.Add([this,Verb,Path,Body,OnJson]{Send(Verb,Path,Body,OnJson);});return;}
    if(Verb!=TEXT("GET")&&!CanEdit())return;
    const FString Token=A->GetAccessTokenForAuthenticatedRequest(),Base=A->GetApiBaseUrlForAuthenticatedRequest();
    auto Pending=FHttpModule::Get().CreateRequest();Request=Pending;Pending->SetURL(Base+Path);Pending->SetVerb(Verb);Pending->SetTimeout(5);
    Pending->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Token);FProjectProject01VersionContract::ApplyToRequest(Pending);
    if(Body){FString Text;FJsonSerializer::Serialize(Body.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));Pending->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));Pending->SetContentAsString(Text);}
    Pending->OnProcessRequestComplete().BindWeakLambda(this,[this,Verb,Token,Base,OnJson](FHttpRequestPtr Completed,FHttpResponsePtr Response,bool Connected)
    {
        if(Request!=Completed)return;Request.Reset();
        ON_SCOPE_EXIT{if(!Request&&!Queued.IsEmpty()){auto Next=Queued[0];Queued.RemoveAt(0);Next();}};
        auto* Auth=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();
        if(!Auth||Auth->GetAccessTokenForAuthenticatedRequest()!=Token||Auth->GetApiBaseUrlForAuthenticatedRequest()!=Base)return;
        if(Response&&Response->GetResponseCode()==401){Auth->InvalidateSessionFromServerFailure(TEXT("로그인이 만료되었습니다. 다시 로그인하세요."));Close();return;}
        // Apply returns the current room, including up to 100 UTF-8 chat messages.
        TSharedPtr<FJsonObject> J;const bool Decoded=Response&&Response->GetContentLength()<=1024*1024&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),J)&&J;
        if(!Connected||!Response||!EHttpResponseCodes::IsOk(Response->GetResponseCode())||!Decoded)
        {Status=TEXT("요청에 실패했습니다.");if(Decoded)J->TryGetStringField(TEXT("message"),Status);return;}
        if(OnJson){OnJson(J);return;} // Rule codes and other side requests: the caller reads its own answer.
        if(Verb==TEXT("GET"))
        {FString Id;const TSharedPtr<FJsonObject>* Rules=nullptr;if(!J->TryGetStringField(TEXT("roomId"),Id)||Id!=RoomId||!J->TryGetBoolField(TEXT("editable"),bEditable)||!J->TryGetObjectField(TEXT("rules"),Rules)||!FProjectCustomMatchRules::Parse(*Rules,Draft)){Status=TEXT("방 규칙을 불러오지 못했습니다.");bEditable=false;return;}}
        else {J->TryGetStringField(TEXT("message"),Status);if(auto* L=GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>())L->RefreshCurrentRoom();}
        ReadPresets(J);
    });
    if(!Pending->ProcessRequest()){Request.Reset();Status=TEXT("서버에 연결하지 못했습니다.");}
}
void UProjectCustomRoomRulesWidget::NativeTick(const FGeometry& G,float D)
{
    Super::NativeTick(G,D);auto* GI=GetGameInstance();auto* A=GI?GI->GetSubsystem<UProjectProject01AuthSubsystem>():nullptr;auto* L=GI?GI->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
    if(!A||!A->IsSignedIn()||!L||L->GetCurrentRoom().RoomId!=RoomId||!L->GetCurrentRoom().bIsHost||L->GetCurrentRoom().bStarting||L->GetCurrentRoom().bStarted||L->GetCurrentRoom().bServerPreparing)Close();
    RefreshHandicapMembers(); // Cheap: rebuilds the list only when who is in the room changed.
}
void UProjectCustomRoomRulesWidget::Close()
{
    RemoveFromParent();if(auto* P=GetOwningPlayer())if(auto* Return=ReturnWidget.Get();Return&&Return->IsInViewport()){FInputModeUIOnly Input;Input.SetWidgetToFocus(Return->TakeWidget());P->SetInputMode(Input);}
}
void UProjectCustomRoomRulesWidget::NativeDestruct()
{if(Request){Request->OnProcessRequestComplete().Unbind();Request->CancelRequest();Request.Reset();}Super::NativeDestruct();}
void UProjectCustomRoomRulesWidget::ReleaseSlateResources(bool Children)
{PresetCombo.Reset();NameInput.Reset();CodeInput.Reset();HandicapCombo.Reset();EscapeCombo.Reset();Super::ReleaseSlateResources(Children);}
void UProjectCustomRoomRulesWidget::AskThen(const FString& Question,const FString& SkipKey,TFunction<void()> Run)
{
    if(UProjectConfirmDialogWidget::IsSkippedToday(SkipKey)){Run();return;}
    if(!IsValid(ApplyConfirm)&&GetOwningPlayer())
    {ApplyConfirm=CreateWidget<UProjectConfirmDialogWidget>(GetOwningPlayer());if(ApplyConfirm)ApplyConfirm->AddToViewport(UProjectConfirmDialogWidget::ViewportZOrder);}
    if(!IsValid(ApplyConfirm)){Run();return;}
    const TWeakObjectPtr<UProjectCustomRoomRulesWidget> Weak(this);
    ApplyConfirm->SetReturnFocus(this);
    ApplyConfirm->Ask(Question,[Weak,Run]{if(Weak.IsValid())Run();},SkipKey);
}
void UProjectCustomRoomRulesWidget::Vote(int64 Id,int32 Value)
{
    auto Body=MakeShared<FJsonObject>();Body->SetNumberField(TEXT("vote"),Value);
    Send(TEXT("POST"),FString::Printf(TEXT("/api/shared-presets/%lld/vote"),Id),Body,[this](const TSharedPtr<FJsonObject>& J){J->TryGetStringField(TEXT("message"),Status);LoadBoard();});
}
void UProjectCustomRoomRulesWidget::LoadBoard()
{
    Send(TEXT("GET"),FString::Printf(TEXT("/api/shared-presets?sort=%s&page=%d"),*BoardSort,BoardPage),nullptr,[this](const TSharedPtr<FJsonObject>& J){ReadBoard(J);});
}
void UProjectCustomRoomRulesWidget::ReadBoard(const TSharedPtr<FJsonObject>& Json)
{
    const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;if(!Json->TryGetArrayField(TEXT("items"),Items))return;
    Board.Reset();bBoardLoaded=true;Json->TryGetBoolField(TEXT("more"),bBoardMore);
    for(const auto& Value:*Items)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;const TSharedPtr<FJsonObject>* R=nullptr;if(!Value->TryGetObject(O)||!(*O)->TryGetObjectField(TEXT("rules"),R))continue;
        FBoardItem I;if(!FProjectCustomMatchRules::Parse(*R,I.Rules))continue; // Rules this build cannot run are skipped.
        (*O)->TryGetNumberField(TEXT("id"),I.Id);(*O)->TryGetStringField(TEXT("code"),I.Code);(*O)->TryGetStringField(TEXT("name"),I.Name);
        (*O)->TryGetStringField(TEXT("summary"),I.Summary);(*O)->TryGetStringField(TEXT("author"),I.Author);
        (*O)->TryGetStringField(TEXT("shareCode"),I.ShareCode);(*O)->TryGetBoolField(TEXT("published"),I.bPublished);
        (*O)->TryGetNumberField(TEXT("myVote"),I.MyVote);(*O)->TryGetNumberField(TEXT("dislikes"),I.Dislikes);(*O)->TryGetBoolField(TEXT("mine"),I.bMine);
        (*O)->TryGetBoolField(TEXT("reported"),I.bReported);(*O)->TryGetBoolField(TEXT("hidden"),I.bHidden);(*O)->TryGetBoolField(TEXT("cleared"),I.bCleared);
        int32 Plays=0,Survivor=0,Mannequin=0,Rate=-1;(*O)->TryGetNumberField(TEXT("plays"),Plays);(*O)->TryGetNumberField(TEXT("survivorWins"),Survivor);
        (*O)->TryGetNumberField(TEXT("mannequinWins"),Mannequin);(*O)->TryGetNumberField(TEXT("likes"),I.Likes);(*O)->TryGetNumberField(TEXT("clearRate"),Rate);
        I.Stats=MakeBoardStats(Plays,Rate,Survivor,Mannequin);
        Board.Add(MoveTemp(I));if(Board.Num()>=4)break;
    }
}
FString UProjectCustomRoomRulesWidget::MakeBoardStats(int32 Plays,int32 ClearRate,int32 SurvivorWins,int32 MannequinWins)
{
    return FString::Printf(TEXT("%d판 · 클리어률 %s · 생존자 승 %d · 마네킹 승 %d"),Plays,ClearRate<0?TEXT("—"):*FString::Printf(TEXT("%d%%"),ClearRate),SurvivorWins,MannequinWins);
}
void UProjectCustomRoomRulesWidget::ReadCodeAnswer(const TSharedPtr<FJsonObject>& Json,bool bLoadRules)
{
    // Docs/CustomGamePlayground.md 6: the code goes into the box (to copy), its plain summary into the fixed line.
    FString Code;const TSharedPtr<FJsonObject>* Summary=nullptr;
    if(!Json||!Json->TryGetStringField(TEXT("code"),Code)||!Json->TryGetObjectField(TEXT("summary"),Summary)){Status=TEXT("코드를 읽지 못했습니다.");return;}
    if(bLoadRules)
    {
        const TSharedPtr<FJsonObject>* Rules=nullptr;FProjectCustomMatchRules Loaded;
        if(!Json->TryGetObjectField(TEXT("rules"),Rules)||!FProjectCustomMatchRules::Parse(*Rules,Loaded)){Status=TEXT("이 코드의 규칙을 이 버전에서 쓸 수 없습니다.");return;}
        {const auto Keep=Draft.Handicaps;Draft=Loaded;Draft.Handicaps=Keep;}Status=FString::Printf(TEXT("코드 %s의 규칙을 불러왔습니다. 적용을 눌러야 방에 반영됩니다."),*Code);
    }
    else Status=TEXT("현재 규칙의 코드를 만들었습니다. 상자에서 복사해 나눌 수 있습니다.");
    if(CodeInput)CodeInput->SetText(FText::FromString(Code));
    int32 Games=0,SurvivorWins=0,MannequinWins=0,MyGames=0,MyWins=0;
    (*Summary)->TryGetNumberField(TEXT("games"),Games);(*Summary)->TryGetNumberField(TEXT("survivorWins"),SurvivorWins);(*Summary)->TryGetNumberField(TEXT("mannequinWins"),MannequinWins);
    (*Summary)->TryGetNumberField(TEXT("myGames"),MyGames);(*Summary)->TryGetNumberField(TEXT("myWins"),MyWins);
    CodeLine=Games==0?FString::Printf(TEXT("%s · 아직 이 코드로 끝난 판이 없습니다."),*Code)
        :FString::Printf(TEXT("%s · 지금까지 %d판 · 생존자 승 %d · 마네킹 승 %d\n내 결과: %d판 중 %d승"),*Code,Games,SurvivorWins,MannequinWins,MyGames,MyWins);
}
void UProjectCustomRoomRulesWidget::RefreshHandicapMembers()
{
    auto* GI=GetGameInstance();auto* L=GI?GI->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;if(!L)return;
    TArray<FString> Ids,Names;
    for(const auto& M:L->GetCurrentRoom().Members)if(!M.bIsBot&&!M.bIsObserver&&!M.UserId.IsEmpty()){Ids.Add(M.UserId);Names.Add(M.DisplayName.IsEmpty()?M.UserId:M.DisplayName);}
    const FString Key=FString::Join(Ids,TEXT(","))+TEXT("|")+FString::Join(Names,TEXT(","));
    if(Key==HandicapMembersKey)return;
    HandicapMembersKey=Key;HandicapUserIds=Ids;HandicapNames.Reset();for(const FString& N:Names)HandicapNames.Add(MakeShared<FString>(N));
    if(!HandicapUserIds.Contains(HandicapUserId))HandicapUserId.Reset();
    // Someone who left can no longer carry a handicap (the server refuses names outside the room).
    Draft.Handicaps.RemoveAll([this](const FProjectPlaygroundHandicap& H){return !HandicapUserIds.Contains(H.UserId);});
    if(HandicapCombo)HandicapCombo->RefreshOptions();
}
void UProjectCustomRoomRulesWidget::SetHandicapValue(int32 FProjectPlaygroundHandicap::* Member,int32 Value)
{
    if(HandicapUserId.IsEmpty()||!CanEdit())return;
    FProjectPlaygroundHandicap* H=Draft.Handicaps.FindByPredicate([this](const FProjectPlaygroundHandicap& E){return E.UserId==HandicapUserId;});
    if(!H){if(Value==0||Draft.Handicaps.Num()>=ProjectPlaygroundRules::MaxHandicaps)return;H=&Draft.Handicaps.AddDefaulted_GetRef();H->UserId=HandicapUserId;}
    H->*Member=Value;
    if(H->IsNeutral())Draft.Handicaps.RemoveAll([this](const FProjectPlaygroundHandicap& E){return E.UserId==HandicapUserId;});
}

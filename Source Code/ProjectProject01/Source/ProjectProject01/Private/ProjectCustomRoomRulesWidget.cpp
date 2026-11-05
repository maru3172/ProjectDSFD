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
    if(!R.bEnabled){R.AffixIds.Reset();R.bItemCards=R.bInfiniteLives=R.bInfection=R.bGhosts=false;R.HideRevealSeconds=0;}
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
    // 프리셋·코드: recommended presets (three columns, one fixed summary line), my presets, rule codes.
    auto Codes=SNew(SVerticalBox);
    Codes->AddSlot().AutoHeight().Padding(0,0,0,4)[Caption(TEXT("추천 프리셋"))];
    {
        const TArray<ProjectPlaygroundPresets::FPreset> Recommended=ProjectPlaygroundPresets::Available();
        for(int32 I=0;I<Recommended.Num();I+=3)
        {
            auto Row=SNew(SHorizontalBox);
            for(int32 J=I;J<FMath::Min(I+3,Recommended.Num());++J)
            {
                const ProjectPlaygroundPresets::FPreset Preset=Recommended[J];
                Row->AddSlot().FillWidth(1).Padding(J==I?FMargin(0,0,3,0):FMargin(3,0,J==I+2?0:3,0))
                [SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(FMargin(10,5))
                  .IsEnabled_Lambda([this]{return CanEdit();})
                  .OnHovered_Lambda([this,Preset]{HoveredPreset=Preset.Name+TEXT(" · ")+Preset.Summary;})
                  .OnClicked_Lambda([this,Preset]{Draft=Preset.Rules;HoveredPreset=Preset.Name+TEXT(" · ")+Preset.Summary;
                      Status=FString::Printf(TEXT("'%s'을(를) 불러왔습니다. 적용을 눌러야 방에 반영됩니다."),*Preset.Name);return FReply::Handled();})
                  [Caption(Preset.Name,16)]];
            }
            for(int32 Empty=FMath::Min(I+3,Recommended.Num())-I;Empty<3;++Empty)Row->AddSlot().FillWidth(1).Padding(3,0)[SNew(SBox)]; // Short last row keeps the same button width.
            Codes->AddSlot().AutoHeight().Padding(0,2)[Row];
        }
    }
    Codes->AddSlot().AutoHeight().Padding(0,4)
    [SNew(SBox).HeightOverride(22)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text_Lambda([this]{return FText::FromString(HoveredPreset.IsEmpty()?TEXT("추천 프리셋에 마우스를 올리면 설명이 나옵니다."):HoveredPreset);})]];
    Codes->AddSlot().AutoHeight().Padding(0,6,0,4)[Caption(TEXT("내 프리셋"))];
    // The saved-preset list and the name box share one row.
    Codes->AddSlot().AutoHeight().Padding(0,4)
    [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
    [SAssignNew(PresetCombo,SComboBox<TSharedPtr<FString>>).OptionsSource(&PresetNames).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ItemStyle(&ProjectDFSDUI::RowStyle()).HasDownArrow(false)
      .IsEnabled_Lambda([this]{return !Request;}).OnGenerateWidget_Lambda([](TSharedPtr<FString> Name){return Caption(Name?*Name:FString(),16);})
      .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Name,ESelectInfo::Type){SelectedName=Name?*Name:FString();if(NameInput)NameInput->SetText(FText::FromString(SelectedName));})
      [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).Text_Lambda([this]{return FText::FromString(SelectedName.IsEmpty()?TEXT("프리셋 선택"):SelectedName);}).ColorAndOpacity(ProjectDFSDUI::Text())]
       +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0)[SNew(SRulesChevron)]]]
     +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)
    [SAssignNew(NameInput,SEditableTextBox).Style(&ProjectDFSDUI::InputStyle()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).HintText(FText::FromString(TEXT("프리셋 이름"))).IsEnabled_Lambda([this]{return !Request;})]];
    auto Actions=SNew(SHorizontalBox);
    Actions->AddSlot().FillWidth(1).Padding(0,0,4,0)[Button(TEXT("불러오기"),[this]{if(auto* R=Presets.Find(SelectedName)){Draft=*R;Status.Reset();}},[this]{return CanEdit()&&Presets.Contains(SelectedName);})];
    Actions->AddSlot().FillWidth(1).Padding(4,0)[Button(TEXT("저장"),[this]{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("action"),TEXT("Save"));J->SetStringField(TEXT("name"),NameInput->GetText().ToString().TrimStartAndEnd());J->SetObjectField(TEXT("rules"),Outgoing().ToJson());Send(TEXT("POST"),TEXT("/api/room-rule-presets"),J);},[this]{return CanEdit()&&NameInput&&!NameInput->GetText().IsEmpty()&&Outgoing().IsValid();})];
    Actions->AddSlot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("삭제"),[this]{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("action"),TEXT("Delete"));J->SetStringField(TEXT("name"),SelectedName);Send(TEXT("POST"),TEXT("/api/room-rule-presets"),J);},[this]{return CanEdit()&&Presets.Contains(SelectedName);})];
    Codes->AddSlot().AutoHeight().Padding(0,4)[Actions];
    // Rule code: type a code to load its rules, or make the code of the current rules (it lands in the box to copy).
    Codes->AddSlot().AutoHeight().Padding(0,10,0,4)[Caption(TEXT("규칙 코드"))];
    {
        auto Row=SNew(SHorizontalBox);
        Row->AddSlot().FillWidth(1).Padding(0,0,4,0)
        [SAssignNew(CodeInput,SEditableTextBox).Style(&ProjectDFSDUI::InputStyle()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).HintText(FText::FromString(TEXT("예: ZB7K-2QXM"))).IsEnabled_Lambda([this]{return !Request;})];
        Row->AddSlot().AutoWidth().Padding(4,0)[Button(TEXT("불러오기"),[this]
        {
            const FString Code=CodeInput?CodeInput->GetText().ToString().TrimStartAndEnd():FString();
            Send(TEXT("GET"),TEXT("/api/rule-codes/")+FGenericPlatformHttp::UrlEncode(Code),nullptr,[this](const TSharedPtr<FJsonObject>& J){ReadCodeAnswer(J,true);});
        },[this]{return CanEdit()&&CodeInput&&CodeInput->GetText().ToString().TrimStartAndEnd().Len()>=8;})];
        Row->AddSlot().AutoWidth().Padding(4,0,0,0)[Button(TEXT("코드 만들기"),[this]
        {
            auto J=MakeShared<FJsonObject>();J->SetObjectField(TEXT("rules"),Outgoing().ToJson());
            Send(TEXT("POST"),TEXT("/api/rule-codes"),J,[this](const TSharedPtr<FJsonObject>& Answer){ReadCodeAnswer(Answer,false);});
        },[this]{return CanEdit()&&Draft.bEnabled&&Outgoing().IsValid();})];
        Codes->AddSlot().AutoHeight().Padding(0,4)[Row];
    }
    Codes->AddSlot().AutoHeight().Padding(0,4)
    [SNew(SBox).HeightOverride(44)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Text())
      .Text_Lambda([this]{return FText::FromString(CodeLine.IsEmpty()?TEXT("같은 코드로 한 판들의 결과가 여기에 모입니다. 공식 기록과는 따로입니다."):CodeLine);})]];

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
    Vision->AddSlot().AutoHeight().Padding(0,4)
    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text_Lambda([this]{return FText::FromString(Draft.bEnabled?TEXT(""):TEXT("기본 탭에서 사용자 지정을 켜면 고를 수 있습니다."));})];

    auto TabButton=[this](const FString& Text,int32 Index)
    {return SNew(SButton).ButtonStyle(&TightButtonStyle()).HAlign(HAlign_Fill).VAlign(VAlign_Fill).ContentPadding(FMargin(1))
        .OnClicked_Lambda([this,Index]{Tab=Index;return FReply::Handled();})
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).Padding(FMargin(16,5)).HAlign(HAlign_Center)
         .BorderBackgroundColor_Lambda([this,Index]{return Tab==Index?FSlateColor(ProjectDFSDUI::Accent().CopyWithNewOpacity(.6f)):FSlateColor(FLinearColor::Transparent);})
         [Caption(Text,17)]];};
    auto Tabs=SNew(SHorizontalBox);
    Tabs->AddSlot().AutoWidth().Padding(0,0,6,0)[TabButton(TEXT("기본"),0)];
    Tabs->AddSlot().AutoWidth().Padding(0,0,6,0)[TabButton(TEXT("섞기"),1)];
    Tabs->AddSlot().AutoWidth().Padding(0,0,6,0)[TabButton(TEXT("자유 규칙"),2)];
    Tabs->AddSlot().AutoWidth().Padding(0,0,6,0)[TabButton(TEXT("시야·소리"),3)];
    Tabs->AddSlot().AutoWidth()[TabButton(TEXT("프리셋·코드"),4)];

    auto Body=SNew(SVerticalBox);
    Body->AddSlot().AutoHeight().Padding(0,0,0,12)[Caption(TEXT("커스텀 규칙"),25)];
    Body->AddSlot().AutoHeight().Padding(0,0,0,10)[Tabs];
    // Fixed height so switching tabs never resizes the panel.
    Body->AddSlot().AutoHeight()
    [SNew(SBox).HeightOverride(500)[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]{return Tab;})+SWidgetSwitcher::Slot()[Basic]+SWidgetSwitcher::Slot()[Mix]+SWidgetSwitcher::Slot()[Free]+SWidgetSwitcher::Slot()[Vision]+SWidgetSwitcher::Slot()[Codes]]];
    Body->AddSlot().AutoHeight().Padding(0,8)
    [SNew(SBox).HeightOverride(44)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())
      .Text_Lambda([this]{return FText::FromString(Request?TEXT("불러오는 중…"):!Outgoing().IsValid()?TEXT("규칙 조합을 확인하세요."):Status);})]];
    auto Footer=SNew(SHorizontalBox);
    Footer->AddSlot().FillWidth(1).Padding(0,0,4,0)[Button(TEXT("적용"),[this]{AskThen(TEXT("적용하시겠습니까?"),TEXT("RoomRulesApply"),[this]{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("roomId"),RoomId);J->SetObjectField(TEXT("rules"),Outgoing().ToJson());Send(TEXT("PUT"),TEXT("/api/rooms/current/custom-rules"),J);});},[this]{return CanEdit()&&Outgoing().IsValid();})];
    Footer->AddSlot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("돌아가기"),[this]{Close();},[]{return true;})];
    Body->AddSlot().AutoHeight()[Footer];
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(0,0,0,.65f)).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(18)
      [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
       [SNew(SBox).WidthOverride(620)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(24)[Body]]]];
}
void UProjectCustomRoomRulesWidget::NativeConstruct(){Super::NativeConstruct();Send(TEXT("GET"),TEXT("/api/rooms/current/custom-rules"));}
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
    auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();if(Request||!A||!A->IsSignedIn())return;
    if(Verb!=TEXT("GET")&&!CanEdit())return;
    const FString Token=A->GetAccessTokenForAuthenticatedRequest(),Base=A->GetApiBaseUrlForAuthenticatedRequest();
    auto Pending=FHttpModule::Get().CreateRequest();Request=Pending;Pending->SetURL(Base+Path);Pending->SetVerb(Verb);Pending->SetTimeout(5);
    Pending->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Token);FProjectProject01VersionContract::ApplyToRequest(Pending);
    if(Body){FString Text;FJsonSerializer::Serialize(Body.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));Pending->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));Pending->SetContentAsString(Text);}
    Pending->OnProcessRequestComplete().BindWeakLambda(this,[this,Verb,Token,Base,OnJson](FHttpRequestPtr Completed,FHttpResponsePtr Response,bool Connected)
    {
        if(Request!=Completed)return;Request.Reset();auto* Auth=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();
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
}
void UProjectCustomRoomRulesWidget::Close()
{
    RemoveFromParent();if(auto* P=GetOwningPlayer())if(auto* Return=ReturnWidget.Get();Return&&Return->IsInViewport()){FInputModeUIOnly Input;Input.SetWidgetToFocus(Return->TakeWidget());P->SetInputMode(Input);}
}
void UProjectCustomRoomRulesWidget::NativeDestruct()
{if(Request){Request->OnProcessRequestComplete().Unbind();Request->CancelRequest();Request.Reset();}Super::NativeDestruct();}
void UProjectCustomRoomRulesWidget::ReleaseSlateResources(bool Children)
{PresetCombo.Reset();NameInput.Reset();CodeInput.Reset();Super::ReleaseSlateResources(Children);}
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
void UProjectCustomRoomRulesWidget::ReadCodeAnswer(const TSharedPtr<FJsonObject>& Json,bool bLoadRules)
{
    // Docs/CustomGamePlayground.md 6: the code goes into the box (to copy), its plain summary into the fixed line.
    FString Code;const TSharedPtr<FJsonObject>* Summary=nullptr;
    if(!Json||!Json->TryGetStringField(TEXT("code"),Code)||!Json->TryGetObjectField(TEXT("summary"),Summary)){Status=TEXT("코드를 읽지 못했습니다.");return;}
    if(bLoadRules)
    {
        const TSharedPtr<FJsonObject>* Rules=nullptr;FProjectCustomMatchRules Loaded;
        if(!Json->TryGetObjectField(TEXT("rules"),Rules)||!FProjectCustomMatchRules::Parse(*Rules,Loaded)){Status=TEXT("이 코드의 규칙을 이 버전에서 쓸 수 없습니다.");return;}
        Draft=Loaded;Status=FString::Printf(TEXT("코드 %s의 규칙을 불러왔습니다. 적용을 눌러야 방에 반영됩니다."),*Code);
    }
    else Status=TEXT("현재 규칙의 코드를 만들었습니다. 상자에서 복사해 나눌 수 있습니다.");
    if(CodeInput)CodeInput->SetText(FText::FromString(Code));
    int32 Games=0,SurvivorWins=0,MannequinWins=0,MyGames=0,MyWins=0;
    (*Summary)->TryGetNumberField(TEXT("games"),Games);(*Summary)->TryGetNumberField(TEXT("survivorWins"),SurvivorWins);(*Summary)->TryGetNumberField(TEXT("mannequinWins"),MannequinWins);
    (*Summary)->TryGetNumberField(TEXT("myGames"),MyGames);(*Summary)->TryGetNumberField(TEXT("myWins"),MyWins);
    CodeLine=Games==0?FString::Printf(TEXT("%s · 아직 이 코드로 끝난 판이 없습니다."),*Code)
        :FString::Printf(TEXT("%s · 지금까지 %d판 · 생존자 승 %d · 마네킹 승 %d\n내 결과: %d판 중 %d승"),*Code,Games,SurvivorWins,MannequinWins,MyGames,MyWins);
}

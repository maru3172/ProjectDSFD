#include "ProjectCustomRoomRulesWidget.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "ProjectDFSDUITheme.h"
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
TSharedRef<SWidget> UProjectCustomRoomRulesWidget::RebuildWidget()
{
    auto Body=SNew(SVerticalBox);
    Body->AddSlot().AutoHeight().Padding(0,0,0,16)[Caption(TEXT("커스텀 규칙"),25)];
    Body->AddSlot().AutoHeight().Padding(0,0,0,12)
    [SNew(SCheckBox).Style(&ToggleStyle()).Padding(FMargin(8,0,0,0)).IsEnabled_Lambda([this]{return CanEdit();}).IsChecked_Lambda([this]{return Draft.bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
      .OnCheckStateChanged_Lambda([this](ECheckBoxState S){Draft.bEnabled=S==ECheckBoxState::Checked;})[Caption(TEXT("사용자 지정"))]];
    auto Number=[this,&Body](const FString& Label,int32 FProjectCustomMatchRules::* Member,int Min,int Max,int Divisor=1)
    {
        Body->AddSlot().AutoHeight().Padding(0,5)
        [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Caption(Label)]
          +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(180)
           [SNew(SSpinBox<int32>).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).ContentPadding(FMargin(8,4)).MinValue(Min).MaxValue(Max).MinSliderValue(Min).MaxSliderValue(Max).Delta(1)
            .IsEnabled_Lambda([this]{return CanEdit();}).Value_Lambda([this,Member,Divisor]{return Draft.*Member/Divisor;})
            .OnValueChanged_Lambda([this,Member,Divisor](int32 V){Draft.*Member=V*Divisor;})]]];
    };
    Number(TEXT("데스카운트"),&FProjectCustomMatchRules::DeathCount,1,20);
    Number(TEXT("마네킹 수"),&FProjectCustomMatchRules::MannequinCount,1,10);
    Number(TEXT("제한 시간 (분)"),&FProjectCustomMatchRules::TimeLimitSeconds,0,120,60);
    auto Button=[this](const FString& Text,TFunction<void()> Click,TFunction<bool()> Enabled)
    {return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(FMargin(14,7))
        .IsEnabled_Lambda([Enabled]{return Enabled();}).OnClicked_Lambda([Click]{Click();return FReply::Handled();})[Caption(Text,17)];};
    Body->AddSlot().AutoHeight().Padding(0,12)[Button(TEXT("적용"),[this]{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("roomId"),RoomId);J->SetObjectField(TEXT("rules"),Draft.ToJson());Send(TEXT("PUT"),TEXT("/api/rooms/current/custom-rules"),J);},[this]{return CanEdit();})];
    Body->AddSlot().AutoHeight().Padding(0,5)[Caption(TEXT("프리셋"))];
    Body->AddSlot().AutoHeight().Padding(0,5)
    [SAssignNew(PresetCombo,SComboBox<TSharedPtr<FString>>).OptionsSource(&PresetNames).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ItemStyle(&ProjectDFSDUI::RowStyle()).HasDownArrow(false)
      .IsEnabled_Lambda([this]{return !Request;}).OnGenerateWidget_Lambda([](TSharedPtr<FString> Name){return Caption(Name?*Name:FString(),16);})
      .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Name,ESelectInfo::Type){SelectedName=Name?*Name:FString();if(NameInput)NameInput->SetText(FText::FromString(SelectedName));})
      [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).Text_Lambda([this]{return FText::FromString(SelectedName.IsEmpty()?TEXT("프리셋 선택"):SelectedName);}).ColorAndOpacity(ProjectDFSDUI::Text())]
       +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0)[SNew(SRulesChevron)]]];
    Body->AddSlot().AutoHeight().Padding(0,5)
    [SAssignNew(NameInput,SEditableTextBox).Style(&ProjectDFSDUI::InputStyle()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).HintText(FText::FromString(TEXT("프리셋 이름"))).IsEnabled_Lambda([this]{return !Request;})];
    auto Actions=SNew(SHorizontalBox);
    Actions->AddSlot().FillWidth(1).Padding(0,0,4,0)[Button(TEXT("불러오기"),[this]{if(auto* R=Presets.Find(SelectedName)){Draft=*R;Status.Reset();}},[this]{return CanEdit()&&Presets.Contains(SelectedName);})];
    Actions->AddSlot().FillWidth(1).Padding(4,0)[Button(TEXT("저장"),[this]{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("action"),TEXT("Save"));J->SetStringField(TEXT("name"),NameInput->GetText().ToString().TrimStartAndEnd());J->SetObjectField(TEXT("rules"),Draft.ToJson());Send(TEXT("POST"),TEXT("/api/room-rule-presets"),J);},[this]{return CanEdit()&&NameInput&&!NameInput->GetText().IsEmpty();})];
    Actions->AddSlot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("삭제"),[this]{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("action"),TEXT("Delete"));J->SetStringField(TEXT("name"),SelectedName);Send(TEXT("POST"),TEXT("/api/room-rule-presets"),J);},[this]{return CanEdit()&&Presets.Contains(SelectedName);})];
    Body->AddSlot().AutoHeight().Padding(0,5)[Actions];
    Body->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).Text_Lambda([this]{return FText::FromString(Request?TEXT("불러오는 중…"):Status);}).AutoWrapText(true).ColorAndOpacity(ProjectDFSDUI::Muted())];
    Body->AddSlot().AutoHeight()[Button(TEXT("돌아가기"),[this]{Close();},[]{return true;})];
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
void UProjectCustomRoomRulesWidget::Send(const FString& Verb,const FString& Path,const TSharedPtr<FJsonObject>& Body)
{
    auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();if(Request||!A||!A->IsSignedIn())return;
    if(Verb!=TEXT("GET")&&!CanEdit())return;
    const FString Token=A->GetAccessTokenForAuthenticatedRequest(),Base=A->GetApiBaseUrlForAuthenticatedRequest();
    auto Pending=FHttpModule::Get().CreateRequest();Request=Pending;Pending->SetURL(Base+Path);Pending->SetVerb(Verb);Pending->SetTimeout(5);
    Pending->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Token);FProjectProject01VersionContract::ApplyToRequest(Pending);
    if(Body){FString Text;FJsonSerializer::Serialize(Body.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));Pending->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));Pending->SetContentAsString(Text);}
    Pending->OnProcessRequestComplete().BindWeakLambda(this,[this,Verb,Token,Base](FHttpRequestPtr Completed,FHttpResponsePtr Response,bool Connected)
    {
        if(Request!=Completed)return;Request.Reset();auto* Auth=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();
        if(!Auth||Auth->GetAccessTokenForAuthenticatedRequest()!=Token||Auth->GetApiBaseUrlForAuthenticatedRequest()!=Base)return;
        if(Response&&Response->GetResponseCode()==401){Auth->InvalidateSessionFromServerFailure(TEXT("로그인이 만료되었습니다. 다시 로그인하세요."));Close();return;}
        // Apply returns the current room, including up to 100 UTF-8 chat messages.
        TSharedPtr<FJsonObject> J;const bool Decoded=Response&&Response->GetContentLength()<=1024*1024&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),J)&&J;
        if(!Connected||!Response||!EHttpResponseCodes::IsOk(Response->GetResponseCode())||!Decoded)
        {Status=TEXT("요청에 실패했습니다.");if(Decoded)J->TryGetStringField(TEXT("message"),Status);return;}
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
{PresetCombo.Reset();NameInput.Reset();Super::ReleaseSlateResources(Children);}

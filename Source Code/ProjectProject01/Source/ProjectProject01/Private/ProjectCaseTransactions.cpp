#include "ProjectCaseRecords.h"
#include "ProjectCaseProfileSlate.h"
#include "ProjectProject01AuthSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/SecureHash.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
    const TCHAR* Section=TEXT("ProjectDFSD.CaseConfirmations");
    TSharedRef<STextBlock> Text(const FString& Value,int32 Size=18)
    {return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(false).WrapTextAt(400).Justification(ETextJustify::Center);}
    TSharedRef<SButton> Button(const FString& Value,TFunction<void()> Action)
    {return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ContentPadding(FMargin(16,9)).HAlign(HAlign_Center).OnClicked_Lambda([Action=MoveTemp(Action)]{Action();return FReply::Handled();})[Text(Value)];}
    TSharedRef<SWidget> Modal(TSharedRef<SWidget> Content)
    {return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(0,0,0,.72f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
        .OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})
        [SNew(SBox).WidthOverride(460)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Canvas()).Padding(28)[Content]]];}
    UProjectCaseRecords* Records(const UUserWidget* W){const auto* GI=W->GetGameInstance();return GI?GI->GetSubsystem<UProjectCaseRecords>():nullptr;}
}
FString UProjectCaseRecords::ConfirmationScope()const
{
    const auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();FString Id;
    if(!A||!A->IsSignedIn()||!Snapshot||!Snapshot->TryGetStringField(TEXT("userId"),Id)||Id.IsEmpty())return FString();
    return FMD5::HashAnsiString(*(A->GetApiBaseUrlForAuthenticatedRequest()+TEXT("|")+Id));
}
bool UProjectCaseRecords::ShouldConfirm(const FString& Category)const
{
    const FString Scope=ConfirmationScope();if(Scope.IsEmpty()||(Category!=TEXT("Draw")&&Category!=TEXT("Purchase")&&Category!=TEXT("MissionReroll")&&Category!=TEXT("CosmeticApply")))return true;
    const FString Key=Scope+TEXT(".")+Category;FString Day;
    if(const auto* Saved=ConfirmationDays.Find(Key))Day=*Saved;
    else if(bPersistence&&GConfig)GConfig->GetString(Section,*Key,Day,GGameUserSettingsIni);
    return Day!=FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
}
void UProjectCaseRecords::SkipConfirmationToday(const FString& Category)
{
    const FString Scope=ConfirmationScope();if(Scope.IsEmpty()||(Category!=TEXT("Draw")&&Category!=TEXT("Purchase")&&Category!=TEXT("MissionReroll")&&Category!=TEXT("CosmeticApply")))return;
    const FString Key=Scope+TEXT(".")+Category,Day=FDateTime::Now().ToString(TEXT("%Y-%m-%d"));ConfirmationDays.Add(Key,Day);
    if(bPersistence&&GConfig){GConfig->SetString(Section,*Key,*Day,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);}
}
void UProjectCaseRecordWidget::ConfirmAction(const FString& Category,const FString& Title,TFunction<void()> Action)
{
    auto* R=Records(this);if(!R||!R->IsAuthenticated()||R->IsWriting()||!TransactionLayer||TransactionLayer->GetChildren()->Num()>0)return;
    if(!R->ShouldConfirm(Category)){Action();return;}
    ConfirmationCategory=Category;ConfirmationAccount=R->ConfirmationScope();
    ConfirmationGeneration=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>()->GetSessionGeneration();ConfirmedAction=MoveTemp(Action);
    auto Check=SNew(SCheckBox).Style(&FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("Checkbox")))[Text(TEXT("오늘 하루 다시 묻지 않기"),16)];
    auto Yes=Button(TEXT("예"),[this,Check]{ResolveConfirmation(true,Check->IsChecked());});
    auto No=Button(TEXT("아니오"),[this,Check]{ResolveConfirmation(false,Check->IsChecked());});
    TransactionLayer->AddSlot()[Modal(SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,22)[Text(Title,23)]
        +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,22)[Check]
        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)[Yes]+SHorizontalBox::Slot().FillWidth(1)[No]])];
    FSlateApplication::Get().SetKeyboardFocus(No);
}
void UProjectCaseRecordWidget::ResolveConfirmation(bool Accept,bool SkipToday)
{
    auto Action=MoveTemp(ConfirmedAction);if(TransactionLayer)TransactionLayer->ClearChildren();
    auto* R=Records(this);auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();
    if(!Action||!R||!A||!R->IsAuthenticated()||R->ConfirmationScope()!=ConfirmationAccount||A->GetSessionGeneration()!=ConfirmationGeneration)return;
    if(SkipToday)R->SkipConfirmationToday(ConfirmationCategory);if(Accept&&!R->IsWriting())Action();
}
void UProjectCaseRecordWidget::RequestPurchase(const FString& Id)
{ConfirmAction(TEXT("Purchase"),TEXT("구매하시겠습니까?"),[this,Id]{if(auto* R=Records(this))R->Action(TEXT("Buy"),Id);});}
void UProjectCaseRecordWidget::RequestCosmeticAction(const FString& Action,const FString& Id)
{
    if(Action!=TEXT("Equip")&&Action!=TEXT("ShowBanner")&&Action!=TEXT("HideBanner"))return;
    ConfirmAction(TEXT("CosmeticApply"),TEXT("적용하시겠습니까?"),[this,Action,Id]{if(auto* R=Records(this))R->Action(Action,Id);});
}
void UProjectCaseRecordWidget::RequestMissionReroll(const FString& Id,const FString& Mode,int32 Count,const FString& Period)
{
    ConfirmAction(TEXT("MissionReroll"),TEXT("변경하시겠습니까?"),[this,Id,Mode,Count,Period]
    {
        auto* R=Records(this);if(!R)return;const TArray<TSharedPtr<FJsonValue>>* Missions=nullptr;
        if(R->Snapshot&&R->Snapshot->TryGetArrayField(TEXT("missions"),Missions))for(const auto& Value:*Missions)
        {
            const auto Current=Value->AsObject();if(!Current)continue;FString CurrentId,CurrentMode,CurrentPeriod;double Rerolls=0;bool CanReroll=false;
            Current->TryGetStringField(TEXT("id"),CurrentId);Current->TryGetStringField(TEXT("mode"),CurrentMode);Current->TryGetStringField(TEXT("period"),CurrentPeriod);
            Current->TryGetNumberField(TEXT("rerolls"),Rerolls);Current->TryGetBoolField(TEXT("canReroll"),CanReroll);
            if(CurrentId==Id&&CurrentMode==Mode&&CurrentPeriod==Period&&int32(Rerolls)==Count&&CanReroll){R->Action(TEXT("RerollMission"),Id,Mode,Count);return;}
        }
        R->Status=TEXT("미션이 변경되었습니다. 다시 확인하세요.");R->OnChanged.Broadcast();
    });
}
void UProjectCaseRecordWidget::ShowReceipt()
{
    auto* R=Records(this);if(!R||!R->IsAuthenticated()||!R->LastReceipt||!TransactionLayer)return;
    ConfirmedAction=nullptr;TransactionLayer->ClearChildren();const auto Receipt=R->LastReceipt;FString Title;Receipt->TryGetStringField(TEXT("title"),Title);
    auto Content=SNew(SVerticalBox);Content->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,18)[Text(Title,25)];
    const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
    if(Receipt->TryGetArrayField(TEXT("items"),Items))for(int32 I=0;I<FMath::Min(Items->Num(),8);++I)
    {
        const auto Item=(*Items)[I]->AsObject();if(!Item)continue;FString Id,Name;double Count=1;Item->TryGetStringField(TEXT("itemId"),Id);Item->TryGetStringField(TEXT("name"),Name);Item->TryGetNumberField(TEXT("count"),Count);
        if(!Id.IsEmpty())Content->AddSlot().AutoHeight().Padding(0,8)[SNew(ProjectCaseProfile::SEmblem).ItemId(Id).Height(96)];
        Content->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0,8)[Text(Name+(Count>1?FString::Printf(TEXT(" × %.0f"),Count):FString()),20)];
    }
    auto Confirm=Button(TEXT("확인"),[this]{if(TransactionLayer)TransactionLayer->ClearChildren();if(auto* R=Records(this))R->LastReceipt.Reset();});
    Content->AddSlot().AutoHeight().Padding(0,20,0,0)[Confirm];TransactionLayer->AddSlot()[Modal(Content)];FSlateApplication::Get().SetKeyboardFocus(Confirm);
}

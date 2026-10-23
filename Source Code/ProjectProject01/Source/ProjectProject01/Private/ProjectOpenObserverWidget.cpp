#include "ProjectOpenObserver.h"
#include "ProjectDFSDUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CheckBox.h"
#include "Components/SizeBox.h"

namespace
{
    UTextBlock* Label(UWidgetTree* Tree,const FString& Value)
    {
        auto* Text=Tree->ConstructWidget<UTextBlock>();Text->SetText(FText::FromString(Value));Text->SetAutoWrapText(true);Text->SetWrapTextAt(280);
        auto Font=Text->GetFont();Font.Size=18;Text->SetFont(Font);Text->SetColorAndOpacity(ProjectDFSDUI::Text());return Text;
    }
    UButton* Button(UWidgetTree* Tree,UVerticalBox* Panel,const FString& Text,UTextBlock** OutLabel=nullptr)
    {
        auto* Item=Tree->ConstructWidget<UButton>();ProjectDFSDUI::Apply(Item);auto* Caption=Label(Tree,Text);Item->AddChild(Caption);
        Panel->AddChildToVerticalBox(Item)->SetPadding(FMargin(4));if(OutLabel)*OutLabel=Caption;return Item;
    }
}
bool UProjectOpenObserverWidget::Initialize()
{
    if(!Super::Initialize())return false;
    auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
    auto Panel=[&](bool Right)
    {
        auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrushColor(ProjectDFSDUI::Panel());Border->SetPadding(FMargin(10));
        auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Border->AddChild(Box);
        auto* Frame=WidgetTree->ConstructWidget<USizeBox>();Frame->SetWidthOverride(340);Frame->AddChild(Border);
        auto* Slot=Canvas->AddChildToCanvas(Frame);Slot->SetAnchors(Right?FAnchors(1,.5):FAnchors(0,0));
        Slot->SetAlignment(Right?FVector2D(1,.5):FVector2D::ZeroVector);Slot->SetPosition(Right?FVector2D(-20,0):FVector2D(20,20));
        Slot->SetSize(FVector2D(340,0));Slot->SetAutoSize(true);Box->SetVisibility(ESlateVisibility::Visible);return Box;
    };
    auto* ViewPanel=Panel(false);auto* Fold=Button(WidgetTree,ViewPanel,TEXT("관전 시점 — 펼치기 / 접기"));Fold->OnClicked.AddUniqueDynamic(this,&ThisClass::FoldViews);
    Views=WidgetTree->ConstructWidget<UVerticalBox>();ViewPanel->AddChildToVerticalBox(Views);
    auto* All=Button(WidgetTree,Views,TEXT("전체 — 자유 비행"));auto* Binding=NewObject<UProjectObserverTargetBinding>(this);Binding->Owner=this;Bindings.Add(Binding);All->OnClicked.AddUniqueDynamic(Binding,&UProjectObserverTargetBinding::Click);
    Views->AddChildToVerticalBox(Label(WidgetTree,TEXT("WASD 이동 · Space/Ctrl 상승/하강\n우클릭을 누른 채 시점 회전")));
    Lock=WidgetTree->ConstructWidget<UCheckBox>();Lock->AddChild(Label(WidgetTree,TEXT("선택한 사람의 시선까지 함께 보기")));Views->AddChildToVerticalBox(Lock);Lock->SetIsEnabled(false);Lock->OnCheckStateChanged.AddUniqueDynamic(this,&ThisClass::LockChanged);
    auto* MatchPanel=Panel(true);auto* MatchFold=Button(WidgetTree,MatchPanel,TEXT("경기 상황 — 펼치기 / 접기"));MatchFold->OnClicked.AddUniqueDynamic(this,&ThisClass::FoldProgress);
    Progress=Label(WidgetTree,TEXT("경기를 준비 중입니다."));MatchPanel->AddChildToVerticalBox(Progress);
    ProjectDFSDUI::ApplyTree(WidgetTree);return true;
}
void UProjectObserverTargetBinding::Click(){if(Owner)Owner->Choose(Id);}
void UProjectOpenObserverWidget::Choose(const FString& Id)
{
    Selected=Id;Lock->SetIsEnabled(!Id.IsEmpty());
    if(auto* PC=Cast<AProjectOpenObserverController>(GetOwningPlayer()))PC->Select(Id,Lock->IsChecked());
}
void UProjectOpenObserverWidget::LockChanged(bool){Choose(Selected);}
void UProjectOpenObserverWidget::FoldViews(){Views->SetVisibility(Views->GetVisibility()==ESlateVisibility::Collapsed?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
void UProjectOpenObserverWidget::FoldProgress(){Progress->SetVisibility(Progress->GetVisibility()==ESlateVisibility::Collapsed?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
void UProjectOpenObserverWidget::Refresh(const TArray<FProjectOpenObserverEntry>& Entries,const FString& Summary)
{
    TArray<FString> NewKeys;for(const auto& Entry:Entries)NewKeys.Add(Entry.PlayerId);
    if(NewKeys!=Keys)
    {
        if(!Selected.IsEmpty()&&!NewKeys.Contains(Selected))Choose(FString());
        for(const auto& Name:Names)if(Name&&Name->GetParent())Name->GetParent()->RemoveFromParent();
        Names.Reset();Bindings.SetNum(FMath::Min(1,Bindings.Num()));Keys=MoveTemp(NewKeys);
        for(const auto& Entry:Entries)
        {
            UTextBlock* Caption=nullptr;auto* Item=Button(WidgetTree,Views,Entry.Name,&Caption);Names.Add(Caption);
            auto* Binding=NewObject<UProjectObserverTargetBinding>(this);Binding->Owner=this;Binding->Id=Entry.PlayerId;Bindings.Add(Binding);Item->OnClicked.AddUniqueDynamic(Binding,&UProjectObserverTargetBinding::Click);
        }
    }
    for(int32 I=0;I<Entries.Num()&&I<Names.Num();++I)
        Names[I]->SetText(FText::FromString((Entries[I].PlayerId==Selected?TEXT("● "):TEXT(""))+Entries[I].Name+TEXT(" — ")+Entries[I].State));
    Progress->SetText(FText::FromString(Summary));
}

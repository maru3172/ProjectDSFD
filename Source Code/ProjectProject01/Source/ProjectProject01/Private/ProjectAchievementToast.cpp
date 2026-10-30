#include "ProjectAchievementToast.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"

namespace
{
struct FAchievementToastEntry{FString Code,Title,Condition;bool bQuest=false;};
bool Describe(const FString& Code,FAchievementToastEntry& E)
{
    E.Code=Code;
    if(Code==TEXT("campaign_checkpoint")){E.Title=TEXT("첫 기록");E.Condition=TEXT("싱글플레이 체크포인트 저장");}
    else if(Code==TEXT("campaign_escape")){E.Title=TEXT("출구를 찾다");E.Condition=TEXT("싱글플레이 탈출 성공");}
    else if(Code==TEXT("campaign_complete")){E.Title=TEXT("사건의 끝");E.Condition=TEXT("싱글 캠페인 완주");}
    else if(Code==TEXT("pvp_first")){E.Title=TEXT("첫 PvP 경기");E.Condition=TEXT("PvP 경기 1회 완료");}
    else if(Code==TEXT("bot_first")){E.Title=TEXT("첫 봇전 경기");E.Condition=TEXT("봇전 경기 1회 완료");}
    else if(Code==TEXT("survivor_escape")){E.Title=TEXT("생존 기록");E.Condition=TEXT("생존자로 탈출 성공");}
    else if(Code==TEXT("mannequin_victory")){E.Title=TEXT("추적 기록");E.Condition=TEXT("마네킹으로 경기 승리");}
    else return false;
    return true;
}
class SProjectAchievementToast final:public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectAchievementToast){}SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        SetCanTick(true);ForceVolatile(true);SetVisibility(EVisibility::Collapsed);
        static FSlateColorBrush Back(FLinearColor(.018f,.022f,.028f,.96f));
        ChildSlot[SNew(SBorder).BorderImage(&Back).Padding(16)
          [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SAssignNew(Title,STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18)).ColorAndOpacity(FLinearColor(.95f,.89f,.73f)).AutoWrapText(false).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
            +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[SAssignNew(Condition,STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15)).ColorAndOpacity(FLinearColor(.85f,.87f,.88f)).AutoWrapText(false).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]];
    }
    void Show(const FString& Code)
    {
        FAchievementToastEntry E;if(!Describe(Code,E)||ActiveCode==Code||Queue.ContainsByPredicate([&](const auto& Q){return Q.Code==Code;}))return;
        if(Queue.Num()<24)Queue.Add(MoveTemp(E));if(ActiveCode.IsEmpty()&&GapRemaining<=0)Next();
    }
    void ShowQuest(const FString& Name,const FString& Kind,const FString& Detail,const FString& EventId)
    {
        if(Kind!=TEXT("Condition")&&Kind!=TEXT("Stage")&&Kind!=TEXT("Complete"))return;
        FAchievementToastEntry E;E.Code=TEXT("Quest:")+EventId;E.bQuest=true;
        if(ActiveCode==E.Code||Queue.ContainsByPredicate([&](const auto& Q){return Q.Code==E.Code;}))return;
        E.Title=TEXT("- ")+Name+(Kind==TEXT("Condition")?TEXT(" 퀘스트 조건 완료 -"):Kind==TEXT("Stage")?TEXT(" 퀘스트 단계 완료 -"):TEXT(" 퀘스트 완료 -"));E.Condition=Detail;
        if(Queue.Num()<24)Queue.Add(MoveTemp(E));if(ActiveCode.IsEmpty()&&GapRemaining<=0)Next();
    }
    void Next()
    {
        if(Queue.IsEmpty()){ActiveCode.Reset();SetVisibility(EVisibility::Collapsed);return;}
        auto E=Queue[0];Queue.RemoveAt(0);ActiveCode=E.Code;bQuest=E.bQuest;Title->SetText(FText::FromString(E.bQuest?E.Title:TEXT("업적·도전과제 달성 · ")+E.Title));Condition->SetText(FText::FromString(E.Condition));Elapsed=0;ChildSlot.GetWidget()->SetRenderOpacity(0);SetVisibility(EVisibility::HitTestInvisible);
    }
    virtual void Tick(const FGeometry& G,double Time,float Delta)override
    {
        SCompoundWidget::Tick(G,Time,Delta);
        if(GapRemaining>0){GapRemaining-=FMath::Clamp(Delta,0.f,.1f);if(GapRemaining<=0)Next();return;}
        if(ActiveCode.IsEmpty())return;Elapsed+=FMath::Clamp(Delta,0.f,.1f);
        const float Alpha=Elapsed<.35f?FMath::SmoothStep(0.f,.35f,Elapsed):1-FMath::SmoothStep(3.85f,4.2f,Elapsed);
        ChildSlot.GetWidget()->SetRenderOpacity(Alpha);if(Elapsed>=4.2f){if(Queue.IsEmpty())Next();else{ActiveCode.Reset();GapRemaining=.5f;ChildSlot.GetWidget()->SetRenderOpacity(0);}}
    }
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(bQuest?480:380,110);}
    virtual void OnArrangeChildren(const FGeometry& G,FArrangedChildren& Children)const override
    {
        const float Width=FMath::Min(bQuest?480.f:380.f,FMath::Max(1.f,float(G.GetLocalSize().X)-32.f));
        const float Hide=Elapsed<.35f?1-FMath::SmoothStep(0.f,.35f,Elapsed):FMath::SmoothStep(3.85f,4.2f,Elapsed);
        Children.AddWidget(G.MakeChild(ChildSlot.GetWidget(),FVector2D(Width,110),FSlateLayoutTransform(FVector2D(G.GetLocalSize().X-Width-20,G.GetLocalSize().Y-130+Hide*130))));
    }
private:
    TSharedPtr<STextBlock> Title,Condition;TArray<FAchievementToastEntry> Queue;FString ActiveCode;float Elapsed=0,GapRemaining=0;bool bQuest=false;
};
}
TSharedRef<SWidget> ProjectAchievementToast::Create(){return SNew(SProjectAchievementToast);}
void ProjectAchievementToast::Show(TSharedRef<SWidget> Layer,const FString& Code){StaticCastSharedRef<SProjectAchievementToast>(Layer)->Show(Code);}
void ProjectAchievementToast::ShowQuest(TSharedRef<SWidget> Layer,const FString& Name,const FString& Kind,const FString& Detail,const FString& EventId){StaticCastSharedRef<SProjectAchievementToast>(Layer)->ShowQuest(Name,Kind,Detail,EventId);}

#if WITH_DEV_AUTOMATION_TESTS
#include "ProjectProject01LoginWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Widgets/SOverlay.h"
#include "Misc/AutomationTest.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectAchievementToastTest,"ProjectProject01.CaseRecords.AchievementToast",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectAchievementToastTest::RunTest(const FString&)
{
    auto Toast=SNew(SProjectAchievementToast);Toast->Show(TEXT("campaign_checkpoint"));Toast->Show(TEXT("campaign_checkpoint"));
    TestFalse(TEXT("Achievement overlay never takes mouse input"),Toast->GetVisibility().IsHitTestVisible());
    for(int32 I=0;I<8;++I)Toast->Tick(FGeometry(),0,.1f);
    auto* Lobby=NewObject<UProjectProject01LobbyWidget>();static_cast<UUserWidget*>(Lobby)->Initialize();auto Scene=SNew(SOverlay)+SOverlay::Slot()[Lobby->GetRootWidget()->TakeWidget()]+SOverlay::Slot()[Toast];
    FWidgetRenderer Renderer(false);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1280,720),TF_Bilinear,false);Renderer.DrawWidget(Target,Scene,FVector2D(1280,720),0);FlushRenderingCommands();
    TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);TestTrue(TEXT("Achievement notification renders"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
    TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(1280,720,Pixels,Png);const FString Dir=FPaths::ProjectSavedDir()/TEXT("FeatureChecks/AchievementToast");IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveArrayToFile(Png,*(Dir/TEXT("Earned.png")));
    bool TitleFound=false,ConditionFound=false;TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> W){if(W->GetType()==FName(TEXT("STextBlock"))){const auto Text=StaticCastSharedRef<STextBlock>(W)->GetText().ToString();TitleFound|=Text==TEXT("업적·도전과제 달성 · 첫 기록");ConditionFound|=Text==TEXT("싱글플레이 체크포인트 저장");}if(auto* C=W->GetChildren())for(int32 I=0;I<C->Num();++I)Inspect(C->GetChildAt(I));};Inspect(Toast);
    TestTrue(TEXT("Toast names the earned achievement and its condition"),TitleFound&&ConditionFound);
    for(int32 I=0;I<32;++I)Toast->Tick(FGeometry(),0,.1f);
    auto Capture=[&](const FString& Name){Renderer.DrawWidget(Target,Scene,FVector2D(1280,720),0);FlushRenderingCommands();TArray<FColor> Frame;Target->GameThread_GetRenderTargetResource()->ReadPixels(Frame,Flags);TArray64<uint8> Bytes;FImageUtils::PNGCompressImageArray(1280,720,Frame,Bytes);FFileHelper::SaveArrayToFile(Bytes,*(Dir/(Name+TEXT(".png"))));};Capture(TEXT("Leaving"));
    for(int32 I=0;I<15;++I)Toast->Tick(FGeometry(),0,.1f);Capture(TEXT("Removed"));
    TestEqual(TEXT("Toast slides down and finishes; duplicate notifications are not queued"),Toast->GetVisibility(),EVisibility::Collapsed);
    Toast->ShowQuest(TEXT("탐사"),TEXT("Condition"),TEXT("경기 1회 플레이"),TEXT("test:1"));
    Toast->ShowQuest(TEXT("탐사"),TEXT("Stage"),TEXT("마네킹으로 승리하기"),TEXT("test:2"));
    Toast->ShowQuest(TEXT("탐사"),TEXT("Complete"),TEXT("마네킹으로 승리하기"),TEXT("test:3"));
    Toast->ShowQuest(TEXT("탐사"),TEXT("Complete"),TEXT("마네킹으로 승리하기"),TEXT("test:3"));
    TArray<FString> Order;int32 EmptyFrames=0;bool GapObserved=false,GoalDescribed=false;
    for(int32 I=0;I<180;++I)
    {
        Toast->Tick(FGeometry(),0,.1f);TArray<FString> Captions;
        TFunction<void(TSharedRef<SWidget>)> Read=[&](TSharedRef<SWidget> W){if(W->GetType()==FName(TEXT("STextBlock")))Captions.Add(StaticCastSharedRef<STextBlock>(W)->GetText().ToString());if(auto* C=W->GetChildren())for(int32 K=0;K<C->Num();++K)Read(C->GetChildAt(K));};Read(Toast);
        const float Opacity=Toast->GetChildren()->GetChildAt(0)->GetRenderOpacity();
        if(Opacity<.001f){++EmptyFrames;continue;}
        if(Opacity>.99f&&Captions.Num()>=2&&(Order.IsEmpty()||Order.Last()!=Captions[0]))
        {
            if(!Order.IsEmpty())GapObserved|=EmptyFrames>=3;
            Order.Add(Captions[0]);GoalDescribed|=Captions[1]==TEXT("경기 1회 플레이");
            Capture(FString::Printf(TEXT("QuestNotice%d"),Order.Num()));EmptyFrames=0;
        }
    }
    TestTrue(TEXT("Quest notices are condition, stage, complete in order without duplicate overlays"),Order==TArray<FString>{TEXT("- 탐사 퀘스트 조건 완료 -"),TEXT("- 탐사 퀘스트 단계 완료 -"),TEXT("- 탐사 퀘스트 완료 -")});
    TestTrue(TEXT("Condition notice names the fulfilled requirement and queued notices have a visible gap"),GoalDescribed&&GapObserved);
    TestEqual(TEXT("Quest notices leave the screen after acknowledgement time"),Toast->GetVisibility(),EVisibility::Collapsed);
    Target->ReleaseResource();Lobby->ReleaseSlateResources(true);
    return true;
}
#endif

#include "ProjectAnnouncementSubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SLeafWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Misc/App.h"
#include "ProjectAchievementToast.h"

namespace ProjectAnnouncement
{
    FString SingleLine(FString Text)
    {Text.ReplaceInline(TEXT("\r\n"),TEXT(" "));Text.ReplaceInline(TEXT("\r"),TEXT(" "));Text.ReplaceInline(TEXT("\n"),TEXT(" "));Text.ReplaceInline(TEXT("\t"),TEXT(" "));return Text.TrimStartAndEnd();}
    struct FPhase{float Alpha=0,X=0;bool Text=false,Finished=false;};
    constexpr float Fade=.5f,FadeOut=1.5f,Speed=115.f;
    FPhase Phase(float Time,float Width,float TextWidth)
    {
        FPhase P;const float Travel=(Width+TextWidth)/Speed,End=Fade+Travel;
        if(Time<Fade){P.Alpha=FMath::SmoothStep(0.f,Fade,Time);return P;}
        P.Text=Time<End;P.X=-TextWidth+FMath::Clamp(Time-Fade,0.f,Travel)*Speed;
        P.Alpha=1-FMath::SmoothStep(Fade+FMath::Max(0.f,Travel-FadeOut),End,Time);
        P.Finished=Time>=End;return P;
    }
}
class SProjectAnnouncementTicker final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectAnnouncementTicker){} SLATE_END_ARGS()
    void Construct(const FArguments&){SetCanTick(true);ForceVolatile(true);SetVisibility(EVisibility::Collapsed);}
    void ResetForHost(){Current.Reset();Pending.Reset();Tag.Reset();PendingTag.Reset();Remaining=0;Deadline=-1;Elapsed=0;Active=false;SetVisibility(EVisibility::Collapsed);}
    void SetRemainingLifetime(double Seconds){Deadline=Seconds>=0?FPlatformTime::Seconds()+Seconds:-1;}
    void Publish(const FString& Notice,const FString& Label=FString(),int32 Repeats=1)
    {
        const FString Clean=ProjectAnnouncement::SingleLine(Notice);
        if(Clean.IsEmpty()){ResetForHost();return;}
        if(Active){Pending=Clean;PendingTag=Label;PendingRepeats=Repeats;return;}
        Current=Clean;Tag=ProjectAnnouncement::SingleLine(Label);Remaining=FMath::Clamp(Repeats,1,100);Elapsed=0;Active=true;SetVisibility(EVisibility::HitTestInvisible);
    }
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(1280,720);}
    virtual void Tick(const FGeometry& G,double Time,float Delta)override
    {
        SLeafWidget::Tick(G,Time,Delta);if(!Active)return;if(Deadline>=0&&FPlatformTime::Seconds()>=Deadline){ResetForHost();return;}Elapsed+=FMath::Clamp(Delta,0.f,.1f);
        if(ProjectAnnouncement::Phase(Elapsed,BandWidth,TextWidth).Finished)
        {
            if(--Remaining>0){Elapsed=0;return;}
            Active=false;SetVisibility(EVisibility::Collapsed);
            if(!Pending.IsEmpty()){FString Next=MoveTemp(Pending),NextTag=MoveTemp(PendingTag);Pending.Reset();Publish(Next,NextTag,PendingRepeats);}
        }
    }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool)const override
    {
        if(!Active||!FSlateApplication::IsInitialized())return Layer;
        const auto Font=FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),20);
        const FVector2D TextSize=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Current,Font);
        BandWidth=FMath::Max(1.f,float(G.GetLocalSize().X)-64.f);TextWidth=TextSize.X;
        const auto Phase=ProjectAnnouncement::Phase(Elapsed,BandWidth,TextWidth);
        if(Phase.Alpha<=0)return Layer;
        const float Left=32,Top=FMath::Max(44.f,float(G.GetLocalSize().Y)*.075f),Height=Tag.IsEmpty()?60.f:78.f;
        const auto Tint=Style.GetColorAndOpacityTint();
        auto Gradient=[&](float Y,float H,FLinearColor Color)
        {
            Color*=Tint;Color.A*=Phase.Alpha;const float Edge=FMath::Min(85.f,BandWidth*.2f);
            TArray<FSlateGradientStop> Stops{{FVector2f(0,0),Color.CopyWithNewOpacity(0)},{FVector2f(Edge,0),Color},{FVector2f(BandWidth-Edge,0),Color},{FVector2f(BandWidth,0),Color.CopyWithNewOpacity(0)}};
            FSlateDrawElement::MakeGradient(Out,Layer,G.ToPaintGeometry(FVector2D(BandWidth,H),FSlateLayoutTransform(FVector2D(Left,Y))),MoveTemp(Stops),Orient_Vertical);
        };
        Gradient(Top,Height,FLinearColor(.018f,.024f,.029f,.88f));
        for(float Y:{Top,Top+Height})Gradient(Y,1,FLinearColor(.4f,.43f,.42f,.6f));
        if(!Tag.IsEmpty())
        {
            const auto TagFont=FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16);const auto TagSize=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Tag,TagFont);
            FSlateDrawElement::MakeText(Out,Layer+2,G.ToPaintGeometry(TagSize,FSlateLayoutTransform(FVector2D(Left+(BandWidth-TagSize.X)*.5f,Top+5))),Tag,TagFont,ESlateDrawEffect::None,FLinearColor(.9f,.85f,.75f,Phase.Alpha)*Tint);
        }
        if(Phase.Text)
        {
            const auto Band=G.MakeChild(FVector2D(BandWidth,Height),FSlateLayoutTransform(FVector2D(Left,Top)));
            Out.PushClip(FSlateClippingZone(Band.GetLayoutBoundingRect()));
            FSlateDrawElement::MakeText(Out,Layer+2,G.ToPaintGeometry(FVector2D(BandWidth,TextSize.Y),FSlateLayoutTransform(FVector2D(Left+Phase.X,Tag.IsEmpty()?Top+(Height-TextSize.Y)*.5f:Top+31))),Current,Font,ESlateDrawEffect::None,FLinearColor(.95f,.95f,.92f,Phase.Alpha)*Tint);
            Out.PopClip();
        }
        return Layer+2;
    }
private:
    friend class FProjectAnnouncementPresentationTest;
    FString Current,Pending,Tag,PendingTag;int32 Remaining=0,PendingRepeats=1;float Elapsed=0;bool Active=false;
    double Deadline=-1;
    mutable float BandWidth=1024,TextWidth=0;
};
bool UProjectAnnouncementSubsystem::ShouldCreateSubsystem(UObject* Outer)const{return !IsRunningDedicatedServer()&&!IsRunningCommandlet()&&Super::ShouldCreateSubsystem(Outer);}
void UProjectAnnouncementSubsystem::Initialize(FSubsystemCollectionBase& C)
{
    Super::Initialize(C);C.InitializeDependency<UProjectProject01AuthSubsystem>();
    if(FApp::IsUnattended())return;
    if(auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>())A->OnServiceStatusChanged.AddUniqueDynamic(this,&ThisClass::ServiceChanged);
    PollHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::Poll),.5f);
}
void UProjectAnnouncementSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);PollHandle.Reset();
    if(auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>())A->OnServiceStatusChanged.RemoveDynamic(this,&ThisClass::ServiceChanged);
    if(Viewport.IsValid()&&Overlay)Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
    if(Viewport.IsValid()&&AchievementOverlay)Viewport->RemoveViewportWidgetContent(AchievementOverlay.ToSharedRef());
    Overlay.Reset();AchievementOverlay.Reset();Viewport.Reset();Super::Deinitialize();
}
void UProjectAnnouncementSubsystem::NotifyAchievement(const FString& Code)
{
    if(!AchievementOverlay)AchievementOverlay=ProjectAchievementToast::Create();ProjectAchievementToast::Show(AchievementOverlay.ToSharedRef(),Code);
}
bool UProjectAnnouncementSubsystem::Poll(float)
{
    auto* V=GetGameInstance()->GetGameViewportClient();if(!V)return true;
    if(Viewport.Get()!=V)
    {
        if(Viewport.IsValid()&&Overlay)Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
        if(Viewport.IsValid()&&AchievementOverlay)Viewport->RemoveViewportWidgetContent(AchievementOverlay.ToSharedRef());
        if(!Overlay)Overlay=SNew(SProjectAnnouncementTicker);
        if(!AchievementOverlay)AchievementOverlay=ProjectAchievementToast::Create();
        V->AddViewportWidgetContent(Overlay.ToSharedRef(),1000002);Viewport=V;
        V->AddViewportWidgetContent(AchievementOverlay.ToSharedRef(),1000003);
    }
    if(FPlatformTime::Seconds()>=NextPoll){NextPoll=FPlatformTime::Seconds()+5;if(auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>())A->CheckMultiplayerServiceStatus();}
    return true;
}
void UProjectAnnouncementSubsystem::ServiceChanged(bool,const FString& Notice,const FString&,const FString&)
{
    auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();if(!A)return;
    if(!A->IsMultiplayerServiceReachable()){bAwaitBroadcastBaseline=true;return;}
    const FString Base=A->GetApiBaseUrlForAuthenticatedRequest(),Broadcast=A->GetAnnouncementBroadcastId();
    // The first successful status establishes a baseline. Joining or changing servers never replays an old broadcast.
    const bool Active=A->IsOperatorAnnouncementActive();
    if(bAwaitBroadcastBaseline||SeenBase!=Base){bAwaitBroadcastBaseline=false;SeenBase=Base;SeenBroadcast=Active?Broadcast:FString();if(Overlay)Overlay->ResetForHost();return;}
    if(!Active){if(Overlay)Overlay->ResetForHost();return;}
    if(SeenBroadcast==Broadcast){if(Overlay)Overlay->SetRemainingLifetime(A->GetAnnouncementRemainingSeconds());return;}SeenBroadcast=Broadcast;
    if(!Overlay)Overlay=SNew(SProjectAnnouncementTicker);
    Overlay->ResetForHost();Overlay->Publish(A->GetOperatorAnnouncement(),A->GetAnnouncementTag(),A->GetAnnouncementRepeatCount());Overlay->SetRemainingLifetime(A->GetAnnouncementRemainingSeconds());
}

#if WITH_DEV_AUTOMATION_TESTS
#include "ProjectProject01LoginWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Widgets/SOverlay.h"
#include "Misc/AutomationTest.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectAnnouncementPresentationTest,"ProjectProject01.Operations.AnnouncementPresentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectAnnouncementPresentationTest::RunTest(const FString&)
{
    using namespace ProjectAnnouncement;
    TestEqual(TEXT("Only line endings become spaces; existing spacing is preserved"),SingleLine(TEXT("첫 줄\r\n두  칸\n마지막")),FString(TEXT("첫 줄 두  칸 마지막")));
    TestFalse(TEXT("No text while the band appears"),Phase(.25f,1000,400).Text);
    const auto Moving=Phase(1,1000,400);TestTrue(TEXT("Text enters from the left after the band appears"),Moving.Text&&Moving.Alpha==1&&Moving.X>-400&&Moving.X<0);
    TestTrue(TEXT("Text keeps moving to the right"),Phase(2,1000,400).X>Moving.X);
    const float Exit=Fade+1400/Speed;const auto Closing=Phase(Exit-FadeOut*.5f,1000,400);
    TestTrue(TEXT("Text and band slowly fade together near the right edge"),Closing.Text&&Closing.X>800&&Closing.Alpha>0&&Closing.Alpha<1);
    TestTrue(TEXT("The banner finishes only after the text reaches the right exit"),Phase(Exit,1000,400).Finished&&!Phase(Exit,1000,400).Text&&Phase(Exit,1000,400).Alpha==0);
    auto Widget=SNew(SProjectAnnouncementTicker);Widget->Publish(TEXT("서버 안내 공지입니다.\n줄바꿈은 공백으로 이어집니다."),TEXT("-긴급 공지-"),2);
    FWidgetRenderer Renderer(false);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1280,720),TF_Bilinear,false);
    Renderer.DrawWidget(Target,Widget,FVector2D(1280,720),0);
    const float Mid=Fade+(Widget->TextWidth+1216)*.5f/Speed,End=Fade+(Widget->TextWidth+1216)/Speed;
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("FeatureChecks/AnnouncementUI");IFileManager::Get().MakeDirectory(*Directory,true);
    for(const auto& Sample:TArray<TPair<FString,float>>{{TEXT("Appearing"),.25f},{TEXT("Moving"),Mid},{TEXT("Disappearing"),End-FadeOut*.5f}})
    {
        Widget->Elapsed=Sample.Value;Renderer.DrawWidget(Target,Widget,FVector2D(1280,720),0);FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);
        TestTrue(TEXT("Announcement band renders"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(1280,720,Pixels,Png);FFileHelper::SaveArrayToFile(Png,*(Directory/(Sample.Key+TEXT(".png"))));
        if(Sample.Key==TEXT("Moving"))
        {TestTrue(TEXT("The lowered band is visible independently of the text"),Pixels[60*1280+500].A>64);TestTrue(TEXT("The horizontal edges are feathered while the center stays visible"),Pixels[60*1280+33].A<Pixels[60*1280+500].A);}
    }
    auto* Lobby=NewObject<UProjectProject01LobbyWidget>();static_cast<UUserWidget*>(Lobby)->Initialize();
    auto Scene=SNew(SOverlay)+SOverlay::Slot()[Lobby->GetRootWidget()->TakeWidget()]+SOverlay::Slot()[Widget];
    for(const bool Shown:{true,false})
    {
        if(Shown)Widget->Elapsed=Mid;else Widget->ResetForHost();
        Renderer.DrawWidget(Target,Scene,FVector2D(1280,720),0);FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(1280,720,Pixels,Png);FFileHelper::SaveArrayToFile(Png,*(Directory/(Shown?TEXT("InLobby-Visible.png"):TEXT("InLobby-Removed.png"))));
    }
    Lobby->ReleaseSlateResources(true);Target->ReleaseResource();
    Widget->Publish(TEXT("반복 횟수 확인"),FString(),2);
    Widget->Elapsed=End+Fade;Widget->Tick(FGeometry(),0,0);TestTrue(TEXT("First pass retains exactly one remaining repetition"),Widget->Active&&Widget->Remaining==1);
    Widget->Elapsed=End+Fade;Widget->Tick(FGeometry(),0,0);TestFalse(TEXT("Selected repetition count finishes without an infinite loop"),Widget->Active);
    Widget->Publish(TEXT("시간 제한 공지"),FString(),100);Widget->SetRemainingLifetime(0);Widget->Tick(FGeometry(),0,0);TestFalse(TEXT("End time stops remaining repetitions"),Widget->Active);
    return true;
}
#endif

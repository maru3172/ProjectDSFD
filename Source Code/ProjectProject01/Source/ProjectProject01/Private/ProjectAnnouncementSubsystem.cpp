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

namespace ProjectAnnouncement
{
    FString SingleLine(FString Text)
    {Text.ReplaceInline(TEXT("\r\n"),TEXT(" "));Text.ReplaceInline(TEXT("\r"),TEXT(" "));Text.ReplaceInline(TEXT("\n"),TEXT(" "));Text.ReplaceInline(TEXT("\t"),TEXT(" "));return Text.TrimStartAndEnd();}
    struct FPhase{float Alpha=0,X=0;bool Text=false,Finished=false;};
    constexpr float Fade=.5f,Speed=70.f;
    FPhase Phase(float Time,float Width,float TextWidth)
    {
        FPhase P;const float Travel=(Width+TextWidth)/Speed;
        if(Time<Fade){P.Alpha=FMath::SmoothStep(0.f,Fade,Time);return P;}
        if(Time<Fade+Travel){P.Alpha=1;P.Text=true;P.X=-TextWidth+(Time-Fade)*Speed;return P;}
        P.Alpha=1-FMath::SmoothStep(0.f,Fade,Time-Fade-Travel);P.Finished=Time>=2*Fade+Travel;return P;
    }
}
class SProjectAnnouncementTicker final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectAnnouncementTicker){} SLATE_END_ARGS()
    void Construct(const FArguments&){SetCanTick(true);ForceVolatile(true);SetVisibility(EVisibility::Collapsed);}
    void ResetForHost(){Current.Reset();Pending.Reset();Elapsed=0;Active=false;SetVisibility(EVisibility::Collapsed);}
    void Publish(const FString& Notice)
    {
        const FString Clean=ProjectAnnouncement::SingleLine(Notice);
        if(Clean.IsEmpty()){Pending.Reset();return;}
        if(Active){Pending=Clean;return;}
        Current=TEXT("-공지- ")+Clean;Elapsed=0;Active=true;SetVisibility(EVisibility::HitTestInvisible);
    }
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(1280,720);}
    virtual void Tick(const FGeometry& G,double Time,float Delta)override
    {
        SLeafWidget::Tick(G,Time,Delta);if(!Active)return;Elapsed+=FMath::Clamp(Delta,0.f,.1f);
        if(ProjectAnnouncement::Phase(Elapsed,BandWidth,TextWidth).Finished)
        {
            Active=false;SetVisibility(EVisibility::Collapsed);
            if(!Pending.IsEmpty()){FString Next=MoveTemp(Pending);Pending.Reset();Publish(Next);}
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
        const float Left=32,Top=FMath::Max(20.f,float(G.GetLocalSize().Y)*.035f),Height=60;
        const auto Tint=Style.GetColorAndOpacityTint();
        auto Gradient=[&](float Y,float H,FLinearColor Color)
        {
            Color*=Tint;Color.A*=Phase.Alpha;const float Edge=FMath::Min(85.f,BandWidth*.2f);
            TArray<FSlateGradientStop> Stops{{FVector2f(0,0),Color.CopyWithNewOpacity(0)},{FVector2f(Edge,0),Color},{FVector2f(BandWidth-Edge,0),Color},{FVector2f(BandWidth,0),Color.CopyWithNewOpacity(0)}};
            FSlateDrawElement::MakeGradient(Out,Layer,G.ToPaintGeometry(FVector2D(BandWidth,H),FSlateLayoutTransform(FVector2D(Left,Y))),MoveTemp(Stops),Orient_Vertical);
        };
        Gradient(Top,Height,FLinearColor(.018f,.024f,.029f,.88f));
        for(float Y:{Top,Top+Height})Gradient(Y,1,FLinearColor(.4f,.43f,.42f,.6f));
        if(Phase.Text)
        {
            const auto Band=G.MakeChild(FVector2D(BandWidth,Height),FSlateLayoutTransform(FVector2D(Left,Top)));
            Out.PushClip(FSlateClippingZone(Band.GetLayoutBoundingRect()));
            FSlateDrawElement::MakeText(Out,Layer+2,G.ToPaintGeometry(FVector2D(BandWidth,TextSize.Y),FSlateLayoutTransform(FVector2D(Left+Phase.X,Top+(Height-TextSize.Y)*.5f))),Current,Font,ESlateDrawEffect::None,FLinearColor(.95f,.95f,.92f)*Tint);
            Out.PopClip();
        }
        return Layer+2;
    }
private:
    friend class FProjectAnnouncementPresentationTest;
    FString Current,Pending;float Elapsed=0;bool Active=false;
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
    Overlay.Reset();Viewport.Reset();Super::Deinitialize();
}
bool UProjectAnnouncementSubsystem::Poll(float)
{
    auto* V=GetGameInstance()->GetGameViewportClient();if(!V)return true;
    if(Viewport.Get()!=V)
    {
        if(Viewport.IsValid()&&Overlay)Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
        if(!Overlay)Overlay=SNew(SProjectAnnouncementTicker);
        V->AddViewportWidgetContent(Overlay.ToSharedRef(),1000002);Viewport=V;
    }
    if(FPlatformTime::Seconds()>=NextPoll){NextPoll=FPlatformTime::Seconds()+5;if(auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>())A->CheckMultiplayerServiceStatus();}
    return true;
}
void UProjectAnnouncementSubsystem::ServiceChanged(bool,const FString& Notice,const FString&,const FString&)
{
    auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();if(!A)return;
    const FString Base=A->GetApiBaseUrlForAuthenticatedRequest();const int64 Revision=A->GetServiceRevision();
    const FString Text=A->IsMultiplayerServiceReachable()?A->GetOperatorAnnouncement():FString();
    if(SeenBase==Base&&SeenRevision==Revision&&SeenText==Text)return;
    if(SeenBase!=Base&&Overlay)Overlay->ResetForHost();
    SeenBase=Base;SeenRevision=Revision;SeenText=Text;
    if(!Overlay)Overlay=SNew(SProjectAnnouncementTicker);
    Overlay->Publish(Text);
}

#if WITH_DEV_AUTOMATION_TESTS
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
    const auto Moving=Phase(1,1000,400);TestTrue(TEXT("Text starts only after the band is fully visible"),Moving.Text&&Moving.Alpha==1&&Moving.X>-400);
    const float Exit=Fade+1400/Speed;TestTrue(TEXT("Band begins to disappear after all text exits right"),Phase(Exit+.1f,1000,400).Alpha<1&&!Phase(Exit+.1f,1000,400).Text);
    TestTrue(TEXT("The banner finishes, it does not loop on every status poll"),Phase(Exit+Fade,1000,400).Finished);
    auto Widget=SNew(SProjectAnnouncementTicker);Widget->Publish(TEXT("서버 안내 공지입니다.\n줄바꿈은 공백으로 이어집니다."));
    FWidgetRenderer Renderer(false);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1280,720),TF_Bilinear,false);
    Renderer.DrawWidget(Target,Widget,FVector2D(1280,720),0);
    const float Mid=Fade+(Widget->TextWidth+120)/Speed,End=Fade+(Widget->TextWidth+1216)/Speed;
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("FeatureChecks/AnnouncementUI");IFileManager::Get().MakeDirectory(*Directory,true);
    for(const auto& Sample:TArray<TPair<FString,float>>{{TEXT("Appearing"),.25f},{TEXT("Moving"),Mid},{TEXT("Disappearing"),End+.25f}})
    {
        Widget->Elapsed=Sample.Value;Renderer.DrawWidget(Target,Widget,FVector2D(1280,720),0);FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);
        TestTrue(TEXT("Announcement band renders"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(1280,720,Pixels,Png);FFileHelper::SaveArrayToFile(Png,*(Directory/(Sample.Key+TEXT(".png"))));
        if(Sample.Key==TEXT("Moving"))
        {TestTrue(TEXT("The band is visible independently of the text"),Pixels[30*1280+640].A>64);TestTrue(TEXT("The horizontal edges are feathered while the center stays visible"),Pixels[30*1280+33].A<Pixels[30*1280+640].A);}
    }
    Target->ReleaseResource();
    return true;
}
#endif

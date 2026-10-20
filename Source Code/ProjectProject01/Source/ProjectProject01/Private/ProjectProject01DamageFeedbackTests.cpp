#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PlayerFeedbackWidgets.h"
#include "ProjectProject01GameInstance.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Styling/CoreStyle.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"
#include "PlayerCharacter.h"
#include "UObject/UnrealType.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDamageFeedbackSeverityTest,"ProjectProject01.Player.DamageFeedbackSeverity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectDamageFeedbackSeverityTest::RunTest(const FString&)
{
    auto Severity=[](int32 Remaining,int32 Starting)
    {return UProjectProject01DamageFeedbackWidget::CalculateDamageSeverity(Remaining,Starting);};
    TestEqual(TEXT("First lost count retains original visual strength"),Severity(2,3),0.0f);
    TestEqual(TEXT("One remaining out of three is midway to critical strength"),Severity(1,3),0.5f);
    TestEqual(TEXT("Zero remaining reaches strongest damage visual"),Severity(0,3),1.0f);
    for(int32 Starting:{1,2,3,5,100})
    {
        float Previous=0.0f;
        for(int32 Remaining=Starting;Remaining>=0;--Remaining)
        {
            const float Current=Severity(Remaining,Starting);
            TestTrue(TEXT("Every lost count increases or preserves bounded severity"),Current>=Previous&&Current<=1.0f);
            Previous=Current;
        }
        TestEqual(TEXT("Custom count totals also reach maximum at zero"),Previous,1.0f);
    }
    TestEqual(TEXT("Reset/checkpoint count restores baseline severity"),Severity(3,3),0.0f);
    TestEqual(TEXT("Restored checkpoint uses saved remaining count rather than a local hit tally"),Severity(1,3),0.5f);
    TestEqual(TEXT("Negative or invalid remaining count safely clamps"),Severity(-5,3),1.0f);
    TestEqual(TEXT("Overfull count does not produce negative severity"),Severity(20,3),0.0f);
    TestEqual(TEXT("Zero starting count is already critical"),Severity(0,0),1.0f);
    const UFunction* RPC=APlayerCharacter::StaticClass()->FindFunctionByName(TEXT("ClientPlayDamageFeedback"));
    TestNotNull(TEXT("Damage feedback retains owning-client RPC"),RPC);
    if(RPC)
    {
        TestTrue(TEXT("Severity is sent in the reliable owning-client RPC"),RPC->HasAllFunctionFlags(FUNC_NetClient|FUNC_NetReliable));
        TestNotNull(TEXT("RPC carries server severity instead of reading potentially stale replicated count"),FindFProperty<FFloatProperty>(RPC,TEXT("DamageSeverity")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDamageFeedbackRenderTest,"ProjectProject01.Player.DamageFeedbackRender",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectDamageFeedbackRenderTest::RunTest(const FString&)
{
    auto* Settings=UProjectProject01GameUserSettings::Get();
    const auto OldIntensity=Settings?Settings->GetVFXIntensity():EProjectProject01VFXIntensity::Standard;
    const float OldFlash=Settings?Settings->GetScreenFlashScale():1.0f;
    if(Settings){Settings->SetVFXIntensity(EProjectProject01VFXIntensity::Standard);Settings->SetScreenFlashScale(1.0f);}
    ON_SCOPE_EXIT {if(Settings){Settings->SetVFXIntensity(OldIntensity);Settings->SetScreenFlashScale(OldFlash);}};
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("DamageFeedbackRefresh");
    IFileManager::Get().MakeDirectory(*Directory,true);
    for(const FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
    {
        auto* Widget=NewObject<UProjectProject01DamageFeedbackWidget>();Widget->Initialize();
        Widget->Configure(0.1f,2.0f,0.7f,150.0f);
        TSharedRef<SWidget> Slate=SNew(SOverlay)
            +SOverlay::Slot()[SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor(FLinearColor(0.55f,0.55f,0.55f,1))]
            +SOverlay::Slot()[Widget->TakeWidget()];
        FWidgetRenderer Renderer(true);
        UTextureRenderTarget2D* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,true);
        if(!TestNotNull(TEXT("Damage vignette render target"),Target))continue;
        auto Capture=[&](const TCHAR* Name)
        {
            Renderer.DrawWidget(Target,Slate,FVector2D(Size),0.0f);FlushRenderingCommands();
            TArray<FColor> Pixels;
            FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
            // Slate already gamma-corrected the target; avoid a second conversion in the PNG readback.
            TestTrue(TEXT("Damage feedback reads back from actual Slate render"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
            if(Name)
            {
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
                TestTrue(TEXT("Save visual QA image"),FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("%s_%dx%d.png"),Name,Size.X,Size.Y))));
            }
            return Pixels;
        };
        const auto Before=Capture(nullptr);
        const auto Geometry=FGeometry::MakeRoot(FVector2D(Size),FSlateLayoutTransform());
        Widget->NativeTick(Geometry,0.1f);
        TestTrue(TEXT("Existing 0.1 second impact reaches configured maximum"),FMath::IsNearlyEqual(Widget->CurrentOpacity,0.7f)&&FMath::IsNearlyEqual(Widget->CurrentThicknessPixels,150.0f));
        const auto Peak=Capture(TEXT("DamagePeak"));
        if(Before.Num()!=Size.X*Size.Y||Peak.Num()!=Before.Num())continue;
        const int32 Center=Size.Y/2*Size.X+Size.X/2;
        const int32 Edge=Size.Y/2*Size.X+4;
        TestEqual(TEXT("Central elliptical sight stays clear"),Peak[Center],Before[Center]);
        TestTrue(TEXT("Outer edge becomes visibly dark blood-red"),Peak[Edge].R>Peak[Edge].G+12&&Peak[Edge].G+12<Peak[Center].G);
        TestTrue(TEXT("Exact corner rays leave no uncovered corner holes"),Peak[2*Size.X+2].G+12<Before[2*Size.X+2].G);
        Widget->Configure(0.1f,2.0f,0.7f,150.0f,0.5f);Widget->NativeTick(Geometry,0.1f);
        const auto Wounded=Capture(TEXT("DamageWounded"));
        const float WoundedOpacity=Widget->CurrentOpacity,WoundedThickness=Widget->CurrentThicknessPixels;
        Widget->Configure(0.1f,2.0f,0.7f,150.0f,1.0f);Widget->NativeTick(Geometry,0.1f);
        const auto Critical=Capture(TEXT("DamageCritical"));
        TestTrue(TEXT("Lower count raises opacity progressively without changing the intro time"),WoundedOpacity>0.7f&&Widget->CurrentOpacity>WoundedOpacity&&FMath::IsNearlyEqual(Widget->CurrentOpacity,0.91f,0.00001f));
        TestTrue(TEXT("Lower count widens the same ellipse progressively"),WoundedThickness>150.0f&&Widget->CurrentThicknessPixels>WoundedThickness&&FMath::IsNearlyEqual(Widget->CurrentThicknessPixels,202.5f));
        if(Wounded.Num()==Before.Num()&&Critical.Num()==Before.Num())
        {
            TestTrue(TEXT("Rendered red edge becomes denser with each severity step"),Wounded[Edge].G<Peak[Edge].G&&Critical[Edge].G<Wounded[Edge].G);
            TestEqual(TEXT("Critical damage still leaves the exact center clear"),Critical[Center],Before[Center]);
        }
        if(Settings)
        {
            Settings->SetScreenFlashScale(0.0f);const auto Off=Capture(nullptr);
            TestEqual(TEXT("Accessibility flash scale zero suppresses vignette"),Off[Edge],Before[Edge]);
            Settings->SetScreenFlashScale(1.0f);Settings->SetVFXIntensity(EProjectProject01VFXIntensity::Reduced);
            const auto Reduced=Capture(nullptr);
            TestTrue(TEXT("Reduced effects mode lowers pain overlay intensity"),Reduced[Edge].G>Peak[Edge].G);
            Settings->SetVFXIntensity(EProjectProject01VFXIntensity::Standard);
        }
        Widget->Configure(0.1f,2.0f,0.7f,150.0f);Widget->NativeTick(Geometry,0.1f);
        Widget->NativeTick(Geometry,0.5f);const auto Fading=Capture(TEXT("DamageFading"));
        TestTrue(TEXT("Existing two second release fades the same single mesh"),Fading[Edge].G>Peak[Edge].G&&Widget->CurrentOpacity>0);
        Widget->NativeTick(Geometry,1.6f);
        TestTrue(TEXT("Effect fully clears when original timeline ends"),Widget->CurrentOpacity==0&&Widget->CurrentThicknessPixels==0);
        Widget->ReleaseSlateResources(true);FlushRenderingCommands();
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMannequinCaptureFeedbackRenderTest,"ProjectProject01.Player.MannequinCaptureFeedbackRender",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectMannequinCaptureFeedbackRenderTest::RunTest(const FString&)
{
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("MannequinCaptureFeedbackRefresh");
    IFileManager::Get().MakeDirectory(*Directory,true);
    for(const FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600),FIntPoint(1920,800)})
    {
        auto* Widget=NewObject<UProjectProject01MannequinCaptureFeedbackWidget>();Widget->Initialize();
        Widget->Configure(false,1.0f);Widget->PatternSeed=3172;
        TestEqual(TEXT("Normal hit keeps the original short feedback duration"),Widget->DurationSeconds,0.68f);
        TestEqual(TEXT("Feedback does not consume attack or mouse input"),Widget->GetVisibility(),ESlateVisibility::HitTestInvisible);
        TSharedRef<SWidget> Slate=SNew(SOverlay)
            +SOverlay::Slot()[SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor(FLinearColor(0.55f,0.55f,0.55f,1))]
            +SOverlay::Slot()[Widget->TakeWidget()];
        FWidgetRenderer Renderer(true);
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,true);
        if(!TestNotNull(TEXT("Capture feedback render target"),Target))continue;
        auto Capture=[&](float Seconds,const TCHAR* Name=nullptr)
        {
            Widget->ElapsedSeconds=Seconds;Widget->Invalidate(EInvalidateWidgetReason::Paint);
            Renderer.DrawWidget(Target,Slate,FVector2D(Size),0.0f);FlushRenderingCommands();
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
            TestTrue(TEXT("Read actual capture feedback Slate pixels"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
            if(Name&&Pixels.Num()==Size.X*Size.Y)
            {
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
                TestTrue(TEXT("Save capture feedback visual QA"),FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("%s_%dx%d.png"),Name,Size.X,Size.Y))));
            }
            return Pixels;
        };
        const auto Before=Capture(0);
        const auto Impact=Capture(0.08f,TEXT("Impact"));
        const auto Repaint=Capture(0.08f);
        const auto Tail=Capture(0.42f,TEXT("Release"));
        const auto End=Capture(0.68f);
        if(Before.Num()!=Size.X*Size.Y||Impact.Num()!=Before.Num()||Tail.Num()!=Before.Num())continue;
        TestTrue(TEXT("Pattern stays stable on repeated paints rather than random flicker"),Impact==Repaint);
        TestTrue(TEXT("No lingering blood after the original duration"),End==Before);
        int32 Visible=0;int64 ImpactContrast=0,TailContrast=0;bool bCenterClear=true;
        const int32 ClearRadius=FMath::Min(Size.X,Size.Y)*0.08f;
        for(int32 Y=0;Y<Size.Y;++Y)for(int32 X=0;X<Size.X;++X)
        {
            const int32 I=Y*Size.X+X;
            if(Impact[I]!=Before[I])++Visible;
            ImpactContrast+=FMath::Abs(static_cast<int32>(Before[I].G)-Impact[I].G);
            TailContrast+=FMath::Abs(static_cast<int32>(Before[I].G)-Tail[I].G);
            if(FMath::Square(X-Size.X/2)+FMath::Square(Y-Size.Y/2)<FMath::Square(ClearRadius))
                bCenterClear&=Impact[I]==Before[I]&&Tail[I]==Before[I];
        }
        TestTrue(TEXT("Impact is visible but sparse, never a full-screen flash"),Visible>100&&Visible<Before.Num()/20);
        TestTrue(TEXT("Aim and target remain clear at screen center"),bCenterClear);
        TestTrue(TEXT("Droplets release gradually after the fast impact"),TailContrast>0&&TailContrast<ImpactContrast);
        Widget->PatternSeed=6178;const auto Alternate=Capture(0.08f);
        TestTrue(TEXT("Different confirmed hits can vary their splash pattern"),Alternate!=Impact);
        Widget->PatternSeed=3172;Widget->Configure(false,0.35f);Widget->PatternSeed=3172;
        const auto Reduced=Capture(0.08f);int64 ReducedContrast=0;
        if(Reduced.Num()==Before.Num())for(int32 I=0;I<Reduced.Num();++I)ReducedContrast+=FMath::Abs(static_cast<int32>(Before[I].G)-Reduced[I].G);
        TestTrue(TEXT("Existing accessibility intensity scales the effect down"),ReducedContrast>0&&ReducedContrast<ImpactContrast);
        Widget->Configure(false,0);TestTrue(TEXT("Zero intensity suppresses all splashes"),Capture(0.08f)==Before);
        Widget->Configure(true,1);Widget->PatternSeed=3172;
        TestEqual(TEXT("Elimination retains the original longer release"),Widget->DurationSeconds,0.95f);
        const auto Elimination=Capture(0.08f,TEXT("Elimination"));
        TestTrue(TEXT("Elimination has a distinct fuller impact"),Elimination!=Impact&&Elimination!=Before);
        TestTrue(TEXT("Elimination also clears completely"),Capture(0.95f)==Before);
        Widget->Configure(false,std::numeric_limits<float>::quiet_NaN());
        TestTrue(TEXT("Invalid intensity cannot poison the renderer"),Capture(0.08f)==Before);
        Widget->ReleaseSlateResources(true);FlushRenderingCommands();
    }
    return true;
}
#endif

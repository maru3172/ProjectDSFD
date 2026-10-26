#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01LoginWidget.h"
#include "MultiplayTestPlayerController.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "ProjectDFSDUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectResultBlackoutTest,"ProjectProject01.Multiplayer.Result.OpaqueHandoff",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectResultBlackoutTest::RunTest(const FString&)
{
    UUserWidget* Base=NewObject<UProjectProject01MatchResultWidget>();
    if(!TestTrue(TEXT("Result widget initializes"),Base->Initialize()))return false;
    auto* Widget=CastChecked<UProjectProject01MatchResultWidget>(Base);
    int32 FrameCount=0;Widget->WidgetTree->ForEachWidget([&](UWidget* W){FrameCount+=W->IsA<UProjectDFSDPageFrame>();});
    TestEqual(TEXT("Death, escape and match results do not use the case-file menu frame"),FrameCount,0);
    for(const FString Outcome:{FString(TEXT("Eliminated")),FString(TEXT("Escaped")),FString(TEXT("MannequinVictory"))})
    {
        FMultiplayTestMatchResult Result;Result.Role=Outcome==TEXT("MannequinVictory")?TEXT("Mannequin"):TEXT("Survivor");
        Result.Outcome=Outcome;Result.bSuccess=Outcome!=TEXT("Eliminated");
        Widget->ConfigureResult(Result);
        TestEqual(TEXT("Before viewport handoff, multiplayer background is already fully opaque"),Widget->FadeBackground->GetBrushColor().A,1.f);
        TestEqual(TEXT("Blackout first: the result menu starts hidden"),Widget->ResultMenu->GetRenderOpacity(),0.f);
        // NativeConstruct calls this same initializer. Construction must not reset
        // the opaque background to transparent after the old death layer is released.
        Widget->InitializeResultPresentation();
        float Previous=-1.f;
        for(float Delta:{0.f,.016f,.5f,2.f})
        {
            Widget->NativeTick(FGeometry(),Delta);
            TestEqual(TEXT("Every result frame keeps the game world covered"),Widget->FadeBackground->GetBrushColor().A,1.f);
            TestTrue(TEXT("The blackout releases into the results gradually"),Widget->ResultMenu->GetRenderOpacity()>=Previous);
            Previous=Widget->ResultMenu->GetRenderOpacity();
        }
        TestEqual(TEXT("Results end fully visible"),Widget->ResultMenu->GetRenderOpacity(),1.f);
        FWidgetRenderer Renderer(false);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1280,720),TF_Bilinear,false);
        Renderer.DrawWidget(Target,Widget->GetRootWidget()->TakeWidget(),FVector2D(1280,720),0);FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);
        TestTrue(TEXT("Fully revealed result renders"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
        int32 Bright=0;for(const auto& P:Pixels)Bright+=P.R>160&&P.G>160&&P.B>160;TestTrue(TEXT("Result heading and actions remain readable after removing menu styling"),Bright>50);
        const FString Dir=FPaths::ProjectSavedDir()/TEXT("FeatureChecks/ResultTheme");IFileManager::Get().MakeDirectory(*Dir,true);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1280,720,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/(Outcome+TEXT(".png"))));Target->ReleaseResource();
        Result.bLeaderboardVerificationReady=true;Widget->ConfigureResult(Result);
        TestEqual(TEXT("A verification refresh cannot replay blackout"),Widget->FadeBackground->GetBrushColor().A,1.f);
    }
    FMultiplayTestMatchResult Observed;Observed.Role=TEXT("Observer");Widget->ConfigureResult(Observed);
    TestEqual(TEXT("Observer result starts with the world already covered"),Widget->FadeBackground->GetBrushColor().A,1.f);
    TestEqual(TEXT("Observer result contents start hidden for a gradual reveal"),Widget->ResultMenu->GetRenderOpacity(),0.f);
    for(float Delta:{.016f,.5f,2.f})
    {Widget->NativeTick(FGeometry(),Delta);TestEqual(TEXT("Observer content fade never exposes the world"),Widget->FadeBackground->GetBrushColor().A,1.f);}
    TestEqual(TEXT("Observer final results become fully visible"),Widget->ResultMenu->GetRenderOpacity(),1.f);
    Widget->ConfigureSinglePlayerResult(false);
    TestEqual(TEXT("Single-player death also hands off against opaque black"),Widget->FadeBackground->GetBrushColor().A,1.f);
    Widget->ConfigureSinglePlayerResult(true);
    TestEqual(TEXT("Single-player escape keeps its original gradual fade"),Widget->FadeElapsedSeconds,0.f);
    auto* Leaderboard=NewObject<UProjectProject01LeaderboardWidget>();static_cast<UUserWidget*>(Leaderboard)->Initialize();int32 LeaderboardFrames=0;Leaderboard->WidgetTree->ForEachWidget([&](UWidget* W){LeaderboardFrames+=W->IsA<UProjectDFSDPageFrame>();});TestTrue(TEXT("Leaderboard retains the case-file menu frame"),LeaderboardFrames>0);
    return true;
}
#endif

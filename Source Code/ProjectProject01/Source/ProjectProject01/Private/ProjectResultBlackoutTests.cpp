#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01LoginWidget.h"
#include "MultiplayTestPlayerController.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectResultBlackoutTest,"ProjectProject01.Multiplayer.Result.OpaqueHandoff",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectResultBlackoutTest::RunTest(const FString&)
{
    UUserWidget* Base=NewObject<UProjectProject01MatchResultWidget>();
    if(!TestTrue(TEXT("Result widget initializes"),Base->Initialize()))return false;
    auto* Widget=CastChecked<UProjectProject01MatchResultWidget>(Base);
    for(const FString Outcome:{FString(TEXT("Eliminated")),FString(TEXT("Escaped")),FString(TEXT("MannequinVictory"))})
    {
        FMultiplayTestMatchResult Result;Result.Role=Outcome==TEXT("MannequinVictory")?TEXT("Mannequin"):TEXT("Survivor");
        Result.Outcome=Outcome;Result.bSuccess=Outcome!=TEXT("Eliminated");
        Widget->ConfigureResult(Result);
        TestEqual(TEXT("Before viewport handoff, multiplayer background is already fully opaque"),Widget->FadeBackground->GetBrushColor().A,1.f);
        TestEqual(TEXT("Result menu is ready immediately, not after a second fade"),Widget->ResultMenu->GetRenderOpacity(),1.f);
        TestTrue(TEXT("No secondary multiplayer fade remains"),Widget->FadeElapsedSeconds>=Widget->FadeDurationSeconds);
        // NativeConstruct calls this same initializer. Construction must not reset
        // the opaque background to transparent after the old death layer is released.
        Widget->InitializeResultPresentation();
        for(float Delta:{0.f,.016f,.5f,2.f})
        {
            Widget->NativeTick(FGeometry(),Delta);
            TestEqual(TEXT("Every result frame keeps the game world covered"),Widget->FadeBackground->GetBrushColor().A,1.f);
            TestEqual(TEXT("Every result frame keeps the menu visible"),Widget->ResultMenu->GetRenderOpacity(),1.f);
        }
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
    return true;
}
#endif

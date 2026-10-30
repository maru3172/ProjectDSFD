#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/WidgetSwitcher.h"
#include "Dom/JsonObject.h"
#include "ProjectProject01LoginWidget.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectDFSDUITheme.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectFrontendBehaviorTest,"ProjectProject01.Frontend.InitialTutorialAndRandomRole",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectFrontendBehaviorTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Instance=NewObject<UGameInstance>();
    World->SetGameInstance(Instance);Instance->Init();
    auto* Auth=Instance->GetSubsystem<UProjectProject01AuthSubsystem>();auto* Lobby=Instance->GetSubsystem<UProjectProject01LobbySubsystem>();
    auto* Widget=NewObject<UProjectProject01LobbyWidget>(World);static_cast<UUserWidget*>(Widget)->Initialize();
    auto* Guide=NewObject<UProjectProject01GameGuideWidget>(World);static_cast<UUserWidget*>(Guide)->Initialize();
    ON_SCOPE_EXIT{Lobby->ReviewRequestSink=nullptr;Widget->ReleaseSlateResources(true);Guide->ReleaseSlateResources(true);Instance->Shutdown();World->SetGameInstance(nullptr);World->DestroyWorld(false);};
    int32 Frames=0;Guide->WidgetTree->ForEachWidget([&](UWidget* W){Frames+=W->IsA<UProjectDFSDPageFrame>();});
    TestEqual(TEXT("In-game pamphlet does not have a second case-file page behind it"),Frames,0);
    TestEqual(TEXT("First-tutorial prompt starts hidden"),Widget->InitialTutorialPrompt->GetVisibility(),ESlateVisibility::Collapsed);
    Auth->AuthState=EProjectProject01AuthState::SignedIn;Auth->AccessToken=TEXT("offline-fixture");Auth->ApiBaseUrl.Reset();
    TestFalse(TEXT("New account starts without an initial-tutorial decision"),Auth->HasSeenInitialTutorialForAccount());
    Auth->MarkInitialTutorialSeenForAccount();Auth->MarkInitialTutorialSeenForAccount();
    TestTrue(TEXT("Tutorial offer is consumed once without consuming either role pamphlet"),Auth->HasSeenInitialTutorialForAccount()&&!Auth->HasSeenSurvivorGuideForAccount()&&!Auth->HasSeenMannequinGuideForAccount());
    Widget->InitialTutorialPrompt->SetVisibility(ESlateVisibility::Visible);Widget->HandleInitialTutorialSkip();
    TestEqual(TEXT("Skip dismisses prompt without starting tutorial travel"),Widget->InitialTutorialPrompt->GetVisibility(),ESlateVisibility::Collapsed);
    TestFalse(TEXT("Skip preserves the normal matchmaking screen"),Widget->bTravelRequested);
    TArray<FString> Requests;Lobby->ReviewRequestSink=[&](const FString& Path){Requests.Add(Path);};
    Lobby->CreateBotMatch(TEXT("Any"),2);
    TestTrue(TEXT("Random bot role reaches the ordinary admission request without client-side rejection"),Requests.Num()==1&&Requests[0]==TEXT("/api/bot-matches"));
    Lobby->CreateBotMatch(TEXT("Observer"),2);Lobby->CreateBotMatch(TEXT("Any"),6);
    TestEqual(TEXT("Invalid role and difficulty remain rejected"),Requests.Num(),1);
    Auth->ForceClearLocalSession();TestFalse(TEXT("Account switch clears the previous initial-tutorial flag"),Auth->HasSeenInitialTutorialForAccount());
    {
        TStrongObjectPtr<UWorld> KeepWorld(World);TStrongObjectPtr<UGameInstance> KeepInstance(Instance);
        TStrongObjectPtr<UProjectProject01LobbyWidget> KeepWidget(Widget);TStrongObjectPtr<UProjectProject01GameGuideWidget> KeepGuide(Guide);
        UWidget* Generated=Widget->GenerateBotDifficultyItem(TEXT("보통"));
        TWeakObjectPtr<UWidget> GeneratedWeak=Generated;
        TWeakObjectPtr<UWidget> TooltipWeak=Generated->GetToolTip();
        auto TooltipSlate=Generated->GetToolTip()->TakeWidget();
        TSharedPtr<SWidget> GeneratedSlate=Generated->TakeWidget();Generated=nullptr;
        auto PageSlate=Widget->TakeWidget();
        for(int32 Attempt=0;Attempt<12;++Attempt)
        {
            Lobby->CurrentRoom.RoomId=TEXT("offline-ui-party");Lobby->CurrentRoom.bRandomParty=true;
            Lobby->CurrentRoom.bIsHost=true;Lobby->CurrentRoom.Members.SetNum(1);
            Widget->ShowLobbyView(3);
            Widget->RandomMatchRole->SetSelectedOption(TEXT("생존자"));
            Widget->RandomMatchRole->SetSelectedOption(TEXT("무작위"));
            Lobby->bMatchmaking=true;Widget->HandleMatchCancelClicked();
            auto Cancelled=MakeShared<FJsonObject>();Cancelled->SetStringField(TEXT("state"),TEXT("Cancelled"));FString Error;
            TestTrue(TEXT("Cancellation returns the matchmaking model to idle"),Lobby->ParseMatchmaking(Cancelled,Error));
            Lobby->bCancelMatchmaking=false;
            Widget->HandleLobbyModesClicked();Lobby->CurrentRoom={};Widget->ShowLobbyView(4);
            PageSlate->SlatePrepass();
        }
        TestTrue(TEXT("Random role and cancellation round trips leave no travel or matchmaking lock"),!Widget->bTravelRequested&&!Lobby->IsMatchmaking()&&Widget->LobbyViewSwitcher->GetActiveWidgetIndex()==4);
        CollectGarbage(RF_NoFlags);
        TestTrue(TEXT("A combo option still displayed by Slate must survive garbage collection"),GeneratedWeak.IsValid());
        if(GeneratedWeak.IsValid()){GeneratedSlate->SlatePrepass();PageSlate->SlatePrepass();}
        GeneratedSlate.Reset();CollectGarbage(RF_NoFlags);
        TestTrue(TEXT("A visible tooltip survives after its option row is released"),TooltipWeak.IsValid());
        if(TooltipWeak.IsValid())TooltipSlate->SlatePrepass();
    }
    return true;
}
#endif

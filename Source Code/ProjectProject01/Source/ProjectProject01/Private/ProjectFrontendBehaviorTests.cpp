#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/WidgetSwitcher.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "ProjectProject01LoginWidget.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectCaseRecords.h"
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
    ON_SCOPE_EXIT{Lobby->ReviewRequestSink=nullptr;if(auto* R=Instance->GetSubsystem<UProjectCaseRecords>())R->ReviewRequestSink=nullptr;Widget->ReleaseSlateResources(true);Guide->ReleaseSlateResources(true);Instance->Shutdown();World->SetGameInstance(nullptr);World->DestroyWorld(false);};
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
    auto* Records=Instance->GetSubsystem<UProjectCaseRecords>();Records->Snapshot=MakeShared<FJsonObject>();
    auto PartyState=MakeShared<FJsonObject>();Records->Snapshot->SetObjectField(TEXT("partyState"),PartyState);
    for(const FString Role:{FString(TEXT("Any")),FString(TEXT("Survivor")),FString(TEXT("Mannequin"))})
    {
        PartyState->SetStringField(TEXT("role"),Role);Widget->RefreshRoleSelection();Widget->RefreshRoleSelection();
        const FString Expected=Role==TEXT("Any")?TEXT("무작위"):Role==TEXT("Survivor")?TEXT("생존자"):TEXT("마네킹");
        TestTrue(TEXT("Repeated role refresh preserves each saved random/open matchmaking role"),Widget->RandomMatchRole->GetSelectedOption()==Expected&&Widget->RoomMatchRole->GetSelectedOption()==Expected);
    }
    Records->Snapshot->SetStringField(TEXT("userId"),TEXT("1"));
    TArray<FString> RoleWrites;
    Records->ReviewRequestSink=[&](const FString& Path,const FString& Body)
    {TSharedPtr<FJsonObject> Json;FString Role;if(Path==TEXT("/api/parties/action")&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Json)&&Json->TryGetStringField(TEXT("role"),Role))RoleWrites.Add(Role);};
    // Own the fake requests without ProcessRequest: exercise the real widget/write path without a backend.
    Lobby->ActiveRequest=FHttpModule::Get().CreateRequest();Lobby->ActiveRequest->SetVerb(TEXT("GET"));
    TestFalse(TEXT("Periodic lobby reads do not count as a blocking state change"),Lobby->IsStateChangeInFlight());
    Lobby->ActiveRequest->SetVerb(TEXT("POST"));TestTrue(TEXT("Actual lobby writes still lock state changes"),Lobby->IsStateChangeInFlight());Lobby->ActiveRequest->SetVerb(TEXT("GET"));
    for(bool Open:{false,true})
    {
        Lobby->CurrentRoom={};if(Open){Lobby->CurrentRoom.RoomId=TEXT("offline-open-room");Lobby->CurrentRoom.bIsHost=true;}
        Widget->ShowLobbyView(Open?2:3);PartyState->SetStringField(TEXT("role"),TEXT("Any"));Records->Status.Reset();
        auto* Combo=Open?Widget->RoomMatchRole.Get():Widget->RandomMatchRole.Get();auto* Parent=Combo->GetParent();const int32 Before=RoleWrites.Num();
        Combo->SetSelectedOption(TEXT("생존자"));Widget->HandleMatchRoleChanged(TEXT("생존자"),ESelectInfo::OnMouseClick);Widget->RefreshRoleSelection();
        TestTrue(TEXT("Random and open role controls stay enabled and attached while saving"),Combo->GetIsEnabled()&&Combo->GetParent()==Parent&&Widget->LobbyViewSwitcher->GetActiveWidgetIndex()==(Open?2:3)&&RoleWrites.Num()==Before+1);
        Combo->SetSelectedOption(TEXT("마네킹"));Widget->HandleMatchRoleChanged(TEXT("마네킹"),ESelectInfo::OnMouseClick);
        Combo->SetSelectedOption(TEXT("무작위"));Widget->HandleMatchRoleChanged(TEXT("무작위"),ESelectInfo::OnMouseClick);Widget->RefreshRoleSelection();
        TestTrue(TEXT("Rapid role edits keep the latest visible choice and coalesce pending requests"),RoleWrites.Num()==Before+1&&Combo->GetSelectedOption()==TEXT("무작위")&&Combo->GetIsEnabled());
        Records->Request.Reset();PartyState->SetStringField(TEXT("role"),TEXT("Survivor"));Widget->RefreshRoleSelection();
        TestTrue(TEXT("An older role response saves the latest queued choice once without rolling the menu back"),RoleWrites.Num()==Before+2&&RoleWrites.Last()==TEXT("Any")&&Combo->GetSelectedOption()==TEXT("무작위"));
        Widget->RefreshRoleSelection();TestEqual(TEXT("A pending write is never retried on every UI refresh"),RoleWrites.Num(),Before+2);
        Records->Request.Reset();PartyState->SetStringField(TEXT("role"),TEXT("Any"));Widget->RefreshRoleSelection();
        TestTrue(TEXT("Finishing a role save releases the pending state without changing the page"),!Widget->bRoleWritePending&&Combo->GetIsEnabled()&&Widget->LobbyViewSwitcher->GetActiveWidgetIndex()==(Open?2:3));
        Combo->SetSelectedOption(TEXT("마네킹"));Widget->HandleMatchRoleChanged(TEXT("마네킹"),ESelectInfo::OnMouseClick);
        Records->Request.Reset();Records->Status=TEXT("역할 변경에 실패했습니다.");Widget->RefreshRoleSelection();
        TestTrue(TEXT("A failed role save stops the queue and restores the authoritative role in place"),!Widget->bRoleWritePending&&Combo->GetSelectedOption()==TEXT("무작위")&&Combo->GetParent()==Parent);
    }
    Records->Status.Reset();Lobby->CurrentRoom={};
    TArray<FProjectProject01RoomMember> Members;auto& Self=Members.AddDefaulted_GetRef();Self.UserId=TEXT("1");Self.DisplayName=TEXT("나");Self.bIsHost=true;
    auto& Other=Members.AddDefaulted_GetRef();Other.UserId=TEXT("2");Other.DisplayName=TEXT("동료");
    Widget->PopulateRoomMemberProfiles(Members,false);auto* First=Widget->MemberProfilePanel->GetChildAt(0);auto* OtherBinding=Widget->MemberProfileBindings[1].Get();auto* RoleParent=Widget->RoomRoleRow->GetParent();
    Members[1].bIsReady=true;Members[1].AssignedRole=TEXT("Survivor");Members[1].DisplayName=TEXT("변경된 동료");Widget->PopulateRoomMemberProfiles(Members,true);
    TestTrue(TEXT("Ready, name and revealed role changes update existing participant labels without reparenting role controls"),Widget->MemberProfilePanel->GetChildAt(0)==First&&Widget->MemberProfileBindings[1]==OtherBinding&&Widget->RoomRoleRow->GetParent()==RoleParent&&OtherBinding->Label->GetText().ToString().Contains(TEXT("변경된 동료 — 준비 [Survivor]")));
    Lobby->RefreshLeaderboard(EProjectProject01LeaderboardRole::Survivor,EProjectProject01LeaderboardSort::Overall,EProjectProject01LeaderboardScope::Individual);Lobby->LeaderboardEntries.AddDefaulted();
    Lobby->RefreshLeaderboard(EProjectProject01LeaderboardRole::Survivor,EProjectProject01LeaderboardSort::Overall,EProjectProject01LeaderboardScope::Individual);TestEqual(TEXT("Refreshing the same leaderboard keeps its visible records until the reply"),Lobby->LeaderboardEntries.Num(),1);
    Lobby->RefreshLeaderboard(EProjectProject01LeaderboardRole::Mannequin,EProjectProject01LeaderboardSort::Overall,EProjectProject01LeaderboardScope::Individual);TestEqual(TEXT("Changing leaderboard category cannot show records from the old category"),Lobby->LeaderboardEntries.Num(),0);
    Widget->HandleMatchRoleChanged(TEXT("생존자"),ESelectInfo::OnMouseClick);const int32 BeforeLogout=RoleWrites.Num();
    Auth->ForceClearLocalSession();Widget->RefreshRoleSelection();TestFalse(TEXT("Account switch clears the previous initial-tutorial flag"),Auth->HasSeenInitialTutorialForAccount());
    TestTrue(TEXT("Logout discards a pending role write instead of sending it under another account"),!Widget->bRoleWritePending&&!Records->IsBusy()&&RoleWrites.Num()==BeforeLogout);
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

#include "ProjectProject01MatchTimeline.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MultiplayTestGameMode.h"
#include "ProjectProject01LoginWidget.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"
#include "AIController.h"
#include "Engine/World.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Components/SizeBox.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace
{
    UWorld* CreateHistoryTestWorld()
    {
        const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        return UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMatchTimelineBoundedTest,"ProjectProject01.MatchTimeline.BoundedSerialization",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMatchTimelineBoundedTest::RunTest(const FString&)
{
    FProjectMatchTimelineLog Log;
    Log.Add(0,EProjectMatchEvent::Started);
    Log.Add(10,EProjectMatchEvent::DeathCountLost,TEXT("테스트\n플레이어"),TEXT("2"));
    Log.Add(5,EProjectMatchEvent::Escaped,TEXT("테스트"));
    TestEqual(TEXT("Out-of-order clock cannot reverse displayed chronology"),Log.Events.Last().Seconds,10.0);
    TestFalse(TEXT("Player names cannot inject new rows"),Log.Events[1].PlayerName.Contains(TEXT("\n")));
    TestTrue(TEXT("Localized row shows elapsed time and event"),ProjectProject01Timeline::FormatEvent(Log.Events[1]).ToString().Contains(TEXT("00:10")));
    for(int32 Index=0;Index<140;++Index) Log.Add(20+Index,EProjectMatchEvent::DeathCountLost,FString::ChrN(500,TEXT('A')),FString::ChrN(500,TEXT('B')));
    TestEqual(TEXT("Event memory is bounded"),Log.Events.Num(),128);
    TestEqual(TEXT("Omitted events are disclosed"),Log.OmittedEvents,15);
    TestTrue(TEXT("Match start survives pruning"),Log.Events[0].Kind==EProjectMatchEvent::Started);
    TestEqual(TEXT("Name size is bounded"),Log.Events.Last().PlayerName.Len(),48);
    TestEqual(TEXT("Details size is bounded"),Log.Events.Last().Detail.Len(),96);
    Log.Events.Last().PlayerName=TEXT("생존자 가나다");
    FMultiplayTestMatchResult Result; Result.Timeline=Log.Events; Result.OmittedTimelineEvents=Log.OmittedEvents;
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive WriteArchive(Writer,false);
    FMultiplayTestMatchResult::StaticStruct()->SerializeItem(WriteArchive,&Result,nullptr);
    FMultiplayTestMatchResult Restored;
    FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive ReadArchive(Reader,false);
    FMultiplayTestMatchResult::StaticStruct()->SerializeItem(ReadArchive,&Restored,nullptr);
    TestEqual(TEXT("Final result reflected serialization preserves events"),Restored.Timeline.Num(),Result.Timeline.Num());
    TestEqual(TEXT("Final result serialization preserves omission count"),Restored.OmittedTimelineEvents,15);
    TestEqual(TEXT("Korean event metadata survives serialization"),Restored.Timeline.Last().PlayerName,FString(TEXT("생존자 가나다")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMatchTimelineServerTest,"ProjectProject01.MatchTimeline.ServerPrivacyAndTravel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMatchTimelineServerTest::RunTest(const FString&)
{
    UWorld* World=CreateHistoryTestWorld();
    auto* Instance=NewObject<UProjectProject01GameInstance>();World->SetGameInstance(Instance);
    auto* Mode=World->SpawnActor<AMultiplayTestGameMode>();
    Mode->AuthoritativeMatchId=TEXT("test-one");Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::InProgress;
    Mode->BeginMatchTimeline();
    Mode->TimelineLevelStartWorldSeconds=-10;
    Mode->RecordTimeline(EProjectMatchEvent::Escaped,TEXT("생존자 A"));
    FMultiplayTestServerPlayerRecord Record;Record.Role=TEXT("Survivor");
    TestTrue(TEXT("History is not exposed by an in-progress result"),Mode->BuildFinalResult(nullptr,Record).Timeline.IsEmpty());
    Mode->PrepareLevelTransitionTravel();
    TestTrue(TEXT("Committed travel marks history carryover"),Instance->ServerMatchTimeline->bAwaitingNextLevel);
    const int32 Before=Mode->TimelineLog->Events.Num();
    Mode->PrepareLevelTransitionTravel();
    TestEqual(TEXT("Duplicate travel preparation does not double elapsed time"),Mode->TimelineLog->PreviousLevelsSeconds,10.0);
    auto* Next=World->SpawnActor<AMultiplayTestGameMode>();
    Next->AuthoritativeMatchId=TEXT("test-one");Next->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::InProgress;
    Next->BeginMatchTimeline();
    TestEqual(TEXT("Same match across maps preserves prior events"),Next->TimelineLog->Events.Num(),Before+1);
    TestEqual(TEXT("New map event uses accumulated time"),Next->TimelineLog->Events.Last().Seconds,10.0);
    Next->bFinishCommitted=true;Next->RecordTimeline(EProjectMatchEvent::Ended,{},TEXT("TimeLimit"));
    TestFalse(TEXT("Completed match includes final timeline"),Next->BuildFinalResult(nullptr,Record).Timeline.IsEmpty());
    const int32 Finished=Next->TimelineLog->Events.Num();
    Next->RecordTimeline(EProjectMatchEvent::DeathCountLost,TEXT("late"));
    TestEqual(TEXT("Late events cannot modify a completed match"),Next->TimelineLog->Events.Num(),Finished);
    Next->AuthoritativeMatchId=TEXT("test-two");Next->bFinishCommitted=false;Next->BeginMatchTimeline();
    TestEqual(TEXT("New match never inherits prior history"),Next->TimelineLog->Events.Num(),1);
    World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMatchTimelineWidgetTest,"ProjectProject01.MatchTimeline.ResultWidget",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMatchTimelineWidgetTest::RunTest(const FString&)
{
    UUserWidget* Base=NewObject<UProjectProject01MatchResultWidget>();
    if(!TestTrue(TEXT("Result widget initializes"),Base->Initialize()))return false;
    auto* Widget=CastChecked<UProjectProject01MatchResultWidget>(Base);
    FMultiplayTestMatchResult Result;Result.Role=TEXT("Survivor");
    FProjectMatchTimelineEvent Event;Event.Kind=EProjectMatchEvent::Escaped;Event.Seconds=70;Event.PlayerName=TEXT("생존자 A");Result.Timeline.Add(Event);
    Widget->ConfigureResult(Result);Widget->FadeElapsedSeconds=Widget->FadeDurationSeconds;
    TestTrue(TEXT("History button appears for multiplayer history"),Widget->TimelineButton->GetVisibility()==ESlateVisibility::Visible);
    Widget->HandleTimelineClicked();
    TestTrue(TEXT("History can replace summary without travel"),Widget->bShowingTimeline&&Widget->TimelineFrame->GetVisibility()==ESlateVisibility::Visible);
    auto* Row=Cast<UTextBlock>(Widget->TimelineRows->GetChildAt(0));
    TestTrue(TEXT("History row contains real timestamp"),Row&&Row->GetText().ToString().Contains(TEXT("01:10")));
    TestEqual(TEXT("Repeated verification refresh does not duplicate rows"),Widget->TimelineRows->GetChildrenCount(),1);
    Widget->ConfigureResult(Result);TestEqual(TEXT("Server verification update replaces history"),Widget->TimelineRows->GetChildrenCount(),1);
    Widget->HandleTimelineClicked();TestFalse(TEXT("Summary can be restored"),Widget->bShowingTimeline);
    Widget->ConfigureSinglePlayerResult(false);
    TestTrue(TEXT("Single player result does not show multiplayer history"),Widget->TimelineButton->GetVisibility()==ESlateVisibility::Collapsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMannequinSearchTest,"ProjectProject01.Multiplayer.LastSeenSearch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMannequinSearchTest::RunTest(const FString&)
{
    UWorld* World=CreateHistoryTestWorld();
    auto* Mode=World->SpawnActor<AMultiplayTestGameMode>();
    auto* Pawn=World->SpawnActor<AMannequinAICharacter>();auto* AI=World->SpawnActor<AAIController>();AI->Possess(Pawn);
    auto* Survivor=World->SpawnActor<APlayerCharacter>();auto* PC=World->SpawnActor<APlayerController>();PC->Possess(Survivor);
    Pawn->SetActorLocation(FVector(0,0,100));Survivor->SetActorLocation(FVector(1000,0,100));
    Pawn->SetMultiplayCommand(EMultiplayMannequinCommand::FollowMannequinPlayer);
    FMultiplayMannequinSearchState Memory;Memory.Target=Survivor;Memory.LastSeenLocation=Pawn->GetActorLocation();Memory.StartedSeconds=1;
    Mode->MultiplayerSearchStates.Add(Pawn,Memory);
    Survivor->SetActorLocation(FVector(9000,9000,100));
    TestTrue(TEXT("Lost target begins local observation at remembered position"),Mode->ContinueLastSeenSearch(Pawn,AI,2));
    TestTrue(TEXT("Search never updates to hidden target's new coordinates"),Mode->MultiplayerSearchStates.FindChecked(Pawn).LastSeenLocation.Equals(FVector(0,0,100)));
    TestTrue(TEXT("Search status is distinct from patrol/chase"),Pawn->GetMultiplayActivity()==EMultiplayMannequinActivity::Searching);
    TestFalse(TEXT("Observation ends after its bounded duration"),Mode->ContinueLastSeenSearch(Pawn,AI,5));
    TestTrue(TEXT("Search does not replace the player's follow command"),Pawn->GetMultiplayCommand()==EMultiplayMannequinCommand::FollowMannequinPlayer);
    Mode->MultiplayerSearchStates.Add(Pawn,Memory);
    TestFalse(TEXT("Total search has a hard timeout"),Mode->ContinueLastSeenSearch(Pawn,AI,12));
    Memory.StartedSeconds=-1;Mode->MultiplayerSearchStates.Add(Pawn,Memory);
    TestFalse(TEXT("Missing/unreachable navigation safely abandons search"),Mode->ContinueLastSeenSearch(Pawn,AI,2));
    Mode->MultiplayerSearchStates.Add(Pawn,Memory);PC->UnPossess();
    TestFalse(TEXT("Disconnected/invalid target cannot keep search alive"),Mode->ContinueLastSeenSearch(Pawn,AI,2));
    Mode->MultiplayerSearchStates.Add(Pawn,Memory);Pawn->SetMultiplayCommand(EMultiplayMannequinCommand::HoldPosition);
    Mode->UpdateMultiplayerMannequinAI();
    TestFalse(TEXT("Hold command immediately clears remembered search"),Mode->MultiplayerSearchStates.Contains(Pawn));
    World->DestroyWorld(false);return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MultiplayTestGameMode.h"
#include "ProjectMatchBots.h"
#include "ProjectProject01GameInstance.h"
#include "Engine/World.h"

// Leaving a match: who gets a forfeit (shown to players as "기권"), who keeps a plain result, and when the
// match ends because nobody human is left playing.
namespace
{
    UWorld* CreateForfeitWorld()
    {
        const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        return UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    }
    FMultiplayTestServerPlayerRecord Person(const FString& Id,const TCHAR* Role,EMultiplayTestSurvivorState State,bool Bot=false)
    {
        FMultiplayTestServerPlayerRecord Record;Record.UserId=Id;Record.MatchId=TEXT("forfeit-match");Record.Role=Role;
        Record.SurvivorState=State;Record.bServerBot=Bot;Record.DisplayName=Id;return Record;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectForfeitRulesTest,"ProjectProject01.Review.ForfeitRules",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectForfeitRulesTest::RunTest(const FString&)
{
    using EState=EMultiplayTestSurvivorState;
    auto Fresh=[](UWorld* World)
    {
        World->SetGameInstance(NewObject<UProjectProject01GameInstance>());
        auto* Mode=World->SpawnActor<AMultiplayTestGameMode>();
        Mode->AuthoritativeMatchId=TEXT("forfeit-match");Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::InProgress;Mode->MatchStartWorldSeconds=-100.0;
        return Mode;
    };
    auto AddPlayer=[](UWorld* World,AMultiplayTestGameMode* Mode,const FMultiplayTestServerPlayerRecord& Record)
    {auto* PC=World->SpawnActor<AMultiplayTestPlayerController>();Mode->MatchPlayerRecords.Add(PC,Record);return PC;};

    // Which matches have forfeits at all: PvP always, a bot match only with two or more people.
    {
        UWorld* World=CreateForfeitWorld();ON_SCOPE_EXIT{World->DestroyWorld(false);};
        auto* Mode=Fresh(World);
        AddPlayer(World,Mode,Person(TEXT("1"),TEXT("Survivor"),EState::Active));
        TestTrue(TEXT("PvP (no bot roster): leaving in play is a forfeit"),Mode->CountsAsDesertion());
        FString Error;const FString Room=FGuid::NewGuid().ToString(),Match=FGuid::NewGuid().ToString();
        TestTrue(TEXT("Bot roster accepted"),Mode->MatchBots->AcceptRoster(Room,Match,{{FGuid::NewGuid().ToString(),TEXT("봇"),TEXT("Mannequin"),3}},Error));
        TestFalse(TEXT("One person with bots: leaving is only a loss"),Mode->CountsAsDesertion());
        Mode->DisconnectedMatchRecords.Add(TEXT("2"),Person(TEXT("2"),TEXT("Survivor"),EState::Active));
        TestTrue(TEXT("Two people with bots (one already gone still counts): forfeits apply"),Mode->CountsAsDesertion());
    }

    // A teammate still in play is a human survivor, not caught, escaped, gone or forfeited.
    {
        UWorld* World=CreateForfeitWorld();ON_SCOPE_EXIT{World->DestroyWorld(false);};
        auto* Mode=Fresh(World);
        auto* A=AddPlayer(World,Mode,Person(TEXT("A"),TEXT("Survivor"),EState::Active));
        auto* B=AddPlayer(World,Mode,Person(TEXT("B"),TEXT("Survivor"),EState::Eliminated));
        AddPlayer(World,Mode,Person(TEXT("Bot"),TEXT("Survivor"),EState::Active,true));
        TestFalse(TEXT("A caught teammate and a bot are not a human teammate in play"),Mode->HasOtherHumanSurvivorInPlay(A));
        TestTrue(TEXT("The living survivor is in play for the caught one"),Mode->HasOtherHumanSurvivorInPlay(B));
    }

    // Grace over: one who left alive forfeits; one who left after being caught keeps the loss, no forfeit.
    {
        UWorld* World=CreateForfeitWorld();ON_SCOPE_EXIT{World->DestroyWorld(false);};
        auto* Mode=Fresh(World);
        AddPlayer(World,Mode,Person(TEXT("M"),TEXT("Mannequin"),EState::NotApplicable));
        AddPlayer(World,Mode,Person(TEXT("Stay"),TEXT("Survivor"),EState::Active));
        World->TimeSeconds=100; // Both graces ran out at 50 s.
        auto Alive=Person(TEXT("Alive"),TEXT("Survivor"),EState::Active);Alive.bDisconnected=true;Alive.ReconnectDeadlineWorldSeconds=50;
        auto Caught=Person(TEXT("Caught"),TEXT("Survivor"),EState::Eliminated);Caught.bDisconnected=true;Caught.ReconnectDeadlineWorldSeconds=50;
        Mode->DisconnectedMatchRecords.Add(Alive.UserId,Alive);Mode->DisconnectedMatchRecords.Add(Caught.UserId,Caught);
        Mode->ProcessReconnectTimeouts();
        TestTrue(TEXT("Left alive and never came back: forfeit"),Mode->DisconnectedMatchRecords[TEXT("Alive")].bForfeited);
        TestFalse(TEXT("Left after being caught: no forfeit"),Mode->DisconnectedMatchRecords[TEXT("Caught")].bForfeited);
        TestFalse(TEXT("Left after being caught: no longer waited for"),Mode->DisconnectedMatchRecords[TEXT("Caught")].bDisconnected);
        TestTrue(TEXT("Left after being caught: still a loss"),Mode->DisconnectedMatchRecords[TEXT("Caught")].SurvivorState==EState::Eliminated);
        TestTrue(TEXT("The teammate still playing keeps the match going"),Mode->IsAuthoritativeMatchInProgress());
    }

    // A stand-in holds a place only beside a human still playing: when that teammate is caught, whoever it stands
    // in for forfeits at once and the match resolves (the caught teammate's own result is a loss).
    {
        UWorld* World=CreateForfeitWorld();ON_SCOPE_EXIT{World->DestroyWorld(false);};
        auto* Mode=Fresh(World);
        AddPlayer(World,Mode,Person(TEXT("M"),TEXT("Mannequin"),EState::NotApplicable));
        AddPlayer(World,Mode,Person(TEXT("Stay"),TEXT("Survivor"),EState::Eliminated));
        auto Left=Person(TEXT("Left"),TEXT("Survivor"),EState::Active);Left.bDisconnected=true;Left.bReplacedByBot=true;
        Left.ReconnectDeadlineWorldSeconds=World->GetTimeSeconds()+30;Mode->DisconnectedMatchRecords.Add(Left.UserId,Left);
        Mode->CheckForMatchCompletion();
        TestTrue(TEXT("Replaced leaver forfeits once no human is left playing"),Mode->DisconnectedMatchRecords[TEXT("Left")].bForfeited);
        TestFalse(TEXT("And the match no longer runs on the stand-in"),Mode->IsAuthoritativeMatchInProgress());
    }
    return true;
}
#endif

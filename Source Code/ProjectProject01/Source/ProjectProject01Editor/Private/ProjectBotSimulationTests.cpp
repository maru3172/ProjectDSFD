#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "ProjectMatchBots.h"
#include "MultiplayTestGameMode.h"
#include "ProjectOpenObserver.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "PlayInEditorDataTypes.h"
#include "UObject/UnrealType.h"

// On-demand balance runs: whole bot-only matches on the real map (three bots, one headless observer so the
// match may start), time sped up. Not part of the regular suite; run e.g.
//   Automation RunTests ProjectProject01.Simulation.BotMatch.Level5
// and read the "[BotSim]" / "[BotScript]" lines from the log.
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FProjectBotSimulationTest,"ProjectProject01.Simulation.BotMatch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FProjectBotSimulationTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{
    for(int32 Level=1;Level<=5;++Level)for(int32 Run=1;Run<=20;++Run)
    {const FString Name=FString::Printf(TEXT("Level%d.Run%02d"),Level,Run);Names.Add(Name);Commands.Add(Name);}
    // Difficulty ladder: a level-3 side stands in for an average player against each level of the other side.
    // "M%dS3": average survivors vs each mannequin level. "H3S%d": a level-3 mannequin that parks no bodies, like a
    // human mannequin player, vs each survivor level. "P%dS3": one human survivor with a bot partner (one level-3 bot
    // plays the human, its partner stays in helper mode as beside a human) vs each mannequin level.
    for(int32 Level=1;Level<=5;++Level)for(int32 Run=1;Run<=30;++Run)
    {
        const FString AsSurvivor=FString::Printf(TEXT("Ladder.M%dS3.Run%02d"),Level,Run),AsMannequin=FString::Printf(TEXT("Ladder.H3S%d.Run%02d"),Level,Run);
        const FString AsSoloSurvivor=FString::Printf(TEXT("Ladder.P%dS3.Run%02d"),Level,Run);
        Names.Add(AsSurvivor);Commands.Add(AsSurvivor);Names.Add(AsMannequin);Commands.Add(AsMannequin);Names.Add(AsSoloSurvivor);Commands.Add(AsSoloSurvivor);
    }
}
bool FProjectBotSimulationTest::RunTest(const FString& Parameter)
{
    if(!GEditor||GEditor->IsPlaySessionInProgress()){AddError(TEXT("Stop PIE before bot simulation"));return false;}
    // Floors before the last have level-transition exits only; the game mode reports that as an error by design.
    AddExpectedMessage(TEXT("No multiplayer escape TriggerBox"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0);
    // On arrival the mannequin player's first view can come before the bodies exist (also seen on real servers).
    AddExpectedMessage(TEXT("No valid mannequin control slot"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0);
    // "LevelN.RunXX" puts every bot on N; "Ladder.MaSb.RunXX" sets the mannequin to a and both survivors to b.
    const bool Ladder=Parameter.StartsWith(TEXT("Ladder."));
    const int32 Level=Ladder?FMath::Clamp(FCString::Atoi(*Parameter.Mid(8,1)),1,5):FMath::Clamp(FCString::Atoi(*Parameter.Mid(5,1)),1,5);
    const int32 SurvivorLevel=Ladder?FMath::Clamp(FCString::Atoi(*Parameter.Mid(10,1)),1,5):Level;
    ProjectBots::ParkOverride()=Parameter.StartsWith(TEXT("Ladder.H"))?0:-1;
    ProjectBots::HumanTeamOverride()=Parameter.StartsWith(TEXT("Ladder.P"));
    // -BotSimProbe=N: masters take one expert value (1 reaction, 2 sharing, 3 coordination, 4 skill) or one expert
    // table group (5 watch discipline, 6 combo book, 7 play tuning, 8 map/mood/ping tables).
    int32 Probe=0;FParse::Value(FCommandLine::Get(),TEXT("BotSimProbe="),Probe);ProjectBots::Difficulty::Probe()=Probe;
    float Dilation=4.f;FParse::Value(FCommandLine::Get(),TEXT("BotSimDilation="),Dilation);Dilation=FMath::Clamp(Dilation,1.f,8.f);
    // 10 min cap: the rare long stalemates otherwise hold a whole batch; results barely change.
    float LimitSeconds=600.f;FParse::Value(FCommandLine::Get(),TEXT("BotSimLimit="),LimitSeconds);
    // Comparison runs: -BotSimEvolution=2 restores the earlier, slower habit growth (read by the bot adapter).
    const FString Previous=GEditor->UserEditedPlayWorldURL;
    GEditor->UserEditedPlayWorldURL=TEXT("/Game/MyProject/Level/MultiplayTest?LobbyRole=Survivor");
    auto Request=UProjectProject01TutorialEditorLibrary::MakePracticeRequest();
    Request.GameModeOverride=AMultiplayTestGameMode::StaticClass();
    Request.GlobalMapOverride=TEXT("/Game/MyProject/Level/MultiplayTest"); // The real first floor, not the practice map.
    GEditor->RequestPlaySession(Request);GEditor->StartQueuedPlaySessionRequest();GEditor->UserEditedPlayWorldURL=Previous;
    struct FState{bool Initialized=false;FString Match;double WallStart=0;TWeakObjectPtr<AProjectOpenObserverController> Observer;};
    const auto State=MakeShared<FState>();State->WallStart=FPlatformTime::Seconds();
    const double Deadline=FPlatformTime::Seconds()+90+LimitSeconds/Dilation;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Deadline,Level,SurvivorLevel,Dilation,Parameter]()
    {
        auto End=[this]()
        {
            GEditor->RequestEndPlayMap();const double Stop=FPlatformTime::Seconds()+20;
            AddCommand(new FFunctionLatentCommand([this,Stop](){if(!GEditor->IsPlaySessionInProgress())return true;if(FPlatformTime::Seconds()<Stop)return false;AddError(TEXT("Simulation PIE failed to stop"));return true;}));
        };
        UWorld* World=GEditor->PlayWorld;auto* Mode=World?World->GetAuthGameMode<AMultiplayTestGameMode>():nullptr;
        if(FPlatformTime::Seconds()>Deadline)
        {
            // Out of time: record it as unfinished rather than failing the run.
            UE_LOG(LogTemp,Display,TEXT("[BotSim] Run=%s Level=%d Result=Unfinished WallSeconds=%.0f"),*Parameter,Level,FPlatformTime::Seconds()-State->WallStart);
            End();return true;
        }
        if(!Mode||!World->HasBegunPlay()||UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return false;
        // Each floor is a fresh world after travel: keep the time sped up.
        if(auto* Settings=World->GetWorldSettings();Settings&&!FMath::IsNearlyEqual(Settings->TimeDilation,Dilation))Settings->SetTimeDilation(Dilation);
        if(!State->Initialized)
        {
            auto* Human=Cast<AMultiplayTestPlayerController>(UGameplayStatics::GetPlayerController(World,0));
            auto* Instance=World->GetGameInstance<UProjectProject01GameInstance>();
            auto* Address=FindFProperty<FStrProperty>(UProjectProject01GameInstance::StaticClass(),TEXT("SecurityApiBaseUrl"));
            auto* Local=Human?Cast<ULocalPlayer>(Human->Player):nullptr;
            if(!Human||!Instance||!Address||!Local){AddError(TEXT("Simulation needs the PIE local player"));End();return true;}
            Address->SetPropertyValue_InContainer(Instance,FString()); // Never report simulated results to a backend.
            auto* Observer=World->SpawnActor<AProjectOpenObserverController>();if(!Observer){AddError(TEXT("Observer spawn failed"));End();return true;}
            Mode->MatchPlayerRecords.Reset();Mode->AuthoritativeMatchId.Reset();
            Local->SwitchController(Observer);Human->Destroy();
            const FString Room=FGuid::NewGuid().ToString();State->Match=FGuid::NewGuid().ToString();
            FProjectProject01ValidatedJoinClaim Claim;Claim.UserId=TEXT("Simulation.Observer");Claim.DisplayName=TEXT("관전자");Claim.Role=TEXT("Observer");Claim.MatchId=State->Match;Claim.RoomId=Room;
            Observer->SetAuthenticatedLobbyIdentity(Claim);Observer->Admit();State->Observer=Observer;
            TArray<FProjectMatchBotSpec> Specs;
            // Solo stand-in: bot 3 is the helper partner (named so the bot code can tell), bot 2 plays the human.
            for(int32 I=0;I<3;++I)Specs.Add({FGuid::NewGuid().ToString(),I==2&&ProjectBots::HumanTeamOverride()?FString(TEXT("SimHelper")):FString::Printf(TEXT("봇 %d"),I+1),I==0?TEXT("Mannequin"):TEXT("Survivor"),I==0?Level:SurvivorLevel});
            FString Error;if(!Mode->MatchBots->AcceptRoster(Room,State->Match,Specs,Error)){AddError(Error);End();return true;}
            State->Initialized=true;return false;
        }
        const auto Phase=Mode->AuthoritativeMatchPhase;
        if(Phase!=EMultiplayTestMatchPhase::Ending&&Phase!=EMultiplayTestMatchPhase::Results)return false;
        int32 Escaped=0,Caught=0;
        for(const auto& Pair:Mode->MatchPlayerRecords)if(Pair.Value.Role==TEXT("Survivor"))
        {Escaped+=Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Escaped;Caught+=Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Eliminated;}
        // Each survivor's own outcome: in the solo stand-in, which of the two (the helper or the one playing the human) got out.
        FString Each;
        for(const auto& Pair:Mode->MatchPlayerRecords)if(Pair.Value.Role==TEXT("Survivor"))
            Each+=FString::Printf(TEXT(" %s=%s@%.0f"),Pair.Value.DisplayName==TEXT("SimHelper")?TEXT("Helper"):TEXT("Lead"),
                Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Escaped?TEXT("Escaped"):Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Eliminated?TEXT("Caught"):TEXT("Other"),
                Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Escaped?Pair.Value.EscapeSeconds:Pair.Value.FirstCaptureSeconds);
        const auto Script=Mode->MatchBots->ScriptOf();
        const TCHAR* Result=Escaped==2?TEXT("BothEscaped"):Escaped==1?TEXT("OneEscaped"):TEXT("MannequinWin");
        UE_LOG(LogTemp,Display,TEXT("[BotSim] Run=%s Level=%d SurvivorLevel=%d Mannequin=%s Survivors=%s Result=%s Escaped=%d Caught=%d MatchSeconds=%.0f Floor=%d%s"),*Parameter,Level,SurvivorLevel,
            ANSI_TO_TCHAR(ProjectBots::MatchScript::Name(Script.Mannequin)),ANSI_TO_TCHAR(ProjectBots::MatchScript::Name(Script.Survivors)),Result,Escaped,Caught,
            Mode->GetElapsedMatchSeconds(),Mode->MatchBots->FloorDepth,*Each);
        End();return true;
    }));
    return true;
}
#endif

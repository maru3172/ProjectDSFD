#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ProjectMultiplayerRescue.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

// Rescue rules (user, 2026-11-28): who may be held for rescue and who may free them.
// A person needs another person; a bot is freed by a person if one is in play, else by its bot partner; bots never
// rescue while a person is a survivor in play.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectRescueRulesTest,"ProjectProject01.Review.RescueRules",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectRescueRulesTest::RunTest(const FString&)
{
    struct FTeam{UWorld* World=nullptr;AProjectMultiplayerRescueGameMode* Mode=nullptr;TArray<AMultiplayTestPlayerController*> PCs;TArray<APlayerCharacter*> Pawns;};
    auto Make=[this](std::initializer_list<bool> Bots,FTeam& Team)
    {
        const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        Team.World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Team.World);Team.World->SetGameInstance(NewObject<UGameInstance>());
        FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.ProjectMultiplayerRescueGameMode"));
        if(!TestTrue(TEXT("Rescue mode fixture"),Team.World->SetGameMode(URL)))return false;
        Team.Mode=Team.World->GetAuthGameMode<AProjectMultiplayerRescueGameMode>();if(!TestNotNull(TEXT("Rescue mode"),Team.Mode))return false;
        Team.Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::InProgress;
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        int32 Index=0;
        for(const bool Bot:Bots)
        {
            auto* PC=Team.World->SpawnActor<AMultiplayTestPlayerController>();PC->bServerControlledBot=Bot;
            auto* Pawn=Team.World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(300.f*++Index,0,0)),Spawn);PC->Possess(Pawn);
            FMultiplayTestServerPlayerRecord Record;Record.UserId=FString::Printf(TEXT("S%d"),Index);Record.Role=TEXT("Survivor");
            Record.SurvivorState=EMultiplayTestSurvivorState::Active;Record.bServerBot=Bot;Record.SurvivorPawn=Pawn;
            Team.Mode->MatchPlayerRecords.Add(PC,Record);Team.PCs.Add(PC);Team.Pawns.Add(Pawn);
        }
        return true;
    };
    auto Drop=[](FTeam& Team){if(Team.World){GEngine->DestroyWorldContext(Team.World);Team.World->DestroyWorld(false);}};

    {   // One person and the helper bot.
        FTeam T;ON_SCOPE_EXIT{Drop(T);};if(!Make({false,true},T))return false;
        TestFalse(TEXT("Person + helper: the person caught is out (a bot never comes for a person)"),T.Mode->HasRescuerFor(T.PCs[0]));
        TestTrue(TEXT("Person + helper: the helper caught is held, the person can free it"),T.Mode->HasRescuerFor(T.PCs[1]));
        TestTrue(TEXT("Person + helper: the person may rescue"),T.Mode->MayRescue(*T.Pawns[0]));
        TestFalse(TEXT("Person + helper: the helper never rescues while a person plays"),T.Mode->MayRescue(*T.Pawns[1]));
    }
    {   // Two survivor bots, no person.
        FTeam T;ON_SCOPE_EXIT{Drop(T);};if(!Make({true,true},T))return false;
        TestTrue(TEXT("Bots only: a caught bot is held for its partner"),T.Mode->HasRescuerFor(T.PCs[0]));
        TestTrue(TEXT("Bots only: the partner bot may rescue"),T.Mode->MayRescue(*T.Pawns[1]));
        T.Pawns[1]->SetAwaitingMultiplayerRescue(true);
        TestFalse(TEXT("Bots only: with the partner already held, nobody is left to come"),T.Mode->HasRescuerFor(T.PCs[0]));
        T.Pawns[1]->SetAwaitingMultiplayerRescue(false);
        T.Mode->MatchPlayerRecords[T.PCs[1]].SurvivorState=EMultiplayTestSurvivorState::Eliminated;
        TestFalse(TEXT("Bots only: with the partner out, the caught bot is out too"),T.Mode->HasRescuerFor(T.PCs[0]));
    }
    {   // Two people: unchanged.
        FTeam T;ON_SCOPE_EXIT{Drop(T);};if(!Make({false,false},T))return false;
        TestTrue(TEXT("Two people: a caught person is held for the other"),T.Mode->HasRescuerFor(T.PCs[0]));
        TestTrue(TEXT("Two people: either may rescue"),T.Mode->MayRescue(*T.Pawns[1]));
    }
    return true;
}
#endif

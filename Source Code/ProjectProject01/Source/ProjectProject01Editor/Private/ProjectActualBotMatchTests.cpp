#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "ProjectMatchBots.h"
#include "MultiplayTestGameMode.h"
#include "MannequinAICharacter.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "PlayInEditorDataTypes.h"
#include "Engine/TriggerBox.h"
#include "Components/BoxComponent.h"
#include "UObject/UnrealType.h"
#include "ProjectOpenObserver.h"
#include "GameFramework/SpectatorPawn.h"
#include "Engine/LocalPlayer.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/GameStateBase.h"
#include "Components/SkeletalMeshComponent.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FProjectActualBotMatchEditorTest,"ProjectProject01.Editor.ActualBotMatch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FProjectActualBotMatchEditorTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{
    for(const TCHAR* Role:{TEXT("Survivor"),TEXT("Mannequin")})for(int32 Level=1;Level<=5;++Level)
    {const FString Name=FString::Printf(TEXT("%s.Level%d"),Role,Level);Names.Add(Name);Commands.Add(Name);}
    Names.Add(TEXT("Observer.Free"));Commands.Add(TEXT("Observer.Free"));
    Names.Add(TEXT("Observer.Locked"));Commands.Add(TEXT("Observer.Locked"));
}
bool FProjectActualBotMatchEditorTest::RunTest(const FString& Parameter)
{
    if(!GEditor||GEditor->IsPlaySessionInProgress()){AddError(TEXT("Stop PIE before bot match tests"));return false;}
    const bool Mannequin=Parameter.StartsWith(TEXT("Mannequin"));const int32 Level=FCString::Atoi(*Parameter.Right(1));
    const bool ObserverCase=Parameter.StartsWith(TEXT("Observer")),Locked=Parameter.EndsWith(TEXT("Locked"));
    const FString Role=Mannequin?TEXT("Mannequin"):TEXT("Survivor");
    const FString Previous=GEditor->UserEditedPlayWorldURL;
    GEditor->UserEditedPlayWorldURL=TEXT("/Game/MyProject/Level/MultiplayerTutorial?LobbyRole=")+Role;
    auto Request=UProjectProject01TutorialEditorLibrary::MakePracticeRequest();
    // Reuse safe test geometry, but execute the REAL match mode, not tutorial/practice AI.
    Request.GameModeOverride=AMultiplayTestGameMode::StaticClass();
    // The tutorial asset has its own objective volume. Supply the real match's escape
    // trigger only in the unsaved PIE copy; never change or save the authored map.
    const auto Fixture=FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* World,const UWorld::InitializationValues&)
    {
        if(!World||World->WorldType!=EWorldType::PIE)return;
        if(auto* Exit=World->SpawnActor<ATriggerBox>())
        {Exit->Tags.Add(TEXT("ProjectProject01Escape"));Exit->SetActorLocation(FVector(1550,0,130));CastChecked<UBoxComponent>(Exit->GetCollisionComponent())->SetBoxExtent(FVector(180,230,150));}
    });
    GEditor->RequestPlaySession(Request);GEditor->StartQueuedPlaySessionRequest();GEditor->UserEditedPlayWorldURL=Previous;
    FWorldDelegates::OnPostWorldInitialization.Remove(Fixture);
    struct FState{bool Initialized=false,SawInProgress=false,CheckedTransition=false;double Started=0;FString Room,Match;TMap<FString,FVector> Initial;TWeakObjectPtr<AProjectOpenObserverController> Observer;};
    const auto State=MakeShared<FState>();const double Deadline=FPlatformTime::Seconds()+60;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Deadline,Role,Level,Mannequin,ObserverCase,Locked]()
    {
        auto End=[this]()
        {
            GEditor->RequestEndPlayMap();const double Stop=FPlatformTime::Seconds()+15;
            AddCommand(new FFunctionLatentCommand([this,Stop](){if(!GEditor->IsPlaySessionInProgress())return true;if(FPlatformTime::Seconds()<Stop)return false;AddError(TEXT("Bot match PIE failed to stop"));return true;}));
        };
        if(FPlatformTime::Seconds()>Deadline){AddError(TEXT("Actual bot match startup timeout"));End();return true;}
        UWorld* World=GEditor->PlayWorld;auto* Mode=World?World->GetAuthGameMode<AMultiplayTestGameMode>():nullptr;
        auto* Human=World?Cast<AMultiplayTestPlayerController>(UGameplayStatics::GetPlayerController(World,0)):nullptr;
        if(!Mode||!Human||!World->HasBegunPlay()||UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return false;
        if(!State->Initialized)
        {
            // This is an in-memory execution test, not a production result submission.
            // Clear only this transient PIE instance; never save or change project/operator settings.
            auto* Instance=World->GetGameInstance<UProjectProject01GameInstance>();
            auto* Address=FindFProperty<FStrProperty>(UProjectProject01GameInstance::StaticClass(),TEXT("SecurityApiBaseUrl"));
            if(!Instance||!Address){AddError(TEXT("Cannot isolate bot test backend traffic"));End();return true;}
            Address->SetPropertyValue_InContainer(Instance,FString());
            if(ObserverCase)
            {
                auto* Local=Cast<ULocalPlayer>(Human->Player);if(!Local){AddError(TEXT("Observer fixture needs the PIE local player"));End();return true;}
                auto* Observer=World->SpawnActor<AProjectOpenObserverController>();
                if(!Observer){AddError(TEXT("Observer controller spawn failed"));End();return true;}
                Mode->MatchPlayerRecords.Reset();Mode->AuthoritativeMatchId.Reset();
                Local->SwitchController(Observer);Human->Destroy();
                State->Room=FGuid::NewGuid().ToString();State->Match=FGuid::NewGuid().ToString();
                FProjectProject01ValidatedJoinClaim Claim;Claim.UserId=TEXT("ObserverTest.Human");Claim.DisplayName=TEXT("관전자");Claim.Role=TEXT("Observer");Claim.MatchId=State->Match;Claim.RoomId=State->Room;
                Observer->SetAuthenticatedLobbyIdentity(Claim);Observer->Admit();State->Observer=Observer;
                TestTrue(TEXT("Observer receives a model-less free-flight pawn"),Observer->GetPawn()&&Observer->GetPawn()->IsA<ASpectatorPawn>()&&Observer->GetPawn()->IsHidden());
                Mode->RegisterMatchPlayer(Observer);TestEqual(TEXT("Observer cannot become a playing participant"),Mode->MatchPlayerRecords.Num(),0);
                TArray<FProjectMatchBotSpec> Specs;
                for(int32 I=0;I<3;++I)Specs.Add({FGuid::NewGuid().ToString(),FString::Printf(TEXT("봇 %d"),I+1),I==0?TEXT("Mannequin"):TEXT("Survivor"),1});
                FString Error;TestTrue(TEXT("Three bots can play while a human only observes"),Mode->MatchBots->AcceptRoster(State->Room,State->Match,Specs,Error));
                State->Initialized=true;return false;
            }
            TestTrue(TEXT("Actual multiplayer mode, never the practice mode"),Mode->GetClass()==AMultiplayTestGameMode::StaticClass());
            State->Room=FGuid::NewGuid().ToString();State->Match=FGuid::NewGuid().ToString();
            FProjectProject01ValidatedJoinClaim Claim;Claim.UserId=TEXT("BotTest.Human");Claim.MatchId=State->Match;Claim.RoomId=State->Room;Claim.Role=Role;Claim.DisplayName=TEXT("플레이어");
            Human->SetAuthenticatedLobbyIdentity(Claim);Mode->MatchPlayerRecords.Reset();Mode->AuthoritativeMatchId.Reset();Mode->RegisterMatchPlayer(Human);
            FProjectMatchBotSpec A{FGuid::NewGuid().ToString(),TEXT("봇 1"),Mannequin?TEXT("Survivor"):TEXT("Mannequin"),Level};
            FProjectMatchBotSpec B{FGuid::NewGuid().ToString(),TEXT("봇 2"),TEXT("Survivor"),Level};FString Error;
            TestTrue(TEXT("Server accepts trusted match roster"),Mode->MatchBots->AcceptRoster(State->Room,State->Match,{A,B},Error));
            State->Initialized=true;State->Started=FPlatformTime::Seconds();return false;
        }
        auto* Adapter=Mode->MatchBots.Get();
        if(!Adapter->Spawned)return false;
        if(ObserverCase)
        {
            auto* Observer=State->Observer.Get();if(!Observer){AddError(TEXT("Observer was lost"));End();return true;}
            TestTrue(TEXT("Observer-only room starts a real match"),Mode->IsAuthoritativeMatchInProgress());
            TestEqual(TEXT("Three playing slots exclude the observer"),Mode->MatchPlayerRecords.Num(),3);
            TestFalse(TEXT("Observer cannot take mannequin control"),Mode->TryPossessMannequin(Observer,0));
            TestFalse(TEXT("Observer cannot command mannequins"),Mode->TryIssueMannequinCommand(Observer,EMultiplayMannequinCommand::Patrol));
            TestFalse(TEXT("Observer cannot attack"),Mode->TryMannequinAttack(Observer));
            AMultiplayTestPlayerController* TargetController=nullptr;APlayerCharacter* Target=nullptr;FString TargetId;
            for(const auto& Pair:Mode->MatchPlayerRecords)if(Pair.Value.Role==TEXT("Survivor")){TargetController=Pair.Key.Get();Target=Pair.Value.SurvivorPawn.Get();TargetId=Pair.Value.UserId;break;}
            TestNotNull(TEXT("An actual survivor can be selected"),Target);if(!Target){End();return true;}
            Observer->Select(TargetId,Locked);Observer->TickActor(.1f,LEVELTICK_All,Observer->PrimaryActorTick);
            TestTrue(TEXT("Selected first-person actor is the real survivor"),Observer->GetObservedActor()==Target&&Observer->GetViewTarget()==Target);
            TestTrue(TEXT("Following cannot move the free-flight pawn"),Observer->IsMoveInputIgnored());
            if(Locked)
            {Observer->PlayerTick(1);TestTrue(TEXT("Locked mode receives the selected bot look direction"),Observer->GetControlRotation().Equals(Target->GetActorRotation(),.5f));}
            else
            {TestFalse(TEXT("Following without the player's view turns the camera with the mouse"),Observer->IsLookInputIgnored());
             Observer->SetControlRotation(FRotator(0,113,0));Observer->PlayerTick(.01f);TestTrue(TEXT("Free-look does not rotate the living target"),!Observer->GetControlRotation().Equals(TargetController->GetControlRotation(),.5f));}
            World->Tick(LEVELTICK_All,.1f);
            Observer->Select(TEXT("Not-A-Participant"),false);Observer->TickActor(.1f,LEVELTICK_All,Observer->PrimaryActorTick);
            TestTrue(TEXT("Unknown player selection cannot change the target"),Observer->GetObservedActor()==Target);
            Observer->Select(FString(),false);Observer->TickActor(.1f,LEVELTICK_All,Observer->PrimaryActorTick);
            TestTrue(TEXT("All returns to a movable free-flight pawn"),Observer->GetViewTarget()==Observer->GetPawn()&&!Observer->IsMoveInputIgnored());
            TestNull(TEXT("All leaves the participant camera"),Observer->GetObservedActor());
            if(Locked)
            {
                AController* Travelling=Observer;Mode->HandleSeamlessTravelPlayer(Travelling);
                auto* Arrived=Cast<AProjectOpenObserverController>(Travelling);
                TestNotNull(TEXT("Engine controller swap preserves observer type on arrival"),Arrived);
                if(Arrived){TestEqual(TEXT("Observer keeps its authenticated role through the swap"),Arrived->GetAuthenticatedRole(),FString(TEXT("Observer")));TestNotNull(TEXT("Arrival restores the free-flight pawn"),Arrived->GetPawn().Get());}
                TestEqual(TEXT("Travel cannot register the observer as a survivor"),Mode->MatchPlayerRecords.Num(),3);
            }
            End();return true;
        }
        State->SawInProgress|=Mode->IsAuthoritativeMatchInProgress();
        if(!State->CheckedTransition&&Mode->IsAuthoritativeMatchInProgress())
        {
            Mode->PrepareLevelTransitionTravel(0);
            for(const auto& Pair:Adapter->Controllers)if(auto* PC=Pair.Value.Get())
                TestTrue(TEXT("Bots acknowledge black transition without a client RPC round-trip"),PC->IsLevelTransitionBlackReady());
            Mode->CancelLevelTransitionTravel();State->CheckedTransition=true;
        }
        if(State->Initial.IsEmpty())for(const auto& Pair:Adapter->Controllers)
            if(auto* PC=Pair.Value.Get())if(APawn* Pawn=PC->GetPawn())State->Initial.Add(Pair.Key,Pawn->GetActorLocation());
        if(FPlatformTime::Seconds()-State->Started<5)return false;
        TestFalse(TEXT("Actual native bot creation succeeds"),Adapter->Failed);
        TestEqual(TEXT("Two server-owned bots and one human"),Mode->MatchPlayerRecords.Num(),3);
        TestTrue(TEXT("Actual game phase starts with complete playing roster"),State->SawInProgress);
        TestTrue(TEXT("Transition handshake was checked before natural match completion"),State->CheckedTransition);
        int32 HumanCount=0,Survivors=0,Mannequins=0;
        for(const auto& Pair:Mode->MatchPlayerRecords)
        {
            const auto& Record=Pair.Value;HumanCount+=!Record.bServerBot;Survivors+=Record.Role==TEXT("Survivor");Mannequins+=Record.Role==TEXT("Mannequin");
            if(!Record.bServerBot)continue;
            auto* PC=Pair.Key.Get();TestNotNull(TEXT("Registered bot controller survives"),PC);
            if(!PC)continue;
            TestFalse(TEXT("Server bot never becomes a local client"),PC->IsLocalController());
            TestEqual(TEXT("Bot has the authenticated match identity"),PC->GetAuthenticatedMatchId(),State->Match);
            if(Record.Role==TEXT("Survivor"))TestTrue(TEXT("Actual survivor body and authoritative movement component"),PC->GetPawn()&&PC->GetPawn()->IsA<AProjectMatchBotCharacter>());
            else TestNotNull(TEXT("Mannequin director selects an actual numbered body"),PC->GetViewedMannequin());
        }
        TestEqual(TEXT("No synthetic login users"),HumanCount,1);TestEqual(TEXT("Exactly one mannequin"),Mannequins,1);TestEqual(TEXT("Exactly two survivors"),Survivors,2);
        {
            // A bot body created the way a client receives it (no server-side mesh copy) must still be visible.
            const auto* Defaults=World->GetGameState()?World->GetGameState()->GameModeClass->GetDefaultObject<AGameModeBase>():nullptr;
            const auto* Template=Defaults&&Defaults->DefaultPawnClass?Cast<ACharacter>(Defaults->DefaultPawnClass->GetDefaultObject()):nullptr;
            FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Fresh=World->SpawnActor<AProjectMatchBotCharacter>(AProjectMatchBotCharacter::StaticClass(),FVector(0,0,5000),FRotator::ZeroRotator,Params);
            TestTrue(TEXT("Bot body wears the default survivor look on its own"),Template&&Fresh&&Fresh->GetMesh()->GetSkeletalMeshAsset()==Template->GetMesh()->GetSkeletalMeshAsset());
            if(Fresh)Fresh->Destroy();
        }
        if(!Mannequin)
        {
            // Survivor bot match: the player's elimination ends the match, and no bot may start a level travel meanwhile.
            // The only human taking the exit moves everyone on; the bot survivors still in play come along.
            for(auto& Pair:Mode->MatchPlayerRecords)if(!Pair.Value.bServerBot)Pair.Value.SurvivorState=EMultiplayTestSurvivorState::TransitionWaiting;
            TestTrue(TEXT("When the only human takes the exit, everyone travels"),Mode->CanCommitLevelTransition());
            APlayerCharacter* BotSurvivor=nullptr;
            for(auto& Pair:Mode->MatchPlayerRecords)
            {
                if(!Pair.Value.bServerBot)Pair.Value.SurvivorState=EMultiplayTestSurvivorState::Eliminated;
                else if(Pair.Value.Role==TEXT("Survivor"))BotSurvivor=Pair.Value.SurvivorPawn.Get();
            }
            TestTrue(TEXT("The eliminated player leaves no human survivor in the bot match"),Mode->AreAllHumanSurvivorsEliminatedInBotMatch());
            for(auto& Pair:Mode->MatchPlayerRecords)if(Pair.Value.bServerBot&&Pair.Value.Role==TEXT("Survivor"))Pair.Value.SurvivorState=EMultiplayTestSurvivorState::TransitionWaiting;
            TestFalse(TEXT("A bot already waiting at the exit cannot carry the travel on without the player"),Mode->CanCommitLevelTransition());
            for(auto& Pair:Mode->MatchPlayerRecords)if(Pair.Value.bServerBot&&Pair.Value.Role==TEXT("Survivor"))Pair.Value.SurvivorState=EMultiplayTestSurvivorState::Active;
            TestFalse(TEXT("A bot reaching the exit cannot travel without the player"),BotSurvivor&&Mode->MarkSurvivorWaitingForLevelTransition(BotSurvivor));
            Mode->CheckForMatchCompletion();
            TestFalse(TEXT("The bot match ends right away"),Mode->IsAuthoritativeMatchInProgress());
        }
        else
        {
            TestFalse(TEXT("A human mannequin keeps the normal match rules"),Mode->AreAllHumanSurvivorsEliminatedInBotMatch());
            TestFalse(TEXT("A human mannequin never triggers the bot survivors' level travel"),Mode->CanCommitLevelTransition());
        }
        TestTrue(TEXT("Live adapter creates policy brains"),!Adapter->Brains.IsEmpty());
        for(const auto& Pair:Adapter->Brains)
        {
            TestEqual(TEXT("Selected level preserved at runtime"),Pair.Value.Policy.Profile.Level,Level);
            if(Level<4)TestTrue(TEXT("No low-level deceptive tactics"),Pair.Value.Policy.Current.Tactic!=ProjectBots::Action::Lure);
        }
        End();return true;
    }));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ProjectCustomRoomRulesWidget.h"
#include "ProjectItemRoguelikeRuntime.h"
#include "AIController.h"
#include "TimerManager.h"
#include "MultiplayTestGameMode.h"
#include "PlayerCharacter.h"
#include "MannequinAICharacter.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectCaseRecords.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesTest,"ProjectProject01.CustomRules.AuthorityAndRuntime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesTest::RunTest(const FString&)
{
    FProjectCustomMatchRules Rules;Rules.bEnabled=true;Rules.DeathCount=6;Rules.MannequinCount=4;Rules.TimeLimitSeconds=300;
    auto Json=Rules.ToJson();FProjectCustomMatchRules Parsed;
    TestTrue(TEXT("Rules cross the trusted JSON boundary unchanged"),FProjectCustomMatchRules::Parse(Json,Parsed)&&Parsed==Rules);
    for(double Bad:{0.,21.,1.5}){Json->SetNumberField(TEXT("deathCount"),Bad);TestFalse(TEXT("Invalid death counts rejected atomically"),FProjectCustomMatchRules::Parse(Json,Parsed));TestTrue(TEXT("Failed parse retains last valid rules"),Parsed==Rules);}
    Json=Rules.ToJson();Json->SetNumberField(TEXT("mannequinCount"),11);TestFalse(TEXT("Number-key roster cannot exceed ten"),FProjectCustomMatchRules::Parse(Json,Parsed));
    Json=Rules.ToJson();Json->SetNumberField(TEXT("timeLimitSeconds"),61);TestFalse(TEXT("Fractional minutes rejected"),FProjectCustomMatchRules::Parse(Json,Parsed));
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};World->SetGameInstance(NewObject<UGameInstance>());
    FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));if(!TestTrue(TEXT("Authoritative rules fixture"),World->SetGameMode(URL)))return false;
    auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();FString Error,Match=FGuid::NewGuid().ToString();
    TestFalse(TEXT("No match identity cannot select rules"),Mode->AcceptCustomRules(TEXT(""),Rules,Error));
    TestTrue(TEXT("Trusted first admission pins custom rules"),Mode->AcceptCustomRules(Match,Rules,Error));
    TestEqual(TEXT("Custom roster count reaches spawn system"),Mode->MannequinSpawnCount,4);TestEqual(TEXT("Custom limit reaches authoritative timeout"),Mode->MatchTimeLimitSeconds,300.f);
    TestTrue(TEXT("Further claims are idempotent"),Mode->AcceptCustomRules(Match,Rules,Error));
    auto Changed=Rules;Changed.DeathCount=1;TestFalse(TEXT("Later player cannot change the frozen rules"),Mode->AcceptCustomRules(Match,Changed,Error));
    TestFalse(TEXT("Another match cannot reuse an occupied world"),Mode->AcceptCustomRules(FGuid::NewGuid().ToString(),Rules,Error));
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Survivor=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(0,0,500)),Spawn);Survivor->DispatchBeginPlay();
    TestEqual(TEXT("Spawned survivor uses selected death count after tuning"),Survivor->GetRemainingDeathCount(),6);
    for(int I=0;I<6;++I)World->SpawnActor<AMannequinAICharacter>(AMannequinAICharacter::StaticClass(),FTransform(FVector(I*300,1000,500)),Spawn);
    AddExpectedError(TEXT("Keeping legacy mannequin transforms"),EAutomationExpectedErrorFlags::Contains,1);Mode->InitializeMannequinSpawns();
    int32 Active=0;for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed()){++Active;TestTrue(TEXT("Remaining control slots stay in range"),It->GetControlSlot()>=0&&It->GetControlSlot()<4);}
    TestEqual(TEXT("Extra mannequins removed for custom count on legacy maps too"),Active,4);
    FMultiplayTestServerPlayerRecord Record;Record.Role=TEXT("Survivor");Record.SurvivorState=EMultiplayTestSurvivorState::Escaped;
    auto Result=Mode->BuildFinalResult(nullptr,Record);TestTrue(TEXT("Results preserve success and custom identity"),Result.bSuccess&&Result.bCustomMatch);TestFalse(TEXT("Custom result never offers official registration"),Result.bLeaderboardVerificationReady);
    FMultiplayTestMatchCarryover Carry;Carry.MatchId=Match;Carry.CustomRules=Rules;
    auto* Next=World->SpawnActor<AMultiplayTestGameMode>();TestTrue(TEXT("Next floor accepts carried rules"),Next->AcceptCustomRules(Carry.MatchId,Carry.CustomRules,Error));TestEqual(TEXT("Next floor keeps selected limit"),Next->MatchTimeLimitSeconds,300.f);
    auto* Standard=World->SpawnActor<AMultiplayTestGameMode>();Standard->MannequinSpawnCount=7;Standard->MatchTimeLimitSeconds=900;
    TestTrue(TEXT("Standard admission still accepted"),Standard->AcceptCustomRules(FGuid::NewGuid().ToString(),FProjectCustomMatchRules(),Error));
    TestEqual(TEXT("Standard maps retain their authored roster"),Standard->MannequinSpawnCount,7);TestEqual(TEXT("Standard maps retain their authored limit"),Standard->MatchTimeLimitSeconds,900.f);
    return true;
}

// Docs/CustomGamePlayground.md 2.1-2.2: every playground field is required, ranges and combinations are checked, and a
// rule whose runtime is not live yet must stay off.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesPlaygroundParseTest,"ProjectProject01.CustomRules.PlaygroundParse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesPlaygroundParseTest::RunTest(const FString&)
{
    FProjectCustomMatchRules Base;Base.bEnabled=true;Base.DeathCount=3;Base.MannequinCount=6;Base.TimeLimitSeconds=900;
    FProjectCustomMatchRules Parsed;
    TestTrue(TEXT("Rules without playground switches round-trip"),FProjectCustomMatchRules::Parse(Base.ToJson(),Parsed)&&Parsed==Base);
    for(const TCHAR* Key:{TEXT("affixIds"),TEXT("itemCards"),TEXT("infiniteLives"),TEXT("infection"),TEXT("ghosts"),TEXT("hideRevealSeconds")})
    {auto J=Base.ToJson();J->RemoveField(Key);TestFalse(FString(TEXT("Missing field rejected: "))+Key,FProjectCustomMatchRules::Parse(J,Parsed));}
    {auto J=Base.ToJson();J->SetNumberField(TEXT("hideRevealSeconds"),40.5);TestFalse(TEXT("Fractional reveal seconds rejected"),FProjectCustomMatchRules::Parse(J,Parsed));}
    {auto J=Base.ToJson();TArray<TSharedPtr<FJsonValue>> V{MakeShared<FJsonValueNumber>(1)};J->SetArrayField(TEXT("affixIds"),V);TestFalse(TEXT("Non-string affix id rejected"),FProjectCustomMatchRules::Parse(J,Parsed));}

    auto Shaped=[&Base](TFunction<void(FProjectCustomMatchRules&)> Edit){FProjectCustomMatchRules R=Base;Edit(R);return R;};
    TestTrue(TEXT("Infinite lives with a time limit is well formed"),Shaped([](auto& R){R.bInfiniteLives=true;}).IsWellFormed());
    TestFalse(TEXT("Infinite lives needs at least five minutes"),Shaped([](auto& R){R.bInfiniteLives=true;R.TimeLimitSeconds=240;}).IsWellFormed());
    TestFalse(TEXT("Infinite lives cannot run without a time limit"),Shaped([](auto& R){R.bInfiniteLives=true;R.TimeLimitSeconds=0;}).IsWellFormed());
    TestFalse(TEXT("Ghosts never combine with infection"),Shaped([](auto& R){R.bGhosts=true;R.bInfection=true;}).IsWellFormed());
    TestFalse(TEXT("Ghosts never combine with infinite lives"),Shaped([](auto& R){R.bGhosts=true;R.bInfiniteLives=true;}).IsWellFormed());
    TestTrue(TEXT("Infection with infinite lives is the zombie combination"),Shaped([](auto& R){R.bInfection=true;R.bInfiniteLives=true;}).IsWellFormed());
    TestFalse(TEXT("Infection caps the mannequin roster at eight"),Shaped([](auto& R){R.bInfection=true;R.MannequinCount=9;}).IsWellFormed());
    for(int32 Bad:{10,19,121,42})TestFalse(FString::Printf(TEXT("Reveal seconds %d rejected"),Bad),Shaped([Bad](auto& R){R.HideRevealSeconds=Bad;}).IsWellFormed());
    for(int32 Good:{20,40,120})TestTrue(FString::Printf(TEXT("Reveal seconds %d accepted"),Good),Shaped([Good](auto& R){R.HideRevealSeconds=Good;}).IsWellFormed());
    TestTrue(TEXT("Six affixes, at most three per group"),Shaped([](auto& R){R.AffixIds={TEXT("A01"),TEXT("A02"),TEXT("B09"),TEXT("B10"),TEXT("C01"),TEXT("C05")};}).IsWellFormed());
    TestFalse(TEXT("Seven affixes rejected"),Shaped([](auto& R){R.AffixIds={TEXT("A01"),TEXT("A02"),TEXT("B09"),TEXT("B10"),TEXT("C01"),TEXT("C05"),TEXT("C06")};}).IsWellFormed());
    TestFalse(TEXT("Four affixes in one group rejected"),Shaped([](auto& R){R.AffixIds={TEXT("B01"),TEXT("B02"),TEXT("B03"),TEXT("B04")};}).IsWellFormed());
    TestFalse(TEXT("Duplicate affix rejected"),Shaped([](auto& R){R.AffixIds={TEXT("B09"),TEXT("B09")};}).IsWellFormed());
    TestFalse(TEXT("Unknown affix rejected"),Shaped([](auto& R){R.AffixIds={TEXT("Z99")};}).IsWellFormed());
    FProjectCustomMatchRules Off;Off.bItemCards=true;TestFalse(TEXT("Disabled rules carry no playground switches"),Off.IsWellFormed());

    // Runtime switches: a well-formed rule is still refused until its game-server step lands.
    using namespace ProjectPlaygroundRules;
    const TPair<FProjectCustomMatchRules,bool> Gated[]={
        {Shaped([](auto& R){R.AffixIds={TEXT("B09")};}),bAffixesLive},{Shaped([](auto& R){R.bItemCards=true;}),bItemCardsLive},
        {Shaped([](auto& R){R.bInfiniteLives=true;}),bInfiniteLivesLive},{Shaped([](auto& R){R.bInfection=true;}),bInfectionLive},
        {Shaped([](auto& R){R.bGhosts=true;}),bGhostsLive},{Shaped([](auto& R){R.HideRevealSeconds=40;}),bHideRevealLive}};
    for(const auto& G:Gated)
    {
        TestEqual(TEXT("Rule accepted only once its runtime is live"),G.Key.IsValid(),G.Value);
        Parsed=Base;TestEqual(TEXT("Parse follows the same switch"),FProjectCustomMatchRules::Parse(G.Key.ToJson(),Parsed),G.Value);
        if(!G.Value)TestTrue(TEXT("Refused parse keeps the previous rules"),Parsed==Base);
    }
    return true;
}

// Docs/CustomGamePlayground.md 4.1: a catch costs no life, sends the survivor back to the floor start, and the mannequin
// side wins a timed-out match when nobody escaped.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesInfiniteLivesTest,"ProjectProject01.CustomRules.InfiniteLives",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesInfiniteLivesTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};World->SetGameInstance(NewObject<UGameInstance>());
    FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));if(!TestTrue(TEXT("Authoritative fixture"),World->SetGameMode(URL)))return false;
    auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();FString Error;
    FProjectCustomMatchRules Rules;Rules.bEnabled=true;Rules.DeathCount=2;Rules.MannequinCount=2;Rules.TimeLimitSeconds=600;Rules.bInfiniteLives=true;
    if(!TestTrue(TEXT("Infinite lives rules accepted"),Rules.IsValid()&&Mode->AcceptCustomRules(FGuid::NewGuid().ToString(),Rules,Error)))return false;
    TestTrue(TEXT("Game mode reports infinite lives"),Mode->HasPlaygroundInfiniteLives());

    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FTransform Start(FRotator(0,90,0),FVector(0,0,500));
    auto* Survivor=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),Start,Spawn);Survivor->DispatchBeginPlay();
    auto* Controller=World->SpawnActor<AAIController>(AAIController::StaticClass(),FTransform::Identity,Spawn);Controller->Possess(Survivor);
    auto* Mannequin=World->SpawnActor<AMannequinAICharacter>(AMannequinAICharacter::StaticClass(),FTransform(FVector(200,0,500)),Spawn);
    const int32 Lives=Survivor->GetRemainingDeathCount();

    Mode->SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::InProgress);
    Mode->PlaygroundFloorStarts.Add(Controller,Start);
    Survivor->SetActorLocation(FVector(3000,0,500),false,nullptr,ETeleportType::TeleportPhysics);
    TestTrue(TEXT("The catch is handled"),Survivor->HandleMannequinCatch(Mannequin));
    TestEqual(TEXT("No life is lost"),Survivor->GetRemainingDeathCount(),Lives);
    TestFalse(TEXT("Nobody is out"),Survivor->IsGameOver());
    TestTrue(TEXT("Moving back waits for the hit feedback"),Survivor->IsPlaygroundRespawnPending());
    FTransform Given;TestTrue(TEXT("The game mode gives this floor's start"),Mode->HandlePlaygroundCatch(Survivor,Given)&&Given.GetLocation().Equals(Start.GetLocation()));
    Survivor->FinishPlaygroundRespawn(Given);
    TestTrue(TEXT("Back at the floor start after the hit feedback"),FVector::Dist2D(Survivor->GetActorLocation(),Start.GetLocation())<50.f);
    TestTrue(TEXT("A second catch right away is ignored (protection)"),Survivor->HandleMannequinCatch(Mannequin)&&!Survivor->IsGameOver());

    FMultiplayTestServerPlayerRecord Man;Man.Role=TEXT("Mannequin");Man.PlaygroundCatches=4;
    FMultiplayTestServerPlayerRecord Runner;Runner.Role=TEXT("Survivor");Runner.SurvivorState=EMultiplayTestSurvivorState::Active;
    Mode->MatchPlayerRecords.Add(nullptr,Runner);
    auto Result=Mode->BuildFinalResult(nullptr,Man);
    TestTrue(TEXT("Time out with nobody out the exit: mannequin side wins"),Result.bSuccess&&Result.Outcome==TEXT("MannequinVictory"));
    TestEqual(TEXT("Catches made reach the result"),Result.PlaygroundCatchCount,4);
    Mode->MatchPlayerRecords.FindChecked(nullptr).SurvivorState=EMultiplayTestSurvivorState::Escaped;
    TestFalse(TEXT("Any escape: survivors win"),Mode->BuildFinalResult(nullptr,Man).bSuccess);
    Mode->MatchPlayerRecords.Reset();
    return true;
}

// Docs/CustomGamePlayground.md 4.4: staying within 4 m warns 10 s before and then reveals; moving resets; time only
// counts while the match runs and while the survivor is in play.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesHideRevealTest,"ProjectProject01.CustomRules.HideReveal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesHideRevealTest::RunTest(const FString&)
{
    using EStep=FPlaygroundHideTracker::EStep;
    FPlaygroundHideTracker T;const FVector Spot(0,0,0);
    TestTrue(TEXT("First sight only sets the anchor"),T.Step(Spot,1.f,20)==EStep::None);
    EStep Got=EStep::None;int32 WarnAt=-1,RevealAt=-1;
    for(int32 S=1;S<=25&&RevealAt<0;++S){Got=T.Step(Spot+FVector(100,0,0),1.f,20);if(Got==EStep::Warn)WarnAt=S;if(Got==EStep::Reveal)RevealAt=S;}
    TestEqual(TEXT("Warned 10 s before"),WarnAt,10);TestEqual(TEXT("Revealed after 20 s"),RevealAt,20);
    for(int32 S=0;S<15;++S)T.Step(Spot,1.f,20);
    TestTrue(TEXT("Moving past 4 m starts over"),T.Step(Spot+FVector(700,0,0),1.f,20)==EStep::None&&T.Stayed==0.f);
    TestTrue(TEXT("Off (0 s) never counts"),T.Step(Spot+FVector(500,0,0),100.f,0)==EStep::None);

    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};World->SetGameInstance(NewObject<UGameInstance>());
    FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));if(!TestTrue(TEXT("Authoritative fixture"),World->SetGameMode(URL)))return false;
    auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();FString Error;
    FProjectCustomMatchRules Rules;Rules.bEnabled=true;Rules.MannequinCount=2;Rules.HideRevealSeconds=20;
    if(!TestTrue(TEXT("Reveal rules accepted"),Rules.IsValid()&&Mode->AcceptCustomRules(FGuid::NewGuid().ToString(),Rules,Error)))return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Survivor=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(0,0,500)),Spawn);Survivor->DispatchBeginPlay();
    FMultiplayTestServerPlayerRecord Record;Record.Role=TEXT("Survivor");Record.SurvivorState=EMultiplayTestSurvivorState::TransitionWaiting;Record.SurvivorPawn=Survivor;
    Mode->MatchPlayerRecords.Add(nullptr,Record);
    for(int32 S=0;S<30;++S)Mode->TickPlaygroundHideReveal(1.f);
    TestTrue(TEXT("Waiting at the exit never reveals"),Mode->PlaygroundRevealEchoes.IsEmpty());
    Mode->MatchPlayerRecords.FindChecked(nullptr).SurvivorState=EMultiplayTestSurvivorState::Active;
    for(int32 S=0;S<21;++S)Mode->TickPlaygroundHideReveal(1.f);
    TestEqual(TEXT("An active survivor who stays is revealed once (second light queued)"),Mode->PlaygroundRevealEchoes.Num(),1);
    Mode->MatchPlayerRecords.Reset();
    return true;
}

// Docs/CustomGamePlayground.md 4.3: an eliminated survivor stays as a ghost with a timed whisper and a ping every 20 s.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesGhostsTest,"ProjectProject01.CustomRules.Ghosts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesGhostsTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};World->SetGameInstance(NewObject<UGameInstance>());
    FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));if(!TestTrue(TEXT("Authoritative fixture"),World->SetGameMode(URL)))return false;
    auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();FString Error;
    FProjectCustomMatchRules Rules;Rules.bEnabled=true;Rules.DeathCount=1;Rules.MannequinCount=2;Rules.bGhosts=true;
    if(!TestTrue(TEXT("Ghost rules accepted"),Rules.IsValid()&&Mode->AcceptCustomRules(FGuid::NewGuid().ToString(),Rules,Error)))return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Ghost=World->SpawnActor<AMultiplayTestPlayerController>(AMultiplayTestPlayerController::StaticClass(),FTransform::Identity,Spawn);
    auto* Living=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(0,0,500)),Spawn);Living->DispatchBeginPlay();
    FMultiplayTestServerPlayerRecord Dead;Dead.Role=TEXT("Survivor");Dead.SurvivorState=EMultiplayTestSurvivorState::Eliminated;
    FMultiplayTestServerPlayerRecord Alive;Alive.Role=TEXT("Survivor");Alive.SurvivorState=EMultiplayTestSurvivorState::Active;Alive.SurvivorPawn=Living;
    Mode->MatchPlayerRecords.Add(Ghost,Dead);Mode->MatchPlayerRecords.Add(nullptr,Alive);
    ON_SCOPE_EXIT{Mode->MatchPlayerRecords.Reset();};

    TestFalse(TEXT("No whisper before the match runs"),Mode->TryOpenGhostWhisper(Ghost));
    Mode->SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::InProgress);
    TestTrue(TEXT("An eliminated survivor is a ghost"),Mode->IsGhost(Dead));
    TestTrue(TEXT("Whisper opens"),Mode->TryOpenGhostWhisper(Ghost)&&Ghost->IsGhostWhisperOpen());
    TestFalse(TEXT("No second whisper during the cooldown"),Mode->TryOpenGhostWhisper(Ghost));
    Mode->GhostWhisperUntil.Add(Ghost,-1.0);Mode->TickGhostWhispers();
    TestFalse(TEXT("The whisper closes by itself"),Ghost->IsGhostWhisperOpen());

    TestTrue(TEXT("A ghost ping near the living goes out"),Mode->TryBroadcastSurvivorPing(Ghost,EProjectProject01PingType::Location,FVector(300,0,500)));
    TestFalse(TEXT("Ghost pings wait 20 s"),Mode->TryBroadcastSurvivorPing(Ghost,EProjectProject01PingType::Location,FVector(300,0,500)));
    Mode->GhostPingReadyAt.Reset();
    TestFalse(TEXT("Ghost pings far from every living survivor are refused"),Mode->TryBroadcastSurvivorPing(Ghost,EProjectProject01PingType::Location,FVector(9000,0,500)));

    Mode->MatchPlayerRecords.FindChecked(Ghost).bForfeited=true;
    TestFalse(TEXT("A survivor who forfeited is not a ghost"),Mode->IsGhost(Mode->MatchPlayerRecords.FindChecked(Ghost)));
    return true;
}

// Docs/CustomGamePlayground.md 4.2, 11: infection + infinite lives infects on the first catch; the infected player gets
// one mannequin body outside the slots, may attack only with it, and shares the mannequin side's result.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesInfectionTest,"ProjectProject01.CustomRules.Infection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesInfectionTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};World->SetGameInstance(NewObject<UGameInstance>());
    FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));if(!TestTrue(TEXT("Authoritative fixture"),World->SetGameMode(URL)))return false;
    auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();FString Error;
    FProjectCustomMatchRules Rules;Rules.bEnabled=true;Rules.DeathCount=3;Rules.MannequinCount=3;Rules.TimeLimitSeconds=600;Rules.bInfection=true;Rules.bInfiniteLives=true;
    if(!TestTrue(TEXT("Zombie rules accepted"),Rules.IsValid()&&Mode->AcceptCustomRules(FGuid::NewGuid().ToString(),Rules,Error)))return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* PC=World->SpawnActor<AMultiplayTestPlayerController>(AMultiplayTestPlayerController::StaticClass(),FTransform::Identity,Spawn);
    auto* Bystander=World->SpawnActor<AMultiplayTestPlayerController>(AMultiplayTestPlayerController::StaticClass(),FTransform::Identity,Spawn);
    auto* Caught=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(0,0,500)),Spawn);Caught->DispatchBeginPlay();PC->Possess(Caught);
    auto* Other=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(4000,0,500)),Spawn);Other->DispatchBeginPlay();
    auto* Catcher=World->SpawnActor<AMannequinAICharacter>(AMannequinAICharacter::StaticClass(),FTransform(FVector(150,0,500)),Spawn);
    FMultiplayTestServerPlayerRecord A;A.UserId=TEXT("infected");A.DisplayName=TEXT("생존자 1");A.Role=TEXT("Survivor");A.SurvivorState=EMultiplayTestSurvivorState::Active;A.SurvivorPawn=Caught;
    FMultiplayTestServerPlayerRecord B;B.UserId=TEXT("alive");B.DisplayName=TEXT("생존자 2");B.Role=TEXT("Survivor");B.SurvivorState=EMultiplayTestSurvivorState::Active;B.SurvivorPawn=Other;
    Mode->MatchPlayerRecords.Add(PC,A);Mode->MatchPlayerRecords.Add(Bystander,B);
    ON_SCOPE_EXIT{Mode->MatchPlayerRecords.Reset();};
    Mode->SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::InProgress);

    TestTrue(TEXT("Catch handled"),Caught->HandleMannequinCatch(Catcher));
    TestTrue(TEXT("Infection + infinite lives: the first catch puts the survivor out"),Caught->IsGameOver());
    const auto& Record=Mode->MatchPlayerRecords.FindChecked(PC);
    TestTrue(TEXT("The caught survivor is marked infected"),Record.bInfected&&Record.SurvivorState==EMultiplayTestSurvivorState::Eliminated);
    TestFalse(TEXT("Not yet playing a body (the fall presentation plays first)"),Mode->IsInfectedController(PC));

    AMannequinAICharacter* Body=Mode->SpawnInfectedBody(PC,Caught->GetActorTransform());
    if(!TestNotNull(TEXT("Infected body spawned"),Body))return false;
    TestTrue(TEXT("The infected player plays the body directly"),PC->GetPawn()==Body&&Body->IsManualControlEnabled());
    TestEqual(TEXT("The body is outside the mannequin player's number keys"),Body->GetControlSlot(),-1);
    TestTrue(TEXT("Infected controller may attack with its body"),Mode->IsInfectedController(PC));
    TestFalse(TEXT("A living survivor is not infected"),Mode->IsInfectedController(Bystander));
    TestTrue(TEXT("Asking again keeps the same body"),Mode->SpawnInfectedBody(PC,FTransform(FVector(900,0,500)))==Body);

    auto Result=Mode->BuildFinalResult(PC,Record);
    TestTrue(TEXT("Result says infected and the mannequin side won (nobody out)"),Result.bPlaygroundInfected&&Result.bInfectedSideWon);
    Mode->MatchPlayerRecords.FindChecked(Bystander).SurvivorState=EMultiplayTestSurvivorState::Escaped;
    TestFalse(TEXT("An escape means the infected side lost"),Mode->BuildFinalResult(PC,Record).bInfectedSideWon);
    return true;
}

// Docs/CustomGamePlayground.md 12.1: phase-2 fields are on the wire with their ranges, the gaze limit runs, and the
// rules whose runtime is not built yet are still refused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesGazeLimitTest,"ProjectProject01.CustomRules.GazeLimit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesGazeLimitTest::RunTest(const FString&)
{
    FProjectCustomMatchRules Base;Base.bEnabled=true;Base.MannequinCount=4;
    auto With=[&Base](TFunction<void(FProjectCustomMatchRules&)> Edit){FProjectCustomMatchRules R=Base;Edit(R);return R;};
    for(int32 Bad:{1,2,16})TestFalse(FString::Printf(TEXT("Gaze limit %d s rejected"),Bad),With([Bad](auto& R){R.GazeLimitSeconds=Bad;}).IsWellFormed());
    for(int32 Good:{3,8,15})TestTrue(FString::Printf(TEXT("Gaze limit %d s accepted"),Good),With([Good](auto& R){R.GazeLimitSeconds=Good;}).IsValid());
    TestFalse(TEXT("Darkness above 3 rejected"),With([](auto& R){R.Darkness=4;}).IsWellFormed());
    TestFalse(TEXT("Sound-only distance below 4 m rejected"),With([](auto& R){R.SoundOnlyMeters=3;}).IsWellFormed());
    TestFalse(TEXT("Unknown escape mode rejected"),With([](auto& R){R.EscapeMode=4;}).IsWellFormed());
    {auto J=Base.ToJson();J->RemoveField(TEXT("gazeLimitSeconds"));FProjectCustomMatchRules P;TestFalse(TEXT("Missing phase-2 field rejected"),FProjectCustomMatchRules::Parse(J,P));}
    using namespace ProjectPlaygroundRules;
    TestEqual(TEXT("Darkness waits for its runtime"),With([](auto& R){R.Darkness=2;}).IsValid(),bDarknessLive);
    TestEqual(TEXT("Sound-only waits for its runtime"),With([](auto& R){R.SoundOnlyMeters=8;}).IsValid(),bSoundOnlyLive);
    TestEqual(TEXT("Escape mode waits for its runtime"),With([](auto& R){R.EscapeMode=2;}).IsValid(),bEscapeModeLive);
    TestEqual(TEXT("Spreading fire waits for its runtime"),With([](auto& R){R.bSpreadingFire=true;}).IsValid(),bSpreadingFireLive);

    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};World->SetGameInstance(NewObject<UGameInstance>());
    FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));if(!TestTrue(TEXT("Authoritative fixture"),World->SetGameMode(URL)))return false;
    auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();FString Error;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Body=World->SpawnActor<AMannequinAICharacter>(AMannequinAICharacter::StaticClass(),FTransform(FVector(0,0,500)),Spawn);
    TestTrue(TEXT("Without the rule a watched body stays frozen"),Mode->ApplyPlaygroundGazeLimit(Body,true));
    auto Rules=With([](auto& R){R.GazeLimitSeconds=8;});
    if(!TestTrue(TEXT("Gaze limit rules accepted"),Mode->AcceptCustomRules(FGuid::NewGuid().ToString(),Rules,Error)))return false;
    TestTrue(TEXT("Freshly watched body freezes"),Mode->ApplyPlaygroundGazeLimit(Body,true));
    const double Now=World->GetTimeSeconds();
    Mode->PlaygroundGazeSince.FindOrAdd(Body)=Now-7.0;TestTrue(TEXT("Watched 7 s of 8: still frozen"),Mode->ApplyPlaygroundGazeLimit(Body,true));
    Mode->PlaygroundGazeSince.FindOrAdd(Body)=Now-9.0;TestFalse(TEXT("Watched 9 s of 8: moves anyway"),Mode->ApplyPlaygroundGazeLimit(Body,true));
    TestFalse(TEXT("Unwatched: not frozen"),Mode->ApplyPlaygroundGazeLimit(Body,false));
    TestTrue(TEXT("Looking again starts a new count"),Mode->ApplyPlaygroundGazeLimit(Body,true));
    return true;
}

// Docs/CustomGamePlayground.md 12.1: darkness shortens how far the freezing gaze reaches and reaches every player's
// screen (the level replicates to each controller; the screen effect itself is local presentation).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesDarknessTest,"ProjectProject01.CustomRules.Darkness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesDarknessTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};World->SetGameInstance(NewObject<UGameInstance>());
    FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));if(!TestTrue(TEXT("Authoritative fixture"),World->SetGameMode(URL)))return false;
    auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();FString Error;
    TestEqual(TEXT("No rules: the gaze reaches as far as before"),Mode->PlaygroundVisionRange(),0.f);
    FProjectCustomMatchRules Rules;Rules.bEnabled=true;Rules.MannequinCount=4;Rules.Darkness=2;
    if(!TestTrue(TEXT("Darkness rules accepted"),Rules.IsValid()&&Mode->AcceptCustomRules(FGuid::NewGuid().ToString(),Rules,Error)))return false;
    TestEqual(TEXT("Darkness 2: the gaze reaches 20 m"),Mode->PlaygroundVisionRange(),2000.f);
    Mode->CustomMatchRules.Darkness=3;TestEqual(TEXT("Darkness 3: 12 m"),Mode->PlaygroundVisionRange(),1200.f);
    Mode->CustomMatchRules.Darkness=1;Mode->CustomMatchRules.SoundOnlyMeters=8;TestEqual(TEXT("The shorter of darkness and sound-only wins"),Mode->PlaygroundVisionRange(),800.f);
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* PC=World->SpawnActor<AMultiplayTestPlayerController>(AMultiplayTestPlayerController::StaticClass(),FTransform::Identity,Spawn);
    PC->SetPlaygroundDarkness(2);TestEqual(TEXT("The level reaches the player's controller"),PC->GetPlaygroundDarkness(),2);
    PC->SetPlaygroundDarkness(9);TestEqual(TEXT("Out-of-range levels are clamped"),PC->GetPlaygroundDarkness(),3);
    TestTrue(TEXT("Zombie department store 3 is offered now that darkness runs"),ProjectPlaygroundPresets::Available().ContainsByPredicate([](const auto& P){return P.Rules.Darkness>0;}));
    return true;
}

// Docs/CustomGamePlayground.md 12.1: sound-only bodies are drawn on survivor screens only within the set distance, with
// a small margin so a body on the edge never blinks; the distance reaches each player.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesSoundOnlyTest,"ProjectProject01.CustomRules.SoundOnly",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesSoundOnlyTest::RunTest(const FString&)
{
    using PC=AMultiplayTestPlayerController;
    TestTrue(TEXT("Rule off: always drawn"),PC::ShouldShowSoundOnlyBody(99999.f,0,false));
    TestTrue(TEXT("Inside 8 m: drawn"),PC::ShouldShowSoundOnlyBody(700.f,8,false));
    TestFalse(TEXT("Beyond 8 m: hidden"),PC::ShouldShowSoundOnlyBody(900.f,8,true));
    TestTrue(TEXT("On the edge a shown body stays shown"),PC::ShouldShowSoundOnlyBody(820.f,8,true));
    TestFalse(TEXT("On the edge a hidden body stays hidden"),PC::ShouldShowSoundOnlyBody(780.f,8,false));
    FProjectCustomMatchRules R;R.bEnabled=true;R.MannequinCount=4;
    for(int32 Bad:{1,3,16})TestFalse(FString::Printf(TEXT("Sound-only %d m rejected"),Bad),[&]{auto C=R;C.SoundOnlyMeters=Bad;return C.IsWellFormed();}());
    TestTrue(TEXT("Sound-only 8 m runs now"),[&]{auto C=R;C.SoundOnlyMeters=8;return C.IsValid();}());

    auto* World=UWorld::CreateWorld(EWorldType::Game,false);ON_SCOPE_EXIT{World->DestroyWorld(false);};
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Controller=World->SpawnActor<PC>(PC::StaticClass(),FTransform::Identity,Spawn);
    Controller->SetPlaygroundSoundOnlyMeters(8);TestEqual(TEXT("The distance reaches the player's controller"),Controller->GetPlaygroundSoundOnlyMeters(),8);
    return true;
}

// Docs/CustomGamePlayground.md 5: every recommended preset is well formed, and only presets the server runs are offered.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesPresetsTest,"ProjectProject01.CustomRules.Presets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesPresetsTest::RunTest(const FString&)
{
    TSet<FString> Names;
    for(const auto& P:ProjectPlaygroundPresets::All())
    {
        TestTrue(TEXT("Preset is well formed: ")+P.Name,P.Rules.IsWellFormed()&&P.Rules.bEnabled&&P.Rules.HasPlaygroundRules());
        TestFalse(TEXT("Preset names are unique: ")+P.Name,Names.Contains(P.Name));Names.Add(P.Name);
        FProjectCustomMatchRules Back;TestEqual(TEXT("Preset survives the wire: ")+P.Name,FProjectCustomMatchRules::Parse(P.Rules.ToJson(),Back),P.Rules.IsValid());
    }
    for(const auto& P:ProjectPlaygroundPresets::Available())TestTrue(TEXT("Offered presets are all runnable: ")+P.Name,P.Rules.IsValid());
    TestEqual(TEXT("Infection presets wait for the infection runtime"),ProjectPlaygroundPresets::Available().ContainsByPredicate([](const auto& P){return P.Rules.bInfection;}),ProjectPlaygroundRules::bInfectionLive);
    return true;
}

// Docs/CustomGamePlayground.md 3.1-3.2: affix picks and item cards run together; picked affixes never come as cards and
// the ticket's two copies of the mix switches must agree.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesPlaygroundCardsTest,"ProjectProject01.CustomRules.PlaygroundCards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesPlaygroundCardsTest::RunTest(const FString&)
{
    FProjectCustomMatchRules Rules;Rules.bEnabled=true;Rules.MannequinCount=4;Rules.AffixIds={TEXT("B09"),TEXT("B10"),TEXT("A06")};Rules.bItemCards=true;
    FProjectMatchAffixes Affixes;Affixes.Mode=EProjectAffixMode::Custom;Affixes.Ids=Rules.AffixIds;FString Error;
    TestTrue(TEXT("Matching ticket copies are accepted"),AMultiplayTestGameMode::IsPlaygroundTicketConsistent(Rules,Affixes,EProjectMatchMode::ItemRoguelike,Error));
    TestFalse(TEXT("Item cards without the item mode are refused"),AMultiplayTestGameMode::IsPlaygroundTicketConsistent(Rules,Affixes,EProjectMatchMode::Normal,Error));
    auto Other=Affixes;Other.Ids={TEXT("B09")};TestFalse(TEXT("Different affix ids are refused"),AMultiplayTestGameMode::IsPlaygroundTicketConsistent(Rules,Other,EProjectMatchMode::ItemRoguelike,Error));
    auto Bot=Affixes;Bot.Mode=EProjectAffixMode::Weekly;TestFalse(TEXT("Bot affix modes never ride with custom rules"),AMultiplayTestGameMode::IsPlaygroundTicketConsistent(Rules,Bot,EProjectMatchMode::ItemRoguelike,Error));
    FProjectCustomMatchRules Plain;FProjectMatchAffixes Weekly;Weekly.Mode=EProjectAffixMode::Weekly;Weekly.Ids={TEXT("A01"),TEXT("B01"),TEXT("C01")};
    TestTrue(TEXT("Without custom rules the ticket is left to the existing checks"),AMultiplayTestGameMode::IsPlaygroundTicketConsistent(Plain,Weekly,EProjectMatchMode::Normal,Error));

    // The card pool leaves out affixes that are already on for the match.
    auto* Rogue=NewObject<UProjectItemRoguelikeRuntime>();const FString Match=FGuid::NewGuid().ToString();
    TestTrue(TEXT("Item mode accepted"),Rogue->Accept(Match,EProjectMatchMode::ItemRoguelike,Error));
    Rogue->SetFixedBuffs(Rules.AffixIds);
    auto Record=[](const TCHAR* Id,const TCHAR* Role)
    {FMultiplayTestServerPlayerRecord R;R.UserId=Id;R.DisplayName=Id;R.Role=Role;R.SurvivorState=FString(Role)==TEXT("Survivor")?EMultiplayTestSurvivorState::Active:EMultiplayTestSurvivorState::NotApplicable;return R;};
    const TArray<FMultiplayTestServerPlayerRecord> Records={Record(TEXT("s1"),TEXT("Survivor")),Record(TEXT("s2"),TEXT("Survivor")),Record(TEXT("m"),TEXT("Mannequin"))};
    bool bLeaked=false;
    for(int32 Seed=0;Seed<ProjectItemRoguelike::MaxFloors&&!bLeaked;++Seed)
    {
        Rogue->SeedForTest(Seed);Rogue->OpenForTest(Records,100.0+Seed);
        for(const TCHAR* Id:{TEXT("s1"),TEXT("s2"),TEXT("m")})if(const auto* P=Rogue->FindPlayer(Id))
            for(const FString& Card:P->Offer)bLeaked|=Rules.AffixIds.Contains(Card);
        for(const TCHAR* Id:{TEXT("s1"),TEXT("s2"),TEXT("m")})if(const auto* P=Rogue->FindPlayer(Id))Rogue->PickForTest(Id,P->Serial,0);
        Rogue->TickForTest(200.0+Seed);
    }
    TestFalse(TEXT("Picked affixes never appear as buff cards"),bLeaked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesRenderTest,"ProjectProject01.CustomRules.Render",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectCustomRulesRenderTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* GI=NewObject<UGameInstance>();World->SetGameInstance(GI);GI->Init();
    auto* L=GI->GetSubsystem<UProjectProject01LobbySubsystem>();auto* W=NewObject<UProjectCustomRoomRulesWidget>(World);
    ON_SCOPE_EXIT{W->ReleaseSlateResources(true);GI->Shutdown();World->SetGameInstance(nullptr);World->DestroyWorld(false);};
    L->CurrentRoom.RoomId=TEXT("offline-render");L->CurrentRoom.bIsHost=true;W->RoomId=L->CurrentRoom.RoomId;W->bEditable=true;W->Draft.bEnabled=true;W->Draft.DeathCount=6;W->Draft.MannequinCount=4;
    W->Draft.AffixIds={TEXT("B09"),TEXT("C05")};W->Draft.bItemCards=true;W->Draft.bInfiniteLives=true;W->Draft.HideRevealSeconds=40;W->Draft.GazeLimitSeconds=8;W->HoveredAffix=TEXT("B09");W->CodeLine=TEXT("ZB7K-2QXM · 지금까지 37판 · 생존자 승 12 · 마네킹 승 25\n내 결과: 3판 중 1승");
    static_cast<UUserWidget*>(W)->Initialize();auto Slate=W->TakeWidget();
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("CustomRulesUIQA");IFileManager::Get().MakeDirectory(*Directory,true);FWidgetRenderer Renderer(false);
    FVector2D PanelSize[5];
    for(int32 Tab:{0,1,2,3,4})for(FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
    {
        W->Tab=Tab;const FString Label=FString::Printf(TEXT("[tab %d, %dx%d] "),Tab,Size.X,Size.Y);
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,false);float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
        // Let the renderer finish uploading newly requested Korean/font atlas glyphs before capture.
        for(int I=0;I<6;++I){Renderer.DrawWidget(Target,Slate,Dpi,FVector2D(Size),.016f);FlushRenderingCommands();}
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);
        if(TestTrue(Label+TEXT("Custom editor renders"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)))
        {TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("%s_%dx%d.png"),Tab==0?TEXT("Rules"):Tab==1?TEXT("RulesMix"):Tab==2?TEXT("RulesFree"):Tab==3?TEXT("RulesVision"):TEXT("RulesCodes"),Size.X,Size.Y)));}
        int Numbers=0,Badges=0;bool Back=false,Apply=false,Scroll=false,Fits=true;TArray<FSlateRect> Buttons;FVector2D Panel=FVector2D::ZeroVector;
        TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> Node)
        {
            const FName Type=Node->GetType();
            if(Type==FName(TEXT("SScrollBox")))Scroll=true;
            if(Type.ToString().StartsWith(TEXT("SSpinBox")))++Numbers;
            if(Type==FName(TEXT("SButton")))
            {
                const auto G=Node->GetCachedGeometry();const FVector2D P=G.GetAbsolutePosition(),S=G.GetAbsoluteSize();
                if(S.X>1&&S.Y>1)Buttons.Add(FSlateRect(P,P+S));
                if(FMath::IsNearlyEqual(G.GetLocalSize().X,42.f,1.f)&&FMath::IsNearlyEqual(G.GetLocalSize().Y,42.f,1.f))++Badges;
            }
            if(Type==FName(TEXT("SWidgetSwitcher")))
            {
                // The active tab must fit the fixed tab area (nothing clipped), and the area itself never changes size.
                Panel=Node->GetCachedGeometry().GetLocalSize();
                if(auto* C=Node->GetChildren();C&&C->Num()>0){const float Need=C->GetChildAt(0)->GetDesiredSize().Y;AddInfo(Label+FString::Printf(TEXT("tab content %.0f of %.0f"),Need,Panel.Y));Fits&=Need<=Panel.Y+.5f;}
            }
            if(Type==FName(TEXT("STextBlock")))
            {auto T=StaticCastSharedRef<STextBlock>(Node);FString Text=T->GetText().ToString();Back|=Text==TEXT("돌아가기");Apply|=Text==TEXT("적용");
              if(!Text.IsEmpty())
              {
                  const auto G=Node->GetCachedGeometry();auto P=G.GetAbsolutePosition(),S=G.GetAbsoluteSize();TestTrue(Label+TEXT("Visible labels fit viewport"),P.X>=0&&P.Y>=0&&P.X+S.X<=Size.X+1&&P.Y+S.Y<=Size.Y+1);
                  if(Text==TEXT("데스카운트")||Text==TEXT("마네킹 수")||Text==TEXT("적용")||Text==TEXT("돌아가기")||Text==TEXT("섞기"))
                  {
                      int Bright=0;for(int Y=FMath::Max(0,int(P.Y));Y<FMath::Min(Size.Y,int(P.Y+S.Y));++Y)for(int X=FMath::Max(0,int(P.X));X<FMath::Min(Size.X,int(P.X+S.X));++X)
                          if(Pixels.IsValidIndex(Y*Size.X+X)&&Pixels[Y*Size.X+X].R>120)++Bright;
                      TestTrue(Label+TEXT("Primary labels contain visibly rendered glyphs: ")+Text,Bright>5);
                  }
              }}
            if(auto* C=Node->GetChildren())for(int I=0;I<C->Num();++I)Inspect(C->GetChildAt(I));
        };Inspect(Slate);
        TestTrue(Label+TEXT("Apply and back remain visible without page scrolling"),Apply&&Back&&!Scroll);
        TestTrue(Label+TEXT("The active tab fits its fixed area"),Fits);
        bool Overlap=false;for(int32 A=0;A<Buttons.Num();++A)for(int32 B=A+1;B<Buttons.Num();++B)
        {const FSlateRect I=Buttons[A].IntersectionWith(Buttons[B]);Overlap|=I.GetSize().X>1&&I.GetSize().Y>1;}
        TestFalse(Label+TEXT("No two buttons overlap"),Overlap);
        if(Tab==0)TestEqual(Label+TEXT("Basic tab keeps the three numbers"),Numbers,3);
        else if(Tab==1)TestEqual(Label+TEXT("Mix tab shows every playable affix"),Badges,ProjectAffixes::All().Num());
        if(Size.X==1280)PanelSize[Tab]=Panel;
        Target->ReleaseResource();
    }
    TestTrue(TEXT("Switching tabs keeps the tab area size (no jump)"),PanelSize[0].Equals(PanelSize[1],.5f)&&PanelSize[0].Equals(PanelSize[2],.5f)&&PanelSize[0].Equals(PanelSize[3],.5f)&&PanelSize[0].Equals(PanelSize[4],.5f)&&PanelSize[0].Y>0);
    return true;
}
#endif

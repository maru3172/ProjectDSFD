#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ProjectMatchBots.h"
#include "MultiplayTestGameMode.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMatchBotsRuntimeTest,"ProjectProject01.Bots.TrustedRoster",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMatchBotsRuntimeTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    ON_SCOPE_EXIT {if(World)World->DestroyWorld(false);};
    auto* Mode=World->SpawnActor<AMultiplayTestGameMode>();
    auto* Bots=Mode->MatchBots.Get();
    TestNotNull(TEXT("Real game mode owns dedicated adapter, not tutorial mode"),Bots);
    const FString Room=FGuid::NewGuid().ToString(),Match=FGuid::NewGuid().ToString();
    FProjectMatchBotSpec A{FGuid::NewGuid().ToString(),TEXT("봇 1"),TEXT("Survivor"),5};
    FProjectMatchBotSpec B{FGuid::NewGuid().ToString(),TEXT("봇 2"),TEXT("Mannequin"),3};
    FString Error;
    TestTrue(TEXT("Trusted frozen roster is accepted"),Bots->AcceptRoster(Room,Match,{A,B},Error));
    TestTrue(TEXT("Order-independent admission stays idempotent"),Bots->AcceptRoster(Room,Match,{B,A},Error));
    A.Difficulty=1;
    TestFalse(TEXT("Client reconnect cannot silently change level"),Bots->AcceptRoster(Room,Match,{A,B},Error));
    TestFalse(TEXT("Match identity cannot change"),Bots->AcceptRoster(Room,FGuid::NewGuid().ToString(),{B},Error));
    TestFalse(TEXT("Frozen roster cannot disappear"),Bots->AcceptRoster(Room,Match,{},Error));
    TestFalse(TEXT("No human admission means no unattended match"),Bots->SpawnParticipants());
    TestTrue(TEXT("No controllers created by an uncommitted admission"),Bots->Controllers.IsEmpty());

    auto Json=[&](const FProjectMatchBotSpec& Spec,double Level)
    {
        auto Object=MakeShared<FJsonObject>();Object->SetStringField(TEXT("botId"),Spec.BotId);
        Object->SetStringField(TEXT("displayName"),Spec.DisplayName);Object->SetStringField(TEXT("assignedRole"),Spec.Role);
        Object->SetNumberField(TEXT("difficulty"),Level);return MakeShared<FJsonValueObject>(Object);
    };
    TArray<FProjectMatchBotSpec> Parsed;
    for(int32 Level=1;Level<=5;++Level)
        TestTrue(TEXT("Five exact levels admitted from trusted JSON"),FProjectMatchBotSpec::Parse({Json(A,Level),Json(B,Level)},Parsed,Error));
    const auto Before=Parsed;
    for(double Level:{0.,6.,2.5})
    {
        TestFalse(TEXT("Invalid or fractional levels rejected"),FProjectMatchBotSpec::Parse({Json(A,Level)},Parsed,Error));
        TestTrue(TEXT("Failed parse cannot partially replace a frozen roster"),Parsed==Before);
    }
    TestFalse(TEXT("Duplicate canonical bot identity rejected"),FProjectMatchBotSpec::Parse({Json(A,1),Json(A,2)},Parsed,Error));
    TestFalse(TEXT("Null participant rejected"),FProjectMatchBotSpec::Parse({MakeShared<FJsonValueNull>()},Parsed,Error));
    A.Role=TEXT("Mannequin");
    TestFalse(TEXT("Two mannequin roles rejected"),FProjectMatchBotSpec::Parse({Json(A,2),Json(B,2)},Parsed,Error));
    A.Role=TEXT("Observer");
    TestFalse(TEXT("Spectator is never treated as a bot playing slot"),FProjectMatchBotSpec::Parse({Json(A,2)},Parsed,Error));
    TestTrue(TEXT("Human-only ticket keeps backward compatibility"),FProjectMatchBotSpec::Parse({},Parsed,Error)&&Parsed.IsEmpty());
    {
        // Below the map's fall height a survivor dies outright, whatever death count is left, and only once.
        TestTrue(TEXT("A map without start points uses only the world KillZ"),Mode->GetFallDeathZ()<-1.e30f);
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Faller=World->SpawnActor<AProjectMatchBotCharacter>(AProjectMatchBotCharacter::StaticClass(),FVector(0,0,-50000),FRotator::ZeroRotator,Params);
        TestTrue(TEXT("A fall kills the survivor"),Faller&&Faller->HandleFallDeath()&&Faller->IsGameOver());
        TestFalse(TEXT("A fall death is applied once"),Faller&&Faller->HandleFallDeath());
        if(Faller)Faller->Destroy();
    }

    // Experience across matches: text round trip, strict shape, level caps on load and on the way out.
    {
        using namespace ProjectBots;
        TMap<uint8,FProjectBotExperience> Pool;auto& Grown=Pool.Add(5);
        Grown.Combos.Links[0]={Play::PeekDash,Play::ExitFeint,6};
        Grown.Tuning.Knobs[static_cast<size_t>(Play::FalseRetreat)].Duration=1.3f;Grown.Tuning.Knobs[static_cast<size_t>(Play::FalseRetreat)].Reach=.7f;
        const FString Text=UProjectMatchBots::EncodeExperience(Pool);TMap<uint8,FProjectBotExperience> Back;
        TestTrue(TEXT("Experience text round-trips"),UProjectMatchBots::DecodeExperience(Text,Back)&&Back.Contains(5)&&
            Back[5].Combos.Links[0].To==Play::ExitFeint&&FMath::IsNearlyEqual(Back[5].Tuning.ReachScale(Play::FalseRetreat),.7f,.01f));
        for(const TCHAR* Bad:{TEXT("v2|1;;"),TEXT("v1|9;;"),TEXT("v1|1;3-3-10;"),TEXT("v1|1;;4-200-100"),TEXT("v1|1;99-2-10;"),TEXT("v1|1;x;")})
            TestFalse(TEXT("Malformed experience is refused"),UProjectMatchBots::DecodeExperience(Bad,Back));
        TestTrue(TEXT("Empty pool is valid"),UProjectMatchBots::DecodeExperience(TEXT("v1"),Back)&&Back.IsEmpty());

        // A level-1 bot given a master-sized pool only gets level-1 growth.
        FProjectMatchBotSpec Easy{FGuid::NewGuid().ToString(),TEXT("봇 3"),TEXT("Survivor"),1};
        TMap<uint8,FProjectBotExperience> Mixed;Mixed.Add(UProjectMatchBots::StyleOf(Easy),Grown);Easy.Experience=UProjectMatchBots::EncodeExperience(Mixed);
        UProjectMatchBots::FBotPlayState State;UProjectMatchBots::LoadExperience(State,Easy,Difficulty::For(1));
        TestTrue(TEXT("Loaded experience is capped to the bot's level"),State.ExperienceLoaded&&
            State.Tuning.DurationScale(Play::FalseRetreat)<=1+PlayTuning::Range(Difficulty::For(1))+1e-4f&&
            State.Tuning.ReachScale(Play::FalseRetreat)>=1-PlayTuning::Range(Difficulty::For(1))-1e-4f&&
            State.Combos.Links[0].Score<=ComboBook::Needed(Difficulty::For(1))+1);

        // Match end: one pool per role and level, other styles kept, this match's style updated.
        auto* Fresh=NewObject<UProjectMatchBots>(Mode);
        FProjectMatchBotSpec S{FGuid::NewGuid().ToString(),TEXT("봇 4"),TEXT("Survivor"),4},M{FGuid::NewGuid().ToString(),TEXT("봇 5"),TEXT("Mannequin"),4};
        const uint8 Other=static_cast<uint8>((UProjectMatchBots::StyleOf(S)+1)%8);TMap<uint8,FProjectBotExperience> Stored;Stored.Add(Other,Grown);
        S.Experience=UProjectMatchBots::EncodeExperience(Stored);
        TestTrue(TEXT("Second roster accepted"),Fresh->AcceptRoster(FGuid::NewGuid().ToString(),FGuid::NewGuid().ToString(),{S,M},Error));
        Fresh->Plays.FindOrAdd(S.BotId).Combos.Links[0]={Play::Listen,Play::HideStill,6};
        const auto Uploads=Fresh->ExportExperience();
        const auto* Survivor=Uploads.FindByPredicate([](const FProjectBotExperienceUpload& U){return U.Role==TEXT("Survivor");});
        TMap<uint8,FProjectBotExperience> Out;
        TestTrue(TEXT("Each role and level uploads its own pool"),Uploads.Num()==2&&Survivor&&Survivor->Difficulty==4&&UProjectMatchBots::DecodeExperience(Survivor->Payload,Out));
        TestTrue(TEXT("Other styles survive, this style is written and capped"),Out.Contains(Other)&&Out.Contains(UProjectMatchBots::StyleOf(S))&&
            Out[UProjectMatchBots::StyleOf(S)].Combos.Links[0].Score<=ComboBook::Needed(Difficulty::For(4))+1);
    }
    return true;
}
#endif

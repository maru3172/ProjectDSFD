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
    return true;
}
#endif

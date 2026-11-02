#include "ProjectMiniGameRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
using namespace ProjectMiniGame;

// The same stages and solutions the server's checks use (Tools/ProjectProject01FeatureChecks/MiniGameChecks.cs):
// client and server must agree move for move, or a won stage would be refused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMiniGameStepsTest,"ProjectProject01.MiniGames.StepsRules",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMiniGameStepsTest::RunTest(const FString&)
{
    FStepStage First;First.Id=TEXT("1");First.Map={TEXT("########"),TEXT("##E..M.#"),TEXT("##.....#"),TEXT("##.#...#"),TEXT("##.#..##"),TEXT("##S.#..#"),TEXT("########")};First.Facing=TEXT('R');First.Par=8;
    FStepStage Last;Last.Id=TEXT("10");Last.Map={TEXT("########"),TEXT("#ME.M.M#"),TEXT("#...#..#"),TEXT("##.#...#"),TEXT("##.#...#"),TEXT("#XSX...#"),TEXT("########")};Last.Facing=TEXT('R');Last.Shutters=2;Last.Par=15;
    auto Split=[](const TCHAR* Text){TArray<FString> Out;FString(Text).ParseIntoArrayWS(Out);return Out;};
    FStepState S;
    TestEqual(TEXT("Stage 1: the server's shortest win also wins here, in par"),Replay(First,Split(TEXT("U D R L U U U U")),S),EStepResult::Won);
    TestEqual(TEXT("Stage 1 actions"),S.Actions,First.Par);
    TestEqual(TEXT("Stage 10 (two shutters): same"),Replay(Last,Split(TEXT("tL S U U D D R R R U U U U L L L")),S),EStepResult::Won);
    TestEqual(TEXT("Stage 10 actions, turns free"),S.Actions,Last.Par);
    TestTrue(TEXT("The lowered shutter is a wall now"),S.Grid[5][1]==TEXT('C')&&S.ShuttersLeft==1);
    FStepStage Chase;Chase.Map={TEXT("######"),TEXT("#M...#"),TEXT("#...S#"),TEXT("######")};Chase.Facing=TEXT('R');
    TestEqual(TEXT("Stepping back next to an unwatched mannequin gets caught"),Replay(Chase,Split(TEXT("U D U")),S),EStepResult::Caught);
    TestEqual(TEXT("Walking into a wall is refused"),Replay(First,Split(TEXT("L")),S),EStepResult::Invalid);
    TestEqual(TEXT("A shutter where there is none is refused"),Replay(First,Split(TEXT("S")),S),EStepResult::Invalid);
    TestEqual(TEXT("Turning to where you already look is refused"),Replay(First,Split(TEXT("tR")),S),EStepResult::Invalid);
    FStepStage Watch;Watch.Map={TEXT("#######"),TEXT("#..M..#"),TEXT("#.....#"),TEXT("#..S..#"),TEXT("#######")};Watch.Facing=TEXT('L');
    FStepState W(Watch);W.Apply(TEXT("tU"));
    TestTrue(TEXT("Turning is free: nothing moved"),W.Actions==0&&W.Mannequins[0]==FIntPoint(3,1));
    TestEqual(TEXT("The mannequin straight ahead is the watched one"),W.Watched(),0);
    TestTrue(TEXT("The sight line runs up to it"),W.SightLine().Num()==2&&W.SightLine().Last()==FIntPoint(3,1));
    W.Apply(TEXT("L"));
    TestTrue(TEXT("Looking away lets it come closer"),W.Mannequins[0]!=FIntPoint(3,1));
    TestTrue(TEXT("Stars: par 3, par+3 two, beyond one"),First.Stars(8)==3&&First.Stars(11)==2&&First.Stars(12)==1);
    TSharedPtr<FJsonObject> Json;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"id":"4","name":"첫 셔터","map":["###","#S#","###"],"facing":"U","shutters":1,"par":12})")),Json);
    FStepStage Parsed;TestTrue(TEXT("A stage from the server parses"),FStepStage::Parse(Json,Parsed)&&Parsed.Id==TEXT("4")&&Parsed.Shutters==1&&Parsed.Par==12&&Parsed.Facing==TEXT('U'));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMiniGameSpotTest,"ProjectProject01.MiniGames.SpotRound",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMiniGameSpotTest::RunTest(const FString&)
{
    // The server writes figure fields in camelCase.
    TSharedPtr<FJsonObject> Json;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"round":3,"cleared":2,"lives":2,"score":350,"changes":1,
        "before":[{"x":100,"y":300,"head":0,"arm":1},{"x":500,"y":700,"head":2,"arm":0}],
        "after":[{"x":100,"y":300,"head":0,"arm":1},{"x":420,"y":700,"head":2,"arm":0}]})")),Json);
    FSpotRound R;
    TestTrue(TEXT("A round from the server parses"),FSpotRound::Parse(Json,R)&&R.Round==3&&R.Lives==2&&R.Before.Num()==2);
    TestTrue(TEXT("Only the moved figure differs"),R.Before[0]==R.After[0]&&!(R.Before[1]==R.After[1]));
    TestTrue(TEXT("More mannequins, longer to remember"),R.MemorizeSeconds()>3.f);
    TSharedPtr<FJsonObject> Broken;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"before":[{"x":1,"y":1,"head":0,"arm":0}],"after":[]})")),Broken);
    TestFalse(TEXT("Pictures of different sizes are refused"),FSpotRound::Parse(Broken,R));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMiniGameBlackoutTest,"ProjectProject01.MiniGames.BlackoutRules",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMiniGameBlackoutTest::RunTest(const FString&)
{
    FBlackoutSim A,B;A.Start(77);B.Start(77);
    TestTrue(TEXT("The same seed lays out the same room"),A.Fuses==B.Fuses&&A.Fuses.Num()==3&&A.Mannequins.Num()==2);
    // A mannequin in the torch beam stays; out of it, it walks and leaves footprints.
    FBlackoutSim S;S.Start(5);S.Mannequins={{FVector2D(320,130),0}};S.Player=FVector2D(320,330);
    FBlackoutInput Look;Look.Aim=FVector2D(320,130);
    for(int32 I=0;I<20;++I)S.Tick(.1,Look);
    TestTrue(TEXT("Lit: frozen"),S.Mannequins[0].Pos.Equals(FVector2D(320,130),.01));
    TestTrue(TEXT("The torch drains the battery"),S.Battery<100&&S.Battery>90);
    FBlackoutInput Away;Away.Aim=FVector2D(20,330);
    for(int32 I=0;I<10;++I)S.Tick(.1,Away);
    TestTrue(TEXT("Unlit: it comes closer and leaves footprints (the tell)"),S.Mannequins[0].Pos.Y>130&&S.Prints.Num()>0);
    for(int32 I=0;I<200&&S.Outcome==FBlackoutSim::EOutcome::Playing;++I)S.Tick(.1,Away);
    TestEqual(TEXT("Left unwatched, it catches the player"),S.Outcome,FBlackoutSim::EOutcome::Caught);
    // Fuses: pick up by walking over, install by holding E at the board; the torch points at the board meanwhile.
    FBlackoutSim F;F.Start(9);F.Mannequins.Reset();F.NextSpawn=1000;F.Player=FBlackoutSim::Board+FVector2D(0,30);F.Fuses={F.Player};
    FBlackoutInput Idle;Idle.Aim=FVector2D(600,300);F.Tick(.05,Idle);
    TestEqual(TEXT("Walking over a fuse picks it up"),F.Carried,1);
    FBlackoutInput Hold=Idle;Hold.bHoldInstall=true;F.Tick(.05,Hold);
    TestTrue(TEXT("Installing points the torch at the board"),F.bInstalling&&F.TorchDirection().Equals((FBlackoutSim::Board-F.Player).GetSafeNormal(),.01));
    for(int32 I=0;I<40;++I)F.Tick(.05,Hold);
    TestEqual(TEXT("Holding E long enough installs it"),F.Installed,1);
    F.Carried=2;for(int32 I=0;I<80&&F.Outcome==FBlackoutSim::EOutcome::Playing;++I)F.Tick(.05,Hold);
    TestEqual(TEXT("Three installed: the power is back"),F.Outcome,FBlackoutSim::EOutcome::Cleared);
    FBlackoutSim Dark;Dark.Start(3);Dark.Battery=.05;Dark.Tick(.1,Idle);
    TestTrue(TEXT("An empty battery turns the torch off"),!Dark.bLight&&!Dark.Lit(Dark.Player+FVector2D(0,-50)));
    FBlackoutInput Toggle=Idle;Toggle.bToggleLight=true;Dark.Tick(.1,Toggle);
    TestFalse(TEXT("It cannot be switched back on without battery"),Dark.bLight);
    FBlackoutSim Saved;Saved.Start(11);for(int32 I=0;I<30;++I)Saved.Tick(.1,Away);
    FBlackoutSim Loaded;
    TestTrue(TEXT("A save loads back where it stopped"),Loaded.Load(Saved.Save())&&Loaded.Player.Equals(Saved.Player,.1)&&FMath::IsNearlyEqual(Loaded.Battery,Saved.Battery,.01)&&Loaded.Mannequins.Num()==Saved.Mannequins.Num()&&FMath::IsNearlyEqual(Loaded.Elapsed,Saved.Elapsed,.01));
    TestFalse(TEXT("A broken save is refused"),Loaded.Load(MakeShared<FJsonObject>()));
    return true;
}
#endif

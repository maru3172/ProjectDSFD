// File: Source/ProjectProject01/Private/ProjectAffixesTests.cpp
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#include "ProjectAffixes.h"
#include "ProjectAffixRuntime.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectAffixesTest,"ProjectProject01.Bots.Affixes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectAffixesTest::RunTest(const FString&)
{
    using namespace ProjectAffixes;
    int32 Groups[3]{};TSet<FString> Ids;
    for(const FInfo& I:All()){++Groups[uint8(I.Group)];Ids.Add(I.Id);TestTrue(TEXT("Every affix has a name, summary and badge glyph"),FCString::Strlen(I.Name)>0&&FCString::Strlen(I.Summary)>0&&FCString::Strlen(I.Glyph)==1);}
    TestTrue(TEXT("Thirty affixes, ten per group, unique ids"),All().Num()==30&&Groups[0]==10&&Groups[1]==10&&Groups[2]==10&&Ids.Num()==30);

    auto Set=[](EProjectAffixMode Mode,TArray<FString> List){FProjectMatchAffixes A;A.Mode=Mode;A.Ids=MoveTemp(List);return A;};
    TestTrue(TEXT("No affixes is valid"),FProjectMatchAffixes().IsValid());
    TestTrue(TEXT("Master+ is one environment affix"),Set(EProjectAffixMode::MasterPlus,{TEXT("C05")}).IsValid());
    TestFalse(TEXT("Master+ never takes a survivor affix"),Set(EProjectAffixMode::MasterPlus,{TEXT("A02")}).IsValid());
    TestTrue(TEXT("Master++ is one of each group"),Set(EProjectAffixMode::MasterPlusPlus,{TEXT("A02"),TEXT("B09"),TEXT("C05")}).IsValid());
    TestFalse(TEXT("Master++ needs all three groups"),Set(EProjectAffixMode::MasterPlusPlus,{TEXT("A02"),TEXT("C05")}).IsValid());
    {bool AllPlayable=true;for(const FInfo& I:All())AllPlayable&=I.bPlayable;TestTrue(TEXT("All thirty affixes are playable"),AllPlayable);}
    TestTrue(TEXT("Custom mode takes any number of affixes"),Set(EProjectAffixMode::Custom,{TEXT("A01"),TEXT("B10"),TEXT("C07"),TEXT("C09")}).IsValid());
    TestFalse(TEXT("No duplicates"),Set(EProjectAffixMode::Custom,{TEXT("A02"),TEXT("A02")}).IsValid());
    TestFalse(TEXT("Unknown ids are refused"),Set(EProjectAffixMode::Custom,{TEXT("Z99")}).IsValid());

    FProjectMatchAffixes Weekly=Set(EProjectAffixMode::Weekly,{TEXT("A02"),TEXT("B09"),TEXT("C05")});Weekly.WeekName=TEXT("러시아워 주간");
    FProjectMatchAffixes Back;
    TestTrue(TEXT("JSON round trip keeps mode, ids and week name"),FProjectMatchAffixes::Parse(Weekly.ToJson(),Back)&&Back==Weekly);
    TestTrue(TEXT("Weekly title shows the week name"),Weekly.ModeTitle().Contains(TEXT("러시아워 주간")));
    TSharedPtr<FJsonObject> Empty;TSharedRef<TJsonReader<>> Reader=TJsonReaderFactory<>::Create(TEXT("{\"mode\":\"None\",\"ids\":null,\"weekName\":\"\"}"));
    TestTrue(TEXT("A room without affixes may send a null list"),FJsonSerializer::Deserialize(Reader,Empty)&&FProjectMatchAffixes::Parse(Empty,Back)&&!Back.IsActive());
    Reader=TJsonReaderFactory<>::Create(TEXT("{\"mode\":\"MasterPlus\",\"ids\":null}"));
    TestFalse(TEXT("An affix mode without ids is refused"),FJsonSerializer::Deserialize(Reader,Empty)&&FProjectMatchAffixes::Parse(Empty,Back));

    TestEqual(TEXT("Strongest slow is capped at 30%"),UProjectAffixRuntime::SpeedScale(.9f,0.f),.7f);
    TestEqual(TEXT("Strongest boost is capped at 25%"),UProjectAffixRuntime::SpeedScale(0.f,.9f),1.25f);
    TestEqual(TEXT("No affix leaves speed unchanged"),UProjectAffixRuntime::SpeedScale(0.f,0.f),1.f);
    return true;
}
#endif

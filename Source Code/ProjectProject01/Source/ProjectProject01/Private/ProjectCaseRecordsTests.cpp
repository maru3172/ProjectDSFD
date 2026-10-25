#if WITH_DEV_AUTOMATION_TESTS
#include "ProjectCaseRecords.h"
#include "ProjectCasePresentation.h"
#include "ProjectCaseStoryArchive.h"
#include "ProjectProject01AuthSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCaseRecordsTest,"ProjectProject01.CaseRecords.OfflineAchievementsAndIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCaseRecordsTest::RunTest(const FString&)
{
    // Dummy standalone world, with editor persistence disabled: no real slot/account files or HTTP.
    auto* Instance=NewObject<UGameInstance>(GEngine);Instance->InitializeStandalone();auto* World=Instance->GetWorld();
    ON_SCOPE_EXIT{Instance->Shutdown();if(World){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}};
    auto* R=Instance->GetSubsystem<UProjectCaseRecords>();
    if(!TestNotNull(TEXT("Campaign records exist without an online account"),R))return false;
    TestFalse(TEXT("Editor tests cannot overwrite packaged campaign achievements"),R->bPersistence);
    TestFalse(TEXT("Offline has no authenticated wallet or account state"),R->IsAuthenticated());
    TestTrue(TEXT("Checkpoint achievement earns offline"),R->RecordCampaignAchievement(TEXT("campaign_checkpoint")));
    TestTrue(TEXT("Escape achievement earns offline"),R->RecordCampaignAchievement(TEXT("campaign_escape")));
    TestTrue(TEXT("Repeated checkpoint event is safe"),R->RecordCampaignAchievement(TEXT("campaign_checkpoint")));
    TestEqual(TEXT("Duplicate events never create duplicate achievements"),R->GetLocalAchievements().Num(),2);
    TestFalse(TEXT("Campaign cannot mint a competitive server achievement"),R->RecordCampaignAchievement(TEXT("mannequin_victory")));
    R->Snapshot=MakeShared<FJsonObject>();R->Snapshot->SetNumberField(TEXT("credits"),100);
    R->ClearSession();TestFalse(TEXT("Logout removes the old account wallet"),R->Snapshot.IsValid());
    TestTrue(TEXT("Logout preserves offline campaign achievements"),R->HasCampaignAchievement(TEXT("campaign_checkpoint")));
    R->Local->PublicStoryArchivesJson=TEXT("{\"archives\":[{\"season\":\"RECORD 0\",\"stages\":[]}]}");
    FBufferArchive Bytes;
    {FObjectAndNameAsStringProxyArchive Save(Bytes,false);Save.ArIsSaveGame=true;R->Local->Serialize(Save);}
    auto* Reload=NewObject<UProjectCampaignAchievementSave>();
    {FMemoryReader Reader(Bytes);FObjectAndNameAsStringProxyArchive Load(Reader,true);Load.ArIsSaveGame=true;Reload->Serialize(Load);}
    TestEqual(TEXT("Achievement save preserves local identity"),Reload->SaveId,R->Local->SaveId);
    TestEqual(TEXT("Achievement save preserves earned records"),Reload->Earned.Num(),2);
    TestEqual(TEXT("Public story archives persist independently from the account snapshot"),Reload->PublicStoryArchivesJson,R->Local->PublicStoryArchivesJson);
    auto Public=MakeShared<FJsonObject>(),Season=MakeShared<FJsonObject>();Season->SetStringField(TEXT("season"),TEXT("RECORD 0"));
    TArray<TSharedPtr<FJsonValue>> Stages;for(int32 Part=1;Part<=13;++Part){auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("stage"),Part);Entry->SetStringField(TEXT("story"),TEXT("공개된 이야기"));Stages.Add(MakeShared<FJsonValueObject>(Entry));}Season->SetArrayField(TEXT("stages"),Stages);Public->SetArrayField(TEXT("archives"),{MakeShared<FJsonValueObject>(Season)});
    auto Empty=MakeShared<FJsonObject>();Empty->SetArrayField(TEXT("archives"),{});FString Encoded;TSharedPtr<FJsonObject> Merged;
    TestTrue(TEXT("Empty/stale public response preserves already stored seasons"),ProjectCaseStoryArchive::Merge(Public,Empty,Merged,Encoded)&&Merged->GetArrayField(TEXT("archives")).Num()==1);
    auto Private=MakeShared<FJsonObject>();Private->SetStringField(TEXT("userId"),TEXT("private-account"));
    TestFalse(TEXT("An account snapshot cannot be saved as a public archive"),ProjectCaseStoryArchive::Merge(Public,Private,Merged,Encoded));
    TestFalse(TEXT("Offline cannot bind records to an absent account"),R->CanImport());
    auto State=MakeShared<FJsonObject>();auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("slot"),TEXT("Frame"));Item->SetStringField(TEXT("itemId"),TEXT("investigation_seal_1"));State->SetArrayField(TEXT("loadout"),{MakeShared<FJsonValueObject>(Item)});
    TestTrue(TEXT("Rare investigation seal changes the visible profile frame"),ProjectCasePresentation::FrameColor(State)!=ProjectCasePresentation::FrameColor(nullptr));
    return true;
}
#endif

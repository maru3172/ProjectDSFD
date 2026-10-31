#if WITH_DEV_AUTOMATION_TESTS
#include "ProjectCaseRecords.h"
#include "ProjectCasePresentation.h"
#include "ProjectCaseAchievementPresentation.h"
#include "ProjectCaseStoryArchive.h"
#include "ProjectProject01AuthSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "ProjectAnnouncementSubsystem.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
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
    {
        auto* Viewport=NewObject<UGameViewportClient>(GEngine);
        auto Canvas=SNew(SOverlay);auto Window=SNew(SWindow).ClientSize(FVector2D(1280,720));
        // Isolated routing fixture: no real scene viewport, window, RHI, or HTTP connection.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
        Viewport->SetViewportOverlayWidget(Window,Canvas);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
        Instance->GetWorldContext()->GameViewport=Viewport;
        ON_SCOPE_EXIT{Instance->GetWorldContext()->GameViewport=nullptr;Viewport->RemoveAllViewportWidgets();};
        auto Quest=MakeShared<FJsonObject>();Quest->SetNumberField(TEXT("revision"),5);
        auto Old=MakeShared<FJsonObject>();Old->SetNumberField(TEXT("revision"),4);Old->SetStringField(TEXT("kind"),TEXT("Condition"));Old->SetStringField(TEXT("questName"),TEXT("탐사"));Old->SetStringField(TEXT("stepTitle"),TEXT("과거 조건"));
        auto Completed=MakeShared<FJsonObject>();Completed->SetNumberField(TEXT("revision"),5);Completed->SetStringField(TEXT("kind"),TEXT("Stage"));Completed->SetStringField(TEXT("questName"),TEXT("탐사"));Completed->SetStringField(TEXT("stepTitle"),TEXT("생존자 튜토리얼 완료"));
        Quest->SetArrayField(TEXT("notices"),{MakeShared<FJsonValueObject>(Old),MakeShared<FJsonValueObject>(Completed)});
        R->Snapshot=MakeShared<FJsonObject>();R->Snapshot->SetStringField(TEXT("userId"),TEXT("notice-fixture"));R->Snapshot->SetObjectField(TEXT("mainQuest"),Quest);
        auto Response=MakeShared<FJsonObject>();Response->SetArrayField(TEXT("questNotices"),{MakeShared<FJsonValueObject>(Completed)});
        R->ObserveMainQuest(Response);
        bool Stage=false,Detail=false,Historical=false;
        TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> W){if(W->GetType()==FName(TEXT("STextBlock"))){const auto T=StaticCastSharedRef<STextBlock>(W)->GetText().ToString();Stage|=T==TEXT("- 탐사 퀘스트 단계 완료 -");Detail|=T==TEXT("생존자 튜토리얼 완료");Historical|=T.Contains(TEXT("과거 조건"));}if(auto* C=W->GetChildren())for(int32 I=0;I<C->Num();++I)Inspect(C->GetChildAt(I));};Inspect(Canvas);
        TestTrue(TEXT("First tutorial completion response reaches the actual game viewport with its condition"),Stage&&Detail&&!Historical&&Canvas->GetChildren()->Num()==2);
        TestEqual(TEXT("Fresh completion is consumed once rather than mistaken for login history"),R->SeenQuestRevision,int64(5));
        R->ObserveMainQuest(Response);TestEqual(TEXT("Repeated completion does not attach duplicate overlay slots"),Canvas->GetChildren()->Num(),2);
        Viewport->RemoveAllViewportWidgets();
        Instance->GetSubsystem<UProjectAnnouncementSubsystem>()->NotifyQuest(TEXT("탐사"),TEXT("Complete"),TEXT("탐사"),TEXT("reattach-fixture"));
        TestEqual(TEXT("Tutorial UI cleanup cannot permanently detach completion notifications"),Canvas->GetChildren()->Num(),2);
        R->PendingTutorialRole=TEXT("Survivor");R->ClearSession();TestTrue(TEXT("Logging out clears pending tutorial completion and notification cursor"),R->PendingTutorialRole.IsEmpty()&&R->QuestAccount.IsEmpty()&&R->SeenQuestRevision==0);
    }
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectAchievementProgressTest,"ProjectProject01.CaseRecords.AchievementCountsAndRates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectAchievementProgressTest::RunTest(const FString&)
{
    auto State=MakeShared<FJsonObject>(),Checkpoint=MakeShared<FJsonObject>(),Escape=MakeShared<FJsonObject>(),Hidden=MakeShared<FJsonObject>();
    Checkpoint->SetStringField(TEXT("code"),TEXT("campaign_checkpoint"));Escape->SetStringField(TEXT("code"),TEXT("campaign_escape"));Hidden->SetStringField(TEXT("code"),TEXT("pvp_first"));
    State->SetArrayField(TEXT("achievements"),{MakeShared<FJsonValueObject>(Checkpoint),MakeShared<FJsonValueObject>(Escape),MakeShared<FJsonValueObject>(Hidden)});
    const auto Earned=ProjectCaseAchievements::Earned({TEXT("campaign_checkpoint")},State);
    TestEqual(TEXT("Local/account duplicate and hidden achievements do not inflate visible completion"),Earned.Num(),2);
    TestEqual(TEXT("Offline campaign has the same three visible definitions"),ProjectCaseAchievements::Definitions().Num(),3);
    auto Rate=MakeShared<FJsonObject>();Rate->SetStringField(TEXT("code"),TEXT("campaign_checkpoint"));Rate->SetNumberField(TEXT("percentage"),12.34);auto Rates=MakeShared<FJsonObject>();Rates->SetArrayField(TEXT("achievements"),{MakeShared<FJsonValueObject>(Rate)});
    TestEqual(TEXT("Global percentage is distinct from personal completion"),ProjectCaseAchievements::GlobalRate(Rates,TEXT("campaign_checkpoint")),FString(TEXT("전체 달성률 12.3%")));
    TestEqual(TEXT("Missing network data is not shown as a false zero"),ProjectCaseAchievements::GlobalRate(nullptr,TEXT("campaign_checkpoint")),FString(TEXT("전체 달성률 —")));
    return true;
}
#endif

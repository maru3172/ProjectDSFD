#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ProjectCaseRecords.h"
#include "ProjectProject01AuthSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ProjectUIRenderCapture.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Widgets/Input/SButton.h"
#include "InputCoreTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCaseUiRenderTest,"ProjectProject01.CaseRecords.RenderPages",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectCaseUiRenderTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Instance=NewObject<UGameInstance>();World->SetGameInstance(Instance);Instance->Init();
    auto* Auth=Instance->GetSubsystem<UProjectProject01AuthSubsystem>();auto* R=Instance->GetSubsystem<UProjectCaseRecords>();
    auto* Widget=NewObject<UProjectCaseRecordWidget>(World);auto* Dock=NewObject<UProjectCaseRecordDock>(World);
    ON_SCOPE_EXIT{Widget->ReleaseSlateResources(true);Dock->ReleaseSlateResources(true);Auth->ForceClearLocalSession();Instance->Shutdown();World->SetGameInstance(nullptr);World->DestroyWorld(false);};
    Auth->AuthState=EProjectProject01AuthState::SignedIn;Auth->AccessToken=TEXT("offline-render-fixture-only");Auth->SignedInDisplayName=TEXT("조사관");
    R->Request=FHttpModule::Get().CreateRequest(); // Never ProcessRequest: fixture cannot contact an operator's backend.
    const FString Json=TEXT(R"json({"userId":"1","level":7,"xp":4100,"levelStartXp":3600,"nextLevelXp":4900,"levelProgress":0.3846,"credits":500,
      "inventory":[],"loadout":[],"achievements":[],"catalogue":[],"missions":[
       {"id":"daily_play","name":"오늘의 조사: 경기 1회 완료","mode":"Pvp","weekly":false,"progress":1,"target":1,"reward":30,"claimed":true,"completed":true,"canReroll":false,"rerolls":0,"rerollCost":10},
       {"id":"daily_win","name":"생존자로 경기 1회 완료","mode":"Pvp","weekly":false,"progress":0,"target":1,"reward":30,"claimed":false,"completed":false,"canReroll":true,"rerolls":2,"rerollCost":30}],
      "partyState":{"role":"Survivor","invites":[],"party":{"id":"fixture","mode":"Open","leaderId":"1","maximum":3,"isLeader":true,"members":[{"userId":"1","displayName":"테스트 조사관","role":"Survivor"}]}},
      "season":{"incidentTimestamp":"202X-XX-XX 18:24:01","availability":"(~ 정식 출시 전까지)","investigation":{"enabled":true,"weeklyLimit":15,"collectedThisWeek":3,"remainingThisWeek":12,"records":[],"stages":[]}}})json");
    if(!TestTrue(TEXT("Offline render fixture decodes"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),R->Snapshot)))return false;
    TSharedPtr<FJsonObject> Collection;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"json({"bio":"폐점 이후의 기록을 수집합니다.","featured":[{"code":"campaign_escape"}],"catalogue":[
      {"id":"banner_afterhours","name":"폐점 이후","slot":"Banner"},{"id":"banner_embers","name":"잔불","slot":"Banner"},
      {"id":"stamp_checkpoint","name":"첫 기록 인장","slot":"Stamp","achievement":"campaign_checkpoint"},
      {"id":"badge_exit","name":"탈출구 기념 배지","slot":"Badge","achievement":"campaign_escape"},
      {"id":"plate_training","name":"교육 직원 명찰","slot":"Nameplate","achievement":"bot_first"},{"id":"icon_receipt","name":"오래된 영수증","slot":"Icon"}],
      "draw":{"revision":1,"tickets":5,"pity":83,"pityAvailable":true,"active":true,"pool":{"title":"RECORD 0","prizes":[
       {"id":"a","name":"폐점 이후","grade":1,"itemId":"banner_afterhours","probability":1},{"id":"b","name":"잔불","grade":2,"itemId":"banner_embers","probability":5},
       {"id":"c","name":"보관 문서 테두리","grade":3,"itemId":"frame_archive","probability":10},{"id":"d","name":"오래된 영수증","grade":4,"itemId":"icon_receipt","probability":20},
       {"id":"e","name":"기록 포인트","grade":5,"credits":5,"probability":64}],"guaranteePrizes":[{"id":"a","probability":100}]}}})json"))),Collection);
    struct FExtraDrawItem{const TCHAR* Id;const TCHAR* Name;const TCHAR* Slot;int32 Grade;};
    auto DrawPool=Collection->GetObjectField(TEXT("draw"))->GetObjectField(TEXT("pool"));auto DrawPrizes=DrawPool->GetArrayField(TEXT("prizes"));auto DrawCatalogue=Collection->GetArrayField(TEXT("catalogue"));
    for(const FExtraDrawItem& Extra:TArray<FExtraDrawItem>{{TEXT("banner_lastlight"),TEXT("마지막 점등"),TEXT("Banner"),1},{TEXT("banner_silentfloor"),TEXT("침묵의 층"),TEXT("Banner"),1},{TEXT("frame_burntcase"),TEXT("그을린 사건철"),TEXT("Frame"),1},{TEXT("banner_emergency"),TEXT("비상등 아래"),TEXT("Banner"),2},{TEXT("banner_shutter"),TEXT("닫힌 셔터"),TEXT("Banner"),2},{TEXT("plate_nightwatch"),TEXT("야간 경비 명찰"),TEXT("Nameplate"),2},{TEXT("seal_unresolved"),TEXT("미결 사건 인장"),TEXT("Stamp"),2},{TEXT("icon_masterkey"),TEXT("분실된 마스터키"),TEXT("Icon"),2},{TEXT("frame_carbon"),TEXT("먹지 테두리"),TEXT("Frame"),3},{TEXT("frame_evidence"),TEXT("증거 봉투 테두리"),TEXT("Frame"),3},{TEXT("banner_escalator"),TEXT("멈춘 에스컬레이터"),TEXT("Banner"),3},{TEXT("banner_service"),TEXT("직원 전용 통로"),TEXT("Banner"),3},{TEXT("plate_lostfound"),TEXT("분실물 보관 명찰"),TEXT("Nameplate"),3},{TEXT("stamp_closed"),TEXT("폐점 확인 소인"),TEXT("Stamp"),3},{TEXT("badge_rooftop"),TEXT("옥상 출입 배지"),TEXT("Badge"),3},{TEXT("icon_floormap"),TEXT("접힌 층별 안내도"),TEXT("Icon"),3},{TEXT("icon_ticket"),TEXT("빛바랜 대기표"),TEXT("Icon"),4},{TEXT("icon_locker"),TEXT("보관함 열쇠"),TEXT("Icon"),4},{TEXT("stamp_received"),TEXT("접수 완료 도장"),TEXT("Stamp"),4},{TEXT("stamp_checked"),TEXT("열람 확인 도장"),TEXT("Stamp"),4},{TEXT("badge_basement"),TEXT("지하층 기념 배지"),TEXT("Badge"),4},{TEXT("badge_service"),TEXT("안내 데스크 배지"),TEXT("Badge"),4},{TEXT("frame_staple"),TEXT("스테이플 문서 테두리"),TEXT("Frame"),4},{TEXT("plate_archive"),TEXT("기록실 직원 명찰"),TEXT("Nameplate"),4}})
    {auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),Extra.Id);J->SetStringField(TEXT("itemId"),Extra.Id);J->SetStringField(TEXT("name"),Extra.Name);J->SetStringField(TEXT("slot"),Extra.Slot);J->SetNumberField(TEXT("grade"),Extra.Grade);J->SetNumberField(TEXT("probability"),3);DrawPrizes.Add(MakeShared<FJsonValueObject>(J));DrawCatalogue.Add(MakeShared<FJsonValueObject>(J));}DrawPool->SetArrayField(TEXT("prizes"),DrawPrizes);Collection->SetArrayField(TEXT("catalogue"),DrawCatalogue);
    R->Snapshot->SetObjectField(TEXT("collection"),Collection);
    {
        const auto Original=R->Snapshot;auto NewState=MakeShared<FJsonObject>();NewState->Values=Original->Values;
        auto NewCollection=MakeShared<FJsonObject>();NewCollection->Values=Collection->Values;NewState->SetObjectField(TEXT("collection"),NewCollection);NewState->SetNumberField(TEXT("credits"),0);
        auto NewDraw=MakeShared<FJsonObject>();NewDraw->Values=Collection->GetObjectField(TEXT("draw"))->Values;NewDraw->SetNumberField(TEXT("pity"),17);NewDraw->SetNumberField(TEXT("tickets"),2);
        auto Response=MakeShared<FJsonObject>();Response->SetObjectField(TEXT("snapshot"),NewState);Response->SetObjectField(TEXT("draw"),NewDraw);R->PendingDrawId=TEXT("pending-fixture");R->ApplyDrawResponse(Response);
        TestTrue(TEXT("A draw response replaces wallet/inventory state immediately"),R->Snapshot==NewState&&R->Snapshot->GetNumberField(TEXT("credits"))==0);
        TestEqual(TEXT("New stack appears without another polling request"),R->Snapshot->GetObjectField(TEXT("collection"))->GetObjectField(TEXT("draw"))->GetNumberField(TEXT("pity")),17.0);
        TestTrue(TEXT("Draw success resolves the pending idempotency request"),R->PendingDrawId.IsEmpty());R->Snapshot=Original;R->DrawResult.Reset();
        TestEqual(TEXT("A complete response avoids an immediate duplicate account poll"),R->PollIn,15.f);
    }
    R->Local->Earned.Add(TEXT("campaign_checkpoint"));
    auto Escaped=MakeShared<FJsonObject>();Escaped->SetStringField(TEXT("code"),TEXT("campaign_escape"));R->Snapshot->SetArrayField(TEXT("achievements"),{MakeShared<FJsonValueObject>(Escaped)});
    TArray<TSharedPtr<FJsonValue>> AchievementRewards;
    for(const FString Code:{TEXT("campaign_checkpoint"),TEXT("campaign_escape"),TEXT("campaign_complete"),TEXT("pvp_first"),TEXT("bot_first"),TEXT("survivor_escape"),TEXT("mannequin_victory")})
    {auto Reward=MakeShared<FJsonObject>();Reward->SetStringField(TEXT("code"),Code);Reward->SetBoolField(TEXT("earned"),Code==TEXT("campaign_escape")||Code==TEXT("pvp_first"));Reward->SetBoolField(TEXT("claimed"),false);AchievementRewards.Add(MakeShared<FJsonValueObject>(Reward));}
    R->Snapshot->SetArrayField(TEXT("achievementRewards"),AchievementRewards);
    R->AchievementRateBase=Auth->GetApiBaseUrlForAuthenticatedRequest();R->AchievementRates=MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> RateRows;for(const FString Code:{FString(TEXT("campaign_checkpoint")),FString(TEXT("campaign_escape")),FString(TEXT("campaign_complete"))})
    {auto Rate=MakeShared<FJsonObject>();Rate->SetStringField(TEXT("code"),Code);Rate->SetNumberField(TEXT("percentage"),Code==TEXT("campaign_checkpoint")?75:Code==TEXT("campaign_escape")?25:0);RateRows.Add(MakeShared<FJsonValueObject>(Rate));}R->AchievementRates->SetArrayField(TEXT("achievements"),RateRows);
    TArray<TSharedPtr<FJsonValue>> FullMissions;
    for(const FString Mode:{FString(TEXT("Pvp")),FString(TEXT("Bot"))})for(int32 I=0;I<4;++I)
    {
        auto M=MakeShared<FJsonObject>();M->Values=R->Snapshot->GetArrayField(TEXT("missions"))[I%2]->AsObject()->Values;
        M->SetStringField(TEXT("mode"),Mode);M->SetStringField(TEXT("id"),FString::FromInt(I));M->SetBoolField(TEXT("weekly"),I>=2);M->SetStringField(TEXT("period"),I>=2?TEXT("fixture-week"):TEXT("fixture-day"));
        FullMissions.Add(MakeShared<FJsonValueObject>(M));
    }
    R->Snapshot->SetArrayField(TEXT("missions"),FullMissions);
    TArray<TSharedPtr<FJsonValue>> Catalogue;
    const TArray<FString> ItemIds{TEXT("frame_crimson"),TEXT("frame_silver"),TEXT("icon_file"),TEXT("icon_seal"),TEXT("title_witness")};
    const TArray<FString> ItemNames{TEXT("붉은 봉인 테두리"),TEXT("조사관 테두리"),TEXT("사건 기록 아이콘"),TEXT("증거 봉인 아이콘"),TEXT("목격자 명찰")};
    for(int32 I=0;I<ItemIds.Num();++I){auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("id"),ItemIds[I]);Item->SetStringField(TEXT("name"),ItemNames[I]);Item->SetStringField(TEXT("slot"),ItemIds[I].StartsWith(TEXT("frame_"))?TEXT("Frame"):ItemIds[I].StartsWith(TEXT("icon_"))?TEXT("Icon"):TEXT("Nameplate"));Item->SetNumberField(TEXT("price"),100);Catalogue.Add(MakeShared<FJsonValueObject>(Item));}
    R->Snapshot->SetArrayField(TEXT("catalogue"),Catalogue);
    auto OwnedFrame=MakeShared<FJsonObject>();OwnedFrame->SetStringField(TEXT("itemId"),TEXT("level_frame_25"));R->Snapshot->SetArrayField(TEXT("inventory"),{MakeShared<FJsonValueObject>(OwnedFrame)});
    TArray<TSharedPtr<FJsonValue>> Rewards;
    for(int32 Level=5;Level<=100;Level+=5){auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("level"),Level);J->SetNumberField(TEXT("credits"),100+Level*10);J->SetBoolField(TEXT("available"),Level==5);J->SetBoolField(TEXT("claimed"),false);Rewards.Add(MakeShared<FJsonValueObject>(J));}R->Snapshot->SetArrayField(TEXT("levelRewards"),Rewards);
    auto Investigation=R->Snapshot->GetObjectField(TEXT("season"))->GetObjectField(TEXT("investigation"));TArray<TSharedPtr<FJsonValue>> Pieces;
    for(int32 I=0;I<117;++I)
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),FString::FromInt(I));J->SetStringField(TEXT("title"),TEXT("서관 출입 기록"));J->SetStringField(TEXT("description"),I%3==0?TEXT("폐점"):I%3==1?TEXT("경보"):TEXT("서관"));
        J->SetNumberField(TEXT("stage"),I/9+1);J->SetNumberField(TEXT("part"),I%9+1);J->SetNumberField(TEXT("points"),30);J->SetBoolField(TEXT("discovered"),I<3);J->SetBoolField(TEXT("stageAvailable"),I<9);J->SetBoolField(TEXT("availableToday"),true);J->SetBoolField(TEXT("claimed"),false);Pieces.Add(MakeShared<FJsonValueObject>(J));
    }
    Investigation->SetArrayField(TEXT("records"),Pieces);
    static_cast<UUserWidget*>(Widget)->Initialize();static_cast<UUserWidget*>(Dock)->Initialize();auto Slate=Widget->TakeWidget();auto DockSlate=Dock->TakeWidget();
    R->Request.Reset(); // Construction completed without HTTP; the synchronous render does not run the core ticker.
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("CaseRecordUIQA");IFileManager::Get().MakeDirectory(*Directory,true);FWidgetRenderer Renderer(false);
    R->bPersistence=false;R->ConfirmationDays.Reset();
    R->Request=FHttpModule::Get().CreateRequest();R->Request->SetVerb(TEXT("POST"));
    R->MainQuestAction(TEXT("TutorialComplete"),TEXT("Survivor"));
    TestTrue(TEXT("Finishing a tutorial during another account request retains the completion for this session"),R->PendingTutorialRole==TEXT("Survivor")&&R->PendingTutorialGeneration==Auth->GetSessionGeneration());
    R->Request.Reset();R->PendingTutorialRole.Reset();
    const auto CaptureTransaction=[&](const FString& Name)
    {
        for(const FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
        {
            auto* Target=ProjectUIRenderCapture::Create(Size);const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
            for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,Slate,Dpi,FVector2D(Size),0);
            FlushRenderingCommands();TArray<FColor> Pixels;
            if(ProjectUIRenderCapture::Read(Target,Pixels)){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("%s_%dx%d.png"),*Name,Size.X,Size.Y)));}
            Target->ReleaseResource();
        }
    };
    int32 Confirmed=0;
    Widget->Page=TEXT("Draw");Widget->RefreshPanel();
    Widget->ConfirmAction(TEXT("Draw"),TEXT("계속 진행하시겠습니까?"),[&]{++Confirmed;});
    TestTrue(TEXT("Opening a confirmation neither spends nor runs the action"),Confirmed==0&&Widget->TransactionLayer->GetChildren()->Num()==1&&!R->Request.IsValid());
    CaptureTransaction(TEXT("DrawConfirmation"));Widget->ResolveConfirmation(false,false);
    TestTrue(TEXT("No cancels without saving a skip preference"),Confirmed==0&&R->ShouldConfirm(TEXT("Draw")));
    Widget->ConfirmAction(TEXT("Purchase"),TEXT("구매하시겠습니까?"),[&]{++Confirmed;});CaptureTransaction(TEXT("PurchaseConfirmation"));Widget->ResolveConfirmation(true,false);
    TestTrue(TEXT("Yes without the checkbox still asks on the next purchase"),Confirmed==1&&R->ShouldConfirm(TEXT("Purchase")));
    Widget->ConfirmAction(TEXT("Draw"),TEXT("계속 진행하시겠습니까?"),[&]{++Confirmed;});Widget->ResolveConfirmation(true,true);
    TestTrue(TEXT("Daily draw preference covers draw sizes without silencing point purchases"),Confirmed==2&&!R->ShouldConfirm(TEXT("Draw"))&&R->ShouldConfirm(TEXT("Purchase")));
    Widget->ConfirmAction(TEXT("Draw"),TEXT("계속 진행하시겠습니까?"),[&]{++Confirmed;});TestTrue(TEXT("Suppressed draw proceeds once without a modal"),Confirmed==3&&Widget->TransactionLayer->GetChildren()->Num()==0);
    Widget->ConfirmAction(TEXT("Purchase"),TEXT("구매하시겠습니까?"),[&]{++Confirmed;});Widget->ResolveConfirmation(false,true);
    TestTrue(TEXT("No never buys, while the checked daily preference is remembered"),Confirmed==3&&!R->ShouldConfirm(TEXT("Purchase")));
    R->ConfirmationDays.Add(R->ConfirmationScope()+TEXT(".Draw"),(FDateTime::Now()-FTimespan::FromDays(1)).ToString(TEXT("%Y-%m-%d")));
    TestTrue(TEXT("Yesterday's preference expires at the calendar-day boundary"),R->ShouldConfirm(TEXT("Draw")));
    Widget->ConfirmAction(TEXT("Draw"),TEXT("계속 진행하시겠습니까?"),[&]{++Confirmed;});R->Snapshot->SetStringField(TEXT("userId"),TEXT("other"));Widget->ResolveConfirmation(true,true);
    TestTrue(TEXT("Account changes invalidate a pending confirmation and do not copy daily preferences"),Confirmed==3&&R->ShouldConfirm(TEXT("Draw")));R->Snapshot->SetStringField(TEXT("userId"),TEXT("1"));
    {
        int32 Requests=0;FString LastBody;R->ReviewRequestSink=[&](const FString& Path,const FString& Body){if(Path==TEXT("/api/case-records/action")){++Requests;LastBody=Body;}};
        ON_SCOPE_EXIT{R->ReviewRequestSink=nullptr;if(R->Request){R->Request->OnProcessRequestComplete().Unbind();R->Request.Reset();}};
        for(const FString Mode:{FString(TEXT("Pvp")),FString(TEXT("Bot"))})for(const bool Weekly:{false,true})
        {
            Widget->Page=Mode;Widget->RefreshPanel();
            const FString Id=Weekly?TEXT("3"):TEXT("1"),Period=Weekly?TEXT("fixture-week"):TEXT("fixture-day");const int32 Before=Requests;
            Widget->RequestMissionReroll(Id,Mode,2,Period);TestTrue(TEXT("Every PvP/bot daily/weekly reroll opens its confirmation before sending HTTP"),Widget->TransactionLayer->GetChildren()->Num()==1&&Requests==Before);
            if(Mode==TEXT("Pvp")&&!Weekly)CaptureTransaction(TEXT("MissionRerollConfirmation"));
            Widget->ResolveConfirmation(false,false);TestEqual(TEXT("No cancels a mission reroll without spending"),Requests,Before);
            Widget->RequestMissionReroll(Id,Mode,2,Period);Widget->ResolveConfirmation(true,false);
            TestTrue(TEXT("Yes sends exactly one mission reroll and preserves the selected slot revision"),Requests==Before+1&&R->Request.IsValid()&&LastBody.Contains(TEXT("expectedRerolls")));
            R->Request->OnProcessRequestComplete().Unbind();R->Request.Reset();
        }
        Widget->RequestMissionReroll(TEXT("1"),TEXT("Pvp"),2,TEXT("fixture-day"));
        auto Mission=FullMissions[1]->AsObject();Mission->SetStringField(TEXT("period"),TEXT("next-day"));const int32 Before=Requests;Widget->ResolveConfirmation(true,false);
        TestTrue(TEXT("A reset while the confirmation is open cannot reroll a new period's mission"),Requests==Before&&!R->Request.IsValid());Mission->SetStringField(TEXT("period"),TEXT("fixture-day"));R->Status.Reset();
        Widget->RequestMissionReroll(TEXT("1"),TEXT("Pvp"),2,TEXT("fixture-day"));Widget->ResolveConfirmation(false,true);
        TestTrue(TEXT("The daily checkbox suppresses mission reroll questions without executing No"),Requests==Before&&!R->ShouldConfirm(TEXT("MissionReroll")));
        Widget->RequestMissionReroll(TEXT("3"),TEXT("Bot"),2,TEXT("fixture-week"));TestTrue(TEXT("Daily suppression covers both mission periods and modes"),Requests==Before+1&&Widget->TransactionLayer->GetChildren()->Num()==0);
        R->Request->OnProcessRequestComplete().Unbind();R->Request.Reset();R->ConfirmationDays.Add(R->ConfirmationScope()+TEXT(".MissionReroll"),(FDateTime::Now()-FTimespan::FromDays(1)).ToString(TEXT("%Y-%m-%d")));
        TestTrue(TEXT("Mission reroll confirmation returns on the next day"),R->ShouldConfirm(TEXT("MissionReroll")));
        Widget->Page=TEXT("Collection");Widget->RefreshPanel();
        for(const FString Action:{FString(TEXT("Equip")),FString(TEXT("ShowBanner")),FString(TEXT("HideBanner"))})
        {
            const int32 BeforeCosmetic=Requests;Widget->RequestCosmeticAction(Action,TEXT("frame_silver"));
            TestTrue(TEXT("Applying or changing cosmetic display asks before sending the operation"),Widget->TransactionLayer->GetChildren()->Num()==1&&Requests==BeforeCosmetic);
            if(Action==TEXT("Equip"))CaptureTransaction(TEXT("CosmeticApplyConfirmation"));
            Widget->ResolveConfirmation(false,false);TestEqual(TEXT("No leaves equipped cosmetics and displayed banners unchanged"),Requests,BeforeCosmetic);
            Widget->RequestCosmeticAction(Action,TEXT("frame_silver"));Widget->ResolveConfirmation(true,false);
            TestTrue(TEXT("Yes submits the chosen cosmetic action exactly once"),Requests==BeforeCosmetic+1&&LastBody.Contains(Action));R->Request->OnProcessRequestComplete().Unbind();R->Request.Reset();
        }
        const int32 BeforeSkip=Requests;Widget->RequestCosmeticAction(TEXT("Equip"),TEXT("frame_silver"));Widget->ResolveConfirmation(false,true);
        TestTrue(TEXT("Cosmetic confirmation has an independent daily checkbox"),!R->ShouldConfirm(TEXT("CosmeticApply"))&&R->ShouldConfirm(TEXT("MissionReroll"))&&Requests==BeforeSkip);
        Widget->RequestCosmeticAction(TEXT("Equip"),TEXT("frame_crimson"));TestTrue(TEXT("Checked cosmetic preference applies without another modal today"),Requests==BeforeSkip+1&&Widget->TransactionLayer->GetChildren()->Num()==0);R->Request->OnProcessRequestComplete().Unbind();R->Request.Reset();
        R->ConfirmationDays.Add(R->ConfirmationScope()+TEXT(".CosmeticApply"),(FDateTime::Now()-FTimespan::FromDays(1)).ToString(TEXT("%Y-%m-%d")));TestTrue(TEXT("Cosmetic confirmation returns the following day"),R->ShouldConfirm(TEXT("CosmeticApply")));
    }
    for(const auto& Entry:TArray<TPair<FString,FString>>{{TEXT("구매하셨습니다"),TEXT("frame_silver")},{TEXT("획득하셨습니다"),TEXT("stamp_checkpoint")}})
    {
        R->LastReceipt=MakeShared<FJsonObject>();R->LastReceipt->SetStringField(TEXT("title"),Entry.Key);auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("itemId"),Entry.Value);Item->SetStringField(TEXT("name"),Entry.Value==TEXT("frame_silver")?TEXT("은빛 기록 테두리"):TEXT("첫 기록 인장"));Item->SetNumberField(TEXT("count"),1);
        R->LastReceipt->SetArrayField(TEXT("items"),{MakeShared<FJsonValueObject>(Item)});Widget->ShowReceipt();TestEqual(TEXT("Successful receipt shows a single acknowledgement modal"),Widget->TransactionLayer->GetChildren()->Num(),1);
        CaptureTransaction(Entry.Key==TEXT("구매하셨습니다")?TEXT("PurchaseReceipt"):TEXT("AchievementReceipt"));Widget->TransactionLayer->ClearChildren();R->LastReceipt.Reset();
    }
    auto Quest=MakeShared<FJsonObject>();Quest->SetStringField(TEXT("name"),TEXT("탐사"));Quest->SetStringField(TEXT("baseStory"),TEXT("폐점 안내가 끝난 뒤, 조사 기록의 첫 장을 펼쳤다."));Quest->SetNumberField(TEXT("stage"),0);Quest->SetBoolField(TEXT("completed"),false);
    TArray<TSharedPtr<FJsonValue>> QuestSteps;const TArray<FString> QuestTitles{TEXT("생존자 튜토리얼 완료"),TEXT("어떤 매칭이든 생존자로 게임 1번 플레이"),TEXT("3층 맵 탈출하기"),TEXT("2층 맵 탈출하기"),TEXT("백화점을 무사히 탈출하기"),TEXT("마네킹 튜토리얼 완료"),TEXT("마네킹으로 플레이어 1명 잡기"),TEXT("마네킹으로 승리하기")};
    for(int32 I=0;I<8;++I){auto S=MakeShared<FJsonObject>();S->SetStringField(TEXT("id"),I==0?TEXT("survivor_tutorial"):I==5?TEXT("mannequin_tutorial"):FString::FromInt(I));S->SetStringField(TEXT("title"),QuestTitles[I]);S->SetBoolField(TEXT("active"),I==0);S->SetBoolField(TEXT("completed"),false);S->SetBoolField(TEXT("canSkip"),I==0);auto G=MakeShared<FJsonObject>();G->SetStringField(TEXT("label"),I==1?TEXT("생존자로 경기 1회 플레이"):QuestTitles[I]);G->SetNumberField(TEXT("progress"),0);G->SetNumberField(TEXT("target"),1);S->SetArrayField(TEXT("goals"),{MakeShared<FJsonValueObject>(G)});QuestSteps.Add(MakeShared<FJsonValueObject>(S));}
    Quest->SetArrayField(TEXT("steps"),QuestSteps);R->Snapshot->SetObjectField(TEXT("mainQuest"),Quest);
    {
        auto Started=MakeShared<FJsonObject>();Started->Values=Quest->Values;Started->SetNumberField(TEXT("stage"),1);Started->SetNumberField(TEXT("revision"),1);
        auto Update=MakeShared<FJsonObject>();Update->SetStringField(TEXT("userId"),TEXT("1"));Update->SetObjectField(TEXT("mainQuest"),Started);Update->SetArrayField(TEXT("questNotices"),{});
        R->Request=FHttpModule::Get().CreateRequest();R->Request->SetVerb(TEXT("GET"));R->PollIn=15;R->Refresh();
        TestTrue(TEXT("A server event during an older request schedules a fresh read without a duplicate HTTP request"),R->PollIn==0&&R->Request.IsValid());
        R->ApplyMainQuestUpdate(Update);
        TestTrue(TEXT("Game admission applies quest progress immediately and cancels the older account poll"),!R->Request.IsValid()&&R->Snapshot->GetObjectField(TEXT("mainQuest"))->GetNumberField(TEXT("stage"))==1&&R->Snapshot->HasField(TEXT("credits")));
        Update->SetStringField(TEXT("userId"),TEXT("other-account"));auto Wrong=MakeShared<FJsonObject>();Wrong->SetNumberField(TEXT("revision"),2);Update->SetObjectField(TEXT("mainQuest"),Wrong);R->ApplyMainQuestUpdate(Update);
        TestEqual(TEXT("Admission updates cannot replace another account's quest state"),R->Snapshot->GetObjectField(TEXT("mainQuest"))->GetNumberField(TEXT("stage")),1.0);
        R->Snapshot->SetObjectField(TEXT("mainQuest"),Quest);R->QuestAccount.Reset();R->SeenQuestRevision=0;
    }
    {
        Widget->Page=TEXT("Quest");Widget->RenderedFingerprint.Reset();Widget->RefreshPanel();
        const auto OriginalContent=Widget->Body->GetChildren()->GetChildAt(0);
        R->Request=FHttpModule::Get().CreateRequest();R->Request->SetVerb(TEXT("GET"));Widget->RefreshPanel();
        TestTrue(TEXT("A background account read preserves quest widgets and leaves their controls usable"),Widget->Body->GetChildren()->GetChildAt(0)==OriginalContent&&Widget->Body->IsEnabled());
        auto* RequestTarget=ProjectUIRenderCapture::Create(FIntPoint(1280,720));const float RequestDpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(1280,720));
        TArray<FColor> BeforeRequest,DuringRequest;for(int32 I=0;I<3;++I)Renderer.DrawWidget(RequestTarget,Slate,RequestDpi,FVector2D(1280,720),0);FlushRenderingCommands();ProjectUIRenderCapture::Read(RequestTarget,BeforeRequest);
        R->Request->SetVerb(TEXT("POST"));Widget->RefreshPanel();
        TestTrue(TEXT("A write blocks duplicate clicks without dimming or recreating the page"),Widget->Body->GetChildren()->GetChildAt(0)==OriginalContent&&Widget->Body->IsEnabled()&&Widget->RequestShield->GetVisibility()==EVisibility::Visible);
        for(int32 I=0;I<3;++I)Renderer.DrawWidget(RequestTarget,Slate,RequestDpi,FVector2D(1280,720),0);FlushRenderingCommands();ProjectUIRenderCapture::Read(RequestTarget,DuringRequest);RequestTarget->ReleaseResource();
        TestTrue(TEXT("A write's transparent input shield leaves the rendered page pixels unchanged"),!BeforeRequest.IsEmpty()&&BeforeRequest==DuringRequest);
        R->Request.Reset();R->Status=TEXT("처리되었습니다.");Widget->RefreshPanel();
        TestTrue(TEXT("Status-only completion unlocks the same content rather than resetting reading position"),Widget->Body->GetChildren()->GetChildAt(0)==OriginalContent&&Widget->Body->IsEnabled());
        R->Snapshot->SetNumberField(TEXT("credits"),501);Widget->RefreshPanel();
        TestTrue(TEXT("Unrelated wallet updates do not rebuild the quest reading pane"),Widget->Body->GetChildren()->GetChildAt(0)==OriginalContent);
        R->Snapshot->SetNumberField(TEXT("credits"),500);R->Status.Reset();Widget->RenderedFingerprint.Reset();
    }
    {
        R->LastAchievementRateFetchAt=FPlatformTime::Seconds();Widget->Page=TEXT("Achievements");Widget->RefreshPanel();
        const auto OriginalContent=Widget->Body->GetChildren()->GetChildAt(0);
        R->AchievementRates->SetStringField(TEXT("updatedAtUtc"),TEXT("2030-01-01T00:00:00Z"));Widget->RefreshPanel();
        TestTrue(TEXT("Refreshing the statistics timestamp alone preserves achievement widgets"),Widget->Body->GetChildren()->GetChildAt(0)==OriginalContent);
        RateRows[0]->AsObject()->SetNumberField(TEXT("percentage"),76);Widget->RefreshPanel();
        TestTrue(TEXT("Changed achievement percentages still rebuild the displayed values"),Widget->Body->GetChildren()->GetChildAt(0)!=OriginalContent);
        RateRows[0]->AsObject()->SetNumberField(TEXT("percentage"),75);Widget->RenderedFingerprint.Reset();
    }
    const auto PartyState=R->Snapshot->GetObjectField(TEXT("partyState")),Party=PartyState->GetObjectField(TEXT("party"));
    const auto OriginalMembers=Party->GetArrayField(TEXT("members"));
    for(int32 Count:{1,3,2,0})
    {
        auto Members=OriginalMembers;
        for(int32 I=1;I<Count;++I){auto M=MakeShared<FJsonObject>();M->SetStringField(TEXT("userId"),FString::FromInt(I+1));M->SetStringField(TEXT("displayName"),FString::Printf(TEXT("파티원 %d"),I));M->SetNumberField(TEXT("level"),I+2);Members.Add(MakeShared<FJsonValueObject>(M));}
        Party->SetArrayField(TEXT("members"),Members);
        if(Count==0)PartyState->SetField(TEXT("party"),MakeShared<FJsonValueNull>());else PartyState->SetObjectField(TEXT("party"),Party);
    for(FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
    {
        auto* Target=ProjectUIRenderCapture::Create(Size);const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
        for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,DockSlate,Dpi,FVector2D(Size),0);
        FlushRenderingCommands();TArray<FColor> Pixels;
        if(TestTrue(TEXT("Persistent account/party dock renders"),ProjectUIRenderCapture::Read(Target,Pixels)))
        {TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("Dock%d_%dx%d.png"),Count,Size.X,Size.Y)));}
        TSharedPtr<SButton> Invite;TSharedPtr<SWidget> Experience;bool InviteShown=false,InviteEnabled=false;
        TFunction<void(TSharedRef<SWidget>,bool,bool)> FindInvite=[&](TSharedRef<SWidget> Node,bool ParentShown,bool ParentEnabled)
        {const bool Shown=ParentShown&&Node->GetVisibility().IsVisible(),Enabled=ParentEnabled&&Node->IsEnabled();if(Node->GetType()==FName(TEXT("SProgressBar")))Experience=Node;if(Node->GetType()==FName(TEXT("SButton"))){auto B=StaticCastSharedRef<SButton>(Node);if(B->GetContent()->GetType()==FName(TEXT("SProjectDFSDIcon"))){Invite=B;InviteShown=Shown;InviteEnabled=Shown&&Enabled;}}if(auto* Children=Node->GetChildren())for(int32 I=0;I<Children->Num();++I)FindInvite(Children->GetChildAt(I),Shown,Enabled);};FindInvite(DockSlate,true,true);
        if(TestTrue(TEXT("Party invite is an icon button"),Invite.IsValid()))
        {TestEqual(TEXT("Party invite is unavailable at three and recovers when space opens"),InviteEnabled,Count<3);TestEqual(TEXT("Full party hides its plus instead of keeping a disabled plus"),InviteShown,Count<3);}
        if(TestTrue(TEXT("Own experience bar stays present"),Experience.IsValid()))
        {const auto G=Experience->GetCachedGeometry();const auto P=G.GetAbsolutePosition(),E=G.GetAbsoluteSize();auto Pixel=[&](float Fraction){int32 X=FMath::Clamp(int32(P.X+E.X*Fraction),0,Size.X-1),Y=FMath::Clamp(int32(P.Y+E.Y*.5),0,Size.Y-1);return Pixels[Y*Size.X+X];};TestTrue(TEXT("Experience fill is visibly distinguishable from its background"),Pixel(.2f).R>Pixel(.8f).R+20);}
        TSharedPtr<SWidget> Card;TFunction<void(TSharedRef<SWidget>)> FindCard=[&](TSharedRef<SWidget> Node)
        {
            if(Card)return;if(Node->GetType()==FName(TEXT("SBox"))){Card=Node;return;}
            if(auto* Children=Node->GetChildren())for(int32 I=0;I<Children->Num();++I)FindCard(Children->GetChildAt(I));
        };FindCard(DockSlate);
        if(TestTrue(TEXT("Dock has a bounded card"),Card.IsValid()))
        {const auto G=Card->GetCachedGeometry();const auto P=G.GetAbsolutePosition(),Extent=G.GetAbsoluteSize();TestTrue(TEXT("Left footer group fits and leaves space for four social icons"),P.X>=0&&P.Y>=0&&P.X+Extent.X<=Size.X-110&&P.Y+Extent.Y<=Size.Y+1);}
        Target->ReleaseResource();
    }
    }
    Party->SetArrayField(TEXT("members"),OriginalMembers);PartyState->SetObjectField(TEXT("party"),Party);
    const auto OriginalLoadout=R->Snapshot->GetArrayField(TEXT("loadout"));auto Banner=MakeShared<FJsonObject>();Banner->SetStringField(TEXT("slot"),TEXT("Banner"));Banner->SetStringField(TEXT("itemId"),TEXT("banner_afterhours"));R->Snapshot->SetArrayField(TEXT("loadout"),{MakeShared<FJsonValueObject>(Banner)});
    for(FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
    {
        auto* Target=ProjectUIRenderCapture::Create(Size);const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);for(int Pass=0;Pass<3;Pass++)Renderer.DrawWidget(Target,DockSlate,Dpi,FVector2D(Size),0);FlushRenderingCommands();TArray<FColor> Pixels;
        if(TestTrue(TEXT("Equipped banner renders inside the existing profile dock"),ProjectUIRenderCapture::Read(Target,Pixels))){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("DockBanner_%dx%d.png"),Size.X,Size.Y)));}Target->ReleaseResource();
    }
    R->Snapshot->SetArrayField(TEXT("loadout"),OriginalLoadout);
    for(const FString Page:{FString(TEXT("Profile")),FString(TEXT("ProfileClaimed")),FString(TEXT("ProfileEdit")),FString(TEXT("Collection")),FString(TEXT("CollectionBanner")),FString(TEXT("CollectionFrame")),FString(TEXT("Draw")),FString(TEXT("DrawReset")),FString(TEXT("DrawInactive")),FString(TEXT("DrawNoFunds")),FString(TEXT("DrawRates")),FString(TEXT("DrawResult")),FString(TEXT("DrawRare")),FString(TEXT("Events")),FString(TEXT("Achievements")),FString(TEXT("OnlineAchievements")),FString(TEXT("Quest")),FString(TEXT("QuestStory")),FString(TEXT("QuestAll")),FString(TEXT("Pvp")),FString(TEXT("Bot")),FString(TEXT("Shop")),FString(TEXT("ShopNoFunds")),FString(TEXT("Replays")),FString(TEXT("Track")),FString(TEXT("TrackUnclaimed")),FString(TEXT("TrackComplete")),FString(TEXT("Archive"))})
    {
        Auth->AuthState=Page==TEXT("Archive")?EProjectProject01AuthState::SignedOut:EProjectProject01AuthState::SignedIn;
        if(Page==TEXT("TrackComplete")||Page==TEXT("TrackUnclaimed")||Page==TEXT("Archive"))
        {
            auto Completed=MakeShared<FJsonObject>();Completed->SetNumberField(TEXT("stage"),1);Completed->SetBoolField(TEXT("complete"),true);Completed->SetBoolField(TEXT("storyAvailable"),Page==TEXT("TrackComplete"));Completed->SetBoolField(TEXT("claimed"),Page==TEXT("TrackComplete"));Completed->SetStringField(TEXT("rareItem"),TEXT("investigation_seal_1"));
            Completed->SetStringField(TEXT("story"),TEXT("폐점 안내가 끝난 뒤에도 서관 쇼윈도에는 불이 켜져 있었다.\n\n출입 기록에는 마지막 근무자가 돌아오지 않았다고 적혀 있었다."));
            Investigation->SetArrayField(TEXT("stages"),{MakeShared<FJsonValueObject>(Completed)});
            for(int32 Piece=0;Piece<9;++Piece)Pieces[Piece]->AsObject()->SetBoolField(TEXT("discovered"),true);
            if(Page==TEXT("Archive"))
            {
                auto Archived=MakeShared<FJsonObject>();Archived->SetStringField(TEXT("season"),TEXT("RECORD 0"));Archived->SetArrayField(TEXT("stages"),{MakeShared<FJsonValueObject>(Completed)});
                R->StoryArchives=MakeShared<FJsonObject>();R->StoryArchives->SetArrayField(TEXT("archives"),{MakeShared<FJsonValueObject>(Archived)});Widget->bOpenedOnline=false;
            }
        }
        // Rebuild the root per case so bound header/tab visibility is evaluated for
        // that page, rather than reusing one frame's cached attributes across cases.
        Widget->ReleaseSlateResources(true);Widget=NewObject<UProjectCaseRecordWidget>(World);static_cast<UUserWidget*>(Widget)->Initialize();
        if(Page==TEXT("ProfileClaimed"))Rewards[0]->AsObject()->SetBoolField(TEXT("claimed"),true);
        R->Snapshot->SetArrayField(TEXT("loadout"),OriginalLoadout);
        if(Page.StartsWith(TEXT("Profile")))
        {
            TArray<TSharedPtr<FJsonValue>> Displayed;for(const FString Id:{FString(TEXT("banner_afterhours")),FString(TEXT("banner_embers"))}){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("itemId"),Id);Displayed.Add(MakeShared<FJsonValueObject>(J));}Collection->SetArrayField(TEXT("displayedBanners"),Displayed);
            R->Snapshot->SetArrayField(TEXT("loadout"),{MakeShared<FJsonValueObject>(Banner)});
        }
        else Collection->SetArrayField(TEXT("displayedBanners"),TArray<TSharedPtr<FJsonValue>>{});
        R->Snapshot->SetNumberField(TEXT("credits"),Page==TEXT("ShopNoFunds")?0:500);
        Collection->GetObjectField(TEXT("draw"))->SetNumberField(TEXT("tickets"),Page==TEXT("DrawNoFunds")?0:5);
        Collection->GetObjectField(TEXT("draw"))->SetBoolField(TEXT("active"),Page!=TEXT("DrawInactive"));
        Widget->bEditingProfile=Page==TEXT("ProfileEdit");Widget->BioDraft=TEXT("폐점 이후의 기록을 수집합니다.");
        R->DrawResult.Reset();
        if(Page==TEXT("DrawReset"))
        {R->DrawResult=MakeShared<FJsonObject>();R->DrawResult->SetStringField(TEXT("operation"),TEXT("EditorReset"));R->DrawResult->SetStringField(TEXT("requestId"),TEXT("reset-response"));Widget->bDrawResultOpen=Widget->bDrawRequestedHere=true;Widget->ParcelOpenTimes.Add(0,FPlatformTime::Seconds());Widget->RareRevealQueue.Add(0);}
        if(Page==TEXT("DrawResult")||Page==TEXT("DrawRare"))
        {
            R->DrawResult=MakeShared<FJsonObject>();R->DrawResult->SetStringField(TEXT("operation"),TEXT("Draw"));R->DrawResult->SetStringField(TEXT("requestId"),TEXT("render-only"));
            TArray<TSharedPtr<FJsonValue>> Awards;for(int32 I=0;I<5;I++){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),I==0?TEXT("폐점 이후"):I==1?TEXT("잔불"):TEXT("기록 포인트"));if(I<2)J->SetStringField(TEXT("itemId"),I==0?TEXT("banner_afterhours"):TEXT("banner_embers"));J->SetNumberField(TEXT("grade"),I+1);J->SetNumberField(TEXT("credits"),I<2?0:5);Awards.Add(MakeShared<FJsonValueObject>(J));}R->DrawResult->SetArrayField(TEXT("awards"),Awards);
            Widget->LastDrawReceipt=TEXT("render-only");Widget->bDrawResultOpen=true;
        }
        Widget->CollectionCategory=Page==TEXT("CollectionBanner")?TEXT("Banner"):Page==TEXT("CollectionFrame")?TEXT("Frame"):TEXT("All");
        if(Page==TEXT("QuestStory"))
        {
            Quest->SetNumberField(TEXT("stage"),1);
            QuestSteps[0]->AsObject()->SetBoolField(TEXT("completed"),true);QuestSteps[0]->AsObject()->SetBoolField(TEXT("active"),false);QuestSteps[0]->AsObject()->SetBoolField(TEXT("canSkip"),false);
            QuestSteps[0]->AsObject()->SetStringField(TEXT("story"),TEXT("닫힌 매장 사이를 지나 첫 번째 기록을 찾았다.\n\n마네킹은 시선을 돌린 사이에만 움직였다. 다음 층으로 향하기 전, 동료와 함께 주변을 살폈다."));
            QuestSteps[1]->AsObject()->SetBoolField(TEXT("active"),true);
        }
        if(Page==TEXT("QuestAll"))
        {
            Quest->SetNumberField(TEXT("stage"),7);
            for(int32 I=0;I<QuestSteps.Num();++I){auto S=QuestSteps[I]->AsObject();S->SetBoolField(TEXT("active"),I==7);S->SetBoolField(TEXT("completed"),I<7);S->SetBoolField(TEXT("canSkip"),false);}
        }
        Widget->Page=Page.StartsWith(TEXT("Quest"))?TEXT("Quest"):Page.StartsWith(TEXT("Collection"))?TEXT("Collection"):Page.StartsWith(TEXT("Draw"))?TEXT("Draw"):Page.StartsWith(TEXT("Track"))?TEXT("Track"):Page.StartsWith(TEXT("Profile"))?TEXT("Profile"):Page.StartsWith(TEXT("Shop"))?TEXT("Shop"):Page;Widget->bShowDrawRates=Page==TEXT("DrawRates");R->Status=Page==TEXT("TrackComplete")?TEXT("보상을 수령했습니다."):Page==TEXT("ShopNoFunds")?TEXT("포인트가 부족합니다"):FString();
        R->Request=FHttpModule::Get().CreateRequest();Slate=Widget->TakeWidget();R->Request.Reset();Widget->RefreshPanel();
        if(Page==TEXT("DrawReset"))TestTrue(TEXT("Confirmed reset clears previous boxes and solo reveal without showing stale results"),!Widget->bDrawResultOpen&&!Widget->bDrawRequestedHere&&Widget->ParcelOpenTimes.IsEmpty()&&Widget->RareRevealQueue.IsEmpty());
        if(Page==TEXT("DrawInactive"))
        {
            Widget->RequestDrawAction(TEXT("Draw"),1);TestTrue(TEXT("An unpublished pool explains the reason without creating boxes or HTTP"),Widget->DrawFeedback.Contains(TEXT("공개되지"))&&!R->Request.IsValid()&&!Widget->bDrawRequestedHere);
            int32 EnabledActions=0;TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> N){if(N->GetType()==FName(TEXT("SButton"))){auto* C=N->GetChildren();if(C&&C->Num()==1&&C->GetChildAt(0)->GetType()==FName(TEXT("STextBlock"))){auto Caption=StaticCastSharedRef<STextBlock>(C->GetChildAt(0))->GetText().ToString();if(Caption==TEXT("1개 뽑기")||Caption==TEXT("5개 뽑기")||Caption==TEXT("재화 1개 교환 · 500 P")||Caption==TEXT("최대 교환"))EnabledActions+=N->IsEnabled()?1:0;}}if(auto* C=N->GetChildren())for(int32 I=0;I<C->Num();I++)Inspect(C->GetChildAt(I));};Inspect(Slate);TestEqual(TEXT("Draw and exchange buttons remain clickable for feedback"),EnabledActions,4);
        }
        if(Page==TEXT("DrawNoFunds"))
        {
            auto Draw=Collection->GetObjectField(TEXT("draw"));Draw->SetNumberField(TEXT("tickets"),0);Widget->RequestDrawAction(TEXT("Draw"),1);Widget->ResolveConfirmation(true,false);Widget->RequestDrawAction(TEXT("Draw"),5);Widget->ResolveConfirmation(true,false);
            TestEqual(TEXT("Insufficient currency uses the shop-style feedback"),Widget->DrawFeedback,FString(TEXT("재화가 부족합니다")));TestFalse(TEXT("Insufficient currency sends no HTTP and creates no draw"),R->Request.IsValid()||R->DrawResult.IsValid()||Widget->bDrawResultOpen||Widget->bDrawRequestedHere);
        }
        if(Page==TEXT("Draw")||Page==TEXT("DrawNoFunds"))
        {
            int32 Parcels=0;TFunction<void(TSharedRef<SWidget>)> Count=[&](TSharedRef<SWidget> N){if(N->GetType()==FName(TEXT("SParcel")))Parcels++;if(auto* Children=N->GetChildren())for(int I=0;I<Children->Num();I++)Count(Children->GetChildAt(I));};Count(Slate);TestEqual(TEXT("No boxes exist before a successful draw"),Parcels,0);
        }
        if(Page==TEXT("DrawResult")||Page==TEXT("DrawRare"))
        {
            TestTrue(TEXT("Five results start unopened"),Widget->ParcelOpenTimes.IsEmpty());Widget->OpenDrawParcel(0);const double First=Widget->ParcelOpenTimes[0];Widget->OpenAllDrawParcels();
            TestEqual(TEXT("Open all affects every remaining parcel"),Widget->ParcelOpenTimes.Num(),5);TestEqual(TEXT("Open all keeps already opened parcel timing"),Widget->ParcelOpenTimes[0],First);
            for(int32 I=2;I<5;I++)TestEqual(TEXT("Unopened parcels begin their normal animation simultaneously"),Widget->ParcelOpenTimes[I],Widget->ParcelOpenTimes[1]);
            TestFalse(TEXT("Opening parcels never submits another draw request"),R->Request.IsValid());
            TestEqual(TEXT("Two rare prizes each receive their own solo presentation"),Widget->RareRevealQueue.Num(),2);
            if(Page==TEXT("DrawResult"))
            {
                FPointerEvent Click(0,FVector2D::ZeroVector,FVector2D::ZeroVector,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
                auto Solo=Widget->RarePresentationLayer->GetChildren()->GetChildAt(0);Solo->OnMouseButtonDown(FGeometry(),Click);
                TestEqual(TEXT("First click skips to the product without closing it"),Widget->RareRevealQueue.Num(),2);Solo->OnMouseButtonDown(FGeometry(),Click);
                TestEqual(TEXT("Immediate second click cannot skip the product reveal and acknowledgement together"),Widget->RareRevealQueue.Num(),2);
                for(int32 Remaining=1;Remaining>=0;Remaining--){Widget->RareRevealBeganAt=FPlatformTime::Seconds()-5;Widget->RenderedFingerprint.Reset();Widget->RefreshPanel();Widget->RarePresentationLayer->GetChildren()->GetChildAt(0)->OnMouseButtonDown(FGeometry(),Click);TestEqual(TEXT("After product reveal, a click advances exactly one reward"),Widget->RareRevealQueue.Num(),Remaining);}
            }
            else Widget->RareRevealBeganAt=FPlatformTime::Seconds()-5;
            for(auto& Open:Widget->ParcelOpenTimes)Open.Value-=5;Widget->RenderedFingerprint.Reset();Widget->RefreshPanel();
        }
        for(FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
        {
            auto* Target=ProjectUIRenderCapture::Create(Size);const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
            for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,Slate,Dpi,FVector2D(Size),0);
            FlushRenderingCommands();TArray<FColor> Pixels;
            if(TestTrue(TEXT("Record page renders pixels"),ProjectUIRenderCapture::Read(Target,Pixels)&&Pixels.Num()==Size.X*Size.Y))
            {TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);TestTrue(TEXT("Record page QA image saved"),FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("%s_%dx%d.png"),*Page,Size.X,Size.Y))));}
            // A scroll body's content is intentionally taller than the screen (117 records).
            // Its viewport, rather than the full content extent, must fit on screen.
            if(!TestTrue(TEXT("Each freshly constructed record page owns its scroll viewport"),Widget->RecordsScroll.IsValid()))return false;
            const auto G=Widget->RecordsScroll->GetCachedGeometry();TestTrue(TEXT("Record scroll viewport fits inside the screen"),G.GetAbsolutePosition().X>=0&&G.GetAbsolutePosition().Y>=0&&G.GetAbsolutePosition().X+G.GetAbsoluteSize().X<=Size.X+1&&G.GetAbsolutePosition().Y+G.GetAbsoluteSize().Y<=Size.Y+1);
            if(Page.StartsWith(TEXT("Track"))||Page.StartsWith(TEXT("Draw"))||Page.StartsWith(TEXT("Profile"))||Page.StartsWith(TEXT("Collection"))||Page==TEXT("Events")||Page==TEXT("Achievements")||Page==TEXT("OnlineAchievements")||Page.StartsWith(TEXT("Quest"))||Page==TEXT("Archive")||Page==TEXT("Pvp")||Page==TEXT("Bot")||Page==TEXT("Shop"))
                TestTrue(FString::Printf(TEXT("Page fits without scrolling: %s %dx%d desired %.1f available %.1f"),*Page,Size.X,Size.Y,Widget->Body->GetDesiredSize().Y,G.GetLocalSize().Y),Widget->Body->GetDesiredSize().Y<=G.GetLocalSize().Y+1);
            if(Page==TEXT("QuestStory"))
            {
                const FString Expected=Quest->GetStringField(TEXT("baseStory"))+TEXT("\n\n")+QuestSteps[0]->AsObject()->GetStringField(TEXT("story"));TSharedPtr<SWidget> Story;
                TFunction<void(TSharedRef<SWidget>)> Find=[&](TSharedRef<SWidget> W){if(W->GetType()==FName(TEXT("STextBlock"))&&StaticCastSharedRef<STextBlock>(W)->GetText().ToString()==Expected)Story=W;if(auto* C=W->GetChildren())for(int32 I=0;I<C->Num();++I)Find(C->GetChildAt(I));};Find(Slate);
                TestTrue(TEXT("Quest shows the base story plus the completed Korean chapter, preserving paragraph breaks"),Story.IsValid());
                if(Story){auto Parent=Story->GetParentWidget();while(Parent&&Parent->GetType()!=FName(TEXT("SScrollBox")))Parent=Parent->GetParentWidget();TestTrue(TEXT("Story keeps a visible reading region below objectives at both resolutions"),Parent&&Parent->GetCachedGeometry().GetLocalSize().Y>=60);}
            }
            if(Page==TEXT("TrackUnclaimed")||Page==TEXT("TrackComplete"))
            {
                bool Clue=false,Story=false;
                TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> Node){if(Node->GetType()==FName(TEXT("STextBlock"))){const auto Text=StaticCastSharedRef<STextBlock>(Node)->GetText().ToString();Clue|=Text==TEXT("폐점");Story|=Text.StartsWith(TEXT("폐점 안내가 끝난 뒤에도"));}if(auto* Children=Node->GetChildren())for(int32 I=0;I<Children->Num();++I)Inspect(Children->GetChildAt(I));};Inspect(Slate);
                TestTrue(TEXT("Collected pieces show word clues independently of story claim"),Clue);
                TestEqual(TEXT("Full story appears only when the server marks it available"),Story,Page==TEXT("TrackComplete"));
            }
            if(Page.StartsWith(TEXT("Profile"))&&Page!=TEXT("ProfileEdit"))
            {
                int32 RewardRows=0;FString RewardText;TFunction<void(TSharedRef<SWidget>)> CountRewards=[&](TSharedRef<SWidget> Node)
                {
                    if(Node->GetType()==FName(TEXT("STextBlock"))){const FString Text=StaticCastSharedRef<STextBlock>(Node)->GetText().ToString();if(Text.StartsWith(TEXT("레벨 "))&&Text.Contains(TEXT(" · "))){++RewardRows;RewardText=Text;}}
                    if(auto* Children=Node->GetChildren())for(int32 I=0;I<Children->Num();++I)CountRewards(Children->GetChildAt(I));
                };CountRewards(Slate);TestEqual(TEXT("Only the next unclaimed level reward is displayed"),RewardRows,1);
                TestTrue(TEXT("Claiming a milestone advances the displayed reward"),RewardText.StartsWith(Page==TEXT("Profile")?TEXT("레벨 5 ·"):TEXT("레벨 10 ·")));
            }
            if(Page==TEXT("ShopNoFunds"))
            {
                int32 Purchases=0;TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> W){if(W->GetType()==FName(TEXT("SButton"))){auto B=StaticCastSharedRef<SButton>(W);if(B->GetContent()->GetType()==FName(TEXT("STextBlock"))&&StaticCastSharedRef<STextBlock>(B->GetContent())->GetText().ToString()==TEXT("구매")){++Purchases;TestTrue(TEXT("Purchase remains clickable with zero points; server still validates spending"),B->IsEnabled());}}if(auto* C=W->GetChildren())for(int32 I=0;I<C->Num();++I)Inspect(C->GetChildAt(I));};Inspect(Slate);TestEqual(TEXT("All five unowned products offer purchase feedback with zero points"),Purchases,5);
            }
            if(Page==TEXT("Achievements"))
            {
                TSet<FString> Captions;TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> W){if(W->GetType()==FName(TEXT("STextBlock"))){const auto Text=StaticCastSharedRef<STextBlock>(W);Captions.Add(Text->GetText().ToString());if(Text->GetText().ToString().StartsWith(TEXT("전체 달성률")))TestTrue(TEXT("Global achievement percentage stays on one line"),Text->GetDesiredSize().Y<=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text->GetText(),Text->GetFont()).Y*1.3f+2);}if(auto* C=W->GetChildren())for(int32 I=0;I<C->Num();++I)Inspect(C->GetChildAt(I));};Inspect(Slate);
                TestTrue(TEXT("Personal overall and area completion counts are visible"),Captions.Contains(TEXT("전체 2/3"))&&Captions.Contains(TEXT("체크포인트 1/1"))&&Captions.Contains(TEXT("탈출 1/1"))&&Captions.Contains(TEXT("완주 0/1")));
                TestTrue(TEXT("Per-achievement global percentage appears independently of personal state"),Captions.Contains(TEXT("전체 달성률 75.0%"))&&Captions.Contains(TEXT("전체 달성률 25.0%")));
            }
            if(Page.StartsWith(TEXT("Track"))||Page==TEXT("Archive"))
            {
                TArray<FGeometry> Cells;TSharedPtr<SWidget> Progress;
                TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> W){if(W->GetType()==FName(TEXT("SCasePuzzleCell")))Cells.Add(W->GetCachedGeometry());if(W->GetType()==FName(TEXT("STextBlock"))&&StaticCastSharedRef<STextBlock>(W)->GetText().ToString().StartsWith(TEXT("파트 완성 "))&&StaticCastSharedRef<STextBlock>(W)->GetText().ToString().Contains(TEXT("/7")))Progress=W;if(auto* C=W->GetChildren())for(int32 I=0;I<C->Num();++I)Inspect(C->GetChildAt(I));};Inspect(Slate);
                if(TestEqual(TEXT("The puzzle keeps all nine individually collectible pieces"),Cells.Num(),9))
                {
                    FVector2D Min(1e9,1e9),Max(-1e9,-1e9);TSet<FIntPoint> Shapes;
                    for(const auto& Cell:Cells){const auto P=Cell.GetAbsolutePosition(),S=Cell.GetAbsoluteSize();Min.X=FMath::Min(Min.X,P.X);Min.Y=FMath::Min(Min.Y,P.Y);Max.X=FMath::Max(Max.X,P.X+S.X);Max.Y=FMath::Max(Max.Y,P.Y+S.Y);Shapes.Add(FIntPoint(FMath::RoundToInt(S.X),FMath::RoundToInt(S.Y)));}
                    TestTrue(TEXT("The filled puzzle rectangle is moderately portrait and pieces vary in both dimensions"),(Max.Y-Min.Y)>(Max.X-Min.X)&&(Max.Y-Min.Y)<(Max.X-Min.X)*1.5&&Shapes.Num()>=5);
                    TestTrue(TEXT("Part completion is above the puzzle rather than inside the story"),Progress.IsValid()&&Progress->GetCachedGeometry().GetAbsolutePosition().Y<Min.Y);
                }
            }
            if(Page==TEXT("DrawResult")&&Size.X==1280)
            {
                const auto SavedTimes=Widget->ParcelOpenTimes;
                const double OpeningAge[]{2.54,1.39,.45,.45,.45};
                for(auto& Open:Widget->ParcelOpenTimes)Open.Value=FPlatformTime::Seconds()-OpeningAge[FMath::Clamp(Open.Key,0,4)];
                Widget->RenderedFingerprint.Reset();Widget->RefreshPanel();
                for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,Slate,Dpi,FVector2D(Size),0);
                FlushRenderingCommands();TArray<FColor> OpeningPixels;
                if(TestTrue(TEXT("Opening-seam halo renders without lighting the entire parcel"),ProjectUIRenderCapture::Read(Target,OpeningPixels)))
                {
                    TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,OpeningPixels,PNG);
                    FFileHelper::SaveArrayToFile(PNG,*(Directory/TEXT("DrawOpeningHalo_1280x720.png")));
                }
                Widget->ParcelOpenTimes=SavedTimes;Widget->RenderedFingerprint.Reset();Widget->RefreshPanel();
            }
            Target->ReleaseResource();
        }
    }
    TestFalse(TEXT("Rendering never sends real HTTP"),R->Request.IsValid());return true;
}
#endif

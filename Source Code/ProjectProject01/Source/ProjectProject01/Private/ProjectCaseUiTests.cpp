#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ProjectCaseRecords.h"
#include "ProjectProject01AuthSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

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
    TArray<TSharedPtr<FJsonValue>> FullMissions;
    for(const FString Mode:{FString(TEXT("Pvp")),FString(TEXT("Bot"))})for(int32 I=0;I<4;++I)
    {
        auto M=MakeShared<FJsonObject>();M->Values=R->Snapshot->GetArrayField(TEXT("missions"))[I%2]->AsObject()->Values;
        M->SetStringField(TEXT("mode"),Mode);M->SetStringField(TEXT("id"),FString::FromInt(I));M->SetBoolField(TEXT("weekly"),I>=2);
        FullMissions.Add(MakeShared<FJsonValueObject>(M));
    }
    R->Snapshot->SetArrayField(TEXT("missions"),FullMissions);
    TArray<TSharedPtr<FJsonValue>> Catalogue;
    const TArray<FString> ItemIds{TEXT("frame_crimson"),TEXT("frame_silver"),TEXT("icon_file"),TEXT("icon_seal"),TEXT("title_witness")};
    const TArray<FString> ItemNames{TEXT("붉은 봉인 테두리"),TEXT("조사관 테두리"),TEXT("사건 기록 아이콘"),TEXT("증거 봉인 아이콘"),TEXT("목격자 명찰")};
    for(int32 I=0;I<ItemIds.Num();++I){auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("id"),ItemIds[I]);Item->SetStringField(TEXT("name"),ItemNames[I]);Item->SetNumberField(TEXT("price"),100);Catalogue.Add(MakeShared<FJsonValueObject>(Item));}
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
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,false);const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
        for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,DockSlate,Dpi,FVector2D(Size),0);
        FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);
        if(TestTrue(TEXT("Persistent account/party dock renders"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)))
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
    for(const FString Page:{FString(TEXT("Profile")),FString(TEXT("ProfileClaimed")),FString(TEXT("Achievements")),FString(TEXT("Pvp")),FString(TEXT("Bot")),FString(TEXT("Shop")),FString(TEXT("Replays")),FString(TEXT("Track")),FString(TEXT("TrackUnclaimed")),FString(TEXT("TrackComplete")),FString(TEXT("Archive")),FString(TEXT("Party"))})
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
        Widget->Page=Page.StartsWith(TEXT("Track"))?TEXT("Track"):Page.StartsWith(TEXT("Profile"))?TEXT("Profile"):Page;R->Status=Page==TEXT("TrackComplete")?TEXT("보상을 수령했습니다."):FString();
        R->Request=FHttpModule::Get().CreateRequest();Slate=Widget->TakeWidget();R->Request.Reset();Widget->RefreshPanel();
        for(FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
        {
            auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,false);const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
            for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,Slate,Dpi,FVector2D(Size),0);
            FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);
            if(TestTrue(TEXT("Record page renders pixels"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)&&Pixels.Num()==Size.X*Size.Y))
            {TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);TestTrue(TEXT("Record page QA image saved"),FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("%s_%dx%d.png"),*Page,Size.X,Size.Y))));}
            // A scroll body's content is intentionally taller than the screen (117 records).
            // Its viewport, rather than the full content extent, must fit on screen.
            if(!TestTrue(TEXT("Each freshly constructed record page owns its scroll viewport"),Widget->RecordsScroll.IsValid()))return false;
            const auto G=Widget->RecordsScroll->GetCachedGeometry();TestTrue(TEXT("Record scroll viewport fits inside the screen"),G.GetAbsolutePosition().X>=0&&G.GetAbsolutePosition().Y>=0&&G.GetAbsolutePosition().X+G.GetAbsoluteSize().X<=Size.X+1&&G.GetAbsolutePosition().Y+G.GetAbsoluteSize().Y<=Size.Y+1);
            if(Page.StartsWith(TEXT("Track"))||Page==TEXT("Archive")||Page==TEXT("Pvp")||Page==TEXT("Bot")||Page==TEXT("Shop"))
                TestTrue(FString::Printf(TEXT("Page fits without scrolling: %s %dx%d desired %.1f available %.1f"),*Page,Size.X,Size.Y,Widget->Body->GetDesiredSize().Y,G.GetLocalSize().Y),Widget->Body->GetDesiredSize().Y<=G.GetLocalSize().Y+1);
            if(Page==TEXT("TrackUnclaimed")||Page==TEXT("TrackComplete"))
            {
                bool Clue=false,Story=false;
                TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> Node){if(Node->GetType()==FName(TEXT("STextBlock"))){const auto Text=StaticCastSharedRef<STextBlock>(Node)->GetText().ToString();Clue|=Text==TEXT("폐점");Story|=Text.StartsWith(TEXT("폐점 안내가 끝난 뒤에도"));}if(auto* Children=Node->GetChildren())for(int32 I=0;I<Children->Num();++I)Inspect(Children->GetChildAt(I));};Inspect(Slate);
                TestTrue(TEXT("Collected pieces show word clues independently of story claim"),Clue);
                TestEqual(TEXT("Full story appears only when the server marks it available"),Story,Page==TEXT("TrackComplete"));
            }
            if(Page.StartsWith(TEXT("Profile")))
            {
                int32 RewardRows=0;FString RewardText;TFunction<void(TSharedRef<SWidget>)> CountRewards=[&](TSharedRef<SWidget> Node)
                {
                    if(Node->GetType()==FName(TEXT("STextBlock"))){const FString Text=StaticCastSharedRef<STextBlock>(Node)->GetText().ToString();if(Text.StartsWith(TEXT("레벨 "))&&Text.Contains(TEXT(" · "))){++RewardRows;RewardText=Text;}}
                    if(auto* Children=Node->GetChildren())for(int32 I=0;I<Children->Num();++I)CountRewards(Children->GetChildAt(I));
                };CountRewards(Slate);TestEqual(TEXT("Only the next unclaimed level reward is displayed"),RewardRows,1);
                TestTrue(TEXT("Claiming a milestone advances the displayed reward"),RewardText.StartsWith(Page==TEXT("ProfileClaimed")?TEXT("레벨 10 ·"):TEXT("레벨 5 ·")));
            }
            Target->ReleaseResource();
        }
    }
    TestFalse(TEXT("Rendering never sends real HTTP"),R->Request.IsValid());return true;
}
#endif

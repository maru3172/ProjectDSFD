#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01SocialSubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "ProjectProject01SocialWidget.h"
#include "ProjectCaseRecords.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ProjectDFSDUITheme.h"
#include "Slate/WidgetRenderer.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HttpModule.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSocialSessionCleanupTest,"ProjectProject01.Community.SessionCleanup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectSocialSessionCleanupTest::RunTest(const FString&)
{
    auto* Instance=NewObject<UGameInstance>();
    auto* Auth=NewObject<UProjectProject01AuthSubsystem>(Instance);
    auto* Social=NewObject<UProjectProject01SocialSubsystem>(Instance);
    Auth->OnLocalSessionCleared.AddUObject(Social,&UProjectProject01SocialSubsystem::Clear);
    Social->Snapshot=MakeShared<FJsonObject>();
    Social->Snapshot->SetStringField(TEXT("private-block-list"),TEXT("old-account"));
    Social->Status=TEXT("old account message");
    Social->RoomContext=MakeShared<FJsonObject>();Social->RoomContextUserId=TEXT("123");
    Social->SearchResults=MakeShared<FJsonObject>();Social->Profile=MakeShared<FJsonObject>();Social->Conversation=MakeShared<FJsonObject>();Social->ConversationAccount=TEXT("old friend");
    int32 Changes=0;Social->OnChanged.AddLambda([&Changes]{++Changes;});
    Auth->ForceClearLocalSession();
    TestFalse(TEXT("Logout immediately clears cached blocks, friends, messages and notifications"),Social->Snapshot.IsValid());
    TestTrue(TEXT("Old account status is removed"),Social->Status.IsEmpty());
    TestFalse(TEXT("Search and profile cache removed"),Social->SearchResults.IsValid()||Social->Profile.IsValid());
    TestFalse(TEXT("Room context identity removed on logout"),Social->RoomContext.IsValid()||!Social->RoomContextUserId.IsEmpty());
    TestFalse(TEXT("Conversation and account removed"),Social->Conversation.IsValid()||!Social->ConversationAccount.IsEmpty());
    TestEqual(TEXT("Logout immediately notifies open widgets to hide"),Changes,1);
    TestFalse(TEXT("Logout clears lobby receive permission"),Social->IsLobbyVisible());
    Social->OnChanged.Clear();Auth->OnLocalSessionCleared.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSocialWidgetLayoutTest,"ProjectProject01.Community.WidgetLayout",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectSocialWidgetLayoutTest::RunTest(const FString&)
{
    for(bool Friend:{false,true})for(bool Blocked:{false,true})
    {
        const auto Self=UProjectProject01SocialWidget::RoomMenuActions(true,Friend,Blocked);
        TestEqual(TEXT("Self right-click always has exactly one item"),Self.Num(),1);
        TestEqual(TEXT("Self right-click only allows profile"),Self[0].Value,FString(TEXT("Profile")));
        const auto Other=UProjectProject01SocialWidget::RoomMenuActions(false,Friend,Blocked);
        TestEqual(TEXT("Other participant also permits party invitation"),Other.Num(),6);
        TestEqual(TEXT("Party invitation is available from the participant context menu"),Other[5].Value,FString(TEXT("PartyInvite")));
        TestEqual(TEXT("Friendship action follows current relationship"),Other[1].Value,FString(Friend?TEXT("Remove"):TEXT("Request")));
        TestEqual(TEXT("Block action follows own block state"),Other[3].Value,FString(Blocked?TEXT("Unblock"):TEXT("Block")));
        TestEqual(TEXT("Other participant report action present"),Other[4].Value,FString(TEXT("Report")));
    }
    // No live credentials, account files, or real HTTP requests are used by this fixture.
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto* Instance=NewObject<UGameInstance>();World->SetGameInstance(Instance);Instance->Init();
    auto* Auth=Instance->GetSubsystem<UProjectProject01AuthSubsystem>();
    auto* Social=Instance->GetSubsystem<UProjectProject01SocialSubsystem>();
    auto* Widget=NewObject<UProjectProject01SocialWidget>(World);Widget->Initialize();auto Slate=Widget->TakeWidget();
    UProjectCaseRecordDock* Dock=nullptr;
    ON_SCOPE_EXIT{Auth->ForceClearLocalSession();if(Dock)Dock->ReleaseSlateResources(true);Widget->ReleaseSlateResources(true);Instance->Shutdown();World->SetGameInstance(nullptr);World->DestroyWorld(false);};
    Auth->AuthState=EProjectProject01AuthState::SignedIn;Auth->AccessToken=TEXT("non-network-layout-fixture");
    Social->ReadRequest=FHttpModule::Get().CreateRequest(); // Reserve read slot, without ProcessRequest.
    Social->bLobbyVisible=true;Social->Snapshot=MakeShared<FJsonObject>();
    auto Person=MakeShared<FJsonObject>();
    Person->SetStringField(TEXT("accountId"),TEXT("friend_account"));Person->SetStringField(TEXT("displayName"),TEXT("함께하는 친구"));
    Person->SetStringField(TEXT("statusMessage"),TEXT("오늘 함께 탈출해요!"));Person->SetStringField(TEXT("presence"),TEXT("Online"));Person->SetStringField(TEXT("activity"),TEXT("로비"));
    Person->SetStringField(TEXT("accent"),TEXT("Ocean"));Person->SetStringField(TEXT("title"),TEXT("Explorer"));
    Social->Snapshot->SetArrayField(TEXT("friends"),{MakeShared<FJsonValueObject>(Person)});
    Social->Snapshot->SetArrayField(TEXT("statusMessages"),{MakeShared<FJsonValueObject>(Person)});
    Social->Snapshot->SetObjectField(TEXT("me"),Person);
    Auth->AuthState=EProjectProject01AuthState::SignedOut;
    Dock=NewObject<UProjectCaseRecordDock>(World);Dock->Initialize();auto DockSlate=Dock->TakeWidget();Dock->SetSocialOverlay(Widget);
    Auth->AuthState=EProjectProject01AuthState::SignedIn;Auth->SignedInDisplayName=TEXT("내 닉네임");
    auto* Records=Instance->GetSubsystem<UProjectCaseRecords>();Records->Snapshot=MakeShared<FJsonObject>();Records->Snapshot->SetStringField(TEXT("userId"),TEXT("1"));Records->Snapshot->SetNumberField(TEXT("level"),7);Records->Snapshot->SetNumberField(TEXT("credits"),500);Records->Snapshot->SetNumberField(TEXT("levelProgress"),.4);
    auto Party=MakeShared<FJsonObject>();Party->SetBoolField(TEXT("isLeader"),true);Party->SetNumberField(TEXT("maximum"),3);TArray<TSharedPtr<FJsonValue>> PartyMembers;
    for(int32 I=0;I<3;++I){auto M=MakeShared<FJsonObject>();M->SetStringField(TEXT("userId"),FString::FromInt(I+1));M->SetStringField(TEXT("accountId"),I==0?TEXT("self_account"):TEXT("friend_account"));M->SetStringField(TEXT("displayName"),I==0?TEXT("내 닉네임"):FString::Printf(TEXT("파티원 %d"),I));M->SetNumberField(TEXT("level"),I+2);PartyMembers.Add(MakeShared<FJsonValueObject>(M));}
    Party->SetArrayField(TEXT("members"),PartyMembers);auto PartyState=MakeShared<FJsonObject>();PartyState->SetObjectField(TEXT("party"),Party);Records->Snapshot->SetObjectField(TEXT("partyState"),PartyState);
    Slate=SNew(SOverlay)+SOverlay::Slot()[ProjectDFSDUI::FrameSlate(SNullWidget::NullWidget,1120,128)]+SOverlay::Slot()[DockSlate]+SOverlay::Slot()[Slate];
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("FeatureChecks/SocialUI");IFileManager::Get().MakeDirectory(*Directory,true);
    auto Render=[&](const FString& Name)
    {
        FWidgetRenderer Renderer(false);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1280,720),TF_Bilinear,false);
        for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,Slate,FVector2D(1280,720),0.016f);FlushRenderingCommands();TArray<FColor> Pixels;
        FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);TestTrue(TEXT("Social panel renders"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));TArray64<uint8> Image;FImageUtils::PNGCompressImageArray(1280,720,Pixels,Image);
        TestTrue(TEXT("UI verification artifact saved"),FFileHelper::SaveArrayToFile(Image,*(Directory/(Name+TEXT(".png")))));
    };
    Widget->OpenPartyInvite();Render(TEXT("PartyInviteFull"));
    TestEqual(TEXT("Party invite opens inside the existing social overlay"),Widget->Page,FString(TEXT("PartyInvite")));
    TestTrue(TEXT("Invite popup owns a visible account input"),Widget->TargetInput.IsValid()&&Widget->Panel->GetChildren()->Num()>0);
    Widget->TargetInput->SetText(FText::FromString(TEXT("friend_account")));
    TestEqual(TEXT("Invite input keeps its draft across refreshes"),Widget->PartyInviteDraft,FString(TEXT("friend_account")));
    TestTrue(TEXT("Opening invite never submits a real party request"),!Records->IsBusy());
    PartyMembers.RemoveAt(2);Party->SetArrayField(TEXT("members"),PartyMembers);
    auto Received=MakeShared<FJsonObject>();Received->SetStringField(TEXT("id"),TEXT("offline-invite"));Received->SetStringField(TEXT("leaderName"),TEXT("다른 조사관"));
    PartyState->SetArrayField(TEXT("invites"),{MakeShared<FJsonValueObject>(Received)});Widget->RefreshView();Render(TEXT("PartyInvite"));
    TestTrue(TEXT("Received party invitation has a name and acceptance controls in the overlay"),Widget->List->GetChildren()->Num()>=2);
    Widget->Open(TEXT("Friends"));Render(TEXT("Friends"));
    TestTrue(TEXT("Switching to friends replaces the party input without replacing the screen"),Widget->Page==TEXT("Friends")&&Widget->ModalKind.IsEmpty());
    const auto& G=Widget->Panel->GetCachedGeometry();
    TestTrue(TEXT("Friends panel is on right-hand side"),G.GetAbsolutePosition().X>640);
    TestTrue(TEXT("Friends panel has readable width"),G.GetLocalSize().X>=400);
    TestTrue(TEXT("Panel bottom remains in viewport"),G.GetAbsolutePosition().Y+G.GetLocalSize().Y<=720);
    Widget->ShowModal(TEXT("Add"));Render(TEXT("AddFriend"));
    TestFalse(TEXT("Extra friend input popup disables underlying list"),Widget->Panel->IsEnabled());
    Widget->CloseModal();TestTrue(TEXT("Closing popup restores list"),Widget->Panel->IsEnabled());
    Widget->ShowModal(TEXT("Edit"));Render(TEXT("ProfileEdit"));Widget->CloseModal();
    Widget->Page=TEXT("Thread");Widget->ThreadAccount=TEXT("friend_account");Social->Conversation=MakeShared<FJsonObject>();
    auto Message=MakeShared<FJsonObject>();Message->SetStringField(TEXT("id"),TEXT("1"));Message->SetStringField(TEXT("body"),TEXT("이번에는 같이 탈출하자!"));Message->SetStringField(TEXT("direction"),TEXT("Incoming"));Message->SetStringField(TEXT("createdAtUtc"),TEXT("2026-10-18 14:00"));
    Social->Conversation->SetArrayField(TEXT("messages"),{MakeShared<FJsonValueObject>(Message)});
    Widget->BuildPanel();Render(TEXT("WhisperThread"));TestEqual(TEXT("Read acknowledgement is deferred, not blocking typing"),Social->PendingReadId,FString(TEXT("1")));
    auto Invite=MakeShared<FJsonObject>();Invite->SetStringField(TEXT("inviteId"),TEXT("fixture-invite"));Invite->SetStringField(TEXT("displayName"),TEXT("친구"));Invite->SetStringField(TEXT("roomName"),TEXT("우리 방"));
    Social->Snapshot->SetArrayField(TEXT("invites"),{MakeShared<FJsonValueObject>(Invite)});Widget->RefreshView();Render(TEXT("RoomInvite"));
    TestEqual(TEXT("New invite automatically displays an accept/decline popup"),Widget->ModalKind,FString(TEXT("Invite")));
    Social->Snapshot->SetArrayField(TEXT("invites"),{});Widget->RefreshView();TestTrue(TEXT("Expired or consumed invitation closes safely"),Widget->ModalKind.IsEmpty());
    auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("accountId"),TEXT("new_friend"));Request->SetStringField(TEXT("displayName"),TEXT("친구 요청자"));Request->SetStringField(TEXT("createdAtUtc"),TEXT("2026-10-18T12:00:00"));
    Social->Snapshot->SetArrayField(TEXT("requests"),{MakeShared<FJsonValueObject>(Request)});Widget->RefreshView();Render(TEXT("FriendRequest"));
    TestEqual(TEXT("Incoming friend request displays accept/decline confirmation"),Widget->ModalKind,FString(TEXT("FriendRequest")));
    Widget->CloseModal();Widget->RefreshView();TestTrue(TEXT("Postponing request does not reopen popup every poll"),Widget->ModalKind.IsEmpty());
    Request->SetStringField(TEXT("createdAtUtc"),TEXT("2026-10-18T12:01:00"));Widget->RefreshView();TestEqual(TEXT("New request from same account prompts again"),Widget->ModalKind,FString(TEXT("FriendRequest")));
    Social->Snapshot->SetArrayField(TEXT("requests"),{});Widget->RefreshView();TestTrue(TEXT("Resolved friend request closes popup"),Widget->ModalKind.IsEmpty());
    Social->Profile=MakeShared<FJsonObject>();Social->Profile->SetObjectField(TEXT("profile"),Person);
    Social->Profile->SetArrayField(TEXT("statistics"),{});Social->Profile->SetStringField(TEXT("statisticsNote"),TEXT("서버 검증 전적 기준입니다."));
    Widget->bRoomMemberProfile=true;Widget->Page=TEXT("Profile");Widget->BuildPanel();Render(TEXT("RoomMemberProfile"));
    Widget->CloseRoomMemberProfile();TestTrue(TEXT("Closing room member profile only closes overlay"),Widget->Page.IsEmpty()&&!Widget->bRoomMemberProfile);
    Widget->Page=TEXT("RoomActions");Widget->BuildPanel();Widget->ShowModal(TEXT("Report"),TEXT("123"),TEXT("신고 대상 [friend_account]"));Render(TEXT("PlayerReport"));
    TestTrue(TEXT("Report requires explicitly selected reason"),Widget->ReportReason.IsEmpty());
    TestEqual(TEXT("Report target is stable participant id"),Widget->ModalTarget,FString(TEXT("123")));
    Social->SubmitRoomReport(TEXT("123"),FString(),TEXT("상세 내용"));
    TestFalse(TEXT("Missing reason cannot start HTTP report"),Social->WriteRequest.IsValid());
    TestTrue(TEXT("Invalid report gives visible feedback"),Social->Status.Contains(TEXT("신고 사유")));
    TestFalse(TEXT("Report popup disables underlying social panel"),Widget->Panel->IsEnabled());
    Widget->CloseModal();Widget->ShowModal(TEXT("AccountReport"));Render(TEXT("PlayerReportByAccount"));
    TestEqual(TEXT("ID-only report is a separate popup without right click"),Widget->ModalKind,FString(TEXT("AccountReport")));
    Social->SubmitAccountReport(FString(),TEXT("Abuse"),TEXT("상세 사유"));
    TestFalse(TEXT("Empty report account cannot submit HTTP"),Social->WriteRequest.IsValid());
    TestTrue(TEXT("ID-only report explains required account"),Social->Status.Contains(TEXT("계정 아이디")));
    Widget->Open(TEXT("Friends"));Render(TEXT("ReportToFriends"));
    TestTrue(TEXT("Friends tab closes report popup and enables its list"),Widget->ModalKind.IsEmpty()&&Widget->Panel->IsEnabled()&&Widget->Page==TEXT("Friends"));
    TestTrue(TEXT("Report-only error does not remain on friends page"),Social->Status.IsEmpty());
    Widget->ShowModal(TEXT("AccountReport"));Widget->Open(TEXT("Whispers"));Render(TEXT("ReportToWhispers"));
    TestTrue(TEXT("Whispers tab closes report popup instead of leaving it on top"),Widget->ModalKind.IsEmpty()&&Widget->Panel->IsEnabled()&&Widget->Page==TEXT("Whispers"));
    TestFalse(TEXT("Switching away does not submit a report"),Social->WriteRequest.IsValid());
    Auth->ForceClearLocalSession();TestTrue(TEXT("Logout closes all social contents"),Widget->Page.IsEmpty()&&Widget->ModalKind.IsEmpty());
    return true;
}
#endif

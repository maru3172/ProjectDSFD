#include "ProjectProject01LoginWidget.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01LobbySubsystem.h"
#include "Dom/JsonObject.h"
#include "ProjectProject01VoiceSettingsWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectRoomPreparationStateTest,"ProjectProject01.Operations.RoomPreparationState",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectRoomPreparationStateTest::RunTest(const FString&)
{
	auto* Instance=NewObject<UGameInstance>();
	auto* Lobby=NewObject<UProjectProject01LobbySubsystem>(Instance);
	TSharedRef<FJsonObject> Room=MakeShared<FJsonObject>();
	Room->SetStringField(TEXT("roomId"),TEXT("test-room"));Room->SetStringField(TEXT("joinCode"),TEXT("ABCDEFGH"));Room->SetStringField(TEXT("name"),TEXT("세션 대기"));
	for(const TCHAR* Key:{TEXT("isHost"),TEXT("isReady"),TEXT("canStart"),TEXT("starting"),TEXT("started")})Room->SetBoolField(Key,false);
	Room->SetBoolField(TEXT("gameServerOnline"),true);Room->SetBoolField(TEXT("serverPreparing"),true);
	Room->SetArrayField(TEXT("members"),{});
	TSharedRef<FJsonObject> Response=MakeShared<FJsonObject>();Response->SetObjectField(TEXT("room"),Room);
	FString Error;
	if(!TestTrue(TEXT("Preparation JSON parses"),Lobby->ParseRoomState(Response,Error)))return false;
	TestTrue(TEXT("Preparation is distinguished from countdown"),Lobby->GetCurrentRoom().bServerPreparing&&!Lobby->GetCurrentRoom().bStarting);
	TestFalse(TEXT("Preparation never implies a started game"),Lobby->GetCurrentRoom().bStarted);
	TestFalse(TEXT("Preparation does not issue a game ticket"),Lobby->HasPendingGameJoinTicket());
	Room->SetBoolField(TEXT("serverPreparing"),false);Room->SetBoolField(TEXT("starting"),true);
	TestTrue(TEXT("Initial countdown parses"),Lobby->ParseRoomState(Response,Error));
	TestTrue(TEXT("Countdown is not session search"),Lobby->GetCurrentRoom().bStarting&&!Lobby->GetCurrentRoom().bServerPreparing);
	Room->RemoveField(TEXT("serverPreparing"));
	TestTrue(TEXT("Legacy room JSON remains supported"),Lobby->ParseRoomState(Response,Error));
	TestFalse(TEXT("Absent preparation flag defaults to false"),Lobby->GetCurrentRoom().bServerPreparing);
	Room->SetBoolField(TEXT("started"),true);
	Room->SetStringField(TEXT("matchId"),TEXT("match-a"));
	Room->SetStringField(TEXT("assignedRole"),TEXT("Survivor"));
	Room->SetStringField(TEXT("travelUrl"),TEXT("127.0.0.1:7777"));
	TestTrue(TEXT("Room exposes current authoritative match identity"),Lobby->ParseRoomState(Response,Error));
	Lobby->GameJoinTicket=TEXT("fixture-ticket");Lobby->GameEncryptionKeyBase64=TEXT("fixture-key");
	Lobby->GameTicketRoomId=TEXT("test-room");Lobby->GameTicketMatchId=TEXT("match-a");
	Lobby->GameTicketRole=TEXT("Survivor");Lobby->GameTicketTravelUrl=TEXT("127.0.0.1:7777");
	TestTrue(TEXT("Only the current room/match/role/server ticket is usable"),Lobby->HasGameJoinTicketForCurrentRoom());
	Lobby->GameTicketMatchId=TEXT("match-old");
	TestFalse(TEXT("A ticket from the previous match in the same room is rejected"),Lobby->HasGameJoinTicketForCurrentRoom());
	Lobby->GameTicketMatchId=TEXT("match-a");Lobby->GameTicketRole=TEXT("Mannequin");
	TestFalse(TEXT("A ticket for another role is rejected"),Lobby->HasGameJoinTicketForCurrentRoom());
	Lobby->GameTicketRole=TEXT("Survivor");Lobby->GameTicketTravelUrl=TEXT("127.0.0.1:7778");
	TestFalse(TEXT("A ticket for another server is rejected"),Lobby->HasGameJoinTicketForCurrentRoom());
	Lobby->GameTicketTravelUrl=TEXT("127.0.0.1:7777");Room->SetStringField(TEXT("matchId"),TEXT("match-b"));
	TestTrue(TEXT("A new match in the same room parses"),Lobby->ParseRoomState(Response,Error));
	TestFalse(TEXT("Changing MatchId clears the cached ticket and key"),Lobby->HasPendingGameJoinTicket());
	auto Ticket=MakeShared<FJsonObject>();
	Ticket->SetStringField(TEXT("ticket"),TEXT("old-ticket"));Ticket->SetStringField(TEXT("encryptionKey"),TEXT("old-key"));
	Ticket->SetStringField(TEXT("travelUrl"),TEXT("127.0.0.1:7777"));Ticket->SetStringField(TEXT("roomId"),TEXT("test-room"));
	Ticket->SetStringField(TEXT("matchId"),TEXT("match-a"));Ticket->SetStringField(TEXT("role"),TEXT("Survivor"));
	TestFalse(TEXT("Late previous-match response is rejected before configuring encryption"),Lobby->ParseGameJoinTicket(Ticket,Error));
	TestTrue(TEXT("Late response reports stale admission rather than a crypto error"),Error.Contains(TEXT("일치하지")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLobbySessionPresentationTest,"ProjectProject01.Operations.LobbySessionPresentation",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectLobbySessionPresentationTest::RunTest(const FString&)
{
	UUserWidget* Base=NewObject<UProjectProject01LobbyWidget>();
	if(!TestTrue(TEXT("Lobby initializes"),Base->Initialize()))return false;
	auto* Lobby=CastChecked<UProjectProject01LobbyWidget>(Base);
	Lobby->BeginGameStartCountdown();
	TestEqual(TEXT("Countdown starts at five"),Lobby->GameStartCountdownNumber,5);
	TestFalse(TEXT("Not starting game before a ready session"),Lobby->StartCountdownText->GetText().ToString().Contains(TEXT("게임을 시작합니다")));
	TestFalse(TEXT("No fade during countdown"),Lobby->bGameStartFadeActive);
	Lobby->AdvanceGameStartCountdown();
	TestEqual(TEXT("Only one countdown number shown"),Lobby->StartCountdownText->GetText().ToString(),FString(TEXT("게임 시작 준비\n4...")));
	Lobby->CancelGameStartCountdown();Lobby->ShowGameSessionSearch();
	TestEqual(TEXT("Search message"),Lobby->StartCountdownText->GetText().ToString(),FString(TEXT("세션 찾는 중…")));
	TestFalse(TEXT("Search keeps room screen visible"),Lobby->bGameStartFadeActive);
	TestTrue(TEXT("Search does not obscure cancel controls"),Lobby->StartCountdownPanel->GetVisibility()==ESlateVisibility::HitTestInvisible);
	FProjectProject01RoomState Room;Lobby->bGameStartCountdownComplete=true;
	Lobby->BeginGameStartFade(Room);TestFalse(TEXT("Cannot fade before server allows start"),Lobby->bGameStartFadeActive);
	Room.bStarted=true;Room.AssignedRole=TEXT("Mannequin");Lobby->BeginGameStartFade(Room);
	TestTrue(TEXT("Ready session begins cinematic fade"),Lobby->bGameStartFadeActive);
	TestEqual(TEXT("Start and role shown only at admission"),Lobby->StartCountdownText->GetText().ToString(),FString(TEXT("게임을 시작합니다\n역할: 마네킹")));
	Lobby->CancelGameStartCountdown();
	TestTrue(TEXT("Cancel clears every start/search message"),Lobby->StartCountdownText->GetText().IsEmpty());
	TestTrue(TEXT("Cancel hides overlay"),Lobby->StartCountdownPanel->GetVisibility()==ESlateVisibility::Collapsed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSurveyAnswerContrastTest,"ProjectProject01.Operations.SurveyAnswerContrast",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectSurveyAnswerContrastTest::RunTest(const FString&)
{
	UUserWidget* Base=NewObject<UProjectProject01SurveyWidget>();
	if(!TestTrue(TEXT("Survey initializes"),Base->Initialize()))return false;
	auto* Survey=CastChecked<UProjectProject01SurveyWidget>(Base);
	FProjectProject01SurveyDefinition Definition;
	Definition.SurveyId=TEXT("test");Definition.Title=TEXT("설문");
	FProjectProject01SurveyQuestion Choice;Choice.Id=TEXT("choice");Choice.Type=TEXT("Choice");Choice.Prompt=TEXT("객관식");Choice.Options={TEXT("답변 A"),TEXT("답변 B")};
	FProjectProject01SurveyQuestion Text;Text.Id=TEXT("text");Text.Type=TEXT("Text");Text.Prompt=TEXT("주관식");
	Definition.Questions={Choice,Text};Survey->ConfigureSurvey(Definition,nullptr);
	TArray<UWidget*> Widgets;Survey->WidgetTree->GetAllWidgets(Widgets);int32 Choices=0,Fields=0;
	for(UWidget* Widget:Widgets)
	{
		if(auto* Check=Cast<UCheckBox>(Widget))
		{
			if(auto* Label=Cast<UTextBlock>(Check->GetChildAt(0)))
			{++Choices;TestTrue(TEXT("Choice answers are white"),Label->GetColorAndOpacity().GetSpecifiedColor().Equals(FLinearColor::White));}
		}
		if(auto* Field=Cast<UMultiLineEditableTextBox>(Widget))
		{
			++Fields;
			TestTrue(TEXT("Written answer is white"),Field->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(FLinearColor::White));
			TestTrue(TEXT("Focused answer remains white"),Field->WidgetStyle.FocusedForegroundColor.GetSpecifiedColor().Equals(FLinearColor::White));
			TestTrue(TEXT("Written answer field stays dark"),Field->WidgetStyle.BackgroundColor.GetSpecifiedColor().R<0.2f);
			TestEqual(TEXT("Survey writing font is readable"),Field->WidgetStyle.TextStyle.Font.Size,20.0f);
			if(auto* Frame=Cast<USizeBox>(Field->GetParent()))TestTrue(TEXT("Survey writing field has real height"),Frame->GetMinDesiredHeight()>=160);
		}
	}
	TestEqual(TEXT("Both choice labels inspected"),Choices,2);TestEqual(TEXT("Written answer inspected"),Fields,1);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectVoiceSettingsLayoutTest,"ProjectProject01.Operations.VoiceSettingsControls",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectVoiceSettingsLayoutTest::RunTest(const FString&)
{
	// Initialize the tree only. No microphone access or EOS connection during automated testing.
	UUserWidget* Widget=NewObject<UProjectProject01VoiceSettingsWidget>();
	if(!TestTrue(TEXT("Voice settings initialize"),Widget->Initialize()))return false;
	TArray<UWidget*> Widgets;Widget->WidgetTree->GetAllWidgets(Widgets);int32 Combos=0,Meters=0,Sliders=0;
	for(UWidget* Child:Widgets)
	{
		Combos+=Child->IsA<UComboBoxString>()?1:0;Meters+=Child->IsA<UProgressBar>()?1:0;
		if(auto* Slider=Cast<USlider>(Child)){++Sliders;TestEqual(TEXT("Volume lower bound"),Slider->GetMinValue(),0.0f);TestEqual(TEXT("Volume upper bound"),Slider->GetMaxValue(),1.0f);}
	}
	TestEqual(TEXT("Input/output/peer selectors"),Combos,3);TestEqual(TEXT("Microphone level meter"),Meters,1);TestEqual(TEXT("Overall and selected peer volume gauges"),Sliders,2);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectLobbyReadabilityTest,"ProjectProject01.Operations.LobbyReadability",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectLobbyReadabilityTest::RunTest(const FString&)
{
	auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Instance=NewObject<UGameInstance>();World->SetGameInstance(Instance);Instance->Init();
	UUserWidget* Base=NewObject<UProjectProject01LobbyWidget>(World);Base->Initialize();auto* Lobby=CastChecked<UProjectProject01LobbyWidget>(Base);
	ON_SCOPE_EXIT{Lobby->ReleaseSlateResources(true);Instance->Shutdown();World->SetGameInstance(nullptr);World->DestroyWorld(false);};
	FProjectProject01RoomSummary A;A.Name=TEXT("함께 탈출해요");A.JoinCode=TEXT("AAA11111");A.HostDisplayName=TEXT("테스트 방장");A.MemberCount=1;
	FProjectProject01RoomSummary B=A;B.JoinCode=TEXT("BBB22222");
    Lobby->PopulatePublicRooms({A,B});TestEqual(TEXT("All public rooms are visible list rows, even with identical names"),Lobby->PublicRoomBindings.Num(),2);
    TestTrue(TEXT("Public rooms require explicit selection"),Lobby->SelectedPublicRoomCode.IsEmpty());
    TestEqual(TEXT("Code and password form is initially hidden"),Lobby->CodeJoinPanel->GetVisibility(),ESlateVisibility::Collapsed);
	Lobby->DirectRoomCodeInput->SetText(FText::FromString(TEXT("OLD12345")));Lobby->JoinPasswordInput->SetText(FText::FromString(TEXT("old-password")));
    Lobby->PublicRoomBindings[0]->HandleClicked();
	TestTrue(TEXT("User selecting a room sees selected banner"),Lobby->SelectedPublicRoomText->GetText().ToString().Contains(TEXT("✓ 선택됨")));
	TestTrue(TEXT("Selected room cannot accidentally join an old direct code"),Lobby->DirectRoomCodeInput->GetText().IsEmpty()&&Lobby->JoinPasswordInput->GetText().IsEmpty());
	A.MemberCount=2;Lobby->PopulatePublicRooms({B,A});
    TestEqual(TEXT("Refresh preserves stable room code, not changing row caption"),Lobby->SelectedPublicRoomCode,A.JoinCode);
	TestTrue(TEXT("Banner shows updated member count"),Lobby->SelectedPublicRoomText->GetText().ToString().Contains(TEXT("2/3")));
    A.bHasPassword=true;Lobby->PopulatePublicRooms({B,A});
    TestEqual(TEXT("Selecting a locked public room reveals its password field"),Lobby->PublicJoinPasswordInput->GetVisibility(),ESlateVisibility::Visible);
    Lobby->PublicJoinPasswordInput->SetText(FText::FromString(TEXT("test-password")));auto* StablePublicRow=Lobby->PublicRoomBindings[1].Get();Lobby->PopulatePublicRooms({B,A});
    TestTrue(TEXT("Polling preserves selected room button and typed public password"),Lobby->PublicRoomBindings[1]==StablePublicRow&&Lobby->PublicJoinPasswordInput->GetText().ToString()==TEXT("test-password"));
    Lobby->PopulatePublicRooms({B});TestTrue(TEXT("Disappearing selected room does not auto-select another room"),Lobby->SelectedPublicRoomCode.IsEmpty());
	TestTrue(TEXT("Disappearing room clears selected banner"),Lobby->SelectedPublicRoomText->GetText().ToString().Contains(TEXT("선택한 공개방 없음")));
    TestEqual(TEXT("Room item font enlarged"),Lobby->PublicRoomBindings[0]->Label->GetFont().Size,22.0f);
	TestEqual(TEXT("Room chat font enlarged"),Lobby->ChatInput->GetWidgetStyle().TextStyle.Font.Size,20.0f);
	TestTrue(TEXT("Room chat writing field enlarged"),CastChecked<USizeBox>(Lobby->ChatInput->GetParent())->GetMinDesiredHeight()>=54);
	const FString Directory=FPaths::ProjectSavedDir()/TEXT("FeatureChecks/ReadabilityUI");IFileManager::Get().MakeDirectory(*Directory,true);
    // Hold the live Slate root across renders; otherwise the harness reconstructs the widget and resets the countdown.
    auto LobbySlate=Lobby->TakeWidget();
	auto Render=[&](UUserWidget* Widget,const FString& Name)
	{
		auto Slate=Widget==Lobby?LobbySlate:Widget->TakeWidget();if(Widget==Lobby)Lobby->SetStatus(TEXT("입력란·공개방 표시 렌더링 검사"),false);
		FWidgetRenderer Renderer(true);const FVector2D Size(1920,1080);auto* Target=Renderer.DrawWidget(Slate,Size);
		Renderer.DrawWidget(Target,Slate,Size,.016f);FlushRenderingCommands();FBufferArchive Image;
		TestTrue(TEXT("Readability UI renders"),FImageUtils::ExportRenderTarget2DAsPNG(Target,Image));TestTrue(TEXT("Readability screenshot written"),FFileHelper::SaveArrayToFile(Image,*(Directory/(Name+TEXT(".png")))));
	};
    Render(Lobby,TEXT("MatchingModes"));TestEqual(TEXT("Lobby opens matching mode chooser"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),4);
    auto* State=Instance->GetSubsystem<UProjectProject01LobbySubsystem>();
    State->CurrentRoom.RoomId=TEXT("fixture-party");State->CurrentRoom.bRandomParty=true;State->CurrentRoom.bIsHost=true;
    FProjectProject01RoomMember P;P.UserId=TEXT("1");P.DisplayName=TEXT("개인 참가자");P.bIsHost=true;State->CurrentRoom.Members={P};
    Lobby->RefreshCurrentRoomView();Render(Lobby,TEXT("RandomSolo"));
    TestEqual(TEXT("Solo matchmaking has separate screen"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),3);
    TestTrue(TEXT("Idle solo can start and go back"),Lobby->MatchStartButton->GetIsEnabled()&&Lobby->MatchBackButton->GetIsEnabled());
    TArray<UWidget*> MatchWidgets;Lobby->WidgetTree->GetAllWidgets(MatchWidgets);int32 MatchButtons=0,MatchInputs=0;
    for(UWidget* Widget:MatchWidgets)
    {
        for(UWidget* Parent=Widget;Parent;Parent=Parent->GetParent())
        {
            if(Parent!=Lobby->LobbyViewSwitcher->GetActiveWidget())continue;
            if(Cast<UButton>(Widget))++MatchButtons;
            if(Cast<UEditableTextBox>(Widget)||Cast<UMultiLineEditableTextBox>(Widget))++MatchInputs;
            break;
        }
    }
    TestEqual(TEXT("Random screen has only start, cancel and back; no party invitation"),MatchButtons,3);
    TestEqual(TEXT("Random screen has no chat input"),MatchInputs,0);
    State->CurrentRoom.Members.Add(P);Lobby->RefreshMatchmakingView();
    TestFalse(TEXT("Legacy multi-user party cannot start a solo search"),Lobby->MatchStartButton->GetIsEnabled());
    State->CurrentRoom.Members.SetNum(1);
    State->bMatchmaking=true;State->MatchmakingWaiting=1;State->MatchmakingSeconds=175;Lobby->RefreshMatchmakingView();Render(Lobby,TEXT("RandomWaiting"));
    TestTrue(TEXT("Waiting offers cancellation but not back"),Lobby->MatchCancelButton->GetIsEnabled()&&!Lobby->MatchBackButton->GetIsEnabled());
    State->bMatchmaking=false;State->CurrentRoom.bRandomMatch=true;State->CurrentRoom.bStarting=true;State->CurrentRoom.AssignedRole=TEXT("Survivor");
    Lobby->RefreshCurrentRoomView();Render(Lobby,TEXT("RandomConfirmed"));
    TestFalse(TEXT("Confirmed match disables cancel and back"),Lobby->MatchCancelButton->GetIsEnabled()||Lobby->MatchBackButton->GetIsEnabled());
    TestTrue(TEXT("Confirmed countdown reveals only own role"),Lobby->StartCountdownText->GetText().ToString().Contains(TEXT("생존자"))&&!Lobby->StartCountdownText->GetText().ToString().Contains(TEXT("마네킹")));
    TestEqual(TEXT("Random countdown begins at five"),Lobby->GameStartCountdownNumber,5);
    Lobby->CancelGameStartCountdown();State->ForceClearLocalSession();Lobby->HandleOpenMatchClicked();Lobby->RefreshCurrentRoomView();
    Lobby->PopulatePublicRooms({A,B});Lobby->PublicRoomBindings[0]->HandleClicked();Render(Lobby,TEXT("PublicRooms"));
    Lobby->HandleOpenCodeJoinClicked();Render(Lobby,TEXT("JoinByCode"));
    TestEqual(TEXT("Code entry button reveals code/password form"),Lobby->CodeJoinPanel->GetVisibility(),ESlateVisibility::Visible);
    TestTrue(TEXT("Code form masks password"),Lobby->JoinPasswordInput->GetIsPassword());
    Lobby->HandleJoinByCodeClicked();TestTrue(TEXT("Blank direct code never falls back to selected public room"),Lobby->LobbyStatusText->GetText().ToString().Contains(TEXT("참가 코드를 입력")));
    Lobby->DirectRoomCodeInput->SetText(FText::FromString(TEXT("ABCD1234")));Lobby->JoinPasswordInput->SetText(FText::FromString(TEXT("secret-fixture")));
    Lobby->RefreshCurrentRoomView();TestEqual(TEXT("Periodic room refresh keeps code form open"),Lobby->CodeJoinPanel->GetVisibility(),ESlateVisibility::Visible);
    Lobby->HandleCloseCodeJoinClicked();TestTrue(TEXT("Closing code form clears code and password"),Lobby->DirectRoomCodeInput->GetText().IsEmpty()&&Lobby->JoinPasswordInput->GetText().IsEmpty());
	FProjectProject01RoomMember M1;M1.UserId=TEXT("101");M1.DisplayName=TEXT("같은 이름");M1.bIsHost=true;M1.AssignedRole=TEXT("Mannequin");
	FProjectProject01RoomMember M2=M1;M2.UserId=TEXT("202");M2.bIsHost=false;
    State->CurrentRoom.RoomId=TEXT("host-selection-fixture");State->CurrentRoom.bIsHost=true;State->CurrentRoom.Members={M1,M2};Lobby->RefreshCurrentRoomView();
    TestEqual(TEXT("Host transfer displays each other participant as a list row"),Lobby->HostTransferBindings.Num(),1);
    TestFalse(TEXT("Host transfer requires explicitly selecting a target"),Lobby->TransferHostButton->GetIsEnabled());
    auto* HostRow=Lobby->HostTransferBindings[0].Get();HostRow->HandleClicked();
    TestEqual(TEXT("Host row click selects stable user id"),Lobby->SelectedHostTransferUserId,M2.UserId);
    TestTrue(TEXT("Only selected target enables transfer button"),Lobby->TransferHostButton->GetIsEnabled());
    Lobby->RefreshCurrentRoomView();TestTrue(TEXT("Periodic refresh preserves live host row and selection"),Lobby->HostTransferBindings[0]==HostRow&&Lobby->SelectedHostTransferUserId==M2.UserId);
    Render(Lobby,TEXT("HostTransferList"));
    State->CurrentRoom.Members={M1};Lobby->RefreshCurrentRoomView();
    TestTrue(TEXT("Departed host transfer target clears selection without selecting another"),Lobby->SelectedHostTransferUserId.IsEmpty()&&!Lobby->TransferHostButton->GetIsEnabled());
    State->ForceClearLocalSession();
	Lobby->PopulateRoomMemberProfiles({M1,M2},false);
	TestEqual(TEXT("Every room member has a profile action"),Lobby->MemberProfileBindings.Num(),2);
	TestTrue(TEXT("Same names bind to distinct stable user IDs"),Lobby->MemberProfileBindings[0]->UserId!=Lobby->MemberProfileBindings[1]->UserId);
	auto* FirstBinding=Lobby->MemberProfileBindings[0].Get();Lobby->PopulateRoomMemberProfiles({M1,M2},false);
	TestTrue(TEXT("Polling does not recreate buttons under mouse press"),Lobby->MemberProfileBindings[0]==FirstBinding);
	auto* MemberRow=CastChecked<UHorizontalBox>(CastChecked<UBorder>(Lobby->MemberProfilePanel->GetChildAt(0))->GetContent());
	TestFalse(TEXT("Profile buttons do not expose roles before reveal"),CastChecked<UTextBlock>(MemberRow->GetChildAt(0))->GetText().ToString().Contains(TEXT("Mannequin")));
	TestEqual(TEXT("Profile button is not always shown; right-click only"),MemberRow->GetChildrenCount(),1);
	FPointerEvent LeftClick(0,FVector2D::ZeroVector,FVector2D::ZeroVector,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
	TestFalse(TEXT("Left click never opens the profile menu"),FirstBinding->HandleMouseButtonDown(FGeometry(),LeftClick).NativeReply.IsEventHandled());
	Lobby->PopulateRoomMemberProfiles({},false);TestEqual(TEXT("Leaving room clears stale profile buttons"),Lobby->MemberProfilePanel->GetChildrenCount(),0);
	FPointerEvent RightClick(0,FVector2D::ZeroVector,FVector2D::ZeroVector,TSet<FKey>{EKeys::RightMouseButton},EKeys::RightMouseButton,0,FModifierKeysState());
	TestFalse(TEXT("Old right-click callback is harmless after room exit"),FirstBinding->HandleMouseButtonDown(FGeometry(),RightClick).NativeReply.IsEventHandled());
	Lobby->PopulateRoomMemberProfiles({M1,M2},false);
	Lobby->ShowLobbyView(2);Lobby->ChatLogText->SetText(FText::FromString(TEXT("[친구] 같이 탈출하자!\n[나] 준비됐어요.")));Lobby->ChatInput->SetText(FText::FromString(TEXT("한글 채팅 입력 가독성 검사")));Render(Lobby,TEXT("RoomChat"));
	UUserWidget* SurveyBase=NewObject<UProjectProject01SurveyWidget>(World);SurveyBase->Initialize();auto* Survey=CastChecked<UProjectProject01SurveyWidget>(SurveyBase);
	FProjectProject01SurveyDefinition Definition;Definition.Title=TEXT("설문조사 입력란 가독성");FProjectProject01SurveyQuestion Q;Q.Id=TEXT("readability");Q.Type=TEXT("Text");Q.Prompt=TEXT("게임을 하면서 느낀 점을 적어주세요.");Definition.Questions={Q};Survey->ConfigureSurvey(Definition,nullptr);Render(Survey,TEXT("SurveyWriting"));Survey->ReleaseSlateResources(true);
	return true;
}
#endif

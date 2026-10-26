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
#include "Components/ScrollBox.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/UserInterfaceSettings.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "ProjectLoadingScreenSubsystem.h"
#include "ProjectLoadingScreenState.h"
#include "ProjectOpenObserver.h"
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
	auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Instance=NewObject<UGameInstance>();World->SetGameInstance(Instance);Instance->Init();
	UUserWidget* Base=NewObject<UProjectProject01LobbyWidget>(World);
	if(!TestTrue(TEXT("Lobby initializes"),Base->Initialize()))return false;
	auto* Lobby=CastChecked<UProjectProject01LobbyWidget>(Base);
	ON_SCOPE_EXIT{Lobby->CancelGameStartCountdown();Instance->Shutdown();World->SetGameInstance(nullptr);World->DestroyWorld(false);};
	auto* State=Instance->GetSubsystem<UProjectProject01LobbySubsystem>();auto* Loading=Instance->GetSubsystem<UProjectLoadingScreenSubsystem>();
	if(!TestNotNull(TEXT("Loading presentation subsystem exists"),Loading))return false;
	FProjectProject01RoomState Room;Room.RoomId=TEXT("fixture-room");Room.bStarting=true;Room.bIsHost=true;
	State->CurrentRoom=Room;
	TArray<FString> Requests;State->ReviewRequestSink=[&Requests](const FString& Endpoint){Requests.Add(Endpoint);};
	Lobby->BeginGameStartCountdown();
	TestEqual(TEXT("Countdown starts at five"),Lobby->GameStartCountdownNumber,5);
	TestTrue(TEXT("Countdown presents the start, without granting admission"),Lobby->StartCountdownText->GetText().ToString().Contains(TEXT("게임을 시작합니다")));
	TestFalse(TEXT("No fade during countdown"),Lobby->bGameStartFadeActive);
	Lobby->AdvanceGameStartCountdown();
	TestEqual(TEXT("Only one countdown number shown"),Lobby->StartCountdownText->GetText().ToString(),FString(TEXT("게임을 시작합니다\n4...")));
	for(int32 Count=0;Count<4;++Count)Lobby->AdvanceGameStartCountdown();
	TestTrue(TEXT("Countdown ends in fade even while server is still Starting"),Lobby->bGameStartFadeActive && !State->GetCurrentRoom().bStarted);
	TestEqual(TEXT("Fade must not suppress the host's confirm request"),Requests.Num(),1);
	TestFalse(TEXT("Loading overlay is not exposed before fading black"),Loading->IsLoading());
	Lobby->CompleteGameStartFade();
	TestTrue(TEXT("Same loading screen takes over after blackout"),Loading->IsLoading() && Loading->bAwaitingServer);
	TestTrue(TEXT("Old lobby countdown panel is removed"),Lobby->StartCountdownPanel->GetVisibility()==ESlateVisibility::Collapsed);
	TestTrue(TEXT("Session status is inside the loading state"),Loading->State->Read().Stage.Contains(TEXT("세션 접속 중")));
	State->CurrentRoom.bStarting=false;State->CurrentRoom.bServerPreparing=true;State->CurrentRoom.SessionWaitPhase=TEXT("Queued");
	Lobby->RefreshCurrentRoomView();
	TestTrue(TEXT("Queue polling does not reset the completed countdown or hide loading"),Lobby->bGameStartCountdownComplete && Loading->IsLoading());
	TestEqual(TEXT("Queue phase stays in loading screen"),Loading->State->Read().Progress,uint8(5));
	TestTrue(TEXT("Open host retains cancel inside the loading screen"),Loading->State->Read().CanCancel);
	State->CurrentRoom.SessionWaitPhase=TEXT("Preparing");Lobby->ContinueGameAdmission(1);
	TestEqual(TEXT("Server startup updates the same loading screen"),Loading->State->Read().Stage,FString(TEXT("게임 서버 준비 중…")));
	{FScopeLock Lock(&Loading->State->Mutex);Loading->State->Data.Started=FPlatformTime::Seconds()-180;}
	Loading->TickLoading(.05f);
	TestTrue(TEXT("Allocation wait does not consume map-loading timeout"),Loading->IsLoading());
	TestEqual(TEXT("Allocation never requests a ticket early"),Requests.Num(),1);
	State->CurrentRoom.bServerPreparing=false;State->CurrentRoom.bStarted=true;State->CurrentRoom.MatchId=TEXT("match-a");
	Lobby->ContinueGameAdmission(0);
	TestEqual(TEXT("Only a started session requests admission ticket"),Requests.Num(),2);
	TestEqual(TEXT("Admission is represented inside loading, not a separate label"),Loading->State->Read().Progress,uint8(15));
	TestFalse(TEXT("Random/committed admission cannot be cancelled by a loading button"),Loading->State->Read().CanCancel);
	Loading->BeginServerTravel();
	TestEqual(TEXT("Server connection continues on the same progress bar"),Loading->State->Read().Stage,FString(TEXT("게임 서버 접속 중…")));
	TestFalse(TEXT("Connection phase is no longer allocation"),Loading->bAwaitingServer);
	Lobby->CancelGameStartCountdown();
	TestFalse(TEXT("Rollback removes loading and clears preparation callbacks"),Loading->IsLoading());
	int32 Cancellations=0;Loading->ShowSessionPreparation(TEXT("세션 접속 중…"),5,true,FSimpleDelegate::CreateLambda([&Cancellations](){++Cancellations;}));
	Loading->RequestCancelPreparation();Loading->RequestCancelPreparation();
	TestEqual(TEXT("Repeated cancel clicks issue only one action until the next state update"),Cancellations,1);
	Loading->CancelLoading();
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
		auto Slate=Widget==Lobby?LobbySlate:Widget->TakeWidget();if(Widget==Lobby)Lobby->SetStatus(TEXT("방을 생성했습니다."),false);
		FWidgetRenderer Renderer(true);const FVector2D Size(1920,1080);auto* Target=Renderer.DrawWidget(Slate,Size);
		Renderer.DrawWidget(Target,Slate,Size,Name==TEXT("ObserverResults")?2.f:.016f);FlushRenderingCommands();FBufferArchive Image;
		TestTrue(TEXT("Readability UI renders"),FImageUtils::ExportRenderTarget2DAsPNG(Target,Image));TestTrue(TEXT("Readability screenshot written"),FFileHelper::SaveArrayToFile(Image,*(Directory/(Name+TEXT(".png")))));
	};
    Render(Lobby,TEXT("MatchingModes"));TestEqual(TEXT("Lobby opens matching mode chooser"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),4);
    TestNotNull(TEXT("Matching mode chooser has a logout button"),Lobby->ModesLogoutButton.Get());
    TestTrue(TEXT("Mode logout is directly below the mode choices, not in a bottom footer"),
        Lobby->ModesLogoutButton->GetParent()->GetParent()==Lobby->LobbyViewSwitcher->GetActiveWidget());
    TestTrue(TEXT("Leaderboard opens from the matching-mode selection page"),
        Lobby->OpenLeaderboardTestButton->GetParent()==Lobby->ModesLogoutButton->GetParent());
    auto* State=Instance->GetSubsystem<UProjectProject01LobbySubsystem>();
    Lobby->HandleBotMatchClicked();
    TestEqual(TEXT("Bot match has a separate authenticated match setup page"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),7);
    TestTrue(TEXT("Opening bot match does not start offline practice or join a queue"),!State->HasCurrentRoom()&&!State->IsMatchmaking());
    TestEqual(TEXT("Bot setup offers both player roles and random"),Lobby->BotMatchRole->GetOptionCount(),3);
    TestTrue(TEXT("Each matching flow has its own role chooser"),Lobby->RandomMatchRole&&Lobby->RoomMatchRole);
    TestEqual(TEXT("Random role choice uses requested label"),Lobby->RandomMatchRole->GetOptionAtIndex(0),FString(TEXT("무작위")));
    TestEqual(TEXT("Open room role choice uses requested label"),Lobby->RoomMatchRole->GetOptionAtIndex(0),FString(TEXT("무작위")));
    TestEqual(TEXT("Bot setup offers exactly five difficulty levels"),Lobby->BotMatchDifficulty->GetOptionCount(),5);
    Lobby->RefreshCurrentRoomView();TestEqual(TEXT("Polling preserves bot match configuration"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),7);
    Render(Lobby,TEXT("BotMatchSetup"));
    Lobby->HandleLobbyModesClicked();TestEqual(TEXT("Bot match back returns to matching selection"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),4);
    State->CurrentRoom.RoomId=TEXT("fixture-open-bots");State->CurrentRoom.bIsHost=true;
    FProjectProject01RoomMember BotHost;BotHost.UserId=TEXT("11");BotHost.DisplayName=TEXT("방장");BotHost.bIsHost=true;State->CurrentRoom.Members={BotHost};
    FProjectProject01RoomBot BotA;BotA.BotId=FGuid::NewGuid().ToString();BotA.DisplayName=TEXT("봇 1");BotA.Difficulty=1;
    auto BotB=BotA;BotB.BotId=FGuid::NewGuid().ToString();BotB.DisplayName=TEXT("봇 2");BotB.Difficulty=5;State->CurrentRoom.Bots={BotA,BotB};
    Lobby->RefreshCurrentRoomView();
    TestEqual(TEXT("Host sees open-room bot controls"),Lobby->RoomBotPanel->GetVisibility(),ESlateVisibility::Visible);
    TestFalse(TEXT("Human plus two bots fills playing capacity"),Lobby->AddRoomBotButton->GetIsEnabled());
    TestTrue(TEXT("Host can choose each bot for delete or level change"),Lobby->RoomBotSelection->GetOptionCount()==2&&Lobby->RemoveRoomBotButton->GetIsEnabled()&&Lobby->ChangeRoomBotButton->GetIsEnabled());
    Lobby->RoomBotSelection->SetSelectedIndex(1);Lobby->RefreshCurrentRoomView();
    TestEqual(TEXT("Room polling does not reset bot selection"),Lobby->RoomBotSelection->GetSelectedIndex(),1);
    Render(Lobby,TEXT("OpenRoomBots"));
    State->CurrentRoom.bIsHost=false;Lobby->RefreshCurrentRoomView();
    TestEqual(TEXT("Non-host cannot access bot management UI"),Lobby->RoomBotPanel->GetVisibility(),ESlateVisibility::Collapsed);
    State->CurrentRoom={};Lobby->RefreshCurrentRoomView();
    Lobby->HandleTutorialClicked();
    TestEqual(TEXT("Tutorial role chooser is separate from both matching flows"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),6);
    TestTrue(TEXT("Opening tutorial selection does not join a room or queue"),!State->HasCurrentRoom() && !State->IsMatchmaking());
    Render(Lobby,TEXT("TutorialRoles"));
    Lobby->RefreshCurrentRoomView();
    TestEqual(TEXT("Periodic lobby refresh preserves tutorial role selection"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),6);
    Lobby->HandleLobbyModesClicked();
    TestEqual(TEXT("Tutorial back returns to matching choices"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),4);
    State->CurrentRoom.RoomId=TEXT("fixture-party");State->CurrentRoom.bRandomParty=true;State->CurrentRoom.bIsHost=true;
    FProjectProject01RoomMember P;P.UserId=TEXT("1");P.DisplayName=TEXT("개인 참가자");P.bIsHost=true;State->CurrentRoom.Members={P};
    Lobby->RefreshCurrentRoomView();Render(Lobby,TEXT("RandomSolo"));
    TestTrue(TEXT("Random matching ready screen permits logout"),Lobby->MatchLogoutButton->GetIsEnabled());
    TestTrue(TEXT("Random logout is directly below back, not in a bottom footer"),
        Lobby->MatchLogoutButton->GetParent()==Lobby->MatchBackButton->GetParent());
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
    TestEqual(TEXT("Random screen has start, cancel, back and logout; no party invitation"),MatchButtons,4);
    TestEqual(TEXT("Random screen has no chat input"),MatchInputs,0);
    State->CurrentRoom.Members.Add(P);Lobby->RefreshMatchmakingView();
    TestFalse(TEXT("Legacy multi-user party cannot start a solo search"),Lobby->MatchStartButton->GetIsEnabled());
    State->CurrentRoom.Members.SetNum(1);
    State->bMatchmaking=true;State->MatchmakingWaiting=1;State->MatchmakingSeconds=175;State->MatchmakingStartedAtSeconds=FPlatformTime::Seconds();
    Lobby->RefreshMatchmakingView();
    TestTrue(TEXT("Waiting shows elapsed time from zero, not the expiry countdown"),Lobby->MatchStatusText->GetText().ToString().Contains(TEXT("매칭 경과 시간: 0분 00초")) && !Lobby->MatchStatusText->GetText().ToString().Contains(TEXT("남은 대기 시간")));
    TestFalse(TEXT("Role preference is locked while searching"),Lobby->RandomMatchRole->GetIsEnabled());
    for(const int32 Elapsed:{59,60,125})
    {
        State->MatchmakingStartedAtSeconds=FPlatformTime::Seconds()-Elapsed-.01;
        Lobby->NativeTick(FGeometry(),.016f);
        TestTrue(TEXT("Elapsed display advances between polls and rolls seconds into minutes"),Lobby->MatchStatusText->GetText().ToString().Contains(FString::Printf(TEXT("%d분 %02d초"),Elapsed/60,Elapsed%60)));
    }
    auto WaitingResponse=MakeShared<FJsonObject>();WaitingResponse->SetStringField(TEXT("state"),TEXT("Waiting"));WaitingResponse->SetNumberField(TEXT("secondsRemaining"),10);
    FString MatchError;State->ParseMatchmaking(WaitingResponse,MatchError);
    TestTrue(TEXT("Polling remaining time cannot reset the elapsed stopwatch"),State->GetMatchmakingElapsedSeconds()>=125);
    auto CancelledResponse=MakeShared<FJsonObject>();CancelledResponse->SetStringField(TEXT("state"),TEXT("Cancelled"));State->ParseMatchmaking(CancelledResponse,MatchError);
    TestTrue(TEXT("Cancellation clears the elapsed stopwatch"),State->GetMatchmakingElapsedSeconds()==0 && State->MatchmakingStartedAtSeconds==0);
    State->ReviewRequestSink=[](const FString&){};State->JoinMatchmaking();State->ReviewRequestSink=nullptr;
    TestTrue(TEXT("A fresh search starts at zero with a fresh stopwatch"),State->IsMatchmaking() && State->GetMatchmakingElapsedSeconds()==0 && State->MatchmakingStartedAtSeconds>0);
    State->MatchmakingStartedAtSeconds=FPlatformTime::Seconds()-65;Lobby->RefreshMatchmakingView();Render(Lobby,TEXT("RandomWaiting"));
    TestTrue(TEXT("Waiting offers cancellation but not back"),Lobby->MatchCancelButton->GetIsEnabled()&&!Lobby->MatchBackButton->GetIsEnabled());
    TestTrue(TEXT("Random matching queue permits logout"),Lobby->MatchLogoutButton->GetIsEnabled());
    State->bMatchmaking=false;State->CurrentRoom.bRandomMatch=true;State->CurrentRoom.bStarting=true;State->CurrentRoom.AssignedRole=TEXT("Survivor");
    Lobby->RefreshCurrentRoomView();Render(Lobby,TEXT("RandomConfirmed"));
    TestFalse(TEXT("Confirmed match disables cancel and back"),Lobby->MatchCancelButton->GetIsEnabled()||Lobby->MatchBackButton->GetIsEnabled());
    TestFalse(TEXT("Confirmed random match also locks logout until entering gameplay"),Lobby->MatchLogoutButton->GetIsEnabled());
    TestTrue(TEXT("Confirmed countdown reveals only own role"),Lobby->StartCountdownText->GetText().ToString().Contains(TEXT("생존자"))&&!Lobby->StartCountdownText->GetText().ToString().Contains(TEXT("마네킹")));
    TestEqual(TEXT("Random countdown begins at five"),Lobby->GameStartCountdownNumber,5);
    Lobby->CancelGameStartCountdown();State->ForceClearLocalSession();Lobby->HandleOpenMatchClicked();Lobby->RefreshCurrentRoomView();
    auto* Browser=CastChecked<UVerticalBox>(Lobby->LogoutButton->GetParent());
    const int32 LogoutIndex=Browser->GetChildIndex(Lobby->LogoutButton),CreateIndex=Browser->GetChildIndex(Lobby->OpenCreateRoomButton);
    TestEqual(TEXT("Join by code immediately follows selected-room join"),Browser->GetChildIndex(Lobby->OpenCodeJoinButton),Browser->GetChildIndex(Lobby->JoinRoomButton)+1);
    TestEqual(TEXT("Create-room immediately follows join by code"),CreateIndex,Browser->GetChildIndex(Lobby->OpenCodeJoinButton)+1);
    TestEqual(TEXT("Logout immediately follows back below create-room"),LogoutIndex,CreateIndex+2);
    auto* BrowserBack=CastChecked<UButton>(Browser->GetChildAt(CreateIndex+1));
    TestEqual(TEXT("Back immediately follows create-room and uses the requested label"),CastChecked<UTextBlock>(BrowserBack->GetChildAt(0))->GetText().ToString(),FString(TEXT("뒤로가기")));
    TestTrue(TEXT("Leaderboard no longer consumes space in the open browser"),Lobby->OpenLeaderboardTestButton->GetParent()!=Browser);
    Lobby->PopulatePublicRooms({A,B});Lobby->PublicRoomBindings[0]->HandleClicked();Render(Lobby,TEXT("PublicRooms"));
    Lobby->HandleOpenCodeJoinClicked();Render(Lobby,TEXT("JoinByCode"));
    TestEqual(TEXT("Code entry button reveals code/password form"),Lobby->CodeJoinPanel->GetVisibility(),ESlateVisibility::Visible);
    TestEqual(TEXT("Code entry switches to its own page"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),5);
    for(auto* Parent=Lobby->PublicRoomList->GetParent();Parent;Parent=Parent->GetParent())
        TestTrue(TEXT("Public room list does not belong to the active code-entry page"),Parent!=Lobby->LobbyViewSwitcher->GetActiveWidget());
    TestTrue(TEXT("Code form masks password"),Lobby->JoinPasswordInput->GetIsPassword());
    Lobby->HandleJoinByCodeClicked();TestTrue(TEXT("Blank direct code never falls back to selected public room"),Lobby->LobbyStatusText->GetText().ToString().Contains(TEXT("참가 코드를 입력")));
    Lobby->DirectRoomCodeInput->SetText(FText::FromString(TEXT("ABCD1234")));Lobby->JoinPasswordInput->SetText(FText::FromString(TEXT("secret-fixture")));
    Lobby->RefreshCurrentRoomView();TestEqual(TEXT("Periodic room refresh keeps code form open"),Lobby->CodeJoinPanel->GetVisibility(),ESlateVisibility::Visible);
    TestEqual(TEXT("Polling cannot reopen public room list over code entry"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),5);
    Lobby->HandleCloseCodeJoinClicked();TestTrue(TEXT("Closing code form clears code and password"),Lobby->DirectRoomCodeInput->GetText().IsEmpty()&&Lobby->JoinPasswordInput->GetText().IsEmpty());
    TestEqual(TEXT("Code-entry back returns to public room browser"),Lobby->LobbyViewSwitcher->GetActiveWidgetIndex(),0);
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
	Lobby->ShowLobbyView(2);Lobby->ChatLogText->SetText(FText::FromString(TEXT("[친구] 같이 탈출하자!\n[나] 준비됐어요.")));Lobby->ChatInput->SetText(FText::GetEmpty());Render(Lobby,TEXT("RoomChat"));
	UUserWidget* SurveyBase=NewObject<UProjectProject01SurveyWidget>(World);SurveyBase->Initialize();auto* Survey=CastChecked<UProjectProject01SurveyWidget>(SurveyBase);
	FProjectProject01SurveyDefinition Definition;Definition.Title=TEXT("설문조사 입력란 가독성");FProjectProject01SurveyQuestion Q;Q.Id=TEXT("readability");Q.Type=TEXT("Text");Q.Prompt=TEXT("게임을 하면서 느낀 점을 적어주세요.");Definition.Questions={Q};Survey->ConfigureSurvey(Definition,nullptr);Render(Survey,TEXT("SurveyWriting"));Survey->ReleaseSlateResources(true);
    auto* ObserverView=NewObject<UProjectOpenObserverWidget>(World);ObserverView->Initialize();
    TArray<FProjectOpenObserverEntry> Participants;
    for(int32 I=0;I<3;++I){FProjectOpenObserverEntry Entry;Entry.PlayerId=FString::FromInt(I);Entry.Name=I==0?TEXT("마네킹 플레이어"):FString::Printf(TEXT("생존자 %d"),I);Entry.State=I==0?TEXT("마네킹"):TEXT("생존");Participants.Add(Entry);}
    ObserverView->Refresh(Participants,TEXT("MultiplayTest\n경기 시간 128초\n생존 2 / 탈출 0 / 탈락 0"));Render(ObserverView,TEXT("ObserverPanels"));ObserverView->ReleaseSlateResources(true);
    auto* ObserverResult=NewObject<UProjectProject01MatchResultWidget>(World);static_cast<UUserWidget*>(ObserverResult)->Initialize();
    FMultiplayTestMatchResult Observed;Observed.Role=TEXT("Observer");Observed.MatchDurationSeconds=128;
    for(int32 I=0;I<3;++I){FProjectObservedPlayerResult Row;Row.Name=Participants[I].Name;Row.Role=I==0?TEXT("Mannequin"):TEXT("Survivor");Row.bSuccess=I==2;Row.CaptureCount=I==0?1:0;Row.FirstCaptureSeconds=73;Row.EscapeSeconds=I==2?128:0;Row.RescueCount=I==2?1:0;Observed.ObservedPlayers.Add(Row);}
    ObserverResult->ConfigureResult(Observed);FString AllText;
    ObserverResult->WidgetTree->ForEachWidget([&](UWidget* Item){if(auto* Text=Cast<UTextBlock>(Item))AllText+=Text->GetText().ToString();});
    TestTrue(TEXT("Observer results show the requested heading and both survivor records"),AllText.Contains(TEXT("경기가 종료되었습니다."))&&AllText.Contains(TEXT("생존자 1"))&&AllText.Contains(TEXT("생존자 2"))&&AllText.Contains(TEXT("포획 1회")));
    Render(ObserverResult,TEXT("ObserverResults"));ObserverResult->ReleaseSlateResources(true);
	// Regression for the whole-page scroll that previously hid room/browser actions.
	TestFalse(TEXT("Open browser does not scroll its entire page"),Lobby->LobbyViewSwitcher->GetWidgetAtIndex(0)->IsA<UScrollBox>());
	TestFalse(TEXT("Room does not scroll its entire page"),Lobby->LobbyViewSwitcher->GetWidgetAtIndex(2)->IsA<UScrollBox>());
	auto RenderLayout=[&](const FString& Name,const TArray<UWidget*>& Required)
	{
		for(const FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
		{
			FWidgetRenderer Renderer(false);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,false);
			if(!TestNotNull(TEXT("Fixed layout render target"),Target))continue;
			const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
			for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,LobbySlate,Dpi,FVector2D(Size),.016f);
			FReadSurfaceDataFlags PixelFlags(RCM_UNorm);PixelFlags.SetLinearToGamma(true);
			TArray<FColor> Pixels;TestTrue(TEXT("Fixed layout readback"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,PixelFlags));
			for(UWidget* Control:Required)
			{
				if(!TestNotNull(TEXT("Required room/browser control exists"),Control))continue;
				const auto Geometry=Control->GetCachedGeometry();const auto Position=Geometry.GetAbsolutePosition(),Extent=Geometry.GetAbsoluteSize();
				TestTrue(FString::Printf(TEXT("%s: %s is visible without page scrolling at %dx%d"),*Name,*Control->GetName(),Size.X,Size.Y),
					Control->GetVisibility()!=ESlateVisibility::Collapsed&&Extent.X>1&&Extent.Y>1&&Position.X>=-1&&Position.Y>=-1&&Position.X+Extent.X<=Size.X+1&&Position.Y+Extent.Y<=Size.Y+1);
			}
			TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);
			TestTrue(TEXT("Fixed layout screenshot written"),FFileHelper::SaveArrayToFile(Png,*(Directory/FString::Printf(TEXT("%s-%dx%d.png"),*Name,Size.X,Size.Y))));
		}
	};
	State->ForceClearLocalSession();Lobby->HandleOpenMatchClicked();Lobby->PopulatePublicRooms({A,B});Lobby->PublicRoomBindings[0]->HandleClicked();
	RenderLayout(TEXT("FixedBrowser"),{Lobby->JoinRoomButton,Lobby->OpenCodeJoinButton,Lobby->OpenCreateRoomButton,BrowserBack,Lobby->LogoutButton});
	State->CurrentRoom={};State->CurrentRoom.RoomId=TEXT("layout-host");State->CurrentRoom.Name=TEXT("함께 탈출해요");State->CurrentRoom.JoinCode=TEXT("ABCD1234");
	State->CurrentRoom.bIsHost=true;State->CurrentRoom.bGameServerOnline=true;State->CurrentRoom.bCanStart=true;
	auto ObserverMember=M2;ObserverMember.UserId=TEXT("303");ObserverMember.DisplayName=TEXT("관전자");ObserverMember.bIsObserver=true;
	State->CurrentRoom.Members={M1,M2,ObserverMember};State->CurrentRoom.Bots={BotA};Lobby->RefreshCurrentRoomView();
	TestEqual(TEXT("Host administration starts folded, leaving room for chat"),Lobby->RoomToolsFrame->GetVisibility(),ESlateVisibility::Collapsed);
    TestEqual(TEXT("An open-room host with bots keeps the original start-only behavior"),Lobby->ReadyButton->GetVisibility(),ESlateVisibility::Collapsed);
	RenderLayout(TEXT("FixedHostDefault"),{Lobby->StartRoomButton,Lobby->LeaveRoomButton,Lobby->ChatInput,Lobby->RoomToolsToggleButton});
    auto Third=M2;Third.UserId=TEXT("304");Third.DisplayName=TEXT("세 번째 참가자");
    auto SecondObserver=ObserverMember;SecondObserver.UserId=TEXT("305");SecondObserver.DisplayName=TEXT("두 번째 관전자");
    State->CurrentRoom.Members={M1,M2,Third,ObserverMember,SecondObserver};State->CurrentRoom.Bots={};Lobby->RefreshCurrentRoomView();
    TArray<UWidget*> FullRoster{Lobby->StartRoomButton,Lobby->ChatInput};
    for(auto* Roster:{Lobby->MemberProfilePanel.Get(),Lobby->ObserverProfilePanel.Get()})
    {
        for(auto* Parent=Roster->GetParent();Parent;Parent=Parent->GetParent())TestFalse(TEXT("Participant and observer rosters have no scroll container"),Parent->IsA<UScrollBox>());
        for(auto* Row:Roster->GetAllChildren())FullRoster.Add(Row);
    }
    RenderLayout(TEXT("FixedFullRoster"),FullRoster);
    TestEqual(TEXT("All three playing participants remain visible"),Lobby->MemberProfilePanel->GetChildrenCount(),3);
    TestEqual(TEXT("Both observers remain visible"),Lobby->ObserverProfilePanel->GetChildrenCount(),2);
	State->CurrentRoom.Members={M1,M2,ObserverMember};State->CurrentRoom.Bots={BotA};Lobby->RefreshCurrentRoomView();
	Lobby->HandleRoomToolsToggle();
	FString LongChat;for(int32 Line=0;Line<150;++Line)LongChat+=FString::Printf(TEXT("[참가자] 오래된 채팅 %d\n"),Line);
	Lobby->ChatLogText->SetText(FText::FromString(LongChat));Lobby->HandleRoomBotsTabClicked();
	RenderLayout(TEXT("FixedHostBots"),{Lobby->StartRoomButton,Lobby->LeaveRoomButton,Lobby->SwitchObserverButton,Lobby->ChatInput,Lobby->AddRoomBotButton,Lobby->RemoveRoomBotButton});
	Lobby->HandleRoomMembersTabClicked();
	RenderLayout(TEXT("FixedHostMembers"),{Lobby->StartRoomButton,Lobby->LeaveRoomButton,Lobby->TransferHostButton,Lobby->ParticipantManagement});
	for(UWidget* Child:Lobby->ParticipantManagement->GetAllChildren())
	{
		const auto Action=Child->GetCachedGeometry(),Panel=Lobby->RoomToolsFrame->GetCachedGeometry();
		TestTrue(TEXT("Every administration action fits inside its panel, clear of the room footer"),Action.GetAbsolutePosition().Y+Action.GetAbsoluteSize().Y<=Panel.GetAbsolutePosition().Y+Panel.GetAbsoluteSize().Y+1);
	}
	Lobby->HandleRoomSessionTabClicked();RenderLayout(TEXT("FixedHostSession"),{Lobby->DeleteRoomButton,Lobby->LeaveRoomButton,Lobby->StartRoomButton});
	State->CurrentRoom.bIsHost=false;Lobby->RefreshCurrentRoomView();
	TestEqual(TEXT("Guest does not reserve a host administration column"),Lobby->RoomToolsFrame->GetVisibility(),ESlateVisibility::Collapsed);
	RenderLayout(TEXT("FixedGuestRoom"),{Lobby->ReadyButton,Lobby->LeaveRoomButton,Lobby->SwitchObserverButton,Lobby->ChatInput});
    State->CurrentRoom.bIsObserver=true;State->CurrentRoom.bCanStart=false;Lobby->RefreshCurrentRoomView();
    TestEqual(TEXT("Human observers receive a ready button"),Lobby->ReadyButton->GetVisibility(),ESlateVisibility::Visible);
    RenderLayout(TEXT("ObserverGuestReady"),{Lobby->ReadyButton,Lobby->LeaveRoomButton});
    State->CurrentRoom.bIsHost=true;Lobby->RefreshCurrentRoomView();
    TestEqual(TEXT("Observing host does not receive a ready button"),Lobby->ReadyButton->GetVisibility(),ESlateVisibility::Collapsed);
    TestFalse(TEXT("Observing host start stays disabled while others are not ready"),Lobby->StartRoomButton->GetIsEnabled());
    State->CurrentRoom.bCanStart=true;Lobby->RefreshCurrentRoomView();TestTrue(TEXT("Observing host can start after all human participants and observers are ready"),Lobby->StartRoomButton->GetIsEnabled());
    RenderLayout(TEXT("ObserverHostReady"),{Lobby->StartRoomButton,Lobby->LeaveRoomButton});
    State->CurrentRoom.bIsObserver=false;
    State->CurrentRoom.bIsHost=true;State->CurrentRoom.bBotMatch=true;State->CurrentRoom.bIsReady=false;State->CurrentRoom.bCanStart=false;
    State->CurrentRoom.Members={M1,M2};State->CurrentRoom.Bots={BotA};Lobby->RefreshCurrentRoomView();
    TestEqual(TEXT("Duo bot host gets its own ready button"),Lobby->ReadyButton->GetVisibility(),ESlateVisibility::Visible);
    TestFalse(TEXT("Duo bot host cannot start while the server reports a missing ready"),Lobby->StartRoomButton->GetIsEnabled());
    RenderLayout(TEXT("DuoBotHostNotReady"),{Lobby->ReadyButton,Lobby->StartRoomButton,Lobby->LeaveRoomButton});
    State->CurrentRoom.bIsReady=true;State->CurrentRoom.bCanStart=true;Lobby->RefreshCurrentRoomView();
    TestTrue(TEXT("Duo bot start activates after both players are ready"),Lobby->StartRoomButton->GetIsEnabled());
    RenderLayout(TEXT("DuoBotHostReady"),{Lobby->ReadyButton,Lobby->StartRoomButton,Lobby->LeaveRoomButton});
	return true;
}
#endif

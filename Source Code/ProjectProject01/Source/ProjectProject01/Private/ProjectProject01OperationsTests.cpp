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
	TestEqual(TEXT("Input/output/peer selectors"),Combos,3);TestEqual(TEXT("Microphone level meter"),Meters,1);TestEqual(TEXT("Voice receive volume gauge"),Sliders,1);return true;
}
#endif

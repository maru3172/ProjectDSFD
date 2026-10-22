#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/App.h"
#include "ProjectProject01Tutorial.h"
#include "ProjectProject01GameInstance.h"
#include "PlayerCharacter.h"
#include "MannequinAICharacter.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetTree.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "Components/BoxComponent.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/SpinBox.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Kismet/GameplayStatics.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "UnrealClient.h"
#include "Misc/PackageName.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMannequinViewCommandTest,"ProjectProject01.Multiplayer.ViewOnlyCommands",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMannequinViewCommandTest::RunTest(const FString&)
{
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	ON_SCOPE_EXIT { if (World) World->DestroyWorld(false); };
	World->SetGameInstance(NewObject<UGameInstance>());
	FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));
	if (!TestTrue(TEXT("Real multiplayer game mode"),World->SetGameMode(URL))) return false;
	auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();
	auto* PC=World->SpawnActor<AMultiplayTestPlayerController>();
	FProjectProject01ValidatedJoinClaim Claim;Claim.Role=TEXT("Mannequin");Claim.UserId=TEXT("Test.Man");Claim.MatchId=TEXT("Test.Match");
	PC->SetAuthenticatedLobbyIdentity(Claim);
	auto* Body=World->SpawnActor<AMannequinAICharacter>();
	auto* AI=World->SpawnActor<AAIController>();AI->Possess(Body);PC->SetViewedMannequin(Body);
	Mode->SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::InProgress);
	for (auto Command:{EMultiplayMannequinCommand::Patrol,EMultiplayMannequinCommand::ChaseInDetectionRange,EMultiplayMannequinCommand::FollowMannequinPlayer})
	{
		TestTrue(TEXT("Z/X/C accepted while only viewing"),Mode->TryIssueMannequinCommand(PC,Command));
		TestEqual(TEXT("Selected body's persistent command changes immediately"),Body->GetMultiplayCommand(),Command);
		TestTrue(TEXT("Command preserves AI possession and view-only mode"),Body->GetController()==AI && !Body->IsManualControlEnabled() && PC->GetPawn()==nullptr);
	}
	TestTrue(TEXT("Q accepted without direct control"),Mode->TryResetFollowingMannequins(PC));
	TestEqual(TEXT("Q returns followers to patrol"),Body->GetMultiplayCommand(),EMultiplayMannequinCommand::Patrol);
	Mode->ToggleMannequinCatchDamage(PC);TestFalse(TEXT("B remains available without direct control"),Body->IsCatchDamageEnabled());
	auto* Survivor=World->SpawnActor<AMultiplayTestPlayerController>();Survivor->SetViewedMannequin(Body);
	TestFalse(TEXT("Survivor cannot issue mannequin commands"),Mode->TryIssueMannequinCommand(Survivor,EMultiplayMannequinCommand::Patrol));
	Mode->SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Results);
	TestFalse(TEXT("Commands after match end are rejected"),Mode->TryIssueMannequinCommand(PC,EMultiplayMannequinCommand::Patrol));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectTutorialTest,"ProjectProject01.Multiplayer.TutorialFlow",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectTutorialTest::RunTest(const FString&)
{
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	ON_SCOPE_EXIT { if (World) World->DestroyWorld(false); };
	if (!TestNotNull(TEXT("Isolated practice world"),World)) return false;
	World->SetGameInstance(NewObject<UGameInstance>());
	FURL URL; URL.AddOption(TEXT("game=/Script/ProjectProject01.ProjectProject01TutorialGameMode"));
	if (!TestTrue(TEXT("Tutorial world loads its own multiplayer-derived rules"),World->SetGameMode(URL))) return false;
	auto* Mode=World->GetAuthGameMode<AProjectProject01TutorialGameMode>();
	auto* PC=World->SpawnActor<AMultiplayTestPlayerController>(); PC->SetAsLocalPlayerController();
	PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>(); PC->PlayerCameraManager->InitializeFor(PC);
	auto* Pawn=World->SpawnActor<APlayerCharacter>(); PC->Possess(Pawn);
	PC->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Active);
	Mode->Plan=World->SpawnActor<AProjectProject01TutorialPlan>(); Mode->Learner=PC;
	Mode->Steps=Mode->Plan->SurvivorSteps; Mode->bInitialized=true; Mode->EnterStep();
	TestTrue(TEXT("Practice, not single-player rules"),AProjectProject01TutorialGameMode::IsTutorialWorld(World));
	TestTrue(TEXT("Intro waits for the reader"),Mode->StepIndex==0 && Mode->bAwaitingRead);
	Mode->Tick(30);
	TestEqual(TEXT("No reading deadline auto-dismisses the intro"),Mode->StepIndex,0);
	TestFalse(TEXT("Touching exit before training cannot skip the flow"),Mode->SignalEvent(PC,TEXT("Tutorial.Escape")));
	auto* UI=NewObject<UProjectProject01TutorialWidget>(World); UI->Initialize();
	for (const FKey Key:{EKeys::W,EKeys::SpaceBar,EKeys::Enter,EKeys::Escape})
	{
		const FKeyEvent Event(Key,FModifierKeysState(),0,false,0,0);
		UI->NativeOnPreviewKeyDown(FGeometry(),Event); UI->NativeOnKeyDown(FGeometry(),Event);
	}
	UI->NativeOnMouseButtonDown(FGeometry(),FPointerEvent());
	UI->ScreenButton->OnClicked.Broadcast();
	Mode->ToggleTutorialMenu();
	TestTrue(TEXT("Keys, ESC, text and backdrop clicks cannot acknowledge the explanation"),Mode->bAwaitingRead && Mode->StepIndex==0);
	TestEqual(TEXT("The confirmation button has a simple explicit label"),CastChecked<UTextBlock>(UI->ContinueButton->GetContent())->GetText().ToString(),FString(TEXT("확인")));
	UI->ContinueButton->OnClicked.Broadcast();
	TestTrue(TEXT("Acknowledging intro displays the walk explanation"),Mode->StepIndex==1 && Mode->bAwaitingRead);
	Mode->ContinueTutorial();
	TestFalse(TEXT("Acknowledging instructions enables actual practice"),Mode->bAwaitingRead);
	TestTrue(TEXT("Walk goal displays required meters"),Mode->GetPracticeStatusText().ToString().Contains(TEXT("0.0 / 10.0 m")));
	Mode->Progress=250;
	TestTrue(TEXT("Walk distance is converted from centimeters"),Mode->GetPracticeStatusText().ToString().Contains(TEXT("2.5 / 10.0 m")));
	Mode->Progress=0;
	TestFalse(TEXT("A message trigger cannot bypass its step prerequisite"),Mode->ShowMessage(PC,FText::FromString(TEXT("설명")),TEXT("Other")));
	TestTrue(TEXT("An authored explanation pauses the current practice"),Mode->ShowMessage(PC,FText::FromString(TEXT("충분히 읽은 뒤 계속하세요.")),TEXT("Survivor.Walk")));
	Mode->ContinueTutorial();
	TestTrue(TEXT("Closing a message resumes the same task, not the next one"),Mode->StepIndex==1 && !Mode->bAwaitingRead && Mode->ExtraMessage.IsEmpty());
	Mode->RecordAction(PC,EProjectTutorialCondition::Ping);
	TestEqual(TEXT("An unrelated input cannot pass a movement condition"),Mode->StepIndex,1);
	Mode->StepIndex=Mode->Steps.Num()-1; Mode->EnterStep();
	Mode->bPracticeOnly=true;
	TestEqual(TEXT("Survivor practical replay starts at the last escape exercise"),Mode->GetInitialStepIndex(),Mode->Steps.Num()-1);
	Mode->bPracticeOnly=false;
	TestEqual(TEXT("Full tutorial replay starts at the role introduction"),Mode->GetInitialStepIndex(),0);
	TestFalse(TEXT("An exit condition also waits for instruction acknowledgement"),Mode->SignalEvent(PC,TEXT("Tutorial.Escape")));
	Mode->ContinueTutorial();
	TestFalse(TEXT("Wrong named condition cannot finish training"),Mode->SignalEvent(PC,TEXT("Other.Exit")));
	TestTrue(TEXT("The right exit completes only this tutorial, without map travel"),Mode->SignalEvent(PC,TEXT("Tutorial.Escape")));
	TestTrue(TEXT("Closing message precedes result"),Mode->bFinishing && !Mode->bResultVisible);
	Mode->Tick(20);
	TestFalse(TEXT("Closing message also remains until acknowledgement"),Mode->bResultVisible);
	Mode->ContinueTutorial(); Mode->Tick(1.2f);
	TestTrue(TEXT("After closing acknowledgement and blackout the tutorial result appears"),Mode->bResultVisible);
	TestFalse(TEXT("A repeated exit cannot finish twice"),Mode->SignalEvent(PC,TEXT("Tutorial.Escape")));
	TestTrue(TEXT("Mannequin exercise ends by capture of one practice survivor"),Mode->Plan->MannequinSteps.Last().Condition==EProjectTutorialCondition::Capture && Mode->Plan->MannequinSteps.Last().RequiredAmount==1);
	TestEqual(TEXT("Practical replay button wording"),CastChecked<UTextBlock>(UI->RestartButton->GetContent())->GetText().ToString(),FString(TEXT("실전 연습")));
	TestEqual(TEXT("Full tutorial replay has a separate button"),CastChecked<UTextBlock>(UI->FullRestartButton->GetContent())->GetText().ToString(),FString(TEXT("튜토리얼 처음부터 다시하기")));
	// Role separation, authored line breaks and deliberate practice requirements.
	TestEqual(TEXT("Survivor has five independent pages and no ping/voice exercise"),Mode->Plan->SurvivorSteps.Num(),5);
	TestEqual(TEXT("Mannequin has its own seven pages"),Mode->Plan->MannequinSteps.Num(),7);
	TestFalse(TEXT("Command tutorial no longer requires manual control first"),Mode->Plan->MannequinSteps[4].Instructions.ToString().Contains(TEXT("직접 조작 상태에서")));
	TestEqual(TEXT("Commanded handoff requires three repetitions"),Mode->Plan->MannequinSteps[5].RequiredAmount,3.f);
	TestTrue(TEXT("Capture instructions retain the damage exception with readable line breaks"),Mode->Plan->MannequinSteps.Last().Instructions.ToString().Contains(TEXT("비활성화 시켜도 언제든 소모시킬 수 있습니다.)")));
	TestEqual(TEXT("Survivor must continuously observe for three seconds"),Mode->Plan->SurvivorSteps[3].RequiredAmount,3.f);
	TestEqual(TEXT("Default survivor walk goal is ten metres"),Mode->Plan->SurvivorSteps[1].RequiredAmount,1000.f);
	TestEqual(TEXT("Default survivor sprint goal is twenty metres"),Mode->Plan->SurvivorSteps[2].RequiredAmount,2000.f);
	TestEqual(TEXT("Default mannequin movement goal is ten metres"),Mode->Plan->MannequinSteps[3].RequiredAmount,1000.f);
	TestFalse(TEXT("Removed command explanation is not in native defaults"),Mode->Plan->MannequinSteps[4].Instructions.ToString().Contains(TEXT("직접 조작하지 않아도")));
	Mode->bFinishing=false; Mode->bResultVisible=false; Mode->bClosingConfirmed=false;
	Mode->StepIndex=2; Mode->EnterStep(); UI->NativeTick(FGeometry(),0);
	TestEqual(TEXT("Survivor page has only the survivor heading"),UI->Heading->GetText().ToString(),FString(TEXT("생존자 튜토리얼")));
	TestTrue(TEXT("Sprint explanation retains a blank line after its opening sentence"),UI->Body->GetText().ToString().Contains(TEXT("달릴 수 있습니다.\n\n달릴 때 스태미나가 소모되며")));
	TestFalse(TEXT("Author's practice notes never become visible instructions"),UI->Body->GetText().ToString().Contains(TEXT("목표 갱신")));
	Mode->ContinueTutorial();
	Mode->Steps[Mode->StepIndex].MinimumPracticeSeconds=99; // Serialized legacy values must not retain a hidden wait.
	Mode->Progress=Mode->GetCurrentStep()->RequiredAmount-1;
	TestFalse(TEXT("Movement cannot complete below the exact distance goal"),Mode->TryFinishPracticeStep());
	TestFalse(TEXT("UI does not round an unfinished distance up to its completed target"),Mode->GetPracticeStatusText().ToString().Contains(TEXT("20.0 / 20.0")));
	Mode->Progress=Mode->GetCurrentStep()->RequiredAmount;
	TestFalse(TEXT("No vague extra practice prompt remains"),Mode->GetPracticeStatusText().ToString().Contains(TEXT("조금 더")));
	TestTrue(TEXT("Visible distance goal completes immediately despite legacy hidden timer"),Mode->TryFinishPracticeStep());
	TestTrue(TEXT("Sprint leads to observation, never to mannequin controls"),Mode->GetCurrentStep()->Condition==EProjectTutorialCondition::ObserveMannequin);

	Mode->bMannequin=true; Mode->Steps=Mode->Plan->MannequinSteps;
	Mode->StepIndex=0; Mode->EnterStep(); UI->NativeTick(FGeometry(),0);
	TestEqual(TEXT("Mannequin page has only the mannequin heading"),UI->Heading->GetText().ToString(),FString(TEXT("마네킹 튜토리얼")));
	TestTrue(TEXT("Intro preserves authored internal line breaks"),UI->Body->GetText().ToString().Contains(TEXT("생존자가 바라보는 마네킹은 멈추게 되며\n이들이 바라보지 않는 시야에서")));
	auto* First=World->SpawnActor<AMannequinAICharacter>();
	auto* Second=World->SpawnActor<AMannequinAICharacter>();
	PC->UnPossess(); PC->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::NotApplicable);
	PC->SetViewedMannequin(First);
	Mode->StepIndex=1; Mode->EnterStep(); Mode->ContinueTutorial();
	Mode->RecordAction(PC,EProjectTutorialCondition::SelectMannequin);
	TestEqual(TEXT("Pressing the already selected number does not count"),Mode->Progress,0.f);
	for (auto* Next:{Second,First}) { PC->SetViewedMannequin(Next); Mode->RecordAction(PC,EProjectTutorialCondition::SelectMannequin); }
	TestEqual(TEXT("Two real viewpoint changes are not yet three"),Mode->StepIndex,1);
	PC->SetViewedMannequin(Second); Mode->RecordAction(PC,EProjectTutorialCondition::SelectMannequin);
	TestEqual(TEXT("Three viewpoint changes reach manual-control instructions"),Mode->StepIndex,2);
	Mode->StepIndex=4; Mode->EnterStep();
	PC->Possess(Second); Second->SetManualControlEnabled(true); PC->SetViewedMannequin(Second);
	Mode->RecordAction(PC,EProjectTutorialCondition::PatrolCommand);
	TestEqual(TEXT("Commands during the explanation do not count"),Mode->Progress,0.f);
	Mode->ContinueTutorial();
	// The exercise also accepts commands issued while only sharing a body's view.
	PC->UnPossess();Second->SetManualControlEnabled(false);Second->SpawnDefaultController();
	Mode->RecordAction(PC,EProjectTutorialCondition::PatrolCommand);
	Mode->RecordAction(PC,EProjectTutorialCondition::PatrolCommand);
	TestEqual(TEXT("Each successful command counts immediately, including repeats"),Mode->Progress,2.f);
	UI->NativeTick(FGeometry(),.1f);
	TestTrue(TEXT("Goal text is below the role HUD, not at its old overlapping position"),CastChecked<UCanvasPanelSlot>(UI->ObjectiveStack->GetParent()->Slot)->GetPosition().Y>PC->GetMannequinStatusBottom());
	TestTrue(TEXT("The active goal stack is visible during practice"),UI->ObjectiveStack->GetVisibility()==ESlateVisibility::HitTestInvisible);
	for (auto Action:{EProjectTutorialCondition::ResetFollow,EProjectTutorialCondition::ToggleContactDamage,EProjectTutorialCondition::DirectAttack}) Mode->RecordAction(PC,Action);
	Mode->Tick(.1f);
	TestEqual(TEXT("Q/B/attack are optional, not required command checks"),Mode->Progress,2.f);
	Mode->RecordAction(PC,EProjectTutorialCondition::ChaseCommand);
	TestEqual(TEXT("Third valid Z/X/C command reaches the next page without an extra timer"),Mode->StepIndex,5);
	Mode->ContinueTutorial();
	PC->UnPossess(); Second->SetManualControlEnabled(false); PC->SetViewedMannequin(First);
	Mode->RecordAction(PC,EProjectTutorialCondition::SelectMannequin);
	TestFalse(TEXT("Switching without a preceding command cannot satisfy a handoff"),Mode->ReleasedMannequin.IsValid());
	// Three commanded releases must really return to AI, without a hidden two-second wait.
	Mode->Steps.Last().Condition=EProjectTutorialCondition::Continue; // No final NPC spawn in this isolated fixture.
	for (int32 Cycle=0;Cycle<3;++Cycle)
	{
		PC->UnPossess(); Second->SetManualControlEnabled(false); PC->SetViewedMannequin(Second);
		Mode->RecordAction(PC,EProjectTutorialCondition::SelectMannequin);
		Mode->RecordAction(PC,EProjectTutorialCondition::PatrolCommand);
		PC->UnPossess(); Second->SetManualControlEnabled(false); Second->SpawnDefaultController(); PC->SetViewedMannequin(First);
		Mode->RecordAction(PC,EProjectTutorialCondition::SelectMannequin);
		TestTrue(TEXT("Commanded body is tracked after another number is selected"),Mode->ReleasedMannequin==Second);
		TestNotNull(TEXT("The released mannequin has a real AI controller"),Cast<AAIController>(Second->GetController()));
		Mode->Tick(.016f);
		TestEqual(TEXT("Each completed handoff counts once; three are required"),Mode->StepIndex,Cycle<2 ? 5 : 6);
	}

	auto* Objective=PC->FindComponentByClass<UProjectProject01ObjectiveComponent>();
	if (!TestNotNull(TEXT("Multiplayer controller owns objective replication"),Objective)) return false;
	Objective->UpdateObjective(TEXT("First"),FText::FromString(TEXT("첫 목표")),FText());
	auto* Trigger=World->SpawnActor<AProjectProject01ObjectiveTrigger>();
	Trigger->RequiredObjective=TEXT("Other"); Trigger->NewObjectiveId=TEXT("Second"); Trigger->Title=FText::FromString(TEXT("다음 목표"));
	TestFalse(TEXT("An unmet prerequisite does not consume the trigger"),Trigger->ApplyTo(PC));
	Trigger->RequiredObjective=TEXT("First");
	TestTrue(TEXT("Editable goal trigger changes the current objective"),Trigger->ApplyTo(PC));
	TestFalse(TEXT("Repeated overlap does not duplicate its notice"),Trigger->ApplyTo(PC));
	TestTrue(TEXT("The objective carries the author-provided identifier"),Objective->GetObjective().Id==TEXT("Second"));
	auto* Complete=World->SpawnActor<AProjectProject01ObjectiveTrigger>(); Complete->bCompleteCurrentObjective=true; Complete->RequiredObjective=TEXT("Second");
	TestTrue(TEXT("A separate condition trigger marks the current goal complete"),Complete->ApplyTo(PC));
	TestTrue(TEXT("Goal completion is stored, not just a transient UI string"),Objective->GetObjective().bCompleted);
	FString Error; Mode->PreLogin(TEXT(""),TEXT("127.0.0.1"),FUniqueNetIdRepl(),Error);
	TestFalse(TEXT("Tutorial never accepts network clients"),Error.IsEmpty());
	// Render every native tutorial page for text wrapping/alignment review.
	const FString Directory=FPaths::ProjectSavedDir()/TEXT("ReviewRemediation/TutorialReadability-20261021");
	IFileManager::Get().MakeDirectory(*Directory,true);
	FWidgetRenderer Renderer(true);const FVector2D Size(1280,720);
	for(bool MannequinRole:{false,true})
	{
		Mode->bMannequin=MannequinRole;Mode->bFinishing=false;Mode->bResultVisible=false;
		Mode->Steps=MannequinRole?Mode->Plan->MannequinSteps:Mode->Plan->SurvivorSteps;
		for(int32 Page=0;Page<Mode->Steps.Num();++Page)
		{
			if(Mode->Steps[Page].Condition==EProjectTutorialCondition::Capture)Mode->Steps[Page].Condition=EProjectTutorialCondition::Continue;
			Mode->StepIndex=Page;Mode->EnterStep();UI->NativeTick(FGeometry(),0);
			TestEqual(TEXT("All explanation paragraphs use a consistent readable width"),UI->Body->GetWrapTextAt(),620.f);
			TestFalse(TEXT("No native tutorial page displays the removed sentence"),UI->Body->GetText().ToString().Contains(TEXT("직접 조작하지 않아도")));
			// Under -nullrhi the render target has no RHI resource and FWidgetRenderer reads null.
			// Keep every text/flow assertion above; skip only the screenshot capture.
			if (!FApp::CanEverRender()) continue;
			const auto Slate=UI->WidgetTree->RootWidget->TakeWidget();
			auto* Target=Renderer.DrawWidget(Slate,Size);Renderer.DrawWidget(Target,Slate,Size,.016f);FlushRenderingCommands();FBufferArchive Image;
			TestTrue(TEXT("Tutorial page rendered"),FImageUtils::ExportRenderTarget2DAsPNG(Target,Image));
			TestTrue(TEXT("Tutorial page screenshot saved"),FFileHelper::SaveArrayToFile(Image,*(Directory/FString::Printf(TEXT("%s-%02d.png"),MannequinRole?TEXT("Mannequin"):TEXT("Survivor"),Page))));
		}
	}
	Mode->StepIndex=4;Mode->EnterStep();
	Mode->Steps[4].Instructions=FText::FromString(TEXT("시점을 선택한 마네킹에게 명령을 내릴 수 있습니다.\n직접 조작하지 않아도 명령할 수 있으며, 직접 조작 중 내린 명령은 조작 해제 후 수행합니다.\nZ: 정찰"));
	UI->NativeTick(FGeometry(),0);
	TestFalse(TEXT("Old serialized instruction also removes the requested sentence"),UI->Body->GetText().ToString().Contains(TEXT("직접 조작하지 않아도")));
	TestTrue(TEXT("Removing the legacy sentence preserves its neighbouring instructions"),UI->Body->GetText().ToString().Contains(TEXT("Z: 정찰")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSpectatorFadeTest,"ProjectProject01.Multiplayer.SpectatorFadeOrdering",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectSpectatorFadeTest::RunTest(const FString&)
{
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	ON_SCOPE_EXIT { if (World) World->DestroyWorld(false); };
	World->SetGameInstance(NewObject<UGameInstance>());
	auto* PC=World->SpawnActor<AMultiplayTestPlayerController>(); PC->SetAsLocalPlayerController();
	PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>(); PC->PlayerCameraManager->InitializeFor(PC);
	auto* Before=World->SpawnActor<ACameraActor>(); auto* Target=World->SpawnActor<APlayerCharacter>(); PC->SetViewTarget(Before);
	PC->EnterSurvivorSpectator(Target,true);
	// This isolated world has no owning network Player; deliver the queued owner RPC explicitly.
	PC->ClientEnterSurvivorSpectator_Implementation(PC->SurvivorSpectatorView);
	TestTrue(TEXT("A spectator request fades out before exposing its target"),PC->bSpectatorFadePending && PC->GetViewTarget()==Before);
	TestTrue(TEXT("Engine view RPCs cannot override the local spectator handoff"),PC->PlayerCameraManager->bClientSimulatingViewTarget);
	PC->ClientSetViewTarget_Implementation(Target,FViewTargetTransitionParams());
	TestTrue(TEXT("An implicit server view RPC does not show the spectator target before black"),PC->GetViewTarget()==Before);
	const double Deadline=PC->SpectatorSwitchReadyAt;
	PC->OnRep_SurvivorSpectatorView(); PC->ApplySurvivorSpectatorView();
	TestTrue(TEXT("Property+RPC replay cannot restart the fade or reveal early"),PC->SpectatorSwitchReadyAt==Deadline && PC->GetViewTarget()==Before);
	PC->SpectatorSwitchReadyAt=-1; PC->TryFinishSpectatorFade();
	TestTrue(TEXT("At black, view switches once before fading in"),!PC->bSpectatorFadePending && PC->GetViewTarget()==Target);
	PC->SurvivorSpectatorView={};
	PC->EnterSurvivorSpectator(Target,true);
	PC->ClientEnterSurvivorSpectator_Implementation(PC->SurvivorSpectatorView);
	TestTrue(TEXT("A later request after resetting state still gets a new fade"),PC->bSpectatorFadePending && PC->SurvivorSpectatorView.RequestId>1);
	PC->PrepareLevelTransitionCarryover(false,1.5f);
	PC->ClientPrepareForLevelTransition_Implementation(1.5f,PC->TransitionFadeSerial);
	TestTrue(TEXT("Travel blackout supersedes an in-progress observer handoff"),PC->bLocalLevelTransitionFadeHeld && !PC->bSpectatorFadePending);
	TestFalse(TEXT("Server does not equate its delay with a completed remote blackout"),PC->IsLevelTransitionBlackReady());
	PC->ServerConfirmLevelTransitionBlack_Implementation(PC->TransitionFadeSerial-1);
	TestFalse(TEXT("A stale acknowledgement cannot unlock this trip"),PC->IsLevelTransitionBlackReady());
	PC->ServerConfirmLevelTransitionBlack_Implementation(PC->TransitionFadeSerial);
	TestTrue(TEXT("The matching owner acknowledgement unlocks this trip"),PC->IsLevelTransitionBlackReady());
	PC->CancelLevelTransitionCarryover(true);
	TestFalse(TEXT("Rollback revokes the previous acknowledgement"),PC->IsLevelTransitionBlackReady());
	auto* Mode=World->SpawnActor<AMultiplayTestGameMode>();
	for (const FString Role:{FString(TEXT("Survivor")),FString(TEXT("Mannequin"))})
	{
		auto* Other=World->SpawnActor<AMultiplayTestPlayerController>();
		FMultiplayTestServerPlayerRecord Record; Record.Role=Role;
		Mode->MatchPlayerRecords.Add(Other,Record); Other->PrepareLevelTransitionCarryover(false);
	}
	FMultiplayTestServerPlayerRecord ObserverRecord; ObserverRecord.Role=TEXT("Survivor");
	ObserverRecord.SurvivorState=EMultiplayTestSurvivorState::Eliminated;
	Mode->MatchPlayerRecords.Add(PC,ObserverRecord); PC->PrepareLevelTransitionCarryover(false);
	for (const auto& Pair:Mode->MatchPlayerRecords)
		if (Pair.Key.Get()!=PC) Pair.Key->ServerConfirmLevelTransitionBlack_Implementation(Pair.Key->TransitionFadeSerial);
	TestFalse(TEXT("Dead observers participate in the barrier too"),Mode->AreLevelTransitionClientsBlack());
	PC->ServerConfirmLevelTransitionBlack_Implementation(PC->TransitionFadeSerial);
	TestTrue(TEXT("Travel is allowed only after survivor, mannequin AND observer are black"),Mode->AreLevelTransitionClientsBlack());
	for (const auto& Pair:Mode->MatchPlayerRecords)
		if (Pair.Value.Role==TEXT("Mannequin")) Pair.Key->bServerTransitionBlackReady=false;
	TestFalse(TEXT("An unready mannequin blocks early travel just like a survivor"),Mode->AreLevelTransitionClientsBlack());

	auto* GI=NewObject<UProjectProject01GameInstance>(); World->SetGameInstance(GI);
	GI->ClearEntireScreenFade(); GI->FadeEntireScreenToBlack(0.6f); GI->TickEntireScreenFade(10);
	TestFalse(TEXT("A loading hitch cannot skip the complete fade-out"),GI->IsEntireScreenBlack());
	for (int32 Frame=0;Frame<13;++Frame) GI->TickEntireScreenFade(0.05f);
	TestTrue(TEXT("Normal frames finish at solid black"),GI->IsEntireScreenBlack());
	GI->FadeEntireScreenFromBlack(0.6f); GI->TickEntireScreenFade(10);
	TestTrue(TEXT("A loading hitch cannot skip the complete reveal either"),GI->EntireScreenFadeOpacity>0.9f);
	PC->ClientPrepareForLevelTransition_Implementation(0.6f,PC->TransitionFadeSerial+1);
	GI->TickEntireScreenFade(0.05f);
	const float Opacity=GI->EntireScreenFadeOpacity;
	PC->ClientPrepareForLevelTransition_Implementation(0.6f,PC->TransitionFadeSerial+1);
	TestEqual(TEXT("Repeated preparation cannot rewind the current fade"),GI->EntireScreenFadeOpacity,Opacity);
	PC->TryConfirmLevelTransitionBlack();
	TestFalse(TEXT("The owner cannot confirm a partially black screen"),PC->bLocalTransitionBlackConfirmed);
	GI->HoldEntireScreenBlack(); PC->TryConfirmLevelTransitionBlack();
	TestFalse(TEXT("The first fully black frame is held before confirmation"),PC->bLocalTransitionBlackConfirmed);
	PC->LocalTransitionBlackSince=FPlatformTime::Seconds()-0.1;
	PC->TryConfirmLevelTransitionBlack();
	TestTrue(TEXT("Only a held fully black frame confirms the owner blackout"),PC->bLocalTransitionBlackConfirmed);
	PC->ClientCompleteLevelTransition_Implementation(Target,false);
	TestTrue(TEXT("An old-world target cannot reveal the screen before the new map"),PC->bClientArrivalViewPending && PC->bLocalLevelTransitionFadeHeld);
	GI->ClearEntireScreenFade();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectTutorialRuntimeTest,"ProjectProject01.TutorialRuntime.Startup",
	EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FProjectTutorialRuntimeTest::RunTest(const FString&)
{
	AProjectProject01TutorialGameMode* Mode=nullptr;
	for (const auto& Context:GEngine->GetWorldContexts())
		if (auto* World=Context.World(); World && World->IsGameWorld())
			if (auto* Candidate=World->GetAuthGameMode<AProjectProject01TutorialGameMode>()) { Mode=Candidate; break; }
	if (!TestNotNull(TEXT("Run this test with MultiplayerTutorial as a standalone game"),Mode)) return false;
	TestTrue(TEXT("Selected role initializes a local practice session"),Mode->bInitialized && Mode->Learner && Mode->Widget);
	if (!Mode->Learner || !Mode->Widget) return false;
	if (Mode->IsMannequinTutorial())
	{
		TestEqual(TEXT("Saved map uses the ten-metre mannequin goal"),Mode->Steps[3].RequiredAmount,1000.f);
		TestFalse(TEXT("Saved map no longer shows the removed manual/release sentence"),Mode->Steps[4].Instructions.ToString().Contains(TEXT("직접 조작하지 않아도")));
		TestEqual(TEXT("The saved map uses the three-command practice requirement"),Mode->Steps[4].RequiredAmount,3.f);
		TestEqual(TEXT("The saved map also requires three commanded handoffs"),Mode->Steps[5].RequiredAmount,3.f);
	}
	if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(Mode->GetWorld()))
	{
		FNavLocation Point;
		TestTrue(TEXT("The saved training map has usable walkable navigation"),Nav->ProjectPointToNavigation(FVector(0,0,10),Point,FVector(150,150,300)));
	}
	else AddError(TEXT("Training map has no navigation system"));
	TestTrue(TEXT("Intro is still awaiting input after map startup"),Mode->bAwaitingRead && Mode->StepIndex==0);
	TestTrue(TEXT("Practice role is reflected in its controller state"),Mode->IsMannequinTutorial()
		? Mode->Learner->GetSurvivorState()==EMultiplayTestSurvivorState::NotApplicable && IsValid(Mode->Learner->GetViewedMannequin())
		: Mode->Learner->GetSurvivorState()==EMultiplayTestSurvivorState::Active && IsValid(Mode->Learner->GetPawn()));
	const auto Slate=Mode->Widget->WidgetTree->RootWidget->TakeWidget(); FWidgetRenderer Renderer(true); const FVector2D Size(1280,720);
	auto* Target=Renderer.DrawWidget(Slate,Size); Renderer.DrawWidget(Target,Slate,Size,0.016f); FlushRenderingCommands(); FBufferArchive Image;
	const FString Directory=FPaths::ProjectSavedDir()/TEXT("ReviewRemediation/TutorialUI"); IFileManager::Get().MakeDirectory(*Directory,true);
	TestTrue(TEXT("Centered tutorial instructions render"),FImageUtils::ExportRenderTarget2DAsPNG(Target,Image));
	TestTrue(TEXT("Instruction screenshot saved"),FFileHelper::SaveArrayToFile(Image,*(Directory/(Mode->IsMannequinTutorial() ? TEXT("Mannequin.png") : TEXT("Survivor.png")))));
	// Exercise the real map's final condition, not a mock win/escape result.
	Mode->StepIndex=Mode->Steps.Num()-1; Mode->EnterStep(); Mode->ContinueTutorial();
	if (Mode->IsMannequinTutorial())
	{
		TestTrue(TEXT("Manual control is available with the real mannequin actor"),Mode->TryEnableMannequinManualControl(Mode->Learner));
		auto* Controlled=Cast<AMannequinAICharacter>(Mode->Learner->GetPawn()); APlayerCharacter* Victim=nullptr;
		for (TActorIterator<APlayerCharacter> It(Mode->GetWorld()); It; ++It) { Victim=*It; break; }
		if (!TestNotNull(TEXT("Exactly one practice survivor is spawned for the final exercise"),Victim) || !Controlled) return false;
		Controlled->SetActorLocationAndRotation(Victim->GetActorLocation()+FVector(-450,0,0),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
		const TWeakObjectPtr<AProjectProject01TutorialGameMode> Exercise(Mode);
		const TWeakObjectPtr<APlayerCharacter> TargetSurvivor(Victim);
		const TWeakObjectPtr<AMannequinAICharacter> Body(Controlled);
		const FVector Start=Victim->GetActorLocation();
		const float StartTime=Mode->GetWorld()->GetTimeSeconds();
		const double ExerciseDeadline=FPlatformTime::Seconds()+40;
		const TSharedRef<float> MaxDistance=MakeShared<float>(0);
		const TSharedRef<bool> SawWatching=MakeShared<bool>(false), SawLookingAway=MakeShared<bool>(false);
		struct FMovementScenario { int32 Stage=0; float Since=0; FVector Origin,Previous; float Distance=0,LateTravel=0; bool bHUDShot=false; };
		const auto MovementScenario=MakeShared<FMovementScenario>();MovementScenario->Since=StartTime;MovementScenario->Origin=Start;MovementScenario->Previous=Start;
		const FString HUDShot=Directory/TEXT("TutorialHUD-Mannequin.png");
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Exercise,TargetSurvivor,Body,Start,StartTime,ExerciseDeadline,MaxDistance,SawWatching,SawLookingAway,MovementScenario,HUDShot]()
		{
			auto* Current=Exercise.Get(); auto* Survivor=TargetSurvivor.Get(); auto* Mannequin=Body.Get();
			if (!Current || !Survivor || !Mannequin) { AddError(TEXT("Practice actors disappeared before target movement was checked")); return true; }
			*MaxDistance=FMath::Max(*MaxDistance,FVector::Dist2D(Start,Survivor->GetActorLocation()));
			const auto* Threat=Current->FindPracticeThreat(Current->PracticeBots[0]);
			const FVector ToMannequin=((Threat ? Threat->GetActorLocation() : Mannequin->GetActorLocation())-Survivor->GetActorLocation()).GetSafeNormal2D();
			const float Dot=FVector::DotProduct(Survivor->GetActorForwardVector(),ToMannequin);
			*SawWatching|=Dot>.5f; *SawLookingAway|=Dot<0;
			const float Now=Current->GetWorld()->GetTimeSeconds();
			if (!MovementScenario->bHUDShot && Now-StartTime>1)
			{ MovementScenario->bHUDShot=true; FScreenshotRequest::RequestScreenshot(HUDShot,true,false); }
			MovementScenario->Distance=FMath::Max(MovementScenario->Distance,FVector::Dist2D(MovementScenario->Origin,Survivor->GetActorLocation()));
			if (Now-MovementScenario->Since>2.5f) MovementScenario->LateTravel+=FVector::Dist2D(MovementScenario->Previous,Survivor->GetActorLocation());
			MovementScenario->Previous=Survivor->GetActorLocation();
			if (Now-MovementScenario->Since<5 && FPlatformTime::Seconds()<ExerciseDeadline) return false;
			TestTrue(FString::Printf(TEXT("Practice survivor keeps moving in scenario %d (open/corner/live obstacle)"),MovementScenario->Stage),MovementScenario->Distance>100);
			TestTrue(FString::Printf(TEXT("Practice survivor is not only moving at scenario %d startup"),MovementScenario->Stage),MovementScenario->LateTravel>100);
			if (MovementScenario->Stage<2 && FPlatformTime::Seconds()<ExerciseDeadline)
			{
				++MovementScenario->Stage;
				if (MovementScenario->Stage==1)
				{
					Survivor->SetActorLocation(FVector(1750,1120,Survivor->GetActorLocation().Z),false,nullptr,ETeleportType::TeleportPhysics);
					Mannequin->SetActorLocation(FVector(1250,800,Survivor->GetActorLocation().Z),false,nullptr,ETeleportType::TeleportPhysics);
					Current->PracticeBots[0].Path.Reset();Current->PracticeBots[0].RepathSeconds=0;
				}
				else
				{
					// A live blocking object absent from NavMesh must not pin the bot.
					Survivor->SetActorLocation(FVector(600,0,Survivor->GetActorLocation().Z),false,nullptr,ETeleportType::TeleportPhysics);
					Mannequin->SetActorLocation(FVector(100,0,Survivor->GetActorLocation().Z),false,nullptr,ETeleportType::TeleportPhysics);
					auto* Blocker=Current->GetWorld()->SpawnActor<AActor>();
					auto* Box=NewObject<UBoxComponent>(Blocker);Blocker->SetRootComponent(Box);Box->SetBoxExtent(FVector(80,80,100));
					Box->SetCollisionProfileName(TEXT("BlockAll"));Box->SetCanEverAffectNavigation(false);Box->RegisterComponent();
					Blocker->SetActorLocation(Survivor->GetActorLocation()+FVector(170,0,0));
					Current->PracticeBots[0].Path={Survivor->GetActorLocation(),Survivor->GetActorLocation()+FVector(700,0,0)};
					Current->PracticeBots[0].PathIndex=1;Current->PracticeBots[0].RepathSeconds=10;
				}
				Current->PracticeBots[0].LastLocation=Survivor->GetActorLocation();Current->PracticeBots[0].StuckSeconds=0;Current->PracticeBots[0].AvoidDirection=FVector::ZeroVector;
				MovementScenario->Since=Now;MovementScenario->Origin=Survivor->GetActorLocation();MovementScenario->Previous=Survivor->GetActorLocation();MovementScenario->Distance=0;MovementScenario->LateTravel=0;return false;
			}
			TestTrue(TEXT("Practice survivor actually moves along walkable paths"),*MaxDistance>100);
			TestTrue(TEXT("Practice survivor alternates watching the mannequin and looking away"),*SawWatching && *SawLookingAway);
			TestTrue(TEXT("Practice movement never auto-finishes the capture exercise"),!Current->bFinishing);
			const FVector Behind=Survivor->GetActorLocation()-Survivor->GetActorForwardVector()*110;
			Mannequin->SetActorLocationAndRotation(Behind,(Survivor->GetActorLocation()-Behind).Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
			TestTrue(TEXT("The actual authoritative melee attack catches the practice survivor"),Current->TryMannequinAttack(Current->Learner));
			return true;
		}));
	}
	else
	{
		for (TActorIterator<AProjectProject01ObjectiveTrigger> It(Mode->GetWorld()); It; ++It)
			if (It->TutorialEvent==TEXT("Tutorial.Escape"))
			{ Mode->Learner->GetPawn()->SetActorLocation(It->GetActorLocation(),false,nullptr,ETeleportType::TeleportPhysics); break; }
	}
	const TWeakObjectPtr<AProjectProject01TutorialGameMode> WeakMode(Mode);
	const TWeakObjectPtr<UProjectProject01GameInstance> WeakInstance(Mode->GetGameInstance<UProjectProject01GameInstance>());
	const TWeakObjectPtr<UWorld> PracticeWorld(Mode->GetWorld());
	const FString ReturnShot=Directory/(Mode->IsMannequinTutorial() ? TEXT("Returned-Mannequin.png") : TEXT("Returned-Survivor.png"));
	const double Deadline=FPlatformTime::Seconds()+60;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,WeakMode,Deadline]()
	{
		auto* Current=WeakMode.Get();
		if (!Current) { AddError(TEXT("Tutorial world was lost before its local result")); return true; }
		if (!Current->bFinishing && FPlatformTime::Seconds()<Deadline) return false;
		TestTrue(TEXT("Real exit/capture finishes practice without changing maps"),Current->bFinishing);
		Current->ContinueTutorial(); return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,WeakMode,Deadline]()
	{
		auto* Current=WeakMode.Get();
		if (Current && !Current->bResultVisible && FPlatformTime::Seconds()<Deadline+3) return false;
		TestTrue(TEXT("Closing acknowledgement fades into the local tutorial result"),Current && Current->bResultVisible);
		if (Current) Current->ReturnToMatching(); return true;
	}));
	const TSharedRef<double> FrontReadyAt=MakeShared<double>(0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,WeakInstance,PracticeWorld,Deadline,FrontReadyAt,ReturnShot]()
	{
		auto* Instance=WeakInstance.Get(); auto* FrontWorld=Instance ? Instance->GetWorld() : nullptr;
		const bool Ready=IsValid(FrontWorld) && FrontWorld!=PracticeWorld.Get() && FrontWorld->HasBegunPlay();
		if (FPlatformTime::Seconds()>Deadline+10) { AddError(TEXT("Tutorial return did not reach a ready front end")); return true; }
		if (!Ready) return false;
		if (*FrontReadyAt==0) *FrontReadyAt=FPlatformTime::Seconds();
		if (FPlatformTime::Seconds()-*FrontReadyAt<1.0) return false;
		const FString Package=FPackageName::GetShortName(FrontWorld->GetOutermost()->GetName());
		TestTrue(TEXT("Local practice safely returns to the front end (login fallback without an account)"),Package==TEXT("LoginLevel") || Package==TEXT("LobbyLevel"));
		FScreenshotRequest::RequestScreenshot(ReturnShot,true,false); return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,ReturnShot,Deadline]()
	{
		if (IFileManager::Get().FileSize(*ReturnShot)>0) return true;
		if (FPlatformTime::Seconds()<Deadline+12) return false;
		AddError(TEXT("Returned front-end screenshot was not captured")); return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPracticeDirectorTest,"ProjectProject01.Multiplayer.PracticeDirectorCommands",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectPracticeDirectorTest::RunTest(const FString&)
{
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	ON_SCOPE_EXIT { if(World)World->DestroyWorld(false); };
	if(!TestNotNull(TEXT("Isolated director world"),World))return false;
	World->SetGameInstance(NewObject<UGameInstance>());FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.ProjectProject01TutorialGameMode"));
	if(!TestTrue(TEXT("Local multiplayer-derived game mode"),World->SetGameMode(URL)))return false;
	auto* Mode=World->GetAuthGameMode<AProjectProject01TutorialGameMode>();
	Mode->bFreePractice=true;Mode->bPracticeDirector=true;Mode->bMannequin=false;Mode->PracticeRandom.Initialize(21941);
	TArray<AMannequinAICharacter*> Bodies;
	for(int32 I=0;I<3;++I)
	{
		auto* Body=World->SpawnActor<AMannequinAICharacter>(FVector(500+I*250,300,100),FRotator::ZeroRotator);
		auto* AI=World->SpawnActor<AAIController>();AI->Possess(Body);Bodies.Add(Body);
	}
	auto* A=Bodies[0];auto* B=Bodies[1];auto* C=Bodies[2];
	Mode->SelectPracticeDirectorBody(A);
	TestTrue(TEXT("Number selection changes focus without starting manual control"),Mode->GetMannequinPlayerFocusPawn()==A && !A->IsManualControlEnabled());
	TestTrue(TEXT("View-only state can issue commands"),Mode->IssuePracticeDirectorCommand(EMultiplayMannequinCommand::FollowMannequinPlayer));
	A->SetManualControlEnabled(true);
	TestTrue(TEXT("C is also accepted on the controlled body"),Mode->IssuePracticeDirectorCommand(EMultiplayMannequinCommand::FollowMannequinPlayer));
	Mode->SelectPracticeDirectorBody(B);
	TestTrue(TEXT("C persists when the simulated player leaves its body"),!A->IsManualControlEnabled() && A->GetMultiplayCommand()==EMultiplayMannequinCommand::FollowMannequinPlayer);
	TestEqual(TEXT("The native follower leader changes with the player's selected body"),Mode->GetMannequinPlayerFocusPawn(),B);
	Mode->IssuePracticeDirectorCommand(EMultiplayMannequinCommand::ChaseInDetectionRange);
	TestTrue(TEXT("Q actually resets existing followers"),Mode->ResetPracticeDirectorFollowers());
	TestTrue(TEXT("Q restores patrol without overwriting unrelated chase commands"),A->GetMultiplayCommand()==EMultiplayMannequinCommand::Patrol && B->GetMultiplayCommand()==EMultiplayMannequinCommand::ChaseInDetectionRange);
	TestFalse(TEXT("Choosing no further command cannot erase an already issued X"),Mode->IssuePracticeDirectorCommand(EMultiplayMannequinCommand::HoldPosition));
	B->SetManualControlEnabled(true);
	Mode->SelectPracticeDirectorBody(C);
	TestEqual(TEXT("X persists after handoff"),B->GetMultiplayCommand(),EMultiplayMannequinCommand::ChaseInDetectionRange);
	C->SetManualControlEnabled(true);Mode->IssuePracticeDirectorCommand(EMultiplayMannequinCommand::HoldPosition);Mode->SelectPracticeDirectorBody(A);
	TestEqual(TEXT("No Z/X/C before handoff produces hold"),C->GetMultiplayCommand(),EMultiplayMannequinCommand::HoldPosition);
	TestTrue(TEXT("B can turn global contact damage off"),Mode->SetPracticeDirectorContactDamage(false));
	for(auto* Body:Bodies)TestFalse(TEXT("B applies to every mannequin"),Body->IsCatchDamageEnabled());
	TestTrue(TEXT("Survivor status shows OFF without the removed direct-control explanation"),Mode->GetPracticeContactDamageText().ToString().Contains(TEXT("비활성화")) && !Mode->GetPracticeContactDamageText().ToString().Contains(TEXT("직접 조작")));
	Mode->bPracticeDirectorToggleContact=false;
	TestFalse(TEXT("A disabled permission blocks the AI's B action"),Mode->SetPracticeDirectorContactDamage(true));
	TestFalse(TEXT("Blocking B does not silently change the current damage mode"),Mode->IsPracticeContactDamageEnabled());
	Mode->bPracticeDirectorToggleContact=true;TestTrue(TEXT("B can enable global contact damage again"),Mode->SetPracticeDirectorContactDamage(true));
	for(auto* Body:Bodies)TestTrue(TEXT("B enables every mannequin"),Body->IsCatchDamageEnabled());
	Mode->Learner=World->SpawnActor<AMultiplayTestPlayerController>();auto* Target=World->SpawnActor<APlayerCharacter>();Mode->Learner->Possess(Target);
	Mode->NextDirectorAttack=10000;Mode->NextDirectorMove=10000;
	TSet<EMultiplayMannequinCommand> Commands;TSet<int32> Intervals;TSet<AMannequinAICharacter*> Selected;
	bool SawB=false,SawQ=false,SawUncommanded=false;
	for(int32 Sample=0;Sample<300;++Sample)
	{
		Mode->NextDirectorDecision=0;Mode->TickPracticeMannequinDirector(.01f);
		Mode->NextDirectorDecision=10000;Mode->DirectorManualReadyAt=-1;Mode->NextDirectorAction=0;Mode->NextDirectorContactToggle=0;
		int32 FollowersBefore=0;for(auto* Body:Bodies)FollowersBefore+=Body->GetMultiplayCommand()==EMultiplayMannequinCommand::FollowMannequinPlayer;
		const bool ContactBefore=Mode->IsPracticeContactDamageEnabled();Mode->TickPracticeMannequinDirector(.01f);
		int32 FollowersAfter=0;for(auto* Body:Bodies)FollowersAfter+=Body->GetMultiplayCommand()==EMultiplayMannequinCommand::FollowMannequinPlayer;
		SawQ|=FollowersBefore>0 && FollowersAfter==0;SawB|=ContactBefore!=Mode->IsPracticeContactDamageEnabled();
		if(auto* Body=Mode->PracticeDirectorBody.Get()){Selected.Add(Body);Commands.Add(Body->GetMultiplayCommand());SawUncommanded|=!Mode->bDirectorCommandIssued;}
		Intervals.Add(FMath::RoundToInt(Mode->NextDirectorAction*100));
	}
	TestTrue(TEXT("Autonomous choices cover Z, X, C, Q, B and uncommanded release"),Commands.Contains(EMultiplayMannequinCommand::Patrol) && Commands.Contains(EMultiplayMannequinCommand::ChaseInDetectionRange) && Commands.Contains(EMultiplayMannequinCommand::FollowMannequinPlayer) && SawQ && SawB && SawUncommanded);
	TestTrue(TEXT("Body selection and action intervals are not a fixed rotation"),Selected.Num()>1 && Intervals.Num()>10);
	TSet<uint8> Beats;TSet<int32> BeatIntervals;
	for(int32 Sample=0;Sample<100;++Sample)
	{
		Mode->NextDirectorPacing=0;Mode->UpdatePracticeDirectorPacing(0);Beats.Add(Mode->DirectorBeat);BeatIntervals.Add(FMath::RoundToInt(Mode->NextDirectorPacing*100));
	}
	TestTrue(TEXT("Sparring pacing offers pressure, probes, repositioning and pauses at variable intervals"),Beats.Num()==4 && BeatIntervals.Num()>20);
	Mode->bPracticeDirectorToggleContact=false;
	const bool FixedContact=Mode->IsPracticeContactDamageEnabled();
	for(int32 Sample=0;Sample<30;++Sample){Mode->NextDirectorAction=0;Mode->NextDirectorContactToggle=0;Mode->TickPracticeMannequinDirector(.01f);}
	TestEqual(TEXT("Autonomous ticks honour the contact-switch permission"),Mode->IsPracticeContactDamageEnabled(),FixedContact);
	Mode->bFreePractice=false;TestFalse(TEXT("The director cannot change real tutorial/multiplayer rules"),Mode->SetPracticeDirectorContactDamage(!FixedContact));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectFreePracticeRuntimeTest,"ProjectProject01.FreePractice.Runtime",
	EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FProjectFreePracticeRuntimeTest::RunTest(const FString&)
{
	struct FState
	{
		int32 Stage=0;TWeakObjectPtr<UWorld> Previous;
		float Started=0,MaxSpread=0,MinRegroup=100000,NextHit=0;
		int32 Hits=0;
		TWeakObjectPtr<AMannequinAICharacter> DirectorBody;FVector DirectorOrigin=FVector::ZeroVector;
		TWeakObjectPtr<AMannequinAICharacter> Follower,NextLeader;FVector FollowOrigin=FVector::ZeroVector;
		FVector StandingLocations[2];float StandingYaw[2]={0,0},StandingMaxTurn[2]={0,0};
		bool Sprint[2]={false,false};bool Cover=false;
	};
	const auto State=MakeShared<FState>();const double Deadline=FPlatformTime::Seconds()+240;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Deadline]()
	{
		AProjectProject01TutorialGameMode* Mode=nullptr;UWorld* Front=nullptr;
		for (const auto& Context:GEngine->GetWorldContexts())
			if (auto* World=Context.World();World && World->IsGameWorld())
			{ Front=World;if (auto* Candidate=World->GetAuthGameMode<AProjectProject01TutorialGameMode>()) Mode=Candidate; }
		if (FPlatformTime::Seconds()>Deadline)
		{ AddError(FString::Printf(TEXT("Free practice timed out at stage %d"),State->Stage));if (Mode) UGameplayStatics::SetGamePaused(Mode,false);return true; }
		if (State->Stage==7)
		{
			if (!Mode && Front && Front!=State->Previous.Get() && Front->HasBegunPlay())
			{ TestTrue(TEXT("Practice returns safely to the front end"),Front->GetMapName().Contains(TEXT("LoginLevel")) || Front->GetMapName().Contains(TEXT("LobbyLevel")));return true; }
			return false;
		}
		if (!Mode || !Mode->bInitialized || !Mode->PracticeWidget || Mode->bPracticeAwaitingPresentation) return false;
		auto* UI=Mode->PracticeWidget.Get();auto* World=Mode->GetWorld();
		auto CountMannequins=[World]() { int32 Count=0;for (TActorIterator<AMannequinAICharacter> It(World);It;++It) if (IsValid(*It)) ++Count;return Count; };
		auto Capture=[this,UI](const TCHAR* Name)
		{
			UI->NativeTick(FGeometry(),0);const auto Slate=UI->WidgetTree->RootWidget->TakeWidget();FWidgetRenderer Renderer(true);
			auto* Target=Renderer.DrawWidget(Slate,FVector2D(1280,720));FlushRenderingCommands();FBufferArchive Data;
			const FString Directory=FPaths::ProjectSavedDir()/TEXT("ReviewRemediation/FreePracticeUI");IFileManager::Get().MakeDirectory(*Directory,true);
			TestTrue(TEXT("Practice controls render and save"),FImageUtils::ExportRenderTarget2DAsPNG(Target,Data) && FFileHelper::SaveArrayToFile(Data,*(Directory/Name)));
		};
		if (State->Stage==0)
		{
			TestTrue(TEXT("Free practice is separate from tutorial progression"),Mode->IsFreePractice() && !Mode->Widget && !Mode->GetCurrentStep());
			TestTrue(TEXT("Initial configuration is paused only after presentation readiness"),UGameplayStatics::IsGamePaused(Mode));
			TestEqual(TEXT("Requested mannequin count is spawned exactly"),CountMannequins(),4);
			if (!TestEqual(TEXT("Two independently controlled survivor bots"),Mode->PracticeBots.Num(),2)) return true;
			TestTrue(TEXT("Bots have distinct identities and pawns"),Mode->PracticeBots[0].Controller!=Mode->PracticeBots[1].Controller && Mode->PracticeBots[0].Pawn!=Mode->PracticeBots[1].Pawn);
			UI->NativeTick(FGeometry(),0);
			TestTrue(TEXT("Player AI toggle belongs only to survivor practice"),UI->DirectorToggle->GetVisibility()==ESlateVisibility::Collapsed && !Mode->IsPracticeDirectorEnabled());
			TestFalse(TEXT("Infinite lives defaults off"),Mode->HasPracticeInfiniteLives());
			TestTrue(TEXT("Mannequin practice shows only the survivor-role switch"),UI->SurvivorSwitch->GetVisibility()==ESlateVisibility::Visible && UI->MannequinSwitch->GetVisibility()==ESlateVisibility::Collapsed);
			TestTrue(TEXT("Both population controls explicitly allow mouse dragging"),UI->MannequinCount->GetEnableSlider() && UI->SurvivorCount->GetEnableSlider());
			UI->MannequinPlus->OnClicked.Broadcast();TestEqual(TEXT("Mannequin plus button increments without typing"),UI->MannequinCount->GetValue(),5.f);
			UI->MannequinMinus->OnClicked.Broadcast();TestEqual(TEXT("Mannequin minus button decrements without typing"),UI->MannequinCount->GetValue(),4.f);
			UI->SurvivorMinus->OnClicked.Broadcast();TestEqual(TEXT("Survivor minus button decrements without typing"),UI->SurvivorCount->GetValue(),1.f);
			UI->SurvivorPlus->OnClicked.Broadcast();TestEqual(TEXT("Survivor plus button increments without typing"),UI->SurvivorCount->GetValue(),2.f);
			bool bHasGauge=false;UI->WidgetTree->ForEachWidget([&](UWidget* W){bHasGauge|=W->IsA<USlider>();});
			TestFalse(TEXT("Separate population gauge bars have been removed"),bHasGauge);
			UI->NativeTick(FGeometry(),0);
			TestTrue(TEXT("Plus is disabled at the survivor count cap"),!UI->SurvivorPlus->GetIsEnabled());
			Capture(TEXT("Mannequin.png"));
			Mode->PracticeRandom.Initialize(9021);TSet<int32> Decisions;TSet<int32> Intervals;
			for(int32 Sample=0;Sample<20;++Sample)
			{
				Mode->PracticeTacticalSeconds=Sample*10;Mode->NextTacticalDecision=0;Mode->UpdatePracticeTactics();
				Decisions.Add(Mode->PracticeTacticalPhase);Intervals.Add(FMath::RoundToInt((Mode->NextTacticalDecision-Mode->PracticeTacticalSeconds)*10));
			}
			TestTrue(TEXT("Situational decisions and holding intervals are not a fixed three-phase clock"),Decisions.Num()>1 && Intervals.Num()>5);
			Mode->PracticeTacticalSeconds=0;
			// Exercise physical tactics explicitly; decision scheduling is checked separately.
			Mode->PracticeRandom.Initialize(3871);Mode->NextTacticalDecision=10000;Mode->PracticeTacticalPhase=0;
			for (TActorIterator<AMannequinAICharacter> It(World);It;++It) It->SetMultiplayCommand(EMultiplayMannequinCommand::HoldPosition);
			UI->HandleToggle();TestFalse(TEXT("Collapse resumes the world"),UGameplayStatics::IsGamePaused(Mode));
			UI->NativeTick(FGeometry(),0);TestTrue(TEXT("Collapsed panel hides its settings"),UI->SettingsPanel->GetVisibility()==ESlateVisibility::Collapsed);
			State->Started=World->GetTimeSeconds();State->Stage=1;return false;
		}
		if (State->Stage==1)
		{
			for (int32 I=0;I<2;++I) { State->Sprint[I]|=Mode->PracticeBots[I].Pawn->IsSprintingForBreathing();State->Cover|=Mode->PracticeBots[I].bCovering; }
			const float Distance=FVector::Dist2D(Mode->PracticeBots[0].Pawn->GetActorLocation(),Mode->PracticeBots[1].Pawn->GetActorLocation());
			const float Elapsed=World->GetTimeSeconds()-State->Started;
			const int32 Phase=Elapsed<8?0:Elapsed<16?1:2;
			if(Mode->PracticeTacticalPhase!=Phase) { Mode->PracticeTacticalPhase=Phase;for(auto& Bot:Mode->PracticeBots)Bot.RepathSeconds=0; }
			if (Phase==0) State->MaxSpread=FMath::Max(State->MaxSpread,Distance);
			if (Phase==1) State->MinRegroup=FMath::Min(State->MinRegroup,Distance);
			if (World->GetTimeSeconds()-State->Started<24) return false;
			TestTrue(TEXT("Both survivor bots actually sprint while moving"),State->Sprint[0] && State->Sprint[1]);
			TestTrue(TEXT("Bots physically regroup after spreading out"),State->MaxSpread-State->MinRegroup>100);
			TestTrue(TEXT("A survivor actually performs covering watch"),State->Cover);
			auto* Catcher=Mode->Learner->GetViewedMannequin();
			TestEqual(TEXT("OFF uses the multiplayer extra-catch count"),Mode->PracticeBots[0].Pawn->GetRemainingDeathCount(),0);
			Mode->PracticeBots[0].Pawn->HandleMannequinCatch(Catcher);
			TestFalse(TEXT("Catching one of two survivors does not finish practice"),Mode->bPracticeRoundEnded);
			Mode->PracticeBots[1].Pawn->HandleMannequinCatch(Catcher);
			TestTrue(TEXT("Both captures enter the death sequence before practice settings"),Mode->PracticeBots[0].Pawn->IsGameOver() && Mode->PracticeBots[1].Pawn->IsGameOver());
			State->Stage=2;return false;
		}
		if (State->Stage==2)
		{
			if (!Mode->IsPracticeMenuOpen()) return false;
			TestTrue(TEXT("Catching both ends only the local practice round"),Mode->bPracticeRoundEnded);
			TestTrue(TEXT("Settings are reachable after the end animation"),UGameplayStatics::IsGamePaused(Mode));
			State->Previous=World;UI->ApplyButton->OnClicked.Broadcast();State->Stage=10;return false;
		}
		if (World==State->Previous.Get()) return false;
		if (State->Stage==10)
		{
			TestTrue(TEXT("Applying after both deaths respawns both survivors alive"),Mode->PracticeBots.Num()==2 && !Mode->PracticeBots[0].Pawn->IsGameOver() && !Mode->PracticeBots[1].Pawn->IsGameOver());
			TestFalse(TEXT("Restart clears the completed round state"),Mode->IsPracticeRoundEnded());
			TestEqual(TEXT("Respawn also resets survivor death count to multiplayer rules"),Mode->PracticeBots[0].Pawn->GetRemainingDeathCount(),0);
			UI->StationaryToggle->SetIsChecked(true);UI->StandingWatchToggle->SetIsChecked(true);
			State->Previous=World;UI->ApplyButton->OnClicked.Broadcast();State->Stage=11;return false;
		}
		if (State->Stage==11 || State->Stage==13)
		{
			TestTrue(TEXT("Stationary option survives the restart URL"),Mode->ArePracticeSurvivorsStationary());
			TestEqual(TEXT("Stationary watch option survives the restart URL"),Mode->DoStationarySurvivorsLookAround(),State->Stage==11);
			for (TActorIterator<AMannequinAICharacter> It(World);It;++It) It->SetMultiplayCommand(EMultiplayMannequinCommand::HoldPosition);
			for(int32 I=0;I<2;++I){ State->StandingLocations[I]=Mode->PracticeBots[I].Pawn->GetActorLocation();State->StandingYaw[I]=Mode->PracticeBots[I].Controller->GetControlRotation().Yaw;State->StandingMaxTurn[I]=0; }
			Mode->ShowPracticeMenu(false);State->Started=World->GetTimeSeconds();++State->Stage;return false;
		}
		if (State->Stage==12 || State->Stage==14)
		{
			for(int32 I=0;I<2;++I) State->StandingMaxTurn[I]=FMath::Max(State->StandingMaxTurn[I],FMath::Abs(FMath::FindDeltaAngleDegrees(State->StandingYaw[I],Mode->PracticeBots[I].Controller->GetControlRotation().Yaw)));
			if(World->GetTimeSeconds()-State->Started<4) return false;
			for(int32 I=0;I<2;++I)
			{
				const auto& Bot=Mode->PracticeBots[I];
				TestTrue(TEXT("Stationary survivors do not walk or run away"),FVector::Dist2D(State->StandingLocations[I],Bot.Pawn->GetActorLocation())<2 && !Bot.Pawn->IsPracticeSprinting());
				const float Turn=State->StandingMaxTurn[I];
				TestTrue(TEXT("The actual survivor view scans only when watch is enabled"),State->Stage==12?Turn>10:Turn<1);
			}
			Mode->ShowPracticeMenu(true);
			if(State->Stage==12)
			{
				UI->StandingWatchToggle->SetIsChecked(false);State->Previous=World;UI->ApplyButton->OnClicked.Broadcast();State->Stage=13;return false;
			}
			UI->StationaryToggle->SetIsChecked(false);UI->StandingWatchToggle->SetIsChecked(true);
			UI->InfiniteLivesToggle->SetIsChecked(true);UI->CornerToggle->SetIsChecked(true);UI->CornerCount->SetValue(1);
			State->Previous=World;UI->HandleApply();State->Stage=15;return false;
		}
		if(State->Stage==15)
		{
			TestTrue(TEXT("Infinite lives and one-corner placement survive reload"),Mode->HasPracticeInfiniteLives() && Mode->bPracticeCorners && Mode->PracticeCornerCount==1);
			TestEqual(TEXT("One valid corner is actually used"),Mode->PracticeCornersPlaced,1);
			TestFalse(TEXT("The other survivor retains its authored spawn"),Mode->PracticeBots[1].bSpawnedAtCorner);
			for(TActorIterator<AMannequinAICharacter> It(World);It;++It)It->SetMultiplayCommand(EMultiplayMannequinCommand::HoldPosition);
			Mode->ShowPracticeMenu(false);State->Hits=0;State->NextHit=0;State->Stage=16;return false;
		}
		if(State->Stage==16)
		{
			if(World->GetTimeSeconds()<State->NextHit)return false;
			for(auto& Bot:Mode->PracticeBots)
			{
				Bot.Pawn->HandleMannequinCatch(Mode->Learner->GetViewedMannequin());
				TestTrue(TEXT("Each separated infinite-lives hit is accepted as nonfatal feedback"),!Bot.Pawn->IsGameOver() && Bot.Pawn->NextDeathCountDamageAllowedWorldSeconds>World->GetTimeSeconds());
				TestEqual(TEXT("Infinite lives never consumes the remaining count"),Bot.Pawn->GetRemainingDeathCount(),0);
				State->NextHit=FMath::Max(State->NextHit,static_cast<float>(Bot.Pawn->NextDeathCountDamageAllowedWorldSeconds+.1));
			}
			if(++State->Hits<3)return false;
			Mode->ShowPracticeMenu(true);UI->MannequinCount->SetValue(3);UI->SurvivorCount->SetValue(2);UI->DirectorToggle->SetIsChecked(true);
			State->Previous=World;UI->SurvivorSwitch->OnClicked.Broadcast();State->Stage=3;return false;
		}
		if (State->Stage==3)
		{
			TestFalse(TEXT("In-game role switch enters survivor practice"),Mode->IsMannequinTutorial());TestEqual(TEXT("Survivor practice honors selected mannequins"),CountMannequins(),3);
			TestEqual(TEXT("No leftover survivor bots after role switch"),Mode->PracticeBots.Num(),0);TestNotNull(TEXT("Survivor role possesses its own player"),Cast<APlayerCharacter>(Mode->Learner->GetPawn()));
			Capture(TEXT("Survivor.png"));
			TestTrue(TEXT("Survivor practice displays and preserves its explicit player-AI toggle"),UI->DirectorToggle->GetVisibility()==ESlateVisibility::Visible && Mode->IsPracticeDirectorEnabled());
			TestTrue(TEXT("AI contact-toggle permission is explicit and initially enabled"),UI->DirectorContactToggle->GetVisibility()==ESlateVisibility::Visible && UI->DirectorContactToggle->IsChecked() && Mode->CanPracticeDirectorToggleContact());
			TestTrue(TEXT("Survivor contact status is visible even with practice settings collapsed"),UI->ContactPanel->GetVisibility()!=ESlateVisibility::Collapsed);
			Mode->SetPracticeDirectorContactDamage(false);UI->NativeTick(FGeometry(),0);
			TestTrue(TEXT("Contact OFF announcement omits the removed direct-control explanation"),UI->ContactStatus->GetText().ToString().Contains(TEXT("비활성화")) && !UI->ContactStatus->GetText().ToString().Contains(TEXT("직접 조작")));
			Capture(TEXT("SurvivorContactOff.png"));
			Mode->SetPracticeDirectorContactDamage(true);UI->NativeTick(FGeometry(),0);
			TestTrue(TEXT("Contact ON announcement follows authoritative state"),UI->ContactStatus->GetText().ToString().Contains(TEXT("접촉 피해가 활성화되었습니다")));
			auto* Player=CastChecked<APlayerCharacter>(Mode->Learner->GetPawn());
			AMannequinAICharacter* Body=nullptr;
			for(TActorIterator<AMannequinAICharacter> It(World);It;++It){It->SetCatchDamageEnabled(false);It->SetMultiplayCommand(EMultiplayMannequinCommand::HoldPosition);if(!Body)Body=*It;}
			if(!TestNotNull(TEXT("Director body exists"),Body))return true;
			Body->SetActorLocation(Player->GetActorLocation()-FVector(120,0,0));Body->SetActorRotation(FRotator::ZeroRotator);
			Mode->Learner->SetControlRotation(FRotator::ZeroRotator);Body->SetFrozenBySurvivorVision(false);
			Mode->PracticeDirectorBody=Body;Body->SetManualControlEnabled(true);Mode->NextDirectorDecision=10000;Mode->NextDirectorAction=10000;Mode->NextDirectorPacing=10000;Mode->DirectorBeat=0;
			Mode->bPracticeMenuOpen=false;Mode->NextDirectorAttack=0;Mode->TickPracticeMannequinDirector(.1f);
			TestTrue(TEXT("Simulated player can inflict an unseen in-range attack without killing an infinite-life learner"),!Player->IsGameOver() && Player->NextDeathCountDamageAllowedWorldSeconds>World->GetTimeSeconds());
			TestEqual(TEXT("A successful attack gives the learner a brief opening, rather than immediate spam"),Mode->DirectorBeat,static_cast<uint8>(3));
			Mode->DirectorBeat=0;Mode->NextDirectorPacing=10000;
			Player->NextDeathCountDamageAllowedWorldSeconds=0;Body->SetFrozenBySurvivorVision(true);Mode->NextDirectorAttack=0;Mode->TickPracticeMannequinDirector(.1f);
			TestEqual(TEXT("Observed body cannot attack"),Player->NextDeathCountDamageAllowedWorldSeconds,0.0);
			Body->SetFrozenBySurvivorVision(false);Body->SetActorLocation(Player->GetActorLocation()-FVector(1000,0,0));Mode->NextDirectorAttack=0;Mode->TickPracticeMannequinDirector(.1f);
			TestEqual(TEXT("Director respects the real attack range"),Player->NextDeathCountDamageAllowedWorldSeconds,0.0);
			Body->SetActorLocation(Player->GetActorLocation()-FVector(160,0,0));Body->SetActorRotation(FRotator::ZeroRotator);
			auto* Wall=World->SpawnActor<AActor>();auto* Block=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Block);
			Block->SetBoxExtent(FVector(5,180,200));Block->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Block->SetCollisionResponseToAllChannels(ECR_Ignore);Block->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Block->RegisterComponent();
			Wall->SetActorLocation(Player->GetActorLocation()-FVector(80,0,0));Mode->NextDirectorMove=10000;Mode->NextDirectorAttack=0;Mode->TickPracticeMannequinDirector(.1f);
			TestEqual(TEXT("Director cannot attack through a wall even inside range"),Player->NextDeathCountDamageAllowedWorldSeconds,0.0);Wall->Destroy();
			Mode->bPracticeDirector=false;Mode->PracticeDirectorBody.Reset();Body->SetManualControlEnabled(false);Mode->NextDirectorDecision=0;Mode->TickPracticeMannequinDirector(.1f);
			TestFalse(TEXT("OFF adds no simulated player control"),Mode->PracticeDirectorBody.IsValid());Mode->bPracticeDirector=true;
			Mode->NextDirectorDecision=0;Mode->TickPracticeMannequinDirector(.1f);TestTrue(TEXT("ON selects a body for native-path movement"),Mode->PracticeDirectorBody.IsValid());
			Body=Mode->PracticeDirectorBody.Get();if(!Body)return true;
			TArray<AMannequinAICharacter*> Others;for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(*It!=Body)Others.Add(*It);
			if(!TestEqual(TEXT("Follower exercise has two other bodies"),Others.Num(),2))return true;
			auto* Follower=Others[0];auto* Leader=Body;State->NextLeader=Others[1];
			Mode->SelectPracticeDirectorBody(Follower);Follower->SetManualControlEnabled(true);Mode->IssuePracticeDirectorCommand(EMultiplayMannequinCommand::FollowMannequinPlayer);
			Mode->SelectPracticeDirectorBody(Leader);Leader->SetManualControlEnabled(true);
			Follower->SetActorLocation(Player->GetActorLocation()+FVector(1400,-700,0));Leader->SetActorLocation(Player->GetActorLocation()+FVector(1000,-700,0));Leader->SetActorRotation(FRotator::ZeroRotator);
			Mode->Learner->SetControlRotation(FRotator(0,180,0));Mode->Learner->PlayerCameraManager->UpdateCamera(.1f);
			Mode->NextDirectorDecision=10000;Mode->NextDirectorAction=10000;Mode->NextDirectorMove=10000;Mode->NextDirectorAttack=10000;
			State->Follower=Follower;State->FollowOrigin=Follower->GetActorLocation();State->Started=World->GetTimeSeconds();
			Mode->ShowPracticeMenu(false);
			TestTrue(TEXT("Survivor practice shows only the mannequin-role switch"),UI->SurvivorSwitch->GetVisibility()==ESlateVisibility::Collapsed && UI->MannequinSwitch->GetVisibility()==ESlateVisibility::Visible);
			State->Stage=31;return false;
		}
		if(State->Stage==31)
		{
			if(World->GetTimeSeconds()-State->Started<3)return false;
			if(!State->Follower.IsValid() || !State->NextLeader.IsValid()){AddError(TEXT("Follower exercise bodies disappeared"));return true;}
			TestTrue(TEXT("C performs actual native follower movement"),FVector::Dist2D(State->FollowOrigin,State->Follower->GetActorLocation())>100 && State->Follower->GetMultiplayActivity()==EMultiplayMannequinActivity::Following);
			auto* Player=CastChecked<APlayerCharacter>(Mode->Learner->GetPawn());auto* Leader=State->NextLeader.Get();
			Leader->SetActorLocation(Player->GetActorLocation()+FVector(1500,-200,0));Mode->SelectPracticeDirectorBody(Leader);Leader->SetManualControlEnabled(true);
			Mode->NextDirectorMove=10000;State->FollowOrigin=State->Follower->GetActorLocation();State->Started=World->GetTimeSeconds();State->Stage=32;return false;
		}
		if(State->Stage==32)
		{
			if(World->GetTimeSeconds()-State->Started<3)return false;
			TestTrue(TEXT("Existing followers physically retarget the newly selected body"),Mode->GetMannequinPlayerFocusPawn()==State->NextLeader.Get() && State->Follower.IsValid() && FVector::Dist2D(State->FollowOrigin,State->Follower->GetActorLocation())>100 && State->Follower->GetMultiplayCommand()==EMultiplayMannequinCommand::FollowMannequinPlayer);
			TestTrue(TEXT("Q cancels followers in the real practice world"),Mode->ResetPracticeDirectorFollowers());
			TestEqual(TEXT("Q returns the follower to patrol"),State->Follower->GetMultiplayCommand(),EMultiplayMannequinCommand::Patrol);
			auto* Body=Mode->PracticeDirectorBody.Get();auto* Player=CastChecked<APlayerCharacter>(Mode->Learner->GetPawn());
			Body->SetActorLocation(Player->GetActorLocation()-FVector(600,0,0));Body->SetActorRotation(FRotator::ZeroRotator);Body->SetFrozenBySurvivorVision(false);
			Mode->Learner->SetControlRotation(FRotator::ZeroRotator);Mode->Learner->PlayerCameraManager->UpdateCamera(.1f);Mode->NextDirectorMove=0;
			State->DirectorBody=Body;State->DirectorOrigin=Body->GetActorLocation();State->Started=World->GetTimeSeconds();State->Stage=30;return false;
		}
		if(State->Stage==30)
		{
			if(World->GetTimeSeconds()-State->Started<6)return false;
			TestTrue(TEXT("Simulated player actually moves its chosen body along navigation"),State->DirectorBody.IsValid() && FVector::Dist2D(State->DirectorOrigin,State->DirectorBody->GetActorLocation())>100);
			TestTrue(TEXT("Free survivor escape resolves locally without tutorial steps"),Mode->SignalEvent(Mode->Learner,TEXT("Tutorial.Escape")));
			State->Stage=8;return false;
		}
		if (State->Stage==8)
		{
			if (!Mode->IsPracticeMenuOpen()) return false;
			TestTrue(TEXT("Escape returns to practice settings instead of a backend result"),Mode->IsPracticeRoundEnded() && UGameplayStatics::IsGamePaused(Mode));
			UI->NativeTick(FGeometry(),0);
			TestEqual(TEXT("Escape retains the existing restart button, without a continue duplicate"),UI->ApplyText->GetText().ToString(),FString(TEXT("인원 적용 / 다시 연습")));
			UI->InfiniteLivesToggle->SetIsChecked(false);UI->DirectorToggle->SetIsChecked(true);UI->DirectorContactToggle->SetIsChecked(false);UI->MannequinCount->SetValue(1);State->Previous=World;UI->ApplyButton->OnClicked.Broadcast();State->Stage=4;return false;
		}
		if (State->Stage==4)
		{
			TestEqual(TEXT("Reducing counts removes old actors instead of accumulating them"),CountMannequins(),1);
			TestFalse(TEXT("Disallowing AI contact switching survives reload"),Mode->CanPracticeDirectorToggleContact());
			TestTrue(TEXT("Blocking B does not disable the player AI itself"),Mode->IsPracticeDirectorEnabled());
			Mode->ShowPracticeMenu(false);
			auto* Player=CastChecked<APlayerCharacter>(Mode->Learner->GetPawn());TestEqual(TEXT("Survivor learner also uses normal multiplayer lives with OFF"),Player->GetRemainingDeathCount(),0);
			for (TActorIterator<AMannequinAICharacter> It(World);It;++It) { Player->HandleMannequinCatch(*It);break; }
			TestTrue(TEXT("The survivor enters its real death sequence"),Player->IsGameOver());State->Stage=9;return false;
		}
		if (State->Stage==9)
		{
			if (!Mode->IsPracticeMenuOpen()) return false;
			TestTrue(TEXT("Survivor death leaves practice controls accessible"),Mode->IsPracticeRoundEnded() && UGameplayStatics::IsGamePaused(Mode));
			UI->MannequinCount->SetValue(2);UI->SurvivorCount->SetValue(2);State->Previous=World;UI->HandleMannequin();State->Stage=5;return false;
		}
		if (State->Stage==5)
		{
			TestTrue(TEXT("Switch back restores mannequin practice"),Mode->IsMannequinTutorial());TestEqual(TEXT("Mannequin practice also supports changing mannequin counts"),CountMannequins(),2);
			TestTrue(TEXT("Other role preserves but hides the survivor-only contact-switch option"),!Mode->CanPracticeDirectorToggleContact() && UI->DirectorContactToggle->GetVisibility()==ESlateVisibility::Collapsed);
			TestEqual(TEXT("Two fresh survivor bots after switching back"),Mode->PracticeBots.Num(),2);
			UI->CornerToggle->SetIsChecked(true);UI->CornerCount->SetValue(2);State->Previous=World;UI->HandleApply();State->Stage=21;return false;
		}
		if(State->Stage==21)
		{
			TestEqual(TEXT("Both requested survivors can use separate valid corners"),Mode->PracticeCornersPlaced,2);
			TestTrue(TEXT("Corner placements are separated"),FVector::Dist2D(Mode->PracticeBots[0].Pawn->GetActorLocation(),Mode->PracticeBots[1].Pawn->GetActorLocation())>400);
			State->Previous=World;Mode->RestartFreePractice(true,999,-5);State->Stage=6;return false;
		}
		TestEqual(TEXT("Mannequin counts are clamped to supported slots"),CountMannequins(),10);TestEqual(TEXT("Survivor counts are clamped to one or two"),Mode->PracticeBots.Num(),1);
		State->Previous=World;Mode->ReturnToMatching();State->Stage=7;return false;
	}));
	return true;
}
#endif

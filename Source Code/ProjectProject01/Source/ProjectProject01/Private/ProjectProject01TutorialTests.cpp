#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ProjectProject01Tutorial.h"
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
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"
#include "Misc/PackageName.h"

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
		Controlled->SetActorLocationAndRotation(Victim->GetActorLocation()+FVector(0,-110,0),FRotator(0,90,0),false,nullptr,ETeleportType::TeleportPhysics);
		TestTrue(TEXT("The actual authoritative melee attack catches the practice survivor"),Mode->TryMannequinAttack(Mode->Learner));
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
	const double Deadline=FPlatformTime::Seconds()+12;
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
#endif

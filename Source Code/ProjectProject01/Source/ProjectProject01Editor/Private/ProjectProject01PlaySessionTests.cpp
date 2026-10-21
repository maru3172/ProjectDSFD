#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "ProjectProject01Tutorial.h"
#include "ProjectProject01LevelTransitionTrigger.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01VoiceChatSubsystem.h"
#include "PlayerCharacter.h"
#include "MannequinAICharacter.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/UnrealType.h"
#include "Kismet/GameplayStatics.h"

namespace ProjectPlayTests
{
	void EndSession(FAutomationTestBase* Test,TFunction<void()> Cleanup={})
	{
		GEditor->RequestEndPlayMap();
		const double Deadline=FPlatformTime::Seconds()+15;
		Test->AddCommand(new FFunctionLatentCommand([Test,Deadline,Cleanup]()
		{
			if (!GEditor->IsPlaySessionInProgress()) { if (Cleanup) Cleanup(); return true; }
			if (FPlatformTime::Seconds()<Deadline) return false;
			Test->AddError(TEXT("PIE did not shut down after the test")); if (Cleanup) Cleanup(); return true;
		}));
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FProjectTutorialEditorPracticeTest,"ProjectProject01.Editor.TutorialPractice",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FProjectTutorialEditorPracticeTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{
	Names.Add(TEXT("Survivor")); Commands.Add(TEXT("Survivor"));
	Names.Add(TEXT("Mannequin")); Commands.Add(TEXT("Mannequin"));
	Names.Add(TEXT("FreeSurvivor")); Commands.Add(TEXT("FreeSurvivor"));
	Names.Add(TEXT("FreeMannequin")); Commands.Add(TEXT("FreeMannequin"));
}
bool FProjectTutorialEditorPracticeTest::RunTest(const FString& Role)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress()) { AddError(TEXT("Stop the current PIE session first")); return false; }
	const auto* Defaults=GetDefault<ULevelEditorPlaySettings>();
	int32 Clients=0; EPlayNetMode NetMode; bool OneProcess=false;
	Defaults->GetPlayNumberOfClients(Clients); Defaults->GetPlayNetMode(NetMode); Defaults->GetRunUnderOneProcess(OneProcess);
	const FString PreviousURL=GEditor->UserEditedPlayWorldURL;
	const TWeakObjectPtr<UWorld> EditorWorld=GEditor->GetEditorWorldContext().World();
	const bool bFree=Role.StartsWith(TEXT("Free")),bMannequin=Role.EndsWith(TEXT("Mannequin"));
	TestTrue(TEXT("Role-specific menu starts offline practice"),UProjectProject01TutorialEditorLibrary::StartPractice(bMannequin,bFree));
	GEditor->StartQueuedPlaySessionRequest();
	const double Deadline=FPlatformTime::Seconds()+45;
	struct FReplayState { int32 Stage=0; FName Intro; TWeakObjectPtr<UWorld> Previous; };
	const auto Replay=MakeShared<FReplayState>();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,bMannequin,bFree,Clients,NetMode,OneProcess,PreviousURL,EditorWorld,Deadline,Replay]()
	{
		for (const auto& Context:GEngine->GetWorldContexts())
		{
			auto* World=Context.World(); if (!World || World->WorldType!=EWorldType::PIE) continue;
			auto* Mode=World->GetAuthGameMode<AProjectProject01TutorialGameMode>();
			if (!Mode || (!bFree && !Mode->GetCurrentStep())) continue;
			auto* PC=Cast<AMultiplayTestPlayerController>(World->GetFirstPlayerController());
			if (!PC || (bMannequin ? !PC->GetViewedMannequin() : !PC->GetPawn())) continue;
			if (bFree && !UGameplayStatics::IsGamePaused(World)) continue;
			if (Replay->Stage>0)
			{
				if (World==Replay->Previous.Get()) continue;
				TestEqual(TEXT("Replays preserve the selected role"),Mode->IsMannequinTutorial(),bMannequin);
				TestTrue(TEXT("Replayed instructions still require explicit confirmation"),Mode->IsAwaitingRead());
				if (Replay->Stage==1)
				{
					TestTrue(TEXT("Practical replay skips repeated controls and starts at escape/capture"),Mode->GetCurrentStep()->Condition==(bMannequin ? EProjectTutorialCondition::Capture : EProjectTutorialCondition::Trigger));
					Replay->Stage=2; Replay->Previous=World; Mode->RestartTutorial(false); return false;
				}
				TestTrue(TEXT("Full replay restores the initial role explanation"),Mode->GetCurrentStep()->Id==Replay->Intro);
				ProjectPlayTests::EndSession(this); return true;
			}
			TestEqual(TEXT("Practice is standalone, never a dedicated/client session"),World->GetNetMode(),NM_Standalone);
			TestEqual(TEXT("Chosen editor role reaches the actual tutorial GameMode"),Mode->IsMannequinTutorial(),bMannequin);
			if (!bFree) TestTrue(TEXT("Central role instructions wait for acknowledgement"),Mode->IsAwaitingRead());
			int32 CurrentClients=0; EPlayNetMode CurrentMode; bool CurrentProcess=false;
			const auto* Current=GetDefault<ULevelEditorPlaySettings>();
			Current->GetPlayNumberOfClients(CurrentClients); Current->GetPlayNetMode(CurrentMode); Current->GetRunUnderOneProcess(CurrentProcess);
			TestTrue(TEXT("Normal multiplayer PIE preferences are unchanged"),Clients==CurrentClients && NetMode==CurrentMode && OneProcess==CurrentProcess);
			TestEqual(TEXT("One-shot role URL is restored"),GEditor->UserEditedPlayWorldURL,PreviousURL);
			TestTrue(TEXT("The edited map remains open and is not replaced"),GEditor->GetEditorWorldContext().World()==EditorWorld.Get());
			if (bFree)
			{
				TestTrue(TEXT("Free practice waits in its separate settings, not tutorial instructions"),Mode->IsFreePractice() && Mode->IsPracticeMenuOpen() && !Mode->GetCurrentStep());
				Mode->ShowPracticeMenu(false);TestFalse(TEXT("Closing the practice panel resumes PIE"),UGameplayStatics::IsGamePaused(World));
				ProjectPlayTests::EndSession(this);return true;
			}
			Replay->Intro=Mode->GetCurrentStep()->Id; Replay->Stage=1; Replay->Previous=World;
			Mode->RestartTutorial(true); return false;
		}
		if (FPlatformTime::Seconds()<Deadline) return false;
		AddError(TEXT("Offline tutorial did not initialize the selected editor role")); ProjectPlayTests::EndSession(this); return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectThreeClientTravelTest,"ProjectProject01.Editor.ThreeClientTravel",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectThreeClientTravelTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress()) { AddError(TEXT("Stop the current PIE session first")); return false; }
	// This is a local transport/camera test, not an EOS account/voice test. Disable
	// external voice in transient defaults only; restore after every PIE instance ends.
	auto* VoiceDefaults=GetMutableDefault<UProjectProject01VoiceChatSubsystem>();
	auto* VoiceEnabled=FindFProperty<FBoolProperty>(VoiceDefaults->GetClass(),TEXT("bEnableSurvivorProximityVoice"));
	const bool PreviousVoice=VoiceEnabled->GetPropertyValue_InContainer(VoiceDefaults);
	VoiceEnabled->SetPropertyValue_InContainer(VoiceDefaults,false);
	const TFunction<void()> Cleanup=[VoiceDefaults,VoiceEnabled,PreviousVoice]() { VoiceEnabled->SetPropertyValue_InContainer(VoiceDefaults,PreviousVoice); };
	// These two editable intermediate-floor maps intentionally use level-transition
	// volumes instead of a final-match Escape box. Keep the known map diagnostic
	// explicit; do not suppress any travel/network/assertion errors.
	AddExpectedMessage(TEXT("No multiplayer escape TriggerBox is tagged ProjectProject01Escape"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,2);
	auto Request=UProjectProject01TutorialEditorLibrary::MakePracticeRequest();
	Request.EditorPlaySettings->SetPlayNetMode(PIE_Client); Request.EditorPlaySettings->SetPlayNumberOfClients(3);
	Request.EditorPlaySettings->SetServerPort(18377);
	Request.GlobalMapOverride=TEXT("/Game/MyProject/Level/MultiplayTest");
	Request.GameModeOverride=AMultiplayTestGameMode::StaticClass();
	GEditor->RequestPlaySession(Request); GEditor->StartQueuedPlaySessionRequest();
	const double Deadline=FPlatformTime::Seconds()+90;
	struct FState { int32 Stage=0; double At=0; TWeakObjectPtr<UWorld> Source; TWeakObjectPtr<APlayerCharacter> Live; };
	const auto State=MakeShared<FState>();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Deadline,Cleanup]()
	{
		if (FPlatformTime::Seconds()>Deadline)
		{ AddError(FString::Printf(TEXT("Three-client travel timed out at stage %d"),State->Stage)); ProjectPlayTests::EndSession(this,Cleanup); return true; }
		if (State->Stage==0)
		{
			for (const auto& Context:GEngine->GetWorldContexts())
			{
				auto* World=Context.World(); if (!World || World->WorldType!=EWorldType::PIE || World->GetNetMode()==NM_Client) continue;
				auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();
				if (!Mode || !Mode->IsAuthoritativeMatchInProgress()) continue;
				TArray<APlayerCharacter*> Survivors; AMultiplayTestPlayerController* MannequinPC=nullptr;
				for (FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
					if (auto* PC=Cast<AMultiplayTestPlayerController>(It->Get()))
					{
						if (auto* Pawn=Cast<APlayerCharacter>(PC->GetPawn())) Survivors.Add(Pawn);
						if (PC->GetViewedMannequin()) MannequinPC=PC;
					}
				if (Survivors.Num()!=2 || !MannequinPC) continue;
				Mode->ToggleMannequinCatchDamage(MannequinPC);
				State->Source=World; State->Live=Survivors[0];
				Survivors[1]->SetRemainingDeathCountForGameMode(0);
				Survivors[1]->HandleMannequinCatch(MannequinPC->GetViewedMannequin());
				State->Stage=1; State->At=FPlatformTime::Seconds(); break;
			}
			return false;
		}
		if (State->Stage==1)
		{
			// Allow the real death handoff to finish before requesting survivor-only travel.
			if (FPlatformTime::Seconds()-State->At<4) return false;
			auto* World=State->Source.Get(); auto* Live=State->Live.Get();
			if (!World || !Live) { AddError(TEXT("Source survivor was lost before departure")); ProjectPlayTests::EndSession(this,Cleanup); return true; }
			FActorSpawnParameters Spawn; Spawn.bDeferConstruction=true;
			auto* Trigger=World->SpawnActor<AProjectProject01LevelTransitionTrigger>(AProjectProject01LevelTransitionTrigger::StaticClass(),FTransform(FVector(100000,100000,100000)),Spawn);
			auto* Property=FindFProperty<FSoftObjectProperty>(Trigger->GetClass(),TEXT("DestinationLevel"));
			Property->SetPropertyValue_InContainer(Trigger,FSoftObjectPtr(FSoftObjectPath(TEXT("/Game/MyProject/Level/MultiplayTest1.MultiplayTest1"))));
			Trigger->FinishSpawning(FTransform(FVector(100000,100000,100000)));
			TestTrue(TEXT("Only the living survivor triggers departure"),Trigger->RequestTransition(Live));
			State->Stage=2; State->At=FPlatformTime::Seconds(); return false;
		}
		int32 Ready=0,Observers=0,Mannequins=0;
		for (const auto& Context:GEngine->GetWorldContexts())
		{
			auto* World=Context.World();
			if (!World || World->WorldType!=EWorldType::PIE || World->GetNetMode()!=NM_Client || !World->GetPackage()->GetName().Contains(TEXT("MultiplayTest1"))) continue;
			auto* PC=Cast<AMultiplayTestPlayerController>(World->GetFirstPlayerController());
			if (!PC || PC->GetMatchPhase()!=EMultiplayTestMatchPhase::InProgress) continue;
			if (PC->GetSurvivorState()==EMultiplayTestSurvivorState::Eliminated)
			{
				auto* Target=Cast<APlayerCharacter>(PC->GetViewTarget());
				if (Target && !Target->IsGameOver() && !Target->IsHidden()) { ++Ready; ++Observers; }
			}
			else if (PC->GetViewedMannequin() && PC->GetViewTarget()==PC->GetViewedMannequin()) { ++Ready; ++Mannequins; }
			else if (PC->GetPawn() && PC->GetViewTarget()==PC->GetPawn()) ++Ready;
		}
		if (Ready!=3) return false;
		TestEqual(TEXT("Dead survivor remains an observer after real network travel"),Observers,1);
		TestEqual(TEXT("Mannequin travels without operating a trigger"),Mannequins,1);
		AddInfo(TEXT("Three actual PIE network clients reached MultiplayTest1: survivor, dead observer and mannequin."));
		ProjectPlayTests::EndSession(this,Cleanup); return true;
	}));
	return true;
}
#endif

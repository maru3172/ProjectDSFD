#include "Editor.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "ProjectLoadingScreenSubsystem.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "ProjectSingleIntro.h"

// The prologue and its scene replays are opened by a real map change from the title, which raises the travel loading
// screen. The intro runs its own opening (no player-pawn reveal), so it must close that loading screen itself; before
// this it stayed on "플레이 화면·시점 준비 중" until the 120 s timeout. Opening the map directly in PIE (the flow
// test) never raises the loading screen, so this test travels into it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSingleIntroTravelTest, "ProjectProject01.Editor.SingleIntroAfterTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSingleIntroTravelTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress()) return false;
	// Scene replays need the scenes marked as seen; keep the PC's own value and put it back afterwards.
	int32 OldSeen = 0;
	const bool bHadSeen = GConfig->GetInt(TEXT("ProjectDFSD.IntroScenes"), TEXT("Seen"), OldSeen, GGameUserSettingsIni);
	GConfig->SetInt(TEXT("ProjectDFSD.IntroScenes"), TEXT("Seen"), 7, GGameUserSettingsIni);

	auto Request = UProjectProject01TutorialEditorLibrary::MakePracticeRequest();
	Request.GlobalMapOverride = TEXT("/Game/MyProject/Level/SinglePlayerIntro");
	Request.GameModeOverride = nullptr;
	GEditor->RequestPlaySession(Request);
	GEditor->StartQueuedPlaySessionRequest();

	struct FProgress
	{
		int32 Run = 0; // 0: prologue, 1: replay of the shutter scene (3).
		bool bTraveled = false, bSawLoading = false;
		double StartedAt = 0;
		TWeakObjectPtr<UWorld> From;
	};
	const auto State = MakeShared<FProgress>();
	const double Deadline = FPlatformTime::Seconds() + 120.;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State, Deadline, bHadSeen, OldSeen]()
	{
		auto Finish = [&]()
		{
			if (bHadSeen) GConfig->SetInt(TEXT("ProjectDFSD.IntroScenes"), TEXT("Seen"), OldSeen, GGameUserSettingsIni);
			else GConfig->RemoveKey(TEXT("ProjectDFSD.IntroScenes"), TEXT("Seen"), GGameUserSettingsIni);
			if (GEditor->IsPlaySessionInProgress()) GEditor->RequestEndPlayMap();
			return true;
		};
		if (FPlatformTime::Seconds() > Deadline)
		{
			AddError(FString::Printf(TEXT("Intro run %d did not start after travel"), State->Run));
			return Finish();
		}
		UWorld* World = GEditor->PlayWorld;
		if (!World || !World->HasBegunPlay()) return false;
		auto* Loading = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UProjectLoadingScreenSubsystem>() : nullptr;
		if (!Loading) { AddError(TEXT("No loading screen subsystem")); return Finish(); }
		if (!State->bTraveled)
		{
			State->From = World;State->bTraveled = true;State->bSawLoading = false;State->StartedAt = FPlatformTime::Seconds();
			UGameplayStatics::OpenLevel(World, TEXT("/Game/MyProject/Level/SinglePlayerIntro"), true,
				State->Run == 0 ? TEXT("IntroScene=0") : TEXT("IntroScene=3"));
			return false;
		}
		State->bSawLoading |= Loading->IsLoading();
		if (World == State->From.Get()) return false; // Still on the departing world.
		auto* Mode = World->GetAuthGameMode<AProjectSingleIntroGameMode>();
		if (!Mode || Mode->Phase == AProjectSingleIntroGameMode::EPhase::Waiting || Loading->IsLoading()) return false;
		const double Seconds = FPlatformTime::Seconds() - State->StartedAt;
		const FString Label = State->Run == 0 ? TEXT("Prologue") : TEXT("Shutter scene replay");
		TestTrue(Label + TEXT(": the travel raised the loading screen (the case the direct PIE test misses)"), State->bSawLoading);
		TestTrue(Label + FString::Printf(TEXT(": loading closed and the intro started within 20 s (%.1f s)"), Seconds), Seconds < 20.);
		if (State->Run == 0) TestTrue(TEXT("Prologue starts at its opening"), Mode->Phase == AProjectSingleIntroGameMode::EPhase::Phone);
		else TestTrue(TEXT("The replay starts at the shutter scene"), Mode->Phase == AProjectSingleIntroGameMode::EPhase::Finale);
		if (++State->Run < 2) { State->bTraveled = false; return false; }
		return Finish();
	}));
	return true;
}

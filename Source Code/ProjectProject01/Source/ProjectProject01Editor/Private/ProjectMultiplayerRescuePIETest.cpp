#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MannequinAICharacter.h"
#include "Misc/AutomationTest.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "PlayerInteractionComponent.h"
#include "ProjectMultiplayerRescue.h"
#include "ProjectProject01LevelTransitionTrigger.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "ProjectProject01VoiceChatSubsystem.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMultiplayerRescuePIETest, "ProjectProject01.Editor.MultiplayerRescue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectMultiplayerRescuePIETest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress()) return false;
	auto* Voice = GetMutableDefault<UProjectProject01VoiceChatSubsystem>();
	const auto* VoiceProperty = FindFProperty<FBoolProperty>(Voice->GetClass(), TEXT("bEnableSurvivorProximityVoice"));
	if (!VoiceProperty) return false;
	const bool bOldVoice = VoiceProperty->GetPropertyValue_InContainer(Voice);
	VoiceProperty->SetPropertyValue_InContainer(Voice, false);
	auto Request = UProjectProject01TutorialEditorLibrary::MakePracticeRequest();
	Request.EditorPlaySettings->SetPlayNetMode(PIE_Client);
	Request.EditorPlaySettings->SetPlayNumberOfClients(3);
	Request.EditorPlaySettings->SetServerPort(18379);
	Request.GlobalMapOverride = TEXT("/Game/MyProject/Level/MultiplayerRescueTest");
	Request.GameModeOverride = nullptr;
	GEditor->RequestPlaySession(Request);
	GEditor->StartQueuedPlaySessionRequest();
	struct FState
	{
		int32 Step = 0;
		double NextAt = 0.;
		double DetainedAt = 0.;
		FVector CaptiveLocation = FVector::ZeroVector;
		TWeakObjectPtr<AProjectProject01LevelTransitionTrigger> ReturnTrigger;
		TWeakObjectPtr<AProjectMultiplayerRescueStation> Station;
		TWeakObjectPtr<AProjectMultiplayerRescueStation> LocalStation;
		TWeakObjectPtr<APlayerCharacter> Captive, Rescuer, LocalRescuer, LocalCaptive;
		TWeakObjectPtr<AMannequinAICharacter> Catcher;
	};
	const auto State = MakeShared<FState>();
	const double Deadline = FPlatformTime::Seconds() + 80.;
	const auto End = [Voice, VoiceProperty, bOldVoice]()
	{
		GEditor->RequestEndPlayMap();
		VoiceProperty->SetPropertyValue_InContainer(Voice, bOldVoice);
	};
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State, Deadline, End]()
	{
		const double Now = FPlatformTime::Seconds();
		if (Now > Deadline)
		{
			AddError(FString::Printf(TEXT("Three-client rescue timed out at step %d"), State->Step));
			End();
			return true;
		}
		if (Now < State->NextAt) return false;
		const auto PressF = [&]()
		{
			if (auto* Player = State->LocalRescuer.Get())
				if (auto* Interaction = Player->FindComponentByClass<UPlayerInteractionComponent>()) Interaction->TryInteract();
		};
		switch (State->Step)
		{
		case 0:
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				auto* World = Context.World();
				if (!World || World->WorldType != EWorldType::PIE || World->GetNetMode() != NM_DedicatedServer) continue;
				auto* Mode = World->GetAuthGameMode<AProjectMultiplayerRescueGameMode>();
				if (!Mode || !Mode->IsAuthoritativeMatchInProgress()) continue;
				TArray<APlayerCharacter*> Survivors;
				AMultiplayTestPlayerController* Mannequin = nullptr;
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
					if (auto* PC = Cast<AMultiplayTestPlayerController>(It->Get()))
					{
						if (auto* Player = Cast<APlayerCharacter>(PC->GetPawn())) Survivors.Add(Player);
						if (PC->GetViewedMannequin()) Mannequin = PC;
					}
				TActorIterator<AProjectMultiplayerRescueStation> Station(World);
				if (Survivors.Num() != 2 || !Mannequin || !Station) continue;
				for (TActorIterator<AProjectProject01LevelTransitionTrigger> It(World); It; ++It)
				{
					if (It->ActorHasTag(TEXT("RescueReturn"))) State->ReturnTrigger = *It;
				}
				if (!State->ReturnTrigger.IsValid())
				{
					AddError(TEXT("Rescue test map is missing the basement return trigger"));
					End();
					return true;
				}
				Mode->ToggleMannequinCatchDamage(Mannequin);
				State->Station = *Station;
				State->Captive = Survivors[0];
				State->Rescuer = Survivors[1];
				State->Catcher = Mannequin->GetViewedMannequin();
				Survivors[0]->SetRemainingDeathCountForGameMode(0);
				Survivors[0]->HandleMannequinCatch(State->Catcher.Get());
				State->DetainedAt = World->GetTimeSeconds();
				State->CaptiveLocation = Survivors[0]->GetActorLocation();
				TestTrue(TEXT("A genuine final mannequin catch detains a survivor instead of eliminating them"),
					Survivors[0]->IsAwaitingMultiplayerRescue() && !Survivors[0]->IsGameOver() && Station->GetDetainedSurvivor() == Survivors[0]);
				TestFalse(TEXT("The captive cannot use their own rescue panel"), Station->CanInteract_Implementation(Survivors[0]));
				TestFalse(TEXT("A distant or fabricated camera cannot request rescue"),
					Station->TryInteractFromView(Survivors[1], Station->GetActorLocation() - FVector(200,0,0), FVector::ForwardVector));
				Survivors[1]->TeleportTo(State->ReturnTrigger->GetActorLocation(), FRotator::ZeroRotator);
				TestTrue(TEXT("The exit leaves an incoming rescuer on B1 before rescue"),
					Survivors[1]->GetActorLocation().Z < 0.f);
				Survivors[1]->TeleportTo(Station->GetActorLocation() + FVector(-185,0,-34), FRotator::ZeroRotator);
				++State->Step;
				break;
			}
			break;
		case 1:
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				auto* World = Context.World();
				if (!World || World->WorldType != EWorldType::PIE || World->GetNetMode() != NM_Client) continue;
				auto* PC = World->GetFirstPlayerController();
				auto* Player = PC ? Cast<APlayerCharacter>(PC->GetPawn()) : nullptr;
				TActorIterator<AProjectMultiplayerRescueStation> Station(World);
				if (!Player || !Station || !Station->GetDetainedSurvivor() || !Station->CanInteract_Implementation(Player)
					|| !Station->GetDetainedSurvivor()->IsAwaitingMultiplayerRescue()
					|| FVector::Dist2D(Player->GetActorLocation(), Station->GetActorLocation()) > 235.f) continue;
				FVector Eye; FRotator View;
				PC->GetPlayerViewPoint(Eye, View);
				PC->SetControlRotation((Station->GetActorLocation() - Eye).Rotation());
				State->LocalRescuer = Player;
				State->LocalCaptive = Station->GetDetainedSurvivor();
				State->LocalStation = *Station;
				State->NextAt = Now + .35;
				++State->Step;
				break;
			}
			break;
		case 2:
			PressF();
			State->NextAt = Now + .28;
			++State->Step;
			break;
		case 3:
			if (State->Station.IsValid() && State->Station->IsAttemptInProgress())
			{
				PressF();
				State->NextAt = Now + .28;
				++State->Step;
			}
			break;
		case 4:
			if (State->Station.IsValid() && !State->Station->IsAttemptInProgress() && State->Station->GetAlarmCount() == 1)
			{
				TestTrue(TEXT("A mistimed attempt alarms and leaves the captive in place"),
					State->Captive.IsValid() && State->Station->GetDetainedSurvivor() == State->Captive.Get());
				PressF();
				++State->Step;
			}
			break;
		case 5:
			if (State->Station.IsValid() && State->Station->GetWorld()->GetTimeSeconds() >= State->DetainedAt + 21.)
			{
				if (!TestTrue(TEXT("An abandoned attempt alarms without eliminating the captive, even after 20 seconds"),
					State->Station->GetAlarmCount() == 2 && !State->Station->IsAttemptInProgress()
					&& State->Captive.IsValid() && !State->Captive->IsGameOver()
					&& State->Station->GetDetainedSurvivor() == State->Captive.Get()))
				{
					End();
					return true;
				}
				PressF();
				++State->Step;
			}
			break;
		case 6:
			if (State->Station.IsValid() && State->Station->IsAttemptInProgress() && State->Station->GetAttemptAge() >= .95)
			{
				PressF();
				++State->Step;
			}
			break;
		case 7:
			if (State->Station.IsValid() && State->Captive.IsValid() && !State->Station->GetDetainedSurvivor()
				&& !State->Captive->IsAwaitingMultiplayerRescue() && State->LocalCaptive.IsValid()
				&& !State->LocalCaptive->IsAwaitingMultiplayerRescue() && State->LocalStation.IsValid()
				&& State->LocalStation->IsGateOpen())
			{
				TestFalse(TEXT("Client F interaction rescues the captive without a death"), State->Captive->IsGameOver());
				TestTrue(TEXT("The rescuer's client still sees the same survivor on B1 after release"),
					!State->LocalCaptive->IsHidden() && !State->LocalCaptive->IsGameOver()
					&& State->LocalCaptive->GetActorLocation().Z < 0.f);
				TestTrue(TEXT("The rescued survivor can walk again"),
					State->Captive->GetCharacterMovement()->MovementMode == MOVE_Walking);
				TestTrue(TEXT("Rescue opens the gate and releases the captive in place"),
					State->Station->IsGateOpen() && FVector::Dist2D(State->CaptiveLocation, State->Captive->GetActorLocation()) < 4.f);
				TestTrue(TEXT("Both survivors remain on B1 after release"),
					State->Captive->GetActorLocation().Z < 0.f && State->Rescuer->GetActorLocation().Z < 0.f);
				State->Captive->TeleportTo(State->ReturnTrigger->GetActorLocation(), FRotator::ZeroRotator);
				State->NextAt = Now + .3;
				++State->Step;
			}
			break;
		case 8:
			if (State->Captive.IsValid() && State->Rescuer.IsValid())
			{
				TestTrue(TEXT("Entering the return trigger moves only that survivor to 1F"),
					State->Captive->GetActorLocation().Z > 0.f && State->Rescuer->GetActorLocation().Z < 0.f);
				State->Rescuer->SetRemainingDeathCountForGameMode(0);
				State->Rescuer->HandleMannequinCatch(State->Catcher.Get());
				TestTrue(TEXT("The station can detain the other survivor on a subsequent catch"), State->Rescuer->IsAwaitingMultiplayerRescue());
				auto* Mode = State->Station->GetWorld()->GetAuthGameMode<AProjectMultiplayerRescueGameMode>();
				TestTrue(TEXT("The first survivor can leave the match using the final exit"), Mode->MarkSurvivorEscaped(State->Captive.Get()));
				++State->Step;
			}
			break;
		case 9:
			if (State->Rescuer.IsValid() && State->Rescuer->IsGameOver())
			{
				TestTrue(TEXT("A match can resolve when the only remaining rescuer leaves"),
					State->Station.IsValid() && !State->Station->GetDetainedSurvivor() && !State->Rescuer->IsAwaitingMultiplayerRescue());
				TestEqual(TEXT("Only failed rescue attempts raise alarms"), State->Station->GetAlarmCount(), 2);
				End();
				return true;
			}
			break;
		}
		return false;
	}));
	return true;
}
#endif

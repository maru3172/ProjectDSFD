#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "MannequinAICharacter.h"
#include "Misc/AutomationTest.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "PlayerInteractionComponent.h"
#include "ProjectMultiplayerRescue.h"
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
		bool bSawLocalFall = false;
		int32 RampWaypoint = 0;
		FVector CaptiveLocation = FVector::ZeroVector;
		TWeakObjectPtr<AProjectMultiplayerRescueStation> Station;
		TWeakObjectPtr<AProjectMultiplayerRescueStation> LocalStation;
		TWeakObjectPtr<APlayerCharacter> Captive, Rescuer, LocalRescuer, LocalCaptive;
		TWeakObjectPtr<APlayerCharacter> WalkingClient;
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
		const auto FindCaptiveClient = [&]() -> APlayerCharacter*
		{
			const auto* ServerState = State->Captive.IsValid() ? State->Captive->GetPlayerState() : nullptr;
			if (!ServerState) return nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				auto* World = Context.World();
				if (!World || World->WorldType != EWorldType::PIE || World->GetNetMode() != NM_Client) continue;
				auto* PC = World->GetFirstPlayerController();
				auto* Pawn = PC ? Cast<APlayerCharacter>(PC->GetPawn()) : nullptr;
				const auto* PlayerState = Pawn ? Pawn->GetPlayerState() : nullptr;
				if (PlayerState && PlayerState->GetPlayerId() == ServerState->GetPlayerId()) return Pawn;
			}
			return nullptr;
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
				Mode->ToggleMannequinCatchDamage(Mannequin);
				State->Station = *Station;
				State->Captive = Survivors[0];
				State->Rescuer = Survivors[1];
				State->Catcher = Mannequin->GetViewedMannequin();
				const FVector CaughtAt = Survivors[0]->GetActorLocation();
				Survivors[0]->SetRemainingDeathCountForGameMode(0);
				Survivors[0]->HandleMannequinCatch(State->Catcher.Get());
				State->DetainedAt = World->GetTimeSeconds();
				TestTrue(TEXT("The capture starts on the original spot without ending the survivor's life"),
					Survivors[0]->IsAwaitingMultiplayerRescue() && !Survivors[0]->IsGameOver()
					&& !Station->GetDetainedSurvivor() && Survivors[0]->GetActorLocation().Equals(CaughtAt, 1.f));
				TestFalse(TEXT("The captive cannot use their own rescue panel"), Station->CanInteract_Implementation(Survivors[0]));
				TestFalse(TEXT("A distant or fabricated camera cannot request rescue"),
					Station->TryInteractFromView(Survivors[1], Station->GetActorLocation() - FVector(200,0,0), FVector::ForwardVector));
				Survivors[1]->TeleportTo(Station->GetActorLocation() + FVector(-185,0,-34), FRotator::ZeroRotator);
				++State->Step;
				break;
			}
			break;
		case 1:
		{
			auto* OwnPawn = FindCaptiveClient();
			auto* CaptivePC = OwnPawn ? Cast<AMultiplayTestPlayerController>(OwnPawn->GetController()) : nullptr;
			if (!CaptivePC || !State->Station.IsValid()) break;
			if (CaptivePC->GetViewTarget() == OwnPawn && OwnPawn->GetLocalDeathPresentationRemaining(CaptivePC) > .25f)
				State->bSawLocalFall = true;
			if (!State->Station->GetDetainedSurvivor() || !CaptivePC->IsFreeSpectatingTarget(CaptivePC->GetViewTarget())
				|| CaptivePC->GetViewTarget() == OwnPawn) break;
			TestTrue(TEXT("The captive plays a fall on their own camera before spectating"), State->bSawLocalFall);
			TestTrue(TEXT("The captive actually watches their teammate with movement blocked"),
				CaptivePC->IsMoveInputIgnored() && Cast<APlayerCharacter>(CaptivePC->GetViewTarget()));
			State->WalkingClient = OwnPawn;
			State->CaptiveLocation = State->Captive->GetActorLocation();
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
		}
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
				auto* PC = Cast<AMultiplayTestPlayerController>(State->WalkingClient->GetController());
				TestTrue(TEXT("A failed rescue leaves the captive watching the teammate"),
					PC && PC->GetViewTarget() != State->WalkingClient.Get() && PC->IsFreeSpectatingTarget(PC->GetViewTarget()));
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
				&& State->LocalStation->IsGateOpen() && State->WalkingClient.IsValid())
			{
				auto* PC = Cast<AMultiplayTestPlayerController>(State->WalkingClient->GetController());
				if (!PC || PC->GetViewTarget() != State->WalkingClient.Get() || PC->IsMoveInputIgnored() || PC->IsLookInputIgnored()) break;
				TestFalse(TEXT("Rescue returns the client to their own camera with no lingering death blackout"),
					State->WalkingClient->HasLocalDeathBlackout(PC));
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
				++State->Step;
			}
			break;
		case 8:
			if (State->Captive.IsValid() && State->Rescuer.IsValid() && State->Station.IsValid())
			{
				if (!State->WalkingClient.IsValid()) break;
				const FVector Route[] = {
					FVector(3840,585,-344), FVector(3510,585,-344), FVector(3260,565,-344),
					FVector(2725,550,-151), FVector(2050,550,96), FVector(1850,550,96)
				};
				const FVector Target = Route[State->RampWaypoint];
				if (FVector::Dist2D(Target, State->Captive->GetActorLocation()) > 40.f)
				{
					State->WalkingClient->AddMovementInput((Target - State->WalkingClient->GetActorLocation()).GetSafeNormal2D());
					break;
				}
				TestTrue(TEXT("The rescued survivor stays at the physical height of the ramp"),
					FMath::Abs(State->Captive->GetActorLocation().Z - Target.Z) < 62.f);
				if (++State->RampWaypoint < UE_ARRAY_COUNT(Route)) break;
				TestTrue(TEXT("Only the survivor who walks up the ramp returns to 1F"),
					State->Captive->GetActorLocation().Z > 0.f && State->Rescuer->GetActorLocation().Z < 0.f);
				State->Captive->HandleMannequinCatch(State->Catcher.Get());
				TestTrue(TEXT("A second catch after rescue is a permanent death"),
					State->Captive->IsGameOver() && !State->Captive->IsAwaitingMultiplayerRescue() && !State->Station->GetDetainedSurvivor());
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

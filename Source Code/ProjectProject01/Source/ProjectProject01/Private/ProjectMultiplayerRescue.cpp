#include "ProjectMultiplayerRescue.h"

#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "MannequinAICharacter.h"
#include "MultiplayTestPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "PlayerCharacter.h"
#include "ProjectMiniGameSound.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01SpatialAudio.h"

namespace
{
	constexpr double AttemptSeconds = 2.2;
	constexpr double TimingStart = .72;
	constexpr double TimingEnd = 1.46;
}

AProjectMultiplayerRescueStation::AProjectMultiplayerRescueStation()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	InteractionCollision->SetBoxExtent(FVector(40,48,58));
	InteractionText = NSLOCTEXT("MultiplayerRescue", "FreePartner", "동료 구출하기");
}

void AProjectMultiplayerRescueStation::BeginPlay()
{
	Super::BeginPlay();
	TArray<AActor*> Attached;
	GetAttachedActors(Attached);
	for (auto* Actor : Attached)
		if (Actor->ActorHasTag(TEXT("IntroRescueGate")))
		{
			Gate = Actor;
			ClosedGateLocation = Actor->GetActorLocation();
			OnRep_GateOpen();
			break;
		}
}

void AProjectMultiplayerRescueStation::SetGateOpen(bool bOpen)
{
	bGateOpen = bOpen;
	OnRep_GateOpen();
	ForceNetUpdate();
}

void AProjectMultiplayerRescueStation::OnRep_GateOpen()
{
	if (!Gate) return;
	Gate->SetActorEnableCollision(!bGateOpen);
	Gate->SetActorLocation(ClosedGateLocation + FVector(0,0,bGateOpen ? 270.f : 0.f));
}

void AProjectMultiplayerRescueStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProjectMultiplayerRescueStation, DetainedSurvivor);
	DOREPLIFETIME(AProjectMultiplayerRescueStation, AttemptingSurvivor);
	DOREPLIFETIME(AProjectMultiplayerRescueStation, AttemptStartedAt);
	DOREPLIFETIME(AProjectMultiplayerRescueStation, bGateOpen);
}

double AProjectMultiplayerRescueStation::GetAttemptAge() const
{
	const auto* GameState = GetWorld()->GetGameState();
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	return FMath::Max(0., Now - AttemptStartedAt);
}

FText AProjectMultiplayerRescueStation::GetInteractionText_Implementation(APawn* Interactor) const
{
	if (AttemptingSurvivor != Interactor || !IsAttemptInProgress()) return InteractionText;
	const double Age = GetAttemptAge();
	constexpr int32 Cells = 16;
	const int32 Cursor = FMath::Clamp(FMath::FloorToInt32(Age / AttemptSeconds * Cells), 0, Cells - 1);
	FString Track;
	Track.Reserve(Cells);
	for (int32 I = 0; I < Cells; ++I)
	{
		const double Time = (I + .5) * AttemptSeconds / Cells;
		Track.AppendChar(I == Cursor ? TEXT('◆') : Time >= TimingStart && Time <= TimingEnd ? TEXT('■') : TEXT('─'));
	}
	return FText::FromString(FString::Printf(TEXT("%s   %s"),
		Age >= TimingStart && Age <= TimingEnd ? TEXT("지금 누르세요") : TEXT("구출 타이밍"), *Track));
}

bool AProjectMultiplayerRescueStation::CanInteract_Implementation(APawn* Interactor) const
{
	const auto* Player = Cast<APlayerCharacter>(Interactor);
	const auto* PC = Player ? Cast<AMultiplayTestPlayerController>(Player->GetController()) : nullptr;
	if (!Super::CanInteract_Implementation(Interactor) || !Player || !PC || PC->IsServerControlledBot()
		|| PC->GetSurvivorState() != EMultiplayTestSurvivorState::Active || !DetainedSurvivor
		|| Player == DetainedSurvivor || Player->IsGameOver() || Player->IsAwaitingMultiplayerRescue()) return false;
	if (!HasAuthority()) return true;
	const auto* Mode = GetWorld()->GetAuthGameMode<AProjectMultiplayerRescueGameMode>();
	return Mode && Mode->CanFree(*this, *Player);
}

bool AProjectMultiplayerRescueStation::TryInteractFromView(APawn* Interactor, const FVector& Origin, const FVector& Direction)
{
	if (!HasAuthority() || !CanInteract_Implementation(Interactor) || Origin.ContainsNaN() || Direction.ContainsNaN()
		|| !FMath::IsNearlyEqual(Direction.SizeSquared(), 1., .02)
		|| FVector::DistSquared(Origin, Interactor->GetPawnViewLocation()) > FMath::Square(180.f)) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RescueStationUse), false, Interactor);
	FHitResult Hit;
	if (GetWorld()->LineTraceTestByChannel(Interactor->GetPawnViewLocation(), Origin, ECC_Visibility, Query)
		|| !GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * 300.f, ECC_Visibility, Query)
		|| Hit.GetActor() != this) return false;
	HandleInteraction(Interactor);
	return true;
}

void AProjectMultiplayerRescueStation::HandleInteraction(APawn* Interactor)
{
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProjectMultiplayerRescueGameMode>())
		if (auto* Player = Cast<APlayerCharacter>(Interactor)) Mode->UseStation(*this, *Player);
}

void AProjectMultiplayerRescueStation::MulticastRescueAlarm_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	auto* Alarm = NewObject<UProjectMiniGameSound>(this);
	Alarm->Kind = EProjectMiniGameSound::Bad;
	Alarm->LengthScale = 1.5f;
	Alarm->Duration = UProjectMiniGameSound::Length(Alarm->Kind) * Alarm->LengthScale;
	if (auto* Settings = UProjectProject01GameUserSettings::Get())
		Alarm->SoundClassObject = Settings->GetSFXSoundClass();
	auto* Attenuation = NewObject<USoundAttenuation>(Alarm);
	ProjectProject01SpatialAudio::ApplyWorldSFXSettings(Attenuation->Attenuation);
	UGameplayStatics::PlaySoundAtLocation(this, Alarm, GetActorLocation(), GetActorRotation(), 1.f, 1.5f, 0.f, Attenuation);
}

void AProjectMultiplayerRescueGameMode::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<AProjectMultiplayerRescueStation> It(GetWorld()); It; ++It)
	{
		Station = *It;
		break;
	}
}

bool AProjectMultiplayerRescueGameMode::TryDetain(APlayerCharacter& Survivor, AMannequinAICharacter& Catcher)
{
	if (!Station || !Station->Gate || PendingDetention.IsValid() || Station->DetainedSurvivor
		|| !IsAuthoritativeMatchInProgress() || !IsRegisteredActiveSurvivor(&Survivor)
		|| PreviouslyDetained.Contains(&Survivor)) return false;
	auto* CaptiveController = Cast<AMultiplayTestPlayerController>(Survivor.GetController());
	if (!CaptiveController || CaptiveController->IsServerControlledBot() || !HasOtherHumanSurvivorInPlay(CaptiveController)
		|| !FindLivingSurvivorSpectatorTarget(CaptiveController, 1)) return false;
	CaptureOrigin = Catcher.GetActorLocation();
	LastCatcher = &Catcher;
	PreviouslyDetained.Add(&Survivor);
	PendingDetention = &Survivor;
	Survivor.SetAwaitingMultiplayerRescue(true);
	float Pitch = 0.f, Roll = 0.f;
	Survivor.ResolveFallDirection(CaptureOrigin, Pitch, Roll);
	Survivor.MulticastPlayPainAndDeathWorldSFX(true, .85f);
	Survivor.ClientPlayRescueFall(Pitch, Roll);
	GetWorldTimerManager().SetTimer(CaptureHandoffTimer, this,
		&AProjectMultiplayerRescueGameMode::FinishCapturePresentation, Survivor.GetDeathSequenceHandoffDelaySeconds(), false);
	NotifyMannequinCaptureFeedback(false);
	return true;
}

void AProjectMultiplayerRescueGameMode::FinishCapturePresentation()
{
	auto* Survivor = PendingDetention.Get();
	PendingDetention.Reset();
	if (!IsValid(Survivor)) return;
	if (!IsValid(Station) || !IsAuthoritativeMatchInProgress())
	{
		Survivor->SetAwaitingMultiplayerRescue(false);
		return;
	}
	auto* CaptiveController = Cast<AMultiplayTestPlayerController>(Survivor->GetController());
	auto* Teammate = CaptiveController ? FindLivingSurvivorSpectatorTarget(CaptiveController, 1) : nullptr;
	const FRotator Facing = Station->GetActorRotation();
	const FVector Destination = Station->GetActorTransform().TransformPositionNoScale(Station->DetentionOffset);
	if (!Teammate || !HasOtherHumanSurvivorInPlay(CaptiveController) || !Survivor->TeleportTo(Destination, Facing))
	{
		Survivor->SetAwaitingMultiplayerRescue(false);
		float Pitch = 0.f, Roll = 0.f;
		Survivor->ResolveFallDirection(CaptureOrigin, Pitch, Roll);
		Survivor->FinishMannequinCatch(LastCatcher.Get(), Pitch, Roll);
		return;
	}
	Station->SetGateOpen(false);
	Station->DetainedSurvivor = Survivor;
	Station->ForceNetUpdate();
	CaptiveController->EnterSurvivorSpectator(Teammate, true);
	CaptiveController->ClientShowAffixNotice(TEXT("붙잡혔습니다. 동료의 구출을 기다리세요."), 4.f);
	GetWorldTimerManager().SetTimer(RescuerCheckTimer, this,
		&AProjectMultiplayerRescueGameMode::ResolveDetentionWithoutRescuer, .25f, true);
	if (auto* PC = Cast<AMultiplayTestPlayerController>(Teammate->GetController()); PC && !PC->IsServerControlledBot())
		PC->ClientShowAffixNotice(TEXT("동료가 붙잡혔습니다. 감금 지점에서 구출할 수 있습니다."), 4.f);
}

bool AProjectMultiplayerRescueGameMode::CanFree(const AProjectMultiplayerRescueStation& Source, const APlayerCharacter& Rescuer) const
{
	return HasAuthority() && IsAuthoritativeMatchInProgress() && &Source == Station && Source.DetainedSurvivor
		&& Source.DetainedSurvivor != &Rescuer && Source.DetainedSurvivor->IsAwaitingMultiplayerRescue()
		&& !Rescuer.IsGameOver() && IsRegisteredActiveSurvivor(&Rescuer);
}

void AProjectMultiplayerRescueGameMode::UseStation(AProjectMultiplayerRescueStation& Source, APlayerCharacter& Rescuer)
{
	if (!CanFree(Source, Rescuer)) return;
	if (!Source.AttemptingSurvivor)
	{
		Source.AttemptingSurvivor = &Rescuer;
		Source.AttemptStartedAt = GetWorld()->GetTimeSeconds();
		Source.ForceNetUpdate();
		GetWorldTimerManager().SetTimer(AttemptTimer, this, &AProjectMultiplayerRescueGameMode::ExpireAttempt,
			static_cast<float>(AttemptSeconds));
		return;
	}
	if (Source.AttemptingSurvivor != &Rescuer) return;
	const double Age = Source.GetAttemptAge();
	ClearAttempt();
	if (Age >= TimingStart && Age <= TimingEnd) Free(Source, Rescuer);
	else RaiseAlarm(&Rescuer);
}

void AProjectMultiplayerRescueGameMode::ExpireAttempt()
{
	auto* Rescuer = Station ? Station->AttemptingSurvivor.Get() : nullptr;
	ClearAttempt();
	if (Station && Station->DetainedSurvivor && IsAuthoritativeMatchInProgress()) RaiseAlarm(Rescuer);
}

void AProjectMultiplayerRescueGameMode::ClearAttempt()
{
	GetWorldTimerManager().ClearTimer(AttemptTimer);
	if (!Station) return;
	Station->AttemptingSurvivor = nullptr;
	Station->AttemptStartedAt = 0.;
	Station->ForceNetUpdate();
}

void AProjectMultiplayerRescueGameMode::RaiseAlarm(APlayerCharacter* Rescuer)
{
	if (!Station) return;
	++Station->AlarmCount;
	Station->MulticastRescueAlarm();
	BroadcastMatchNotice(TEXT("감금 구역에서 구출 경보가 울렸습니다."), true, 3.f);
	if (Rescuer)
		if (auto* PC = Cast<AMultiplayTestPlayerController>(Rescuer->GetController()))
			PC->ClientShowAffixNotice(TEXT("구출 실패"), 2.f);
}

void AProjectMultiplayerRescueGameMode::Free(AProjectMultiplayerRescueStation& Source, APlayerCharacter& Rescuer)
{
	if (!CanFree(Source, Rescuer)) return;
	auto* Captive = Source.DetainedSurvivor.Get();
	ClearDetention();
	Source.SetGateOpen(true);
	Captive->SetAwaitingMultiplayerRescue(false, true);
	if (auto* Record = FindSurvivorRecord(&Rescuer)) ++Record->RescueCount;
	if (auto* PC = Cast<AMultiplayTestPlayerController>(Rescuer.GetController()))
		PC->ClientShowAffixNotice(TEXT("동료를 구출했습니다."), 3.f);
	if (auto* PC = Cast<AMultiplayTestPlayerController>(Captive->GetController()))
	{
		PC->ResumeAfterRescue(Source.GetActorRotation());
		PC->ClientShowAffixNotice(TEXT("구출되었습니다. 통로를 따라 1층으로 돌아가세요."), 4.f);
	}
}

void AProjectMultiplayerRescueGameMode::ClearDetention()
{
	GetWorldTimerManager().ClearTimer(RescuerCheckTimer);
	ClearAttempt();
	if (!Station) return;
	Station->DetainedSurvivor = nullptr;
	Station->ForceNetUpdate();
}

void AProjectMultiplayerRescueGameMode::ResolveDetentionWithoutRescuer()
{
	auto* Captive = Station ? Station->DetainedSurvivor.Get() : nullptr;
	if (IsValid(Captive) && IsAuthoritativeMatchInProgress()
		&& HasOtherHumanSurvivorInPlay(Cast<AMultiplayTestPlayerController>(Captive->GetController()))) return;
	ClearDetention();
	if (!IsValid(Captive)) return;
	Captive->SetAwaitingMultiplayerRescue(false);
	if (!IsAuthoritativeMatchInProgress() || !IsRegisteredActiveSurvivor(Captive)) return;
	float Pitch = 0.f, Roll = 0.f;
	Captive->ResolveFallDirection(CaptureOrigin, Pitch, Roll);
	Captive->FinishMannequinCatch(LastCatcher.Get(), Pitch, Roll);
}

void AProjectMultiplayerRescueGameMode::Logout(AController* Exiting)
{
	if (auto* Pending = PendingDetention.Get(); Pending && Pending->GetController() == Exiting)
	{
		GetWorldTimerManager().ClearTimer(CaptureHandoffTimer);
		Pending->SetAwaitingMultiplayerRescue(false);
		PendingDetention.Reset();
	}
	if (Station && Station->DetainedSurvivor && Station->DetainedSurvivor->GetController() == Exiting)
	{
		Station->DetainedSurvivor->SetAwaitingMultiplayerRescue(false);
		ClearDetention();
	}
	Super::Logout(Exiting);
}

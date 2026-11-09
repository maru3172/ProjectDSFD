#include "ProjectSingleIntro.h"

#include "Animation/AnimSequence.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/CameraActor.h"
#include "Components/BoxComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TriggerBox.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PlayerHeartbeatComponent.h"
#include "PlayerInteractionComponent.h"
#include "ProjectDFSDBranding.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectUIClickSound.h"
#include "Sound/SoundWaveProcedural.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FName HatAction(TEXT("Hat"));
	const FName PurchaseAction(TEXT("Purchase"));
	const FName PartnerAction(TEXT("Partner"));
	const FName MannequinAction(TEXT("Mannequin"));
	const FName ShutterAction(TEXT("Shutter"));
	const FName FinaleFigureAction(TEXT("FinaleFigure"));
	constexpr float PhoneDuration = 8.5f;
	constexpr float OpeningPullbackAt = 1.85f;
	constexpr float OpeningWideAt = 3.90f;
	constexpr float OpeningHandoffAt = 6.65f;
	constexpr float OpeningCameraBlendSeconds = .24f;
	constexpr float OpeningPhoneUpAt = 1.55f;
	constexpr float OpeningPhoneOffAt = 6.7f;
	constexpr float FailedPhoneRaiseAt = 4.25f;
	constexpr float FailedPhoneFirstPress = 4.95f;
	constexpr float FailedPhoneSecondPress = 5.63f;
	constexpr float FailedPhoneLowerAt = 6.9f;
	constexpr float FireEndsAt = 7.35f;
	constexpr float EncounterDelaySeconds = 5.f;
	constexpr float EncounterHoldSeconds = .90f;
	constexpr float EncounterBlendSeconds = .45f;
	constexpr float FinaleOpenSeconds = 2.f;
	constexpr float FirstOutageAt = 3.65f;
	constexpr float LastLightHold = 2.2f;
	constexpr float AfterglowSeconds = .75f;
	constexpr float AfterglowIntensity = .035f;
	constexpr float TitleFadeInAt = .45f;
	constexpr float TitleFadeInEndsAt = 2.2f;
	constexpr float TitleFadeOutAt = 3.65f;
	constexpr float TitleFadeOutEndsAt = 5.45f;
}

AProjectSingleIntroTarget::AProjectSingleIntroTarget()
{
	CharacterMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PrototypeCharacter"));
	CharacterMesh->SetupAttachment(SceneRoot);
	CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AProjectSingleIntroTarget::BeginPlay()
{
	Super::BeginPlay();
	if (IdleAnimation) CharacterMesh->PlayAnimation(IdleAnimation, true);
	// A mannequin is still visible to the player but must not intercept the partner's interaction trace.
	if (Action == MannequinAction || Action == FinaleFigureAction)
		InteractionCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Action == FinaleFigureAction) CharacterMesh->bPauseAnims = true;
}

bool AProjectSingleIntroTarget::CanInteract_Implementation(APawn* Interactor) const
{
	const auto* Mode = GetWorld()->GetAuthGameMode<AProjectSingleIntroGameMode>();
	return Super::CanInteract_Implementation(Interactor) && Mode && Mode->CanUse(*this, Interactor);
}

FText AProjectSingleIntroTarget::GetInteractionText_Implementation(APawn*) const
{
	const auto* Mode = GetWorld()->GetAuthGameMode<AProjectSingleIntroGameMode>();
	return Mode ? Mode->GetUseText(*this) : ItemName;
}

void AProjectSingleIntroTarget::HandleInteraction(APawn* Interactor)
{
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProjectSingleIntroGameMode>())
		Mode->Use(*this, Interactor);
}

void AProjectSingleIntroTarget::SetWalking(bool bNewWalking)
{
	if (bWalking == bNewWalking) return;
	bWalking = bNewWalking;
	if (auto* Animation = bWalking && WalkAnimation ? WalkAnimation.Get() : IdleAnimation.Get())
		CharacterMesh->PlayAnimation(Animation, true);
}

void AProjectSingleIntroTarget::MoveToward(const FVector& Destination, float Speed, float DeltaSeconds)
{
	const FVector Position = GetActorLocation();
	const FVector Direction(Destination.X - Position.X, Destination.Y - Position.Y, 0);
	if (Direction.SizeSquared() <= FMath::Square(12.f))
	{
		SetWalking(false);
		return;
	}
	SetActorRotation(Direction.Rotation());
	SetActorLocation(FMath::VInterpConstantTo(Position, Position + Direction, DeltaSeconds, Speed));
	SetWalking(true);
}

AProjectSingleIntroGameMode::AProjectSingleIntroGameMode()
{
	bSpawnAutomaticHelper = false;
	PrimaryActorTick.bCanEverTick = true;
	static ConstructorHelpers::FClassFinder<APawn> PlayerClass(TEXT("/Game/MyProject/BP_PlayerCharacter"));
	if (PlayerClass.Succeeded()) DefaultPawnClass = PlayerClass.Class;
}

void AProjectSingleIntroGameMode::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<AProjectSingleIntroTarget> It(GetWorld()); It; ++It)
	{
		if (It->Action == HatAction) Hat = *It;
		if (It->Action == PartnerAction) Partner = *It;
		if (It->Action == MannequinAction) Mannequin = *It;
		if (It->Action == ShutterAction) ShutterHandle = *It;
		if (It->Action == FinaleFigureAction) FinaleCrowd.Add(*It);
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("IntroShutter"))) Shutter = *It;
		if (It->ActorHasTag(TEXT("IntroOpeningDestination"))) OpeningDestination = *It;
		if (It->ActorHasTag(TEXT("IntroOpeningCamera"))) OpeningCamera = Cast<ACameraActor>(*It);
		if (It->ActorHasTag(TEXT("IntroOpeningCameraEnd"))) OpeningCameraEnd = *It;
		if (It->ActorHasTag(TEXT("IntroEncounterCamera"))) EncounterCamera = Cast<ACameraActor>(*It);
		if (It->ActorHasTag(TEXT("IntroFinaleCamera"))) FinaleCamera = Cast<ACameraActor>(*It);
		if (It->ActorHasTag(TEXT("IntroFinaleShoppingSign"))) FinaleShoppingSigns.Add(*It);
		if (It->ActorHasTag(TEXT("IntroFinaleLight")))
			if (auto* Light = It->FindComponentByClass<ULightComponent>()) FinaleLights.Add(Light);
		if (It->ActorHasTag(TEXT("IntroHatCounter"))) HatCounter = Cast<ATriggerBox>(*It);
		if (It->ActorHasTag(TEXT("IntroMannequinPass"))) Passage = Cast<ATriggerBox>(*It);
	}
	if (!ensureMsgf(Hat && Partner && Mannequin && Shutter && OpeningDestination && ShutterHandle && HatCounter && Passage
		&& FinaleCamera && FinaleLights.Num() >= 2 && FinaleCrowd.Num() >= 2,
		TEXT("Single intro needs its shop, two encounter triggers and a populated finale corridor")))
	{
		SetActorTickEnabled(false);
		return;
	}
	HatStart = Hat->GetActorLocation();
	ShutterOpen = Shutter->GetActorLocation();
	PassageStart = Passage->GetActorLocation();
	FinaleLights.Sort([this](const ULightComponent& A, const ULightComponent& B)
	{
		return FVector::DistSquared2D(A.GetComponentLocation(), ShutterOpen)
			> FVector::DistSquared2D(B.GetComponentLocation(), ShutterOpen);
	});
	for (const auto& Light : FinaleLights)
	{
		FinaleLightIntensities.Add(Light->Intensity);
		TArray<AActor*> Attached;
		Light->GetOwner()->GetAttachedActors(Attached);
		AActor** Diffuser = Attached.FindByPredicate([](const AActor* Actor)
		{
			return Actor->ActorHasTag(TEXT("IntroFinaleDiffuser"));
		});
		FinaleDiffusers.Add(Diffuser ? *Diffuser : nullptr);
	}
	FinaleOutageTimes.Add(FirstOutageAt);
	const int32 LastIndex = FinaleLights.Num() - 1;
	for (int32 I = 1; I < LastIndex; ++I)
	{
		const float Progress = static_cast<float>(I - 1) / FMath::Max(1, LastIndex - 2);
		FinaleOutageTimes.Add(FinaleOutageTimes.Last() + FMath::Lerp(1.10f, .30f, Progress));
	}
	FinaleOutageTimes.Add(FinaleOutageTimes.Last() + LastLightHold);
	for (const auto& Figure : FinaleCrowd) Figure->SetActorHiddenInGame(true);
	HatCounter->OnActorBeginOverlap.AddUniqueDynamic(this, &AProjectSingleIntroGameMode::OnReachHatCounter);
	Passage->OnActorBeginOverlap.AddUniqueDynamic(this, &AProjectSingleIntroGameMode::OnPassMannequin);
	PartnerVoice = NewObject<UPainGroanSynthComponent>(Partner);
	PartnerVoice->SetupAttachment(Partner->GetRootComponent());
	PartnerVoice->SetVolumeMultiplier(.8f);
	if (auto* Settings = UProjectProject01GameUserSettings::Get())
		PartnerVoice->SoundClass = Settings->GetSFXSoundClass();
	PartnerVoice->RegisterComponent();
	PhoneClick = NewObject<UProjectUIClickSound>(this);
	if (auto* Settings = UProjectProject01GameUserSettings::Get())
		PhoneClick->SoundClassObject = Settings->GetSFXSoundClass();
	BuildOverlay();
}

void AProjectSingleIntroGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	LockControls(false);
	if (auto* Viewport = AttachedViewport.Get(); Viewport && Overlay)
		Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	Overlay.Reset();
	Super::EndPlay(Reason);
}

bool AProjectSingleIntroGameMode::CanUse(const AProjectSingleIntroTarget& Target, const APawn* Player) const
{
	if (!Controller.IsValid() || Player != Controller->GetPawn()) return false;
	if (Phase == EPhase::Shopping)
		return &Target == Hat || (Target.Action == PurchaseAction && !PickedItems.Contains(Target.ItemId));
	if (&Target == Partner)
		return Phase == EPhase::Meet || (Phase == EPhase::Captured && IsMannequinObserved());
	return &Target == ShutterHandle && Phase == EPhase::ReadyToLeave;
}

FText AProjectSingleIntroGameMode::GetUseText(const AProjectSingleIntroTarget& Target) const
{
	if (&Target == Partner)
		return FText::FromString(Phase == EPhase::Captured ? TEXT("파트너 구출하기") : TEXT("말 걸기"));
	return Target.ItemName;
}

void AProjectSingleIntroGameMode::Use(AProjectSingleIntroTarget& Target, APawn* Player)
{
	if (!CanUse(Target, Player)) return;
	if (Target.Action == PurchaseAction)
	{
		PickedItems.Add(Target.ItemId);
		Target.SetActorHiddenInGame(true);
		Target.SetActorEnableCollision(false);
	}
	else if (&Target == Hat) Enter(EPhase::InspectHat);
	else if (Phase == EPhase::Meet) Enter(EPhase::Talk);
	else if (Phase == EPhase::Captured) Enter(EPhase::AfterRescue);
	else if (&Target == ShutterHandle) Enter(EPhase::Finale);
}

void AProjectSingleIntroGameMode::LockControls(bool bLock)
{
	if (!Controller.IsValid() || bControlsLocked == bLock) return;
	bControlsLocked = bLock;
	Controller->SetIgnoreMoveInput(bLock);
	Controller->SetIgnoreLookInput(bLock);
	if (bLock && Controller->GetPawn()) Controller->GetPawn()->ConsumeMovementInputVector();
}

void AProjectSingleIntroGameMode::Enter(EPhase Next)
{
	Phase = Next;
	Elapsed = 0.f;
	if (Controller.IsValid()) TurnStart = Controller->GetControlRotation();
	const bool bCutscene = Phase == EPhase::Phone || Phase == EPhase::InspectHat || Phase == EPhase::Fire
		|| Phase == EPhase::Talk || Phase == EPhase::Agree || Phase == EPhase::NoticeMannequin
		|| Phase == EPhase::AfterRescue
		|| Phase == EPhase::Finale || Phase == EPhase::TitleCard;
	LockControls(bCutscene);
	if (Phase == EPhase::Phone)
	{
		APawn* Pawn = Controller->GetPawn();
		OpeningYaw = (OpeningDestination->GetActorLocation() - Pawn->GetActorLocation()).Rotation().Yaw;
		if (OpeningCamera && OpeningCameraEnd)
		{
			OpeningCameraStart = OpeningCamera->GetActorTransform();
			Controller->SetViewTarget(OpeningCamera);
		}
		if (auto* Player = Cast<ACharacter>(Pawn))
		{
			WalkSpeedBeforeIntro = Player->GetCharacterMovement()->MaxWalkSpeed;
			Player->GetCharacterMovement()->MaxWalkSpeed = 248.f;
		}
	}
	if (Phase == EPhase::NoticeMannequin && EncounterCamera)
		Controller->SetViewTargetWithBlend(EncounterCamera, .2f, VTBlend_Cubic, 0.f, true);
	if (Phase == EPhase::Shopping)
	{
		if (auto* Player = Cast<ACharacter>(Controller->GetPawn()))
		{
			Player->GetCharacterMovement()->MaxWalkSpeed = WalkSpeedBeforeIntro;
			Player->GetCharacterMovement()->StopMovementImmediately();
		}
	}
	if (Phase == EPhase::Fire)
	{
		Hat->SetActorHiddenInGame(true);
		Hat->SetActorEnableCollision(false);
	}
	if (Phase == EPhase::Captured && PartnerVoice) PartnerVoice->TriggerPain(true, .78f);
	if (Phase == EPhase::AfterRescue)
	{
		Mannequin->SetWalking(false);
		Partner->SetWalking(false);
	}
	if (Phase == EPhase::Finale)
	{
		Partner->SetWalking(false);
		for (const auto& Figure : FinaleCrowd) Figure->SetActorHiddenInGame(false);
		for (const auto& Sign : FinaleShoppingSigns) Sign->SetActorHiddenInGame(true);
		Controller->SetViewTargetWithBlend(FinaleCamera, .7f, VTBlend_Cubic);
	}
}

void AProjectSingleIntroGameMode::TurnToward(const FVector& Point, float Blend)
{
	FVector ViewOrigin;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewOrigin, ViewRotation);
	const FRotator Aim = (Point - ViewOrigin).Rotation();
	Controller->SetControlRotation(FQuat::Slerp(TurnStart.Quaternion(), Aim.Quaternion(),
		FMath::SmoothStep(0.f, 1.f, Blend)).Rotator());
}

void AProjectSingleIntroGameMode::OnPassMannequin(AActor*, AActor* Other)
{
	if (Phase == EPhase::WalkToShutter && Controller.IsValid() && Other == Controller->GetPawn())
	{
		EncounterAt = GetWorld()->GetTimeSeconds();
		Partner->SetWalking(false);
		Enter(EPhase::NoticeMannequin);
	}
}

void AProjectSingleIntroGameMode::WalkIntoStore(APawn& Player)
{
	const FVector ToDoor = OpeningDestination->GetActorLocation() - Player.GetActorLocation();
	const float Distance = ToDoor.Size2D();
	if (Distance > 24.f)
		Player.AddMovementInput(ToDoor.GetSafeNormal2D(), FMath::Clamp(Distance / 94.f, .28f, 1.f), true);
	const float Lower = FMath::SmoothStep(OpeningPhoneUpAt, OpeningPhoneUpAt + .63f, Elapsed);
	const float Raise = FMath::SmoothStep(OpeningPhoneOffAt + .12f, PhoneDuration - .12f, Elapsed);
	Controller->SetControlRotation(FRotator(FMath::Lerp(FMath::Lerp(12.f, -16.f, Lower), 0.f, Raise), OpeningYaw, 0.f));
}

void AProjectSingleIntroGameMode::OnReachHatCounter(AActor*, AActor* Other)
{
	if (Phase == EPhase::Shopping && Controller.IsValid() && Other == Controller->GetPawn())
		Enter(EPhase::InspectHat);
}

void AProjectSingleIntroGameMode::FollowPartner(const APawn& Player, float DeltaSeconds)
{
	const FVector Position = Player.GetActorLocation();
	const FVector FromPlayer = (Partner->GetActorLocation() - Position).GetSafeNormal2D();
	Partner->MoveToward(Position + FromPlayer * 155.f, 500.f, DeltaSeconds);
}

bool AProjectSingleIntroGameMode::IsMannequinObserved() const
{
	if (!Controller.IsValid() || !Mannequin) return false;
	FVector Eye;
	FRotator View;
	Controller->GetPlayerViewPoint(Eye, View);
	const FVector Target = Mannequin->GetActorLocation() + FVector(0, 0, 125);
	if (FVector::DotProduct(View.Vector(), (Target - Eye).GetSafeNormal()) < .78) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IntroMannequinSight), false, Controller->GetPawn());
	Query.AddIgnoredActor(Partner);
	return !GetWorld()->LineTraceTestByChannel(Eye, Target, ECC_Visibility, Query);
}

float AProjectSingleIntroGameMode::GetLastOutageTime() const
{
	return FinaleOutageTimes.Last();
}

void AProjectSingleIntroGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase == EPhase::Waiting)
	{
		auto* PlayerController = GetWorld()->GetFirstPlayerController();
		if (!PlayerController || !PlayerController->IsLocalController() || !PlayerController->GetPawn()) return;
		Controller = PlayerController;
		Interaction = Controller->GetPawn()->FindComponentByClass<UPlayerInteractionComponent>();
		if (Interaction) Interaction->UseExternalPrompt();
		Enter(EPhase::Phone);
	}
	if (!Controller.IsValid() || !Controller->GetPawn()) return;
	auto& Player = *Controller->GetPawn();
	const float Previous = Elapsed;
	Elapsed += DeltaSeconds;
	switch (Phase)
	{
	case EPhase::Phone:
		WalkIntoStore(Player);
		if (OpeningCamera && OpeningCameraEnd && Previous < OpeningHandoffAt)
		{
			const FVector Forward = FRotator(0.f, OpeningYaw, 0.f).Vector();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
			const FVector Position = Player.GetActorLocation();
			const FVector TrackingPosition = Position - Forward * 270.f + Right * 200.f + FVector(0, 0, -18);
			const FVector TrackingFocus = Position + Forward * 165.f + FVector(0, 0, 65);
			const FTransform Tracking((TrackingFocus - TrackingPosition).Rotation(), TrackingPosition);
			FTransform Wide;
			Wide.Blend(OpeningCameraStart, OpeningCameraEnd->GetActorTransform(),
				FMath::SmoothStep(OpeningWideAt, OpeningHandoffAt, Elapsed));
			FTransform Shot;
			Shot.Blend(Tracking, Wide, FMath::SmoothStep(OpeningPullbackAt, OpeningWideAt, Elapsed));
			OpeningCamera->SetActorTransform(Shot);
			if (Elapsed >= OpeningHandoffAt)
				Controller->SetViewTargetWithBlend(&Player, OpeningCameraBlendSeconds, VTBlend_Cubic);
		}
		if (Elapsed >= PhoneDuration)
		{
			Enter(EPhase::Shopping);
			if (HatCounter->IsOverlappingActor(&Player)) OnReachHatCounter(HatCounter, &Player);
		}
		break;
	case EPhase::InspectHat:
		TurnToward(HatStart, Elapsed * 2.f);
		Hat->SetActorLocation(HatStart + FVector(0, 0, 26 * FMath::Sin(FMath::Min(Elapsed, 2.4f) / 2.4f * PI)));
		Hat->AddActorLocalRotation(FRotator(0, DeltaSeconds * 36.f, 0));
		if (Previous < .6f && Elapsed >= .6f) PlayImpact(ShutterOpen, .83f, .35f);
		if (Previous < 1.05f && Elapsed >= 1.05f) PlayImpact(ShutterOpen, .71f, .62f);
		if (Previous < 1.48f && Elapsed >= 1.48f) PlayImpact(ShutterOpen, .58f, .95f);
		if (Elapsed >= 2.4f) Enter(EPhase::Fire);
		break;
	case EPhase::Fire:
	{
		const float Lowered = FMath::SmoothStep(.38f, 2.1f, Elapsed);
		const FVector Door = ShutterOpen - FVector(0, 0, ShutterDrop);
		TurnToward(Door, Elapsed / .85f);
		Shutter->SetActorLocation(FMath::Lerp(ShutterOpen, Door, Lowered));
		if (Previous < 2.1f && Elapsed >= 2.1f)
		{
			PlayImpact(Door, .5f, 1.f);
			PlayAlarm();
		}
		if ((Previous < FailedPhoneFirstPress && Elapsed >= FailedPhoneFirstPress)
			|| (Previous < FailedPhoneSecondPress && Elapsed >= FailedPhoneSecondPress))
			UGameplayStatics::PlaySound2D(this, PhoneClick, .23f, 1.2f);
		if (Elapsed >= FireEndsAt) Enter(EPhase::Meet);
		break;
	}
	case EPhase::Talk:
		TurnToward(Partner->GetActorLocation() + FVector(0, 0, 148), Elapsed * 2.f);
		if (Elapsed >= 3.35f) Enter(EPhase::LeaveTogether);
		break;
	case EPhase::LeaveTogether:
		if (FVector::DistSquared2D(Partner->GetActorLocation(), Player.GetActorLocation()) > FMath::Square(460.f))
			Enter(EPhase::Agree);
		break;
	case EPhase::Agree:
		TurnToward(Partner->GetActorLocation() + FVector(0, 0, 148), Elapsed * 1.4f);
		if (Elapsed > .6f) FollowPartner(Player, DeltaSeconds);
		if (Elapsed >= 2.4f) Enter(EPhase::WalkToShutter);
		break;
	case EPhase::WalkToShutter:
		FollowPartner(Player, DeltaSeconds);
		break;
	case EPhase::NoticeMannequin:
		TurnToward(Mannequin->GetActorLocation() + FVector(0,0,142), Elapsed / .38f);
		if (EncounterCamera && Previous < EncounterHoldSeconds && Elapsed >= EncounterHoldSeconds)
			Controller->SetViewTargetWithBlend(&Player, EncounterBlendSeconds, VTBlend_Cubic);
		if (Elapsed >= EncounterHoldSeconds + EncounterBlendSeconds) Enter(EPhase::PassedMannequin);
		break;
	case EPhase::PassedMannequin:
		FollowPartner(Player, DeltaSeconds);
		if (GetWorld()->GetTimeSeconds() >= EncounterAt + EncounterDelaySeconds
			&& FVector::DotProduct(Player.GetActorLocation() - PassageStart, Passage->GetActorForwardVector()) > 280.f)
		{
			Partner->SetWalking(false);
			Enter(EPhase::ApproachPartner);
		}
		break;
	case EPhase::ApproachPartner:
		if (IsMannequinObserved()) Mannequin->SetWalking(false);
		else Mannequin->MoveToward(Partner->GetActorLocation(), 410.f, DeltaSeconds);
		if (FVector::DistSquared2D(Mannequin->GetActorLocation(), Partner->GetActorLocation()) < FMath::Square(62.f))
		{
			Mannequin->SetWalking(false);
			Partner->SetActorRotation(FRotator(-24.f, Partner->GetActorRotation().Yaw, 0.f));
			PlayImpact(Partner->GetActorLocation(), .76f, .75f);
			Enter(EPhase::Captured);
		}
		break;
	case EPhase::AfterRescue:
		Partner->SetActorRotation(FRotator(FMath::Lerp(-24.f, 0.f, FMath::Clamp(Elapsed * 2.f, 0.f, 1.f)),
			Partner->GetActorRotation().Yaw, 0.f));
		if (Elapsed >= 4.6f) Enter(EPhase::ReadyToLeave);
		break;
	case EPhase::ReadyToLeave:
		FollowPartner(Player, DeltaSeconds);
		break;
	case EPhase::Finale:
	{
		const FVector Closed = ShutterOpen - FVector(0,0,ShutterDrop);
		Shutter->SetActorLocation(FMath::Lerp(Closed, ShutterOpen, FMath::SmoothStep(0.f, FinaleOpenSeconds, Elapsed)));
		while (NextFinaleLight < FinaleLights.Num() && Elapsed >= FinaleOutageTimes[NextFinaleLight])
		{
			const int32 LightIndex = NextFinaleLight++;
			PlayImpact(FinaleLights[LightIndex]->GetComponentLocation(), 1.55f, .24f);
			if (auto* Point = Cast<UPointLightComponent>(FinaleLights[LightIndex]))
				Point->SetAttenuationRadius(FMath::Min(Point->AttenuationRadius, 345.f));
			if (auto* Diffuser = FinaleDiffusers[LightIndex].Get()) Diffuser->SetActorHiddenInGame(true);
		}
		for (int32 Index = 0; Index < NextFinaleLight; ++Index)
		{
			auto* Light = FinaleLights[Index].Get();
			if (!Light->IsVisible()) continue;
			const float Age = Elapsed - FinaleOutageTimes[Index];
			if (Age >= AfterglowSeconds) Light->SetVisibility(false);
			else Light->SetIntensity(FinaleLightIntensities[Index] * AfterglowIntensity
				* (1.f - FMath::SmoothStep(0.f, AfterglowSeconds, Age)));
		}
		if (Elapsed >= GetLastOutageTime() + AfterglowSeconds + .45f) Enter(EPhase::TitleCard);
		break;
	}
	case EPhase::TitleCard:
		if (Elapsed >= TitleFadeOutEndsAt + .28f)
		{
			if (auto* Game = Cast<UProjectProject01GameInstance>(GetGameInstance()))
				Game->ConsumeStartupIntro();
			SetActorTickEnabled(false);
			UGameplayStatics::OpenLevel(this, TEXT("/Game/MyProject/Level/TitleLevel"));
		}
		break;
	default:
		break;
	}
}

void AProjectSingleIntroGameMode::PlayImpact(const FVector& Position, float Pitch, float Volume) const
{
	static const TCHAR* Path = TEXT("/Game/MyProject/Audio/SFX/Footsteps/Mannequin/Walk/SFX_Mannequin_Footstep_01.SFX_Mannequin_Footstep_01");
	if (auto* Sound = LoadObject<USoundBase>(nullptr, Path))
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Position, Volume, Pitch);
}

void AProjectSingleIntroGameMode::PlayAlarm()
{
	constexpr int32 Rate = 24000;
	TArray<int16> Samples;
	Samples.SetNumUninitialized(Rate * 3);
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const float Time = static_cast<float>(Index) / Rate;
		const float Pulse = FMath::Fmod(Time, .72f);
		const float Gain = FMath::SmoothStep(0.f, .065f, Pulse) * (1.f - FMath::SmoothStep(.32f, .52f, Pulse));
		const float WavePhase = 2.f * PI * (710.f * Time + 10.f * FMath::Sin(4.f * Time));
		Samples[Index] = static_cast<int16>(3200.f * Gain * (.8f * FMath::Sin(WavePhase) + .2f * FMath::Sin(2.f * WavePhase)));
	}
	Alarm = NewObject<USoundWaveProcedural>(this);
	Alarm->SetSampleRate(Rate);
	Alarm->NumChannels = 1;
	Alarm->Duration = 3.f;
	if (auto* Settings = UProjectProject01GameUserSettings::Get())
		Alarm->SoundClassObject = Settings->GetSFXSoundClass();
	Alarm->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
	UGameplayStatics::PlaySoundAtLocation(this, Alarm, ShutterOpen);
}

FText AProjectSingleIntroGameMode::GetSubtitle() const
{
	const TCHAR* Line = TEXT("");
	switch (Phase)
	{
	case EPhase::InspectHat:
		if (Elapsed > 1.4f) Line = TEXT("무슨 소리지?");
		break;
	case EPhase::Fire:
		if (Elapsed >= 2.1f && Elapsed < FailedPhoneRaiseAt)
			Line = TEXT("화재가 발생했습니다. 가까운 비상구로 대피하십시오.");
		else if (Elapsed >= FailedPhoneSecondPress + .16f)
			Line = TEXT("전원이 안 들어와… 연락도 못 하겠네.");
		break;
	case EPhase::Meet:
		if (Elapsed < 1.45f) Line = TEXT("저 사람한테 물어볼까.");
		break;
	case EPhase::Talk:
		Line = Elapsed < 1.65f ? TEXT("저기요, 혹시 폰 켜져요?")
			: TEXT("아뇨. 제 것도 안 켜져요.");
		break;
	case EPhase::LeaveTogether:
		if (Elapsed < 2.2f) Line = TEXT("나갈 길이 있을 텐데…");
		break;
	case EPhase::Agree:
		if (Elapsed > .35f) Line = TEXT("잠깐만요. 같이 가요.");
		break;
	case EPhase::Captured:
		if (Elapsed < 1.4f) Line = TEXT("아악!");
		break;
	case EPhase::AfterRescue:
		Line = Elapsed < 2.3f ? TEXT("뒤에서 갑자기 잡았어요. 저거… 쳐다보니까 멈췄죠?")
			: TEXT("일단 들어온 쪽 셔터를 열어 봐요.");
		break;
	default:
		break;
	}
	return FText::FromString(Line);
}

FText AProjectSingleIntroGameMode::GetShoppingList() const
{
	return FText::FromString(FString::Printf(TEXT("오늘 사야 할 것\n\n□  모자\n%s  손수건\n%s  생수"),
		PickedItems.Contains(TEXT("IntroHandkerchief")) ? TEXT("✓") : TEXT("□"),
		PickedItems.Contains(TEXT("IntroWater")) ? TEXT("✓") : TEXT("□")));
}

FText AProjectSingleIntroGameMode::GetInteractionPrompt() const
{
	if (!Interaction || !Controller.IsValid() || bControlsLocked) return FText::GetEmpty();
	const auto* Target = Cast<AProjectSingleIntroTarget>(Interaction->GetCurrentInteractable());
	return Target && CanUse(*Target, Controller->GetPawn())
		? FText::Format(FText::FromString(TEXT("F   {0}")), GetUseText(*Target)) : FText::GetEmpty();
}

void AProjectSingleIntroGameMode::BuildOverlay()
{
	auto* Viewport = GetWorld()->GetGameViewport();
	if (!Viewport) return;
	AttachedViewport = Viewport;
	const auto Label = [](int32 Size, const TCHAR* Line)
	{
		return SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", Size))
			.ColorAndOpacity(FLinearColor(.92f, .92f, .9f)).Text(FText::FromString(Line));
	};
	const auto* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
	const auto CinematicBar = [this, Brush]()
	{
		return SNew(SBox).HeightOverride_Lambda([this]() -> FOptionalSize
		{
			float Alpha = 0.f;
			if (Phase == EPhase::Phone && OpeningCamera && OpeningCameraEnd)
				Alpha = 1.f - FMath::SmoothStep(OpeningHandoffAt, OpeningHandoffAt + OpeningCameraBlendSeconds, Elapsed);
			else if (Phase == EPhase::NoticeMannequin && EncounterCamera)
				Alpha = FMath::SmoothStep(0.f, .2f, Elapsed)
					* (1.f - FMath::SmoothStep(EncounterHoldSeconds, EncounterHoldSeconds + EncounterBlendSeconds, Elapsed));
			else if (Phase == EPhase::Finale)
				Alpha = FMath::SmoothStep(0.f, .7f, Elapsed);
			return 42.f * Alpha;
		})
		[
			SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor::Black)
		];
	};
	static const FSlateRoundedBoxBrush PhoneFrame(FLinearColor(.045f,.061f,.075f), 24.f,
		FLinearColor(.13f,.15f,.17f), 2.f);
	Overlay = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(Brush)
			.Visibility_Lambda([this]{ return Phase == EPhase::Waiting || Phase == EPhase::Phone
				|| Phase == EPhase::Finale || Phase == EPhase::TitleCard
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.BorderBackgroundColor_Lambda([this]
			{
				const float DoorwayShade = OpeningCamera && OpeningCameraEnd
					? .96f * FMath::SmoothStep(OpeningHandoffAt - .90f, OpeningHandoffAt, Elapsed)
						* (1.f - FMath::SmoothStep(OpeningHandoffAt + OpeningCameraBlendSeconds, OpeningHandoffAt + .95f, Elapsed))
					: 0.f;
				const float Opacity = Phase == EPhase::Phone ? FMath::Max(DoorwayShade, 1.f - FMath::SmoothStep(0.f, .55f, Elapsed))
					: Phase == EPhase::Finale ? FMath::SmoothStep(0.f, .28f, Elapsed - GetLastOutageTime() - AfterglowSeconds) : 1.f;
				return FLinearColor(0,0,0,Opacity);
			})
		]
		+ SOverlay::Slot().VAlign(VAlign_Top)[CinematicBar()]
		+ SOverlay::Slot().VAlign(VAlign_Bottom)[CinematicBar()]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0,0,64,62)
		[
			SNew(SBox).WidthOverride(342).HeightOverride(390).Visibility_Lambda([this]{ return Phase == EPhase::Phone
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.RenderTransform_Lambda([this]() -> TOptional<FSlateRenderTransform>
			{
				const float In = FMath::SmoothStep(OpeningPhoneUpAt, OpeningPhoneUpAt + .65f, Elapsed);
				const float Out = FMath::SmoothStep(OpeningPhoneOffAt + .08f, PhoneDuration - .16f, Elapsed);
				return FSlateRenderTransform(FVector2D(0.f, (1.f - In + Out) * 600.f));
			})
			[
				SNew(SBorder).BorderImage(&PhoneFrame).Padding(FMargin(22, 26))
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SVerticalBox)
						.Visibility_Lambda([this]{ return Elapsed < OpeningPhoneOffAt
							? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,22)[Label(22, TEXT("메시지"))]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0,0,0,12)
					[
						SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(.12f,.14f,.16f))
						.Padding(FMargin(10,9))
						[Label(16, TEXT("오늘 백화점 들르지?\n모자 하나 부탁해."))]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0,0,0,12)
					[
						SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(.13f,.22f,.27f))
						.Padding(FMargin(10,9)).Visibility_Lambda([this]{ return Elapsed >= 2.5f
							? EVisibility::HitTestInvisible : EVisibility::Hidden; })
						[Label(16, TEXT("응. 손수건하고 물도 보고 갈게."))]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
					[
						SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(.12f,.14f,.16f))
						.Padding(FMargin(10,9)).Visibility_Lambda([this]{ return Elapsed >= 4.f
							? EVisibility::HitTestInvisible : EVisibility::Hidden; })
						[Label(16, TEXT("모자 매장은 위층 안쪽이야."))]
					]
					]
					+ SOverlay::Slot()
					[
						SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(.004f,.005f,.007f))
						.Visibility_Lambda([this]{ return Elapsed >= OpeningPhoneOffAt
							? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					]
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0,0,96,74)
		[
			SNew(SBox).WidthOverride(134).HeightOverride(214)
			.Visibility_Lambda([this]{ return Phase == EPhase::Fire && Elapsed >= FailedPhoneRaiseAt && Elapsed < FireEndsAt
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.RenderTransform_Lambda([this]() -> TOptional<FSlateRenderTransform>
			{
				const float Rise = FMath::SmoothStep(FailedPhoneRaiseAt, FailedPhoneRaiseAt + .48f, Elapsed);
				const float Lower = FMath::SmoothStep(FailedPhoneLowerAt, FireEndsAt, Elapsed);
				const float Tap = FMath::Max(0.f, 1.f - FMath::Abs(Elapsed - FailedPhoneFirstPress) / .11f)
					+ FMath::Max(0.f, 1.f - FMath::Abs(Elapsed - FailedPhoneSecondPress) / .11f);
				return FSlateRenderTransform(FVector2D(5.f * Tap, 300.f * (1.f - Rise + Lower) + 4.f * Tap));
			})
			[
				SNew(SBorder).BorderImage(&PhoneFrame).Padding(8)
				[SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(.004f,.005f,.007f))]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(28,26)
		[
			SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(.085f,.085f,.074f,.92f))
			.Padding(FMargin(18, 14)).Visibility_Lambda([this]{ return Phase == EPhase::Shopping
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 17))
				.Text_UObject(this, &AProjectSingleIntroGameMode::GetShoppingList)]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0,80,0,0)
		[
			SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 16)).ShadowOffset(FVector2D(1, 2))
			.Text_UObject(this, &AProjectSingleIntroGameMode::GetInteractionPrompt)
			.Visibility(EVisibility::HitTestInvisible)
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(22,0,22,52)
		[
			SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 20)).ShadowOffset(FVector2D(1, 2))
			.Text_UObject(this, &AProjectSingleIntroGameMode::GetSubtitle).Justification(ETextJustify::Center)
			.AutoWrapText(true).Visibility(EVisibility::HitTestInvisible)
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 46))
			.Text(FText::FromString(ProjectDFSDBranding::GameTitle()))
			.Visibility_Lambda([this]{ return Phase == EPhase::TitleCard
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.ColorAndOpacity_Lambda([this]
			{
				const float In = FMath::SmoothStep(TitleFadeInAt, TitleFadeInEndsAt, Elapsed);
				const float Out = FMath::SmoothStep(TitleFadeOutAt, TitleFadeOutEndsAt, Elapsed);
				return FSlateColor(FLinearColor(.94f,.92f,.87f, In * (1.f - Out)));
			})
		];
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 700);
}

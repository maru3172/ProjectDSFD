#include "ProjectSingleIntro.h"

#include "Animation/AnimSequence.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TriggerBox.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PlayerInteractionComponent.h"
#include "ProjectDFSDBranding.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01SpatialAudio.h"
#include "ProjectUIClickSound.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundAttenuation.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SConstraintCanvas.h"
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
	constexpr float OpeningWalkSpeed = 96.f;
	constexpr float OpeningBlackHold = .28f;
	constexpr float OpeningRevealEndsAt = 2.10f;
	constexpr float OpeningPhoneUpAt = 1.65f;
	constexpr float FirstMessageAt = 3.15f;
	constexpr float ReplyAt = 6.65f;
	constexpr float LastMessageAt = 10.05f;
	constexpr float OpeningPhoneOffAt = 12.80f;
	constexpr float PhoneDuration = 14.1f;
	constexpr float EntranceHandoffAt = 4.45f;
	constexpr float EntranceDuration = 5.55f;
	constexpr float HatReachAt = .78f;
	constexpr float HatGripAt = 1.65f;
	constexpr float HatLiftedAt = 2.35f;
	constexpr float HatCameraReturnAt = 2.55f;
	constexpr float HatInspectDuration = 3.65f;
	constexpr float ShutterLandsAt = .95f;
	constexpr float HatStowAt = 1.55f;
	constexpr float HatStowedAt = 2.35f;
	constexpr float LookBackAt = 2.50f;
	constexpr float FireAlarmAt = 6.10f;
	constexpr float FailedPhoneRaiseAt = FireAlarmAt + 3.25f;
	constexpr float FailedPhoneFirstPress = FireAlarmAt + 4.40f;
	constexpr float FailedPhoneSecondPress = FireAlarmAt + 5.85f;
	constexpr float FailedPhoneLowerAt = FireAlarmAt + 8.f;
	constexpr float FireEndsAt = FireAlarmAt + 9.20f;
	constexpr float FinaleOpenSeconds = 2.f;
	constexpr float FirstOutageAt = 3.65f;
	constexpr float LastLightHold = 2.2f;
	constexpr float AfterglowSeconds = .75f;
	constexpr float AfterglowIntensity = .035f;
	constexpr float TitleFadeInAt = .45f;
	constexpr float TitleFadeInEndsAt = 2.2f;
	constexpr float TitleHandoffAt = 3.65f;

	AActor* FindLitLens(const ULightComponent& Light)
	{
		TArray<AActor*> Attached;
		Light.GetOwner()->GetAttachedActors(Attached);
		AActor** Lens = Attached.FindByPredicate([](const AActor* Actor)
		{
			return Actor->ActorHasTag(TEXT("IntroDownlightLens")) || Actor->ActorHasTag(TEXT("IntroFinaleDiffuser"));
		});
		return Lens ? *Lens : nullptr;
	}

	void ExtinguishFixture(const ULightComponent& Light)
	{
		TArray<AActor*> Attached;
		Light.GetOwner()->GetAttachedActors(Attached);
		for (auto* Part : Attached)
		{
			if (Part->ActorHasTag(TEXT("IntroDownlightLens")) || Part->ActorHasTag(TEXT("IntroFinaleDiffuser")))
				Part->SetActorHiddenInGame(true);
			if (Part->ActorHasTag(TEXT("IntroDownlightBounce")))
				if (auto* Bounce = Part->FindComponentByClass<ULightComponent>()) Bounce->SetVisibility(false);
		}
	}
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
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PhoneWalk(
		TEXT("/Game/Characters/Mannequins/Anims/Pistol/Walk/MF_Pistol_Walk_Fwd"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PhoneIdle(
		TEXT("/Game/Characters/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS"));
	PhoneWalkPose = PhoneWalk.Object;
	PhoneIdlePose = PhoneIdle.Object;
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
		if (It->ActorHasTag(TEXT("IntroPhoneProp"))) PhoneProp = *It;
		if (It->ActorHasTag(TEXT("IntroPhoneDisplay"))) PhoneDisplay = *It;
		if (It->ActorHasTag(TEXT("IntroFinaleCamera"))) FinaleCamera = Cast<ACameraActor>(*It);
		if (It->ActorHasTag(TEXT("IntroFinaleShoppingSign"))) FinaleShoppingSigns.Add(*It);
		if (It->ActorHasTag(TEXT("IntroFinaleLight")))
			if (auto* Light = It->FindComponentByClass<ULightComponent>()) FinaleLights.Add(Light);
		if (It->ActorHasTag(TEXT("IntroStoreLight")))
			if (auto* Light = It->FindComponentByClass<ULightComponent>()) StoreLights.Add(Light);
		if (It->ActorHasTag(TEXT("IntroMannequinPass"))) Passage = Cast<ATriggerBox>(*It);
	}
	if (!ensureMsgf(Hat && Partner && Mannequin && Shutter && OpeningDestination && ShutterHandle && Passage
		&& FinaleCamera && FinaleLights.Num() >= 2 && FinaleCrowd.Num() >= 2,
		TEXT("Single intro needs its shop, an encounter trigger and a populated finale corridor")))
	{
		SetActorTickEnabled(false);
		return;
	}
	if (OpeningCamera) OpeningCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	FinaleCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	HatStart = Hat->GetActorLocation();
	HatStartRotation = Hat->GetActorQuat();
	ShutterOpen = Shutter->GetActorLocation();
	FinaleLights.Sort([this](const ULightComponent& A, const ULightComponent& B)
	{
		return FVector::DistSquared2D(A.GetComponentLocation(), ShutterOpen)
			> FVector::DistSquared2D(B.GetComponentLocation(), ShutterOpen);
	});
	for (const auto& Light : FinaleLights)
	{
		FinaleLightIntensities.Add(Light->Intensity);
		FinaleDiffusers.Add(FindLitLens(*Light));
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
	if (PhoneProp) PhoneProp->SetActorHiddenInGame(true);
	if (PhoneDisplay) PhoneDisplay->SetActorHiddenInGame(true);
	Passage->OnActorBeginOverlap.AddUniqueDynamic(this, &AProjectSingleIntroGameMode::OnPassMannequin);
	PhoneClick = NewObject<UProjectUIClickSound>(this);
	if (auto* Settings = UProjectProject01GameUserSettings::Get())
		PhoneClick->SoundClassObject = Settings->GetSFXSoundClass();
	BuildOverlay();
}

void AProjectSingleIntroGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	FinishHatShot();
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
		return Phase == EPhase::Meet;
	return &Target == ShutterHandle && Phase == EPhase::ReadyToLeave;
}

FText AProjectSingleIntroGameMode::GetUseText(const AProjectSingleIntroTarget& Target) const
{
	if (&Target == Partner)
		return FText::FromString(TEXT("말 걸기"));
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
	TurnDuration = 0.f;
	if (Controller.IsValid()) TurnStart = Controller->GetControlRotation();
	const bool bCutscene = Phase == EPhase::Phone || Phase == EPhase::EnterStore
		|| Phase == EPhase::InspectHat || Phase == EPhase::Fire
		|| Phase == EPhase::Talk || Phase == EPhase::Agree || Phase == EPhase::PartnerInjured
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
			Player->GetCharacterMovement()->MaxWalkSpeed = OpeningWalkSpeed;
		}
	}
	if (Phase == EPhase::Shopping)
	{
		if (auto* Player = Cast<ACharacter>(Controller->GetPawn()))
		{
			Player->GetCharacterMovement()->MaxWalkSpeed = WalkSpeedBeforeIntro;
			Player->GetCharacterMovement()->StopMovementImmediately();
		}
	}
	if (Phase == EPhase::InspectHat)
	{
		APawn* Player = Controller->GetPawn();
		HatApproach = (HatStart - Player->GetActorLocation()).GetSafeNormal2D();
		if (HatApproach.IsNearlyZero()) HatApproach = Player->GetActorForwardVector();
		HatCamera = GetWorld()->SpawnActor<ACameraActor>();
		if (HatCamera)
		{
			HatCamera->GetCameraComponent()->bConstrainAspectRatio = false;
			HatCamera->GetCameraComponent()->SetFieldOfView(70.f);
			UpdateHatShot(*Player);
			Controller->SetViewTargetWithBlend(HatCamera, .92f, VTBlend_Cubic, 0.f, true);
		}
	}
	if (Phase == EPhase::Fire) PlayShutterDrop();
	if (Phase == EPhase::Finale)
	{
		Partner->SetWalking(false);
		for (const auto& Figure : FinaleCrowd) Figure->SetActorHiddenInGame(false);
		for (const auto& Sign : FinaleShoppingSigns) Sign->SetActorHiddenInGame(true);
		Controller->SetViewTargetWithBlend(FinaleCamera, 1.05f, VTBlend_Cubic);
	}
}

void AProjectSingleIntroGameMode::TurnToward(const FVector& Point, float MinimumSeconds, float StartAt)
{
	if (TurnDuration <= 0.f)
	{
		// Keep a single shortest arc for the whole shot, including when the target is directly behind.
		const FRotator Aim = (Point - Controller->GetPawn()->GetPawnViewLocation()).Rotation();
		TurnDelta = (Aim - TurnStart).GetNormalized();
		TurnDuration = FMath::Max(MinimumSeconds, FMath::Max(FMath::Abs(TurnDelta.Yaw), FMath::Abs(TurnDelta.Pitch)) / 54.f);
	}
	const float Alpha = FMath::SmoothStep(0.f, TurnDuration, Elapsed - StartAt);
	Controller->SetControlRotation((TurnStart + TurnDelta * Alpha).GetNormalized());
}

void AProjectSingleIntroGameMode::UpdateHatShot(APawn& Player)
{
	if (!HatCamera) return;
	const float Dolly = FMath::SmoothStep(.2f, 2.1f, Elapsed);
	const FVector Side = FVector::CrossProduct(FVector::UpVector, HatApproach);
	const FVector Focus = FMath::Lerp(Player.GetActorLocation() + FVector(0,0,48), HatStart + FVector(0,0,14), .53f);
	const FVector Desired = Focus - HatApproach * FMath::Lerp(47.f, 33.f, Dolly)
		+ Side * FMath::Lerp(212.f, 172.f, Dolly) + FVector(0,0,54);
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IntroHatCamera), false, &Player);
	Query.AddIgnoredActor(Hat);
	const FVector Location = GetWorld()->LineTraceSingleByChannel(Hit, Focus, Desired, ECC_Visibility, Query)
		? Hit.Location + Hit.Normal * 10.f : Desired;
	HatCamera->SetActorLocationAndRotation(Location, (Focus - Location).Rotation());
}

FVector AProjectSingleIntroGameMode::PoseHatReach(ACharacter& Player, const FVector& GripTarget, float Reach)
{
	auto* Source = Player.GetMesh();
	static const FName UpperName(TEXT("upperarm_r")), LowerName(TEXT("lowerarm_r")), HandName(TEXT("hand_r"));
	if (!Source || !Source->GetSkinnedAsset() || Source->GetBoneIndex(UpperName) == INDEX_NONE
		|| Source->GetBoneIndex(LowerName) == INDEX_NONE || Source->GetBoneIndex(HandName) == INDEX_NONE) return GripTarget;
	if (!HatReachMesh)
	{
		HatReachMesh = NewObject<UPoseableMeshComponent>(&Player);
		HatReachMesh->SetSkinnedAssetAndUpdate(Source->GetSkinnedAsset());
		HatReachMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		for (int32 Index = 0; Index < Source->GetNumMaterials(); ++Index)
			HatReachMesh->SetMaterial(Index, Source->GetMaterial(Index));
		HatReachMesh->SetWorldTransform(Source->GetComponentTransform());
		HatReachMesh->RegisterComponentWithWorld(GetWorld());
		bHatMeshWasVisible = Source->IsVisible();
		Source->SetVisibility(false);
	}
	HatReachMesh->SetWorldTransform(Source->GetComponentTransform());
	HatReachMesh->CopyPoseFromSkeletalComponent(Source);
	auto Upper = HatReachMesh->GetBoneTransformByName(UpperName, EBoneSpaces::WorldSpace);
	auto Lower = HatReachMesh->GetBoneTransformByName(LowerName, EBoneSpaces::WorldSpace);
	const FVector Hand = HatReachMesh->GetBoneLocationByName(HandName, EBoneSpaces::WorldSpace);
	const FVector Shoulder = Upper.GetLocation();
	const FVector OldElbow = Lower.GetLocation();
	const double UpperLength = FVector::Distance(Shoulder, OldElbow);
	const double LowerLength = FVector::Distance(OldElbow, Hand);
	const FVector ToTarget = FMath::Lerp(Hand, GripTarget, Reach) - Shoulder;
	const FVector Direction = ToTarget.GetSafeNormal();
	if (UpperLength < 1. || LowerLength < 1. || Direction.IsNearlyZero()) return GripTarget;
	const double Length = FMath::Clamp(ToTarget.Length(), FMath::Abs(UpperLength - LowerLength) + .1, UpperLength + LowerLength - .1);
	const double Along = (FMath::Square(UpperLength) - FMath::Square(LowerLength) + FMath::Square(Length)) / (2. * Length);
	FVector Bend = FVector::VectorPlaneProject(OldElbow - Shoulder, Direction).GetSafeNormal();
	if (Bend.IsNearlyZero()) Bend = FVector::VectorPlaneProject(-FVector::UpVector, Direction).GetSafeNormal();
	const FVector Elbow = Shoulder + Direction * Along + Bend * FMath::Sqrt(FMath::Max(0., FMath::Square(UpperLength) - FMath::Square(Along)));
	Upper.SetRotation(FQuat::FindBetweenVectors(OldElbow - Shoulder, Elbow - Shoulder) * Upper.GetRotation());
	Lower.SetRotation(FQuat::FindBetweenVectors(Hand - OldElbow, Shoulder + Direction * Length - Elbow) * Lower.GetRotation());
	Lower.SetLocation(Elbow);
	HatReachMesh->SetBoneTransformByName(UpperName, Upper, EBoneSpaces::WorldSpace);
	HatReachMesh->SetBoneTransformByName(LowerName, Lower, EBoneSpaces::WorldSpace);
	HatReachMesh->RefreshBoneTransforms();
	return HatReachMesh->GetBoneLocationByName(HandName, EBoneSpaces::WorldSpace);
}

void AProjectSingleIntroGameMode::UpdateHatHandling(APawn& Player, float Reach, float Lift, float Stow)
{
	FMinimalViewInfo View;
	Player.CalcCamera(0.f, View);
	const FQuat Facing = View.Rotation.Quaternion();
	const FVector Held = View.Location + Facing.RotateVector(FVector(81, 8, 3));
	const FVector Pocket = View.Location + Facing.RotateVector(FVector(30, 38, -103));
	const FVector HatPosition = FMath::Lerp(FMath::Lerp(HatStart, Held, Lift), Pocket, Stow);
	const FVector GripSide = FVector::CrossProduct(FVector::UpVector, HatApproach);
	const FQuat Tilt(GripSide, FMath::DegreesToRadians(56.f * Lift));
	const FVector GripOffset = Tilt.RotateVector(-HatApproach * 18.f + GripSide * 20.f - FVector(0, 0, 15.f));
	const FVector GripTarget = HatPosition + GripOffset;
	auto* Character = Cast<ACharacter>(&Player);
	const FVector Hand = Character ? PoseHatReach(*Character, GripTarget, Reach) : GripTarget;
	if (Lift > 0.f) Hat->SetActorLocationAndRotation(Hand - GripOffset, Tilt * HatStartRotation);
}

void AProjectSingleIntroGameMode::FinishHatShot()
{
	if (HatReachMesh)
	{
		if (const auto* Player = Cast<ACharacter>(HatReachMesh->GetOwner()))
			if (auto* Source = Player->GetMesh()) Source->SetVisibility(bHatMeshWasVisible);
		HatReachMesh->DestroyComponent();
		HatReachMesh = nullptr;
	}
	if (HatCamera)
	{
		HatCamera->Destroy();
		HatCamera = nullptr;
	}
}

void AProjectSingleIntroGameMode::OnPassMannequin(AActor*, AActor* Other)
{
	if (Phase == EPhase::WalkToShutter && Controller.IsValid() && Other == Controller->GetPawn())
	{
		Partner->SetWalking(false);
		PlayPartnerScream();
		PlayImpact(Partner->GetActorLocation(), .76f, .65f);
		Enter(EPhase::PartnerInjured);
	}
}

void AProjectSingleIntroGameMode::WalkIntoStore(APawn& Player)
{
	const FVector ToDoor = OpeningDestination->GetActorLocation() - Player.GetActorLocation();
	const float Distance = ToDoor.Size2D();
	if (auto* Character = Cast<ACharacter>(&Player))
		Character->GetCharacterMovement()->MaxWalkSpeed = OpeningWalkSpeed;
	if (Distance > 24.f)
		Player.AddMovementInput(ToDoor.GetSafeNormal2D(), FMath::Clamp(Distance / 42.f, .28f, 1.f), true);
	Controller->SetControlRotation(FRotator(0.f, OpeningYaw, 0.f));
}

void AProjectSingleIntroGameMode::SetPhonePose(ACharacter& Player, UAnimSequence* Animation)
{
	if (!PhoneProp || !PhoneDisplay || bUsingPhonePose == (Animation != nullptr)) return;
	auto* Mesh = Player.GetMesh();
	if (!Mesh) return;
	bUsingPhonePose = Animation != nullptr;
	PhoneProp->SetActorHiddenInGame(!bUsingPhonePose);
	PhoneDisplay->SetActorHiddenInGame(!bUsingPhonePose || Phase == EPhase::Fire);
	if (Animation)
	{
		PreviousAnimationMode = Mesh->GetAnimationMode();
		Mesh->PlayAnimation(Animation, true);
		if (Animation == PhoneWalkPose) Mesh->SetPlayRate(.8f);
	}
	else Mesh->SetAnimationMode(static_cast<EAnimationMode::Type>(PreviousAnimationMode));
}

FVector AProjectSingleIntroGameMode::UpdateHeldPhone(ACharacter& Player)
{
	const auto* Mesh = Player.GetMesh();
	const FVector Forward = Player.GetActorForwardVector();
	const FVector Hands = (Mesh->GetSocketLocation(TEXT("hand_r")) + Mesh->GetSocketLocation(TEXT("hand_l"))) * .5f;
	const FVector Position = Hands + Forward * 5.f + FVector(0, 0, 3);
	PhoneProp->SetActorLocationAndRotation(Position, FRotator(-12.f, Forward.Rotation().Yaw, 0.f));
	return Position;
}

float AProjectSingleIntroGameMode::GetOpeningFade() const
{
	if (Phase == EPhase::Phone)
		return FMath::Max(1.f - FMath::SmoothStep(OpeningBlackHold, OpeningRevealEndsAt, Elapsed),
			FMath::SmoothStep(PhoneDuration - .80f, PhoneDuration, Elapsed));
	const float BetweenShots = FMath::SmoothStep(EntranceHandoffAt - .90f, EntranceHandoffAt, Elapsed)
		* (1.f - FMath::SmoothStep(EntranceHandoffAt, EntranceDuration - .1f, Elapsed));
	return FMath::Max(1.f - FMath::SmoothStep(0.f, 1.2f, Elapsed), BetweenShots);
}
void AProjectSingleIntroGameMode::FollowPartner(const APawn& Player, float DeltaSeconds)
{
	const FVector Position = Player.GetActorLocation();
	const FVector FromPlayer = (Partner->GetActorLocation() - Position).GetSafeNormal2D();
	Partner->MoveToward(Position + FromPlayer * 155.f, 500.f, DeltaSeconds);
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
		return;
	}
	if (!Controller.IsValid() || !Controller->GetPawn()) return;
	auto& Player = *Controller->GetPawn();
	const float Previous = Elapsed;
	Elapsed += DeltaSeconds;
	switch (Phase)
	{
	case EPhase::Phone:
		WalkIntoStore(Player);
		if (auto* Character = Cast<ACharacter>(&Player))
		{
			SetPhonePose(*Character, Elapsed >= OpeningPhoneUpAt && Elapsed < OpeningPhoneOffAt ? PhoneWalkPose.Get() : nullptr);
			if (bUsingPhonePose) UpdateHeldPhone(*Character);
		}
		if (OpeningCamera && OpeningCameraEnd)
		{
			const FRotator Facing(0.f, OpeningYaw, 0.f);
			const FVector Position = Player.GetActorLocation();
			const FVector TrackingPosition = Position + Facing.RotateVector(FVector(240, -205, -19));
			const FVector TrackingFocus = Position + Facing.RotateVector(FVector(0, -48, 37));
			OpeningCamera->SetActorLocationAndRotation(TrackingPosition, (TrackingFocus - TrackingPosition).Rotation());
		}
		if (Elapsed >= PhoneDuration) Enter(EPhase::EnterStore);
		break;
	case EPhase::EnterStore:
		WalkIntoStore(Player);
		if (OpeningCamera && OpeningCameraEnd && Previous < EntranceHandoffAt)
		{
			FTransform Wide;
			Wide.Blend(OpeningCameraStart, OpeningCameraEnd->GetActorTransform(),
				FMath::SmoothStep(0.f, EntranceHandoffAt, Elapsed));
			OpeningCamera->SetActorTransform(Wide);
			if (Elapsed >= EntranceHandoffAt) Controller->SetViewTarget(&Player);
		}
		if (Elapsed >= EntranceDuration) Enter(EPhase::Shopping);
		break;
	case EPhase::InspectHat:
		TurnToward(HatStart, 1.15f);
		UpdateHatShot(Player);
		if (Elapsed < HatReachAt && FVector::Dist2D(Player.GetActorLocation(), HatStart) > 74.f)
			Player.AddMovementInput(HatApproach, .45f, true);
		else if (Elapsed >= HatReachAt)
		{
			if (auto* Character = Cast<ACharacter>(&Player))
				Character->GetCharacterMovement()->StopMovementImmediately();
			UpdateHatHandling(Player, FMath::SmoothStep(HatReachAt, HatGripAt, Elapsed),
				FMath::SmoothStep(HatGripAt, HatLiftedAt, Elapsed));
		}
		if (Previous < HatGripAt && Elapsed >= HatGripAt)
			Hat->SetActorEnableCollision(false);
		if (HatCamera && Previous < HatCameraReturnAt && Elapsed >= HatCameraReturnAt)
			Controller->SetViewTargetWithBlend(&Player, .95f, VTBlend_Cubic, 0.f, true);
		if (Elapsed >= HatInspectDuration) Enter(EPhase::Fire);
		break;
	case EPhase::Fire:
	{
		const FVector Door = ShutterOpen - FVector(0, 0, ShutterDrop);
		if (Previous < ShutterLandsAt)
			Shutter->SetActorLocation(FMath::Lerp(ShutterOpen, Door, FMath::SmoothStep(0.f, ShutterLandsAt, Elapsed)));
		if (Elapsed < HatStowedAt)
			UpdateHatHandling(Player, 1.f, 1.f, FMath::SmoothStep(HatStowAt, HatStowedAt, Elapsed));
		else if (Previous < HatStowedAt)
		{
			Hat->SetActorHiddenInGame(true);
			TArray<AActor*> Parts;
			Hat->GetAttachedActors(Parts);
			for (auto* Part : Parts) Part->SetActorHiddenInGame(true);
			FinishHatShot();
		}
		if (Elapsed >= LookBackAt) TurnToward(Door, 1.35f, LookBackAt);
		if (Previous < FireAlarmAt && Elapsed >= FireAlarmAt) PlayAlarm();
		if (auto* Character = Cast<ACharacter>(&Player); Character && OpeningCamera && PhoneIdlePose)
		{
			if (Elapsed >= FailedPhoneRaiseAt && Elapsed < FailedPhoneLowerAt + .65f)
			{
				SetPhonePose(*Character, PhoneIdlePose);
				if (bUsingPhonePose)
				{
					const FVector Handset = UpdateHeldPhone(*Character);
					const FRotator Facing(0.f, Player.GetActorRotation().Yaw, 0.f);
					const FVector ShotPosition = Handset + Facing.RotateVector(FVector(-48, 30, 34));
					OpeningCamera->SetActorLocationAndRotation(ShotPosition, (Handset - ShotPosition).Rotation());
					const float Tap = FMath::Max(0.f, 1.f - FMath::Abs(Elapsed - FailedPhoneFirstPress) / .18f)
						+ FMath::Max(0.f, 1.f - FMath::Abs(Elapsed - FailedPhoneSecondPress) / .18f);
					const float Raise = FMath::SmoothStep(FailedPhoneRaiseAt, FailedPhoneRaiseAt + .65f, Elapsed);
					const float Lower = FMath::SmoothStep(FailedPhoneLowerAt, FailedPhoneLowerAt + .65f, Elapsed);
					PhoneProp->AddActorWorldOffset(Facing.RotateVector(FVector(0, Tap * .6f, -24.f * (1.f - Raise + Lower))));
					if (Previous < FailedPhoneRaiseAt)
						Controller->SetViewTargetWithBlend(OpeningCamera, .65f, VTBlend_Cubic, 0.f, true);
					if (Previous < FailedPhoneLowerAt && Elapsed >= FailedPhoneLowerAt)
						Controller->SetViewTargetWithBlend(&Player, .65f, VTBlend_Cubic, 0.f, true);
				}
			}
			else SetPhonePose(*Character, nullptr);
		}
		if ((Previous < FailedPhoneFirstPress && Elapsed >= FailedPhoneFirstPress)
			|| (Previous < FailedPhoneSecondPress && Elapsed >= FailedPhoneSecondPress))
			UGameplayStatics::PlaySound2D(this, PhoneClick, .23f, 1.2f);
		if (Elapsed >= FireEndsAt) Enter(EPhase::Meet);
		break;
	}
	case EPhase::Talk:
		TurnToward(Partner->GetActorLocation() + FVector(0, 0, 148), 1.15f);
		if (Elapsed >= 5.80f) Enter(EPhase::LeaveTogether);
		break;
	case EPhase::LeaveTogether:
		if (FVector::DistSquared2D(Partner->GetActorLocation(), Player.GetActorLocation()) > FMath::Square(460.f))
			Enter(EPhase::Agree);
		break;
	case EPhase::Agree:
		TurnToward(Partner->GetActorLocation() + FVector(0, 0, 148), 1.3f);
		if (Elapsed > .6f) FollowPartner(Player, DeltaSeconds);
		if (Elapsed >= 3.4f) Enter(EPhase::WalkToShutter);
		break;
	case EPhase::WalkToShutter:
		FollowPartner(Player, DeltaSeconds);
		break;
	case EPhase::PartnerInjured:
		TurnToward(Partner->GetActorLocation() + FVector(0, 0, 126), 1.55f);
		Partner->SetActorRotation(FRotator(-24.f * FMath::SmoothStep(0.f, .28f, Elapsed)
			* (1.f - FMath::SmoothStep(4.4f, 5.45f, Elapsed)),
			Partner->GetActorRotation().Yaw, 0.f));
		if (Elapsed >= 8.25f) Enter(EPhase::ReadyToLeave);
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
			if (LightIndex == 0)
				for (const auto& Light : StoreLights)
				{
					Light->SetVisibility(false);
					ExtinguishFixture(*Light);
				}
			PlayImpact(FinaleLights[LightIndex]->GetComponentLocation(), 1.55f, .24f);
			if (auto* Point = Cast<UPointLightComponent>(FinaleLights[LightIndex]))
				Point->SetAttenuationRadius(FMath::Min(Point->AttenuationRadius, 410.f));
			ExtinguishFixture(*FinaleLights[LightIndex]);
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
		if (Elapsed >= TitleHandoffAt)
		{
			if (auto* Game = Cast<UProjectProject01GameInstance>(GetGameInstance()))
				Game->ConsumeStartupIntro();
			SetActorTickEnabled(false);
			UGameplayStatics::OpenLevel(this, TEXT("/Game/MyProject/Level/TitleLevel"), true,
				ProjectDFSDBranding::SingleIntroTitleRevealOption);
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

void AProjectSingleIntroGameMode::PlayShutterDrop()
{
	constexpr int32 Rate = 24000;
	constexpr float Duration = ShutterLandsAt + .55f;
	TArray<int16> Samples;
	Samples.SetNumUninitialized(FMath::CeilToInt(Rate * Duration));
	FRandomStream Rattle(92);
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const float Time = static_cast<float>(Index) / Rate;
		const float Age = Time - ShutterLandsAt;
		const float Roll = FMath::SmoothStep(0.f, .15f, Time)
			* (1.f - FMath::SmoothStep(ShutterLandsAt - .12f, ShutterLandsAt, Time));
		const float Slat = FMath::Square(FMath::Max(0.f, FMath::Cos(2.f * PI * (11.f + 7.f * Time) * Time)));
		const float Rail = Roll * (.29f * FMath::Sin(2.f * PI * 69.f * Time)
			+ Slat * (.29f * FMath::Sin(2.f * PI * 288.f * Time) + .18f * Rattle.FRandRange(-1.f, 1.f)));
		const float Slam = Age < 0.f ? 0.f : FMath::Exp(-8.f * Age)
			* (.65f * FMath::Sin(2.f * PI * 51.f * Age) + .23f * FMath::Sin(2.f * PI * 183.f * Age)
				+ .12f * FMath::Sin(2.f * PI * 463.f * Age));
		Samples[Index] = static_cast<int16>(10600.f * (Rail + Slam)
			* (1.f - FMath::SmoothStep(Duration - .1f, Duration, Time)));
	}
	ShutterDropSound = NewObject<USoundWaveProcedural>(this);
	ShutterDropSound->SetSampleRate(Rate);
	ShutterDropSound->NumChannels = 1;
	ShutterDropSound->Duration = Duration;
	if (auto* Settings = UProjectProject01GameUserSettings::Get())
		ShutterDropSound->SoundClassObject = Settings->GetSFXSoundClass();
	ShutterDropSound->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
	auto* Attenuation = NewObject<USoundAttenuation>(ShutterDropSound);
	ProjectProject01SpatialAudio::ApplyWorldSFXSettings(Attenuation->Attenuation);
	UGameplayStatics::PlaySoundAtLocation(this, ShutterDropSound, ShutterOpen - FVector(0, 0, ShutterDrop),
		FRotator::ZeroRotator, .95f, 1.f, 0.f, Attenuation);
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

void AProjectSingleIntroGameMode::PlayPartnerScream()
{
	constexpr int32 Rate = 24000;
	constexpr float Duration = .76f;
	TArray<int16> Samples;
	Samples.SetNumUninitialized(FMath::RoundToInt(Rate * Duration));
	FRandomStream Air(1527);
	float VoicePhase = 0.f;
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const float Time = static_cast<float>(Index) / Rate;
		const float Pitch = 340.f + 190.f * FMath::SmoothStep(0.f, .12f, Time)
			- 215.f * FMath::SmoothStep(.24f, Duration, Time) + 16.f * FMath::Sin(73.f * Time);
		VoicePhase += 2.f * PI * Pitch / Rate;
		const float Voice = .39f * FMath::Sin(VoicePhase) + .29f * FMath::Sin(2.f * VoicePhase)
			+ .18f * FMath::Sin(3.f * VoicePhase) + .07f * FMath::Sin(5.f * VoicePhase);
		const float Gain = FMath::SmoothStep(0.f, .028f, Time)
			* (1.f - FMath::SmoothStep(.23f, Duration, Time)) * (.91f + .09f * FMath::Sin(88.f * Time));
		Samples[Index] = static_cast<int16>(9500.f * Gain * (Voice + Air.FRandRange(-.14f, .14f)));
	}
	PartnerScream = NewObject<USoundWaveProcedural>(this);
	PartnerScream->SetSampleRate(Rate);
	PartnerScream->NumChannels = 1;
	PartnerScream->Duration = Duration;
	if (auto* Settings = UProjectProject01GameUserSettings::Get())
		PartnerScream->SoundClassObject = Settings->GetSFXSoundClass();
	PartnerScream->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
	auto* Attenuation = NewObject<USoundAttenuation>(PartnerScream);
	ProjectProject01SpatialAudio::ApplyWorldSFXSettings(Attenuation->Attenuation);
	UGameplayStatics::PlaySoundAtLocation(this, PartnerScream, Partner->GetActorLocation(), FRotator::ZeroRotator,
		.87f, 1.f, 0.f, Attenuation);
}

FText AProjectSingleIntroGameMode::GetSubtitle() const
{
	const TCHAR* Line = TEXT("");
	switch (Phase)
	{
	case EPhase::Fire:
		if (Elapsed >= ShutterLandsAt && Elapsed < HatStowedAt)
			Line = TEXT("무슨 일이지?");
		else if (Elapsed >= FireAlarmAt && Elapsed < FailedPhoneRaiseAt)
			Line = TEXT("화재가 발생했습니다. 비상구로 대피하십시오.");
		else if (Elapsed >= FailedPhoneSecondPress + .45f && Elapsed < FailedPhoneLowerAt)
			Line = TEXT("전원이 안 들어오네.");
		else if (Elapsed >= FailedPhoneLowerAt)
			Line = TEXT("연락할 방법이 없겠어.");
		break;
	case EPhase::Meet:
		if (Elapsed < 2.7f) Line = TEXT("저 사람한테 물어볼까.");
		break;
	case EPhase::Talk:
		Line = Elapsed < 2.85f ? TEXT("저기요, 혹시 폰 켜져요?")
			: TEXT("아뇨. 제 것도 안 켜져요.");
		break;
	case EPhase::LeaveTogether:
		if (Elapsed < 3.0f) Line = TEXT("나갈 길이 있을 텐데…");
		break;
	case EPhase::Agree:
		if (Elapsed > .35f) Line = TEXT("잠깐만요. 같이 가요.");
		break;
	case EPhase::PartnerInjured:
		Line = Elapsed < .85f ? TEXT("꺄악!")
			: Elapsed < 3.20f ? TEXT("괜찮아요?")
			: Elapsed < 5.70f ? TEXT("네. 조금 긁혔어요.")
			: TEXT("들어온 쪽 셔터를 열어 봐요.");
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

TSharedRef<SWidget> AProjectSingleIntroGameMode::BuildCinematicMask()
{
	const auto* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
	const auto CinematicBarFraction = [this]()
	{
		float Alpha = 0.f;
		if (Phase == EPhase::Phone)
			Alpha = 1.f;
		else if (Phase == EPhase::EnterStore)
			Alpha = 1.f - FMath::SmoothStep(EntranceHandoffAt, EntranceDuration, Elapsed);
		else if (Phase == EPhase::InspectHat)
			Alpha = FMath::SmoothStep(0.f, .48f, Elapsed);
		else if (Phase == EPhase::Fire)
			Alpha = 1.f - FMath::SmoothStep(FireEndsAt - .65f, FireEndsAt, Elapsed);
		else if (Phase == EPhase::PartnerInjured)
			Alpha = FMath::SmoothStep(0.f, .16f, Elapsed) * (1.f - FMath::SmoothStep(7.55f, 8.25f, Elapsed));
		else if (Phase == EPhase::Finale)
			Alpha = FMath::SmoothStep(0.f, .7f, Elapsed);
		return .085f * Alpha;
	};
	return SNew(SConstraintCanvas).Visibility(EVisibility::HitTestInvisible)
	+ SConstraintCanvas::Slot().Offset(FMargin(0)).Anchors_Lambda([CinematicBarFraction]
	{
		return FAnchors(0.f, 0.f, 1.f, CinematicBarFraction());
	})
	[SNew(SImage).Image(Brush).ColorAndOpacity(FLinearColor::Black)]
	+ SConstraintCanvas::Slot().Offset(FMargin(0)).Anchors_Lambda([CinematicBarFraction]
	{
		return FAnchors(0.f, 1.f - CinematicBarFraction(), 1.f, 1.f);
	})
	[SNew(SImage).Image(Brush).ColorAndOpacity(FLinearColor::Black)];
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
	static const FSlateRoundedBoxBrush PhoneFrame(FLinearColor(.045f,.061f,.075f), 24.f,
		FLinearColor(.13f,.15f,.17f), 2.f);
	Overlay = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(Brush)
			.Visibility_Lambda([this]{ return Phase == EPhase::Waiting || Phase == EPhase::Phone || Phase == EPhase::EnterStore
				|| Phase == EPhase::Finale || Phase == EPhase::TitleCard
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.BorderBackgroundColor_Lambda([this]
			{
				const float Opacity = Phase == EPhase::Phone || Phase == EPhase::EnterStore ? GetOpeningFade()
					: Phase == EPhase::Finale ? FMath::SmoothStep(0.f, .28f, Elapsed - GetLastOutageTime() - AfterglowSeconds) : 1.f;
				return FLinearColor(0,0,0,Opacity);
			})
		]
		+ SOverlay::Slot()
		[
			BuildCinematicMask()
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(58,0,0,70)
		[
			SNew(SBox).WidthOverride(340).Visibility_Lambda([this]{ return Phase == EPhase::Phone
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.RenderTransform_Lambda([this]() -> TOptional<FSlateRenderTransform>
			{
				const float In = FMath::SmoothStep(OpeningPhoneUpAt, OpeningPhoneUpAt + .65f, Elapsed);
				const float Out = FMath::SmoothStep(OpeningPhoneOffAt, PhoneDuration - .5f, Elapsed);
				return FSlateRenderTransform(FVector2D(0.f, (1.f - In + Out) * 600.f));
			})
			[
				SNew(SBorder).BorderImage(&PhoneFrame).Padding(FMargin(16, 18))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Label(17, TEXT("메시지"))]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0,0,0,7)
					[
						SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(.12f,.14f,.16f))
						.Padding(FMargin(9,7)).Visibility_Lambda([this]{ return Elapsed >= FirstMessageAt
							? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						[Label(15, TEXT("백화점 가면 모자 하나 부탁해."))]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0,0,0,7)
					[
						SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(.13f,.22f,.27f))
						.Padding(FMargin(9,7)).Visibility_Lambda([this]{ return Elapsed >= ReplyAt
							? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						[Label(15, TEXT("응. 손수건이랑 물도 살게."))]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
					[
						SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(.12f,.14f,.16f))
						.Padding(FMargin(9,7)).Visibility_Lambda([this]{ return Elapsed >= LastMessageAt
							? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						[Label(15, TEXT("모자 매장은 위층 맨 안쪽이야."))]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(2,6,2,0)
					[
						SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
						.Text_Lambda([this]
						{
							const float Next = Elapsed < FirstMessageAt ? FirstMessageAt : Elapsed < ReplyAt ? ReplyAt : LastMessageAt;
							const float Remaining = Next - Elapsed;
							if (Remaining <= 0.f || Remaining > 1.25f) return FText::GetEmpty();
							return FText::FromString(FString::ChrN(1 + FMath::Clamp(FMath::FloorToInt((1.25f - Remaining) * 2.4f), 0, 2), TEXT('.')));
						})
					]
				]
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
			.Visibility(EVisibility::HitTestInvisible)
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
				return FSlateColor(FLinearColor(.94f,.92f,.87f, In));
			})
		];
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 700);
}

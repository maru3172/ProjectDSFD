#include "ProjectSpectatorCameraManager.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

void AProjectSpectatorCameraManager::UpdateViewTargetInternal(FTViewTarget& OutVT,float DeltaTime)
{
	Super::UpdateViewTargetInternal(OutVT,DeltaTime);
	if(const auto* Controller=Cast<AMultiplayTestPlayerController>(PCOwner);
		Controller && Controller->IsLocalController() && Controller->IsFreeSpectatingTarget(OutVT.Target))
	{
		OutVT.POV.Rotation=Controller->GetControlRotation();
		const FVector Anchor=OutVT.POV.Location;
		// Position belongs to the target's body, not the observer's free-look rotation.
		// Otherwise looking behind moves the camera around/in front of the target's face.
		const FVector BodyForward=OutVT.Target->GetActorForwardVector().GetSafeNormal2D();
		const FVector Desired=Anchor-BodyForward*FMath::Clamp(SpectatorBackOffsetCentimeters,0.f,150.f)
			+FVector::UpVector*FMath::Clamp(SpectatorHeightOffsetCentimeters,0.f,60.f);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(SpectatorCameraClearance),false,OutVT.Target);
		Query.AddIgnoredActor(Controller->GetPawn());FHitResult Hit;
		if(GetWorld()->SweepSingleByChannel(Hit,Anchor,Desired,FQuat::Identity,ECC_Camera,FCollisionShape::MakeSphere(3.f),Query))
			OutVT.POV.Location=FMath::Lerp(Anchor,Desired,FMath::Max(0.f,Hit.Time-.05f));
		else OutVT.POV.Location=Desired;
	}
}

bool AMultiplayTestPlayerController::IsFreeSpectatingTarget(const AActor* Target) const
{
	return SurvivorSpectatorView.bEnabled && SurvivorSpectatorView.Target.Get()==Target && IsValid(Target) &&
		SurvivorState!=EMultiplayTestSurvivorState::Active && Target!=GetPawn();
}

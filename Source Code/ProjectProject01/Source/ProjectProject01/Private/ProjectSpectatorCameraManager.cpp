#include "ProjectSpectatorCameraManager.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "Engine/World.h"

void AProjectSpectatorCameraManager::UpdateViewTargetInternal(FTViewTarget& OutVT,float DeltaTime)
{
	Super::UpdateViewTargetInternal(OutVT,DeltaTime);
	auto* Controller=Cast<AMultiplayTestPlayerController>(PCOwner);
	auto* Target=Cast<APlayerCharacter>(OutVT.Target);
	const bool Observing=Controller && Controller->IsLocalController() && Controller->IsFreeSpectatingTarget(Target);
	if(HiddenObservedTarget.Get()!= (Observing?Target:nullptr))
	{
		if(PCOwner && bAddedHiddenTarget)PCOwner->HiddenActors.RemoveAll([Old=HiddenObservedTarget](const auto& Actor){return !IsValid(Actor) || Actor==Old.Get();});
		HiddenObservedTarget=Observing?Target:nullptr;bAddedHiddenTarget=false;
		if(Observing && !Controller->HiddenActors.Contains(Target))
		{Controller->HiddenActors.Add(Target);bAddedHiddenTarget=true;}
	}
	if(Observing && Target)
	{
		// Target CalcCamera supplies the same camera position/FOV/postprocess. Only aim is independent.
		OutVT.POV.Rotation=Controller->GetControlRotation()+Target->GetPresentationRotationOffset();
	}
}

void AProjectSpectatorCameraManager::EndPlay(const EEndPlayReason::Type Reason)
{
	if(PCOwner && bAddedHiddenTarget)PCOwner->HiddenActors.RemoveAll([Old=HiddenObservedTarget](const auto& Actor){return !IsValid(Actor) || Actor==Old.Get();});
	HiddenObservedTarget.Reset();bAddedHiddenTarget=false;Super::EndPlay(Reason);
}

bool AMultiplayTestPlayerController::IsFreeSpectatingTarget(const AActor* Target) const
{
	return SurvivorSpectatorView.bEnabled && SurvivorSpectatorView.Target.Get()==Target && IsValid(Target) &&
		SurvivorState!=EMultiplayTestSurvivorState::Active && Target!=GetPawn();
}

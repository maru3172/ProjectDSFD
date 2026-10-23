#include "ProjectSpectatorCameraManager.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "ProjectOpenObserver.h"
#include "Engine/World.h"

void AProjectSpectatorCameraManager::UpdateViewTargetInternal(FTViewTarget& OutVT,float DeltaTime)
{
	Super::UpdateViewTargetInternal(OutVT,DeltaTime);
	auto* Controller=Cast<AMultiplayTestPlayerController>(PCOwner);
	auto* Target=Cast<APlayerCharacter>(OutVT.Target);
    const auto* Open=Cast<AProjectOpenObserverController>(Controller);
    AActor* Observed=Open?Open->GetObservedActor():Target;
	const bool Observing=Controller && Controller->IsLocalController() &&
        ((Open&&Observed&&Observed==OutVT.Target)||Controller->IsFreeSpectatingTarget(Target));
	if(HiddenObservedTarget.Get()!= (Observing?Observed:nullptr))
	{
		if(PCOwner && bAddedHiddenTarget)PCOwner->HiddenActors.RemoveAll([Old=HiddenObservedTarget](const auto& Actor){return !IsValid(Actor) || Actor==Old.Get();});
		HiddenObservedTarget=Observing?Observed:nullptr;bAddedHiddenTarget=false;
		if(Observing && !Controller->HiddenActors.Contains(Observed))
		{Controller->HiddenActors.Add(Observed);bAddedHiddenTarget=true;}
	}
	if(Observing && Target)
	{
		// Target CalcCamera supplies the same camera position/FOV/postprocess. Only aim is independent.
		OutVT.POV.Rotation=Controller->GetControlRotation()+Target->GetPresentationRotationOffset();
	}
    else if(Observing&&Open)OutVT.POV.Rotation=Controller->GetControlRotation();
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

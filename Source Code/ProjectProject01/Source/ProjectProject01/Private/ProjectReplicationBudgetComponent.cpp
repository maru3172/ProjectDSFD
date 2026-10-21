#include "ProjectReplicationBudgetComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UProjectReplicationBudgetComponent::UProjectReplicationBudgetComponent()
{
	PrimaryComponentTick.bCanEverTick=true;
	PrimaryComponentTick.TickInterval=.2f;
}

void UProjectReplicationBudgetComponent::BeginPlay()
{
	Super::BeginPlay();
	OriginalFrequency=GetOwner()->GetNetUpdateFrequency();
	// Standalone tutorials/single-player and client-side simulation are untouched.
	if (GetNetMode()==NM_Standalone || GetNetMode()==NM_Client) SetComponentTickEnabled(false);
}

float UProjectReplicationBudgetComponent::FrequencyForView(float FullRate,double DistanceSquared,bool bInFront,bool bMoving)
{
	// Broad front hemisphere prefetches before a quick turn, not a narrow render frustum.
	// Keep close threats at full rate, even behind the camera or through a wall.
	if (DistanceSquared<=FMath::Square(1500.0) || (bInFront && DistanceSquared<=FMath::Square(15000.0))) return FullRate;
	// World SFX remain audible up to 60m. Do not starve the positions used by audio.
	return FMath::Min(FullRate,DistanceSquared<=FMath::Square(6000.0)?20.f:bMoving?12.f:6.f);
}

float UProjectReplicationBudgetComponent::FrequencyForActor(const AActor& Actor,float FullRate)
{
	const UWorld* World=Actor.GetWorld();
	if (!World) return FullRate;
	float Frequency=FMath::Min(FullRate,Actor.GetVelocity().IsNearlyZero(1.f)?6.f:12.f);
	for (FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
	{
		const APlayerController* PC=It->Get();
		if (!IsValid(PC)) continue;
		// Self, the selected mannequin and a spectator's current view target always win.
		if (PC->GetViewTarget()==&Actor || PC->GetPawn()==&Actor || Actor.IsOwnedBy(PC)) return FullRate;
		FVector View;FRotator Rotation;PC->GetPlayerViewPoint(View,Rotation);
		const FVector Offset=Actor.GetActorLocation()-View;
		Frequency=FMath::Max(Frequency,FrequencyForView(FullRate,Offset.SizeSquared(),FVector::DotProduct(Rotation.Vector(),Offset)>=0, !Actor.GetVelocity().IsNearlyZero(1.f)));
	}
	return Frequency;
}

void UProjectReplicationBudgetComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
	AActor* Actor=GetOwner();
	if (!IsValid(Actor) || !Actor->HasAuthority() || !Actor->GetIsReplicated()) return;
	const float Frequency=FrequencyForActor(*Actor,OriginalFrequency);
	const float Previous=Actor->GetNetUpdateFrequency();
	if (FMath::IsNearlyEqual(Frequency,Previous)) return;
	Actor->SetNetUpdateFrequency(Frequency);
	// Do not wait out the old low-rate schedule when a target becomes important.
	if (Frequency>Previous) Actor->ForceNetUpdate();
}

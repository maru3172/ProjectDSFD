#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProjectReplicationBudgetComponent.generated.h"

/** Multiplayer transport budget only. Never suspends authoritative simulation or relevance. */
UCLASS()
class PROJECTPROJECT01_API UProjectReplicationBudgetComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProjectReplicationBudgetComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
	static float FrequencyForActor(const AActor& Actor,float FullRate);
	static float FrequencyForView(float FullRate,double DistanceSquared,bool bInFront,bool bMoving);
private:
	float OriginalFrequency=100.f;
};

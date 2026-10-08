#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectProject01Checkpoint.generated.h"

UCLASS(Blueprintable)
class PROJECTPROJECT01_API AProjectProject01Checkpoint final : public AActor
{
	GENERATED_BODY()

public:
	AProjectProject01Checkpoint();

private:
	UFUNCTION()
	void HandleBeginOverlap(class UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		class UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, Category="Checkpoint")
	TObjectPtr<class UBoxComponent> Trigger;

	UPROPERTY(EditAnywhere, Category="Checkpoint")
	FName CheckpointId = TEXT("Checkpoint");

	UPROPERTY(EditAnywhere, Category="Checkpoint")
	bool bSaveOnlyOncePerSession = true;

	bool bTriggered = false;
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectProject01CheckpointSaveable.h"
#include "ProjectProject01Checkpoint.generated.h"

UCLASS(Blueprintable)
class PROJECTPROJECT01_API AProjectProject01Checkpoint final : public AActor,
	public IProjectProject01CheckpointSaveable
{
	GENERATED_BODY()

public:
	AProjectProject01Checkpoint();
	virtual FName GetCheckpointSaveId_Implementation() const override;
	virtual void OnCheckpointStateRestored_Implementation() override;

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

	UPROPERTY(SaveGame)
	bool bTriggered = false;
};

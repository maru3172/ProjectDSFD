#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectPracticeEnvironment.generated.h"

/** Transient practice-only arena furniture. No authored level is modified. */
UCLASS(NotBlueprintable)
class PROJECTPROJECT01_API AProjectPracticeEnvironment final : public AActor
{
    GENERATED_BODY()
public:
    AProjectPracticeEnvironment();
    int32 Build(const FTransform& Anchor);
    int32 GetObstacleCount()const{return Blocks.Num();}
private:
    UPROPERTY() TObjectPtr<class UStaticMesh> BlockMesh;
    UPROPERTY(Transient) TArray<TObjectPtr<class UStaticMeshComponent>> Blocks;
};

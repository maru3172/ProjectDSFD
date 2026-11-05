// File: Source/ProjectProject01/Public/ProjectPlaygroundFire.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectPlaygroundFire.generated.h"

/**
 * One cell of the custom game's spreading fire (Docs/CustomGamePlayground.md 12.1). The server spawns and places cells;
 * they replicate so every client draws the same fire. The look is built in code from engine shapes and the engine's
 * additive emissive material (no project asset): swaying flame cones, an ember glow on the floor and a soft orange
 * light. A new cell grows in over two seconds so nothing pops in, and the light only drifts gently (no flashing).
 */
UCLASS(NotBlueprintable)
class PROJECTPROJECT01_API AProjectPlaygroundFire final : public AActor
{
    GENERATED_BODY()
public:
    AProjectPlaygroundFire();
    friend class FProjectCustomRulesFireRenderTest;
    /** Ground radius of one cell (cm). */
    static constexpr float Radius = 300.f;
    /** Whether a point stands in this cell (same floor: within 3 m up or down). */
    bool Contains(const FVector& Point) const;
    /** 0..1 while the cell grows in (presentation and tests). */
    float GetGrowth() const { return Growth; }

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Embers;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UPointLightComponent> Glow;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Flames;
    UPROPERTY(Transient) TArray<TObjectPtr<class UMaterialInstanceDynamic>> Materials;
    TArray<FVector> FlameBaseScale;
    double BornAt = 0;
    float Seed = 0, Growth = 0;
    void ApplyLook(float Seconds);
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectDFSDTitleScene.generated.h"

/** A local, decorative set. Never an AI pawn, never replicated or used by gameplay maps. */
UCLASS(NotBlueprintable)
class PROJECTPROJECT01_API AProjectDFSDTitleScene final : public AActor
{
	GENERATED_BODY()
	friend class FProjectDFSDTitleSceneTest;
	friend class FProjectDFSDTitleSceneRenderTest;

public:
	static constexpr float BaseKeyIntensity = 2000;
	AProjectDFSDTitleScene();
	void SetPresentationActive(bool bActive);
	static float LightEnvelope(float Seconds, bool bDoubleDip);
	class UCameraComponent* GetTitleCamera() const { return Camera; }
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	void CreateSetMaterials();
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class USkeletalMeshComponent> Mannequin;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class USpotLightComponent> KeyLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpotLightComponent> RimLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UPointLightComponent> FillLight;
	UPROPERTY() TObjectPtr<class UAnimationAsset> IdlePose;
	UPROPERTY() TObjectPtr<class UMaterialInterface> SurfaceMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> MistMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> DustMaterial;
	UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> SetMeshes;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> MistSheets;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Dust;
	UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> MaterialInstances;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> EffectMaterials;
	TArray<FVector> DustOrigins;
	TArray<FVector> MistOrigins;
	FRandomStream Random;
	float Clock = 0, UntilDip = 2.5f, DipTime = -1, LastEffectsScale = -1, LastDustGain = -1;
	bool bDoubleDip = false;
	bool bPresentationActive = false;
};

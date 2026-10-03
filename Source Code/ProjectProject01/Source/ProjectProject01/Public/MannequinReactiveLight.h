// File: Source/ProjectProject01/Public/MannequinReactiveLight.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MannequinReactiveLight.generated.h"

class AMannequinAICharacter;
class UPrimitiveComponent;
class USceneComponent;
class USphereComponent;
class USpotLightComponent;
class UStaticMeshComponent;

/**
 * 감지 반경 안의 마네킹 수에 따라 더 빠르게 점멸하는 로컬 연출용 조명이다.
 * AI 마네킹과 플레이어가 조종 중인 동일 클래스의 마네킹을 모두 감지한다.
 */
UCLASS(Blueprintable)
class PROJECTPROJECT01_API AMannequinReactiveLight final : public AActor
{
	GENERATED_BODY()

public:
	AMannequinReactiveLight();

	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category="Reactive Light|Debug")
	int32 GetDetectedMannequinCount() const { return CurrentDetectedMannequinCount; }

	UFUNCTION(BlueprintPure, Category="Reactive Light|Debug")
	float GetCurrentBlinkIntervalSeconds() const { return CurrentBlinkIntervalSeconds; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleDetectionBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleDetectionEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

	void RefreshInitialOverlaps();
	void RefreshDetectedMannequinsFromWorld();
	int32 PruneAndCountDetectedMannequins();
	void UpdateBlinkingFromDetectedCount();
	void ToggleBlinkState();
	void ApplyLightState(bool bNewLightOn);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Reactive Light|Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 전등갓 등 점멸하지 않는 외형 메시다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Reactive Light|Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> FixtureMesh;

	/** EmissiveParameterName 스칼라 파라미터로 켜짐과 꺼짐을 표현하는 전구 메시다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Reactive Light|Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> BulbMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Reactive Light|Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USpotLightComponent> ReactiveSpotLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Reactive Light|Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> DetectionSphere;

	/** 마네킹을 감지하는 구 반경이다. 단위는 cm다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="cm"))
	float DetectionRadius = 1500.0f;

	/** Overlap 이벤트가 누락되는 충돌 조합을 보완하는 저빈도 구체 조회 주기다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.05", UIMin="0.05", Units="s"))
	float DetectionRefreshIntervalSeconds = 0.2f;

	/** 마네킹 한 기가 들어왔을 때 조명의 켜짐/꺼짐 상태가 전환되는 간격이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Blink",
		meta=(AllowPrivateAccess="true", ClampMin="0.01", UIMin="0.01", Units="s"))
	float BaseBlinkIntervalSeconds = 0.6f;

	/** 두 번째 마네킹부터 한 기가 추가될 때마다 감소하는 점멸 간격이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Blink",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="s"))
	float BlinkIntervalReductionPerAdditionalMannequin = 0.08f;

	/** 마네킹이 많아도 이 값보다 빠르게 상태를 전환하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Blink",
		meta=(AllowPrivateAccess="true", ClampMin="0.01", UIMin="0.01", Units="s"))
	float MinimumBlinkIntervalSeconds = 0.12f;

	/** 조명이 켜진 상태의 실제 Spot Light 광량이다. 꺼질 때는 0으로 설정된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Light",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float LightOnIntensity = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Emissive", meta=(AllowPrivateAccess="true"))
	FName EmissiveParameterName = TEXT("EmissiveStrength");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Emissive",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float EmissiveOnValue = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Emissive",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float EmissiveOffValue = 0.0f;

	TSet<TWeakObjectPtr<AMannequinAICharacter>> DetectedMannequins;
	FTimerHandle BlinkTimerHandle;
	FTimerHandle DetectionRefreshTimerHandle;
	int32 CurrentDetectedMannequinCount = 0;
	float CurrentBlinkIntervalSeconds = 0.0f;
	bool bLightOn = true;
};

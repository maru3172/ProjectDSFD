// File: Source/ProjectProject01/Public/MannequinReactiveLight.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SynthComponent.h"
#include "ProjectReactiveLightPattern.h"
#include "MannequinReactiveLight.generated.h"

class AMannequinAICharacter;
class UPrimitiveComponent;
class USceneComponent;
class USphereComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class APlayerController;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FConvexVolume;

/** Quiet fixture-local electrical hum and arcing, generated without an external audio asset. */
UCLASS(ClassGroup=(Audio))
class PROJECTPROJECT01_API UReactiveLightElectricalSynth final : public USynthComponent
{
	GENERATED_BODY()
	friend class FProjectReactiveLightPresentationTest;
public:
	UReactiveLightElectricalSynth(const FObjectInitializer& Initializer);
	void SetFault(float Amount);
protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
private:
	int32 Rate=48000;
	float Phase=0, Gain=0, TargetGain=0, FilteredNoise=0, Arc=0;
	uint32 Noise=0xA341316Cu;
};

/**
 * 주변 마네킹 3개 단위로 불규칙한 전압 저하·점등 실패·느린 복구를 표현하는 로컬 조명이다.
 * AI 마네킹과 플레이어가 조종 중인 동일 클래스의 마네킹을 모두 감지한다.
 */
UCLASS(Blueprintable)
class PROJECTPROJECT01_API AMannequinReactiveLight final : public AActor
{
	GENERATED_BODY()
	friend class FProjectReviewRuntimeRegressionTest;
	friend class FProjectReactiveLightPresentationTest;
	friend class FProjectReactiveLightRenderTest;
	friend class FProjectReactiveLightVisibilityTest;

public:
	AMannequinReactiveLight();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category="Reactive Light|Debug")
	int32 GetDetectedMannequinCount() const { return CurrentDetectedMannequinCount; }

	UFUNCTION(BlueprintPure, Category="Reactive Light|Debug")
	float GetCurrentBlinkIntervalSeconds() const { return CurrentBlinkIntervalSeconds; }

	UFUNCTION(BlueprintPure, Category="Reactive Light|Debug")
	int32 GetThreatTier() const { return ProjectReactiveLight::Tier(CurrentDetectedMannequinCount); }

	/** 체크포인트 역직렬화 뒤 저장된 상태를 실제 조명 컴포넌트와 감지 타이머에 반영합니다. */
	void RefreshAfterCheckpointRestore();

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
	void ApplyLightState(bool bNewLightOn);
	void ApplyBrightness(float Brightness);
	void ApplyRecoveryProjection(float Brightness, float FlashScale, float WakeBlend);
	void ApplyColdTint(float Tint);
	bool ScheduleNearbyCascade(TSet<TWeakObjectPtr<AMannequinReactiveLight>>& Members, uint64 GroupId,
		const FVector& ViewLocation, double Now, FRandomStream& Random);
	void ResetFaultPresentation();
	bool IsPresentationRelevant(APlayerController* Viewer);
	bool IntersectsInfluence(const FConvexVolume& Frustum) const;
	FVector GetDetectionOrigin();

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

	/** 전등 아래 같은 층에서 마네킹을 감지하는 수평 반경이다. 단위는 cm다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="cm"))
	float DetectionRadius = 1500.0f;

	/** Overlap 이벤트가 누락되는 충돌 조합을 보완하는 저빈도 구체 조회 주기다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.05", UIMin="0.05", Units="s"))
	float DetectionRefreshIntervalSeconds = 0.2f;

	/** 전등 아래 바닥과 마네킹 발의 높이 허용 간격. 다른 층의 몸체를 제외한다. */
	UPROPERTY(EditAnywhere, Category="Reactive Light|Detection", meta=(ClampMin="100", Units="cm"))
	float FloorHeightTolerance = 450.f;

	/** 고장 중 남겨 두는 광량. 기본 0은 완전 소등이며 원래 광량을 넘는 섬광은 만들지 않는다. */
	UPROPERTY(EditAnywhere, Category="Reactive Light|Light", meta=(ClampMin="0", ClampMax="1"))
	float MinimumBrightnessFraction = 0.f;

	UPROPERTY(EditAnywhere, Category="Reactive Light|Sound")
	bool bElectricalSound = true;

	UPROPERTY(EditAnywhere, Category="Reactive Light|Sound", meta=(ClampMin="0", ClampMax="1"))
	float ElectricalSoundVolume = .45f;

	/** 이전 배치 에셋 호환용. 새 연출은 고정 점멸 간격을 사용하지 않는다. */
	UPROPERTY(BlueprintReadOnly, Category="Reactive Light|Legacy",
		meta=(AllowPrivateAccess="true", ClampMin="0.01", UIMin="0.01", Units="s"))
	float BaseBlinkIntervalSeconds = 0.6f;

	UPROPERTY(BlueprintReadOnly, Category="Reactive Light|Legacy",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="s"))
	float BlinkIntervalReductionPerAdditionalMannequin = 0.08f;

	UPROPERTY(BlueprintReadOnly, Category="Reactive Light|Legacy",
		meta=(AllowPrivateAccess="true", ClampMin="0.01", UIMin="0.01", Units="s"))
	float MinimumBlinkIntervalSeconds = 0.12f;

	/** 조명이 켜진 상태의 실제 Spot Light 광량이다. 꺼질 때는 0으로 설정된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Light",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float LightOnIntensity = 83786.43f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Emissive", meta=(AllowPrivateAccess="true"))
	FName EmissiveParameterName = TEXT("EmissiveStrength");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Emissive",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float EmissiveOnValue = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive Light|Emissive",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float EmissiveOffValue = 0.0f;

	TSet<TWeakObjectPtr<AMannequinAICharacter>> DetectedMannequins;
	FTimerHandle DetectionRefreshTimerHandle;
	UPROPERTY(Transient)
	TObjectPtr<UReactiveLightElectricalSynth> ElectricalSynth;
	ProjectReactiveLight::FFaultPattern FaultPattern;
	float TargetThreat=0, LastBrightness=-1;
	float NearestMannequinDistance=0;
	bool bPresentationAllowed=false;
	bool bPresentationRelevant=false, bDetectionOriginValid=false;
	FVector DetectionOrigin=FVector::ZeroVector, FloorProbeSource=FVector::ZeroVector;
	double LastRelevantAt=-1;
	double NextFloorProbeAt=0;
	uint64 LastSharedGroupId=0;
	float WakeBlendElapsed=.25f, WakeFromBrightness=1.f;
	FVector LastSectionLevels=FVector(-1), WakeFromSections=FVector::OneVector;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> RecoveryProjectionMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RecoveryProjectionInstance;
	FLinearColor LastRecoveryProjection{1,0,1,0};
	FLinearColor WakeFromRecoveryProjection{1,0,1,0};
	double PendingCascadeAt=-1;
	float LastColdTint=0, WakeFromColdTint=0;
	FLinearColor BaseSpotColor=FLinearColor::White;
	FLinearColor BaseBulbColor=FLinearColor::White;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ColdBulbInstance;
	UPROPERTY(SaveGame)
	int32 CurrentDetectedMannequinCount = 0;

	UPROPERTY(SaveGame)
	float CurrentBlinkIntervalSeconds = 0.0f;

	UPROPERTY(SaveGame)
	bool bLightOn = true;
};

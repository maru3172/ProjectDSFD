// File: Source/ProjectProject01/Private/MannequinReactiveLight.cpp
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#include "MannequinReactiveLight.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "MannequinAICharacter.h"
#include "TimerManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMannequinReactiveLight, Log, All);

namespace ProjectProject01ReactiveLight
{
	float CalculateBlinkIntervalSeconds(
		const int32 MannequinCount,
		const float BaseIntervalSeconds,
		const float ReductionPerAdditionalMannequin,
		const float MinimumIntervalSeconds)
	{
		if (MannequinCount <= 0)
		{
			return 0.0f;
		}

		const float SafeBaseInterval = FMath::Max(BaseIntervalSeconds, 0.01f);
		const float SafeMinimumInterval = FMath::Clamp(MinimumIntervalSeconds, 0.01f, SafeBaseInterval);
		const float SafeReduction = FMath::Max(ReductionPerAdditionalMannequin, 0.0f);
		const int32 AdditionalMannequinCount = FMath::Max(MannequinCount - 1, 0);
		return FMath::Max(
			SafeMinimumInterval,
			SafeBaseInterval - (SafeReduction * static_cast<float>(AdditionalMannequinCount)));
	}
}

AMannequinReactiveLight::AMannequinReactiveLight()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root"));
	SetRootComponent(SceneRoot);

	FixtureMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fixture Mesh"));
	FixtureMesh->SetupAttachment(SceneRoot);
	FixtureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	BulbMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bulb Mesh"));
	BulbMesh->SetupAttachment(SceneRoot);
	BulbMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ReactiveSpotLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Reactive Spot Light"));
	ReactiveSpotLight->SetupAttachment(SceneRoot);
	ReactiveSpotLight->SetMobility(EComponentMobility::Movable);
	ReactiveSpotLight->SetIntensity(5000.0f);
	ReactiveSpotLight->SetAttenuationRadius(1500.0f);
	ReactiveSpotLight->SetInnerConeAngle(28.0f);
	ReactiveSpotLight->SetOuterConeAngle(44.0f);

	DetectionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("Mannequin Detection Sphere"));
	DetectionSphere->SetupAttachment(SceneRoot);
	DetectionSphere->InitSphereRadius(DetectionRadius);
	DetectionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DetectionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	DetectionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	DetectionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	DetectionSphere->SetGenerateOverlapEvents(true);

	DetectionSphere->OnComponentBeginOverlap.AddDynamic(
		this, &AMannequinReactiveLight::HandleDetectionBeginOverlap);
	DetectionSphere->OnComponentEndOverlap.AddDynamic(
		this, &AMannequinReactiveLight::HandleDetectionEndOverlap);
}

void AMannequinReactiveLight::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (IsValid(DetectionSphere))
	{
		DetectionSphere->SetSphereRadius(FMath::Max(DetectionRadius, 0.0f), true);
	}
	if (IsValid(ReactiveSpotLight))
	{
		// 런타임 광량 점멸에는 Movable 조명이 필요하다. 블루프린트에서 잘못 바뀌어도 복구한다.
		ReactiveSpotLight->SetMobility(EComponentMobility::Movable);
		ReactiveSpotLight->SetIntensity(FMath::Max(LightOnIntensity, 0.0f));
	}

	ApplyLightState(true);
}

void AMannequinReactiveLight::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		UE_LOG(LogMannequinReactiveLight, Error, TEXT("%s has no valid World."), *GetName());
		return;
	}

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		DetectionSphere->SetGenerateOverlapEvents(false);
		DetectionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	ApplyLightState(true);
	RefreshInitialOverlaps();
	RefreshDetectedMannequinsFromWorld();
	UpdateBlinkingFromDetectedCount();
	World->GetTimerManager().SetTimer(
		DetectionRefreshTimerHandle,
		this,
		&AMannequinReactiveLight::RefreshDetectedMannequinsFromWorld,
		FMath::Max(DetectionRefreshIntervalSeconds, 0.05f),
		true);
}

void AMannequinReactiveLight::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(BlinkTimerHandle);
		World->GetTimerManager().ClearTimer(DetectionRefreshTimerHandle);
	}
	DetectedMannequins.Reset();
	Super::EndPlay(EndPlayReason);
}

void AMannequinReactiveLight::HandleDetectionBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(OtherActor);
	if (!IsValid(Mannequin))
	{
		return;
	}

	DetectedMannequins.Add(Mannequin);
	UpdateBlinkingFromDetectedCount();
}

void AMannequinReactiveLight::HandleDetectionEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex)
{
	AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(OtherActor);
	if (!IsValid(Mannequin))
	{
		return;
	}

	DetectedMannequins.Remove(TWeakObjectPtr<AMannequinAICharacter>(Mannequin));
	UpdateBlinkingFromDetectedCount();
}

void AMannequinReactiveLight::RefreshInitialOverlaps()
{
	DetectedMannequins.Reset();
	if (!ensureMsgf(IsValid(DetectionSphere), TEXT("Reactive light %s has no DetectionSphere."), *GetName()))
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	DetectionSphere->GetOverlappingActors(OverlappingActors, AMannequinAICharacter::StaticClass());
	for (AActor* Actor : OverlappingActors)
	{
		if (AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(Actor); IsValid(Mannequin))
		{
			DetectedMannequins.Add(Mannequin);
		}
	}
}

void AMannequinReactiveLight::RefreshDetectedMannequinsFromWorld()
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_DedicatedServer || !IsValid(DetectionSphere))
	{
		return;
	}
	const float SafeDetectionRadius = FMath::Max(DetectionRadius, 0.0f);
	if (SafeDetectionRadius <= UE_SMALL_NUMBER)
	{
		DetectedMannequins.Reset();
		UpdateBlinkingFromDetectedCount();
		return;
	}

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MannequinReactiveLightDetection), false, this);
	TArray<FOverlapResult> OverlapResults;
	World->OverlapMultiByObjectType(
		OverlapResults,
		DetectionSphere->GetComponentLocation(),
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(SafeDetectionRadius),
		QueryParams);

	TSet<TWeakObjectPtr<AMannequinAICharacter>> RefreshedMannequins;
	for (const FOverlapResult& Result : OverlapResults)
	{
		if (AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(Result.GetActor()); IsValid(Mannequin))
		{
			RefreshedMannequins.Add(Mannequin);
		}
	}

	DetectedMannequins = MoveTemp(RefreshedMannequins);
	UpdateBlinkingFromDetectedCount();
}

int32 AMannequinReactiveLight::PruneAndCountDetectedMannequins()
{
	for (auto Iterator = DetectedMannequins.CreateIterator(); Iterator; ++Iterator)
	{
		if (!Iterator->IsValid())
		{
			Iterator.RemoveCurrent();
		}
	}
	return DetectedMannequins.Num();
}

void AMannequinReactiveLight::UpdateBlinkingFromDetectedCount()
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const int32 PreviousCount = CurrentDetectedMannequinCount;
	const int32 NewCount = PruneAndCountDetectedMannequins();
	CurrentDetectedMannequinCount = NewCount;

	if (NewCount <= 0)
	{
		World->GetTimerManager().ClearTimer(BlinkTimerHandle);
		CurrentBlinkIntervalSeconds = 0.0f;
		ApplyLightState(true);
		if (PreviousCount > 0)
		{
			UE_LOG(LogMannequinReactiveLight, Log,
				TEXT("%s returned to steady light because no mannequins remain in range."), *GetName());
		}
		return;
	}

	const float NewInterval = ProjectProject01ReactiveLight::CalculateBlinkIntervalSeconds(
		NewCount,
		BaseBlinkIntervalSeconds,
		BlinkIntervalReductionPerAdditionalMannequin,
		MinimumBlinkIntervalSeconds);
	const bool bNeedsTimerRestart = !World->GetTimerManager().IsTimerActive(BlinkTimerHandle) ||
		!FMath::IsNearlyEqual(CurrentBlinkIntervalSeconds, NewInterval);
	CurrentBlinkIntervalSeconds = NewInterval;

	if (PreviousCount <= 0)
	{
		ApplyLightState(false);
	}

	if (bNeedsTimerRestart)
	{
		World->GetTimerManager().SetTimer(
			BlinkTimerHandle,
			this,
			&AMannequinReactiveLight::ToggleBlinkState,
			CurrentBlinkIntervalSeconds,
			true,
			CurrentBlinkIntervalSeconds);
	}

	if (PreviousCount != NewCount)
	{
		UE_LOG(LogMannequinReactiveLight, Log,
			TEXT("%s detects %d mannequin(s); blink interval is %.3fs."),
			*GetName(), NewCount, CurrentBlinkIntervalSeconds);
	}
}

void AMannequinReactiveLight::ToggleBlinkState()
{
	const int32 ValidCount = PruneAndCountDetectedMannequins();
	if (ValidCount != CurrentDetectedMannequinCount)
	{
		UpdateBlinkingFromDetectedCount();
		return;
	}

	if (ValidCount <= 0)
	{
		UpdateBlinkingFromDetectedCount();
		return;
	}

	ApplyLightState(!bLightOn);
}

void AMannequinReactiveLight::ApplyLightState(const bool bNewLightOn)
{
	bLightOn = bNewLightOn;
	if (IsValid(ReactiveSpotLight))
	{
		ReactiveSpotLight->SetIntensity(bLightOn ? FMath::Max(LightOnIntensity, 0.0f) : 0.0f);
		ReactiveSpotLight->SetVisibility(bLightOn, false);
	}
	if (IsValid(BulbMesh) && !EmissiveParameterName.IsNone())
	{
		BulbMesh->SetScalarParameterValueOnMaterials(
			EmissiveParameterName,
			bLightOn ? FMath::Max(EmissiveOnValue, 0.0f) : FMath::Max(EmissiveOffValue, 0.0f));
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01ReactiveLightIntervalTest,
	"ProjectProject01.Lighting.MannequinReactiveLightInterval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01ReactiveLightIntervalTest::RunTest(const FString& Parameters)
{
	using ProjectProject01ReactiveLight::CalculateBlinkIntervalSeconds;
	TestEqual(TEXT("No mannequins means no blink interval"), CalculateBlinkIntervalSeconds(0, 0.6f, 0.08f, 0.12f), 0.0f);
	TestEqual(TEXT("One mannequin uses the base interval"), CalculateBlinkIntervalSeconds(1, 0.6f, 0.08f, 0.12f), 0.6f);
	TestEqual(TEXT("Three mannequins reduce the interval twice"), CalculateBlinkIntervalSeconds(3, 0.6f, 0.08f, 0.12f), 0.44f);
	TestEqual(TEXT("Many mannequins clamp to the minimum"), CalculateBlinkIntervalSeconds(20, 0.6f, 0.08f, 0.12f), 0.12f);
	return true;
}
#endif

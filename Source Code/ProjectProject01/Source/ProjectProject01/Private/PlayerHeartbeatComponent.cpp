// File: Source/ProjectProject01/Private/PlayerHeartbeatComponent.cpp
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#include "PlayerHeartbeatComponent.h"
#include "ProjectProject01GameInstance.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "MannequinAICharacter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProjectProject01TuningData.h"
#include "ProjectProject01DiagnosticsSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace ProjectProject01HeartbeatVFX
{
	const FName EffectOpacityParameter(TEXT("EffectOpacity"));
	const FName DistortionAmountParameter(TEXT("DistortionAmount"));
	const FName DistortionSpeedParameter(TEXT("DistortionSpeed"));
	const FName DistortionFrequencyParameter(TEXT("DistortionFrequency"));

	float InterpAlpha(const float Current, const float Target, const float DeltaTime, const float Speed)
	{
		return Speed <= 0.0f ? Target : FMath::FInterpTo(Current, Target, DeltaTime, Speed);
	}

	float CalculateProximityAlpha(const float Distance, const float Range)
	{
		return Range <= UE_SMALL_NUMBER ? 0.0f : 1.0f - FMath::Clamp(Distance / Range, 0.0f, 1.0f);
	}

	float CalculateDensityAlpha(const int32 NearbyCount, const int32 CountForMax)
	{
		return FMath::Clamp(static_cast<float>(NearbyCount) / static_cast<float>(FMath::Max(CountForMax, 1)), 0.0f, 1.0f);
	}
}

UHeartbeatSynthComponent::UHeartbeatSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = false;
	bAutoDestroy = false;
	bStopWhenOwnerDestroyed = true;
	bAllowSpatialization = false;
	bIsUISound = false;
	NumChannels = 2;
}

void UHeartbeatSynthComponent::TriggerHeartbeat(const float Strength)
{
	const float SafeStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
	SynthCommand([this, SafeStrength]()
	{
		PulseFrameIndex = 0;
		PulsePhase = 0.0f;
		PulseStrength = SafeStrength;
		bPulseActive = SafeStrength > UE_SMALL_NUMBER;
	});
}

bool UHeartbeatSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 2;
	SynthSampleRate = FMath::Max(SampleRate, 8000);
	PulseFrameIndex = 0;
	PulsePhase = 0.0f;
	PulseStrength = 0.0f;
	bPulseActive = false;
	return true;
}

int32 UHeartbeatSynthComponent::OnGenerateAudio(float* OutAudio, const int32 NumSamples)
{
	if (OutAudio == nullptr || NumSamples <= 0)
	{
		return 0;
	}

	FMemory::Memzero(OutAudio, NumSamples * sizeof(float));
	const int32 ChannelCount = FMath::Max(NumChannels, 1);
	const int32 FrameCount = NumSamples / ChannelCount;
	constexpr float PulseDurationSeconds = 0.22f;
	constexpr float AttackSeconds = 0.006f;
	const int32 PulseFrameCount = FMath::Max(1, FMath::RoundToInt(PulseDurationSeconds * SynthSampleRate));
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		float Sample = 0.0f;
		if (bPulseActive && PulseFrameIndex < PulseFrameCount)
		{
			const float TimeSeconds = static_cast<float>(PulseFrameIndex) / static_cast<float>(SynthSampleRate);
			const float Progress = FMath::Clamp(TimeSeconds / PulseDurationSeconds, 0.0f, 1.0f);
			const float Attack = FMath::Clamp(TimeSeconds / AttackSeconds, 0.0f, 1.0f);
			const float Envelope = Attack * FMath::Exp(-18.0f * TimeSeconds);
			const float Frequency = FMath::Lerp(82.0f, 48.0f, Progress);
			PulsePhase = FMath::Fmod(
				PulsePhase + (2.0f * UE_PI * Frequency / static_cast<float>(SynthSampleRate)),
				2.0f * UE_PI);
			const float Fundamental = FMath::Sin(PulsePhase);
			const float LowHarmonic = FMath::Sin(PulsePhase * 2.0f) * 0.18f;
			Sample = FMath::Clamp((Fundamental + LowHarmonic) * Envelope * PulseStrength, -1.0f, 1.0f);
			++PulseFrameIndex;
			if (PulseFrameIndex >= PulseFrameCount)
			{
				bPulseActive = false;
			}
		}

		for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
		{
			OutAudio[FrameIndex * ChannelCount + ChannelIndex] = Sample;
		}
	}
	return NumSamples;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01HeartbeatVFXMappingTest,
	"ProjectProject01.Player.HeartbeatVFXMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01HeartbeatVFXMappingTest::RunTest(const FString& Parameters)
{
	using namespace ProjectProject01HeartbeatVFX;
	TestEqual(TEXT("Outside the heartbeat range is fully transparent"), CalculateProximityAlpha(1500.0f, 1500.0f), 0.0f);
	TestEqual(TEXT("Half range maps to half visibility"), CalculateProximityAlpha(750.0f, 1500.0f), 0.5f);
	TestEqual(TEXT("At the player maps to full visibility"), CalculateProximityAlpha(0.0f, 1500.0f), 1.0f);
	TestEqual(TEXT("Density reaches maximum at configured count"), CalculateDensityAlpha(5, 5), 1.0f);
	TestEqual(TEXT("Density clamps above configured count"), CalculateDensityAlpha(10, 5), 1.0f);
	return true;
}
#endif

UPlayerHeartbeatComponent::UPlayerHeartbeatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	HeartbeatVFXMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/MyProject/VFX/M_HeartbeatScreenVFX.M_HeartbeatScreenVFX")));
}

void UPlayerHeartbeatComponent::ApplyHeartbeatTuning(const FPlayerTuningRow& Tuning)
{
	FString ValidationError;
	if (!Tuning.IsValidForApplication(ValidationError))
	{
		UE_LOG(LogProjectProject01Tuning, Error,
			TEXT("Heartbeat tuning was rejected for %s: %s"), *GetNameSafe(GetOwner()), *ValidationError);
		return;
	}

	BaseVisionRange = FMath::Max(0.0f, Tuning.BaseVisionRange);
	BaseVisionHalfAngleDegrees = FMath::Clamp(Tuning.BaseVisionHalfAngleDegrees, 0.0f, 180.0f);
	BaseMinBPM = FMath::Max(0.0f, Tuning.BaseMinBPM);
	BaseMaxBPM = FMath::Max(0.0f, Tuning.BaseMaxBPM);
	EncounterVisionRange = FMath::Max(0.0f, Tuning.EncounterVisionRange);
	EncounterVisionHalfAngleDegrees = FMath::Clamp(Tuning.EncounterVisionHalfAngleDegrees, 0.0f, 180.0f);
	EncounterMinBPM = FMath::Max(0.0f, Tuning.EncounterMinBPM);
	EncounterMaxBPM = FMath::Max(0.0f, Tuning.EncounterMaxBPM);
	EncounterDecayPerSecond = FMath::Max(0.0f, Tuning.EncounterDecayPerSecond);
	EncounterMemorySeconds = FMath::Max(0.0f, Tuning.EncounterMemorySeconds);
	HeartbeatRange = FMath::Max(0.0f, Tuning.HeartbeatRange);
	VisionCheckInterval = FMath::Max(0.0f, Tuning.VisionCheckInterval);
	MannequinRefreshInterval = FMath::Max(0.0f, Tuning.MannequinRefreshInterval);
	bEnableHeartbeatLog = Tuning.bEnableHeartbeatLog;
	BPMLogThreshold = FMath::Max(0.0f, Tuning.BPMLogThreshold);
	HeartbeatSFXVolumeMultiplier = FMath::Max(0.0f, Tuning.HeartbeatSFXVolumeMultiplier);
	if (UHeartbeatSynthComponent* Synth = HeartbeatSFXSynth.Get(); IsValid(Synth))
	{
		Synth->SetVolumeMultiplier(HeartbeatSFXVolumeMultiplier);
	}
	bEnableHeartbeatVFX = Tuning.bEnableHeartbeatVFX;
	HeartbeatVFXDensityCountForMax = FMath::Max(Tuning.HeartbeatVFXDensityCountForMax, 1);
	HeartbeatVFXMaxOpacity = FMath::Clamp(Tuning.HeartbeatVFXMaxOpacity, 0.0f, 1.0f);
	HeartbeatVFXMaxNoiseIntensity = FMath::Max(Tuning.HeartbeatVFXMaxNoiseIntensity, 0.0f);
	HeartbeatVFXMinNoiseSpeed = FMath::Max(Tuning.HeartbeatVFXMinNoiseSpeed, 0.0f);
	HeartbeatVFXMaxNoiseSpeed = FMath::Max(Tuning.HeartbeatVFXMaxNoiseSpeed, HeartbeatVFXMinNoiseSpeed);
	HeartbeatVFXMinNoiseFrequency = FMath::Max(Tuning.HeartbeatVFXMinNoiseFrequency, 0.0f);
	HeartbeatVFXMaxNoiseFrequency = FMath::Max(Tuning.HeartbeatVFXMaxNoiseFrequency, HeartbeatVFXMinNoiseFrequency);
	HeartbeatVFXMaxDistortionAmount = FMath::Max(Tuning.HeartbeatVFXMaxDistortionAmount, 0.0f);
	HeartbeatVFXMinDistortionSpeed = FMath::Max(Tuning.HeartbeatVFXMinDistortionSpeed, 0.0f);
	HeartbeatVFXMaxDistortionSpeed = FMath::Max(Tuning.HeartbeatVFXMaxDistortionSpeed, HeartbeatVFXMinDistortionSpeed);
	HeartbeatVFXMinDistortionFrequency = FMath::Max(Tuning.HeartbeatVFXMinDistortionFrequency, 0.0f);
	HeartbeatVFXMaxDistortionFrequency = FMath::Max(Tuning.HeartbeatVFXMaxDistortionFrequency, HeartbeatVFXMinDistortionFrequency);
	HeartbeatVFXBlendInSpeed = FMath::Max(Tuning.HeartbeatVFXBlendInSpeed, 0.0f);
	HeartbeatVFXBlendOutSpeed = FMath::Max(Tuning.HeartbeatVFXBlendOutSpeed, 0.0f);

	// 재임포트 직후에는 이전 임시 인지·쿨다운 상태를 섞지 않고 새 값으로 다시 계산한다.
	ResetHeartbeatState();
	RefreshMannequinCache();
	UE_LOG(LogProjectProject01Tuning, Log,
		TEXT("Heartbeat tuning applied to %s."), *GetNameSafe(GetOwner()));
}

void UPlayerHeartbeatComponent::GetDiagnosticAppliedTuning(FPlayerTuningRow& OutTuning) const
{
	OutTuning.BaseVisionRange = BaseVisionRange;
	OutTuning.BaseVisionHalfAngleDegrees = BaseVisionHalfAngleDegrees;
	OutTuning.BaseMinBPM = BaseMinBPM;
	OutTuning.BaseMaxBPM = BaseMaxBPM;
	OutTuning.EncounterVisionRange = EncounterVisionRange;
	OutTuning.EncounterVisionHalfAngleDegrees = EncounterVisionHalfAngleDegrees;
	OutTuning.EncounterMinBPM = EncounterMinBPM;
	OutTuning.EncounterMaxBPM = EncounterMaxBPM;
	OutTuning.EncounterDecayPerSecond = EncounterDecayPerSecond;
	OutTuning.EncounterMemorySeconds = EncounterMemorySeconds;
	OutTuning.HeartbeatRange = HeartbeatRange;
	OutTuning.VisionCheckInterval = VisionCheckInterval; OutTuning.MannequinRefreshInterval = MannequinRefreshInterval;
	OutTuning.bEnableHeartbeatLog = bEnableHeartbeatLog; OutTuning.BPMLogThreshold = BPMLogThreshold;
	OutTuning.HeartbeatSFXVolumeMultiplier = HeartbeatSFXVolumeMultiplier;
	OutTuning.bEnableHeartbeatVFX = bEnableHeartbeatVFX;
	OutTuning.HeartbeatVFXDensityCountForMax = HeartbeatVFXDensityCountForMax;
	OutTuning.HeartbeatVFXMaxOpacity = HeartbeatVFXMaxOpacity;
	OutTuning.HeartbeatVFXMaxNoiseIntensity = HeartbeatVFXMaxNoiseIntensity;
	OutTuning.HeartbeatVFXMinNoiseSpeed = HeartbeatVFXMinNoiseSpeed;
	OutTuning.HeartbeatVFXMaxNoiseSpeed = HeartbeatVFXMaxNoiseSpeed;
	OutTuning.HeartbeatVFXMinNoiseFrequency = HeartbeatVFXMinNoiseFrequency;
	OutTuning.HeartbeatVFXMaxNoiseFrequency = HeartbeatVFXMaxNoiseFrequency;
	OutTuning.HeartbeatVFXMaxDistortionAmount = HeartbeatVFXMaxDistortionAmount;
	OutTuning.HeartbeatVFXMinDistortionSpeed = HeartbeatVFXMinDistortionSpeed;
	OutTuning.HeartbeatVFXMaxDistortionSpeed = HeartbeatVFXMaxDistortionSpeed;
	OutTuning.HeartbeatVFXMinDistortionFrequency = HeartbeatVFXMinDistortionFrequency;
	OutTuning.HeartbeatVFXMaxDistortionFrequency = HeartbeatVFXMaxDistortionFrequency;
	OutTuning.HeartbeatVFXBlendInSpeed = HeartbeatVFXBlendInSpeed;
	OutTuning.HeartbeatVFXBlendOutSpeed = HeartbeatVFXBlendOutSpeed;
}

void UPlayerHeartbeatComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetHeartbeatState();
	RefreshMannequinCache();

	if (bEnableHeartbeatLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[Heartbeat] Component initialized | Owner=%s"), *GetNameSafe(GetOwner()));
	}
}

void UPlayerHeartbeatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseHeartbeatVFX();
	ReleaseHeartbeatSFX();
	Super::EndPlay(EndPlayReason);
}

void UPlayerHeartbeatComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwnerPawn) || !OwnerPawn->IsLocallyControlled())
	{
		ReleaseHeartbeatVFX();
		ReleaseHeartbeatSFX();
		return;
	}

	// 마네킹 검색과 시야 판정은 각각의 설정 주기에만 실행한다.
	RefreshAccumulator += DeltaTime;
	if (RefreshAccumulator >= FMath::Max(MannequinRefreshInterval, 0.1f))
	{
		RefreshAccumulator = 0.0f;
		RefreshMannequinCache();
	}

	VisionCheckAccumulator += DeltaTime;
	if (VisionCheckAccumulator >= FMath::Max(VisionCheckInterval, 0.01f))
	{
		VisionCheckAccumulator = 0.0f;
		UpdateDetectionStates(*OwnerPawn);
	}

	UpdateBPM(DeltaTime, *OwnerPawn);
	UpdateHeartbeatVFX(DeltaTime, *OwnerPawn);
	UpdateBeatOutput(DeltaTime, *OwnerPawn);
}

void UPlayerHeartbeatComponent::ResetHeartbeatState()
{
	CachedMannequins.Reset();
	MannequinStates.Reset();
	CurrentBPM = 0.0f;
	DistanceBPM = 0.0f;
	EncounterBPM = 0.0f;
	bHeartbeatActive = false;
	BeatAccumulator = 0.0f;
	VisionCheckAccumulator = 0.0f;
	RefreshAccumulator = 0.0f;
	LastLoggedBPM = 0.0f;
	CurrentVFXNearbyMannequinCount = 0;
	NearestVFXMannequinDistance = TNumericLimits<float>::Max();
	HeartbeatVFXProximityAlpha = 0.0f;
	HeartbeatVFXDensityAlpha = 0.0f;
	ReleaseHeartbeatVFX();
	ReleaseHeartbeatSFX();
}

int32 UPlayerHeartbeatComponent::GetKnownMannequinCount() const
{
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<AMannequinAICharacter>, FHeartbeatMannequinState>& Entry : MannequinStates)
	{
		if (Entry.Key.IsValid() && Entry.Value.bHasEverBeenRecognized)
		{
			++Count;
		}
	}
	return Count;
}

int32 UPlayerHeartbeatComponent::GetActiveMannequinCount() const
{
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<AMannequinAICharacter>, FHeartbeatMannequinState>& Entry : MannequinStates)
	{
		if (Entry.Key.IsValid() && Entry.Value.bActiveForHeartbeat)
		{
			++Count;
		}
	}
	return Count;
}

void UPlayerHeartbeatComponent::RefreshMannequinCache()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(World, AMannequinAICharacter::StaticClass(), FoundActors);
	CachedMannequins.Reset(FoundActors.Num());
	for (AActor* Actor : FoundActors)
	{
		if (AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(Actor); IsValid(Mannequin))
		{
			const TWeakObjectPtr<AMannequinAICharacter> WeakMannequin(Mannequin);
			CachedMannequins.Add(WeakMannequin);
			// VFX 거리 집계는 최초 시야 검사 전에도 동작해야 하므로 인지 상태와 별개로 항목을 준비한다.
			MannequinStates.FindOrAdd(WeakMannequin);
		}
	}
	RemoveInvalidMannequins();
}

void UPlayerHeartbeatComponent::UpdateDetectionStates(APawn& OwnerPawn)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	FProjectProject01DiagnosticWorkScope DiagnosticWork(World);
	DiagnosticWork.AddVisionCheck(CachedMannequins.Num());

	FVector ViewLocation;
	FVector ViewForward;
	if (!GetPlayerViewPoint(OwnerPawn, ViewLocation, ViewForward))
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	const float SafeBaseRange = FMath::Max(BaseVisionRange, 0.0f);
	const float SafeEncounterRange = FMath::Max(EncounterVisionRange, 0.0f);
	const double SafeMemorySeconds = FMath::Max(EncounterMemorySeconds, 0.0f);

	// 미인지 대상은 실제 시야로만 등록하고, 인지 대상만 넓은 유지 범위를 적용한다.
	for (const TWeakObjectPtr<AMannequinAICharacter>& WeakMannequin : CachedMannequins)
	{
		AMannequinAICharacter* Mannequin = WeakMannequin.Get();
		if (!IsValid(Mannequin))
		{
			continue;
		}

		FHeartbeatMannequinState& State = MannequinStates.FindOrAdd(WeakMannequin);
		const float Distance = FVector::Dist(OwnerPawn.GetActorLocation(), Mannequin->GetActorLocation());
		const bool bHasClearSight = HasClearLineOfSight(OwnerPawn, *Mannequin, ViewLocation);
		const bool bInsideBaseCone = Distance <= SafeBaseRange && IsInsideVisionCone(
			*Mannequin, ViewLocation, ViewForward, BaseVisionHalfAngleDegrees);
		const bool bInsideEncounterCone = Distance <= SafeEncounterRange && IsInsideVisionCone(
			*Mannequin, ViewLocation, ViewForward, EncounterVisionHalfAngleDegrees);
		const bool bBaseVisibleNow = bInsideBaseCone && bHasClearSight;
		const bool bEncounterVisibleNow = bInsideEncounterCone && bHasClearSight;

		// 의도된 OR 확인 규칙의 반대 조건이다. 각도/거리 밖이면서 가시선도 끊겨야 타이머가 흐른다.
		const bool bCompletelyUnconfirmed = !bInsideEncounterCone && !bHasClearSight;
		if (!State.bEncounterArmed)
		{
			if (bCompletelyUnconfirmed)
			{
				if (State.UnconfirmedStartTime < 0.0)
				{
					State.UnconfirmedStartTime = CurrentTime;
				}
				if (CurrentTime - State.UnconfirmedStartTime >= SafeMemorySeconds)
				{
					State.bEncounterArmed = true;
					if (bEnableHeartbeatLog)
					{
						UE_LOG(LogTemp, Warning, TEXT("[Heartbeat][EncounterArmed] Mannequin=%s | Unconfirmed=%.2fs"),
							*GetNameSafe(Mannequin), CurrentTime - State.UnconfirmedStartTime);
					}
				}
			}
			else
			{
				State.UnconfirmedStartTime = -1.0;
			}
		}

		if (State.bEncounterArmed && bEncounterVisibleNow)
		{
			const bool bFirstEncounter = !State.bHasEverBeenRecognized;
			TriggerSurpriseBPM(*Mannequin, Distance, bFirstEncounter);
			State.bEncounterArmed = false;
			State.UnconfirmedStartTime = -1.0;
		}

		if (bBaseVisibleNow || bEncounterVisibleNow)
		{
			State.bHasEverBeenRecognized = true;
		}
		State.bCurrentlyVisible = bEncounterVisibleNow;

		const bool bWasActiveForHeartbeat = State.bActiveForHeartbeat;
		State.bActiveForHeartbeat = bBaseVisibleNow;

		if (bEnableHeartbeatLog && bWasActiveForHeartbeat != State.bActiveForHeartbeat)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Heartbeat][ActiveChanged] Mannequin=%s | BaseVisible=%s | EncounterVisible=%s | ClearSight=%s"),
				*GetNameSafe(Mannequin),
				State.bActiveForHeartbeat ? TEXT("true") : TEXT("false"),
				bEncounterVisibleNow ? TEXT("true") : TEXT("false"),
				bHasClearSight ? TEXT("true") : TEXT("false"));
		}
	}

	RemoveInvalidMannequins();
}

void UPlayerHeartbeatComponent::UpdateBPM(float DeltaTime, const APawn& OwnerPawn)
{
	// 여러 활성 대상 중 가장 높은 거리 기반 BPM을 하한으로 사용한다.
	float NewDistanceBPM = 0.0f;
	int32 ActiveCount = 0;
	int32 NearbyVFXCount = 0;
	float NearestVFXDistance = TNumericLimits<float>::Max();
	const float SafeVFXRange = FMath::Max(HeartbeatRange, 0.0f);
	for (const TPair<TWeakObjectPtr<AMannequinAICharacter>, FHeartbeatMannequinState>& Entry : MannequinStates)
	{
		const AMannequinAICharacter* Mannequin = Entry.Key.Get();
		if (!IsValid(Mannequin))
		{
			continue;
		}

		const float Distance = FVector::Dist(OwnerPawn.GetActorLocation(), Mannequin->GetActorLocation());
		// VFX는 심박 인지·시야 상태와 분리한다. 주변에 실제로 존재하는 마네킹만 거리로 집계한다.
		if (SafeVFXRange > UE_SMALL_NUMBER && Distance < SafeVFXRange)
		{
			NearestVFXDistance = FMath::Min(NearestVFXDistance, Distance);
			++NearbyVFXCount;
		}

		if (Entry.Value.bActiveForHeartbeat)
		{
			NewDistanceBPM = FMath::Max(NewDistanceBPM, CalculateBaseBPM(Distance));
			++ActiveCount;
		}
	}
	CurrentVFXNearbyMannequinCount = NearbyVFXCount;
	NearestVFXMannequinDistance = NearestVFXDistance;

	const bool bWasActive = bHeartbeatActive;
	DistanceBPM = NewDistanceBPM;
	EncounterBPM = FMath::Max(DistanceBPM, FMath::FInterpConstantTo(
		EncounterBPM, DistanceBPM, DeltaTime, FMath::Max(EncounterDecayPerSecond, 0.0f)));
	CurrentBPM = FMath::Max(DistanceBPM, EncounterBPM);
	bHeartbeatActive = CurrentBPM > UE_SMALL_NUMBER;
	if (!bHeartbeatActive)
	{
		CurrentBPM = 0.0f;
		DistanceBPM = 0.0f;
		EncounterBPM = 0.0f;
		BeatAccumulator = 0.0f;
		if (bWasActive && bEnableHeartbeatLog)
		{
			UE_LOG(LogTemp, Log, TEXT("[Heartbeat] STOP | Base and encounter BPM reached zero"));
		}
		return;
	}

	if (bEnableHeartbeatLog &&
		FMath::Abs(CurrentBPM - LastLoggedBPM) >= FMath::Max(BPMLogThreshold, 0.0f))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Heartbeat] BPM=%.1f | DistanceFloor=%.1f | Encounter=%.1f | Active=%d"),
			CurrentBPM, DistanceBPM, EncounterBPM, ActiveCount);
		LastLoggedBPM = CurrentBPM;
	}
}

void UPlayerHeartbeatComponent::UpdateHeartbeatVFX(float DeltaTime, APawn& OwnerPawn)
{
	UWorld* World = GetWorld();
	if (!bEnableHeartbeatVFX || !IsValid(World) || World->GetNetMode() == NM_DedicatedServer)
	{
		ReleaseHeartbeatVFX();
		return;
	}

	using namespace ProjectProject01HeartbeatVFX;
	const bool bHasNearbyTarget = CurrentVFXNearbyMannequinCount > 0 &&
		FMath::IsFinite(NearestVFXMannequinDistance);
	const float SafeHeartbeatRange = FMath::Max(HeartbeatRange, 0.0f);
	// 범위 밖은 보간 잔상을 남기지 않고 즉시 완전 투명(효과 미부착) 상태로 만든다.
	if (!bHasNearbyTarget || SafeHeartbeatRange <= UE_SMALL_NUMBER ||
		NearestVFXMannequinDistance >= SafeHeartbeatRange)
	{
		HeartbeatVFXProximityAlpha = 0.0f;
		HeartbeatVFXDensityAlpha = 0.0f;
		ReleaseHeartbeatVFX();
		return;
	}

	const float TargetProximity = CalculateProximityAlpha(NearestVFXMannequinDistance, SafeHeartbeatRange);
	const float TargetDensity = CalculateDensityAlpha(
		CurrentVFXNearbyMannequinCount, HeartbeatVFXDensityCountForMax);
	const float BlendSpeed = TargetProximity > HeartbeatVFXProximityAlpha
		? HeartbeatVFXBlendInSpeed
		: HeartbeatVFXBlendOutSpeed;

	HeartbeatVFXProximityAlpha = InterpAlpha(
		HeartbeatVFXProximityAlpha, TargetProximity, DeltaTime, BlendSpeed);
	HeartbeatVFXDensityAlpha = InterpAlpha(
		HeartbeatVFXDensityAlpha, TargetDensity, DeltaTime, BlendSpeed);

	const UProjectProject01GameUserSettings* UserSettings = UProjectProject01GameUserSettings::Get();
	const float LocalVFXScale = IsValid(UserSettings) ? UserSettings->GetLocalVFXScale() : 1.0f;
	const float EffectOpacity = FMath::Clamp(
		HeartbeatVFXProximityAlpha * HeartbeatVFXMaxOpacity * LocalVFXScale, 0.0f, 1.0f);
	if (EffectOpacity <= KINDA_SMALL_NUMBER && TargetProximity <= KINDA_SMALL_NUMBER)
	{
		HeartbeatVFXProximityAlpha = 0.0f;
		HeartbeatVFXDensityAlpha = 0.0f;
		if (bHeartbeatVFXBlendableAttached && HeartbeatVFXCamera.IsValid() && IsValid(HeartbeatVFXInstance))
		{
			HeartbeatVFXCamera->RemoveBlendable(HeartbeatVFXInstance);
			bHeartbeatVFXBlendableAttached = false;
		}
		return;
	}

	if (!EnsureHeartbeatVFX(OwnerPawn) || !IsValid(HeartbeatVFXInstance))
	{
		return;
	}

	HeartbeatVFXInstance->SetScalarParameterValue(EffectOpacityParameter, EffectOpacity);
	HeartbeatVFXInstance->SetScalarParameterValue(
		DistortionAmountParameter, HeartbeatVFXMaxDistortionAmount * HeartbeatVFXDensityAlpha * LocalVFXScale);
	HeartbeatVFXInstance->SetScalarParameterValue(
		DistortionSpeedParameter, FMath::Lerp(HeartbeatVFXMinDistortionSpeed, HeartbeatVFXMaxDistortionSpeed, HeartbeatVFXDensityAlpha));
	HeartbeatVFXInstance->SetScalarParameterValue(
		DistortionFrequencyParameter, FMath::Lerp(HeartbeatVFXMinDistortionFrequency, HeartbeatVFXMaxDistortionFrequency, HeartbeatVFXDensityAlpha));
}

bool UPlayerHeartbeatComponent::EnsureHeartbeatVFX(APawn& OwnerPawn)
{
	UCameraComponent* Camera = OwnerPawn.FindComponentByClass<UCameraComponent>();
	if (!IsValid(Camera))
	{
		return false;
	}

	if (HeartbeatVFXCamera.Get() != Camera)
	{
		ReleaseHeartbeatVFX();
		HeartbeatVFXCamera = Camera;
	}

	if (!IsValid(HeartbeatVFXInstance))
	{
		UMaterialInterface* Material = HeartbeatVFXMaterial.LoadSynchronous();
		if (!IsValid(Material))
		{
			if (!bHeartbeatVFXLoadFailureLogged)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[HeartbeatVFX] Post-process material is unavailable: %s"),
					*HeartbeatVFXMaterial.ToSoftObjectPath().ToString());
				bHeartbeatVFXLoadFailureLogged = true;
			}
			return false;
		}

		HeartbeatVFXInstance = UMaterialInstanceDynamic::Create(Material, this);
		if (!ensureMsgf(IsValid(HeartbeatVFXInstance), TEXT("Heartbeat VFX dynamic material creation failed.")))
		{
			return false;
		}
		bHeartbeatVFXLoadFailureLogged = false;
	}

	if (!bHeartbeatVFXBlendableAttached)
	{
		Camera->AddOrUpdateBlendable(HeartbeatVFXInstance, 1.0f);
		bHeartbeatVFXBlendableAttached = true;
	}
	return true;
}

void UPlayerHeartbeatComponent::ReleaseHeartbeatVFX()
{
	if (bHeartbeatVFXBlendableAttached && HeartbeatVFXCamera.IsValid() && IsValid(HeartbeatVFXInstance))
	{
		HeartbeatVFXCamera->RemoveBlendable(HeartbeatVFXInstance);
	}
	bHeartbeatVFXBlendableAttached = false;
	HeartbeatVFXCamera.Reset();
	HeartbeatVFXInstance = nullptr;
}

bool UPlayerHeartbeatComponent::EnsureHeartbeatSFX(APawn& OwnerPawn)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_DedicatedServer || !OwnerPawn.IsLocallyControlled())
	{
		return false;
	}

	UHeartbeatSynthComponent* Synth = HeartbeatSFXSynth.Get();
	if (!IsValid(Synth))
	{
		Synth = OwnerPawn.FindComponentByClass<UHeartbeatSynthComponent>();
		HeartbeatSFXSynth = Synth;
	}
	if (!ensureMsgf(IsValid(Synth), TEXT("Player %s has no Heartbeat SFX synth component."), *GetNameSafe(&OwnerPawn)))
	{
		return false;
	}
	if (UProjectProject01GameUserSettings* UserSettings = UProjectProject01GameUserSettings::Get(); IsValid(UserSettings))
	{
		Synth->SoundClass = UserSettings->GetSFXSoundClass();
	}
	Synth->SetVolumeMultiplier(HeartbeatSFXVolumeMultiplier);
	if (!Synth->IsPlaying())
	{
		Synth->Start();
	}
	return true;
}

void UPlayerHeartbeatComponent::ReleaseHeartbeatSFX()
{
	if (UHeartbeatSynthComponent* Synth = HeartbeatSFXSynth.Get(); IsValid(Synth) && Synth->IsPlaying())
	{
		Synth->Stop();
	}
	HeartbeatSFXSynth.Reset();
}

void UPlayerHeartbeatComponent::UpdateBeatOutput(float DeltaTime, APawn& OwnerPawn)
{
	if (!bHeartbeatActive || CurrentBPM <= 0.0f)
	{
		ReleaseHeartbeatSFX();
		return;
	}

	// BPM을 박동 간격(60 / BPM)으로 변환한다.
	BeatAccumulator += DeltaTime;
	const float BeatIntervalSeconds = 60.0f / FMath::Max(CurrentBPM, 1.0f);
	if (BeatAccumulator >= BeatIntervalSeconds)
	{
		BeatAccumulator = FMath::Fmod(BeatAccumulator, BeatIntervalSeconds);
		if (EnsureHeartbeatSFX(OwnerPawn))
		{
			const float MinimumReferenceBPM = FMath::Max(BaseMinBPM, 1.0f);
			const float MaximumReferenceBPM = FMath::Max3(BaseMaxBPM, EncounterMaxBPM, MinimumReferenceBPM + 1.0f);
			const float BPMAlpha = FMath::GetRangePct(MinimumReferenceBPM, MaximumReferenceBPM, CurrentBPM);
			const float Strength = FMath::Lerp(0.48f, 0.82f, FMath::Clamp(BPMAlpha, 0.0f, 1.0f));
			HeartbeatSFXSynth->TriggerHeartbeat(Strength);
		}
		if (bEnableHeartbeatLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Heartbeat] THUMP | BPM=%.1f | Interval=%.3fs"),
				CurrentBPM, BeatIntervalSeconds);
		}
	}
}

void UPlayerHeartbeatComponent::RemoveInvalidMannequins()
{
	for (auto It = MannequinStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UPlayerHeartbeatComponent::GetPlayerViewPoint(
	const APawn& OwnerPawn, FVector& OutLocation, FVector& OutForward) const
{
	const AController* Controller = OwnerPawn.GetController();
	if (!IsValid(Controller))
	{
		return false;
	}
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(OutLocation, ViewRotation);
	OutForward = ViewRotation.Vector().GetSafeNormal();
	return !OutForward.IsNearlyZero();
}

bool UPlayerHeartbeatComponent::IsInsideVisionCone(
	const AMannequinAICharacter& Mannequin,
	const FVector& ViewLocation, const FVector& ViewForward, const float HalfAngleDegrees) const
{
	const float MinimumViewDot = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(HalfAngleDegrees, 0.0f, 180.0f)));
	FVector HorizontalViewForward = ViewForward;
	HorizontalViewForward.Z = 0.0f;
	HorizontalViewForward.Normalize();
	if (HorizontalViewForward.IsNearlyZero())
	{
		return false;
	}
	FVector HorizontalViewToMannequin = Mannequin.GetActorLocation() - ViewLocation;
	HorizontalViewToMannequin.Z = 0.0f;
	HorizontalViewToMannequin.Normalize();
	return !HorizontalViewToMannequin.IsNearlyZero() &&
		FVector::DotProduct(HorizontalViewForward, HorizontalViewToMannequin) >= MinimumViewDot;
}

bool UPlayerHeartbeatComponent::HasClearLineOfSight(
	const APawn& OwnerPawn, const AMannequinAICharacter& Mannequin, const FVector& ViewLocation) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PlayerHeartbeatRetention), true, &OwnerPawn);
	FHitResult HitResult;
	if (UProjectProject01DiagnosticsSubsystem::IsCaptureActiveForWorld(World))
	{
		UProjectProject01DiagnosticsSubsystem::AddWorkMetrics(World, 0.0, 0, 1);
	}
	return World->LineTraceSingleByChannel(HitResult, ViewLocation, Mannequin.GetActorLocation(),
		ECC_GameTraceChannel1, QueryParams) && HitResult.GetActor() == &Mannequin;
}

float UPlayerHeartbeatComponent::CalculateBaseBPM(const float Distance) const
{
	const float SafeRange = FMath::Max(BaseVisionRange, UE_SMALL_NUMBER);
	const float DistanceAlpha = 1.0f - FMath::Clamp(Distance / SafeRange, 0.0f, 1.0f);
	const float SafeMinBPM = FMath::Max(BaseMinBPM, 1.0f);
	const float SafeMaxBPM = FMath::Max(BaseMaxBPM, SafeMinBPM);
	return FMath::Lerp(SafeMinBPM, SafeMaxBPM, DistanceAlpha);
}

float UPlayerHeartbeatComponent::CalculateEncounterBPM(const float Distance) const
{
	const float SafeRange = FMath::Max(EncounterVisionRange, UE_SMALL_NUMBER);
	const float DistanceAlpha = 1.0f - FMath::Clamp(Distance / SafeRange, 0.0f, 1.0f);
	const float SafeMinBPM = FMath::Max(EncounterMinBPM, 1.0f);
	const float SafeMaxBPM = FMath::Max(EncounterMaxBPM, SafeMinBPM);
	return FMath::Lerp(SafeMinBPM, SafeMaxBPM, DistanceAlpha);
}

void UPlayerHeartbeatComponent::TriggerSurpriseBPM(
	AMannequinAICharacter& Mannequin, float Distance, bool bFirstEncounter)
{
	const float NewEncounterBPM = CalculateEncounterBPM(Distance);

	EncounterBPM = FMath::Max(EncounterBPM, NewEncounterBPM);
	bHeartbeatActive = true;
	if (bEnableHeartbeatLog)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Heartbeat] %s | Mannequin=%s | Distance=%.0fcm | Candidate=%.1f | EncounterBPM=%.1f"),
			bFirstEncounter ? TEXT("FIRST ENCOUNTER") : TEXT("REDISCOVERED"),
			*GetNameSafe(&Mannequin), Distance, NewEncounterBPM, EncounterBPM);
	}
}

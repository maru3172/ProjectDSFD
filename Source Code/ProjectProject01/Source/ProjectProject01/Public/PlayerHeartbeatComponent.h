// File: Source/ProjectProject01/Public/PlayerHeartbeatComponent.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SynthComponent.h"
#include "PlayerHeartbeatComponent.generated.h"

class AMannequinAICharacter;
class APawn;
class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTensionBreathingSynthComponent;
struct FPlayerTuningRow;

// 외부 음원 없이 낮고 둔한 단일 심장 박동을 생성하는 로컬 전용 신시사이저다.
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class PROJECTPROJECT01_API UHeartbeatSynthComponent final : public USynthComponent
{
	GENERATED_BODY()
	friend class FProjectHeartbeatTimbreTest;

public:
	UHeartbeatSynthComponent(const FObjectInitializer& ObjectInitializer);

	// BPM에 따른 강도로 짧은 단일 박동을 다시 시작한다.
	void TriggerHeartbeat(float Strength);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	int32 SynthSampleRate = 48000;
	int32 PulseFrameIndex = 0;
	float PulsePhase = 0.0f;
	float PulseStrength = 0.0f;
	bool bPulseActive = false;
};

/** 외부 음원 없이 피격 순간의 낮고 거친 고통 신음을 생성하는 3D 공간음 신시사이저입니다. */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class PROJECTPROJECT01_API UPainGroanSynthComponent final : public USynthComponent
{
	GENERATED_BODY()
	friend class FProjectPainBreathingTest;

public:
	UPainGroanSynthComponent(const FObjectInitializer& ObjectInitializer);
	void TriggerPain(bool bFatal, float Intensity = 1.0f);
	static float GetReactionDuration(bool bFatal, float Intensity = 1.0f);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
    FThreadSafeCounter PlaybackGeneration;
    int32 AudioGeneration=0;
	int32 SynthSampleRate = 48000;
	int32 VoiceFrameIndex = 0;
	float VoicePhase = 0.0f;
	float FlutterPhase = 0.0f;
	float BreathNoiseFiltered = 0.0f;
	float BreathNoiseLow = 0.0f;
	float VoiceDurationSeconds = 0.75f;
	float VoiceIntensity = 1.0f;
	uint32 NoiseState = 0x91E10DA5u;
	bool bFatalVoice = false;
	bool bVoiceActive = false;
};

/** 몸이 대리석 바닥에 힘없이 닿는 둔탁한 충격과 옷 마찰음을 합성합니다. */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class PROJECTPROJECT01_API UBodyFallSynthComponent final : public USynthComponent
{
	GENERATED_BODY()

public:
	UBodyFallSynthComponent(const FObjectInitializer& ObjectInitializer);
	void TriggerBodyFall();

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
    FThreadSafeCounter PlaybackGeneration;
    int32 AudioGeneration=0;
	int32 SynthSampleRate = 48000;
	int32 ImpactFrameIndex = 0;
	float LowPhase = 0.0f;
	uint32 NoiseState = 0x4B1D3A27u;
	bool bImpactActive = false;
};

/** 마네킹 플레이어에게 포획 성공을 알리는 낮고 둔탁한 플라스틱 충격음을 생성합니다. */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class PROJECTPROJECT01_API UMannequinCaptureSynthComponent final : public USynthComponent
{
	GENERATED_BODY()

public:
	UMannequinCaptureSynthComponent(const FObjectInitializer& ObjectInitializer);
	void TriggerImpact(bool bElimination);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
    FThreadSafeCounter PlaybackGeneration;
    int32 AudioGeneration=0;
	int32 SynthSampleRate = 48000;
	int32 ImpactFrameIndex = 0;
	float LowPhase = 0.0f;
	float PlasticPhase = 0.0f;
	float ImpactDurationSeconds = 0.34f;
	uint32 NoiseState = 0xD13A761Bu;
	bool bImpactActive = false;
};

// 각 마네킹의 인지, 노출, 재발견 상태를 보관한다.
struct FHeartbeatMannequinState
{
	// 플레이어가 이 마네킹을 한 번이라도 직접 확인했는지 나타낸다.
	bool bHasEverBeenRecognized = false;
	// 현재 플레이어 시야 안에서 장애물 없이 보이는지 나타낸다.
	bool bCurrentlyVisible = false;
	// 현재 거리 기반 심박 계산에 반영되는지 나타낸다.
	bool bActiveForHeartbeat = false;
	// 조우 시야와 직접 가시선이 모두 끊긴 시각이다. 음수면 미확인 타이머가 동작하지 않는다.
	double UnconfirmedStartTime = -1.0;
	// 최초 조우 또는 3초 미확인 이후 새로운 조우 BPM을 받을 수 있는 상태다.
	bool bEncounterArmed = true;
};

// 로컬 플레이어가 직접 인지한 마네킹을 기준으로 심박 BPM을 계산한다.
UCLASS(ClassGroup=(Player), meta=(BlueprintSpawnableComponent))
class PROJECTPROJECT01_API UPlayerHeartbeatComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class FProjectTensionBreathingTest;

public:
	UPlayerHeartbeatComponent();

	// 현재 로그 박동에 사용하는 최종 BPM이다. 비활성 상태에서는 0이다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|Runtime")
	float GetCurrentBPM() const { return PresentedBPM; }

	// 이번 플레이에서 한 번이라도 직접 본 마네킹 수다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|Runtime")
	int32 GetKnownMannequinCount() const;

	// 현재 심박 계산에 반영되는 인지 완료 마네킹 수다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|Runtime")
	int32 GetActiveMannequinCount() const;

	// 현재 심박 로그가 재생 중인지 반환한다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|Runtime")
	bool IsHeartbeatActive() const { return PresentedBPM > UE_SMALL_NUMBER; }

	// 심박 VFX의 거리·밀집도 계산에 사용하는 최대 거리다.
	UFUNCTION(BlueprintPure, Category="Heartbeat|VFX")
	float GetHeartbeatRange() const { return FMath::Max(HeartbeatRange, 0.0f); }

	UFUNCTION(BlueprintPure, Category="Heartbeat|Base")
	float GetBaseVisionRange() const { return FMath::Max(BaseVisionRange, 0.0f); }

	UFUNCTION(BlueprintPure, Category="Heartbeat|Base")
	float GetBaseVisionHalfAngleDegrees() const
	{
		return FMath::Clamp(BaseVisionHalfAngleDegrees, 0.0f, 180.0f);
	}

	UFUNCTION(BlueprintPure, Category="Heartbeat|Encounter")
	float GetEncounterVisionRange() const { return FMath::Max(EncounterVisionRange, 0.0f); }

	UFUNCTION(BlueprintPure, Category="Heartbeat|Encounter")
	float GetEncounterVisionHalfAngleDegrees() const
	{
		return FMath::Clamp(EncounterVisionHalfAngleDegrees, 0.0f, 180.0f);
	}

	// 최초 인지 기록과 현재 심박 상태를 모두 초기화한다.
	UFUNCTION(BlueprintCallable, Category="Heartbeat|Runtime")
	void ResetHeartbeatState();
	/** Pain reaction owns the breathing channel until its entire recovery sequence ends. */
	void SuspendTensionBreathingForPain(bool bFatal, float DurationSeconds);
	void ResetTensionBreathing();

	/** 서버가 검증한 DataTable의 심박 설정을 로컬 생존자에게 적용합니다. */
	void ApplyHeartbeatTuning(const FPlayerTuningRow& Tuning);
	void GetDiagnosticAppliedTuning(FPlayerTuningRow& OutTuning) const;

	/** Tools에서 선택해 DefaultGame.ini에 저장한 실제 화면 VFX 경로입니다. */
	TArray<FSoftObjectPath> GetHeartbeatVFXMaterialPaths() const;
	/** 연결 변경/머티리얼 재생성 후 이전 MID를 제거하고 다음 로컬 Tick에 다시 연결합니다. */
	void RefreshHeartbeatVFXMaterial();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	// 월드에 존재하는 마네킹 목록을 주기적으로 갱신한다.
	void RefreshMannequinCache();
	// 실제 시야 진입과 인지 대상 유지 여부를 갱신한다.
	void UpdateDetectionStates(APawn& OwnerPawn);
	// 기본 시야 대상의 거리 BPM과 감소 중인 조우 BPM을 합쳐 최종 BPM을 계산한다.
	void UpdateBPM(float DeltaTime, const APawn& OwnerPawn);
	// 시야와 무관하게 HeartbeatRange 안의 가장 가까운 마네킹 거리와 수를 로컬 화면 후처리 파라미터로 변환한다.
	void UpdateHeartbeatVFX(float DeltaTime, APawn& OwnerPawn);
	// 로컬 Pawn 카메라에 동적 후처리 머티리얼을 안전하게 연결한다.
	bool EnsureHeartbeatVFX(APawn& OwnerPawn);
	// 연결한 카메라에서 후처리를 제거하고 런타임 상태를 초기화한다.
	void ReleaseHeartbeatVFX();
	// CurrentBPM을 실제 박동 간격으로 환산해 SFX와 THUMP 로그를 같은 시점에 출력한다.
	void UpdateBeatOutput(float DeltaTime, APawn& OwnerPawn);
	bool EnsureHeartbeatSFX(APawn& OwnerPawn);
	void ReleaseHeartbeatSFX();
	void UpdateBreathingOutput(float DeltaTime, APawn& OwnerPawn);
	void ReleaseBreathingSFX();
	bool CanResumeTensionBreathing(double WorldSeconds) const;
	bool ShouldPlayTensionBreathing() const;
	float CalculateBreathingTension() const;
	bool UpdateReliefBreathState(bool bSprinting, double WorldSeconds);
	bool ShouldCarryTensionWhileSprinting(bool bSprinting) const;
	bool ShouldHoldThreatWhileSprinting(bool bSprinting) const;
	void MarkReliefBreathStarted(double WorldSeconds);
	void ClearReliefBreathState();
	float GetBreathingCalmAlpha(double WorldSeconds) const;
	void UpdatePresentedHeartbeat(bool bSprinting, double WorldSeconds, float DeltaTime);
	static constexpr float BreathingCalmDownSeconds = 2.0f;
	// 파괴된 마네킹의 약한 참조를 내부 상태에서 제거한다.
	void RemoveInvalidMannequins();
	// 소유 Pawn의 실제 플레이 시점과 정면 방향을 가져온다.
	bool GetPlayerViewPoint(const APawn& OwnerPawn, FVector& OutLocation, FVector& OutForward) const;
	// 지정한 수평 반각 안에 마네킹 중심이 들어오는지 검사한다.
	bool IsInsideVisionCone(const AMannequinAICharacter& Mannequin,
		const FVector& ViewLocation, const FVector& ViewForward, float HalfAngleDegrees) const;
	// 카메라와 마네킹 사이에 시야를 막는 물체가 없는지 확인한다.
	bool HasClearLineOfSight(const APawn& OwnerPawn, const AMannequinAICharacter& Mannequin,
		const FVector& ViewLocation) const;
	float CalculateBaseBPM(float Distance) const;
	float CalculateEncounterBPM(float Distance) const;
	// 최초 발견 또는 재발견에 따른 일시적인 BPM 상승을 발생시킨다.
	void TriggerSurpriseBPM(AMannequinAICharacter& Mannequin, float Distance, bool bFirstEncounter);

	// 아래 값은 Player DataTable에서 적용하며, 값이 없을 때는 이 C++ 기본값을 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Base",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="cm"))
	float BaseVisionRange = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Base",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0", Units="deg"))
	float BaseVisionHalfAngleDegrees = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Base",
		meta=(AllowPrivateAccess="true", ClampMin="1.0", UIMin="1.0"))
	float BaseMinBPM = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Base",
		meta=(AllowPrivateAccess="true", ClampMin="1.0", UIMin="1.0"))
	float BaseMaxBPM = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="cm"))
	float EncounterVisionRange = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0", Units="deg"))
	float EncounterVisionHalfAngleDegrees = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="1.0", UIMin="1.0"))
	float EncounterMinBPM = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="1.0", UIMin="1.0"))
	float EncounterMaxBPM = 165.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float EncounterDecayPerSecond = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Encounter",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="s"))
	float EncounterMemorySeconds = 3.0f;

	// VFX 거리·밀집도 계산에 사용하는 최대 거리다. BPM 시야 거리와는 독립적이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", Units="cm"))
	float HeartbeatRange = 1500.0f;

	// 시야 판정 주기다. 낮을수록 즉각적이지만 검사량이 늘어난다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.01", UIMin="0.01", Units="s"))
	float VisionCheckInterval = 0.05f;

	// 런타임에 생성·삭제되는 마네킹 목록을 다시 찾는 주기다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Detection",
		meta=(AllowPrivateAccess="true", ClampMin="0.1", UIMin="0.1", Units="s"))
	float MannequinRefreshInterval = 1.0f;

	// false면 BPM 계산은 유지하면서 관련 로그만 끈다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Debug", meta=(AllowPrivateAccess="true"))
	bool bEnableHeartbeatLog = true;

	// 마지막 출력값과 BPM 차이가 이 값 이상일 때 상태 로그를 갱신한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|Debug",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float BPMLogThreshold = 1.0f;

	// 합성 심장 박동의 최종 음량 배수다. Player DataTable 재적용 시 즉시 갱신된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|SFX",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float HeartbeatSFXVolumeMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true"))
	bool bEnableHeartbeatVFX = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX",
		meta=(AllowPrivateAccess="true", ClampMin="1", UIMin="1"))
	int32 HeartbeatVFXDensityCountForMax = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", ClampMax="1.0"))
	float HeartbeatVFXMaxOpacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXMaxNoiseIntensity = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXMinNoiseSpeed = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXMaxNoiseSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXMinNoiseFrequency = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXMaxNoiseFrequency = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXMaxDistortionAmount = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXMinDistortionSpeed = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXMaxDistortionSpeed = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXMinDistortionFrequency = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXMaxDistortionFrequency = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXBlendInSpeed = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Heartbeat|VFX", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float HeartbeatVFXBlendOutSpeed = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category="Heartbeat|VFX", meta=(AllowedClasses="/Script/Engine.MaterialInterface"))
	TSoftObjectPtr<UMaterialInterface> HeartbeatVFXMaterial;

	// 아래 값들은 실행 중 확인만 가능한 현재 계산 결과다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Heartbeat|Runtime",
		meta=(AllowPrivateAccess="true"))
	float CurrentBPM = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Heartbeat|Runtime",
		meta=(AllowPrivateAccess="true"))
	float DistanceBPM = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Heartbeat|Runtime",
		meta=(AllowPrivateAccess="true"))
	float EncounterBPM = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Heartbeat|Runtime",
		meta=(AllowPrivateAccess="true"))
	bool bHeartbeatActive = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Heartbeat|Runtime",
		meta=(AllowPrivateAccess="true"))
	float HeartbeatVFXProximityAlpha = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Heartbeat|Runtime",
		meta=(AllowPrivateAccess="true"))
	float HeartbeatVFXDensityAlpha = 0.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> HeartbeatVFXInstances;

	TWeakObjectPtr<UCameraComponent> HeartbeatVFXCamera;
	TWeakObjectPtr<UHeartbeatSynthComponent> HeartbeatSFXSynth;
	TWeakObjectPtr<UTensionBreathingSynthComponent> BreathingSFXSynth;
	double BreathingResumeWorldSeconds = -1.0;
	bool bFatalBreathingSuppression = false;
	bool bHadBreathingThreat = false;
	bool bReliefBreathQueued = false;
	bool bRanInsideBreathingThreat = false;
	bool bHadTensionInsideBreathingThreat = false;
	float LastThreatBreathingTension = 0.0f;
	float PeakThreatBPM = 0.0f;
	float PeakThreatVFXProximity = 0.0f;
	float PeakThreatVFXDensity = 0.0f;
	float PresentedVFXProximity = 0.0f;
	float PresentedVFXDensity = 0.0f;
	float CalmStartVFXProximity = 0.0f;
	float CalmStartVFXDensity = 0.0f;
	float PresentedBPM = 0.0f;
	float PresentedHeartbeatGain = 1.0f;
	bool bCarriedTensionOutOfThreat = false;
	double BreathingCalmEndWorldSeconds = -1.0;
	double ReliefBreathEndWorldSeconds = -1.0;
	bool bHeartbeatVFXBlendableAttached = false;
	bool bHeartbeatVFXLoadFailureLogged = false;
	int32 CurrentVFXNearbyMannequinCount = 0;
	float NearestVFXMannequinDistance = TNumericLimits<float>::Max();

	// 전체 마네킹, 최초 인지 대상, 현재 활성 대상을 구분해 보관한다.
	TArray<TWeakObjectPtr<AMannequinAICharacter>> CachedMannequins;
	// 마네킹별 인지, 노출, 재발견 상태를 하나의 Map에서 관리한다.
	TMap<TWeakObjectPtr<AMannequinAICharacter>, FHeartbeatMannequinState> MannequinStates;

	// 매 프레임 계산 대신 설정된 주기로 처리하기 위한 시간 누적값이다.
	float BeatAccumulator = 0.0f;
	float VisionCheckAccumulator = 0.0f;
	float RefreshAccumulator = 0.0f;
	float LastLoggedBPM = 0.0f;
};

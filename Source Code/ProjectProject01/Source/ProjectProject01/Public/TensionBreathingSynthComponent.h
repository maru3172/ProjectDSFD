#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "TensionBreathingSynthComponent.generated.h"

/** Heartbeat-activated local breathing; silence while inactive, very quiet at low active BPM. */
UCLASS(Transient)
class PROJECTPROJECT01_API UTensionBreathingSynthComponent final : public USynthComponent
{
	GENERATED_BODY()
	friend class FProjectTensionBreathingTest;
public:
	UTensionBreathingSynthComponent(const FObjectInitializer& ObjectInitializer);
	void SetTension(float Tension);
	void PlayReliefBreath();
	static constexpr float ReliefDurationSeconds = 0.95f;
protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
private:
	int32 SynthSampleRate = 48000;
	int64 FrameIndex = 0;
	float TargetTension = 0.0f;
	float CurrentTension = 0.0f;
	float BreathTimeSeconds = 0.0f;
	float CycleLengthSeconds = 3.5f;
	float HaaDurationSeconds = 0.52f;
	bool bCycleInitialized = false;
	bool bReliefMode = false;
	int32 ReliefFrameIndex = 0;
	float CycleVariation = 1.0f;
	float FilteredNoise = 0.0f;
	float SlowNoise = 0.0f;
	float AhVoicePhase = 0.0f;
	float FormantOnePrevious = 0.0f;
	float FormantOneOlder = 0.0f;
	float FormantTwoPrevious = 0.0f;
	float FormantTwoOlder = 0.0f;
	uint32 NoiseState = 0x73a108bdu;
};

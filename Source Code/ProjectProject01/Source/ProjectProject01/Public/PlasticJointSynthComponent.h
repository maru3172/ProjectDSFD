#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "PlasticJointSynthComponent.generated.h"

/** Existing plastic body voice, spatialized independently of the long-range foot contact. */
UCLASS(Transient)
class PROJECTPROJECT01_API UPlasticJointSynthComponent final : public USynthComponent
{

	GENERATED_BODY()
	friend class FProjectFootstepTimbreTest;

public:
	UPlasticJointSynthComponent(const FObjectInitializer& ObjectInitializer);
	void TriggerMotion(uint32 Seed, float Delay, float Duration, float Gain, bool bRunning);

protected:
	virtual void BeginPlay() override;
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	int32 SynthSampleRate = 48000;
	int32 MotionFrameIndex = 0;
	uint32 MotionNoiseState = 0x819ab312u;
	float MotionPhase = 0.0f;
	float MotionNoise = 0.0f;
	float MotionNoiseSlow = 0.0f;
	float MotionDelay = 0.0f;
	float MotionDuration = 0.25f;
	float MotionGain = 0.0f;
	bool bMotionRunning = false;
	bool bMotionActive = false;
};

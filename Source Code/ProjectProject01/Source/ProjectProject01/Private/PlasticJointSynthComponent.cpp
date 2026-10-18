#include "PlasticJointSynthComponent.h"

#include "ProjectProject01SpatialAudio.h"

namespace
{
	void ConfigureNearField(USynthComponent& Synth)
	{
		// Preserve wall filtering and SFX spatialization, but never the footstep's 30m radius.
		ProjectProject01SpatialAudio::ConfigureWorldSynth(Synth);
		Synth.AttenuationOverrides.AttenuationShapeExtents = FVector(100.0f, 0.0f, 0.0f);
		Synth.AttenuationOverrides.FalloffDistance = 300.0f;
	}
}

UPlasticJointSynthComponent::UPlasticJointSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = false;
	bAutoDestroy = false;
	bStopWhenOwnerDestroyed = true;
	NumChannels = 1;
	ConfigureNearField(*this);
}

void UPlasticJointSynthComponent::BeginPlay()
{
	ConfigureNearField(*this);
	Super::BeginPlay();
}

void UPlasticJointSynthComponent::TriggerMotion(uint32 Seed, float Delay, float Duration, float Gain, bool bRunning)
{
	if (!IsPlaying()) Start();
	SynthCommand([this, Seed, Delay, Duration, Gain, bRunning]()
	{
		MotionFrameIndex = 0;
		MotionNoiseState = Seed ? Seed : 0x819ab312u;
		MotionPhase = MotionNoise = MotionNoiseSlow = 0.0f;
		MotionDelay = Delay;
		MotionDuration = Duration;
		MotionGain = Gain;
		bMotionRunning = bRunning;
		bMotionActive = true;
	});
}

bool UPlasticJointSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	SynthSampleRate = FMath::Max(SampleRate, 8000);
	MotionFrameIndex = 0;
	bMotionActive = false;
	return true;
}

int32 UPlasticJointSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	if (!OutAudio || NumSamples <= 0) return 0;
	FMemory::Memzero(OutAudio, NumSamples * sizeof(float));
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		if (!bMotionActive) break;
		const float Time = static_cast<float>(MotionFrameIndex++) / SynthSampleRate - MotionDelay;
		if (Time >= MotionDuration) { bMotionActive = false; break; }
		if (Time < 0.0f) continue;
		const float Progress = Time / MotionDuration;
		MotionNoiseState = MotionNoiseState * 1664525u + 1013904223u;
		const float Noise = static_cast<float>((MotionNoiseState >> 8) & 0x00ffffffu) / 8388607.5f - 1.0f;
		MotionNoise += (Noise - MotionNoise) * 0.12f;
		MotionNoiseSlow += (Noise - MotionNoiseSlow) * 0.025f;
		const float Grip = 0.75f + 0.25f * FMath::Sin(Time * 107.0f + FMath::Sin(Time * 39.0f) * 2.0f);
		const float Frequency = 260.0f - 75.0f * Progress + 23.0f * FMath::Sin(Time * 53.0f);
		MotionPhase = FMath::Fmod(MotionPhase + 2.0f * UE_PI * Frequency / SynthSampleRate, 2.0f * UE_PI);
		const float Envelope = FMath::Pow(FMath::Max(0.0f, FMath::Sin(UE_PI * Progress)), 1.6f);
		const float Strain = (FMath::Sin(MotionPhase) * 0.15f +
			(MotionNoise - MotionNoiseSlow) * 0.40f) * Grip * Envelope;
		const float RattleEnvelope = bMotionRunning ?
			FMath::Exp(-FMath::Square((Time - 0.045f) / 0.012f)) +
			0.6f * FMath::Exp(-FMath::Square((Time - 0.105f) / 0.016f)) : 0.0f;
		OutAudio[Index] = FMath::Clamp((Strain + MotionNoise * RattleEnvelope * 0.20f) * MotionGain, -0.95f, 0.95f);
	}
	return NumSamples;
}

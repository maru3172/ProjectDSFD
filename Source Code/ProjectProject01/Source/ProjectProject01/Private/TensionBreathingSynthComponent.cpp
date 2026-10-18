#include "TensionBreathingSynthComponent.h"

UTensionBreathingSynthComponent::UTensionBreathingSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = false;
	bAutoDestroy = false;
	bStopWhenOwnerDestroyed = true;
	bAllowSpatialization = false;
	bIsUISound = false;
	bOverrideAttenuation = true;
	AttenuationOverrides.bAttenuate = false;
	AttenuationOverrides.bEnableOcclusion = false;
	NumChannels = 1;
}

void UTensionBreathingSynthComponent::SetTension(float Tension)
{
	const float SafeTension = FMath::IsFinite(Tension) ? FMath::Clamp(Tension, 0.0f, 1.0f) : 0.0f;
	if (!IsPlaying()) Start();
	SynthCommand([this, SafeTension]()
	{
		if (bReliefMode)
		{
			bReliefMode = false;
			bCycleInitialized = false;
			FrameIndex = 0;
			BreathTimeSeconds = 0.0f;
		}
		TargetTension = SafeTension;
	});
}

void UTensionBreathingSynthComponent::PlayReliefBreath()
{
	if (!IsPlaying()) Start();
	SynthCommand([this]()
	{
		bReliefMode = true;
		ReliefFrameIndex = 0;
		FilteredNoise = SlowNoise = 0.0f;
	});
}

bool UTensionBreathingSynthComponent::Init(int32& SampleRate)
{
	SynthSampleRate = FMath::Max(SampleRate, 8000);
	FrameIndex = 0;
	TargetTension = CurrentTension = BreathTimeSeconds = FilteredNoise = SlowNoise = 0.0f;
	AhVoicePhase = FormantOnePrevious = FormantOneOlder = FormantTwoPrevious = FormantTwoOlder = 0.0f;
	bCycleInitialized = false;
	bReliefMode = false;
	ReliefFrameIndex = 0;
	CycleLengthSeconds = 3.5f;
	HaaDurationSeconds = 0.52f;
	CycleVariation = 1.0f;
	NoiseState = 0x73a108bdu;
	return true;
}

int32 UTensionBreathingSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	if (!OutAudio || NumSamples <= 0) return 0;
	const float TimeStep = 1.0f / SynthSampleRate;
	const float Blend = 1.0f - FMath::Exp(-TimeStep / 0.5f);
	// Broad open /ah/ mouth resonances, rather than the closed, low "hoo" texture.
	const float Radius = FMath::Exp(-UE_PI * 220.0f * TimeStep);
	const float FirstCoefficient = 2.0f * Radius * FMath::Cos(2.0f * UE_PI * 760.0f * TimeStep);
	const float SecondCoefficient = 2.0f * Radius * FMath::Cos(2.0f * UE_PI * 1150.0f * TimeStep);
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		if (bReliefMode)
		{
			const float Time = static_cast<float>(ReliefFrameIndex++) * TimeStep;
			OutAudio[Index] = 0.0f;
			if (Time < ReliefDurationSeconds)
			{
				NoiseState = NoiseState * 1664525u + 1013904223u;
				const float Noise = static_cast<float>((NoiseState >> 8) & 0x00ffffffu) / 8388607.5f - 1.0f;
				FilteredNoise += (Noise - FilteredNoise) * 0.10f;
				SlowNoise += (Noise - SlowNoise) * 0.018f;
				const float Progress = Time / ReliefDurationSeconds;
				const float Envelope = FMath::Clamp(Time / 0.06f, 0.0f, 1.0f) *
					FMath::Pow(FMath::Max(0.0f, FMath::Sin(UE_PI * Progress)), 1.2f) * (1.0f - Progress * 0.55f);
				OutAudio[Index] = (FilteredNoise - SlowNoise) * Envelope * 0.065f;
			}
			continue; // One sigh only; never fall back into a tension loop on completion.
		}
		CurrentTension += (TargetTension - CurrentTension) * Blend;
		// A close normal chase must sound urgent too, not only the higher surprise-BPM peak.
		const float Urgency = FMath::SmoothStep(0.15f, 0.55f, CurrentTension);
		const float WantedCycleLength = FMath::Lerp(3.5f, 0.55f, Urgency) * CycleVariation;
		if (!bCycleInitialized)
		{
			// Latch timing once per breath, so a BPM change cannot reopen the same exhalation.
			CycleLengthSeconds = WantedCycleLength;
			HaaDurationSeconds = FMath::Lerp(0.52f, 0.22f, Urgency);
			bCycleInitialized = true;
		}
		// A sudden close threat may shorten the remaining silence, never cut/reopen the current haa.
		if (BreathTimeSeconds >= 0.06f + HaaDurationSeconds + 0.10f)
			CycleLengthSeconds = FMath::Min(CycleLengthSeconds, WantedCycleLength);
		NoiseState = NoiseState * 1664525u + 1013904223u;
		const float Noise = static_cast<float>((NoiseState >> 8) & 0x00ffffffu) / 8388607.5f - 1.0f;
		FilteredNoise += (Noise - FilteredNoise) * FMath::Lerp(0.10f, 0.28f, Urgency);
		SlowNoise += (Noise - SlowNoise) * 0.018f;
		float Envelope = 0.0f;
		const float ExhaleTime = BreathTimeSeconds - 0.06f;
		const float Progress = FMath::Clamp(ExhaleTime / HaaDurationSeconds, 0.0f, 1.0f);
		if (ExhaleTime >= 0.0f && ExhaleTime < HaaDurationSeconds)
			Envelope = FMath::Lerp(0.70f, 0.85f, Urgency) *
				FMath::Clamp(ExhaleTime / FMath::Lerp(0.045f, 0.012f, Urgency), 0.0f, 1.0f) *
				FMath::Pow(FMath::Max(0.0f, FMath::Sin(UE_PI * Progress)), FMath::Lerp(1.1f, 0.65f, Urgency));
		// One breathy "haa", then silence. No second audible inhalation in the same cycle.
		AhVoicePhase = FMath::Fmod(AhVoicePhase + 2.0f * UE_PI * FMath::Lerp(125.0f, 100.0f, Progress) * TimeStep, 2.0f * UE_PI);
		const float Source = Noise * (0.90f + 0.10f * FMath::Sin(AhVoicePhase));
		const float First = (1.0f - Radius) * Source + FirstCoefficient * FormantOnePrevious - Radius * Radius * FormantOneOlder;
		const float Second = (1.0f - Radius) * Source + SecondCoefficient * FormantTwoPrevious - Radius * Radius * FormantTwoOlder;
		FormantOneOlder = FormantOnePrevious; FormantOnePrevious = First;
		FormantTwoOlder = FormantTwoPrevious; FormantTwoPrevious = Second;
		const float OpenBreath = (FilteredNoise - SlowNoise) * 0.55f + (First + Second * 0.65f) * 0.45f;
		const float Time = static_cast<float>(FrameIndex++) * TimeStep;
		const float Strain = 1.0f + CurrentTension * FMath::Lerp(0.18f, 0.30f, Urgency) *
			FMath::Sin(Time * 47.0f + FMath::Sin(Time * 17.0f) * 1.2f);
		const float FadeIn = FMath::Clamp(Time / 0.25f, 0.0f, 1.0f);
		OutAudio[Index] = FMath::Clamp(OpenBreath * Envelope * Strain * FadeIn *
			FMath::Lerp(0.008f, 0.15f, FMath::Pow(CurrentTension, 1.4f)), -0.20f, 0.20f);
		BreathTimeSeconds += TimeStep;
		if (BreathTimeSeconds >= CycleLengthSeconds)
		{
			// Discard overshoot after shortening a long silence; do not skip into another breath.
			BreathTimeSeconds = 0.0f;
			CycleVariation = FMath::Lerp(0.94f, 1.08f, static_cast<float>((NoiseState >> 8) & 0xffffu) / 65535.0f);
			bCycleInitialized = false;
		}
	}
	return NumSamples;
}

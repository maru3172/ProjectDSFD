// File: Source/ProjectProject01/Private/PlayerHeartbeatComponent.cpp
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#include "PlayerHeartbeatComponent.h"
#include "TensionBreathingSynthComponent.h"
#include "PlayerCharacter.h"
#include "Async/Async.h"
#include "ProjectProject01GameInstance.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "MannequinAICharacter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Misc/ConfigCacheIni.h"
#include "ProjectProject01TuningData.h"
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "Sound/SoundAttenuation.h"
#include "ProjectProject01SpatialAudio.h"

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

namespace ProjectProject01PainVoice
{
	constexpr float GroanEndSeconds = 0.34f;
	constexpr float PantStartSeconds = 0.85f;
	constexpr float PantCount = 3.0f;
	constexpr float ExhaleDurationSeconds = 0.42f;
	constexpr float BetweenExhalesPauseSeconds = 0.38f;
	constexpr float BreathAmplitudeScale = 0.32f;
	constexpr float FatalGroanSeconds = 0.38f;

	float GetPantRate(const float /*Intensity*/)
	{
		// Repeated hits increase strain/volume, never rush the pauses between exhalations.
		return 1.0f / (ExhaleDurationSeconds + BetweenExhalesPauseSeconds);
	}

	float GetPantEndSeconds(const float Intensity)
	{
		return PantStartSeconds + (PantCount - 1.0f) / GetPantRate(Intensity) + ExhaleDurationSeconds;
	}

	float GetNonFatalDurationSeconds(const float Intensity)
	{
		return GetPantEndSeconds(Intensity);
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
	// Exactly one rounded thump per existing heartbeat-system event. No extra closing beat or local timer.
	constexpr float PulseDurationSeconds = 0.30f;
	constexpr float AttackSeconds = 0.004f;
	const int32 PulseFrameCount = FMath::Max(1, FMath::RoundToInt(PulseDurationSeconds * SynthSampleRate));
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		float Sample = 0.0f;
		if (bPulseActive && PulseFrameIndex < PulseFrameCount)
		{
			const float TimeSeconds = static_cast<float>(PulseFrameIndex) / static_cast<float>(SynthSampleRate);
			const float Progress = FMath::Clamp(TimeSeconds / PulseDurationSeconds, 0.0f, 1.0f);
			const float Attack = FMath::Clamp(TimeSeconds / AttackSeconds, 0.0f, 1.0f);
			const float Envelope = Attack * FMath::Exp(-25.0f * TimeSeconds);
			const float Frequency = FMath::Lerp(72.0f, 43.0f, FMath::Min(1.0f, Progress * 2.5f));
			PulsePhase = FMath::Fmod(
				PulsePhase + (2.0f * UE_PI * Frequency / static_cast<float>(SynthSampleRate)),
				2.0f * UE_PI);
			const float Fundamental = FMath::Sin(PulsePhase);
			const float LowHarmonic = FMath::Sin(PulsePhase * 1.92f) * 0.12f;
			const float Release = FMath::Clamp((PulseDurationSeconds - TimeSeconds) / 0.025f, 0.0f, 1.0f);
			Sample = FMath::Clamp((Fundamental + LowHarmonic) * Envelope * PulseStrength * Release, -0.95f, 0.95f);
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

UPainGroanSynthComponent::UPainGroanSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = false;
	bAutoDestroy = false;
	bStopWhenOwnerDestroyed = true;
	bAllowSpatialization = true;
	bIsUISound = false;
	NumChannels = 1;
	ProjectProject01SpatialAudio::ConfigureWorldSynth(*this);
}

float UPainGroanSynthComponent::GetReactionDuration(const bool bFatal, const float Intensity)
{
	return bFatal ? ProjectProject01PainVoice::FatalGroanSeconds :
		ProjectProject01PainVoice::GetNonFatalDurationSeconds(FMath::Clamp(Intensity, 0.35f, 1.0f));
}

void UPainGroanSynthComponent::TriggerPain(const bool bFatal, const float Intensity)
{
	ProjectProject01SpatialAudio::ConfigureWorldSynth(*this);
	const float SafeIntensity = FMath::Clamp(Intensity, 0.35f, 1.0f);
    const int32 Generation=PlaybackGeneration.Increment();
	SynthCommand([this, bFatal, SafeIntensity,Generation]()
	{
        AudioGeneration=Generation;
		VoiceFrameIndex = 0;
		VoicePhase = 0.0f;
		FlutterPhase = 0.0f;
		BreathNoiseFiltered = BreathNoiseLow = 0.0f;
		VoiceIntensity = SafeIntensity;
		bFatalVoice = bFatal;
		// 살아남은 피격은 짧은 고통 신음 뒤 정확히 세 번 헐떡이고 끝낸다.
		// Fatal hits end with a short strained grunt only, with no recovery breathing.
		VoiceDurationSeconds = GetReactionDuration(bFatal, SafeIntensity);
		NoiseState ^= bFatal ? 0xA511E9B3u : 0x63D83595u;
		bVoiceActive = true;
	});
	if (!IsPlaying())
	{
		Start();
	}
}

bool UPainGroanSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	SynthSampleRate = FMath::Max(SampleRate, 8000);
	VoiceFrameIndex = 0;
	VoicePhase = 0.0f;
	FlutterPhase = 0.0f;
	BreathNoiseFiltered = BreathNoiseLow = 0.0f;
	VoiceIntensity = 1.0f;
	bFatalVoice = false;
	bVoiceActive = false;
	return true;
}

int32 UPainGroanSynthComponent::OnGenerateAudio(float* OutAudio, const int32 NumSamples)
{
	if (OutAudio == nullptr || NumSamples <= 0)
	{
		return 0;
	}
	FMemory::Memzero(OutAudio, NumSamples * sizeof(float));
	const int32 ChannelCount = FMath::Max(NumChannels, 1);
	const int32 FrameCount = NumSamples / ChannelCount;
	const int32 VoiceFrameCount = FMath::Max(1, FMath::RoundToInt(VoiceDurationSeconds * SynthSampleRate));
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		float Sample = 0.0f;
		if (bVoiceActive && VoiceFrameIndex < VoiceFrameCount)
		{
			const float Progress = static_cast<float>(VoiceFrameIndex) / static_cast<float>(VoiceFrameCount);
			const float TimeSeconds = static_cast<float>(VoiceFrameIndex) / static_cast<float>(SynthSampleRate);
			const float Attack = FMath::Clamp(TimeSeconds / 0.025f, 0.0f, 1.0f);
			float Envelope = FMath::Pow(FMath::Max(0.0f, 1.0f - Progress), 1.45f);
			float Frequency = FMath::Lerp(132.0f, 72.0f, FMath::SmoothStep(0.0f, 1.0f, Progress));
			float NoiseAmount = 0.12f;
			float VoiceAmount = 0.48f;
			if (bFatalVoice)
			{
				// Low, choked "ugh" with no upward open-vowel scream or recovery tail.
				Frequency = FMath::Lerp(155.0f, 82.0f, FMath::Sqrt(Progress));
				Envelope = FMath::Clamp(TimeSeconds / 0.009f, 0.0f, 1.0f) *
					FMath::Exp(-4.0f * TimeSeconds) * FMath::Clamp((VoiceDurationSeconds - TimeSeconds) / 0.045f, 0.0f, 1.0f);
				VoiceAmount = 0.46f;
				NoiseAmount = 0.22f;
			}
			else
			{
				using namespace ProjectProject01PainVoice;
				// 신음과 헐떡임을 한 덩어리로 이어 붙이면 동시에 재생되는 것처럼 들린다.
				// 피격 신음이 완전히 끝난 뒤 짧은 무음 구간을 둔 다음 호흡을 시작한다.
				const float PantRate = GetPantRate(VoiceIntensity);
				const float PantEnd = GetPantEndSeconds(VoiceIntensity);
				if (TimeSeconds < GroanEndSeconds)
				{
					// 피격 직후 숨이 턱 막히는 낮은 신음/들이마심.
					const float Segment = TimeSeconds / GroanEndSeconds;
					Envelope = FMath::Clamp(TimeSeconds / 0.009f, 0.0f, 1.0f) *
						FMath::Exp(-5.5f * TimeSeconds) * FMath::Clamp((GroanEndSeconds - TimeSeconds) / 0.035f, 0.0f, 1.0f);
					Frequency = FMath::Lerp(164.0f, 92.0f, FMath::Sqrt(Segment));
					VoiceAmount = 0.43f;
					NoiseAmount = 0.28f;
				}
				else if (TimeSeconds < PantStartSeconds)
				{
					// 비명/신음 뒤의 짧은 숨 멎는 구간. 합성기 위상은 계속 진행하되
					// 출력만 0으로 만들어 다음 헐떡임이 별개의 반응으로 들리게 한다.
					Envelope = 0.0f;
					NoiseAmount = 0.0f;
				}
				else if (TimeSeconds < PantEnd)
				{
					// 느린 속도로 정확히 세 번 헐떡인다. 각 호흡의 끝은 짧게 비워서
					// 합성음이 연속 톤처럼 들리지 않게 한다.
					const float PantProgress = (TimeSeconds - PantStartSeconds) /
						FMath::Max(0.01f, PantEnd - PantStartSeconds);
					const float CycleTime = FMath::Fmod(TimeSeconds - PantStartSeconds, 1.0f / PantRate);
					const float PantPulse = CycleTime < ExhaleDurationSeconds
						? FMath::Pow(FMath::Max(0.0f, FMath::Sin(PI * CycleTime / ExhaleDurationSeconds)), 0.8f) : 0.0f;
					Envelope = PantPulse * FMath::Lerp(0.85f, 0.42f, PantProgress) * BreathAmplitudeScale;
					Frequency = FMath::Lerp(104.0f, 78.0f, PantProgress);
					VoiceAmount = 0.035f;
					NoiseAmount = FMath::Lerp(1.0f, 0.72f, PantProgress);
				}
				else
				{
					// No final sigh or post-pant tail, even if a caller supplies a longer duration.
					Envelope = 0.0f;
					NoiseAmount = 0.0f;
				}
			}
			FlutterPhase = FMath::Fmod(FlutterPhase + (2.0f * UE_PI * 5.2f / SynthSampleRate), 2.0f * UE_PI);
			const float Flutter = FMath::Sin(FlutterPhase) * (bFatalVoice ? 3.0f : FMath::Lerp(1.8f, 4.2f, VoiceIntensity));
			VoicePhase = FMath::Fmod(VoicePhase + (2.0f * UE_PI * (Frequency + Flutter) / SynthSampleRate), 2.0f * UE_PI);
			NoiseState = (NoiseState * 1664525u) + 1013904223u;
			float BreathNoise = (static_cast<float>((NoiseState >> 8) & 0xFFFFu) / 32767.5f) - 1.0f;
			BreathNoiseFiltered += (BreathNoise - BreathNoiseFiltered) * (bFatalVoice ? 0.25f : 0.18f);
			BreathNoiseLow += (BreathNoise - BreathNoiseLow) * 0.018f;
			BreathNoise = (BreathNoiseFiltered - BreathNoiseLow) * (bFatalVoice ? 1.7f : 2.0f);
			float Voice = FMath::Sin(VoicePhase) + (0.28f * FMath::Sin(VoicePhase * 2.0f)) +
				(0.11f * FMath::Sin(VoicePhase * 3.0f));
			if (bFatalVoice)
			{
				Voice = 0.75f * FMath::Sin(VoicePhase) + 0.24f * FMath::Sin(VoicePhase * 2.0f) +
					0.11f * FMath::Sin(VoicePhase * 3.0f) + 0.08f * FMath::Sin(VoicePhase * 0.5f);
			}
			const float Strength = FMath::Lerp(0.62f, 1.0f, VoiceIntensity);
			Sample = FMath::Clamp((Voice * VoiceAmount + BreathNoise * NoiseAmount) * Attack * Envelope * Strength,
				-0.88f, 0.88f);
			++VoiceFrameIndex;
			if (VoiceFrameIndex >= VoiceFrameCount)
			{
				bVoiceActive = false;
                AsyncTask(ENamedThreads::GameThread,[Weak=TWeakObjectPtr<UPainGroanSynthComponent>(this),Generation=AudioGeneration]
                {if(auto* Self=Weak.Get();Self&&Self->PlaybackGeneration.GetValue()==Generation){Self->Stop();
#if WITH_DEV_AUTOMATION_TESTS
                    ++Self->ReviewCompletedStopCount;
#endif
                }});
			}
		}
		for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
		{
			OutAudio[FrameIndex * ChannelCount + ChannelIndex] = Sample;
		}
	}
	return NumSamples;
}

UBodyFallSynthComponent::UBodyFallSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = false;
	bAutoDestroy = false;
	bStopWhenOwnerDestroyed = true;
	bAllowSpatialization = true;
	bIsUISound = false;
	NumChannels = 1;
	ProjectProject01SpatialAudio::ConfigureWorldSynth(*this);
}

void UBodyFallSynthComponent::TriggerBodyFall()
{
	ProjectProject01SpatialAudio::ConfigureWorldSynth(*this);
    const int32 Generation=PlaybackGeneration.Increment();
	SynthCommand([this,Generation]()
	{
        AudioGeneration=Generation;
		ImpactFrameIndex = 0;
		LowPhase = 0.0f;
		NoiseState ^= 0x7F4A7C15u;
		bImpactActive = true;
	});
	if (!IsPlaying())
	{
		Start();
	}
}

bool UBodyFallSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	SynthSampleRate = FMath::Max(SampleRate, 8000);
	ImpactFrameIndex = 0;
	LowPhase = 0.0f;
	bImpactActive = false;
	return true;
}

int32 UBodyFallSynthComponent::OnGenerateAudio(float* OutAudio, const int32 NumSamples)
{
	if (OutAudio == nullptr || NumSamples <= 0)
	{
		return 0;
	}
	FMemory::Memzero(OutAudio, NumSamples * sizeof(float));
	const int32 ChannelCount = FMath::Max(NumChannels, 1);
	const int32 FrameCount = NumSamples / ChannelCount;
	constexpr float ImpactDurationSeconds = 0.62f;
	const int32 ImpactFrameCount = FMath::Max(1, FMath::RoundToInt(ImpactDurationSeconds * SynthSampleRate));
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		float Sample = 0.0f;
		if (bImpactActive && ImpactFrameIndex < ImpactFrameCount)
		{
			const float TimeSeconds = static_cast<float>(ImpactFrameIndex) / SynthSampleRate;
			const float Progress = FMath::Clamp(TimeSeconds / ImpactDurationSeconds, 0.0f, 1.0f);
			const float LowEnvelope = FMath::Exp(-10.5f * TimeSeconds);
			const float Frequency = FMath::Lerp(74.0f, 39.0f, Progress);
			LowPhase = FMath::Fmod(LowPhase + (2.0f * UE_PI * Frequency / SynthSampleRate), 2.0f * UE_PI);
			NoiseState = NoiseState * 1664525u + 1013904223u;
			const float Noise = (static_cast<float>((NoiseState >> 8) & 0xFFFFu) / 32767.5f) - 1.0f;
			const float ClothEnvelope = FMath::Pow(FMath::Max(0.0f, 1.0f - Progress), 3.0f) *
				FMath::Clamp(TimeSeconds / 0.012f, 0.0f, 1.0f);
			const float MarbleContact = TimeSeconds < 0.055f
				? FMath::Sin(2.0f * UE_PI * 148.0f * TimeSeconds) * FMath::Exp(-55.0f * TimeSeconds)
				: 0.0f;
			Sample = FMath::Clamp(
				FMath::Sin(LowPhase) * LowEnvelope * 0.68f + Noise * ClothEnvelope * 0.24f + MarbleContact * 0.16f,
				-0.9f, 0.9f);
			++ImpactFrameIndex;
			if (ImpactFrameIndex >= ImpactFrameCount)
			{
				bImpactActive = false;
                AsyncTask(ENamedThreads::GameThread,[Weak=TWeakObjectPtr<UBodyFallSynthComponent>(this),Generation=AudioGeneration]
                {if(auto* Self=Weak.Get();Self&&Self->PlaybackGeneration.GetValue()==Generation){Self->Stop();
#if WITH_DEV_AUTOMATION_TESTS
                    ++Self->ReviewCompletedStopCount;
#endif
                }});
			}
		}
		for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
		{
			OutAudio[FrameIndex * ChannelCount + ChannelIndex] = Sample;
		}
	}
	return NumSamples;
}

UMannequinCaptureSynthComponent::UMannequinCaptureSynthComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = false;
	bAutoDestroy = false;
	bStopWhenOwnerDestroyed = true;
	bAllowSpatialization = false;
	bIsUISound = false;
	NumChannels = 2;
}

void UMannequinCaptureSynthComponent::TriggerImpact(const bool bElimination)
{
    const int32 Generation=PlaybackGeneration.Increment();
	SynthCommand([this, bElimination,Generation]()
	{
        AudioGeneration=Generation;
		ImpactFrameIndex = 0;
		LowPhase = 0.0f;
		PlasticPhase = 0.0f;
		ImpactDurationSeconds = bElimination ? 0.46f : 0.32f;
		NoiseState ^= bElimination ? 0x9E3779B9u : 0x85EBCA6Bu;
		bImpactActive = true;
	});
	if (!IsPlaying())
	{
		Start();
	}
}

bool UMannequinCaptureSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 2;
	SynthSampleRate = FMath::Max(SampleRate, 8000);
	ImpactFrameIndex = 0;
	LowPhase = 0.0f;
	PlasticPhase = 0.0f;
	bImpactActive = false;
	return true;
}

int32 UMannequinCaptureSynthComponent::OnGenerateAudio(float* OutAudio, const int32 NumSamples)
{
	if (OutAudio == nullptr || NumSamples <= 0)
	{
		return 0;
	}
	FMemory::Memzero(OutAudio, NumSamples * sizeof(float));
	const int32 ChannelCount = FMath::Max(NumChannels, 1);
	const int32 FrameCount = NumSamples / ChannelCount;
	const int32 ImpactFrameCount = FMath::Max(1, FMath::RoundToInt(ImpactDurationSeconds * SynthSampleRate));
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		float Sample = 0.0f;
		if (bImpactActive && ImpactFrameIndex < ImpactFrameCount)
		{
			const float TimeSeconds = static_cast<float>(ImpactFrameIndex) / SynthSampleRate;
			const float Progress = FMath::Clamp(TimeSeconds / ImpactDurationSeconds, 0.0f, 1.0f);
			const float Attack = FMath::Clamp(TimeSeconds / 0.006f, 0.0f, 1.0f);
			const float Envelope = Attack * FMath::Pow(FMath::Max(0.0f, 1.0f - Progress), 2.8f);
			LowPhase = FMath::Fmod(LowPhase + (2.0f * UE_PI * FMath::Lerp(88.0f, 42.0f, Progress) / SynthSampleRate), 2.0f * UE_PI);
			PlasticPhase = FMath::Fmod(PlasticPhase + (2.0f * UE_PI * FMath::Lerp(178.0f, 116.0f, Progress) / SynthSampleRate), 2.0f * UE_PI);
			NoiseState = NoiseState * 1664525u + 1013904223u;
			const float Noise = (static_cast<float>((NoiseState >> 8) & 0xFFFFu) / 32767.5f) - 1.0f;
			const float PlasticKnock = FMath::Sin(LowPhase) * 0.64f + FMath::Sin(PlasticPhase) * 0.23f;
			const float SurfaceScuff = Noise * FMath::Exp(-24.0f * TimeSeconds) * 0.13f;
			Sample = FMath::Clamp((PlasticKnock + SurfaceScuff) * Envelope, -0.92f, 0.92f);
			++ImpactFrameIndex;
			if (ImpactFrameIndex >= ImpactFrameCount)
			{
				bImpactActive = false;
                AsyncTask(ENamedThreads::GameThread,[Weak=TWeakObjectPtr<UMannequinCaptureSynthComponent>(this),Generation=AudioGeneration]
                {if(auto* Self=Weak.Get();Self&&Self->PlaybackGeneration.GetValue()==Generation){Self->Stop();
#if WITH_DEV_AUTOMATION_TESTS
                    ++Self->ReviewCompletedStopCount;
#endif
                }});
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

	const float TestIntensity = 0.7f;
	const float PantRate = ProjectProject01PainVoice::GetPantRate(TestIntensity);
	const float GeneratedPantCount =
		(ProjectProject01PainVoice::GetPantEndSeconds(TestIntensity) -
		 ProjectProject01PainVoice::PantStartSeconds - ProjectProject01PainVoice::ExhaleDurationSeconds) * PantRate + 1.0f;
	TestTrue(TEXT("Non-fatal pain voice contains exactly three pant cycles"),
		FMath::IsNearlyEqual(GeneratedPantCount, ProjectProject01PainVoice::PantCount, 0.001f));
	TestTrue(TEXT("Pain reaction ends with the third breath, without a final sigh"),
		FMath::IsNearlyEqual(UPainGroanSynthComponent::GetReactionDuration(false,TestIntensity), 2.87f, 0.001f));
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
	ReleaseBreathingSFX();
	ReleaseHeartbeatVFX();
	ReleaseHeartbeatSFX();
	Super::EndPlay(EndPlayReason);
}

void UPlayerHeartbeatComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	auto* Player=Cast<APlayerCharacter>(OwnerPawn);
	if(Player && Player->IsLocallyObserved())
	{
		const auto& P=Player->GetObserverPresentation();
		PresentedBPM=P.BPM;PresentedVFXProximity=P.Proximity;PresentedVFXDensity=P.Density;PresentedHeartbeatGain=P.HeartbeatGain;
		if(!Player->CanPlayTensionBreathing())PresentedBPM=PresentedVFXProximity=PresentedVFXDensity=0;
		ApplyObserverBreathing(P,*OwnerPawn);UpdateHeartbeatVFX(DeltaTime,*OwnerPawn);UpdateBeatOutput(DeltaTime,*OwnerPawn);return;
	}
	if (!IsValid(OwnerPawn) || !OwnerPawn->IsLocallyControlled())
	{
		ReleaseBreathingSFX();
		ClearReliefBreathState();
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
	UpdateBreathingOutput(DeltaTime, *OwnerPawn);
	UpdateHeartbeatVFX(DeltaTime, *OwnerPawn);
	UpdateBeatOutput(DeltaTime, *OwnerPawn);
}

void UPlayerHeartbeatComponent::ExportObserverPresentation(FProjectSurvivorPresentation& Out) const
{
	Out.BPM=FMath::Clamp(PresentedBPM,0.f,400.f);Out.Proximity=FMath::Clamp(PresentedVFXProximity,0.f,1.f);
	Out.Density=FMath::Clamp(PresentedVFXDensity,0.f,1.f);Out.HeartbeatGain=FMath::Clamp(PresentedHeartbeatGain,0.f,1.f);
	Out.BreathTension=SharedBreathTension;Out.BreathGain=SharedBreathGain;Out.ReliefSerial=SharedReliefSerial;
	Out.ReliefRemaining=GetWorld()?FMath::Clamp(float(ReliefBreathEndWorldSeconds-GetWorld()->GetTimeSeconds()),0.f,1.f):0;
}

void UPlayerHeartbeatComponent::ApplyObserverBreathing(const FProjectSurvivorPresentation& P,APawn& Pawn)
{
	const auto* Player=Cast<APlayerCharacter>(&Pawn);const auto* World=GetWorld();
	if(!Player || !World || !Player->CanPlayTensionBreathing() || !CanResumeTensionBreathing(World->GetTimeSeconds()))
	{ReleaseBreathingSFX();LastObservedReliefSerial=P.ReliefSerial;return;}
	const bool Relief=P.ReliefSerial!=LastObservedReliefSerial && P.ReliefRemaining>0;LastObservedReliefSerial=P.ReliefSerial;
	if(P.BreathTension<0 && !Relief){if(World->GetTimeSeconds()>ObservedReliefUntil)ReleaseBreathingSFX();return;}
	auto* Synth=BreathingSFXSynth.Get();
	if(!Synth){Synth=NewObject<UTensionBreathingSynthComponent>(&Pawn);Synth->SetupAttachment(Pawn.GetRootComponent());Synth->RegisterComponent();BreathingSFXSynth=Synth;}
	if(auto* Settings=UProjectProject01GameUserSettings::Get())Synth->SoundClass=Settings->GetSFXSoundClass();
	Synth->SetVolumeMultiplier(P.BreathGain);
	if(Relief){ObservedReliefUntil=World->GetTimeSeconds()+UTensionBreathingSynthComponent::ReliefDurationSeconds;Synth->PlayReliefBreath();}
	else Synth->SetTension(P.BreathTension);
}

void UPlayerHeartbeatComponent::ResetHeartbeatState()
{
	ResetTensionBreathing();
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
		const bool bInsideBaseCone = Distance <= SafeBaseRange && IsInsideVisionCone(
			*Mannequin, ViewLocation, ViewForward, BaseVisionHalfAngleDegrees);
		const bool bInsideEncounterCone = Distance <= SafeEncounterRange && IsInsideVisionCone(
			*Mannequin, ViewLocation, ViewForward, EncounterVisionHalfAngleDegrees);
		// 가시선 결과는 (1) 어느 한 시야각 안에 있을 때의 가시 판정, (2) 미무장 상태의 미확인 타이머에서만 쓰인다.
		// 이미 무장(armed)됐고 두 시야각 밖이면 결과와 무관하게 상태가 같으므로 트레이스를 생략한다.
		const bool bNeedsLineOfSight = bInsideBaseCone || bInsideEncounterCone || !State.bEncounterArmed;
		const bool bHasClearSight = bNeedsLineOfSight && HasClearLineOfSight(OwnerPawn, *Mannequin, ViewLocation);
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
				bNeedsLineOfSight ? (bHasClearSight ? TEXT("true") : TEXT("false")) : TEXT("skipped"));
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
	// Shared presentation can retain the episode peak while sprinting outside the raw threat range.
	if (PresentedVFXProximity <= UE_SMALL_NUMBER && PresentedVFXDensity <= UE_SMALL_NUMBER)
	{
		HeartbeatVFXProximityAlpha = 0.0f;
		HeartbeatVFXDensityAlpha = 0.0f;
		ReleaseHeartbeatVFX();
		return;
	}

	const float TargetProximity = PresentedVFXProximity;
	const float TargetDensity = PresentedVFXDensity;
	const float BlendSpeed = TargetProximity > HeartbeatVFXProximityAlpha
		? HeartbeatVFXBlendInSpeed
		: HeartbeatVFXBlendOutSpeed;

	const bool bCalming = BreathingCalmEndWorldSeconds > World->GetTimeSeconds();
	HeartbeatVFXProximityAlpha = bCalming ? TargetProximity : InterpAlpha(
		HeartbeatVFXProximityAlpha, TargetProximity, DeltaTime, BlendSpeed);
	HeartbeatVFXDensityAlpha = bCalming ? TargetDensity : InterpAlpha(
		HeartbeatVFXDensityAlpha, TargetDensity, DeltaTime, BlendSpeed);

	const UProjectProject01GameUserSettings* UserSettings = UProjectProject01GameUserSettings::Get();
	const float LocalFlashScale = IsValid(UserSettings) ? UserSettings->GetLocalFlashScale() : 1.0f;
	const float LocalDistortionScale = IsValid(UserSettings) ? UserSettings->GetLocalDistortionScale() : 1.0f;
	const float EffectOpacity = FMath::Clamp(
		HeartbeatVFXProximityAlpha * HeartbeatVFXMaxOpacity * LocalFlashScale, 0.0f, 1.0f);
	const float DistortionAlpha = FMath::Clamp(HeartbeatVFXDensityAlpha * LocalDistortionScale, 0.0f, 1.0f);
	if (EffectOpacity <= KINDA_SMALL_NUMBER && DistortionAlpha <= KINDA_SMALL_NUMBER && TargetProximity <= KINDA_SMALL_NUMBER)
	{
		HeartbeatVFXProximityAlpha = 0.0f;
		HeartbeatVFXDensityAlpha = 0.0f;
		if (bHeartbeatVFXBlendableAttached && HeartbeatVFXCamera.IsValid())
		{
			for (UMaterialInstanceDynamic* Instance : HeartbeatVFXInstances)
				if (IsValid(Instance)) HeartbeatVFXCamera->RemoveBlendable(Instance);
			bHeartbeatVFXBlendableAttached = false;
		}
		return;
	}

	if (!EnsureHeartbeatVFX(OwnerPawn))
	{
		return;
	}

	for (UMaterialInstanceDynamic* Instance : HeartbeatVFXInstances)
	{
		if (!IsValid(Instance)) continue;
		Instance->SetScalarParameterValue(EffectOpacityParameter, EffectOpacity);
		Instance->SetScalarParameterValue(DistortionAmountParameter, HeartbeatVFXMaxDistortionAmount * DistortionAlpha);
		Instance->SetScalarParameterValue(DistortionSpeedParameter,
			FMath::Lerp(HeartbeatVFXMinDistortionSpeed, HeartbeatVFXMaxDistortionSpeed, DistortionAlpha));
		Instance->SetScalarParameterValue(DistortionFrequencyParameter,
			FMath::Lerp(HeartbeatVFXMinDistortionFrequency, HeartbeatVFXMaxDistortionFrequency, DistortionAlpha));
	}
}

TArray<FSoftObjectPath> UPlayerHeartbeatComponent::GetHeartbeatVFXMaterialPaths() const
{
	TArray<FSoftObjectPath> Result;
	TArray<FString> ConfiguredPaths;
	if (GConfig) GConfig->GetArray(TEXT("ProjectProject01.HeartbeatVFX"), TEXT("Materials"), ConfiguredPaths, GGameIni);
	for (const FString& Path : ConfiguredPaths)
		if (!Path.IsEmpty()) Result.AddUnique(FSoftObjectPath(Path));
	if (!Result.IsEmpty()) return Result;

	FString ConfiguredPath;
	if (GConfig && GConfig->GetString(TEXT("ProjectProject01.HeartbeatVFX"), TEXT("Material"), ConfiguredPath, GGameIni) &&
		!ConfiguredPath.IsEmpty())
	{
		Result.Add(FSoftObjectPath(ConfiguredPath));
		return Result;
	}
	// Preserve old Blueprint defaults when no shared tool selection has been saved.
	Result.Add(HeartbeatVFXMaterial.ToSoftObjectPath());
	return Result;
}

void UPlayerHeartbeatComponent::RefreshHeartbeatVFXMaterial()
{
	ReleaseHeartbeatVFX();
	bHeartbeatVFXLoadFailureLogged = false;
}

bool UPlayerHeartbeatComponent::EnsureHeartbeatVFX(APawn& OwnerPawn)
{
	// Match AActor::CalcCamera: only an active camera contributes to the player view.
	UCameraComponent* Camera = nullptr;
	TInlineComponentArray<UCameraComponent*> Cameras(&OwnerPawn);
	for (UCameraComponent* Candidate : Cameras)
	{
		if (IsValid(Candidate) && Candidate->IsActive())
		{
			Camera = Candidate;
			break;
		}
	}
	if (!IsValid(Camera))
	{
		return false;
	}

	if (HeartbeatVFXCamera.Get() != Camera)
	{
		ReleaseHeartbeatVFX();
		HeartbeatVFXCamera = Camera;
	}

	if (HeartbeatVFXInstances.IsEmpty())
	{
		bool bHadLoadFailure = false;
		const float Phase = FMath::FRandRange(0.0f, 90.0f);
		for (const FSoftObjectPath& MaterialPath : GetHeartbeatVFXMaterialPaths())
		{
			UMaterialInterface* Material = Cast<UMaterialInterface>(MaterialPath.TryLoad());
			if (!IsValid(Material) || Material->GetMaterial()->MaterialDomain != MD_PostProcess)
			{
				if (!bHeartbeatVFXLoadFailureLogged)
					UE_LOG(LogTemp, Error, TEXT("[HeartbeatVFX] Missing or non-postprocess material: %s"), *MaterialPath.ToString());
				bHadLoadFailure = true;
				continue;
			}
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
			if (!IsValid(Instance)) continue;
			// Share one phase across selected layers instead of stacking conflicting random movements.
			Instance->SetScalarParameterValue(TEXT("VertigoPhaseOffset"), Phase);
			HeartbeatVFXInstances.Add(Instance);
			UE_LOG(LogTemp, Log,
				TEXT("[HeartbeatVFX] Material=%s -> Owner=%s Camera=%s Active=%d PostProcessWeight=%.3f"),
				*MaterialPath.ToString(), *OwnerPawn.GetName(), *Camera->GetName(),
				Camera->IsActive(), Camera->PostProcessBlendWeight);
		}
		bHeartbeatVFXLoadFailureLogged = bHadLoadFailure;
		if (HeartbeatVFXInstances.IsEmpty()) return false;
	}

	if (!bHeartbeatVFXBlendableAttached)
	{
		for (UMaterialInstanceDynamic* Instance : HeartbeatVFXInstances)
			Camera->AddOrUpdateBlendable(Instance, 1.0f);
		bHeartbeatVFXBlendableAttached = true;
	}
	return true;
}

void UPlayerHeartbeatComponent::ReleaseHeartbeatVFX()
{
	if (bHeartbeatVFXBlendableAttached && HeartbeatVFXCamera.IsValid())
	{
		for (UMaterialInstanceDynamic* Instance : HeartbeatVFXInstances)
			if (IsValid(Instance)) HeartbeatVFXCamera->RemoveBlendable(Instance);
	}
	bHeartbeatVFXBlendableAttached = false;
	HeartbeatVFXCamera.Reset();
	HeartbeatVFXInstances.Reset();
}

bool UPlayerHeartbeatComponent::EnsureHeartbeatSFX(APawn& OwnerPawn)
{
	UWorld* World = GetWorld();
	const auto* Player=Cast<APlayerCharacter>(&OwnerPawn);
	if (!IsValid(World) || World->GetNetMode() == NM_DedicatedServer || (!OwnerPawn.IsLocallyControlled() && !(Player && Player->IsLocallyObserved())))
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
	Synth->SetVolumeMultiplier(HeartbeatSFXVolumeMultiplier * PresentedHeartbeatGain);
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
	if (auto* Synth = HeartbeatSFXSynth.Get(); IsValid(Synth))
		Synth->SetVolumeMultiplier(HeartbeatSFXVolumeMultiplier * PresentedHeartbeatGain);
	if (PresentedBPM <= UE_SMALL_NUMBER)
	{
		BeatAccumulator = 0.0f;
		ReleaseHeartbeatSFX();
		return;
	}

	// BPM을 박동 간격(60 / BPM)으로 변환한다.
	BeatAccumulator += DeltaTime;
	const float BeatIntervalSeconds = 60.0f / FMath::Max(PresentedBPM, 1.0f);
	if (BeatAccumulator >= BeatIntervalSeconds)
	{
		BeatAccumulator = FMath::Fmod(BeatAccumulator, BeatIntervalSeconds);
		if (EnsureHeartbeatSFX(OwnerPawn))
		{
			const float MinimumReferenceBPM = FMath::Max(BaseMinBPM, 1.0f);
			const float MaximumReferenceBPM = FMath::Max3(BaseMaxBPM, EncounterMaxBPM, MinimumReferenceBPM + 1.0f);
			const float BPMAlpha = FMath::GetRangePct(MinimumReferenceBPM, MaximumReferenceBPM, PresentedBPM);
			const float Strength = FMath::Lerp(0.48f, 0.82f, FMath::Clamp(BPMAlpha, 0.0f, 1.0f));
			HeartbeatSFXSynth->TriggerHeartbeat(Strength);
		}
		if (bEnableHeartbeatLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Heartbeat] THUMP | BPM=%.1f | Interval=%.3fs"),
				PresentedBPM, BeatIntervalSeconds);
		}
	}
}

void UPlayerHeartbeatComponent::SuspendTensionBreathingForPain(const bool bFatal, const float DurationSeconds)
{
	bFatalBreathingSuppression |= bFatal;
	if (const UWorld* World = GetWorld())
	{
		BreathingResumeWorldSeconds = FMath::Max(BreathingResumeWorldSeconds,
			World->GetTimeSeconds() + static_cast<double>(FMath::Max(DurationSeconds, 0.0f)));
		if (ReliefBreathEndWorldSeconds > World->GetTimeSeconds())
		{
			bReliefBreathQueued = true;
			ReliefBreathEndWorldSeconds = -1.0;
		}
	}
	if (bFatal) ClearReliefBreathState();
	if (auto* Synth = BreathingSFXSynth.Get(); IsValid(Synth)) Synth->Stop();
}

void UPlayerHeartbeatComponent::ResetTensionBreathing()
{
	ReleaseBreathingSFX();
	BreathingResumeWorldSeconds = -1.0;
	bFatalBreathingSuppression = false;
	ClearReliefBreathState();
}

bool UPlayerHeartbeatComponent::CanResumeTensionBreathing(const double WorldSeconds) const
{
	return !bFatalBreathingSuppression && FMath::IsFinite(WorldSeconds) && WorldSeconds >= BreathingResumeWorldSeconds;
}

bool UPlayerHeartbeatComponent::ShouldPlayTensionBreathing() const
{
	// Share the actual heartbeat activation/memory/decay rules. No independent idle breath loop.
	return bHeartbeatActive && FMath::IsFinite(CurrentBPM) && CurrentBPM > UE_SMALL_NUMBER &&
		CurrentVFXNearbyMannequinCount > 0;
}

float UPlayerHeartbeatComponent::CalculateBreathingTension() const
{
	if (!ShouldPlayTensionBreathing()) return 0.0f;
	const float Minimum = FMath::Max(BaseMinBPM, 1.0f);
	const float Maximum = FMath::Max3(BaseMaxBPM, EncounterMaxBPM, Minimum + 1.0f);
	return FMath::Clamp(FMath::GetRangePct(Minimum, Maximum, CurrentBPM), 0.0f, 1.0f);
}

void UPlayerHeartbeatComponent::ReleaseBreathingSFX()
{
	if (auto* Synth = BreathingSFXSynth.Get(); IsValid(Synth))
	{
		Synth->Stop();
		Synth->DestroyComponent();
	}
	BreathingSFXSynth.Reset();
}

void UPlayerHeartbeatComponent::ClearReliefBreathState()
{
	bHadBreathingThreat = bReliefBreathQueued = bRanInsideBreathingThreat = bHadTensionInsideBreathingThreat = false;
	LastThreatBreathingTension = 0.0f;
	PeakThreatBPM = PresentedBPM = 0.0f;
	PeakThreatVFXProximity = PeakThreatVFXDensity = PresentedVFXProximity = PresentedVFXDensity = 0.0f;
	CalmStartVFXProximity = CalmStartVFXDensity = 0.0f;
	PresentedHeartbeatGain = 1.0f;
	bCarriedTensionOutOfThreat = false;
	BreathingCalmEndWorldSeconds = -1.0;
	ReliefBreathEndWorldSeconds = -1.0;
}

bool UPlayerHeartbeatComponent::UpdateReliefBreathState(const bool bSprinting, const double WorldSeconds)
{
	if (WorldSeconds >= ReliefBreathEndWorldSeconds) ReliefBreathEndWorldSeconds = -1.0;
	if (CurrentVFXNearbyMannequinCount > 0)
	{
		// One threat episode, regardless of how many times sprint is pressed inside it.
		bHadBreathingThreat = true;
		bReliefBreathQueued = false;
		ReliefBreathEndWorldSeconds = -1.0;
		BreathingCalmEndWorldSeconds = -1.0;
		bCarriedTensionOutOfThreat = false;
		bRanInsideBreathingThreat |= bSprinting;
		PeakThreatVFXProximity = FMath::Max(PeakThreatVFXProximity,
			ProjectProject01HeartbeatVFX::CalculateProximityAlpha(NearestVFXMannequinDistance, HeartbeatRange));
		PeakThreatVFXDensity = FMath::Max(PeakThreatVFXDensity,
			ProjectProject01HeartbeatVFX::CalculateDensityAlpha(CurrentVFXNearbyMannequinCount, HeartbeatVFXDensityCountForMax));
		if (ShouldPlayTensionBreathing())
		{
			bHadTensionInsideBreathingThreat = true;
			LastThreatBreathingTension = FMath::Max(LastThreatBreathingTension, CalculateBreathingTension());
			PeakThreatBPM = FMath::Max(PeakThreatBPM, CurrentBPM);
		}
		return false;
	}
	if (bHadBreathingThreat)
	{
		bHadBreathingThreat = false;
		bReliefBreathQueued = true;
		bCarriedTensionOutOfThreat = bSprinting && bRanInsideBreathingThreat;
	}
	if (bSprinting)
	{
		// Only a run carried out of the threat episode may re-hold its peak.
		// Starting a new run outside during a walking exit must not restart the calm-down.
		if (bCarriedTensionOutOfThreat) BreathingCalmEndWorldSeconds = -1.0;
		if (ReliefBreathEndWorldSeconds > WorldSeconds)
		{
			bReliefBreathQueued = true;
			ReliefBreathEndWorldSeconds = -1.0;
		}
		return false;
	}
	if (bReliefBreathQueued && (bHadTensionInsideBreathingThreat ||
		PeakThreatVFXProximity > UE_SMALL_NUMBER || PeakThreatVFXDensity > UE_SMALL_NUMBER))
	{
		if (BreathingCalmEndWorldSeconds < 0.0)
		{
			BreathingCalmEndWorldSeconds = WorldSeconds + BreathingCalmDownSeconds;
			CalmStartVFXProximity = HeartbeatVFXProximityAlpha;
			CalmStartVFXDensity = HeartbeatVFXDensityAlpha;
		}
		if (WorldSeconds < BreathingCalmEndWorldSeconds) return false;
	}
	return bReliefBreathQueued;
}

bool UPlayerHeartbeatComponent::ShouldCarryTensionWhileSprinting(const bool bSprinting) const
{
	return bHadTensionInsideBreathingThreat && ShouldHoldThreatWhileSprinting(bSprinting);
}

bool UPlayerHeartbeatComponent::ShouldHoldThreatWhileSprinting(const bool bSprinting) const
{
	return bSprinting && bRanInsideBreathingThreat &&
		(bHadBreathingThreat || (bReliefBreathQueued && bCarriedTensionOutOfThreat));
}

float UPlayerHeartbeatComponent::GetBreathingCalmAlpha(const double WorldSeconds) const
{
	if (BreathingCalmEndWorldSeconds < 0.0) return 0.0f;
	return FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(
		static_cast<float>((BreathingCalmEndWorldSeconds - WorldSeconds) / BreathingCalmDownSeconds), 0.0f, 1.0f));
}

void UPlayerHeartbeatComponent::UpdatePresentedHeartbeat(const bool bSprinting, const double WorldSeconds, const float DeltaTime)
{
	const bool bCalming = BreathingCalmEndWorldSeconds > WorldSeconds;
	const bool bHoldingPeak = ShouldCarryTensionWhileSprinting(bSprinting);
	const bool bHoldingVFX = ShouldHoldThreatWhileSprinting(bSprinting);
	PresentedHeartbeatGain = bCalming ? GetBreathingCalmAlpha(WorldSeconds) : 1.0f;
	const float ActualProximity = CurrentVFXNearbyMannequinCount > 0 ?
		ProjectProject01HeartbeatVFX::CalculateProximityAlpha(NearestVFXMannequinDistance, HeartbeatRange) : 0.0f;
	const float ActualDensity = ProjectProject01HeartbeatVFX::CalculateDensityAlpha(
		CurrentVFXNearbyMannequinCount, HeartbeatVFXDensityCountForMax);
	PresentedVFXProximity = bHoldingVFX ? PeakThreatVFXProximity : bCalming ?
		CalmStartVFXProximity * PresentedHeartbeatGain : ActualProximity;
	PresentedVFXDensity = bHoldingVFX ? PeakThreatVFXDensity : bCalming ?
		CalmStartVFXDensity * PresentedHeartbeatGain : ActualDensity;
	const float TargetBPM = bHoldingPeak ? PeakThreatBPM : bCalming ? PeakThreatBPM * PresentedHeartbeatGain :
		ShouldPlayTensionBreathing() ? CurrentBPM : 0.0f;
	// Raw recognition/encounter logic remains independent of this presentation hold.
	if (CurrentVFXNearbyMannequinCount > 0 && !bSprinting && TargetBPM < PresentedBPM)
		PresentedBPM = FMath::FInterpTo(PresentedBPM, TargetBPM, FMath::Max(DeltaTime, 0.0f), 2.3f);
	else
		PresentedBPM = TargetBPM;
}

void UPlayerHeartbeatComponent::MarkReliefBreathStarted(const double WorldSeconds)
{
	bReliefBreathQueued = false;
	ReliefBreathEndWorldSeconds = WorldSeconds + UTensionBreathingSynthComponent::ReliefDurationSeconds;
	bRanInsideBreathingThreat = bHadTensionInsideBreathingThreat = false;
	LastThreatBreathingTension = 0.0f;
	PeakThreatBPM = 0.0f;
	PeakThreatVFXProximity = PeakThreatVFXDensity = 0.0f;
	BreathingCalmEndWorldSeconds = -1.0;
	bCarriedTensionOutOfThreat = false;
}

void UPlayerHeartbeatComponent::UpdateBreathingOutput(const float DeltaTime, APawn& OwnerPawn)
{
	SharedBreathTension=-1;
	const UWorld* World = GetWorld();
	const auto* Player = Cast<APlayerCharacter>(&OwnerPawn);
	if (!World || World->GetNetMode() == NM_DedicatedServer || !Player || !Player->CanPlayTensionBreathing())
	{
		ReleaseBreathingSFX();
		ClearReliefBreathState();
		return;
	}
	const double Now = World->GetTimeSeconds();
	const bool bSprinting = Player->IsSprintingForBreathing();
	const bool bReliefReady = UpdateReliefBreathState(bSprinting, Now);
	UpdatePresentedHeartbeat(bSprinting, Now, DeltaTime);
	const bool bTensionActive = ShouldPlayTensionBreathing() ||
		(CurrentVFXNearbyMannequinCount > 0 && bHadTensionInsideBreathingThreat);
	const bool bCarryTension = ShouldCarryTensionWhileSprinting(bSprinting);
	const bool bCalming = BreathingCalmEndWorldSeconds > Now && bHadTensionInsideBreathingThreat;
	if (!bTensionActive && !bCarryTension && !bCalming && !bReliefReady)
	{
		if (ReliefBreathEndWorldSeconds <= Now) ReleaseBreathingSFX();
		return;
	}
	// The synth's queued Stop may lag a game-thread duration by an audio buffer: wait for both.
	const auto* Pain = OwnerPawn.FindComponentByClass<UPainGroanSynthComponent>();
	if (!CanResumeTensionBreathing(Now) || (IsValid(Pain) && Pain->IsPlaying()))
	{
		if (auto* Synth = BreathingSFXSynth.Get(); IsValid(Synth) && Synth->IsPlaying()) Synth->Stop();
		return;
	}
	auto* Synth = BreathingSFXSynth.Get();
	if (!IsValid(Synth))
	{
		Synth = NewObject<UTensionBreathingSynthComponent>(&OwnerPawn);
		Synth->SetupAttachment(OwnerPawn.GetRootComponent());
		Synth->RegisterComponent();
		BreathingSFXSynth = Synth;
	}
	if (auto* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
		Synth->SoundClass = Settings->GetSFXSoundClass();
	SharedBreathGain=bCalming ? GetBreathingCalmAlpha(Now) : 1.0f;
	Synth->SetVolumeMultiplier(SharedBreathGain);
	if (bTensionActive || bCarryTension || bCalming)
	{
		SharedBreathTension=bCarryTension ? LastThreatBreathingTension : bCalming ? LastThreatBreathingTension * GetBreathingCalmAlpha(Now) : CalculateBreathingTension();
		Synth->SetTension(SharedBreathTension);
	}
	else
	{
		++SharedReliefSerial;
		Synth->PlayReliefBreath();
		MarkReliefBreathStarted(Now);
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

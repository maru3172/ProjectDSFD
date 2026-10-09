// File: Source/ProjectProject01/Private/FootstepSynthComponent.cpp

#include "FootstepSynthComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProjectProject01GameInstance.h"
#include "Sound/SoundAttenuation.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace ProjectProject01Footstep
{
	constexpr float IdleStopDelaySeconds = 0.8f;
	constexpr float TeleportDistanceCentimeters = 1000.0f;

	float ResolveCharacterVolume(const bool bPartner)
	{
		return bPartner ? 0.5f : 1.0f;
	}

	bool ShouldTriggerSkid(
		const EProjectProject01FootstepStyle Style,
		const bool bArmed,
		const bool bGrounded,
		const float PreviousSpeed,
		const float CurrentSpeed,
		const float AccelerationSize,
		const float VelocityAccelerationAlignment,
		const float DeltaTime,
		const float RunningThreshold,
		const float MinimumSpeedLossPerSecond)
	{
		if (Style != EProjectProject01FootstepStyle::MarbleSneaker || !bArmed || !bGrounded ||
			PreviousSpeed < RunningThreshold)
		{
			return false;
		}

		const bool bBrakingInput = AccelerationSize <= 5.0f || VelocityAccelerationAlignment < -0.25f;
		const float RequiredSpeedLoss = FMath::Max(10.0f, MinimumSpeedLossPerSecond * FMath::Max(DeltaTime, 0.0f));
		return bBrakingInput && PreviousSpeed - CurrentSpeed >= RequiredSpeedLoss;
	}
}

UFootstepSynthComponent::UFootstepSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = false;
	bAutoDestroy = false;
	bStopWhenOwnerDestroyed = true;
	bAllowSpatialization = true;
	bIsUISound = false;
	NumChannels = 1;

	bOverrideAttenuation = true;
	AttenuationOverrides.bAttenuate = true;
	AttenuationOverrides.bSpatialize = true;
	AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
	AttenuationOverrides.AttenuationShapeExtents = FVector(150.0f, 0.0f, 0.0f);
	AttenuationOverrides.FalloffDistance = 2200.0f;
}

void UFootstepSynthComponent::Configure(
	const EProjectProject01FootstepStyle NewStyle,
	const float NewVolumeScale,
	const bool bNewAutomaticCadence)
{
	FootstepStyle = NewStyle;
	FootstepVolumeScale = FMath::Max(0.0f, NewVolumeScale);
	bAutomaticCadence = bNewAutomaticCadence;
}

void UFootstepSynthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (const AActor* Owner = GetOwner(); IsValid(Owner))
	{
		PreviousOwnerLocation = Owner->GetActorLocation();
		bLocationInitialized = true;
	}
	if (UProjectProject01GameUserSettings* UserSettings = UProjectProject01GameUserSettings::Get(); IsValid(UserSettings))
	{
		SoundClass = UserSettings->GetSFXSoundClass();
	}
}

void UFootstepSynthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsPlaying())
	{
		Stop();
	}
	Super::EndPlay(EndPlayReason);
}

void UFootstepSynthComponent::UpdateFootsteps(const float DeltaTime)
{
	if (!CanProduceLocalAudio())
	{
		return;
	}

	if (bAutomaticCadence)
	{
		UpdateAutomaticCadence(DeltaTime);
	}

	const UWorld* World = GetWorld();
	if (IsPlaying() && IsValid(World) && LastTriggerTimeSeconds >= 0.0 &&
		World->GetTimeSeconds() - LastTriggerTimeSeconds > ProjectProject01Footstep::IdleStopDelaySeconds)
	{
		Stop();
	}
}

bool UFootstepSynthComponent::CanProduceLocalAudio() const
{
	const UWorld* World = GetWorld();
	return IsValid(World) && World->GetNetMode() != NM_DedicatedServer;
}

void UFootstepSynthComponent::UpdateAutomaticCadence(const float DeltaTime)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = IsValid(Character) ? Character->GetCharacterMovement() : nullptr;
	if (!IsValid(Character) || !IsValid(Movement))
	{
		return;
	}

	const FVector CurrentLocation = Character->GetActorLocation();
	if (!bLocationInitialized)
	{
		PreviousOwnerLocation = CurrentLocation;
		bLocationInitialized = true;
		return;
	}

	const float TravelDistance = FVector::Dist2D(CurrentLocation, PreviousOwnerLocation);
	PreviousOwnerLocation = CurrentLocation;
	const float Speed = Character->GetVelocity().Size2D();
	if (!Movement->IsMovingOnGround() ||
		TravelDistance > ProjectProject01Footstep::TeleportDistanceCentimeters)
	{
		AccumulatedTravelDistance = 0.0f;
		PreviousHorizontalSpeed = Speed;
		bSkidArmed = false;
		return;
	}

	const FVector HorizontalVelocity = FVector(Character->GetVelocity().X, Character->GetVelocity().Y, 0.0f);
	const FVector HorizontalAcceleration = FVector(
		Movement->GetCurrentAcceleration().X,
		Movement->GetCurrentAcceleration().Y,
		0.0f);
	const float VelocityAccelerationAlignment =
		HorizontalVelocity.IsNearlyZero() || HorizontalAcceleration.IsNearlyZero()
			? 0.0f
			: FVector::DotProduct(HorizontalVelocity.GetSafeNormal(), HorizontalAcceleration.GetSafeNormal());
	if (ProjectProject01Footstep::ShouldTriggerSkid(
		FootstepStyle,
		bSkidArmed,
		true,
		PreviousHorizontalSpeed,
		Speed,
		HorizontalAcceleration.Size2D(),
		VelocityAccelerationAlignment,
		DeltaTime,
		RunningSpeedThreshold,
		MinimumSkidSpeedLossPerSecond))
	{
		TriggerSkidStop();
		bSkidArmed = false;
		AccumulatedTravelDistance = 0.0f;
		PreviousHorizontalSpeed = Speed;
		return;
	}
	else if (Speed >= RunningSpeedThreshold &&
		HorizontalAcceleration.Size2D() > 5.0f && VelocityAccelerationAlignment > 0.0f)
	{
		bSkidArmed = true;
	}

	if (Speed < MinimumMovingSpeed)
	{
		AccumulatedTravelDistance = 0.0f;
		PreviousHorizontalSpeed = Speed;
		bSkidArmed = false;
		return;
	}

	AccumulatedTravelDistance += TravelDistance;
	const bool bRunning = Speed >= RunningSpeedThreshold;
	const float RequiredDistance = FMath::Max(1.0f, bRunning ? RunStepDistance : WalkStepDistance);
	if (AccumulatedTravelDistance >= RequiredDistance)
	{
		AccumulatedTravelDistance = FMath::Fmod(AccumulatedTravelDistance, RequiredDistance);
		TriggerFootstep(bRunning);
	}
	PreviousHorizontalSpeed = Speed;
}

void UFootstepSynthComponent::TriggerFootstep(const bool bRunning)
{
	UWorld* World = GetWorld();
	if (!CanProduceLocalAudio() || !IsValid(World) || FootstepVolumeScale <= UE_SMALL_NUMBER)
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (LastTriggerTimeSeconds >= 0.0 && CurrentTime - LastTriggerTimeSeconds < MinimumTriggerInterval)
	{
		return;
	}
	LastTriggerTimeSeconds = CurrentTime;

	if (!IsPlaying())
	{
		Start();
	}

	const EProjectProject01FootstepStyle Style = FootstepStyle;
	const float Pitch = FMath::FRandRange(0.94f, 1.06f);
	const float Gain = FootstepVolumeScale * (bRunning ? 1.18f : 1.0f) * FMath::FRandRange(0.92f, 1.0f);
	const uint32 Seed = FMath::Rand();
	SynthCommand([this, Style, Pitch, Gain, Seed, bRunning]()
	{
		PulseFrameIndex = 0;
		PulsePhaseLow = 0.0f;
		PulsePhaseHigh = 0.0f;
		FilteredNoise = 0.0f;
		FilteredNoiseSlow = 0.0f;
		PulsePitch = Pitch;
		PulseGain = Gain;
		NoiseState = Seed == 0 ? 0x12345678u : Seed;
		bPulseRunning = bRunning;
		bSkidPulse = false;
		ActiveStyle = Style;
		bPulseActive = true;
	});
}

void UFootstepSynthComponent::TriggerSkidStop()
{
	UWorld* World = GetWorld();
	if (!CanProduceLocalAudio() || !IsValid(World) ||
		FootstepStyle != EProjectProject01FootstepStyle::MarbleSneaker || FootstepVolumeScale <= UE_SMALL_NUMBER)
	{
		return;
	}

	LastTriggerTimeSeconds = World->GetTimeSeconds();
	if (!IsPlaying())
	{
		Start();
	}

	const float Pitch = FMath::FRandRange(0.94f, 1.04f);
	const float Gain = FootstepVolumeScale * FMath::FRandRange(0.82f, 0.92f);
	const uint32 Seed = FMath::Rand();
	SynthCommand([this, Pitch, Gain, Seed]()
	{
		PulseFrameIndex = 0;
		PulsePhaseLow = 0.0f;
		PulsePhaseHigh = 0.0f;
		FilteredNoise = 0.0f;
		FilteredNoiseSlow = 0.0f;
		PulsePitch = Pitch;
		PulseGain = Gain;
		NoiseState = Seed == 0 ? 0x87654321u : Seed;
		bPulseRunning = false;
		bSkidPulse = true;
		ActiveStyle = EProjectProject01FootstepStyle::MarbleSneaker;
		bPulseActive = true;
	});
}

bool UFootstepSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	SynthSampleRate = FMath::Max(SampleRate, 8000);
	PulseFrameIndex = 0;
	bPulseActive = false;
	return true;
}

int32 UFootstepSynthComponent::OnGenerateAudio(float* OutAudio, const int32 NumSamples)
{
	if (OutAudio == nullptr || NumSamples <= 0)
	{
		return 0;
	}

	FMemory::Memzero(OutAudio, NumSamples * sizeof(float));
	const bool bPlastic = ActiveStyle == EProjectProject01FootstepStyle::MarblePlastic;
	const float Duration = bSkidPulse
		? 0.34f
		: (bPlastic ? (bPulseRunning ? 0.13f : 0.16f) : (bPulseRunning ? 0.17f : 0.20f));
	const int32 PulseFrames = FMath::Max(1, FMath::RoundToInt(Duration * SynthSampleRate));

	for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		float Sample = 0.0f;
		if (bPulseActive && PulseFrameIndex < PulseFrames)
		{
			const float Time = static_cast<float>(PulseFrameIndex) / static_cast<float>(SynthSampleRate);
			NoiseState = NoiseState * 1664525u + 1013904223u;
			const float WhiteNoise = (static_cast<float>((NoiseState >> 8) & 0x00FFFFFFu) / 8388607.5f) - 1.0f;
			if (bSkidPulse)
			{
				const float Progress = FMath::Clamp(Time / Duration, 0.0f, 1.0f);
				const float Attack = FMath::Clamp(Time / 0.012f, 0.0f, 1.0f);
				const float Envelope = Attack * FMath::Pow(1.0f - Progress, 1.35f);
				FilteredNoise += (WhiteNoise - FilteredNoise) * 0.20f;
				FilteredNoiseSlow += (WhiteNoise - FilteredNoiseSlow) * 0.035f;
				const float ScrapeNoise = FilteredNoise - FilteredNoiseSlow;
				const float RubberFrequency = FMath::Lerp(185.0f, 92.0f, Progress) * PulsePitch;
				PulsePhaseLow = FMath::Fmod(
					PulsePhaseLow + 2.0f * UE_PI * RubberFrequency / SynthSampleRate,
					2.0f * UE_PI);
				const float RubberDrag = FMath::Sin(PulsePhaseLow) * 0.12f * Envelope;
				Sample = (ScrapeNoise * 0.48f * Envelope) + RubberDrag;
			}
			else
			{
			// 날카로운 백색 잡음을 그대로 쓰지 않고 저역 통과시켜 '픽' 소리를 없앱니다.
			FilteredNoise += (WhiteNoise - FilteredNoise) * (bPlastic ? 0.11f : 0.07f);

			if (bPlastic)
			{
				const float Progress = FMath::Clamp(Time / Duration, 0.0f, 1.0f);
				const float ImpactEnvelope = FMath::Exp(-29.0f * Time);
				const float BodyEnvelope = FMath::Exp(-18.0f * Time);
				const float LowFrequency = FMath::Lerp(
					bPulseRunning ? 138.0f : 118.0f,
					bPulseRunning ? 88.0f : 76.0f,
					Progress) * PulsePitch;
				const float BodyFrequency = FMath::Lerp(
					bPulseRunning ? 285.0f : 245.0f,
					bPulseRunning ? 175.0f : 155.0f,
					Progress) * PulsePitch;
				PulsePhaseLow = FMath::Fmod(PulsePhaseLow + 2.0f * UE_PI * LowFrequency / SynthSampleRate, 2.0f * UE_PI);
				PulsePhaseHigh = FMath::Fmod(PulsePhaseHigh + 2.0f * UE_PI * BodyFrequency / SynthSampleRate, 2.0f * UE_PI);
				const float HeavyKnock = FMath::Sin(PulsePhaseLow) * 0.62f * BodyEnvelope;
				const float DampedPlasticBody = FMath::Sin(PulsePhaseHigh) * 0.13f * ImpactEnvelope;
				const float ContactThump = FilteredNoise * 0.18f * ImpactEnvelope;
				Sample = HeavyKnock + DampedPlasticBody + ContactThump;
			}
			else
			{
				const float Progress = FMath::Clamp(Time / Duration, 0.0f, 1.0f);
				const float SoleEnvelope = FMath::Exp(-(bPulseRunning ? 18.0f : 15.0f) * Time);
				const float ScuffEnvelope = FMath::Exp(-16.0f * Time);
				const float LowFrequency = FMath::Lerp(
					bPulseRunning ? 102.0f : 86.0f,
					bPulseRunning ? 62.0f : 54.0f,
					Progress) * PulsePitch;
				const float SoleFrequency = FMath::Lerp(
					bPulseRunning ? 178.0f : 148.0f,
					bPulseRunning ? 112.0f : 96.0f,
					Progress) * PulsePitch;
				PulsePhaseLow = FMath::Fmod(PulsePhaseLow + 2.0f * UE_PI * LowFrequency / SynthSampleRate, 2.0f * UE_PI);
				PulsePhaseHigh = FMath::Fmod(PulsePhaseHigh + 2.0f * UE_PI * SoleFrequency / SynthSampleRate, 2.0f * UE_PI);
				const float HeelThump = FMath::Sin(PulsePhaseLow) * 0.66f * SoleEnvelope;
				const float RubberContact = FMath::Sin(PulsePhaseHigh) * 0.09f * SoleEnvelope;
				const float MarbleScuff = FilteredNoise * 0.16f * ScuffEnvelope;
				Sample = HeelThump + RubberContact + MarbleScuff;
			}
			}

			Sample = FMath::Clamp(Sample * PulseGain, -0.95f, 0.95f);
			++PulseFrameIndex;
			if (PulseFrameIndex >= PulseFrames)
			{
				bPulseActive = false;
			}
		}
		OutAudio[SampleIndex] = Sample;
	}
	return NumSamples;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01FootstepMixTest,
	"ProjectProject01.Audio.FootstepMix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01FootstepMixTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Player footstep keeps full character volume"),
		ProjectProject01Footstep::ResolveCharacterVolume(false), 1.0f);
	TestEqual(TEXT("Partner footstep is exactly fifty percent lighter"),
		ProjectProject01Footstep::ResolveCharacterVolume(true), 0.5f);
	TestTrue(TEXT("A running sneaker character braking sharply produces a skid"),
		ProjectProject01Footstep::ShouldTriggerSkid(
			EProjectProject01FootstepStyle::MarbleSneaker, true, true,
			900.0f, 850.0f, 0.0f, 0.0f, 1.0f / 60.0f, 700.0f, 250.0f));
	TestTrue(TEXT("A running sneaker character stopping in one frame still produces a skid"),
		ProjectProject01Footstep::ShouldTriggerSkid(
			EProjectProject01FootstepStyle::MarbleSneaker, true, true,
			900.0f, 0.0f, 0.0f, 0.0f, 1.0f / 60.0f, 700.0f, 250.0f));
	TestFalse(TEXT("Releasing sprint while continuing forward does not produce a skid"),
		ProjectProject01Footstep::ShouldTriggerSkid(
			EProjectProject01FootstepStyle::MarbleSneaker, true, true,
			900.0f, 850.0f, 1000.0f, 1.0f, 1.0f / 60.0f, 700.0f, 250.0f));
	TestFalse(TEXT("A mannequin plastic footstep never produces a sneaker skid"),
		ProjectProject01Footstep::ShouldTriggerSkid(
			EProjectProject01FootstepStyle::MarblePlastic, true, true,
			900.0f, 850.0f, 0.0f, 0.0f, 1.0f / 60.0f, 700.0f, 250.0f));
	return true;
}
#endif

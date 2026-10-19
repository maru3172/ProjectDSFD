// File: Source/ProjectProject01/Private/FootstepSynthComponent.cpp

#include "FootstepSynthComponent.h"
#include "PlasticJointSynthComponent.h"
#include "ProjectProject01LocomotionFeedback.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProjectProject01GameInstance.h"
#include "Sound/SoundAttenuation.h"
#include "ProjectProject01SpatialAudio.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

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

	bool CanTriggerPlasticMotion(EProjectProject01FootstepStyle Style, bool bMoving, bool bGrounded, double Now, double NextAllowed)
	{
		return Style == EProjectProject01FootstepStyle::MarblePlastic && bMoving && bGrounded &&
			FMath::IsFinite(Now) && Now >= NextAllowed;
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

	ProjectProject01SpatialAudio::ConfigureWorldSynth(*this);
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
	ProjectProject01SpatialAudio::ConfigureWorldSynth(*this);
	Super::BeginPlay();
	if (const AActor* Owner = GetOwner(); IsValid(Owner))
	{
		PreviousOwnerLocation = Owner->GetActorLocation();
		bLocationInitialized = true;
	}
	if (GetWorld()) NextPlasticMotionTimeSeconds = GetWorld()->GetTimeSeconds() + FMath::FRandRange(1.5f, 3.5f);
	if (UProjectProject01GameUserSettings* UserSettings = UProjectProject01GameUserSettings::Get(); IsValid(UserSettings))
	{
		SoundClass = UserSettings->GetSFXSoundClass();
	}
}

void UFootstepSynthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(PlasticJointSynth))
	{
		PlasticJointSynth->Stop();
		PlasticJointSynth->DestroyComponent();
		PlasticJointSynth = nullptr;
	}
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
	if (FootstepStyle == EProjectProject01FootstepStyle::MarblePlastic)
	{
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		if (IsValid(Character))
		{
			const float Yaw = Character->GetActorRotation().Yaw;
			const float Turn = bPlasticYawInitialized ? FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousPlasticYaw, Yaw)) : 0.0f;
			PreviousPlasticYaw = Yaw;
			bPlasticYawInitialized = true;
			const float Speed = Character->GetVelocity().Size2D();
			if (Speed < MinimumMovingSpeed || Turn > 120.0f) AccumulatedPlasticTurnDegrees = 0.0f;
			else AccumulatedPlasticTurnDegrees += Turn;
			if (AccumulatedPlasticTurnDegrees >= 40.0f)
			{
				AccumulatedPlasticTurnDegrees = 0.0f;
				TryTriggerPlasticBodyMotion(Speed >= RunningSpeedThreshold, true);
			}
		}
	}

	if (bAutomaticCadence)
	{
		UpdateAutomaticCadence(DeltaTime);
	}

	const UWorld* World = GetWorld();
	if (IsValid(PlasticJointSynth) && PlasticJointSynth->IsPlaying() && IsValid(World) &&
		World->GetTimeSeconds() - LastPlasticMotionTimeSeconds > ProjectProject01Footstep::IdleStopDelaySeconds)
	{
		PlasticJointSynth->Stop();
	}
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
	if (!CanProduceLocalAudio() || !IsValid(World))
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (LastTriggerTimeSeconds >= 0.0 && CurrentTime - LastTriggerTimeSeconds < MinimumTriggerInterval)
	{
		return;
	}
	LastTriggerTimeSeconds = CurrentTime;
	const bool bRightFoot = bNextFootIsRight;
	bNextFootIsRight = !bNextFootIsRight;
	// Keep the same small variation within a pair so random pitch cannot erase the 10% difference.
	if (!bRightFoot) PairedFootPitch = FMath::FRandRange(0.97f, 1.03f);
	const float Pitch = PairedFootPitch * FProjectProject01LocomotionFeedback::FootPitchScale(bRightFoot);
	OnFootContact.Broadcast(bRunning, bRightFoot);
	if (FootstepVolumeScale <= UE_SMALL_NUMBER) return;
	TryTriggerPlasticBodyMotion(bRunning, false);
	
	// Explicit opt-in only: older Blueprint WAV assignments must not hide the material mix.
	if (PlaySoundAsset(bRunning, Pitch))
	{
		return;
	}

	// Default material-specific synthesis (also used when the opted-in asset list is empty).
	if (!IsPlaying())
	{
		Start();
	}

	const EProjectProject01FootstepStyle Style = FootstepStyle;
	const float Gain = FootstepVolumeScale * (bRunning ? 1.18f : 1.0f) * FMath::FRandRange(0.92f, 1.0f);
	const uint32 Seed = FMath::Rand();
	SynthCommand([this, Style, Pitch, Gain, Seed, bRunning]()
	{
		PulseFrameIndex = 0;
		PulsePhaseLow = 0.0f;
		PulsePhaseHigh = 0.0f;
		PlasticWeightPhase = 0.0f;
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

void UFootstepSynthComponent::TryTriggerPlasticBodyMotion(const bool bRunning, const bool bTurning)
{
	const UWorld* World = GetWorld();
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = IsValid(Character) ? Character->GetCharacterMovement() : nullptr;
	if (!CanProduceLocalAudio() || !World || !Movement || FootstepVolumeScale <= UE_SMALL_NUMBER ||
		!ProjectProject01Footstep::CanTriggerPlasticMotion(FootstepStyle,
			Character->GetVelocity().Size2D() >= MinimumMovingSpeed, Movement->IsMovingOnGround(),
			World->GetTimeSeconds(), NextPlasticMotionTimeSeconds)) return;
	const double Now = World->GetTimeSeconds();
	// Failed rolls also wait. Repeated animation notifies cannot turn this into a constant loop.
	NextPlasticMotionTimeSeconds = Now + (bRunning ? FMath::FRandRange(1.8f, 3.6f) : FMath::FRandRange(2.8f, 5.2f));
	if (FMath::FRand() >= (bTurning ? 0.72f : bRunning ? 0.55f : 0.32f)) return;
	UPlasticJointSynthComponent* Joint = GetOrCreatePlasticJointSynth();
	if (!IsValid(Joint)) return;
	LastPlasticMotionTimeSeconds = Now;
	const uint32 Seed = FMath::Rand();
	const float Delay = FMath::FRandRange(0.025f, 0.075f);
	const float Duration = bRunning ? FMath::FRandRange(0.16f, 0.24f) : FMath::FRandRange(0.21f, 0.32f);
	const float Gain = FootstepVolumeScale * FMath::FRandRange(0.16f, 0.22f);
	Joint->TriggerMotion(Seed, Delay, Duration, Gain, bRunning);
}

UPlasticJointSynthComponent* UFootstepSynthComponent::GetOrCreatePlasticJointSynth()
{
	if (!CanProduceLocalAudio() || FootstepStyle != EProjectProject01FootstepStyle::MarblePlastic) return nullptr;
	if (IsValid(PlasticJointSynth)) return PlasticJointSynth;
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !IsValid(Owner->GetRootComponent())) return nullptr;
	PlasticJointSynth = NewObject<UPlasticJointSynthComponent>(Owner);
	// Follow the body, not this component: foot notifies move this component down to the floor.
	PlasticJointSynth->SetupAttachment(Owner->GetRootComponent());
	PlasticJointSynth->SetRelativeLocation(FVector::ZeroVector);
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
		PlasticJointSynth->SoundClass = Settings->GetSFXSoundClass();
	PlasticJointSynth->RegisterComponent();
	return PlasticJointSynth;
}

bool UFootstepSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	SynthSampleRate = FMath::Max(SampleRate, 8000);
	PulseFrameIndex = 0;
	bPulseActive = false;
	PlasticWeightPhase = 0.0f;
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
		: (bPlastic ? (bPulseRunning ? 0.14f : 0.16f) : (bPulseRunning ? 0.15f : 0.18f));
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
				// Polished department-store stone: dry, rigid contact, not a sustained granular scrape.
				// Friction is a very quiet, brief sole slip; the impact carries the material identity.
				const float Progress = FMath::Clamp(Time / Duration, 0.0f, 1.0f);
				const float Attack = FMath::Clamp(Time / 0.0025f, 0.0f, 1.0f);
				const float Release = FMath::Clamp((Duration - Time) / 0.025f, 0.0f, 1.0f);
				FilteredNoise += (WhiteNoise - FilteredNoise) * (bPlastic ? 0.26f : 0.20f);
				FilteredNoiseSlow += (WhiteNoise - FilteredNoiseSlow) * 0.035f;
				const float FrictionNoise = FilteredNoise - FilteredNoiseSlow;
				const float ToeContact = FMath::Exp(-FMath::Square((Time - (bPulseRunning ? 0.014f : 0.020f)) / 0.005f));
				const float FrictionTime = FMath::Max(0.0f, Time - 0.006f);
				const float FrictionEnvelope = (1.0f - FMath::Exp(-180.0f * FrictionTime)) * FMath::Exp(-125.0f * FrictionTime);
				const float ImpactEnvelope = Attack * FMath::Exp(-210.0f * Time);
				const float BodyEnvelope = Attack * FMath::Exp(-(bPlastic ? 58.0f : 48.0f) * Time);
				const float LowFrequency = FMath::Lerp(bPlastic ? 142.0f : 105.0f, bPlastic ? 112.0f : 84.0f, Progress) * PulsePitch;
				const float BodyFrequency = FMath::Lerp(bPlastic ? 340.0f : 210.0f, bPlastic ? 265.0f : 164.0f, Progress) * PulsePitch;
				PulsePhaseLow = FMath::Fmod(PulsePhaseLow + 2.0f * UE_PI * LowFrequency / SynthSampleRate, 2.0f * UE_PI);
				PulsePhaseHigh = FMath::Fmod(PulsePhaseHigh + 2.0f * UE_PI * BodyFrequency / SynthSampleRate, 2.0f * UE_PI);
				const float Body = FMath::Sin(PulsePhaseLow) * (bPlastic ? 0.48f : 0.54f) * BodyEnvelope;
				const float DampedContact = FMath::Sin(PulsePhaseHigh) * (bPlastic ? 0.18f : 0.075f) * Attack * FMath::Exp(-90.0f * Time);
				const float FloorImpact = FilteredNoise * (bPlastic ? 0.55f : 0.40f) * (ImpactEnvelope + ToeContact * 0.12f);
				const float FloorFriction = FrictionNoise * (bPlastic ? 0.065f : 0.085f) * FrictionEnvelope;
				Sample = (Body + DampedContact + FloorImpact + FloorFriction) * Attack * Release;
				if (bPlastic)
				{
					// Add weight below the existing plastic contact without brightening it into metal.
					const float WeightFrequency = FMath::Lerp(78.0f, 54.0f, Progress) * PulsePitch;
					PlasticWeightPhase = FMath::Fmod(PlasticWeightPhase + 2.0f * UE_PI * WeightFrequency / SynthSampleRate, 2.0f * UE_PI);
					Sample += FMath::Sin(PlasticWeightPhase) * 0.23f * Attack * FMath::Exp(-38.0f * Time) * Release;
				}
			}
			Sample *= PulseGain;
			++PulseFrameIndex;
			if (PulseFrameIndex >= PulseFrames)
			{
				bPulseActive = false;
			}
		}
		OutAudio[SampleIndex] = FMath::Clamp(Sample, -0.95f, 0.95f);
	}
	return NumSamples;
}

// 사운드 재생 함수
bool UFootstepSynthComponent::PlaySoundAsset(const bool bRunning, const float Pitch)
{
	if (!bUseAssignedSoundAssets) return false;
	const TArray<TObjectPtr<USoundBase>>& SelectedSounds =
		bRunning && !RunSounds.IsEmpty()
			? RunSounds
			: WalkSounds;

	USoundBase* SelectedSound =
		SelectRandomSound(SelectedSounds);

	if (!IsValid(SelectedSound))
	{
		return false;
	}
	if(!IsValid(OccludedSoundAttenuation)||OcclusionSource!=SoundAttenuation)
	{
		OcclusionSource=SoundAttenuation;
		OccludedSoundAttenuation=ProjectProject01SpatialAudio::MakeOccludedCopy(this,SoundAttenuation,AttenuationOverrides);
	}

	UAudioComponent* SpawnedAudioComponent =
		UGameplayStatics::SpawnSoundAtLocation(
			this,
			SelectedSound,
			GetComponentLocation(),
			GetComponentRotation(),
			FootstepVolumeScale *
				(bRunning ? 1.18f : 1.0f) *
				FMath::FRandRange(0.92f, 1.0f),
			Pitch,
			0.0f,
			OccludedSoundAttenuation);

	if (!IsValid(SpawnedAudioComponent))
	{
		return false;
	}

	if (UProjectProject01GameUserSettings* UserSettings =
		UProjectProject01GameUserSettings::Get();
		IsValid(UserSettings))
	{
		SpawnedAudioComponent->SoundClassOverride =
			UserSettings->GetSFXSoundClass();
	}

	return true;
}

// 무작위 선택 함수
USoundBase* UFootstepSynthComponent::SelectRandomSound(const TArray<TObjectPtr<USoundBase>>& Sounds) const
{
	TArray<USoundBase*> ValidSounds;
	ValidSounds.Reserve(Sounds.Num());

	for (USoundBase* Sound : Sounds)
	{
		if (IsValid(Sound))
		{
			ValidSounds.Add(Sound);
		}
	}

	if (ValidSounds.IsEmpty())
	{
		return nullptr;
	}

	return ValidSounds[FMath::RandRange(0, ValidSounds.Num() - 1)];
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
	TestTrue(TEXT("Moving grounded plastic can emit a body creak after cooldown"),
		ProjectProject01Footstep::CanTriggerPlasticMotion(EProjectProject01FootstepStyle::MarblePlastic,true,true,5,4));
	TestFalse(TEXT("A stopped mannequin cannot start a new joint sound"),
		ProjectProject01Footstep::CanTriggerPlasticMotion(EProjectProject01FootstepStyle::MarblePlastic,false,true,5,4));
	TestFalse(TEXT("Airborne mannequins cannot start body creaks"),
		ProjectProject01Footstep::CanTriggerPlasticMotion(EProjectProject01FootstepStyle::MarblePlastic,true,false,5,4));
	TestFalse(TEXT("Animation notifies cannot bypass the body sound cooldown"),
		ProjectProject01Footstep::CanTriggerPlasticMotion(EProjectProject01FootstepStyle::MarblePlastic,true,true,3,4));
	TestFalse(TEXT("Sneakers never use mannequin body sounds"),
		ProjectProject01Footstep::CanTriggerPlasticMotion(EProjectProject01FootstepStyle::MarbleSneaker,true,true,5,4));
	return true;
}
#endif

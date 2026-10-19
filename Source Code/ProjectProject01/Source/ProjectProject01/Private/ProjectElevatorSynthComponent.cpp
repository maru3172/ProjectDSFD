#include "ProjectElevatorSynthComponent.h"
#include "ProjectProject01SpatialAudio.h"

UProjectElevatorSynthComponent::UProjectElevatorSynthComponent(const FObjectInitializer& Initializer) : Super(Initializer)
{
	bAutoActivate = false; bAutoDestroy = false; bStopWhenOwnerDestroyed = true; NumChannels = 1;
	PrimaryComponentTick.bCanEverTick = false;
	ProjectProject01SpatialAudio::ConfigureWorldSynth(*this);
	AttenuationOverrides.AttenuationShapeExtents = FVector(500,0,0);
	AttenuationOverrides.FalloffDistance = 2500;
}
bool UProjectElevatorSynthComponent::Init(int32& SampleRate)
{
	Rate = FMath::Max(8000,SampleRate); NumChannels = 1;
	MotorGain = MotorTarget = 0; MotorPhase = 0; DingFrame = 0; bDingActive = false;
	return true;
}
void UProjectElevatorSynthComponent::SetMotor(bool bEnabled, float Gain)
{
	if (bEnabled && !IsPlaying()) Start();
	SynthCommand([this,bEnabled,Gain]() { MotorTarget = bEnabled ? 0.12f*FMath::Clamp(Gain,0.0f,1.0f) : 0.0f; });
}
void UProjectElevatorSynthComponent::Ding()
{
	if (!IsPlaying()) Start();
	SynthCommand([this]() { DingFrame = 0; bDingActive = true; });
}
int32 UProjectElevatorSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	if (!OutAudio || NumSamples <= 0) return 0;
	for (int32 I=0; I<NumSamples; ++I)
	{
		MotorGain += (MotorTarget-MotorGain) * (8.0f/Rate);
		MotorPhase = FMath::Fmod(MotorPhase + 2.0*UE_PI*58.0/Rate, 2.0*UE_PI);
		float Value = MotorGain * (FMath::Sin(MotorPhase) + 0.55f*FMath::Sin(MotorPhase*2) + 0.20f*FMath::Sin(MotorPhase*3));
		if (bDingActive)
		{
			const float T = static_cast<float>(DingFrame++)/Rate;
			const float Attack = FMath::Min(1.0f,T/0.008f);
			const float Tail = FMath::Clamp((1.5f-T)/0.12f,0.0f,1.0f);
			Value += 0.28f*Attack*Tail*FMath::Exp(-3.0f*T) *
				(FMath::Sin(2*UE_PI*880*T)+0.35f*FMath::Sin(2*UE_PI*1764*T));
			if (T>=1.5f) bDingActive = false;
		}
		OutAudio[I] = FMath::Clamp(Value,-0.9f,0.9f);
	}
	return NumSamples;
}

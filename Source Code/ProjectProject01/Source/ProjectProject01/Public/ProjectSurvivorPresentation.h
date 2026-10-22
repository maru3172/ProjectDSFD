#pragma once
#include "CoreMinimal.h"
#include "ProjectSurvivorPresentation.generated.h"

/** Cosmetic only. Never used for damage, sight, movement, score or admission. Values are unscaled by accessibility. */
USTRUCT()
struct FProjectSurvivorPresentation
{
	GENERATED_BODY()
	UPROPERTY() float StepOffset=0;
	UPROPERTY() float StepPitch=0;
	UPROPERTY() float BPM=0;
	UPROPERTY() float Proximity=0;
	UPROPERTY() float Density=0;
	UPROPERTY() float HeartbeatGain=0;
	UPROPERTY() float BreathTension=-1;
	UPROPERTY() float BreathGain=1;
	UPROPERTY() uint32 ReliefSerial=0;
	UPROPERTY() float ReliefRemaining=0;
	bool IsValid() const
	{
		return FMath::IsFinite(StepOffset) && FMath::IsFinite(StepPitch) && FMath::IsFinite(BPM) &&
			FMath::IsFinite(Proximity) && FMath::IsFinite(Density) && FMath::IsFinite(HeartbeatGain) && FMath::IsFinite(BreathTension) && FMath::IsFinite(BreathGain) && FMath::IsFinite(ReliefRemaining) &&
			FMath::Abs(StepOffset)<=15 && FMath::Abs(StepPitch)<=10 && BPM>=0 && BPM<=400 &&
			Proximity>=0 && Proximity<=1 && Density>=0 && Density<=1 && HeartbeatGain>=0 && HeartbeatGain<=1 &&
			BreathTension>=-1 && BreathTension<=1 && BreathGain>=0 && BreathGain<=1 && ReliefRemaining>=0 && ReliefRemaining<=1;
	}
};

/** Authoritative hit/death event retained for a spectator who switches targets during the effect. */
USTRUCT()
struct FProjectSurvivorFeedback
{
	GENERATED_BODY()
	UPROPERTY() uint32 Serial=0;
	UPROPERTY() double Started=0;
	UPROPERTY() float Pitch=0;
	UPROPERTY() float Roll=0;
	UPROPERTY() float Severity=0;
	UPROPERTY() bool Fatal=false;
};

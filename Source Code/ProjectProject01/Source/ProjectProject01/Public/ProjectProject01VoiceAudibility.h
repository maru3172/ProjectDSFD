#pragma once
#include "CoreMinimal.h"
#include "ProjectProject01VoiceAudibility.generated.h"

/** Owner-only server decision. No enemy identity, position, or voice state. */
USTRUCT()
struct FProjectVoiceAudibility
{
	GENERATED_BODY()
	UPROPERTY() FString UserId;
	UPROPERTY() float Gain=0;
};

namespace ProjectVoiceAudibility
{
	/** Interruptible short ramp; hard eligibility/mute gates are handled outside it. */
	struct FGainRamp
	{
		float Current=0,Start=0,Target=0,Elapsed=0;
		void Reset() { Current=Start=Target=Elapsed=0; }
		float Advance(float NewTarget,float DeltaSeconds,float Duration)
		{
			NewTarget=FMath::IsFinite(NewTarget)?FMath::Clamp(NewTarget,0.f,1.f):0.f;
			if (!FMath::IsNearlyEqual(Target,NewTarget,0.000001f)) { Start=Current;Target=NewTarget;Elapsed=0; }
			if (!FMath::IsFinite(Duration) || Duration<=0) return Current=Target;
			Elapsed=FMath::Min(Duration,Elapsed+(FMath::IsFinite(DeltaSeconds)?FMath::Max(0.f,DeltaSeconds):0.f));
			const float Alpha=FMath::Clamp(Elapsed/Duration,0.f,1.f);
			return Current=FMath::Lerp(Start,Target,Alpha*Alpha*(3.f-2.f*Alpha));
		}
	};
	inline float DistanceGain(float Distance,float Full,float Maximum,float Rolloff)
	{
		if(!FMath::IsFinite(Distance)||Distance<0 || !FMath::IsFinite(Full) || !FMath::IsFinite(Maximum) || !FMath::IsFinite(Rolloff))return 0;
		Full=FMath::Clamp(Full,0.0f,1000000.0f);Maximum=FMath::Clamp(Maximum,Full+1.0f,10000000.0f);
		const float Alpha=FMath::Clamp((Distance-Full)/(Maximum-Full),0.0f,1.0f);
		return FMath::Pow(1.0f-Alpha,FMath::Max(0.01f,Rolloff));
	}
}

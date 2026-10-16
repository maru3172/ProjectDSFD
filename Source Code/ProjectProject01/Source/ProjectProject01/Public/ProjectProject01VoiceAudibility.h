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
	inline float DistanceGain(float Distance,float Full,float Maximum,float Rolloff)
	{
		if(!FMath::IsFinite(Distance)||Distance<0 || !FMath::IsFinite(Full) || !FMath::IsFinite(Maximum) || !FMath::IsFinite(Rolloff))return 0;
		Full=FMath::Clamp(Full,0.0f,1000000.0f);Maximum=FMath::Clamp(Maximum,Full+1.0f,10000000.0f);
		const float Alpha=FMath::Clamp((Distance-Full)/(Maximum-Full),0.0f,1.0f);
		return FMath::Pow(1.0f-Alpha,FMath::Max(0.01f,Rolloff));
	}
}

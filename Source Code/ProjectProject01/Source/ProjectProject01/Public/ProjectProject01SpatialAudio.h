#pragma once
#include "CoreMinimal.h"
#include "Sound/SoundAttenuation.h"

class USynthComponent;

/** World SFX only. Heartbeat/UI/music and EOS survivor channel rules are not changed. */
namespace ProjectProject01SpatialAudio
{
    /** Full distance gain through 30m, linear fade over the next 30m; wall filtering is independent. */
    PROJECTPROJECT01_API void ApplyWorldSFXSettings(FSoundAttenuationSettings& Settings);
    /** Reassert the override after Blueprint defaults load, so an assigned asset cannot bypass the policy. */
    PROJECTPROJECT01_API void ConfigureWorldSynth(USynthComponent& Synth);
    PROJECTPROJECT01_API USoundAttenuation* MakeOccludedCopy(UObject* Outer,const USoundAttenuation* Source,
        const FSoundAttenuationSettings& Fallback);
}

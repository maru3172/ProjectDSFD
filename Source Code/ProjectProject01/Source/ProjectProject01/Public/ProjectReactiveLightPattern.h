#pragma once

#include "CoreMinimal.h"

// Bounded, seeded fault envelopes: sample the same event over time, never randomise per render frame.
namespace ProjectReactiveLight
{
    inline int32 Tier(int32 Count) { return FMath::Clamp(Count / 3, 0, 3); }
    inline float Threat(int32 Count, float NearestDistance, float Radius)
    {
        const int32 T = Tier(Count);
        if (!T || Radius <= UE_SMALL_NUMBER) return 0.f;
        const float Proximity = 1.f - FMath::Clamp(NearestDistance / Radius, 0.f, 1.f);
        return (T / 3.f) * FMath::Lerp(.55f, 1.f, Proximity);
    }

    struct FKey { float Time, Value; };
    struct FFaultPattern
    {
        FRandomStream Random;
        TArray<FKey, TInlineAllocator<16>> Keys;
        float Rest = 0, Elapsed = 0, Strength = 0, Depth = 0, Brightness = 1, Electrical = 0;
        int32 LastKind = INDEX_NONE, Serial = 0;
        bool bSound = false;

        void Reset(int32 Seed)
        {
            Random.Initialize(Seed); Keys.Reset(); Rest = Random.FRandRange(2.5f, 7.f);
            Elapsed = Strength = Depth = Electrical = 0; Brightness = 1;
            LastKind = INDEX_NONE; Serial = 0; bSound = false;
        }
        void BeginEvent(int32 T)
        {
            int32 Kind = Random.RandRange(0, 4);
            if (Kind == LastKind) Kind = (Kind + Random.RandRange(1, 4)) % 5;
            LastKind = Kind; ++Serial; Keys.Reset(); Elapsed = 0;
            bSound = Random.FRand() < .67f;
            Depth = Random.FRandRange(.85f, 1.f);
            float Time = 0;
            auto Key = [&](float Duration, float Value) { Time += Duration; Keys.Add({Time, Value}); };
            Key(0, 1);
            const float Stretch = Random.FRandRange(.85f, 1.25f);
            // A symptom may recover without a full outage. Longer failures replace faster strobing at higher tiers.
            if (Kind == 0) // A recognisably slow voltage collapse: no short blink inside this family.
            {
                Key(1.f*Stretch, .78f); Key(2.1f*Stretch, .18f);
                Key((.7f+.4f*T)*Stretch, .18f); Key(1.6f*Stretch, .75f); Key(1.2f*Stretch, 1);
            }
            else if (Kind == 1 || Kind == 2) // One blink or an irregular double blink, with time to read the scene.
            {
                Key(.10f, .08f); Key(.20f*Stretch, .08f); Key(.25f, 1);
                if (Kind == 2)
                {
                    // Fully restore between blinks, with a clearly separated, deeper second interruption.
                    Key(.75f*Stretch, 1); Key(.11f, .01f); Key(.36f*Stretch, .01f); Key(.32f, .92f);
                    Key(.7f*Stretch, 1);
                }
                else Key(.45f*Stretch, 1);
            }
            else if (Kind == 3) // Failed restart; the fixture never exceeds its normal brightness.
            {
                Key(.28f, .04f); Key((.35f+.15f*T)*Stretch, .04f);
                Key(.38f, .88f); Key(.2f, .88f); Key(.14f, .06f); Key(.5f*Stretch, .06f);
                Key(.48f, .82f); Key(.18f, .82f); Key(.16f, .03f);
                Key((.7f+.2f*T)*Stretch, .03f); Key(1.f*Stretch, .48f);
                Key(1.2f*Stretch, .91f); Key(.6f*Stretch, 1);
            }
            else // A long, incomplete restoration: different fixtures leave different dark pockets.
            {
                Key(.9f*Stretch, .35f); Key((2.8f+.6f*T)*Stretch, .35f);
                Key(.8f*Stretch, .48f); Key(1.6f*Stretch, .42f);
                Key((1.f+.35f*T)*Stretch, .42f); Key(2.2f*Stretch, 1);
            }
        }
        void Step(float Delta, int32 Count, float Target, float MinimumBrightness)
        {
            const float Dt = FMath::Clamp(Delta, 0.f, .25f);
            const float Rate = Target > Strength ? .8f : .28f;
            Strength = FMath::FInterpTo(Strength, FMath::Clamp(Target, 0.f, 1.f), Dt, Rate);
            if (Target == 0 && Strength < .001f) Strength = 0;
            const int32 T = Tier(Count);
            if (Keys.IsEmpty())
            {
                Rest -= Dt;
                if (Rest <= 0 && T && Target > 0) BeginEvent(T);
            }
            float Envelope = 1;
            if (!Keys.IsEmpty())
            {
                Elapsed += Dt;
                for (int32 I = 1; I < Keys.Num(); ++I)
                {
                    if (Elapsed > Keys[I].Time) continue;
                    const float A = FMath::Clamp((Elapsed-Keys[I-1].Time)/(Keys[I].Time-Keys[I-1].Time), 0.f, 1.f);
                    Envelope = FMath::Lerp(Keys[I-1].Value, Keys[I].Value, A*A*(3-2*A));
                    break;
                }
                if (Elapsed >= Keys.Last().Time)
                {
                    Keys.Reset();
                    Rest = Random.FRandRange(14.f-2.5f*T, 25.f-4.f*T);
                }
            }
            // A small, non-periodic-frequency after-effect lingers while the threat decays.
            const float Shiver = .025f * Strength * (.5f+.5f*FMath::Sin(Elapsed*8.1f + Random.GetInitialSeed()*.001f))
                * (1.f-Envelope);
            // Distance/tall-ceiling attenuation previously buried all five signatures in tiny brightness changes.
            // A perceptual curve retains smooth falloff but keeps the event's timing readable at modest threat.
            const float Contrast=FMath::Min(1.f,1.35f*FMath::Sqrt(Strength));
            Brightness = FMath::Clamp(1.f - ((1.f-Envelope)*Depth + Shiver)*Contrast, MinimumBrightness, 1.f);
            Electrical = bSound ? FMath::Clamp((1.f-Envelope)*Strength, 0.f, 1.f) : 0.f;
        }
    };
}

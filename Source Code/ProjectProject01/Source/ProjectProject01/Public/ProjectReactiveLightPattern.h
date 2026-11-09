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
    // One irregular local power disturbance per world, not a queue that serialises individual fixtures.
    struct FSharedFaultSurges
    {
        FRandomStream Random;
        double NextAt=-1, StartedAt=-1;
        int32 Serial=0;
        void Reset(int32 Seed) { Random.Initialize(Seed);NextAt=StartedAt=-1;Serial=0; }
        void Step(double Now,int32 Count)
        {
            const int32 T=Tier(Count);
            if(!T){NextAt=-1;return;}
            if(NextAt<0){NextAt=Now+Random.FRandRange(.15f,.55f);return;}
            if(Now<NextAt)return;
            StartedAt=Now;++Serial;
            NextAt=Now+Random.FRandRange(1.4f-.3f*T,2.95f-.55f*T);
            if(Random.FRand()<.15f)NextAt=Now+(NextAt-Now)*Random.FRandRange(1.6f,2.3f);
        }
    };
    struct FFaultPattern
    {
        FRandomStream Random;
        FRandomStream SectionRandom;
        FVector SectionLevels=FVector::OneVector, SectionTarget=FVector::OneVector;
        float SectionRest=0;
        float RecoveryQuietAt=-1, RecoveryStartsAt=-1, RecoveryProgress=1;
        float RecoverySide=1;
        TArray<FKey, TInlineAllocator<16>> Keys;
        float Rest = 0, Elapsed = 0, Strength = 0, Depth = 0, Brightness = 1, Electrical = 0;
        int32 LastKind = INDEX_NONE, Serial = 0, LastSurgeSerial=0;
        bool bSound = false;

        void Reset(int32 Seed)
        {
            Random.Initialize(Seed); Keys.Reset(); Rest = Random.FRandRange(.1f, .7f);
            SectionRandom.Initialize(Seed^0x53A91);SectionLevels=SectionTarget=FVector::OneVector;SectionRest=0;
            RecoveryQuietAt=RecoveryStartsAt=-1;
            RecoveryProgress=1;
            Elapsed = Strength = Depth = Electrical = 0; Brightness = 1;
            LastKind = INDEX_NONE; Serial = LastSurgeSerial = 0; bSound = false;
        }
        bool QueueSharedSurge(int32 SurgeSerial,float Age,int32 Count,float Target)
        {
            if(SurgeSerial<=LastSurgeSerial)return false;
            LastSurgeSerial=SurgeSerial;
            const int32 T=Tier(Count);
            // Never restart an active envelope or replay an old disturbance after checkpoint/range changes.
            if(!T||Target<=0||Age<0||Age>.35f||!Keys.IsEmpty())return false;
            if(Random.FRand()>(.6f+.1f*T))return false;
            Rest=FMath::Min(Rest,Random.FRandRange(.025f,.25f));
            return true;
        }
        void BeginEvent(int32 T)
        {
            int32 Kind = Random.RandRange(0, 4);
            if (Kind == LastKind) Kind = (Kind + Random.RandRange(1, 4)) % 5;
            LastKind = Kind; ++Serial; Keys.Reset(); Elapsed = 0;
            SectionRest=0;
            RecoveryQuietAt=RecoveryStartsAt=-1;
            RecoveryProgress=1;
            bSound = Random.FRand() < .67f;
            Depth = Random.FRandRange(.9f, 1.f);
            float Time = 0;
            // Jitter each edge once when scheduling, so even the same family never has a fixed rhythm.
            auto Key = [&](float Duration, float Value) { Time += Duration*Random.FRandRange(.82f,1.18f); Keys.Add({Time, Value}); };
            Key(0, 1);
            const float Stretch = Random.FRandRange(.55f, .9f);
            // Only voltage collapse and incomplete restoration get an unstable afterglow.
            // This belongs to this event's keys; it is not a global minimum for other faults.
            const float Residual=(Kind==0||Kind==4) ? Random.FRandRange(.015f,.045f) : 0.f;
            // Compact failure clusters alternate with darkness and silence, never a continuous strobe loop.
            if (Kind == 0) // Abrupt voltage collapse followed by a struggling, stepped recovery.
            {
                Key(.07f, .52f); Key(.11f, 0); Key((.25f+.12f*T)*Stretch, 0);
                Key(.11f, .76f); Key(.09f, Residual); Key((.4f+.16f*T)*Stretch, Residual);
                Key(.26f*Stretch, .9f); Key(.16f*Stretch, 1);
            }
            else if (Kind == 1 || Kind == 2) // One blink or an irregular double blink, with time to read the scene.
            {
                Key(.065f, 0); Key(.16f*Stretch, 0); Key(.12f, 1);
                if (Kind == 2)
                {
                    // Fully restore between blinks, with a clearly separated, deeper second interruption.
                    Key(.24f*Stretch, 1); Key(.075f, 0); Key(.26f*Stretch, 0); Key(.12f, .88f);
                    Key(.2f*Stretch, 1);
                }
                else Key(.14f*Stretch, 1);
            }
            else if (Kind == 3) // Failed restart; the fixture never exceeds its normal brightness.
            {
                Key(.075f, 0); Key((.18f+.06f*T)*Stretch, 0);
                Key(.1f, .96f); Key(.16f*Stretch, .96f); Key(.07f, 0); Key(.25f*Stretch, 0);
                Key(.12f, .9f); Key(.19f*Stretch, .9f); Key(.075f, 0);
                Key((.3f+.1f*T)*Stretch, 0); Key(.16f*Stretch, .64f);
                Key(.22f*Stretch, .94f); Key(.14f*Stretch, 1);
            }
            else // A long, incomplete restoration: different fixtures leave different dark pockets.
            {
                Key(.075f, 0); Key(.16f*Stretch, 0); Key(.18f*Stretch, .7f); Key(.07f, Residual);
                Key((1.8f+.18f*T)*Stretch, Residual); Key(.1f, .64f); Key(.11f, Residual*.6f);
                Key((.45f+.12f*T)*Stretch, Residual*.6f); Key(.14f, .86f);
                if (SectionRandom.FRand()<.65f)
                {
                    RecoverySide=SectionRandom.RandRange(0,1)==0 ? -1.f : 1.f;
                    Key(.12f, 0);
                    RecoveryQuietAt=Time;
                    Key(.26f, 0);
                    RecoveryStartsAt=Time;
                    Key(.10f, .45f);
                    Key(.10f, .45f);
                    Key(.055f, .38f);
                    Key(.22f, 1);
                }
                else
                {
                    Key(.12f, Residual);
                    Key(.3f*Stretch, 1);
                }
            }
        }
        void Step(float Delta, int32 Count, float Target, float MinimumBrightness)
        {
            const float Dt = FMath::Clamp(Delta, 0.f, .25f);
            const float Rate = Target > Strength ? 4.5f : .28f;
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
                    const float Curve=Keys[I].Value<Keys[I-1].Value ? 1.f-FMath::Pow(1.f-A,3.f) : A*A*(3-2*A);
                    Envelope = FMath::Lerp(Keys[I-1].Value, Keys[I].Value, Curve);
                    break;
                }
                if (Elapsed >= Keys.Last().Time)
                {
                    Keys.Reset();
                    Rest = Random.FRandRange(2.5f-.7f*T, 5.5f-1.3f*T);
                    // Occasional longer silence keeps this from becoming a metronomic repeating alarm.
                    if (Random.FRand()<.18f) Rest*=Random.FRandRange(1.7f,2.6f);
                }
            }
            // A small, non-periodic-frequency after-effect lingers while the threat decays.
            const float Phase=Random.GetInitialSeed()*.001f;
            const float Shiver = .035f * Strength * (.5f+.3f*FMath::Sin(Elapsed*13.7f+Phase)+.2f*FMath::Sin(Elapsed*21.1f+Phase*1.7f))
                * (1.f-Envelope)*FMath::Min(1.f,Envelope/.15f);
            // Once a nearby group has activated the fault, dark keys really extinguish the fixture.
            // Depth varies the lit recovery segments without lifting an off key above zero.
            // Distant/decaying influence still blends continuously back to normal light.
            const float FaultBlend=FMath::Clamp(Strength/.15f,0.f,1.f);
            const float Fault=FMath::Clamp(Envelope-(1.f-Depth)*Envelope*(1.f-Envelope)-Shiver,0.f,1.f);
            Brightness = FMath::Clamp(FMath::Lerp(1.f,Fault,FaultBlend),FMath::Clamp(MinimumBrightness,0.f,1.f),1.f);
            const bool HasRecovery=!Keys.IsEmpty()&&RecoveryStartsAt>=0;
            const bool IsRecovering=HasRecovery&&Elapsed>=RecoveryStartsAt;
            RecoveryProgress=IsRecovering ? FMath::Clamp((Elapsed-RecoveryStartsAt)/(Keys.Last().Time-RecoveryStartsAt),0.f,1.f) : 1.f;
            if (IsRecovering)
            {
                const float Middle=FMath::Lerp(.03f,1.f,FMath::SmoothStep(.30f,.82f,RecoveryProgress));
                const float Far=FMath::Lerp(.015f,1.f,FMath::SmoothStep(.50f,1.f,RecoveryProgress));
                const FVector RecoverySections=RecoverySide>0 ? FVector(Far,Middle,1) : FVector(1,Middle,Far);
                SectionLevels=FMath::Lerp(FVector::OneVector,RecoverySections,FaultBlend);
                Electrical=bSound ? (1.f-RecoveryProgress)*Strength*.32f : 0.f;
                return;
            }
            const bool SectionsActive=!Keys.IsEmpty()&&Strength>.01f&&Brightness>0&&Envelope<.98f;
            if(SectionsActive)
            {
                SectionRest-=Dt;
                if(SectionRest<=0)
                {
                    SectionRest=SectionRandom.FRandRange(.055f,.19f);
                    SectionTarget=FVector(SectionRandom.FRandRange(.65f,1.f),SectionRandom.FRandRange(.65f,1.f),SectionRandom.FRandRange(.65f,1.f));
                    const int32 Failed=SectionRandom.RandRange(0,2);
                    SectionTarget[Failed]=SectionRandom.FRandRange(.001f,.035f);
                    if(SectionRandom.FRand()<.3f)SectionTarget[(Failed+SectionRandom.RandRange(1,2))%3]=SectionRandom.FRandRange(.008f,.065f);
                }
            }
            const FVector SectionGoal=SectionsActive?FMath::Lerp(FVector::OneVector,SectionTarget,FaultBlend):FVector::OneVector;
            SectionLevels=FMath::VInterpTo(SectionLevels,SectionGoal,Dt,SectionsActive?35.f:16.f);
            if(SectionLevels.Equals(FVector::OneVector,.0001))SectionLevels=FVector::OneVector;
            const bool QuietRecovery=HasRecovery&&Elapsed>=RecoveryQuietAt&&Elapsed<RecoveryStartsAt;
            Electrical = bSound&&!QuietRecovery ? FMath::Clamp(((1.f-Envelope)+(1.f-static_cast<float>(SectionLevels.GetMin()))*.1f)*Strength, 0.f, 1.f) : 0.f;
        }
    };
}

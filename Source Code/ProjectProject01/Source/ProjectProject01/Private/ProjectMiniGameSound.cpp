#include "ProjectMiniGameSound.h"
#include "ProjectProject01GameInstance.h"
#include "Sound/SoundGenerator.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    /**
     * Small-speaker sounds: filtered noise and a few sine partials with short envelopes. Each kind is one fixed shape,
     * slightly different every time it plays (the noise seed), so repeats do not sound machine-stamped. A directional
     * sound is placed in stereo by level and inter-ear delay; from behind it is low-passed and quieter.
     */
    class FMiniGameGenerator final:public ISoundGenerator
    {
        EProjectMiniGameSound Kind;float Rate,Gain,Stretch;int32 Frame=0,Total,Channels;uint32 Noise;float Low=0,Band=0,Slow=0;
        float LeftGain=1,RightGain=1,Muffle=1,Dull=0;int32 LeftDelay=0,RightDelay=0;
        static constexpr int32 History=64;float Past[History]={};int32 Cursor=0;
        float N(){Noise=1664525u*Noise+1013904223u;return (float(Noise>>8)/16777215.f)*2-1;}
        static float Env(float T,float Attack,float Decay){return FMath::Min(1.f,T/FMath::Max(Attack,1e-4f))*FMath::Exp(-T*Decay);}
        static float Tone(float T,float Hz){return FMath::Sin(2*PI*Hz*T);}
        float Sample(float T,float W)
        {
            switch(Kind)
            {
            case EProjectMiniGameSound::Creak:
            {
                // Plastic rubbing: a rough, slowly bending tone made of noise bursts.
                const float Grain=FMath::Abs(FMath::Sin(2*PI*(38+18*T)*T));
                return .16f*Env(T,.02f,6.f)*Grain*(Low-Slow)+.05f*Env(T,.01f,9.f)*Tone(T,220+60*FMath::Sin(9*T));
            }
            case EProjectMiniGameSound::Step:return .12f*Env(T,.001f,90.f)*(Low-Slow)+.05f*Env(T,.001f,70.f)*Tone(T,140);
            case EProjectMiniGameSound::Pick:return .1f*Env(T,.0008f,160.f)*(Band-Low)+.06f*Env(T,.001f,120.f)*Tone(T,880);
            case EProjectMiniGameSound::LightsOut:return .2f*Env(T,.001f,26.f)*Slow+.14f*Env(T,.002f,14.f)*Tone(T,62);
            case EProjectMiniGameSound::LightsOn:return .12f*Env(T,.001f,40.f)*Low+.06f*FMath::Min(1.f,T/.05f)*FMath::Exp(-T*3.f)*(Tone(T,100)+.5f*Tone(T,200));
            case EProjectMiniGameSound::Switch:return .13f*Env(T,.0006f,220.f)*Band+.08f*(T>.03f?Env(T-.03f,.0006f,260.f):0.f)*Low;
            case EProjectMiniGameSound::Fuse:return .16f*Env(T,.001f,60.f)*Low+.07f*Env(T,.02f,5.f)*(Tone(T,120)+.4f*Tone(T,240));
            case EProjectMiniGameSound::Shutter:
            {
                const float Rattle=.5f+.5f*FMath::Sin(2*PI*28*T);
                return .15f*FMath::Min(1.f,T/.02f)*FMath::Exp(-T*2.6f)*Rattle*(Band-Slow)+.18f*(T>.55f?Env(T-.55f,.001f,30.f):0.f)*Slow;
            }
            case EProjectMiniGameSound::Good:return .09f*(Env(T,.004f,7.f)*Tone(T,660)+(T>.11f?Env(T-.11f,.004f,6.f)*Tone(T,880):0.f));
            case EProjectMiniGameSound::Bad:return .11f*Env(T,.004f,9.f)*(Tone(T,180)+.6f*Tone(T,190))+.05f*Env(T,.001f,30.f)*Slow;
            case EProjectMiniGameSound::Caught:return .2f*Env(T,.002f,4.f)*Slow+.1f*Env(T,.01f,3.f)*(Tone(T,55)+.5f*Tone(T,82.4f))+.06f*Env(T,.001f,12.f)*(Band-Low);
            case EProjectMiniGameSound::Knock:
            {
                // Two dull knocks on a metal pipe, ringing a little.
                const float Second=T>.16f?Env(T-.16f,.001f,22.f):0.f;
                return .16f*(Env(T,.001f,22.f)+Second)*(Tone(T,310)+.4f*Tone(T,740))+.08f*(Env(T,.001f,90.f)+(T>.16f?Env(T-.16f,.001f,90.f):0.f))*Low;
            }
            case EProjectMiniGameSound::Rattle:return .12f*FMath::Min(1.f,T/.01f)*FMath::Exp(-T*5.f)*(.5f+.5f*FMath::Sign(FMath::Sin(2*PI*17*T)))*(Band-Slow);
            case EProjectMiniGameSound::Breath:
            {
                // A short, held breath drawn in through the nose: soft noise rising and falling.
                const float Shape=FMath::Sin(PI*FMath::Clamp(T/.5f,0.f,1.f));
                return .09f*Shape*Shape*(Band-Low);
            }
            }
            return 0;
        }
    public:
        FMiniGameGenerator(const UProjectMiniGameSound& Sound,float InRate,uint32 Seed):Kind(Sound.Kind),Rate(FMath::Max(8000.f,InRate)),Gain(Sound.Gain),
            Stretch(FMath::Clamp(Sound.LengthScale,.2f,2.f)),Channels(Sound.bDirectional?2:1),Noise(Seed|1u)
        {
            Total=FMath::CeilToInt(Rate*UProjectMiniGameSound::Length(Kind)*Stretch);
            if(Sound.bDirectional)
            {
                const float A=FMath::DegreesToRadians(Sound.Angle),Side=FMath::Sin(A),Front=FMath::Cos(A);
                LeftGain=FMath::Sqrt((1-Side)*.5f);RightGain=FMath::Sqrt((1+Side)*.5f);
                // Up to 0.6 ms later at the far ear; from behind quieter and duller (the cue for front/back).
                const int32 Delay=FMath::RoundToInt(.0006f*Rate*FMath::Abs(Side));
                (Side>0?LeftDelay:RightDelay)=FMath::Min(Delay,History-1);
                Dull=FMath::Clamp(-Front,0.f,1.f);Muffle=1-.3f*Dull;
            }
        }
        virtual int32 GetNumChannels()const override{return Channels;}
        virtual bool IsFinished()const override{return Frame>=Total;}
        virtual int32 OnGenerateAudio(float* Audio,int32 Samples)override
        {
            FMemory::Memzero(Audio,Samples*sizeof(float));
            const int32 Frames=Samples/Channels;
            // Behind: a one-pole low-pass from 6 kHz down to 900 Hz.
            const float Cut=FMath::Lerp(6000.f,900.f,Dull),Smooth=1.f-FMath::Exp(-2.f*PI*Cut/Rate);
            for(int32 I=0;I<Frames&&!IsFinished();++I)
            {
                const float T=float(Frame++)/Rate/Stretch,W=N();
                Low+=(1.f-FMath::Exp(-2.f*PI*1400.f/Rate))*(W-Low);
                Band+=(1.f-FMath::Exp(-2.f*PI*3200.f/Rate))*(W-Band);
                Slow+=(1.f-FMath::Exp(-2.f*PI*260.f/Rate))*(W-Slow);
                float S=Sample(T,W)*Gain;
                // Fade the last 20 ms so a shortened sound never clicks.
                const int32 Left=Total-Frame;if(Left<Rate*.02f)S*=Left/(Rate*.02f);
                if(Channels==1){Audio[I]=FMath::Clamp(S,-1.f,1.f);continue;}
                DullState+=Smooth*(S-DullState);S=DullState*Muffle;
                Past[Cursor]=S;
                const float L=Past[(Cursor-LeftDelay+History)%History]*LeftGain,R=Past[(Cursor-RightDelay+History)%History]*RightGain;
                Cursor=(Cursor+1)%History;
                Audio[I*2]=FMath::Clamp(L,-1.f,1.f);Audio[I*2+1]=FMath::Clamp(R,-1.f,1.f);
            }
            return Samples;
        }
        float DullState=0;
    };
}

float UProjectMiniGameSound::Length(EProjectMiniGameSound Kind)
{
    switch(Kind)
    {
    case EProjectMiniGameSound::Creak:return .55f;
    case EProjectMiniGameSound::Step:case EProjectMiniGameSound::Pick:case EProjectMiniGameSound::Switch:return .1f;
    case EProjectMiniGameSound::LightsOut:return .5f;
    case EProjectMiniGameSound::LightsOn:return .8f;
    case EProjectMiniGameSound::Fuse:return .7f;
    case EProjectMiniGameSound::Shutter:return .8f;
    case EProjectMiniGameSound::Good:return .55f;
    case EProjectMiniGameSound::Bad:return .45f;
    case EProjectMiniGameSound::Knock:return .45f;
    case EProjectMiniGameSound::Rattle:return .5f;
    case EProjectMiniGameSound::Breath:return .5f;
    default:return 1.1f;
    }
}
UProjectMiniGameSound::UProjectMiniGameSound(const FObjectInitializer& Initializer):Super(Initializer)
{SetSampleRate(24000);NumChannels=1;Duration=1.1f;bLooping=false;}
ISoundGeneratorPtr UProjectMiniGameSound::CreateSoundGenerator(const FSoundGeneratorInitParams& Params)
{
    static uint32 Seed=0x5eed1u;Seed=Seed*747796405u+2891336453u;
    return MakeShared<FMiniGameGenerator,ESPMode::ThreadSafe>(*this,Params.SampleRate>0?Params.SampleRate:24000.f,Seed);
}
void UProjectMiniGameSound::Play(EProjectMiniGameSound Kind)
{
    if(!FSlateApplication::IsInitialized())return;
    static TMap<uint8,TStrongObjectPtr<UProjectMiniGameSound>> Waves;
    auto& Wave=Waves.FindOrAdd(uint8(Kind));
    if(!Wave.IsValid()){Wave.Reset(NewObject<UProjectMiniGameSound>(GetTransientPackage()));Wave->Kind=Kind;Wave->Duration=Length(Kind);}
    if(auto* Settings=UProjectProject01GameUserSettings::Get())Wave->SoundClassObject=Settings->GetUISoundClass();
    FSlateSound Sound;Sound.SetResourceObject(Wave.Get());
    FSlateApplication::Get().PlaySound(Sound);
}
void UProjectMiniGameSound::PlayFrom(EProjectMiniGameSound Kind,float AngleDegrees,float Gain,float LengthScale)
{
    if(!FSlateApplication::IsInitialized())return;
    // Each placement is its own two-channel wave; the last few are kept alive while they play.
    static TArray<TStrongObjectPtr<UProjectMiniGameSound>> Recent;
    auto* Wave=NewObject<UProjectMiniGameSound>(GetTransientPackage());
    Wave->Kind=Kind;Wave->Angle=AngleDegrees;Wave->Gain=Gain;Wave->LengthScale=LengthScale;Wave->bDirectional=true;
    Wave->NumChannels=2;Wave->Duration=Length(Kind)*LengthScale;
    if(auto* Settings=UProjectProject01GameUserSettings::Get())Wave->SoundClassObject=Settings->GetUISoundClass();
    Recent.Emplace(Wave);if(Recent.Num()>12)Recent.RemoveAt(0);
    FSlateSound Sound;Sound.SetResourceObject(Wave);
    FSlateApplication::Get().PlaySound(Sound);
}

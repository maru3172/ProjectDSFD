#include "ProjectMiniGameSound.h"
#include "ProjectProject01GameInstance.h"
#include "Sound/SoundGenerator.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    /**
     * Small-speaker sounds: filtered noise and a few sine partials with short envelopes. Each kind is one fixed shape,
     * slightly different every time it plays (the noise seed), so repeats do not sound machine-stamped.
     */
    class FMiniGameGenerator final:public ISoundGenerator
    {
        EProjectMiniGameSound Kind;float Rate;int32 Frame=0,Total;uint32 Noise;float Low=0,Band=0,Slow=0;
        float N(){Noise=1664525u*Noise+1013904223u;return (float(Noise>>8)/16777215.f)*2-1;}
        static float Env(float T,float Attack,float Decay){return FMath::Min(1.f,T/FMath::Max(Attack,1e-4f))*FMath::Exp(-T*Decay);}
        static float Tone(float T,float Hz){return FMath::Sin(2*PI*Hz*T);}
    public:
        FMiniGameGenerator(EProjectMiniGameSound InKind,float InRate,uint32 Seed):Kind(InKind),Rate(FMath::Max(8000.f,InRate)),Noise(Seed|1u)
        {Total=FMath::CeilToInt(Rate*UProjectMiniGameSound::Length(Kind));}
        virtual int32 GetNumChannels()const override{return 1;}
        virtual bool IsFinished()const override{return Frame>=Total;}
        virtual int32 OnGenerateAudio(float* Audio,int32 Samples)override
        {
            FMemory::Memzero(Audio,Samples*sizeof(float));
            for(int32 I=0;I<Samples&&!IsFinished();++I)
            {
                const float T=float(Frame++)/Rate,W=N();
                Low+=(1.f-FMath::Exp(-2.f*PI*1400.f/Rate))*(W-Low);
                Band+=(1.f-FMath::Exp(-2.f*PI*3200.f/Rate))*(W-Band);
                Slow+=(1.f-FMath::Exp(-2.f*PI*260.f/Rate))*(W-Slow);
                float S=0;
                switch(Kind)
                {
                case EProjectMiniGameSound::Creak:
                {
                    // Plastic rubbing: a rough, slowly bending tone made of noise bursts.
                    const float Grain=FMath::Abs(FMath::Sin(2*PI*(38+18*T)*T));
                    S=.16f*Env(T,.02f,6.f)*Grain*(Low-Slow)+.05f*Env(T,.01f,9.f)*Tone(T,220+60*FMath::Sin(9*T));
                    break;
                }
                case EProjectMiniGameSound::Step:S=.12f*Env(T,.001f,90.f)*(Low-Slow)+.05f*Env(T,.001f,70.f)*Tone(T,140);break;
                case EProjectMiniGameSound::Pick:S=.1f*Env(T,.0008f,160.f)*(Band-Low)+.06f*Env(T,.001f,120.f)*Tone(T,880);break;
                case EProjectMiniGameSound::LightsOut:S=.2f*Env(T,.001f,26.f)*Slow+.14f*Env(T,.002f,14.f)*Tone(T,62);break;
                case EProjectMiniGameSound::LightsOn:S=.12f*Env(T,.001f,40.f)*Low+.06f*FMath::Min(1.f,T/.05f)*FMath::Exp(-T*3.f)*(Tone(T,100)+.5f*Tone(T,200));break;
                case EProjectMiniGameSound::Switch:S=.13f*Env(T,.0006f,220.f)*Band+.08f*(T>.03f?Env(T-.03f,.0006f,260.f):0.f)*Low;break;
                case EProjectMiniGameSound::Fuse:S=.16f*Env(T,.001f,60.f)*Low+.07f*Env(T,.02f,5.f)*(Tone(T,120)+.4f*Tone(T,240));break;
                case EProjectMiniGameSound::Shutter:
                {
                    const float Rattle=.5f+.5f*FMath::Sin(2*PI*28*T);
                    S=.15f*FMath::Min(1.f,T/.02f)*FMath::Exp(-T*2.6f)*Rattle*(Band-Slow)+.18f*(T>.55f?Env(T-.55f,.001f,30.f):0.f)*Slow;
                    break;
                }
                case EProjectMiniGameSound::Good:S=.09f*(Env(T,.004f,7.f)*Tone(T,660)+(T>.11f?Env(T-.11f,.004f,6.f)*Tone(T,880):0.f));break;
                case EProjectMiniGameSound::Bad:S=.11f*Env(T,.004f,9.f)*(Tone(T,180)+.6f*Tone(T,190))+.05f*Env(T,.001f,30.f)*Slow;break;
                case EProjectMiniGameSound::Caught:S=.2f*Env(T,.002f,4.f)*Slow+.1f*Env(T,.01f,3.f)*(Tone(T,55)+.5f*Tone(T,82.4f))+.06f*Env(T,.001f,12.f)*(Band-Low);break;
                }
                Audio[I]=FMath::Clamp(S,-1.f,1.f);
            }
            return Samples;
        }
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
    default:return 1.1f;
    }
}
UProjectMiniGameSound::UProjectMiniGameSound(const FObjectInitializer& Initializer):Super(Initializer)
{SetSampleRate(24000);NumChannels=1;Duration=1.1f;bLooping=false;}
ISoundGeneratorPtr UProjectMiniGameSound::CreateSoundGenerator(const FSoundGeneratorInitParams& Params)
{
    static uint32 Seed=0x5eed1u;Seed=Seed*747796405u+2891336453u;
    return MakeShared<FMiniGameGenerator,ESPMode::ThreadSafe>(Kind,Params.SampleRate>0?Params.SampleRate:24000.f,Seed);
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

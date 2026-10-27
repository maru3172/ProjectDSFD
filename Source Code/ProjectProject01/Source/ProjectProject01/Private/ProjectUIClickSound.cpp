#include "ProjectUIClickSound.h"
#include "Sound/SoundGenerator.h"

namespace
{
    class FClickGenerator final:public ISoundGenerator
    {
        float Rate;int32 Frame=0;uint32 Noise=731;float LowNoise=0,BodyNoise=0;
    public:
        explicit FClickGenerator(float SampleRate):Rate(FMath::Max(8000.f,SampleRate)){}
        virtual int32 GetNumChannels()const override{return 1;}
        virtual bool IsFinished()const override{return Frame>=FMath::CeilToInt(Rate*.075f);}
        virtual int32 OnGenerateAudio(float* Audio,int32 Samples)override
        {
            FMemory::Memzero(Audio,Samples*sizeof(float));
            for(int32 I=0;I<Samples&&!IsFinished();++I)
            {
                const float T=float(Frame++)/Rate;Noise=1664525*Noise+1013904223;
                const float N=(float(Noise>>8)/16777215.f)*2-1;
                // A keycap bottoming out, followed by a quieter return; no electronic pitch sweep.
                LowNoise+=(1.f-FMath::Exp(-2.f*PI*1900.f/Rate))*(N-LowNoise);
                BodyNoise+=(1.f-FMath::Exp(-2.f*PI*420.f/Rate))*(N-BodyNoise);
                const float Down=FMath::Min(1.f,T/.0008f)*FMath::Exp(-T*150.f);
                const float BottomTime=FMath::Max(0.f,T-.012f);
                const float Bottom=T>=.012f?FMath::Min(1.f,BottomTime/.0006f)*FMath::Exp(-BottomTime*210.f):0.f;
                const float ReturnTime=FMath::Max(0.f,T-.041f);
                const float Return=T>=.041f?FMath::Min(1.f,ReturnTime/.0007f)*FMath::Exp(-ReturnTime*240.f):0.f;
                const float Body=FMath::Sin(2*PI*185*T)*FMath::Exp(-T*125.f);
                Audio[I]=.105f*Down*(LowNoise-BodyNoise)+.12f*Bottom*LowNoise
                    +.035f*Return*(N-LowNoise)+.052f*Body;
            }
            return Samples;
        }
    };
}
UProjectUIClickSound::UProjectUIClickSound(const FObjectInitializer& Initializer):Super(Initializer)
{SetSampleRate(24000);NumChannels=1;Duration=.075f;bLooping=false;}
ISoundGeneratorPtr UProjectUIClickSound::CreateSoundGenerator(const FSoundGeneratorInitParams& Params)
{return MakeShared<FClickGenerator,ESPMode::ThreadSafe>(Params.SampleRate>0?Params.SampleRate:24000.f);}

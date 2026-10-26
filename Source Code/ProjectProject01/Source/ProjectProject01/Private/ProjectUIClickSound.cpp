#include "ProjectUIClickSound.h"
#include "Sound/SoundGenerator.h"

namespace
{
    class FClickGenerator final:public ISoundGenerator
    {
        float Rate;int32 Frame=0;uint32 Noise=731;
    public:
        explicit FClickGenerator(float SampleRate):Rate(FMath::Max(8000.f,SampleRate)){}
        virtual int32 GetNumChannels()const override{return 1;}
        virtual bool IsFinished()const override{return Frame>=FMath::CeilToInt(Rate*.06f);}
        virtual int32 OnGenerateAudio(float* Audio,int32 Samples)override
        {
            FMemory::Memzero(Audio,Samples*sizeof(float));
            for(int32 I=0;I<Samples&&!IsFinished();++I)
            {
                const float T=float(Frame++)/Rate;Noise=1664525*Noise+1013904223;
                const float N=(float(Noise>>8)/16777215.f)*2-1;
                const float Envelope=FMath::Min(1.f,T/.002f)*FMath::Exp(-T*85.f);
                Audio[I]=.22f*Envelope*(.78f*FMath::Sin(2*PI*(780*T-1800*T*T))+.22f*N);
            }
            return Samples;
        }
    };
}
UProjectUIClickSound::UProjectUIClickSound(const FObjectInitializer& Initializer):Super(Initializer)
{SetSampleRate(24000);NumChannels=1;Duration=.06f;bLooping=false;}
ISoundGeneratorPtr UProjectUIClickSound::CreateSoundGenerator(const FSoundGeneratorInitParams& Params)
{return MakeShared<FClickGenerator,ESPMode::ThreadSafe>(Params.SampleRate>0?Params.SampleRate:24000.f);}

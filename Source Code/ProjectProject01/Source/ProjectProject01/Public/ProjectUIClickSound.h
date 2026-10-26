#pragma once
#include "Sound/SoundWaveProcedural.h"
#include "ProjectUIClickSound.generated.h"

UCLASS()
class PROJECTPROJECT01_API UProjectUIClickSound final : public USoundWaveProcedural
{
    GENERATED_BODY()
public:
    UProjectUIClickSound(const FObjectInitializer& Initializer);
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& Params)override;
};

#pragma once
#include "Sound/SoundWaveProcedural.h"
#include "ProjectMiniGameSound.generated.h"

/** The phone mini games' sounds, made in code like the UI click (nothing to cook, nothing to load). */
UENUM()
enum class EProjectMiniGameSound:uint8
{
    Creak,      // a mannequin's plastic joint moving in the dark: the tell in 정전 복구 and 한 칸씩
    Step,       // the player's step on the grid
    Pick,       // choosing a mannequin in 움직였다
    LightsOut,  // a breaker dropping: the lights go out
    LightsOn,   // the breaker back with a short hum
    Switch,     // the torch switching on or off
    Fuse,       // a fuse seated in the board
    Shutter,    // a fire shutter rattling down
    Good,       // right answer / stage cleared
    Bad,        // wrong answer
    Caught      // caught by a mannequin
};

UCLASS()
class PROJECTPROJECT01_API UProjectMiniGameSound final : public USoundWaveProcedural
{
    GENERATED_BODY()
public:
    UProjectMiniGameSound(const FObjectInitializer& Initializer);
    EProjectMiniGameSound Kind=EProjectMiniGameSound::Pick;
    static float Length(EProjectMiniGameSound Kind);
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& Params)override;
    /** Plays through the UI sound class (the player's UI volume), from the phone's small speaker. */
    static void Play(EProjectMiniGameSound Kind);
};

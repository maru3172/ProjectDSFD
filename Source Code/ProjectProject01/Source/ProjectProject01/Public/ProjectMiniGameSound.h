#pragma once
#include "Sound/SoundWaveProcedural.h"
#include "ProjectMiniGameSound.generated.h"

/** The phone mini games' sounds, made in code like the UI click (nothing to cook, nothing to load). */
UENUM()
enum class EProjectMiniGameSound:uint8
{
    Creak,      // a mannequin's plastic joint moving: the tell in 정전 복구 and 한 칸씩, the sound to find in 어디서 났지
    Step,       // the player's step on the grid
    Pick,       // choosing a mannequin in 움직였다
    LightsOut,  // a breaker dropping: the lights go out
    LightsOn,   // the breaker back with a short hum
    Switch,     // the torch switching on or off
    Fuse,       // a fuse seated in the board
    Shutter,    // a fire shutter rattling down
    Good,       // right answer / stage cleared
    Bad,        // wrong answer
    Caught,     // caught by a mannequin
    Knock,      // a pipe knocking (decoy in 어디서 났지)
    Rattle,     // a loose shutter rattling (decoy in 어디서 났지)
    Breath      // the survivor's breath before it turns round (the tell in 다가가기)
};

UCLASS()
class PROJECTPROJECT01_API UProjectMiniGameSound final : public USoundWaveProcedural
{
    GENERATED_BODY()
public:
    UProjectMiniGameSound(const FObjectInitializer& Initializer);
    EProjectMiniGameSound Kind=EProjectMiniGameSound::Pick;
    /** Stereo placement (directional sounds only): degrees clockwise from straight ahead. */
    float Angle=0;
    float Gain=1,LengthScale=1;
    bool bDirectional=false;
    static float Length(EProjectMiniGameSound Kind);
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& Params)override;
    /** Plays through the UI sound class (the player's UI volume), from the phone's small speaker. */
    static void Play(EProjectMiniGameSound Kind);
    /**
     * Plays from a direction (0 ahead, 90 right, 180 behind). Left and right come from loudness and the small delay
     * between the ears; sounds from behind are duller and quieter. Best with headphones.
     */
    static void PlayFrom(EProjectMiniGameSound Kind,float AngleDegrees,float Gain=1,float LengthScale=1);
};

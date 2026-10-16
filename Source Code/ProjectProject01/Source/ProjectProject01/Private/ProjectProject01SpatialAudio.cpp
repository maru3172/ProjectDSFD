#include "ProjectProject01SpatialAudio.h"
#include "Components/SynthComponent.h"

void ProjectProject01SpatialAudio::ApplyWorldSFXSettings(FSoundAttenuationSettings& Settings)
{
    // Unreal distances are centimetres. A sphere's falloff begins OUTSIDE the inner radius.
    Settings.bAttenuate=true;
    Settings.bSpatialize=true;
    Settings.AttenuationShape=EAttenuationShape::Sphere;
    Settings.AttenuationShapeExtents=FVector(3000.0f,0.0f,0.0f);
    Settings.DistanceAlgorithm=EAttenuationDistanceModel::Linear;
    Settings.FalloffDistance=3000.0f;
    // Keep the distinction unambiguous: open space changes volume with distance, not wall filtering.
    Settings.bAttenuateWithLPF=false;
    Settings.bEnableListenerFocus=false;
    Settings.bEnableOcclusion=true;
    // A merged room mesh can have one convex simple hull covering its empty interior.
    // Test the actual surfaces instead: a floor below both endpoints is not a wall.
    Settings.bUseComplexCollisionForOcclusion=true;
    Settings.OcclusionTraceChannel=ECC_Visibility;
    // An inactive/custom plugin slot suppresses the engine's built-in trace, even when it is null.
    Settings.PluginSettings.OcclusionPluginSettingsArray.Reset();
    Settings.OcclusionVolumeAttenuation=0.25f;
    Settings.OcclusionLowPassFilterFrequency=600.0f;
    Settings.OcclusionInterpolationTime=0.15f;
}

void ProjectProject01SpatialAudio::ConfigureWorldSynth(USynthComponent& Synth)
{
    Synth.bOverrideAttenuation=true;
    Synth.bAllowSpatialization=true;
    Synth.bIsUISound=false;
    ApplyWorldSFXSettings(Synth.AttenuationOverrides);
}

USoundAttenuation* ProjectProject01SpatialAudio::MakeOccludedCopy(UObject* Outer,const USoundAttenuation* Source,
    const FSoundAttenuationSettings& Fallback)
{
    auto* Copy=NewObject<USoundAttenuation>(Outer,NAME_None,RF_Transient);
    Copy->Attenuation=IsValid(Source)?Source->Attenuation:Fallback;
    ApplyWorldSFXSettings(Copy->Attenuation);
    return Copy;
}

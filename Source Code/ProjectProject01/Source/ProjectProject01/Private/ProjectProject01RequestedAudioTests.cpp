#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "FootstepSynthComponent.h"
#include "PlasticJointSynthComponent.h"
#include "TensionBreathingSynthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "PlayerHeartbeatComponent.h"
#include "PlayerCharacter.h"
#include "HelperRearGuardCharacter.h"
#include "Audio.h"
#include "Sound/SoundWave.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
    bool SaveAudioPreview(const TCHAR* Name, const TArray<float>& Pulse, int32 Channels, int32 IntervalFrames, int32 Repeats)
    {
        constexpr int32 Rate=48000;
        TArray<int16> PCM;
        PCM.SetNumZeroed((IntervalFrames*(Repeats-1)+FMath::Max(IntervalFrames,Pulse.Num()/Channels))*Channels);
        for (int32 Beat=0;Beat<Repeats;++Beat)
            for (int32 Index=0;Index<Pulse.Num();++Index)
                PCM[Beat*IntervalFrames*Channels+Index]=static_cast<int16>(FMath::Clamp(Pulse[Index],-1.0f,1.0f)*32767.0f);
        TArray<uint8> Wave;
        SerializeWaveFile(Wave,reinterpret_cast<const uint8*>(PCM.GetData()),PCM.Num()*sizeof(int16),Channels,Rate);
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("AudioMaterialRefresh");
        IFileManager::Get().MakeDirectory(*Folder,true);
        return FFileHelper::SaveArrayToFile(Wave,*(Folder/Name));
    }
    double Energy(const TArray<float>& Audio, int32 Start, int32 End)
    {
        double Sum=0;
        for (int32 Index=Start;Index<End;++Index) Sum+=FMath::Square(static_cast<double>(Audio[Index]));
        return Sum/FMath::Max(1,End-Start);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectFootstepTimbreTest,"ProjectProject01.Audio.MaterialFootsteps",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectFootstepTimbreTest::RunTest(const FString&)
{
    auto* Synth=NewObject<UFootstepSynthComponent>();
    auto Render=[Synth](EProjectProject01FootstepStyle Style,bool Running,float Gain,int32 Block=511)
    {
        int32 Rate=48000;Synth->Init(Rate);
        Synth->ActiveStyle=Style;Synth->bPulseRunning=Running;Synth->bSkidPulse=false;
        Synth->PulsePhaseLow=Synth->PulsePhaseHigh=Synth->FilteredNoise=Synth->FilteredNoiseSlow=0;
        Synth->PlasticWeightPhase=0;
        Synth->PulsePitch=1;Synth->PulseGain=Gain;Synth->NoiseState=0x12345678u;Synth->bPulseActive=true;
        TArray<float> Samples;Samples.SetNumZeroed(16800);
        for (int32 Offset=0;Offset<Samples.Num();Offset+=Block)
            Synth->OnGenerateAudio(Samples.GetData()+Offset,FMath::Min(Block,Samples.Num()-Offset));
        return Samples;
    };
    const auto Walk=Render(EProjectProject01FootstepStyle::MarbleSneaker,false,0.85f);
    const auto Run=Render(EProjectProject01FootstepStyle::MarbleSneaker,true,0.85f*1.18f);
    const auto Partner=Render(EProjectProject01FootstepStyle::MarbleSneaker,false,0.425f);
    const auto Plastic=Render(EProjectProject01FootstepStyle::MarblePlastic,true,0.9f*1.18f);
    const auto Whole=Render(EProjectProject01FootstepStyle::MarbleSneaker,false,0.85f,16800);
    float Peak=0,RatioError=0,BlockError=0;bool Finite=true;
    double MaterialDifference=0,MaterialEnergy=0;
    for (int32 Index=0;Index<Walk.Num();++Index)
    {
        for (const float Sample:{Walk[Index],Run[Index],Partner[Index],Plastic[Index]})
        {Finite&=FMath::IsFinite(Sample);Peak=FMath::Max(Peak,FMath::Abs(Sample));}
        RatioError=FMath::Max(RatioError,FMath::Abs(Partner[Index]-Walk[Index]*0.5f));
        BlockError=FMath::Max(BlockError,FMath::Abs(Walk[Index]-Whole[Index]));
        MaterialDifference+=FMath::Square(static_cast<double>(Walk[Index]-Plastic[Index]));
        MaterialEnergy+=FMath::Square(static_cast<double>(Walk[Index]))+FMath::Square(static_cast<double>(Plastic[Index]));
    }
    TestTrue(TEXT("Material sounds are finite and retain unclipped headroom"),Finite&&Peak>0.1f&&Peak<0.94f);
    TestTrue(TEXT("Partner uses identical material timbre at exactly half amplitude"),RatioError<0.000001f);
    TestTrue(TEXT("Audio block boundaries do not restart footsteps"),BlockError<0.000001f);
    TestTrue(TEXT("Polished stone has a short dry tail, not sustained granular friction"),Energy(Walk,2400,7200)<Energy(Walk,0,1440)*0.04);
    // Distinct waveforms may have the same loudness. Compare signal shape, not total energy difference.
    TestTrue(TEXT("Plastic and sneaker retain distinct waveforms at comparable loudness"),MaterialEnergy>0&&MaterialDifference/MaterialEnergy>0.25);
    TestTrue(TEXT("Footsteps start and finish without hard edges"),Walk[0]==0&&Walk.Last()==0&&Plastic[0]==0&&Plastic.Last()==0);
    const auto* Player=GetDefault<APlayerCharacter>()->FindComponentByClass<UFootstepSynthComponent>();
    const auto* Helper=GetDefault<AHelperRearGuardCharacter>()->FindComponentByClass<UFootstepSynthComponent>();
    TestNotNull(TEXT("Player owns material synth"),Player);TestNotNull(TEXT("Partner owns material synth"),Helper);
    if (Player&&Helper) TestEqual(TEXT("Actual character defaults preserve fifty percent partner mix"),Helper->FootstepVolumeScale,Player->FootstepVolumeScale*0.5f);
    Synth->WalkSounds.Add(NewObject<USoundWave>());
    TestFalse(TEXT("Old assigned WAV cannot silently bypass refreshed synthesis"),Synth->PlaySoundAsset(false));
    TestTrue(TEXT("Write sneaker walk audition"),SaveAudioPreview(TEXT("Sneaker_Walk.wav"),Walk,1,28800,5));
    TestTrue(TEXT("Write sneaker run audition"),SaveAudioPreview(TEXT("Sneaker_Run.wav"),Run,1,17760,7));
    TestTrue(TEXT("Write half-volume partner audition"),SaveAudioPreview(TEXT("Partner_Walk.wav"),Partner,1,28800,5));
    TestTrue(TEXT("Write plastic run audition"),SaveAudioPreview(TEXT("Plastic_Run.wav"),Plastic,1,18720,7));
    auto* JointSynth=NewObject<UPlasticJointSynthComponent>();
    auto RenderBody=[&](bool WithFoot,int32 Block)
    {
        int32 Rate=48000;JointSynth->Init(Rate);
        JointSynth->MotionFrameIndex=0;JointSynth->MotionNoiseState=0x819ab312u;
        JointSynth->MotionPhase=JointSynth->MotionNoise=JointSynth->MotionNoiseSlow=0;
        JointSynth->MotionDelay=0.05f;JointSynth->MotionDuration=0.24f;JointSynth->MotionGain=0.18f;
        JointSynth->bMotionRunning=true;JointSynth->bMotionActive=true;
        TArray<float> Samples;Samples.SetNumZeroed(16800);
        for(int32 Offset=0;Offset<Samples.Num();Offset+=Block)
            JointSynth->OnGenerateAudio(Samples.GetData()+Offset,FMath::Min(Block,Samples.Num()-Offset));
        if (WithFoot)
        {
            const auto Contact=Render(EProjectProject01FootstepStyle::MarblePlastic,true,0.9f*1.18f,Block);
            for(int32 Index=0;Index<Samples.Num();++Index) Samples[Index]+=Contact[Index];
        }
        return Samples;
    };
    const auto Joint=RenderBody(false,511);
    const auto Mixed=RenderBody(true,511);
    const auto MixedWhole=RenderBody(true,16800);
    float JointPeak=0,MixedPeak=0;bool Continuous=true,BodyFinite=true;
    for(int32 Index=0;Index<Joint.Num();++Index)
    {
        JointPeak=FMath::Max(JointPeak,FMath::Abs(Joint[Index]));MixedPeak=FMath::Max(MixedPeak,FMath::Abs(Mixed[Index]));
        Continuous&=FMath::IsNearlyEqual(Mixed[Index],MixedWhole[Index],0.000001f);
        BodyFinite&=FMath::IsFinite(Joint[Index])&&FMath::IsFinite(Mixed[Index]);
    }
    TestTrue(TEXT("Joint sound is subtle and mixed plastic retains headroom"),BodyFinite&&JointPeak>0.005f&&JointPeak<0.12f&&MixedPeak<0.94f);
    TestTrue(TEXT("Separate body voice remains continuous between audio buffers"),Continuous);
    TestTrue(TEXT("Delayed joint sound leaves the initial foot contact intact"),Energy(Joint,0,2300)==0&&Energy(Joint,3000,12000)>0.000001);
    TestTrue(TEXT("Body sound ends without a lingering loop"),Energy(Joint,14400,Joint.Num())==0);
    const auto& Near=JointSynth->AttenuationOverrides;
    TestTrue(TEXT("Joint has independent 3D attenuation, not UI audio"),JointSynth->bOverrideAttenuation&&JointSynth->bAllowSpatialization&&!JointSynth->bIsUISound);
    TestEqual(TEXT("Joint retains full distance gain within one metre"),Near.AttenuationShapeExtents.X,100.0);
    for (float Distance:{0.0f,50.0f,100.0f})
        TestTrue(TEXT("Very close joint remains clear"),FMath::IsNearlyEqual(Near.Evaluate(FTransform::Identity,FVector(Distance,0,0)),1.0f));
    TestTrue(TEXT("Joint fades halfway at 2.5m"),FMath::IsNearlyEqual(Near.Evaluate(FTransform::Identity,FVector(250,0,0)),0.5f));
    for (float Distance:{400.0f,500.0f,3000.0f})
        TestTrue(TEXT("Joint is inaudible at and beyond four metres"),FMath::IsNearlyZero(Near.Evaluate(FTransform::Identity,FVector(Distance,0,0))));
    TestTrue(TEXT("Close joint still uses actual-surface wall occlusion"),Near.bEnableOcclusion&&Near.bUseComplexCollisionForOcclusion);
    TestEqual(TEXT("Footstep attenuation is not shortened with the joint"),Synth->AttenuationOverrides.AttenuationShapeExtents.X,3000.0);
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto* Owner=World->SpawnActor<ACharacter>();
    auto* Foot=NewObject<UFootstepSynthComponent>(Owner);
    Foot->SetupAttachment(Owner->GetRootComponent());Foot->RegisterComponent();
    Foot->Configure(EProjectProject01FootstepStyle::MarblePlastic,0.9f,false);
    auto* AttachedJoint=Foot->GetOrCreatePlasticJointSynth();
    if (TestNotNull(TEXT("Plastic creates separate body sound component"),AttachedJoint))
    {
        TestTrue(TEXT("Joint follows mannequin root, not foot trace"),AttachedJoint->GetAttachParent()==Owner->GetRootComponent());
        TestTrue(TEXT("One joint component is reused rather than duplicated each step"),AttachedJoint==Foot->GetOrCreatePlasticJointSynth());
        const FVector Before=AttachedJoint->GetComponentLocation();
        Foot->SetRelativeLocation(FVector(0,0,-80));
        TestTrue(TEXT("Moving contact down to floor cannot move the joint into the floor"),Before.Equals(AttachedJoint->GetComponentLocation()));
    }
    Foot->Configure(EProjectProject01FootstepStyle::MarbleSneaker,0.85f,true);
    TestNull(TEXT("Sneakers cannot create or trigger mannequin joint audio"),Foot->GetOrCreatePlasticJointSynth());
    World->DestroyWorld(false);
    TArray<float> HorrorSequence;HorrorSequence.SetNumZeroed(48000*4);
    for(int32 Step=0;Step<8;++Step)
    {
        const auto& OneShot=(Step==1||Step==6)?Mixed:Plastic;
        for(int32 Index=0;Index<OneShot.Num();++Index) HorrorSequence[Step*19200+Index]+=OneShot[Index];
    }
    TestTrue(TEXT("Write occasional creak plus heavy footsteps audition"),SaveAudioPreview(TEXT("Plastic_Horror_Run.wav"),HorrorSequence,1,HorrorSequence.Num(),1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectHeartbeatTimbreTest,"ProjectProject01.Audio.WeightedHeartbeat",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectHeartbeatTimbreTest::RunTest(const FString&)
{
    auto* Synth=NewObject<UHeartbeatSynthComponent>();int32 Rate=48000;Synth->Init(Rate);
    Synth->PulseStrength=0.82f;Synth->bPulseActive=true;
    TArray<float> Pulse;Pulse.SetNumZeroed(48000);
    for (int32 Offset=0;Offset<Pulse.Num();Offset+=1022)
        Synth->OnGenerateAudio(Pulse.GetData()+Offset,FMath::Min(1022,Pulse.Num()-Offset));
    float Peak=0;bool Finite=true,Stereo=true;
    for (int32 Frame=0;Frame<Pulse.Num()/2;++Frame)
    {Finite&=FMath::IsFinite(Pulse[Frame*2]);Stereo&=Pulse[Frame*2]==Pulse[Frame*2+1];Peak=FMath::Max(Peak,FMath::Abs(Pulse[Frame*2]));}
    TestTrue(TEXT("Heartbeat keeps headroom and centered stereo"),Finite&&Stereo&&Peak>0.2f&&Peak<0.94f);
    TestTrue(TEXT("Each system event has one thump without a delayed second attack"),Energy(Pulse,11520,15360)<Energy(Pulse,480,4320)*0.025);
    TestTrue(TEXT("Completed heartbeat is silent until next BPM trigger"),Energy(Pulse,28800,Pulse.Num())==0);
    TestTrue(TEXT("Write heartbeat at existing slow cadence"),SaveAudioPreview(TEXT("Heartbeat_80BPM.wav"),Pulse,2,36000,5));
    TestTrue(TEXT("Write heartbeat at existing urgent cadence"),SaveAudioPreview(TEXT("Heartbeat_150BPM.wav"),TArray<float>(Pulse.GetData(),28800),2,19200,7));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPainBreathingTest,"ProjectProject01.Audio.PainRecoveryBreaths",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectPainBreathingTest::RunTest(const FString&)
{
    auto* Synth=NewObject<UPainGroanSynthComponent>();
    auto Render=[Synth](bool Fatal,float Intensity)
    {
        int32 Rate=48000;Synth->Init(Rate);Synth->VoiceIntensity=Intensity;Synth->bFatalVoice=Fatal;
        Synth->VoiceDurationSeconds=UPainGroanSynthComponent::GetReactionDuration(Fatal,Intensity);Synth->NoiseState=0x91E10DA5u;Synth->bVoiceActive=true;
        TArray<float> Audio;Audio.SetNumZeroed(48000*6);
        for(int32 Offset=0;Offset<Audio.Num();Offset+=1024)
            Synth->OnGenerateAudio(Audio.GetData()+Offset,FMath::Min(1024,Audio.Num()-Offset));
        return Audio;
    };
    const auto Mild=Render(false,0.35f),Strong=Render(false,1.0f),Fatal=Render(true,1.0f);
    auto SegmentEnergy=[](const TArray<float>& Audio,float Start,float End)
    {return Energy(Audio,FMath::RoundToInt(Start*48000),FMath::RoundToInt(End*48000));};
    TestTrue(TEXT("Sharp pain reaction is followed by a silent beat"),SegmentEnergy(Strong,0.02f,0.28f)>0.001&&SegmentEnergy(Strong,0.36f,0.83f)==0);
    for(float Start:{0.85f,1.65f,2.45f})
        TestTrue(TEXT("Each quiet shortened exhalation remains audible"),SegmentEnergy(Strong,Start+0.08f,Start+0.32f)>0.0001);
    for(const FVector2D Gap:{FVector2D(1.30,1.62),FVector2D(2.10,2.42)})
        TestTrue(TEXT("A real silent pause separates the three recovery breaths"),SegmentEnergy(Strong,Gap.X,Gap.Y)==0);
    TestTrue(TEXT("Third breath ends by 2.87 seconds without a final sigh"),
        FMath::IsNearlyEqual(UPainGroanSynthComponent::GetReactionDuration(false),2.87f,0.0001f) &&
        SegmentEnergy(Strong,2.89f,6.0f)==0 && SegmentEnergy(Mild,2.89f,6.0f)==0);
    TestTrue(TEXT("Breathing remains audible but clearly quieter than the initial pain grunt"),SegmentEnergy(Strong,0.85f,2.87f)>0.00001&&SegmentEnergy(Strong,0.85f,2.87f)<SegmentEnergy(Strong,0.02f,0.28f)*0.10);
    TestTrue(TEXT("Lower death count strengthens the initial pain grunt as well as the breaths"),SegmentEnergy(Strong,0.02f,0.28f)>SegmentEnergy(Mild,0.02f,0.28f));
    TestTrue(TEXT("Repeated-hit intensity adds strain without removing breath pauses"),SegmentEnergy(Strong,0.85f,2.87f)>SegmentEnergy(Mild,0.85f,2.87f));
    TestTrue(TEXT("Death is a short grunt, not a sustained scream or recovery breathing"),SegmentEnergy(Fatal,0.02f,0.30f)>0.001&&SegmentEnergy(Fatal,0.40f,6.0f)==0);
    bool Finite=true;for(const float Sample:Strong)Finite&=FMath::IsFinite(Sample)&&FMath::Abs(Sample)<=0.88f;
    TestTrue(TEXT("Pain sequence stays finite and bounded"),Finite);
    const int32 RecoveryPreviewFrames=FMath::RoundToInt((UPainGroanSynthComponent::GetReactionDuration(false)+0.15f)*48000);
    TestTrue(TEXT("Write pain recovery audition with no final sigh"),SaveAudioPreview(TEXT("Pain_Recovery.wav"),TArray<float>(Strong.GetData(),RecoveryPreviewFrames),1,RecoveryPreviewFrames,1));
    TestTrue(TEXT("Write short fatal grunt audition"),SaveAudioPreview(TEXT("Pain_Fatal.wav"),TArray<float>(Fatal.GetData(),27840),1,27840,1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectTensionBreathingTest,"ProjectProject01.Audio.HeartbeatTensionBreathing",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectTensionBreathingTest::RunTest(const FString&)
{
    auto* Heartbeat=NewObject<UPlayerHeartbeatComponent>();
    Heartbeat->CurrentBPM=0;
    TestFalse(TEXT("No active heartbeat means no idle breathing"),Heartbeat->ShouldPlayTensionBreathing());
    TestEqual(TEXT("Inactive heartbeat has zero breathing tension"),Heartbeat->CalculateBreathingTension(),0.0f);
    Heartbeat->BaseMinBPM=60;Heartbeat->BaseMaxBPM=120;Heartbeat->EncounterMaxBPM=165;
    Heartbeat->CurrentBPM=112.5f;
    TestFalse(TEXT("A stale positive BPM cannot bypass inactive heartbeat state"),Heartbeat->ShouldPlayTensionBreathing());
    Heartbeat->bHeartbeatActive=true;
    Heartbeat->CurrentVFXNearbyMannequinCount=1;
    Heartbeat->CurrentBPM=60;
    TestTrue(TEXT("Heartbeat onset enables only the very quiet breath"),Heartbeat->ShouldPlayTensionBreathing());
    TestEqual(TEXT("Minimum active BPM starts at lowest breathing intensity"),Heartbeat->CalculateBreathingTension(),0.0f);
    Heartbeat->CurrentBPM=112.5f;
    TestEqual(TEXT("Breathing follows actual tuned heartbeat range"),Heartbeat->CalculateBreathingTension(),0.5f);
    Heartbeat->CurrentBPM=165;
    TestEqual(TEXT("High encounter BPM reaches roughest ambient breath"),Heartbeat->CalculateBreathingTension(),1.0f);
    Heartbeat->CurrentVFXNearbyMannequinCount=0;
    TestFalse(TEXT("Residual encounter BPM outside the threat range does not start idle breathing"),Heartbeat->ShouldPlayTensionBreathing());
    Heartbeat->CurrentBPM=0;
    TestFalse(TEXT("When heartbeat decays to zero, breathing stops even if active flag is stale"),Heartbeat->ShouldPlayTensionBreathing());
    Heartbeat->bHeartbeatActive=false;
    const float PainDuration=UPainGroanSynthComponent::GetReactionDuration(false);
    Heartbeat->BreathingResumeWorldSeconds=PainDuration;
    TestFalse(TEXT("Ambient breathing is suppressed through all three recovery breaths"),Heartbeat->CanResumeTensionBreathing(PainDuration-0.02));
    TestTrue(TEXT("Ambient breathing can resume after the third breath, without waiting for a removed sigh"),Heartbeat->CanResumeTensionBreathing(PainDuration+0.02));
    Heartbeat->SuspendTensionBreathingForPain(true,UPainGroanSynthComponent::GetReactionDuration(true));
    TestFalse(TEXT("Fatal reaction never resumes ambient breathing later"),Heartbeat->CanResumeTensionBreathing(10000));
    Heartbeat->ResetTensionBreathing();
    TestTrue(TEXT("Checkpoint reload resets fatal suppression"),Heartbeat->CanResumeTensionBreathing(0));
    TestFalse(TEXT("Sprinting outside without any threat episode cannot queue a sigh"),Heartbeat->UpdateReliefBreathState(true,0));
    TestFalse(TEXT("Stopping outside without an episode cannot produce a sigh"),Heartbeat->UpdateReliefBreathState(false,1));
    Heartbeat->bHeartbeatActive=true;Heartbeat->CurrentBPM=165;
    Heartbeat->CurrentVFXNearbyMannequinCount=1;Heartbeat->NearestVFXMannequinDistance=750;
    TestFalse(TEXT("Entering danger while sprinting never plays relief"),Heartbeat->UpdateReliefBreathState(true,2));
    Heartbeat->UpdatePresentedHeartbeat(true,2,1.0f/60);
    TestEqual(TEXT("Actual heartbeat output starts at encountered BPM"),Heartbeat->GetCurrentBPM(),165.0f);
    Heartbeat->CurrentBPM=90;Heartbeat->NearestVFXMannequinDistance=1200;
    Heartbeat->UpdateReliefBreathState(true,3);Heartbeat->UpdatePresentedHeartbeat(true,3,1.0f/60);
    TestEqual(TEXT("Sprinting retains highest experienced BPM rather than most recent lower BPM"),Heartbeat->PresentedBPM,165.0f);
    TestEqual(TEXT("Sprinting retains peak breathing severity"),Heartbeat->LastThreatBreathingTension,1.0f);
    TestEqual(TEXT("Sprinting retains peak vertigo proximity"),Heartbeat->PresentedVFXProximity,0.5f);
    auto* Inside=NewObject<UPlayerHeartbeatComponent>();
    Inside->CurrentVFXNearbyMannequinCount=1;Inside->NearestVFXMannequinDistance=750;
    Inside->bHeartbeatActive=true;Inside->CurrentBPM=165;
    Inside->UpdateReliefBreathState(true,0);Inside->UpdatePresentedHeartbeat(true,0,1.0f/60);
    Inside->CurrentBPM=90;Inside->NearestVFXMannequinDistance=1200;
    TestFalse(TEXT("Stopping while still inside danger never queues relief"),Inside->UpdateReliefBreathState(false,1));
    Inside->UpdatePresentedHeartbeat(false,1,1.0f/60);
    TestTrue(TEXT("Inside sprint stop lowers BPM gradually, not to zero or instantly to the target"),Inside->PresentedBPM>90&&Inside->PresentedBPM<165);
    TestTrue(TEXT("Inside stop retains current nonzero visual danger instead of fading everything out"),FMath::IsNearlyEqual(Inside->PresentedVFXProximity,0.2f,0.00001f)&&Inside->BreathingCalmEndWorldSeconds<0&&!Inside->bReliefBreathQueued);
    for(int32 Frame=0;Frame<240;++Frame)Inside->UpdatePresentedHeartbeat(false,1.0+Frame/60.0,1.0f/60);
    TestTrue(TEXT("Inside stop settles at the actual danger BPM and stays there"),Inside->PresentedBPM>=90&&Inside->PresentedBPM<90.1f);
    TestTrue(TEXT("Breathing also returns to current danger, not the previous peak"),Inside->CalculateBreathingTension()>0&&Inside->CalculateBreathingTension()<1);
    Heartbeat->CurrentVFXNearbyMannequinCount=0;Heartbeat->CurrentBPM=0;Heartbeat->bHeartbeatActive=false;
    TestFalse(TEXT("Exiting while still sprinting delays relief"),Heartbeat->UpdateReliefBreathState(true,4));
    TestTrue(TEXT("Threat breathing continues while running out of danger"),Heartbeat->ShouldCarryTensionWhileSprinting(true));
    Heartbeat->UpdatePresentedHeartbeat(true,4,1.0f/60);
    TestEqual(TEXT("Heartbeat retains peak outside even after raw detection BPM reaches zero"),Heartbeat->PresentedBPM,165.0f);
    TestEqual(TEXT("Vertigo presentation also holds peak outside"),Heartbeat->PresentedVFXProximity,0.5f);
    Heartbeat->HeartbeatVFXProximityAlpha=Heartbeat->PresentedVFXProximity;
    Heartbeat->HeartbeatVFXDensityAlpha=Heartbeat->PresentedVFXDensity;
    TestFalse(TEXT("Stopping outside begins calm-down before the one-shot relief"),Heartbeat->UpdateReliefBreathState(false,5));
    Heartbeat->UpdatePresentedHeartbeat(false,5,1.0f/60);
    TestEqual(TEXT("Calm-down starts without a gain jump"),Heartbeat->GetBreathingCalmAlpha(5),1.0f);
    TestFalse(TEXT("Relief stays queued halfway through calm-down"),Heartbeat->UpdateReliefBreathState(false,6));
    Heartbeat->UpdatePresentedHeartbeat(false,6,1.0f/60);
    TestTrue(TEXT("Heartbeat BPM and gain fade halfway with the same two-second curve"),FMath::IsNearlyEqual(Heartbeat->PresentedBPM,82.5f)&&FMath::IsNearlyEqual(Heartbeat->PresentedHeartbeatGain,0.5f));
    TestTrue(TEXT("Vertigo uses the same calm-down curve without a strength jump"),FMath::IsNearlyEqual(Heartbeat->PresentedVFXProximity,0.25f));
    TestTrue(TEXT("Exactly one relief becomes ready when calm-down ends"),Heartbeat->UpdateReliefBreathState(false,7));
    Heartbeat->UpdatePresentedHeartbeat(false,7,1.0f/60);
    TestTrue(TEXT("Heartbeat and vertigo fully clear at the end"),Heartbeat->PresentedBPM==0&&Heartbeat->PresentedVFXProximity==0&&Heartbeat->PresentedVFXDensity==0);
    Heartbeat->MarkReliefBreathStarted(7);
    TestFalse(TEXT("Finished threat episode does not queue the sigh again"),Heartbeat->UpdateReliefBreathState(false,9));
    TestFalse(TEXT("Running outside after completed relief does not restart tension"),Heartbeat->ShouldCarryTensionWhileSprinting(true));
    Heartbeat->ResetTensionBreathing();Heartbeat->CurrentVFXNearbyMannequinCount=1;
    Heartbeat->bHeartbeatActive=true;Heartbeat->CurrentBPM=100;
    Heartbeat->UpdateReliefBreathState(false,10);
    Heartbeat->CurrentVFXNearbyMannequinCount=0;
    TestFalse(TEXT("Walking out also starts a gradual calm-down"),Heartbeat->UpdateReliefBreathState(false,11));
    TestFalse(TEXT("Starting a new sprint outside does not produce relief"),Heartbeat->UpdateReliefBreathState(true,11.5));
    Heartbeat->UpdatePresentedHeartbeat(true,11.5,1.0f/60);
    TestTrue(TEXT("New outside sprint cannot restore peak or restart walking calm-down"),!Heartbeat->ShouldCarryTensionWhileSprinting(true)&&Heartbeat->BreathingCalmEndWorldSeconds==13&&Heartbeat->PresentedBPM<100);
    TestFalse(TEXT("Stopping that unrelated sprint cannot prematurely trigger the sigh"),Heartbeat->UpdateReliefBreathState(false,12));
    TestTrue(TEXT("Walking exit has one relief ready after the calm-down"),Heartbeat->UpdateReliefBreathState(false,13));
    Heartbeat->MarkReliefBreathStarted(13);
    TestFalse(TEXT("Walking exit also produces no repeated relief"),Heartbeat->UpdateReliefBreathState(false,15));
    Heartbeat->ResetTensionBreathing();Heartbeat->CurrentVFXNearbyMannequinCount=1;Heartbeat->CurrentBPM=165;
    Heartbeat->UpdateReliefBreathState(true,20);Heartbeat->CurrentVFXNearbyMannequinCount=0;
    Heartbeat->UpdateReliefBreathState(true,21);Heartbeat->UpdateReliefBreathState(false,22);
    Heartbeat->CurrentVFXNearbyMannequinCount=1;
    TestFalse(TEXT("Re-entering threat cancels pending relief and calm-down"),Heartbeat->UpdateReliefBreathState(true,23));
    TestTrue(TEXT("Re-entry cancels fade state and pending one-shot"),!Heartbeat->bReliefBreathQueued&&Heartbeat->BreathingCalmEndWorldSeconds<0);
    Heartbeat->ResetTensionBreathing();Heartbeat->bHeartbeatActive=false;Heartbeat->CurrentBPM=0;
    Heartbeat->CurrentVFXNearbyMannequinCount=1;Heartbeat->NearestVFXMannequinDistance=750;
    Heartbeat->UpdateReliefBreathState(true,30);Heartbeat->CurrentVFXNearbyMannequinCount=0;
    Heartbeat->UpdateReliefBreathState(true,31);Heartbeat->UpdatePresentedHeartbeat(true,31,1.0f/60);
    TestTrue(TEXT("Visual-only threat can hold vertigo without inventing heartbeat audio"),Heartbeat->PresentedVFXProximity>0&&Heartbeat->PresentedBPM==0&&!Heartbeat->ShouldCarryTensionWhileSprinting(true));
    Heartbeat->ResetTensionBreathing();
    auto* Synth=NewObject<UTensionBreathingSynthComponent>();
    auto Render=[Synth](float Tension,int32 Block,bool bSuddenDanger=false)
    {
        int32 Rate=48000;Synth->Init(Rate);
        Synth->TargetTension=Tension;Synth->CurrentTension=bSuddenDanger?0.0f:Tension;
        TArray<float> Audio;Audio.SetNumZeroed(48000*10);
        for(int32 Offset=0;Offset<Audio.Num();Offset+=Block)
            Synth->OnGenerateAudio(Audio.GetData()+Offset,FMath::Min(Block,Audio.Num()-Offset));
        return Audio;
    };
    const auto Quiet=Render(0,511),Tense=Render(1,511),Whole=Render(1,480000);
    const auto SuddenDanger=Render(1,511,true);
    const auto CloseChase=Render((120.0f-60.0f)/(165.0f-60.0f),511);
    float Peak=0,OnsetPeak=0,BlockError=0;bool Finite=true;
    for(int32 Index=0;Index<Tense.Num();++Index)
    {
        Finite&=FMath::IsFinite(Tense[Index])&&FMath::IsFinite(Quiet[Index]);
        Peak=FMath::Max(Peak,FMath::Abs(Tense[Index]));
        OnsetPeak=FMath::Max(OnsetPeak,FMath::Abs(Quiet[Index]));
        BlockError=FMath::Max(BlockError,FMath::Abs(Tense[Index]-Whole[Index]));
    }
    TestTrue(TEXT("Ambient breath stays quiet and finite, below the pain reaction"),Finite&&Peak>0.01f&&Peak<0.20f);
    TestTrue(TEXT("Low active BPM starts very quietly, well below tense breathing"),OnsetPeak>0.0001f&&OnsetPeak<0.02f&&Peak>OnsetPeak*3.0f);
    auto CountBreaths=[](const TArray<float>& Audio,int32 End)
    {
        int32 Count=0;bool Audible=false;
        for(int32 Index=0;Index<End;++Index)
        {
            const bool Current=Audio[Index]!=0.0f;
            if(Current&&!Audible)++Count;
            Audible=Current;
        }
        return Count;
    };
    TestEqual(TEXT("Low active BPM cycle contains exactly one haa, not an inhale/exhale pair"),CountBreaths(Quiet,158400),1);
    TestEqual(TEXT("Each fast cycle still contains only one haa"),CountBreaths(Tense,24000),1);
    TestTrue(TEXT("Very close danger produces at least three urgent haas in two seconds"),CountBreaths(Tense,96000)>=3);
    TestTrue(TEXT("A close normal chase is urgent even without a surprise-BPM spike"),CountBreaths(CloseChase,96000)>=3);
    TestTrue(TEXT("Sudden danger shortens old silence instead of waiting the whole calm cycle"),CountBreaths(SuddenDanger,96000)>=3);
    TestTrue(TEXT("Fast urgent breath ends by 0.3s and still leaves a distinct short pause"),Energy(Quiet,28800,158400)==0&&Energy(Tense,14400,24000)==0);
    TestTrue(TEXT("Fast high-danger rhythm is much denser than low danger"),CountBreaths(Tense,Tense.Num())>=15&&CountBreaths(Quiet,Quiet.Num())<=4);
    TestTrue(TEXT("Breathing phase is continuous across audio blocks"),BlockError<0.000001f);
    TestFalse(TEXT("Personal tension breathing is not broadcast as a world sound"),Synth->bAllowSpatialization!=0);
    TestFalse(TEXT("SFX settings apply instead of marking breathing as UI audio"),Synth->bIsUISound!=0);
    TestTrue(TEXT("Write very quiet heartbeat-onset breathing audition"),SaveAudioPreview(TEXT("Breathing_Onset.wav"),Quiet,1,Quiet.Num(),1));
    TestTrue(TEXT("Write heartbeat-tensed breathing audition"),SaveAudioPreview(TEXT("Breathing_Tense.wav"),Tense,1,Tense.Num(),1));
    int32 ReliefRate=48000;Synth->Init(ReliefRate);Synth->bReliefMode=true;
    TArray<float> Relief;Relief.SetNumZeroed(48000*3);
    for(int32 Offset=0;Offset<Relief.Num();Offset+=511)
        Synth->OnGenerateAudio(Relief.GetData()+Offset,FMath::Min(511,Relief.Num()-Offset));
    TestEqual(TEXT("Relief is a single exhalation, not a repeating loop"),CountBreaths(Relief,Relief.Num()),1);
    TestTrue(TEXT("Relief is audible and fully silent after its duration"),Energy(Relief,4800,38400)>0.000001&&Energy(Relief,48000,Relief.Num())==0);
    TestTrue(TEXT("Write one-shot relief exhale audition"),SaveAudioPreview(TEXT("Breathing_Relief.wav"),Relief,1,Relief.Num(),1));
    return true;
}
#endif

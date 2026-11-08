// Fixture-local presentation only: no perception, movement, collision or match-rule changes.
#include "MannequinReactiveLight.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01SpatialAudio.h"
#include "MultiplayTestPlayerController.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"
#include "TimerManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
    // One body snapshot per local world, not one world scan per fixture. Weak world keys keep PIE sessions isolated.
    struct FBodySample { FVector Position; float Speed; };
    struct FWorldBodies { double At=-1; TArray<FBodySample> Bodies; };
    TMap<TWeakObjectPtr<UWorld>, FWorldBodies> WorldBodies;
    const TArray<FBodySample>& SampleBodies(UWorld* World)
    {
        for (auto It=WorldBodies.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
        auto& Sample=WorldBodies.FindOrAdd(World);
        const double Now=World->GetTimeSeconds();
        if (Sample.At<0 || Now<Sample.At || Now-Sample.At>=.18)
        {
            Sample.At=Now; Sample.Bodies.Reset();
            for (TActorIterator<AMannequinAICharacter> It(World); It; ++It)
                if (IsValid(*It) && !It->IsActorBeingDestroyed())
                    Sample.Bodies.Add({It->GetActorLocation(),static_cast<float>(It->GetVelocity().Size2D())});
        }
        return Sample.Bodies;
    }
}

UReactiveLightElectricalSynth::UReactiveLightElectricalSynth(const FObjectInitializer& Initializer) : Super(Initializer)
{
    NumChannels=1; bAutoActivate=false;
    ProjectProject01SpatialAudio::ConfigureWorldSynth(*this);
    AttenuationOverrides.AttenuationShapeExtents=FVector(80,0,0);
    AttenuationOverrides.FalloffDistance=1800; // Also reaches a listener below the authored 12.8m mall ceiling.
}
void UReactiveLightElectricalSynth::SetFault(float Amount)
{
    const float Safe=FMath::Clamp(Amount,0.f,1.f);
    SynthCommand([this,Safe]{TargetGain=Safe;});
}
bool UReactiveLightElectricalSynth::Init(int32& SampleRate)
{
    NumChannels=1; Rate=FMath::Max(8000,SampleRate); Phase=Gain=TargetGain=FilteredNoise=0;
    return true;
}
int32 UReactiveLightElectricalSynth::OnGenerateAudio(float* OutAudio,int32 NumSamples)
{
    if (!OutAudio || NumSamples<=0) return 0;
    for (int32 I=0; I<NumSamples; ++I)
    {
        Gain+=(TargetGain-Gain)*.0015f;
        Noise=1664525u*Noise+1013904223u;
        const float White=static_cast<float>((Noise>>8)&0xFFFFFF)/8388607.5f-1;
        FilteredNoise+=(White-FilteredNoise)*.18f;
        Phase+=2.f*PI*100.f/Rate; if (Phase>2*PI) Phase-=2*PI;
        // Restrained mains buzz with irregular arcing; no jump-scare burst.
        OutAudio[I]=Gain*(.075f*FMath::Sin(Phase)+.025f*FMath::Sin(Phase*3.f)
            +.11f*FilteredNoise*(.2f+.8f*FMath::Abs(FMath::Sin(Phase*.37f))));
    }
    return NumSamples;
}

AMannequinReactiveLight::AMannequinReactiveLight()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickInterval=1.f/30.f;
    SetReplicates(false);
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root")); SetRootComponent(SceneRoot);
    FixtureMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fixture Mesh"));
    FixtureMesh->SetupAttachment(SceneRoot); FixtureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BulbMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bulb Mesh"));
    BulbMesh->SetupAttachment(SceneRoot); BulbMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ReactiveSpotLight=CreateDefaultSubobject<USpotLightComponent>(TEXT("Reactive Spot Light"));
    ReactiveSpotLight->SetupAttachment(SceneRoot); ReactiveSpotLight->SetMobility(EComponentMobility::Movable);
    ReactiveSpotLight->SetIntensity(LightOnIntensity); ReactiveSpotLight->SetAttenuationRadius(1500);
    ReactiveSpotLight->SetInnerConeAngle(28); ReactiveSpotLight->SetOuterConeAngle(44);
    DetectionSphere=CreateDefaultSubobject<USphereComponent>(TEXT("Mannequin Detection Sphere"));
    DetectionSphere->SetupAttachment(SceneRoot); DetectionSphere->InitSphereRadius(DetectionRadius);
    // The existing component remains available to placed Blueprints. Detection now shares a world sample.
    DetectionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DetectionSphere->SetGenerateOverlapEvents(false);
}
void AMannequinReactiveLight::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (DetectionSphere) { DetectionSphere->SetSphereRadius(FMath::Max(0.f,DetectionRadius),false);
        DetectionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision); DetectionSphere->SetGenerateOverlapEvents(false); }
    if (ReactiveSpotLight) ReactiveSpotLight->SetMobility(EComponentMobility::Movable);
    LastBrightness=-1; // Editor edits to the base intensity/material must apply even while logically steady.
    ApplyLightState(true);
}
void AMannequinReactiveLight::BeginPlay()
{
    Super::BeginPlay();
    if (!GetWorld() || GetNetMode()==NM_DedicatedServer) { SetActorTickEnabled(false); return; }
    ResetFaultPresentation();
    RefreshDetectedMannequinsFromWorld();
    GetWorldTimerManager().SetTimer(DetectionRefreshTimerHandle,this,
        &AMannequinReactiveLight::RefreshDetectedMannequinsFromWorld,FMath::Max(.1f,DetectionRefreshIntervalSeconds),true);
}
void AMannequinReactiveLight::EndPlay(const EEndPlayReason::Type Reason)
{
    if (GetWorld()) GetWorldTimerManager().ClearTimer(DetectionRefreshTimerHandle);
    if (ElectricalSynth) ElectricalSynth->Stop();
    DetectedMannequins.Reset();
    Super::EndPlay(Reason);
}
void AMannequinReactiveLight::ResetFaultPresentation()
{
    // A new local seed at level/checkpoint entry prevents every fixture from blinking together.
    FaultPattern.Reset(static_cast<int32>(HashCombine(GetTypeHash(GetPathName()),FMath::Rand())));
    TargetThreat=0; LastBrightness=-1; CurrentDetectedMannequinCount=0; CurrentBlinkIntervalSeconds=0;
    if (ElectricalSynth) ElectricalSynth->Stop();
    ApplyLightState(true); SetActorTickEnabled(false);
}
void AMannequinReactiveLight::RefreshAfterCheckpointRestore()
{
    if (!GetWorld() || GetNetMode()==NM_DedicatedServer) return;
    ResetFaultPresentation();
    RefreshDetectedMannequinsFromWorld();
}
void AMannequinReactiveLight::HandleDetectionBeginOverlap(UPrimitiveComponent*,AActor*,UPrimitiveComponent*,int32,bool,const FHitResult&)
{ RefreshDetectedMannequinsFromWorld(); }
void AMannequinReactiveLight::HandleDetectionEndOverlap(UPrimitiveComponent*,AActor*,UPrimitiveComponent*,int32)
{ RefreshDetectedMannequinsFromWorld(); }
void AMannequinReactiveLight::RefreshInitialOverlaps() { RefreshDetectedMannequinsFromWorld(); }
int32 AMannequinReactiveLight::PruneAndCountDetectedMannequins()
{
    for (auto It=DetectedMannequins.CreateIterator();It;++It) if (!It->IsValid()) It.RemoveCurrent();
    return DetectedMannequins.Num();
}
void AMannequinReactiveLight::RefreshDetectedMannequinsFromWorld()
{
    UWorld* World=GetWorld();
    if (!World || GetNetMode()==NM_DedicatedServer) return;
    APlayerController* Viewer=World->GetFirstPlayerController();
    bPresentationAllowed=IsValid(Viewer) && Viewer->IsLocalController();
    if (const auto* Match=Cast<AMultiplayTestPlayerController>(Viewer))
    {
        const auto Phase=Match->GetMatchPhase();
        bPresentationAllowed &= Phase!=EMultiplayTestMatchPhase::Ending && Phase!=EMultiplayTestMatchPhase::Results
            && Phase!=EMultiplayTestMatchPhase::ReturningToRoom;
    }
    else if (const auto* Survivor=Viewer ? Cast<APlayerCharacter>(Viewer->GetPawn()) : nullptr)
        bPresentationAllowed &= Survivor->CanPlayTensionBreathing();
    CurrentDetectedMannequinCount=0; TargetThreat=0; NearestMannequinDistance=DetectionRadius;
    if (bPresentationAllowed && DetectionRadius>UE_SMALL_NUMBER)
    {
        FVector Eye; FRotator Direction; Viewer->GetPlayerViewPoint(Eye,Direction);
        // Count around the player/viewed survivor, never the entire map. Ignore view direction and watched/frozen state.
        float FixtureDistance=MAX_flt, Moving=0;
        for (const auto& Body:SampleBodies(World))
        {
            if (FMath::Abs(Body.Position.Z-Eye.Z)>FloorHeightTolerance) continue;
            const float Distance=FVector::Dist(Eye,Body.Position);
            if (Distance>DetectionRadius) continue;
            ++CurrentDetectedMannequinCount;
            NearestMannequinDistance=FMath::Min(NearestMannequinDistance,Distance);
            FixtureDistance=FMath::Min(FixtureDistance,static_cast<float>(FVector::Dist(GetActorLocation(),Body.Position)));
            Moving=FMath::Max(Moving,Body.Speed);
        }
        const float FixtureReach=DetectionRadius*1.25f;
        // FloorHeightTolerance separates bodies, not tall ceiling fixtures. A mall lamp may hang 12m above the
        // same floor; its authored light reach decides whether it can affect this viewer's space.
        const float VerticalReach=FMath::Max(FloorHeightTolerance,ReactiveSpotLight ? ReactiveSpotLight->AttenuationRadius : DetectionRadius);
        const bool FixtureWithinReach=FMath::Abs(GetActorLocation().Z-Eye.Z)<=VerticalReach;
        const float Spatial=FixtureWithinReach ? FMath::Clamp(1.f-FixtureDistance/FixtureReach,0.f,1.f) : 0.f;
        const float NearViewer=1.f-FMath::Clamp((static_cast<float>(FVector::Dist(GetActorLocation(),Eye))-DetectionRadius)/(DetectionRadius*.5f),0.f,1.f);
        TargetThreat=ProjectReactiveLight::Threat(CurrentDetectedMannequinCount,NearestMannequinDistance,DetectionRadius)
            *FMath::Sqrt(Spatial)*NearViewer;
        // Motion is only a subtle influence, not a light on/off trigger or an exact movement indicator.
        TargetThreat*=Moving>20 ? 1.f : .93f;
    }
    UpdateBlinkingFromDetectedCount();
}
void AMannequinReactiveLight::UpdateBlinkingFromDetectedCount()
{
    if (!GetWorld() || GetNetMode()==NM_DedicatedServer) return;
    if (!bPresentationAllowed)
    {
        if (ElectricalSynth) ElectricalSynth->Stop();
        FaultPattern.Keys.Reset(); FaultPattern.Strength=0;
        ApplyLightState(true); SetActorTickEnabled(false); return;
    }
    // Reading settings at the polling rate also restores steady light when effects are disabled mid-event.
    if (TargetThreat>0 || FaultPattern.Strength>0 || !FaultPattern.Keys.IsEmpty()) SetActorTickEnabled(true);
    else ApplyBrightness(1.f);
}
void AMannequinReactiveLight::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode()==NM_DedicatedServer) return;
    FaultPattern.Step(DeltaSeconds,CurrentDetectedMannequinCount,TargetThreat,FMath::Clamp(MinimumBrightnessFraction,.1f,1.f));
    ApplyBrightness(FaultPattern.Brightness);
    // Kept for old Blueprint debug displays: this is time to the next random event, not a periodic blink interval.
    CurrentBlinkIntervalSeconds=FMath::Max(0.f,FaultPattern.Rest);
    auto* Settings=UProjectProject01GameUserSettings::Get();
    if (ElectricalSynth) ElectricalSynth->SetVolumeMultiplier(FMath::Max(0.f,ElectricalSoundVolume));
    const float Sound=bPresentationAllowed && bElectricalSound ? FaultPattern.Electrical : 0.f;
    if (Sound>.005f)
    {
        if (!ElectricalSynth)
        {
            ElectricalSynth=NewObject<UReactiveLightElectricalSynth>(this);
            ElectricalSynth->SetupAttachment(SceneRoot); ElectricalSynth->RegisterComponent();
        }
        if (Settings) ElectricalSynth->SoundClass=Settings->GetSFXSoundClass();
        ElectricalSynth->SetVolumeMultiplier(FMath::Max(0.f,ElectricalSoundVolume));
        if (!ElectricalSynth->IsPlaying()) ElectricalSynth->Start();
        ElectricalSynth->SetFault(Sound);
    }
    else if (ElectricalSynth)
    {
        ElectricalSynth->SetFault(0);
        if (FaultPattern.Keys.IsEmpty()) ElectricalSynth->Stop();
    }
    if (TargetThreat==0 && FaultPattern.Strength==0)
    {
        ApplyBrightness(1); if (ElectricalSynth) ElectricalSynth->Stop();
        SetActorTickEnabled(false);
    }
}
void AMannequinReactiveLight::ApplyLightState(bool bNewLightOn)
{
    // Retained for checkpoint/accessibility callers; the runtime pattern uses continuous brightness.
    bLightOn=bNewLightOn; ApplyBrightness(bNewLightOn ? 1.f : 0.f);
}
void AMannequinReactiveLight::ApplyBrightness(float Brightness)
{
    const auto* Settings=UProjectProject01GameUserSettings::Get();
    const float Flash=Settings ? FMath::Clamp(Settings->GetLocalFlashScale(),0.f,1.f) : 1.f;
    const float Presented=FMath::Lerp(1.f,FMath::Clamp(Brightness,0.f,1.f),Flash);
    if (FMath::IsNearlyEqual(Presented,LastBrightness,.0005f)) return;
    LastBrightness=Presented;
    if (ReactiveSpotLight)
    {
        ReactiveSpotLight->SetIntensity(FMath::Max(0.f,LightOnIntensity)*Presented);
        ReactiveSpotLight->SetVisibility(Presented>UE_SMALL_NUMBER,false);
    }
    if (BulbMesh && !EmissiveParameterName.IsNone())
        BulbMesh->SetScalarParameterValueOnMaterials(EmissiveParameterName,
            FMath::Lerp(FMath::Max(0.f,EmissiveOffValue),FMath::Max(0.f,EmissiveOnValue),Presented));
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectProject01ReactiveLightIntervalTest,
    "ProjectProject01.Lighting.MannequinReactiveLightInterval",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectProject01ReactiveLightIntervalTest::RunTest(const FString&)
{
    for (int32 N=0;N<15;++N)
        TestEqual(TEXT("Threat rises only for each complete group of three"),ProjectReactiveLight::Tier(N),FMath::Min(N/3,3));
    TestEqual(TEXT("One or two bodies cannot trigger a fault"),ProjectReactiveLight::Threat(2,0,1500),0.f);
    TestTrue(TEXT("Distance changes strength inside a tier"),
        ProjectReactiveLight::Threat(3,100,1500)>ProjectReactiveLight::Threat(3,1400,1500));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReactiveLightFaultPatternsTest,
    "ProjectProject01.Lighting.MannequinFaultPatterns",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReactiveLightFaultPatternsTest::RunTest(const FString&)
{
    ProjectReactiveLight::FFaultPattern A,B; A.Reset(8721); B.Reset(8721);
    TSet<int32> Kinds; int32 LastSerial=0,LastKind=INDEX_NONE; float Previous=1; bool Varied=false;
    for (int32 I=0;I<18000;++I)
    {
        A.Step(1.f/30.f,9,1.f,.18f); B.Step(1.f/30.f,9,1.f,.18f);
        if (A.Brightness!=B.Brightness) { AddError(TEXT("A fixed seed must be deterministic across renders")); return false; }
        if (!FMath::IsFinite(A.Brightness)||A.Brightness<.18f||A.Brightness>1) { AddError(TEXT("Faults must preserve the light floor and never brighten beyond normal")); return false; }
        Varied |= FMath::Abs(A.Brightness-Previous)>.0001f; Previous=A.Brightness;
        if (A.Serial!=LastSerial)
        {
            if (A.LastKind==LastKind) { AddError(TEXT("The same fault must not repeat immediately")); return false; }
            Kinds.Add(A.LastKind); LastKind=A.LastKind; LastSerial=A.Serial;
        }
    }
    TestTrue(TEXT("Multiple fault families occur, not one repeated blink"),Kinds.Num()==5&&Varied);
    TestTrue(TEXT("There are normal rests between faults"),A.Serial>15&&A.Serial<100);
    float Durations[5]={};
    for (int32 Kind=0;Kind<5;++Kind)
    {
        ProjectReactiveLight::FFaultPattern P; P.Reset(441+Kind);
        for (int32 Attempt=0;Attempt<100&&P.LastKind!=Kind;++Attempt) P.BeginEvent(3);
        if (!TestEqual(TEXT("Sample the requested fault family"),P.LastKind,Kind)) return false;
        Durations[Kind]=P.Keys.Last().Time; P.Strength=.15f;
        float Min=1;
        for (int32 Frame=0;Frame<FMath::CeilToInt(Durations[Kind]*30);++Frame)
        {P.Step(1.f/30.f,9,.15f,.18f);Min=FMath::Min(Min,P.Brightness);}
        TestTrue(TEXT("Each stronger signature keeps at least a 20 percent dip at attenuated/tall-ceiling strength"),Min<.8f);
    }
    TestTrue(TEXT("One blink is short; double blink is separated; voltage sag is slow"),Durations[1]<Durations[2]&&Durations[2]<Durations[0]);
    TestTrue(TEXT("Incomplete restoration holds its dark pocket longer than a voltage sag"),Durations[4]>Durations[0]);
    const float Before=A.Strength; A.Step(1.f/30.f,0,0,.18f);
    TestTrue(TEXT("Leaving the range decays instead of cutting abruptly"),A.Strength>0&&A.Strength<Before);
    for (int32 I=0;I<1200;++I) A.Step(1.f/30.f,0,0,.18f);
    TestEqual(TEXT("Lingering faults return completely to steady light"),A.Brightness,1.f);
    ProjectReactiveLight::FFaultPattern Quiet; Quiet.Reset(14);
    for (int32 I=0;I<3000;++I) Quiet.Step(1.f/30.f,2,0,.18f);
    TestTrue(TEXT("Sub-threshold groups never create an event"),Quiet.Serial==0&&Quiet.Brightness==1);
    return true;
}
#endif

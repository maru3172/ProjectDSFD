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
#include "Components/CapsuleComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#include "ConvexVolume.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
    // One body snapshot per local world, not one world scan per fixture. Weak world keys keep PIE sessions isolated.
    struct FBodySample { FVector Position; float FeetZ, Speed; };
    struct FSharedLightGroup
    {
        ProjectReactiveLight::FSharedFaultSurges Surges;
        TSet<TWeakObjectPtr<AMannequinReactiveLight>> Members;
        FRandomStream CascadeRandom;
        double UsedAt=-1, NextCascadeAt=0;
        uint64 Id=0;
        int32 CascadeSerial=0;
    };
    struct FWorldBodies
    {
        double At=-1; TArray<FBodySample> Bodies;
        TMap<FIntVector,FSharedLightGroup> Groups;
        double PrunedAt=-1, ViewAt=-1;
        FConvexVolume ViewFrustum;
        bool HasView=false;
        uint64 NextGroupId=0;
    };
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
                    Sample.Bodies.Add({It->GetActorLocation(),static_cast<float>(It->GetActorLocation().Z-It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),static_cast<float>(It->GetVelocity().Size2D())});
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
    NumChannels=1; Rate=FMath::Max(8000,SampleRate); Phase=Gain=TargetGain=FilteredNoise=Arc=0;
    return true;
}
int32 UReactiveLightElectricalSynth::OnGenerateAudio(float* OutAudio,int32 NumSamples)
{
    if (!OutAudio || NumSamples<=0) return 0;
    for (int32 I=0; I<NumSamples; ++I)
    {
        const float Attack=FMath::Max(0.f,TargetGain-Gain);
        Arc=FMath::Max(Arc*.9992f,FMath::Min(1.f,Attack*1.8f));
        Gain+=(TargetGain-Gain)*.003f;
        Noise=1664525u*Noise+1013904223u;
        const float White=static_cast<float>((Noise>>8)&0xFFFFFF)/8388607.5f-1;
        FilteredNoise+=(White-FilteredNoise)*.18f;
        Phase+=2.f*PI*100.f/Rate; if (Phase>2*PI) Phase-=2*PI;
        // Less comfortable steady hum; electrical interruption edges carry a brief, dry arc/crackle.
        const float Sample=Gain*(.035f*FMath::Sin(Phase)+.01f*FMath::Sin(Phase*3.f)
            +.10f*FilteredNoise*(.2f+.8f*FMath::Abs(FMath::Sin(Phase*.37f))))
            +Arc*.12f*(White-FilteredNoise);
        // Lower the output after limiting so existing fixtures keep the same sound texture at half gain.
        OutAudio[I]=.1f*FMath::Tanh(Sample/.2f);
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
    // Nominal 4.14m ceiling: floor brightness matches the test corridor's 85cd / 3.30m light.
    ReactiveSpotLight->SetIntensity(LightOnIntensity); ReactiveSpotLight->SetAttenuationRadius(2015.5635f);
    ReactiveSpotLight->SetInnerConeAngle(28); ReactiveSpotLight->SetOuterConeAngle(44);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Projection(
        TEXT("/Game/MyProject/Lighting/M_ReactiveRecoveryProjection.M_ReactiveRecoveryProjection"));
    RecoveryProjectionMaterial=Projection.Object;
    DetectionSphere=CreateDefaultSubobject<USphereComponent>(TEXT("Mannequin Detection Sphere"));
    DetectionSphere->SetupAttachment(SceneRoot); DetectionSphere->InitSphereRadius(DetectionRadius);
    // The existing component remains available to placed Blueprints. Detection now shares a world sample.
    DetectionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DetectionSphere->SetGenerateOverlapEvents(false);
}
void AMannequinReactiveLight::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    bDetectionOriginValid=false;NextFloorProbeAt=0;
    if (DetectionSphere) { DetectionSphere->SetSphereRadius(FMath::Max(0.f,DetectionRadius),false);
        DetectionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision); DetectionSphere->SetGenerateOverlapEvents(false); }
    if (ReactiveSpotLight) ReactiveSpotLight->SetMobility(EComponentMobility::Movable);
    LastBrightness=-1;LastSectionLevels=FVector(-1); // Apply editor intensity/material edits even while steady.
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
    // Individual seeds keep nearby overlapping faults from playing the same envelope in lockstep.
    FaultPattern.Reset(static_cast<int32>(HashCombine(GetTypeHash(GetPathName()),FMath::Rand())));
    TargetThreat=0; LastBrightness=-1;LastSectionLevels=FVector(-1);WakeFromSections=FVector::OneVector; CurrentDetectedMannequinCount=0; CurrentBlinkIntervalSeconds=0;
    WakeFromRecoveryProjection=FLinearColor(1,0,1,0);
    PendingCascadeAt=-1;
    WakeFromColdTint=0;
    bPresentationRelevant=bDetectionOriginValid=false;LastRelevantAt=-1;NextFloorProbeAt=0;LastSharedGroupId=0;WakeBlendElapsed=.25f;WakeFromBrightness=1;
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
bool AMannequinReactiveLight::IntersectsInfluence(const FConvexVolume& Frustum) const
{
    // Cull the light's whole cone, never just the bulb: an offscreen lamp can illuminate visible flooring.
    const FSphere Influence=ReactiveSpotLight ? ReactiveSpotLight->GetBoundingSphere() : FSphere(GetActorLocation(),DetectionRadius);
    return Frustum.IntersectSphere(Influence.Center,Influence.W+500.f);
}
bool AMannequinReactiveLight::IsPresentationRelevant(APlayerController* Viewer)
{
    const auto* Settings=UProjectProject01GameUserSettings::Get();
    const bool Audible=bElectricalSound&&ElectricalSoundVolume>0
        &&(!Settings||(Settings->GetMasterVolume()>0&&Settings->GetSFXVolume()>0));
    FVector Eye;FRotator Rotation;Viewer->GetPlayerViewPoint(Eye,Rotation);
    // Nearby electrical audio must not vanish simply because the lamp is behind the listener.
    bool Relevant=Audible&&FVector::DistSquared(Eye,GetActorLocation())<FMath::Square(1880.f);
    auto& Shared=WorldBodies.FindOrAdd(GetWorld());
    const double Now=GetWorld()->GetTimeSeconds();
    if(Shared.ViewAt<0||Now<Shared.ViewAt||Now-Shared.ViewAt>=.05)
    {
        Shared.ViewAt=Now;Shared.HasView=false;
        auto* Local=Viewer->GetLocalPlayer();
        FSceneViewProjectionData Projection;
        if(Local&&Local->ViewportClient&&Local->ViewportClient->Viewport
            &&Local->GetProjectionData(Local->ViewportClient->Viewport,Projection))
        {
            GetViewFrustumBounds(Shared.ViewFrustum,Projection.ComputeViewProjectionMatrix(),false);
            Shared.HasView=true;
        }
    }
    // Headless tests/temporarily unavailable view data fail open, rather than breaking lighting.
    Relevant|=!Shared.HasView||IntersectsInfluence(Shared.ViewFrustum);
    if(Now<LastRelevantAt)LastRelevantAt=-1;
    if(Relevant)LastRelevantAt=Now;
    return Relevant||(LastRelevantAt>=0&&Now-LastRelevantAt<.75);
}
bool AMannequinReactiveLight::ScheduleNearbyCascade(TSet<TWeakObjectPtr<AMannequinReactiveLight>>& Members,
    uint64 GroupId, const FVector& ViewLocation, double Now, FRandomStream& Random)
{
    struct FCandidate { AMannequinReactiveLight* Light; double ViewDistance; };
    TArray<FCandidate,TInlineAllocator<8>> Candidates;
    for (auto It=Members.CreateIterator(); It; ++It)
    {
        auto* Light=It->Get();
        if (!IsValid(Light) || Light->LastSharedGroupId!=GroupId)
        {
            It.RemoveCurrent();
            continue;
        }
        if (Light->bPresentationAllowed && Light->bPresentationRelevant && Light->TargetThreat>0
            && Light->FaultPattern.Keys.IsEmpty() && Light->PendingCascadeAt<0)
            Candidates.Add({Light,FVector::DistSquared2D(Light->GetActorLocation(),ViewLocation)});
    }
    Candidates.Sort([](const FCandidate& A,const FCandidate& B){return A.ViewDistance>B.ViewDistance;});
    const auto NextNeighbor=[&Candidates](int32 From)
    {
        int32 Nearest=INDEX_NONE;
        double NearestDistance=FMath::Square(1200.);
        const FVector Position=Candidates[From].Light->GetActorLocation();
        for (int32 I=From+1; I<Candidates.Num(); ++I)
        {
            const double Distance=FVector::DistSquared2D(Position,Candidates[I].Light->GetActorLocation());
            if (Distance>=NearestDistance) continue;
            Nearest=I;
            NearestDistance=Distance;
        }
        return Nearest;
    };
    for (int32 First=0; First+1<Candidates.Num(); ++First)
    {
        const int32 Second=NextNeighbor(First);
        if (Second==INDEX_NONE) continue;
        const double FirstAt=Now+Random.FRandRange(.025f,.075f);
        const double SecondAt=FirstAt+Random.FRandRange(.16f,.34f);
        Candidates[First].Light->PendingCascadeAt=FirstAt;
        Candidates[Second].Light->PendingCascadeAt=SecondAt;
        const int32 Third=NextNeighbor(Second);
        if (Third!=INDEX_NONE && Random.FRand()<.55f)
            Candidates[Third].Light->PendingCascadeAt=SecondAt+Random.FRandRange(.16f,.34f);
        return true;
    }
    return false;
}

FVector AMannequinReactiveLight::GetDetectionOrigin()
{
    const FVector Source=ReactiveSpotLight ? ReactiveSpotLight->GetComponentLocation() : GetActorLocation();
    if(!Source.Equals(FloorProbeSource,10.f)){bDetectionOriginValid=false;NextFloorProbeAt=0;}
    if(!bDetectionOriginValid&&GetWorld()->GetTimeSeconds()>=NextFloorProbeAt)
    {
        FloorProbeSource=Source;DetectionOrigin=GetActorLocation();NextFloorProbeAt=GetWorld()->GetTimeSeconds()+1;
        FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        FHitResult Hit;
        const float Reach=FMath::Max(DetectionRadius,ReactiveSpotLight?ReactiveSpotLight->AttenuationRadius:0.f)*2+FloorHeightTolerance;
        if(GetWorld()->LineTraceSingleByObjectType(Hit,Source,Source-FVector(0,0,Reach),Objects,
            FCollisionQueryParams(SCENE_QUERY_STAT(ReactiveLightFloor),true,this)))
        {DetectionOrigin=FVector(Source.X,Source.Y,Hit.ImpactPoint.Z);bDetectionOriginValid=true;}
    }
    return DetectionOrigin;
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
    if(!bPresentationAllowed){CurrentDetectedMannequinCount=0;TargetThreat=0;bPresentationRelevant=false;UpdateBlinkingFromDetectedCount();return;}
    const bool WasRelevant=bPresentationRelevant;
    bPresentationRelevant=IsPresentationRelevant(Viewer);
    if(!bPresentationRelevant)
    {
        // Keep the last state; no brightness reset or restart when the camera turns back.
        if(ElectricalSynth)ElectricalSynth->Stop();
        SetActorTickEnabled(false);return;
    }
    if(!WasRelevant)
    {
        const auto* Settings=UProjectProject01GameUserSettings::Get();
        const float Flash=Settings?FMath::Clamp(Settings->GetLocalFlashScale(),0.f,1.f):1.f;
        const float Held=LastBrightness<0?1.f:LastBrightness;
        // Convert the actually held brightness back through this client's current flash setting.
        WakeFromBrightness=Flash>UE_SMALL_NUMBER?FMath::Clamp((Held-1.f+Flash)/Flash,0.f,1.f):1.f;
        for(int32 I=0;I<3;++I)WakeFromSections[I]=Flash>UE_SMALL_NUMBER&&LastSectionLevels[I]>=0
            ?FMath::Clamp((LastSectionLevels[I]-1+Flash)/Flash,0.,1.):1.;
        WakeFromRecoveryProjection=LastRecoveryProjection;
        WakeFromRecoveryProjection.A=FMath::Min(WakeFromRecoveryProjection.A,Flash);
        WakeFromColdTint=FMath::Min(LastColdTint,Flash);
        WakeBlendElapsed=0;
    }
    CurrentDetectedMannequinCount=0; TargetThreat=0; NearestMannequinDistance=DetectionRadius;
    if (bPresentationAllowed && DetectionRadius>UE_SMALL_NUMBER)
    {
        const FVector Origin=GetDetectionOrigin();
        float Moving=0;
        for (const auto& Body:SampleBodies(World))
        {
            if (FMath::Abs(Body.FeetZ-Origin.Z)>FloorHeightTolerance) continue;
            const float Distance=FVector::Dist2D(Origin,Body.Position);
            if (Distance>DetectionRadius) continue;
            ++CurrentDetectedMannequinCount;
            NearestMannequinDistance=FMath::Min(NearestMannequinDistance,Distance);
            Moving=FMath::Max(Moving,Body.Speed);
        }
        TargetThreat=ProjectReactiveLight::Threat(CurrentDetectedMannequinCount,NearestMannequinDistance,DetectionRadius);
        // Motion is only a subtle influence, not a light on/off trigger or an exact movement indicator.
        TargetThreat*=Moving>20 ? 1.f : .93f;
    }
    if(bPresentationAllowed&&TargetThreat>0)
    {
        auto& Shared=WorldBodies.FindOrAdd(World);const double Now=World->GetTimeSeconds();
        if(Shared.PrunedAt<0||Now-Shared.PrunedAt>10)
        {for(auto It=Shared.Groups.CreateIterator();It;++It)if(Now-It.Value().UsedAt>30)It.RemoveCurrent();Shared.PrunedAt=Now;}
        const FVector Origin=GetDetectionOrigin();const float Cell=FMath::Max(100.f,DetectionRadius*2);
        const FIntVector Key(FMath::FloorToInt((Origin.X+Cell*.5f)/Cell),FMath::FloorToInt((Origin.Y+Cell*.5f)/Cell),
            FMath::RoundToInt(Origin.Z/FMath::Max(100.f,FloorHeightTolerance)));
        auto& Group=Shared.Groups.FindOrAdd(Key);Group.UsedAt=Now;
        if (!Group.Id)
        {
            const int32 Seed=FMath::Rand();
            Group.Surges.Reset(Seed);
            Group.CascadeRandom.Initialize(Seed^0x77533);
            Group.Id=++Shared.NextGroupId;
        }
        if (LastSharedGroupId!=Group.Id)
        {
            FaultPattern.LastSurgeSerial=0;
            PendingCascadeAt=-1;
            LastSharedGroupId=Group.Id;
            Group.Members.Add(this);
        }
        const int32 PreviousSurge=Group.Surges.Serial;
        Group.Surges.Step(Now,CurrentDetectedMannequinCount);
        if (Group.Surges.Serial!=PreviousSurge && Now>=Group.NextCascadeAt && Group.CascadeRandom.FRand()<.22f)
        {
            FVector ViewLocation; FRotator ViewRotation;
            Viewer->GetPlayerViewPoint(ViewLocation,ViewRotation);
            if (ScheduleNearbyCascade(Group.Members,Group.Id,ViewLocation,Now,Group.CascadeRandom))
            {
                Group.CascadeSerial=Group.Surges.Serial;
                Group.NextCascadeAt=Now+Group.CascadeRandom.FRandRange(5.f,8.f);
            }
        }
        if (PendingCascadeAt<0 && Group.CascadeSerial!=Group.Surges.Serial)
            FaultPattern.QueueSharedSurge(Group.Surges.Serial,static_cast<float>(Now-Group.Surges.StartedAt),CurrentDetectedMannequinCount,TargetThreat);
    }
    else PendingCascadeAt=-1;
    UpdateBlinkingFromDetectedCount();
}
void AMannequinReactiveLight::UpdateBlinkingFromDetectedCount()
{
    if (!GetWorld() || GetNetMode()==NM_DedicatedServer) return;
    if (!bPresentationAllowed)
    {
        if (ElectricalSynth) ElectricalSynth->Stop();
        FaultPattern.Keys.Reset(); FaultPattern.Strength=0;
        FaultPattern.SectionLevels=FaultPattern.SectionTarget=FVector::OneVector;
        FaultPattern.RecoveryProgress=1.f;
        FaultPattern.ColdTint=WakeFromColdTint=0;
        PendingCascadeAt=-1;
        WakeFromRecoveryProjection=FLinearColor(1,0,1,0);
        ApplyLightState(true); SetActorTickEnabled(false); return;
    }
    // Reading settings at the polling rate also restores steady light when effects are disabled mid-event.
    if (TargetThreat>0 || FaultPattern.Strength>0 || !FaultPattern.Keys.IsEmpty()) SetActorTickEnabled(true);
    else ApplyBrightness(1.f);
}
void AMannequinReactiveLight::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode()==NM_DedicatedServer||!bPresentationAllowed||!bPresentationRelevant) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if (PendingCascadeAt>=0 && Now>=PendingCascadeAt)
    {
        if (Now-PendingCascadeAt<.18 && TargetThreat>0 && GetThreatTier()>0 && FaultPattern.Keys.IsEmpty())
            FaultPattern.BeginEvent(GetThreatTier(),true);
        PendingCascadeAt=-1;
    }
    FaultPattern.Step(DeltaSeconds,CurrentDetectedMannequinCount,TargetThreat,
        FMath::Clamp(MinimumBrightnessFraction,0.f,1.f),PendingCascadeAt<0);
    WakeBlendElapsed=FMath::Min(.25f,WakeBlendElapsed+FMath::Clamp(DeltaSeconds,0.f,.1f));
    const float A=WakeBlendElapsed/.25f;
    ApplyBrightness(FMath::Lerp(WakeFromBrightness,FaultPattern.Brightness,A*A*(3-2*A)));
    // Kept for old Blueprint debug displays: this is time to the next random event, not a periodic blink interval.
    CurrentBlinkIntervalSeconds=PendingCascadeAt>=0 ? static_cast<float>(PendingCascadeAt-Now) : FMath::Max(0.f,FaultPattern.Rest);
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

void AMannequinReactiveLight::ApplyRecoveryProjection(float Brightness, float FlashScale, float WakeBlend)
{
    if (!ReactiveSpotLight || !RecoveryProjectionMaterial) return;
    const FLinearColor Unmasked(1,0,1,0);
    FLinearColor Projection=Unmasked;
    if (FaultPattern.RecoveryProgress<1.f && FlashScale>0.f)
    {
        // Light function UVs are aligned to local light Z and Y, perpendicular to the beam.
        const FVector DiffuserAxis=BulbMesh->GetForwardVector()*FaultPattern.RecoverySide;
        FVector2D Direction(FVector::DotProduct(DiffuserAxis,ReactiveSpotLight->GetUpVector()),
            FVector::DotProduct(DiffuserAxis,ReactiveSpotLight->GetRightVector()));
        if (!Direction.Normalize()) Direction=FVector2D(FaultPattern.RecoverySide,0);
        const float Spread=FMath::SmoothStep(.35f,1.f,FaultPattern.RecoveryProgress);
        Projection=FLinearColor(Direction.X,Direction.Y,Spread,FlashScale*FMath::Clamp(FaultPattern.Strength/.15f,0.f,1.f));
    }
    Projection=FMath::Lerp(WakeFromRecoveryProjection,Projection,WakeBlend);
    if (Brightness<=UE_SMALL_NUMBER || FlashScale<=0.f || Projection.A<=.001f)
    {
        if (RecoveryProjectionInstance && ReactiveSpotLight->LightFunctionMaterial==RecoveryProjectionInstance)
            ReactiveSpotLight->SetLightFunctionMaterial(nullptr);
        LastRecoveryProjection=Unmasked;
        return;
    }

    if (ReactiveSpotLight->LightFunctionMaterial && ReactiveSpotLight->LightFunctionMaterial!=RecoveryProjectionInstance) return;
    if (!RecoveryProjectionInstance)
        RecoveryProjectionInstance=UMaterialInstanceDynamic::Create(RecoveryProjectionMaterial,this);
    if (!Projection.Equals(LastRecoveryProjection,.0005f))
        RecoveryProjectionInstance->SetVectorParameterValue(TEXT("Recovery"),Projection);
    if (ReactiveSpotLight->LightFunctionMaterial!=RecoveryProjectionInstance)
        ReactiveSpotLight->SetLightFunctionMaterial(RecoveryProjectionInstance);
    LastRecoveryProjection=Projection;
}

void AMannequinReactiveLight::ApplyBrightness(float Brightness)
{
    const auto* Settings=UProjectProject01GameUserSettings::Get();
    const float Flash=Settings ? FMath::Clamp(Settings->GetLocalFlashScale(),0.f,1.f) : 1.f;
    const float Presented=FMath::Lerp(1.f,FMath::Clamp(Brightness,0.f,1.f),Flash);
    const float Wake=FMath::Clamp(WakeBlendElapsed/.25f,0.f,1.f);
    const float WakeAlpha=Wake*Wake*(3-2*Wake);
    ApplyColdTint(Presented>UE_SMALL_NUMBER && Flash>0.f ? FMath::Lerp(WakeFromColdTint,FaultPattern.ColdTint*Flash,WakeAlpha) : 0.f);
    ApplyRecoveryProjection(Presented,Flash,WakeAlpha);
    const FVector Sections=FMath::Lerp(FVector::OneVector,
        FMath::Lerp(WakeFromSections,FaultPattern.SectionLevels,WakeAlpha),Flash);
    const bool SectionsChanged=!Sections.Equals(LastSectionLevels,.0005);
    // Never let the near-equal cache skip the final transition to a physically invisible light.
    if ((Presented>UE_SMALL_NUMBER)==(LastBrightness>UE_SMALL_NUMBER)
        && FMath::IsNearlyEqual(Presented,LastBrightness,.0005f)&&!SectionsChanged) return;
    LastBrightness=Presented;LastSectionLevels=Sections;
    if (ReactiveSpotLight)
    {
        const float SectionLight=FMath::Lerp(1.f,static_cast<float>((Sections.X+Sections.Y+Sections.Z)/3),.35f);
        ReactiveSpotLight->SetIntensity(FMath::Max(0.f,LightOnIntensity)*Presented*SectionLight);
        ReactiveSpotLight->SetVisibility(Presented>UE_SMALL_NUMBER,false);
    }
    if (BulbMesh && !EmissiveParameterName.IsNone())
    {
        if(SectionsChanged)BulbMesh->SetVectorParameterValueOnMaterials(TEXT("LampSections"),Sections);
        BulbMesh->SetScalarParameterValueOnMaterials(EmissiveParameterName,
            FMath::Lerp(FMath::Max(0.f,EmissiveOffValue),FMath::Max(0.f,EmissiveOnValue),Presented));
    }
}

void AMannequinReactiveLight::ApplyColdTint(float Tint)
{
    if (Tint<.002f) Tint=0;
    if (Tint==LastColdTint) return;
    static const FName BulbColorParameter(TEXT("Color"));
    if (LastColdTint==0)
    {
        BaseSpotColor=ReactiveSpotLight ? ReactiveSpotLight->GetLightColor() : FLinearColor::White;
        auto* Material=BulbMesh ? BulbMesh->GetMaterial(0) : nullptr;
        if (Material && Material->GetVectorParameterValue(FMaterialParameterInfo(BulbColorParameter),BaseBulbColor))
            ColdBulbInstance=BulbMesh->CreateDynamicMaterialInstance(0);
    }
    const FLinearColor Multiplier=FMath::Lerp(FLinearColor::White,FLinearColor(.79f,.91f,1.f),Tint);
    if (ReactiveSpotLight) ReactiveSpotLight->SetLightColor(BaseSpotColor*Multiplier);
    if (ColdBulbInstance && BulbMesh->GetMaterial(0)==ColdBulbInstance.Get())
        ColdBulbInstance->SetVectorParameterValue(BulbColorParameter,BaseBulbColor*Multiplier);
    if (Tint==0) ColdBulbInstance=nullptr;
    LastColdTint=Tint;
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
    TSet<int32> Kinds; int32 LastSerial=0,LastKind=INDEX_NONE; float Previous=1; bool Varied=false,SectionsDiffer=false;
    for (int32 I=0;I<18000;++I)
    {
        A.Step(1.f/30.f,9,1.f,0); B.Step(1.f/30.f,9,1.f,0);
        if (A.Brightness!=B.Brightness||A.SectionLevels!=B.SectionLevels) { AddError(TEXT("A fixed seed must reproduce brightness and partial discharges")); return false; }
        if(A.SectionLevels.ContainsNaN()||A.SectionLevels.GetMin()<0||A.SectionLevels.GetMax()>1)
        {AddError(TEXT("Partial emission must remain finite and bounded"));return false;}
        SectionsDiffer|=A.SectionLevels.GetMax()-A.SectionLevels.GetMin()>.15;
        if (!FMath::IsFinite(A.Brightness)||A.Brightness<0||A.Brightness>1) { AddError(TEXT("Faults must stay between complete darkness and normal light")); return false; }
        Varied |= FMath::Abs(A.Brightness-Previous)>.0001f; Previous=A.Brightness;
        if (A.Serial!=LastSerial)
        {
            if (A.LastKind==LastKind) { AddError(TEXT("The same fault must not repeat immediately")); return false; }
            Kinds.Add(A.LastKind); LastKind=A.LastKind; LastSerial=A.Serial;
        }
    }
    TestTrue(TEXT("Multiple fault families occur, not one repeated blink"),Kinds.Num()==5&&Varied);
    TestTrue(TEXT("Faults affect different areas of the diffuser rather than uniformly dimming it"),SectionsDiffer);
    AddInfo(FString::Printf(TEXT("High-threat failure clusters in 600 seconds: %d"),A.Serial));
    TestTrue(TEXT("More frequent faults still have rests rather than constant strobing"),A.Serial>140&&A.Serial<260);
    float Durations[5]={};
    for (int32 Kind=0;Kind<5;++Kind)
    {
        ProjectReactiveLight::FFaultPattern P; P.Reset(441+Kind);
        for (int32 Attempt=0;Attempt<100&&P.LastKind!=Kind;++Attempt) P.BeginEvent(3);
        if (!TestEqual(TEXT("Sample the requested fault family"),P.LastKind,Kind)) return false;
        Durations[Kind]=P.Keys.Last().Time; P.Strength=.15f;
        TestTrue(TEXT("Every fault family starts its failure within 0.1 second"),P.Keys[1].Time<=.1f);
        bool HasResidualHold=false;
        for(int32 K=1;K<P.Keys.Num();++K)
            HasResidualHold|=P.Keys[K].Value>0&&P.Keys[K].Value<.06f
                &&P.Keys[K].Value==P.Keys[K-1].Value&&P.Keys[K].Time-P.Keys[K-1].Time>.1f;
        TestEqual(TEXT("Only voltage collapse and incomplete restoration hold a faint afterglow"),HasResidualHold,Kind==0||Kind==4);
        float Min=1;int32 OffFrames=0,GlowFrames=0,SectionFrames=0;
        for (int32 Frame=0;Frame<FMath::CeilToInt(Durations[Kind]*30);++Frame)
        {
            P.Step(1.f/30.f,3,.15f,0);
            Min=FMath::Min(Min,P.Brightness);
            if(P.Brightness==0)++OffFrames;
            if(P.Brightness>0&&P.SectionLevels.GetMax()-P.SectionLevels.GetMin()>.15)++SectionFrames;
            for(int32 K=1;K<P.Keys.Num();++K)
            {
                const auto& Key=P.Keys[K];
                const auto& PreviousKey=P.Keys[K-1];
                if(P.Elapsed>=PreviousKey.Time&&P.Elapsed<=Key.Time&&Key.Value==PreviousKey.Value
                    &&Key.Value>0.f&&Key.Value<.06f&&P.Brightness>0.f&&P.Brightness<.003f)
                    ++GlowFrames;
            }
        }
        TestTrue(TEXT("Every family fully extinguishes even at first-tier influence, for multiple frames"),Min==0&&OffFrames>=2);
        if(Kind==0||Kind==4)TestTrue(TEXT("Afterglow holds below 0.3 percent of normal output over multiple frames"),GlowFrames>=4);
        TestTrue(TEXT("Every fault family can show a visible partial diffuser discharge"),SectionFrames>0);
    }
    TestTrue(TEXT("Single interruption, separated double interruption and voltage collapse remain distinct"),Durations[1]<Durations[2]&&Durations[2]<Durations[0]);
    TestTrue(TEXT("Incomplete restoration holds its dark pocket longer than a voltage sag"),Durations[4]>Durations[0]);
    AddInfo(FString::Printf(TEXT("Failure durations sag=%.2f single=%.2f double=%.2f restart=%.2f lingering=%.2f"),Durations[0],Durations[1],Durations[2],Durations[3],Durations[4]));
    const float Before=A.Strength; A.Step(1.f/30.f,0,0,.18f);
    TestTrue(TEXT("Leaving the range decays instead of cutting abruptly"),A.Strength>0&&A.Strength<Before);
    for (int32 I=0;I<1200;++I) A.Step(1.f/30.f,0,0,.18f);
    TestEqual(TEXT("Lingering faults return completely to steady light"),A.Brightness,1.f);
    ProjectReactiveLight::FFaultPattern Quiet; Quiet.Reset(14);
    for (int32 I=0;I<3000;++I) Quiet.Step(1.f/30.f,2,0,.18f);
    TestTrue(TEXT("Sub-threshold groups never create an event"),Quiet.Serial==0&&Quiet.Brightness==1);
    TestEqual(TEXT("No threat means no independent shader animation or dark diffuser sections"),Quiet.SectionLevels,FVector::OneVector);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReactiveLightOverlapTest,
    "ProjectProject01.Lighting.MannequinFaultOverlap",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReactiveLightOverlapTest::RunTest(const FString&)
{
    ProjectReactiveLight::FSharedFaultSurges Shared,Replay,Quiet;
    Shared.Reset(1557);Replay.Reset(1557);Quiet.Reset(1557);
    ProjectReactiveLight::FFaultPattern Lamps[3],Far;
    for(int32 I=0;I<3;++I)Lamps[I].Reset(782+I*317);
    Far.Reset(337);
    int32 SingleFrames=0,PairFrames=0,TripleFrames=0,DifferentFrames=0;
    for(int32 Frame=0;Frame<3600;++Frame)
    {
        const double Now=Frame/30.;
        if(Frame%6==0)
        {
            Shared.Step(Now,3);Replay.Step(Now,3);Quiet.Step(Now,2);
            if(Shared.Serial!=Replay.Serial||Shared.NextAt!=Replay.NextAt)
            {AddError(TEXT("Shared disturbances must reproduce from a fixed seed"));return false;}
            for(auto& Lamp:Lamps)
            {
                Lamp.QueueSharedSurge(Shared.Serial,static_cast<float>(Now-Shared.StartedAt),3,1);
                const float Rest=Lamp.Rest;
                if(Lamp.QueueSharedSurge(Shared.Serial,0,3,1)||Lamp.Rest!=Rest)
                {AddError(TEXT("A shared disturbance may only schedule a fixture once"));return false;}
            }
            Far.QueueSharedSurge(Shared.Serial,0,3,0);
        }
        int32 Active=0;
        for(auto& Lamp:Lamps){Lamp.Step(1.f/30,3,1,0);Active+=!Lamp.Keys.IsEmpty();}
        Far.Step(1.f/30,3,0,0);
        SingleFrames+=Active==1;PairFrames+=Active==2;TripleFrames+=Active==3;
        DifferentFrames+=Active>=2&&(!FMath::IsNearlyEqual(Lamps[0].Brightness,Lamps[1].Brightness,.01f)
            ||!FMath::IsNearlyEqual(Lamps[1].Brightness,Lamps[2].Brightness,.01f));
    }
    TestTrue(TEXT("One, two and three fixtures all occur, instead of serial or permanently synchronised faults"),
        SingleFrames>0&&PairFrames>30&&TripleFrames>30&&DifferentFrames>100);
    TestTrue(TEXT("Distant fixtures and groups under three are not pulled into a shared fault"),Far.Serial==0&&Quiet.Serial==0);
    ProjectReactiveLight::FFaultPattern Late;Late.Reset(88);
    const float OldRest=Late.Rest;
    TestTrue(TEXT("An expired disturbance is not replayed after entering range or restoring a checkpoint"),
        !Late.QueueSharedSurge(10,2,3,1)&&Late.Rest==OldRest);
    AddInfo(FString::Printf(TEXT("120s overlap frames single=%d pair=%d triple=%d divergent=%d"),SingleFrames,PairFrames,TripleFrames,DifferentFrames));
    return true;
}
#endif

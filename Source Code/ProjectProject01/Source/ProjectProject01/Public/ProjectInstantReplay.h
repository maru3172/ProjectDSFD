#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectInstantReplay.generated.h"

USTRUCT()
struct FProjectReplayBody
{
    GENERATED_BODY()
    UPROPERTY() uint32 Key=0;
    UPROPERTY() TSoftObjectPtr<class USkeletalMesh> Mesh;
    UPROPERTY() TArray<TSoftObjectPtr<class UMaterialInterface>> Materials;
};
USTRUCT()
struct FProjectReplayPose
{
    GENERATED_BODY()
    UPROPERTY() uint32 Key=0;
    UPROPERTY() FTransform Transform;
    UPROPERTY() TArray<FTransform> Bones;
};
USTRUCT()
struct FProjectReplayFrame
{
    GENERATED_BODY()
    UPROPERTY() double Seconds=0;
    UPROPERTY() FVector CameraLocation=FVector::ZeroVector;
    UPROPERTY() FRotator CameraRotation=FRotator::ZeroRotator;
    UPROPERTY() float Fov=90;
    UPROPERTY() TArray<FProjectReplayPose> Poses;
};
USTRUCT()
struct FProjectReplayScenery
{
    GENERATED_BODY()
    UPROPERTY() TSoftObjectPtr<class UStaticMesh> Mesh;
    UPROPERTY() TArray<TSoftObjectPtr<class UMaterialInterface>> Materials;
    UPROPERTY() FTransform Transform;
};

/** Bounded, memory-only visual clip. Never written to disk or restored into gameplay. */
UCLASS()
class PROJECTPROJECT01_API UProjectDeathCameraClip final : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 Version=1;
    UPROPERTY() FString Protocol;
    UPROPERTY() FString Map;
    UPROPERTY() FString Caption;
    UPROPERTY() FDateTime RecordedAt;
    UPROPERTY() uint32 Focus=0;
    UPROPERTY() TArray<FProjectReplayBody> Bodies;
    UPROPERTY() TArray<FProjectReplayFrame> Frames;
    UPROPERTY() TArray<FProjectReplayScenery> Scenery;
    bool IsValidClip() const;
};

/** Dead clients replay buffered poses in an isolated, non-physical preview scene. */
UCLASS()
class PROJECTPROJECT01_API UProjectInstantReplay final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
    friend class FProjectReplayContractTest;
public:
    UProjectInstantReplay(const FObjectInitializer& ObjectInitializer);
    UProjectInstantReplay(FVTableHelper& Helper);
    virtual ~UProjectInstantReplay() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void QueueDeathCamera(AActor* Victim,const FString& Reason);
    void CancelPresentation();
    const FString& GetStatus()const{return Status;}
    bool IsPresenting()const{return bPresenting;}
    bool IsDeathCameraPending()const{return bDeathQueued||bPresenting;}
    FSimpleMulticastDelegate OnChanged;
private:
    struct FPreview;
    bool Tick(float Delta);
    void CaptureFrame();
    void AppendFrame(FProjectReplayFrame Frame);
    void FinishPendingMoment();
    UProjectDeathCameraClip* MakeClip(uint32 Focus,const FString& Caption,double At);
    void StartPresentation(UProjectDeathCameraClip* Clip,APlayerController* Owner);
    void FinishPresentation();
    void RenderClip();
    void ClearMatch();
    void SessionCleared();
    TUniquePtr<FPreview> Preview;
    FTSTicker::FDelegateHandle Ticker;
    TArray<FProjectReplayFrame> Ring;
    int64 RingBytes=0;
    TMap<uint32,FProjectReplayBody> Bodies;
    UPROPERTY(Transient) TObjectPtr<UProjectDeathCameraClip> Death;
    UPROPERTY(Transient) TObjectPtr<UProjectDeathCameraClip> Current;
    TWeakObjectPtr<UWorld> MatchWorld,PresentationWorld;
    TWeakObjectPtr<APlayerController> PresentationOwner;
    FString MatchId,Status,PendingCaption;
    uint32 PendingFocus=0;
    double NextCaptureAt=0,PendingMomentAt=0,DeathReadyAt=0,PlaybackStartedAt=0,LastRenderedAt=0;
    bool bMomentPending=false,bDeathQueued=false,bPresenting=false;
};

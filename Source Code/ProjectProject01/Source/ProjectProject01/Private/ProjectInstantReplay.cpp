#include "ProjectInstantReplay.h"
#include "MultiplayTestPlayerController.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01VersionContract.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "PreviewScene.h"
#include "RHI.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    constexpr int32 MaximumFrames=60,MaximumBodies=24,MaximumBones=256,MaximumScenery=400;
    constexpr double CaptureInterval=.1;
    constexpr int64 MaximumRingBytes=16LL*1024*1024;
    int64 FrameBytes(const FProjectReplayFrame& F){int64 Bytes=sizeof(F)+F.Poses.GetAllocatedSize();for(const auto& P:F.Poses)Bytes+=P.Bones.GetAllocatedSize();return Bytes;}
    bool Finite(const FTransform& T){return !T.ContainsNaN()&&T.IsValid();}
    bool IsBody(const AActor* A){return IsValid(A)&& (A->IsA<APlayerCharacter>()||A->IsA<AMannequinAICharacter>());}
}
bool UProjectDeathCameraClip::IsValidClip()const
{
    if(Version!=1||Frames.Num()<3||Frames.Num()>MaximumFrames||Bodies.IsEmpty()||Bodies.Num()>MaximumBodies||Scenery.Num()>MaximumScenery||Focus==0)return false;
    TSet<uint32> Keys;for(const auto& B:Bodies){if(B.Key==0||Keys.Contains(B.Key)||B.Mesh.IsNull()||B.Materials.Num()>32)return false;Keys.Add(B.Key);}
    bool FoundFocus=false;
    double Previous=-DBL_MAX;
    for(const auto& F:Frames)
    {
        if(!FMath::IsFinite(F.Seconds)||F.Seconds<Previous||F.CameraLocation.ContainsNaN()||F.CameraRotation.ContainsNaN()||!FMath::IsFinite(F.Fov)||F.Fov<30||F.Fov>150||F.Poses.Num()>MaximumBodies)return false;
        Previous=F.Seconds;
        TSet<uint32> FrameKeys;
        for(const auto& P:F.Poses)
        {if(!Keys.Contains(P.Key)||FrameKeys.Contains(P.Key)||!Finite(P.Transform)||P.Bones.Num()>MaximumBones)return false;FrameKeys.Add(P.Key);FoundFocus|=P.Key==Focus;for(const auto& B:P.Bones)if(!Finite(B))return false;}
    }
    return FoundFocus&&Frames.Last().Seconds>Frames[0].Seconds&&Frames.Last().Seconds-Frames[0].Seconds<=9;
}

struct UProjectInstantReplay::FPreview
{
    // Reuse one scene per game instance. It has no physics, AI, audio, game instance or server connection.
    FPreviewScene Scene{FPreviewScene::ConstructionValues().SetEditor(false).SetCreatePhysicsScene(false).AllowAudioPlayback(false).SetForceMipsResident(false).SetTransactional(false).SetSkyBrightness(.08f).SetLightBrightness(1.4f)};
    TMap<uint32,UPoseableMeshComponent*> Proxies;
    TArray<UActorComponent*> ClipComponents;
    USceneCaptureComponent2D* Camera=nullptr;
    UTextureRenderTarget2D* Target=nullptr;
    FSlateBrush Brush;
    TSharedPtr<SWidget> Overlay;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    bool bPreviousWorldRenderingDisabled=false;
    bool bPreviousCursor=false;
    FVector FocusOffset=FVector(-240,160,150);
    FPreview()
    {
        Target=NewObject<UTextureRenderTarget2D>();Target->RenderTargetFormat=RTF_RGBA8;Target->ClearColor=FLinearColor::Black;Target->InitAutoFormat(1280,720);
        Camera=NewObject<USceneCaptureComponent2D>();Camera->TextureTarget=Target;Camera->bCaptureEveryFrame=false;Camera->bCaptureOnMovement=false;Camera->CaptureSource=SCS_FinalColorLDR;
        Camera->ShowFlags.SetMotionBlur(false);Camera->ShowFlags.SetDynamicShadows(false);
        Scene.AddComponent(Camera,FTransform::Identity);
        Brush.SetResourceObject(Target);Brush.ImageSize=FVector2D(1280,720);
    }
    void Clear(bool RestoreViewport=true)
    {
        if(auto* V=Viewport.Get())
        {if(Overlay)V->RemoveViewportWidgetContent(Overlay.ToSharedRef());if(RestoreViewport)V->bDisableWorldRendering=bPreviousWorldRenderingDisabled;}
        Viewport.Reset();Overlay.Reset();
        for(auto* Component:ClipComponents)Scene.RemoveComponent(Component);
        ClipComponents.Reset();Proxies.Reset();
    }
    ~FPreview(){Clear();}
};
UProjectInstantReplay::UProjectInstantReplay(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer){}
UProjectInstantReplay::UProjectInstantReplay(FVTableHelper& Helper):Super(Helper){}
UProjectInstantReplay::~UProjectInstantReplay()=default;
bool UProjectInstantReplay::ShouldCreateSubsystem(UObject* Outer)const{return !IsRunningDedicatedServer()&&Super::ShouldCreateSubsystem(Outer);}
void UProjectInstantReplay::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);Collection.InitializeDependency<UProjectProject01AuthSubsystem>();
    GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>()->OnLocalSessionCleared.AddUObject(this,&ThisClass::SessionCleared);
    FCoreUObjectDelegates::PreLoadMapWithContext.AddWeakLambda(this,[this](const FWorldContext& Context,const FString&)
    {
        if(Context.OwningGameInstance==GetGameInstance()){CancelPresentation();ClearMatch();}
    });
    Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::Tick));
}
void UProjectInstantReplay::Deinitialize()
{
    FCoreUObjectDelegates::PreLoadMapWithContext.RemoveAll(this);
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    if(auto* Auth=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>())Auth->OnLocalSessionCleared.RemoveAll(this);
    CancelPresentation();ClearMatch();Preview.Reset();Super::Deinitialize();
}
void UProjectInstantReplay::SessionCleared(){CancelPresentation();ClearMatch();}
void UProjectInstantReplay::ClearMatch()
{
    Ring.Reset();RingBytes=0;Bodies.Reset();Death=nullptr;MatchId.Reset();MatchWorld.Reset();
    PendingFocus=0;bMomentPending=bDeathQueued=false;NextCaptureAt=0;
}
bool UProjectInstantReplay::Tick(float)
{
    const double Now=FPlatformTime::Seconds();auto* World=GetWorld();
    if(bPresenting)
    {
        if(!World||World!=PresentationWorld.Get()||!PresentationOwner.IsValid()){CancelPresentation();return true;}
        if(Now-LastRenderedAt>=1./30){LastRenderedAt=Now;RenderClip();}
        if(Current&&Now-PlaybackStartedAt>=Current->Frames.Last().Seconds-Current->Frames[0].Seconds+.3)FinishPresentation();
    }
    auto* PC=Cast<AMultiplayTestPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    if(!World||World->IsPlayingReplay()||!PC||PC->IsServerControlledBot())
    {if(MatchWorld.IsValid()&&MatchWorld.Get()!=World){CancelPresentation();ClearMatch();}return true;}
    const FString NewMatch=PC->GetAuthenticatedMatchId();FGuid Id;
    if(!FGuid::Parse(NewMatch,Id)||!Id.IsValid())return true;
    if(NewMatch!=MatchId||MatchWorld.Get()!=World)
    {
        CancelPresentation();ClearMatch();MatchId=NewMatch;MatchWorld=World;
    }
    if(PC->GetMatchPhase()==EMultiplayTestMatchPhase::InProgress&&Now>=NextCaptureAt)
    {NextCaptureAt=Now+CaptureInterval;CaptureFrame();}
    if(bMomentPending&&Now>=PendingMomentAt+1.5)FinishPendingMoment();
    if(bDeathQueued&&!bPresenting&&Now>=DeathReadyAt)
    {
        bDeathQueued=false;
        if(Death&&Death->IsValidClip())StartPresentation(Death,PC);
    }
    return true;
}
void UProjectInstantReplay::CaptureFrame()
{
    auto* World=GetWorld();auto* PC=GetGameInstance()->GetFirstLocalPlayerController();if(!World||!PC||!PC->PlayerCameraManager)return;
    FProjectReplayFrame Frame;Frame.Seconds=FPlatformTime::Seconds();
    const auto& View=PC->PlayerCameraManager->GetCameraCacheView();Frame.CameraLocation=View.Location;Frame.CameraRotation=View.Rotation;Frame.Fov=FMath::Clamp(View.FOV,30.f,150.f);
    for(TActorIterator<ACharacter> It(World);It&&Frame.Poses.Num()<MaximumBodies;++It)
    {
        if(!IsBody(*It)||It->IsHidden())continue;auto* Mesh=It->GetMesh();if(!Mesh||!Mesh->GetSkeletalMeshAsset())continue;
        const auto& Bones=Mesh->GetBoneSpaceTransforms();if(Bones.Num()==0||Bones.Num()>MaximumBones)continue;
        const uint32 Key=It->GetUniqueID();
        if(!Bodies.Contains(Key))
        {
            if(Bodies.Num()>=MaximumBodies)continue;
            FProjectReplayBody Body;Body.Key=Key;Body.Mesh=Mesh->GetSkeletalMeshAsset();
            for(auto* Material:Mesh->GetMaterials())Body.Materials.Add(Material);Bodies.Add(Key,MoveTemp(Body));
        }
        auto& Pose=Frame.Poses.AddDefaulted_GetRef();Pose.Key=Key;Pose.Transform=Mesh->GetComponentTransform();Pose.Bones=Bones;
    }
    AppendFrame(MoveTemp(Frame));
}
void UProjectInstantReplay::AppendFrame(FProjectReplayFrame Frame)
{
    RingBytes+=FrameBytes(Frame);Ring.Add(MoveTemp(Frame));
    while(Ring.Num()>MaximumFrames||(RingBytes>MaximumRingBytes&&Ring.Num()>3))
    {RingBytes-=FrameBytes(Ring[0]);Ring.RemoveAt(0,1,EAllowShrinking::No);}
}
void UProjectInstantReplay::QueueDeathCamera(AActor* Focus,const FString& Caption)
{
    if(!Focus||Focus->GetWorld()!=GetWorld()||!IsBody(Focus))return;
    auto* PC=Cast<AMultiplayTestPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());if(!PC||PC->GetMatchPhase()!=EMultiplayTestMatchPhase::InProgress)return;
    if(PC->GetPawn()!=Focus||PC->GetAuthenticatedRole()!=TEXT("Survivor"))return;
    if(MatchId!=PC->GetAuthenticatedMatchId()||MatchWorld.Get()!=GetWorld())
    {ClearMatch();MatchId=PC->GetAuthenticatedMatchId();MatchWorld=GetWorld();}
    // Only the server's fatal event can queue this owner's death camera.
    if(Ring.IsEmpty())CaptureFrame();
    PendingFocus=Focus->GetUniqueID();PendingCaption=Caption.Left(180);bMomentPending=true;PendingMomentAt=FPlatformTime::Seconds();
    Death=MakeClip(PendingFocus,TEXT("데스캠 · ")+PendingCaption,PendingMomentAt);bDeathQueued=Death!=nullptr;
    const auto* Victim=Cast<APlayerCharacter>(PC->GetPawn());DeathReadyAt=FPlatformTime::Seconds()+(Victim?Victim->GetDeathSequenceHandoffDelaySeconds():3.);
}
UProjectDeathCameraClip* UProjectInstantReplay::MakeClip(uint32 Focus,const FString& Caption,double At)
{
    if(Ring.Num()<3)return nullptr;
    auto* Clip=NewObject<UProjectDeathCameraClip>(this);Clip->Protocol=FProjectProject01VersionContract::GetNetworkProtocolVersion();Clip->Map=GetWorld()->GetMapName();Clip->Caption=Caption;Clip->Focus=Focus;Clip->RecordedAt=FDateTime::UtcNow();
    for(const auto& F:Ring)if(F.Seconds>=At-2.5&&F.Seconds<=At+1.5)Clip->Frames.Add(F);
    Bodies.GenerateValueArray(Clip->Bodies);
    FVector Center=Clip->Frames.IsEmpty()?FVector::ZeroVector:Clip->Frames.Last().CameraLocation;
    if(Clip->Frames.Num()>0)if(const auto* P=Clip->Frames.Last().Poses.FindByPredicate([Focus](const auto& P){return P.Key==Focus;}))Center=P->Transform.GetLocation();
    for(TActorIterator<AActor> It(GetWorld());It&&Clip->Scenery.Num()<MaximumScenery;++It)
    {
        if(IsBody(*It)||It->IsHidden())continue;
        TInlineComponentArray<UStaticMeshComponent*> Meshes;It->GetComponents(Meshes);
        for(auto* Mesh:Meshes)
        {
            if(!Mesh->IsVisible()||!Mesh->GetStaticMesh()||Mesh->Bounds.GetBox().ComputeSquaredDistanceToPoint(Center)>FMath::Square(4500.))continue;
            auto Add=[&](const FTransform& Transform)
            {
                if(Clip->Scenery.Num()>=MaximumScenery)return;
                auto& S=Clip->Scenery.AddDefaulted_GetRef();S.Mesh=Mesh->GetStaticMesh();S.Transform=Transform;for(auto* M:Mesh->GetMaterials())S.Materials.Add(M);
            };
            if(auto* Instances=Cast<UInstancedStaticMeshComponent>(Mesh))
            {for(int32 I=0;I<Instances->GetInstanceCount()&&Clip->Scenery.Num()<MaximumScenery;++I){FTransform T;if(Instances->GetInstanceTransform(I,T,true)&&FVector::DistSquared(T.GetLocation(),Center)<FMath::Square(4500.))Add(T);}}
            else Add(Mesh->GetComponentTransform());
        }
    }
    return Clip->IsValidClip()?Clip:nullptr;
}
void UProjectInstantReplay::FinishPendingMoment()
{
    if(!bMomentPending)return;
    if(auto* Clip=MakeClip(PendingFocus,TEXT("데스캠 · ")+PendingCaption,PendingMomentAt)){Death=Clip;bDeathQueued=true;}
    bMomentPending=false;
}
void UProjectInstantReplay::StartPresentation(UProjectDeathCameraClip* Clip,APlayerController* Owner)
{
    if(GUsingNullRHI||!Clip||!Clip->IsValidClip()||!Owner)return;auto* V=GetGameInstance()->GetGameViewportClient();if(!V)return;
    if(!Preview)Preview=MakeUnique<FPreview>();Preview->Clear();
    for(const auto& S:Clip->Scenery)
    {
        auto* Mesh=S.Mesh.Get();if(!Mesh)continue;auto* Proxy=NewObject<UStaticMeshComponent>();Proxy->SetStaticMesh(Mesh);Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);Proxy->SetCastShadow(false);
        for(int32 I=0;I<S.Materials.Num();++I)if(auto* M=S.Materials[I].Get())Proxy->SetMaterial(I,M);
        Preview->Scene.AddComponent(Proxy,S.Transform);Preview->ClipComponents.Add(Proxy);
    }
    for(const auto& B:Clip->Bodies)
    {
        auto* Mesh=B.Mesh.Get();if(!Mesh)continue;auto* Proxy=NewObject<UPoseableMeshComponent>();Proxy->SetSkinnedAssetAndUpdate(Mesh);Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);Proxy->SetCastShadow(false);
        for(int32 I=0;I<B.Materials.Num();++I)if(auto* M=B.Materials[I].Get())Proxy->SetMaterial(I,M);
        Preview->Scene.AddComponent(Proxy,FTransform::Identity);Preview->ClipComponents.Add(Proxy);Preview->Proxies.Add(B.Key,Proxy);
    }
    if(Preview->Proxies.IsEmpty())return;
    if(const auto* const* Focus=Preview->Proxies.Find(Clip->Focus))
    {
        if(const auto* Pose=Clip->Frames.Last().Poses.FindByPredicate([Key=Clip->Focus](const auto& P){return P.Key==Key;}))
        {
            const FVector Center=Pose->Transform.GetLocation()+FVector(0,0,100);FHitResult Hit;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(DeathCameraPosition),false);
            for(TActorIterator<ACharacter> It(GetWorld());It;++It)Query.AddIgnoredActor(*It);
            if(GetWorld()->LineTraceSingleByChannel(Hit,Center,Center+FVector(-240,160,150),ECC_Visibility,Query))
                Preview->FocusOffset=Hit.Location+Hit.Normal*15-Center;
            else Preview->FocusOffset=FVector(-240,160,150);
        }
    }
    Current=Clip;PresentationOwner=Owner;PresentationWorld=GetWorld();PlaybackStartedAt=FPlatformTime::Seconds();bPresenting=true;
    Preview->Viewport=V;Preview->bPreviousWorldRenderingDisabled=V->bDisableWorldRendering;V->bDisableWorldRendering=true;
    Preview->bPreviousCursor=Owner->bShowMouseCursor;
    Preview->Overlay=SNew(SOverlay)
      +SOverlay::Slot()[SNew(SBorder).BorderBackgroundColor(FLinearColor::Black).Padding(0)
        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(&Preview->Brush)]]]
      +SOverlay::Slot().VAlign(VAlign_Top).Padding(30)[SNew(STextBlock).Text(FText::FromString(Clip->Caption)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),24)).ColorAndOpacity(ProjectDFSDUI::Text())]
      +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(30)[SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).OnClicked_Lambda([Weak=TWeakObjectPtr<UProjectInstantReplay>(this)]{if(auto* S=Weak.Get())S->FinishPresentation();return FReply::Handled();})
        [SNew(STextBlock).Text(FText::FromString(TEXT("건너뛰기"))).ColorAndOpacity(ProjectDFSDUI::Text())]];
    V->AddViewportWidgetContent(Preview->Overlay.ToSharedRef(),1000003);
    Owner->bShowMouseCursor=true;Owner->SetInputMode(FInputModeUIOnly());RenderClip();OnChanged.Broadcast();
}
void UProjectInstantReplay::RenderClip()
{
    if(!Current||!Preview||Current->Frames.Num()<2)return;
    const double Time=Current->Frames[0].Seconds+FPlatformTime::Seconds()-PlaybackStartedAt;
    int32 Index=0;while(Index+1<Current->Frames.Num()&&Current->Frames[Index+1].Seconds<Time)++Index;
    const auto& A=Current->Frames[Index];const auto& B=Current->Frames[FMath::Min(Index+1,Current->Frames.Num()-1)];
    const float Alpha=B.Seconds>A.Seconds?FMath::Clamp(float((Time-A.Seconds)/(B.Seconds-A.Seconds)),0.f,1.f):0;
    for(auto& Pair:Preview->Proxies)
    {
        auto* Proxy=Pair.Value;const auto* P=A.Poses.FindByPredicate([Key=Pair.Key](const auto& P){return P.Key==Key;});const auto* Q=B.Poses.FindByPredicate([Key=Pair.Key](const auto& P){return P.Key==Key;});
        Proxy->SetVisibility(P!=nullptr);if(!P)continue;
        FTransform T=P->Transform;if(Q)T.Blend(P->Transform,Q->Transform,Alpha);Proxy->SetWorldTransform(T);
        if(Proxy->BoneSpaceTransforms.Num()==P->Bones.Num())
        {
            for(int32 I=0;I<P->Bones.Num();++I){Proxy->BoneSpaceTransforms[I]=P->Bones[I];if(Q&&Q->Bones.Num()==P->Bones.Num())Proxy->BoneSpaceTransforms[I].Blend(P->Bones[I],Q->Bones[I],Alpha);}
            Proxy->MarkRefreshTransformDirty();Proxy->RefreshBoneTransforms();
        }
    }
    FVector Location=FMath::Lerp(A.CameraLocation,B.CameraLocation,Alpha);FRotator Rotation=FQuat::Slerp(A.CameraRotation.Quaternion(),B.CameraRotation.Quaternion(),Alpha).Rotator();
    // A close, fixed-side replay camera makes an attacker visible even when they came from behind.
    if(auto* const* Focus=Preview->Proxies.Find(Current->Focus))
    {
        const FVector Center=(*Focus)->GetComponentLocation()+FVector(0,0,100);
        Location=Center+Preview->FocusOffset;Rotation=(Center-Location).Rotation();
    }
    Preview->Camera->SetWorldLocationAndRotation(Location,Rotation);Preview->Camera->FOVAngle=FMath::Lerp(A.Fov,B.Fov,Alpha);Preview->Camera->CaptureScene();
}
void UProjectInstantReplay::FinishPresentation()
{
    auto* Owner=PresentationOwner.Get();auto* World=PresentationWorld.Get();const bool PreviousCursor=Preview&&Preview->bPreviousCursor;
    CancelPresentation();
    if(Owner&&World==GetWorld())
    {
        auto* PC=Cast<AMultiplayTestPlayerController>(Owner);
        const bool Results=PC&&(PC->GetMatchPhase()==EMultiplayTestMatchPhase::Ending||PC->GetMatchPhase()==EMultiplayTestMatchPhase::Results);
        // The controller owns the live spectator/result fades. Do not reveal the
        // world here: a reliable final result can precede phase replication.
        if(!Results){Owner->bShowMouseCursor=PreviousCursor;if(PreviousCursor)Owner->SetInputMode(FInputModeGameAndUI());else Owner->SetInputMode(FInputModeGameOnly());}
    }
}
void UProjectInstantReplay::CancelPresentation()
{
    if(Preview)Preview->Clear(PresentationWorld.Get()==GetWorld());Current=nullptr;PresentationOwner.Reset();PresentationWorld.Reset();bPresenting=false;bDeathQueued=false;bMomentPending=false;
}

void AMultiplayTestPlayerController::ClientQueueDeathCamera_Implementation(APlayerCharacter* Victim,const FString& Reason)
{
    if(!IsLocalController()||!Victim||Victim!=GetPawn()||GetAuthenticatedRole()!=TEXT("Survivor"))return;
    if(auto* GI=GetGameInstance())if(auto* Replay=GI->GetSubsystem<UProjectInstantReplay>())Replay->QueueDeathCamera(Victim,Reason);
}

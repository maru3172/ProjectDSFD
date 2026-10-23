#include "ProjectProject01CheckpointPreview.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/App.h"
#include "Misc/Crc.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Containers/Ticker.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"
#include "ProjectProject01DiagnosticsTypes.h"
#include <atomic>

namespace
{
    struct FPreviewCaptureTiming { double SetupMs=0, RenderReadbackMs=0, EncodeMs=0; };

    // Spawns a transient, one-shot scene capture matching the player's current camera. Capture is not issued yet.
    ASceneCapture2D* SpawnPreviewCapture(UWorld* World, APlayerController* Player, UTextureRenderTarget2D*& OutTarget)
    {
        using namespace ProjectCheckpointPreview;
        OutTarget=nullptr;
        if (!FApp::CanEverRender() || !IsValid(World) || !IsValid(Player) || !IsValid(Player->PlayerCameraManager)) return nullptr;
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags |= RF_Transient;
        ASceneCapture2D* Actor=World->SpawnActor<ASceneCapture2D>(Spawn);
        if (!IsValid(Actor)) return nullptr;
        USceneCaptureComponent2D* Capture=Actor->GetCaptureComponent2D();
        Capture->bCaptureEveryFrame=false;
        Capture->bCaptureOnMovement=false;
        UTextureRenderTarget2D* Target=NewObject<UTextureRenderTarget2D>(Actor);
        Target->ClearColor=FLinearColor::Black;
        Target->InitCustomFormat(Width,Height,PF_B8G8R8A8,false);
        Target->UpdateResourceImmediate(true);
        const FMinimalViewInfo& View=Player->PlayerCameraManager->GetCameraCacheView();
        Actor->SetActorLocationAndRotation(View.Location,View.Rotation);
        Capture->FOVAngle=View.FOV;
        Capture->PostProcessSettings=View.PostProcessSettings;
        Capture->PostProcessBlendWeight=View.PostProcessBlendWeight;
        Capture->CaptureSource=SCS_FinalColorLDR;
        Capture->TextureTarget=Target;
        if (APawn* Pawn=Player->GetPawn()) Capture->HideActorComponents(Pawn);
        OutTarget=Target;
        return Actor;
    }

    // Shared with the render thread. Holds no UObject references (released wherever the last owner drops it).
    struct FPreviewReadbackState
    {
        TUniquePtr<FRHIGPUTextureReadback> Readback;
        TArray<FColor> Pixels;              // Written on the render thread before bReady is published.
        std::atomic<bool> bPollInFlight{false};
        std::atomic<bool> bReady{false};
        std::atomic<bool> bFailed{false};
    };

    // Render thread: copies the finished staging texture into tightly packed opaque pixels.
    void CopyReadback_RenderThread(FPreviewReadbackState& State)
    {
        using namespace ProjectCheckpointPreview;
        int32 RowPitchInPixels=0, BufferHeight=0;
        const FColor* Source=static_cast<const FColor*>(State.Readback->Lock(RowPitchInPixels,&BufferHeight));
        if (Source && RowPitchInPixels>=Width && BufferHeight>=Height)
        {
            State.Pixels.SetNumUninitialized(Width*Height);
            for (int32 Row=0;Row<Height;++Row)
                FMemory::Memcpy(State.Pixels.GetData()+Row*Width,Source+Row*RowPitchInPixels,Width*sizeof(FColor));
            for (FColor& Pixel:State.Pixels) Pixel.A=255;
        }
        else State.bFailed.store(true);
        State.Readback->Unlock();
        State.Readback.Reset();
        State.bReady.store(true,std::memory_order_release);
    }

    // Game-thread side of an async capture. Holds the UObjects, so it is never handed to the render thread.
    struct FPreviewCaptureOwner
    {
        TStrongObjectPtr<UTextureRenderTarget2D> Target;
        TWeakObjectPtr<ASceneCapture2D> Actor;
        ProjectCheckpointPreview::FPixelsReady OnReady;
        double Deadline=0;
    };
    constexpr double AsyncReadbackTimeoutSeconds=5.0;

    // Body of Capture without the standalone-only gate, so the dev benchmark below can time it in PIE worlds.
    // OutPixels set: return raw pixels and leave encoding to the caller (worker thread). Otherwise encode inline.
    void CaptureView(UWorld* World, APlayerController* Player, TArray<uint8>& OutPng, FPreviewCaptureTiming* Timing, TArray<FColor>* OutPixels=nullptr)
    {
    OutPng.Reset();
    if (OutPixels) OutPixels->Reset();
    using namespace ProjectCheckpointPreview;
    const double SetupStart=FPlatformTime::Seconds();
    UTextureRenderTarget2D* Target=nullptr;
    ASceneCapture2D* Actor=SpawnPreviewCapture(World,Player,Target);
    if (!IsValid(Actor)) return;
    USceneCaptureComponent2D* Capture=Actor->GetCaptureComponent2D();
    const double RenderStart=FPlatformTime::Seconds();
    Capture->CaptureScene();
    TArray<FColor> Pixels;
    FTextureRenderTargetResource* Resource=Target->GameThread_GetRenderTargetResource();
    const bool bRead=Resource && Resource->ReadPixels(Pixels) && Pixels.Num()==Width*Height;
    Capture->TextureTarget=nullptr;
    Actor->Destroy();
    const double EncodeStart=FPlatformTime::Seconds();
    if (bRead)
    {
        for (FColor& Pixel:Pixels) Pixel.A=255;
        if (OutPixels) *OutPixels=MoveTemp(Pixels);
        else ProjectCheckpointPreview::EncodePixels(Pixels,OutPng);
    }
    if (Timing)
    {
        const double End=FPlatformTime::Seconds();
        Timing->SetupMs=(RenderStart-SetupStart)*1000.0;Timing->RenderReadbackMs=(EncodeStart-RenderStart)*1000.0;Timing->EncodeMs=(End-EncodeStart)*1000.0;
    }
    }

#if !UE_BUILD_SHIPPING
    // Dev-only measurement: times the checkpoint thumbnail capture from the first local player's camera of each
    // running game/PIE world. Usage: ProjectDFSD.BenchCheckpointPreview [Runs=5]. Writes nothing to disk.
    FAutoConsoleCommandWithWorldAndArgs BenchCheckpointPreviewCommand(
        TEXT("ProjectDFSD.BenchCheckpointPreview"),
        TEXT("Dev only: time the checkpoint preview capture (setup / render+ReadPixels / PNG) in running game worlds."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
        {
            const int32 Runs=FMath::Clamp(Args.Num()>0?FCString::Atoi(*Args[0]):5,1,20);
            for (const FWorldContext& Context:GEngine->GetWorldContexts())
            {
                UWorld* World=Context.World();
                if (!IsValid(World) || (World->WorldType!=EWorldType::PIE && World->WorldType!=EWorldType::Game)) continue;
                APlayerController* Player=World->GetFirstPlayerController();
                if (!IsValid(Player) || !Player->IsLocalController()) continue;
                FPreviewCaptureTiming Sum;double Max=0;int32 Bytes=0;
                for (int32 Run=0;Run<Runs;++Run)
                {
                    FPreviewCaptureTiming T;TArray<uint8> Png;CaptureView(World,Player,Png,&T);
                    const double Total=T.SetupMs+T.RenderReadbackMs+T.EncodeMs;Max=FMath::Max(Max,Total);Bytes=Png.Num();
                    Sum.SetupMs+=T.SetupMs;Sum.RenderReadbackMs+=T.RenderReadbackMs;Sum.EncodeMs+=T.EncodeMs;
                }
                UE_LOG(LogProjectProject01Diagnostics,Display,
                    TEXT("CheckpointPreviewBench world=%s runs=%d avg setup=%.2fms render+readback=%.2fms png=%.2fms total=%.2fms max=%.2fms bytes=%d"),
                    *World->GetName(),Runs,Sum.SetupMs/Runs,Sum.RenderReadbackMs/Runs,Sum.EncodeMs/Runs,
                    (Sum.SetupMs+Sum.RenderReadbackMs+Sum.EncodeMs)/Runs,Max,Bytes);
                // Save path: only the async request runs on the game thread; readback/encode complete later.
                double RequestSum=0,RequestMax=0;
                for (int32 Run=0;Run<Runs;++Run)
                {
                    const double Start=FPlatformTime::Seconds();
                    const FString WorldName=World->GetName();
                    ProjectCheckpointPreview::CapturePixelsAsync(World,Player,[Start,WorldName,Run](TArray<FColor>&& Pixels)
                    {
                        UE_LOG(LogProjectProject01Diagnostics,Display,TEXT("CheckpointPreviewAsyncBench world=%s run=%d pixels=%d completed_after=%.1fms"),
                            *WorldName,Run,Pixels.Num(),(FPlatformTime::Seconds()-Start)*1000.0);
                    });
                    const double Request=(FPlatformTime::Seconds()-Start)*1000.0;
                    RequestSum+=Request;RequestMax=FMath::Max(RequestMax,Request);
                }
                UE_LOG(LogProjectProject01Diagnostics,Display,TEXT("CheckpointPreviewAsyncBench world=%s runs=%d game-thread request avg=%.2fms max=%.2fms"),
                    *World->GetName(),Runs,RequestSum/Runs,RequestMax);
            }
        }));
#endif
}

void ProjectCheckpointPreview::Capture(UWorld* World, APlayerController* Player, TArray<uint8>& OutPng)
{
    OutPng.Reset();
    if (!IsValid(World) || World->GetNetMode()!=NM_Standalone) return;
    CaptureView(World,Player,OutPng,nullptr);
}

bool ProjectCheckpointPreview::CapturePixels(UWorld* World, APlayerController* Player, TArray<FColor>& OutPixels)
{
    OutPixels.Reset();
    if (!IsInGameThread() || !IsValid(World) || World->GetNetMode()!=NM_Standalone) return false;
    TArray<uint8> Unused;
    CaptureView(World,Player,Unused,nullptr,&OutPixels);
    return OutPixels.Num()==Width*Height;
}

bool ProjectCheckpointPreview::CapturePixelsAsync(UWorld* World, APlayerController* Player, FPixelsReady OnReady)
{
    if (!IsInGameThread() || !OnReady || !IsValid(World) || World->GetNetMode()!=NM_Standalone) return false;
    UTextureRenderTarget2D* Target=nullptr;
    ASceneCapture2D* Actor=SpawnPreviewCapture(World,Player,Target);
    if (!IsValid(Actor)) return false;
    FTextureRenderTargetResource* Resource=IsValid(Target) ? Target->GameThread_GetRenderTargetResource() : nullptr;
    if (!Resource) { Actor->Destroy(); return false; }
    // Same image as the synchronous path: the scene is rendered from this frame's camera.
    Actor->GetCaptureComponent2D()->CaptureScene();

    const TSharedRef<FPreviewReadbackState,ESPMode::ThreadSafe> State=MakeShared<FPreviewReadbackState,ESPMode::ThreadSafe>();
    State->Readback=MakeUnique<FRHIGPUTextureReadback>(TEXT("CheckpointPreviewReadback"));
    // Render commands run in order, so this copy runs before the render target resource can be released.
    ENQUEUE_RENDER_COMMAND(ProjectCheckpointPreviewReadbackCopy)([State,Resource](FRHICommandListImmediate& RHICmdList)
    {
        FRHITexture* Texture=Resource->GetRenderTargetTexture();
        if (!Texture)
        {
            State->Readback.Reset();State->bFailed.store(true);State->bReady.store(true,std::memory_order_release);
            return;
        }
        RHICmdList.Transition(FRHITransitionInfo(Texture,ERHIAccess::SRVMask,ERHIAccess::CopySrc));
        State->Readback->EnqueueCopy(RHICmdList,Texture,FResolveRect(0,0,Width,Height));
        RHICmdList.Transition(FRHITransitionInfo(Texture,ERHIAccess::CopySrc,ERHIAccess::SRVMask));
    });

    const TSharedRef<FPreviewCaptureOwner> Owner=MakeShared<FPreviewCaptureOwner>();
    Owner->Target.Reset(Target);Owner->Actor=Actor;Owner->OnReady=MoveTemp(OnReady);
    Owner->Deadline=FPlatformTime::Seconds()+AsyncReadbackTimeoutSeconds;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State,Owner](float)->bool
    {
        const bool bReady=State->bReady.load(std::memory_order_acquire);
        if (!bReady && FPlatformTime::Seconds()<Owner->Deadline)
        {
            // Readback status may only be queried on the render thread; keep at most one poll in flight.
            if (!State->bPollInFlight.exchange(true))
            {
                ENQUEUE_RENDER_COMMAND(ProjectCheckpointPreviewReadbackPoll)([State](FRHICommandListImmediate&)
                {
                    if (!State->bReady.load() && State->Readback && State->Readback->IsReady()) CopyReadback_RenderThread(*State);
                    State->bPollInFlight.store(false);
                });
            }
            return true;
        }
        if (!bReady)
        {
            // Timed out: release the staging resources on the render thread; the save continues without a preview.
            ENQUEUE_RENDER_COMMAND(ProjectCheckpointPreviewReadbackCancel)([State](FRHICommandListImmediate&){ if (!State->bReady.load()) State->Readback.Reset(); });
        }
        if (ASceneCapture2D* CaptureActor=Owner->Actor.Get())
        {
            CaptureActor->GetCaptureComponent2D()->TextureTarget=nullptr;
            CaptureActor->Destroy();
        }
        TArray<FColor> Pixels;
        if (bReady && !State->bFailed.load() && State->Pixels.Num()==Width*Height) Pixels=MoveTemp(State->Pixels);
        FPixelsReady Callback=MoveTemp(Owner->OnReady);
        Owner->Target.Reset();
        Callback(MoveTemp(Pixels));
        return false;
    }));
    return true;
}

bool ProjectCheckpointPreview::PrepareEncoder()
{
    // UE 5.8 FImageUtils::CompressImage only uses an already-loaded ImageWrapper module off the game thread.
    if (!IsInGameThread()) return FModuleManager::Get().IsModuleLoaded(TEXT("ImageWrapper"));
    return FModuleManager::Get().LoadModule(TEXT("ImageWrapper"))!=nullptr;
}

void ProjectCheckpointPreview::EncodePixels(const TArray<FColor>& Pixels, TArray<uint8>& OutPng)
{
    OutPng.Reset();
    if (Pixels.Num()!=Width*Height) return;
    TArray64<uint8> Png;
    FImageUtils::PNGCompressImageArray(Width,Height,Pixels,Png);
    if (Png.Num()>0 && Png.Num()<=MaximumBytes) OutPng.Append(Png.GetData(),static_cast<int32>(Png.Num()));
}

bool ProjectCheckpointPreview::IsValidImage(const TArray<uint8>& Png, uint32 ExpectedCrc)
{
    if (Png.IsEmpty() || Png.Num()>MaximumBytes || FCrc::MemCrc32(Png.GetData(),Png.Num())!=ExpectedCrc) return false;
    IImageWrapperModule& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> Image=Module.CreateImageWrapper(EImageFormat::PNG);
    return Image && Image->SetCompressed(Png.GetData(),Png.Num()) && Image->GetWidth()==Width && Image->GetHeight()==Height;
}

UTexture2D* ProjectCheckpointPreview::Decode(const TArray<uint8>& Png, uint32 ExpectedCrc)
{
    return IsValidImage(Png,ExpectedCrc) ? FImageUtils::ImportBufferAsTexture2D(Png) : nullptr;
}

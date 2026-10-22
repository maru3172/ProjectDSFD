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
#include "ProjectProject01DiagnosticsTypes.h"

namespace
{
    struct FPreviewCaptureTiming { double SetupMs=0, RenderReadbackMs=0, EncodeMs=0; };

    // Body of Capture without the standalone-only gate, so the dev benchmark below can time it in PIE worlds.
    void CaptureView(UWorld* World, APlayerController* Player, TArray<uint8>& OutPng, FPreviewCaptureTiming* Timing)
    {
    OutPng.Reset();
    if (!FApp::CanEverRender() || !IsValid(World) || !IsValid(Player) || !IsValid(Player->PlayerCameraManager)) return;
    using namespace ProjectCheckpointPreview;
    const double SetupStart=FPlatformTime::Seconds();

    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags |= RF_Transient;
    ASceneCapture2D* Actor=World->SpawnActor<ASceneCapture2D>(Spawn);
    if (!IsValid(Actor)) return;
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
    const double RenderStart=FPlatformTime::Seconds();
    Capture->CaptureScene();
    TArray<FColor> Pixels;
    FTextureRenderTargetResource* Resource=Target->GameThread_GetRenderTargetResource();
    const bool bRead=Resource && Resource->ReadPixels(Pixels) && Pixels.Num()==Width*Height;
    const double EncodeStart=FPlatformTime::Seconds();
    if (bRead)
    {
        for (FColor& Pixel:Pixels) Pixel.A=255;
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(Width,Height,Pixels,Png);
        if (Png.Num()>0 && Png.Num()<=MaximumBytes) OutPng.Append(Png.GetData(),static_cast<int32>(Png.Num()));
    }
    Capture->TextureTarget=nullptr;
    Actor->Destroy();
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

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

void ProjectCheckpointPreview::Capture(UWorld* World, APlayerController* Player, TArray<uint8>& OutPng)
{
    OutPng.Reset();
    if (!FApp::CanEverRender() || !IsValid(World) || World->GetNetMode()!=NM_Standalone ||
        !IsValid(Player) || !IsValid(Player->PlayerCameraManager)) return;

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
    Capture->CaptureScene();
    TArray<FColor> Pixels;
    FTextureRenderTargetResource* Resource=Target->GameThread_GetRenderTargetResource();
    if (Resource && Resource->ReadPixels(Pixels) && Pixels.Num()==Width*Height)
    {
        for (FColor& Pixel:Pixels) Pixel.A=255;
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(Width,Height,Pixels,Png);
        if (Png.Num()>0 && Png.Num()<=MaximumBytes) OutPng.Append(Png.GetData(),static_cast<int32>(Png.Num()));
    }
    Capture->TextureTarget=nullptr;
    Actor->Destroy();
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

#pragma once

#include "CoreMinimal.h"

class UWorld;
class APlayerController;
class UTexture2D;

/** Optional, local-only save thumbnail. Never capture UI or upload this image. */
namespace ProjectCheckpointPreview
{
    constexpr int32 Width = 320;
    constexpr int32 Height = 180;
    constexpr int32 MaximumBytes = 512 * 1024;
    PROJECTPROJECT01_API void Capture(UWorld* World, APlayerController* Player, TArray<uint8>& OutPng);
    /** Game thread only: renders the view and reads back Width*Height opaque pixels. False when unavailable. */
    PROJECTPROJECT01_API bool CapturePixels(UWorld* World, APlayerController* Player, TArray<FColor>& OutPixels);
    /** Called once on the game thread with Width*Height opaque pixels, or an empty array on failure/timeout. */
    using FPixelsReady = TFunction<void(TArray<FColor>&&)>;
    /**
     * Game thread only: renders the view now, but reads it back from the GPU over the next frames without
     * stalling the game thread. Returns false (and never calls OnReady) when no capture could be started.
     */
    PROJECTPROJECT01_API bool CapturePixelsAsync(UWorld* World, APlayerController* Player, FPixelsReady OnReady);
    /** Game thread only: loads the image encoder so EncodePixels can run on a worker thread. */
    PROJECTPROJECT01_API bool PrepareEncoder();
    /** Any thread after PrepareEncoder(): PNG-encodes captured pixels. Empty when encoding fails or exceeds MaximumBytes. */
    PROJECTPROJECT01_API void EncodePixels(const TArray<FColor>& Pixels, TArray<uint8>& OutPng);
    PROJECTPROJECT01_API bool IsValidImage(const TArray<uint8>& Png, uint32 ExpectedCrc);
    PROJECTPROJECT01_API UTexture2D* Decode(const TArray<uint8>& Png, uint32 ExpectedCrc);
}

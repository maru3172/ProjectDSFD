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
    PROJECTPROJECT01_API bool IsValidImage(const TArray<uint8>& Png, uint32 ExpectedCrc);
    PROJECTPROJECT01_API UTexture2D* Decode(const TArray<uint8>& Png, uint32 ExpectedCrc);
}

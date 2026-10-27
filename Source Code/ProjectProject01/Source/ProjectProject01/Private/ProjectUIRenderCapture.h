#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/TextureRenderTarget2D.h"

// Keep dark gradients in linear float precision until the single PNG/sRGB conversion.
namespace ProjectUIRenderCapture
{
    inline UTextureRenderTarget2D* Create(FIntPoint Size)
    {
        auto* Target=NewObject<UTextureRenderTarget2D>();
        Target->ClearColor=FLinearColor::Transparent;Target->TargetGamma=1;
        Target->InitCustomFormat(Size.X,Size.Y,PF_FloatRGBA,true);
        Target->UpdateResourceImmediate(true);return Target;
    }
    inline bool Read(UTextureRenderTarget2D* Target,TArray<FColor>& Pixels)
    {
        TArray<FLinearColor> Linear;FReadSurfaceDataFlags Flags(RCM_MinMax);Flags.SetLinearToGamma(false);
        if(!Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Linear,Flags))return false;
        Pixels.SetNumUninitialized(Linear.Num());
        for(int32 I=0;I<Linear.Num();++I)Pixels[I]=Linear[I].ToFColor(true);
        return true;
    }
}
#endif

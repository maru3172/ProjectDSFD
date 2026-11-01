#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "ProjectStoreBackdrop.generated.h"

class FSlateWindowElementList;
struct FGeometry;

namespace ProjectDFSDUI
{
    /**
     * The menus' background: a closed, abandoned department store at night. A dark aisle with dead ceiling lights (one
     * faintly on, far away), empty shelves, a half-lowered fire shutter, a "폐점" sign and mannequins standing in the
     * dark, under heavy vignette and dust. Static (no flicker or motion), drawn on Layer..Layer+2 to cover the geometry.
     */
    PROJECTPROJECT01_API int32 PaintStoreBackdrop(FSlateWindowElementList& Out,const FGeometry& Geometry,int32 Layer,const FLinearColor& Tint);
}

/** The store backdrop as a widget, for pages built in UMG (the single-player save slots). */
UCLASS()
class PROJECTPROJECT01_API UProjectDFSDStoreBackdrop final : public UWidget
{
    GENERATED_BODY()
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};

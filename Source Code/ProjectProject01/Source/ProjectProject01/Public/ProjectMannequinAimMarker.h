// File: Source/ProjectProject01/Public/ProjectMannequinAimMarker.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

/**
 * Before the command wheel opens: a thin steady ring on the mannequin the crosshair points at, so the mannequin player
 * sees who a right click would give the order to (Docs/MannequinCommandWheel.md). Drawn on this screen only; nothing is
 * replicated, so survivors never see it. No blinking. Hidden while nothing is aimed at or the wheel is open.
 */
class PROJECTPROJECT01_API SProjectMannequinAimMarker final : public SLeafWidget
{
public:
    /** Fills the target's screen centre (0..1) and height (fraction of the screen); false hides the ring. */
    using FLocate=TFunction<bool(FVector2D& At,float& Height,FString& Label)>;
    SLATE_BEGIN_ARGS(SProjectMannequinAimMarker){}
        SLATE_ARGUMENT(FLocate,Locate)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args){Locate=Args._Locate;SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D::ZeroVector;}
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled)const override;
private:
    FLocate Locate;
};

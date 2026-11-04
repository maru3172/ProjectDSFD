// File: Source/ProjectProject01/Public/ProjectMannequinWheel.h

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

/**
 * The mannequin player's command wheel (opened with the right mouse button; see AMultiplayTestPlayerController).
 * A ring of commands around the screen centre, like an emote wheel: the wedge under the mouse lights up, pressing the
 * left button shows it pressed and picks it on release, the right button closes the wheel without a command.
 * It also rings the chosen mannequin on screen; the wheel exists only on the mannequin player's own screen.
 */
class SProjectMannequinWheel final:public SLeafWidget // used only inside this module (constexpr members cannot be exported)
{
public:
    struct FOption{FString Label;bool bEnabled=true;};
    SLATE_BEGIN_ARGS(SProjectMannequinWheel){}
        SLATE_ARGUMENT(TArray<FOption>,Options)
        SLATE_ARGUMENT(FString,Title)
        /** What that mannequin is doing now, drawn under the title (read every frame, so it follows a new order). */
        SLATE_ARGUMENT(TFunction<FString()>,Status)
        /** Where the chosen mannequin is, as a 0..1 fraction of the screen, and roughly how tall; false when off screen. */
        SLATE_ARGUMENT(TFunction<bool(FVector2D&,float&)>,Locate)
        SLATE_ARGUMENT(TFunction<void(int32)>,OnChoose)
        SLATE_ARGUMENT(TFunction<void()>,OnCancel)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    int32 HoveredIndex()const{return Hovered;}
    /** For tests: the option under a point in the widget's own space (INDEX_NONE in the middle or outside). */
    int32 HitTest(const FVector2D& Local,const FVector2D& Size)const;

    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(400,400);}
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled)const override;
    virtual FReply OnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event)override;
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event)override;
    virtual bool SupportsKeyboardFocus()const override{return false;} // the keyboard stays with the game: the mannequin can keep moving

    static constexpr float OuterRadius=190,InnerRadius=64;
private:
    TArray<FOption> Options;FString Title;TFunction<FString()> Status;TFunction<bool(FVector2D&,float&)> Locate;TFunction<void(int32)> OnChoose;TFunction<void()> OnCancel;
    int32 Hovered=INDEX_NONE,Pressed=INDEX_NONE;
};

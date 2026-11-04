// File: Source/ProjectProject01/Private/ProjectMannequinAimMarker.cpp
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#include "ProjectMannequinAimMarker.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"

int32 SProjectMannequinAimMarker::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool)const
{
    FVector2D At;float Height=0;FString Label;
    if(!Locate||!Locate(At,Height,Label))return Layer;
    const FVector2D Size=G.GetLocalSize(),P(At.X*Size.X,At.Y*Size.Y);
    const float R=FMath::Clamp(Height*Size.Y*.5f,22.f,170.f);
    // A thinner, paler ring than the wheel's red one: "this is who you would pick", not "picked".
    TArray<FVector2D> Ring;for(int32 I=0;I<=48;++I){const float A=2*PI*I/48;Ring.Add(P+FVector2D(FMath::Cos(A),FMath::Sin(A)*1.15f)*R);}
    FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Ring,ESlateDrawEffect::None,FLinearColor(1.f,.78f,.62f,.7f),true,2.f);
    if(!Label.IsEmpty())
    {
        const FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12);
        const FVector2D TextSize=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label,Font);
        const FVector2D TextAt(P.X-TextSize.X*.5f,P.Y-R*1.15f-TextSize.Y-4);
        FSlateDrawElement::MakeText(Out,Layer+1,G.ToPaintGeometry(TextSize,FSlateLayoutTransform(TextAt)),Label,Font,ESlateDrawEffect::None,FLinearColor(1.f,.86f,.75f,.9f));
    }
    return Layer+1;
}

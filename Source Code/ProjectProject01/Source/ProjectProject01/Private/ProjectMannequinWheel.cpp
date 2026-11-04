// File: Source/ProjectProject01/Private/ProjectMannequinWheel.cpp

#include "ProjectMannequinWheel.h"
#include "ProjectDFSDUITheme.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"

namespace
{
/** A filled ring sector between two radii (Inner 0: a pie slice), drawn as triangles so it has no line texture. */
void FillSector(FSlateWindowElementList& Out,int32 Layer,const FGeometry& G,FVector2D Center,float Inner,float Outer,float FromDeg,float ToDeg,const FLinearColor& Color)
{
    const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FSlateResourceHandle Handle=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
    const FSlateRenderTransform& T=G.GetAccumulatedRenderTransform();const FColor C=Color.ToFColor(true);
    const int32 Steps=FMath::Max(8,int32(FMath::Abs(ToDeg-FromDeg)/3.f));
    TArray<FSlateVertex> Verts;TArray<SlateIndex> Index;
    for(int32 S=0;S<=Steps;++S)
    {
        const float A=FMath::DegreesToRadians(FMath::Lerp(FromDeg,ToDeg,float(S)/Steps));const FVector2D Dir(FMath::Cos(A),FMath::Sin(A));
        Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T,FVector2f(Center+Dir*Outer),FVector2f(.5f,.5f),C));
        Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T,FVector2f(Center+Dir*Inner),FVector2f(.5f,.5f),C));
        if(S<Steps){const SlateIndex B=SlateIndex(S*2);Index.Append({B,SlateIndex(B+1),SlateIndex(B+2),SlateIndex(B+1),SlateIndex(B+3),SlateIndex(B+2)});}
    }
    FSlateDrawElement::MakeCustomVerts(Out,Layer,Handle,Verts,Index,nullptr,0,0);
}
}

void SProjectMannequinWheel::Construct(const FArguments& Args)
{
    Options=Args._Options;Title=Args._Title;Status=Args._Status;Locate=Args._Locate;OnChoose=Args._OnChoose;OnCancel=Args._OnCancel;
}
int32 SProjectMannequinWheel::HitTest(const FVector2D& Local,const FVector2D& Size)const
{
    const FVector2D D=Local-Size*.5f;const float R=D.Size();
    if(Options.IsEmpty()||R<InnerRadius||R>OuterRadius+40)return INDEX_NONE;
    // Wedge 0 is centred straight up; the rest follow clockwise.
    float Angle=FMath::RadiansToDegrees(FMath::Atan2(D.X,-D.Y));if(Angle<0)Angle+=360;
    const float Step=360.f/Options.Num();
    return int32(FMath::Fmod(Angle+Step*.5f,360.f)/Step)%Options.Num();
}
int32 SProjectMannequinWheel::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool)const
{
    const FVector2D Size=G.GetLocalSize(),Center=Size*.5f;
    const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    // The chosen mannequin: a ring on screen (drawn first, under the wheel). Only this player's screen has it.
    FVector2D At;float Height=0;
    if(Locate&&Locate(At,Height))
    {
        const FVector2D P(At.X*Size.X,At.Y*Size.Y);const float R=FMath::Clamp(Height*Size.Y*.5f,24.f,180.f);
        TArray<FVector2D> Ring;for(int32 I=0;I<=48;++I){const float A=2*PI*I/48;Ring.Add(P+FVector2D(FMath::Cos(A),FMath::Sin(A)*1.15f)*R);}
        FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Ring,ESlateDrawEffect::None,FLinearColor(.95f,.35f,.25f,.9f),true,3.f);
    }
    ++Layer;
    // A dark ring behind the wheel so it reads over any scene.
    FillSector(Out,Layer,G,Center,InnerRadius-7,OuterRadius+7,0,360,FLinearColor(0,0,0,.55f));
    ++Layer;
    const int32 Count=Options.Num();const float Step=Count>0?360.f/Count:360.f;
    const TSharedRef<FSlateFontMeasure> Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    for(int32 I=0;I<Count;++I)
    {
        // Each wedge is a thick arc between the inner and outer radius, with a small gap to its neighbours.
        const bool bOn=Options[I].bEnabled,bHover=I==Hovered&&bOn,bDown=I==Pressed&&bHover;
        FLinearColor Fill=!bOn?FLinearColor(.08f,.08f,.08f,.75f):bDown?FLinearColor(.45f,.12f,.08f,.95f):bHover?FLinearColor(.95f,.42f,.28f,.95f):FLinearColor(.17f,.16f,.15f,.9f);
        const float From=-90+I*Step-Step*.5f+1.5f,To=-90+I*Step+Step*.5f-1.5f;const float Mid=(OuterRadius+InnerRadius)*.5f;
        const float Grow=bHover&&!bDown?6.f:0.f; // the lit wedge stands a little proud; pressed sinks back
        FillSector(Out,Layer,G,Center,InnerRadius,OuterRadius+Grow,From,To,Fill);
        const float A=FMath::DegreesToRadians(-90+I*Step);const FVector2D LabelAt=Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Mid;
        const FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),bHover?15:13);
        const FVector2D TextSize=Measure->Measure(Options[I].Label,Font);
        const FLinearColor Ink=!bOn?FLinearColor(.4f,.4f,.4f,1.f):bHover&&!bDown?FLinearColor(.06f,.03f,.02f,1.f):ProjectDFSDUI::Text();
        FSlateDrawElement::MakeText(Out,Layer+1,G.ToPaintGeometry(TextSize,FSlateLayoutTransform(LabelAt-TextSize*.5f)),Options[I].Label,Font,ESlateDrawEffect::None,Ink);
    }
    // The middle: whom the order is for, on a dark disc so it reads over any scene.
    FillSector(Out,Layer,G,Center,0,InnerRadius-8,0,360,FLinearColor(.02f,.02f,.02f,.9f));
    const FSlateFontInfo TitleFont=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),13);
    const FVector2D TitleSize=Measure->Measure(Title,TitleFont);
    const FString Now=Status?Status():FString();
    const FSlateFontInfo StatusFont=FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11);
    const FVector2D StatusSize=Now.IsEmpty()?FVector2D::ZeroVector:Measure->Measure(Now,StatusFont);
    // The title, and right under it what the mannequin is doing now; both centred on the disc.
    const FVector2D TitleAt=Center-TitleSize*.5f-FVector2D(0,Now.IsEmpty()?0.f:StatusSize.Y*.5f+1.f);
    FSlateDrawElement::MakeText(Out,Layer+1,G.ToPaintGeometry(TitleSize,FSlateLayoutTransform(TitleAt)),Title,TitleFont,ESlateDrawEffect::None,ProjectDFSDUI::Text());
    if(!Now.IsEmpty())FSlateDrawElement::MakeText(Out,Layer+1,G.ToPaintGeometry(StatusSize,FSlateLayoutTransform(FVector2D(Center.X-StatusSize.X*.5f,TitleAt.Y+TitleSize.Y+2.f))),Now,StatusFont,ESlateDrawEffect::None,FLinearColor(.95f,.72f,.55f,1.f));
    return Layer+2;
}
FReply SProjectMannequinWheel::OnMouseMove(const FGeometry& G,const FPointerEvent& E)
{
    Hovered=HitTest(G.AbsoluteToLocal(E.GetScreenSpacePosition()),G.GetLocalSize());
    if(Pressed!=INDEX_NONE&&Pressed!=Hovered)Pressed=INDEX_NONE; // sliding off a pressed wedge lets it go
    return FReply::Handled();
}
FReply SProjectMannequinWheel::OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()==EKeys::RightMouseButton){if(OnCancel)OnCancel();return FReply::Handled();}
    if(E.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        Hovered=HitTest(G.AbsoluteToLocal(E.GetScreenSpacePosition()),G.GetLocalSize());
        Pressed=Hovered!=INDEX_NONE&&Options[Hovered].bEnabled?Hovered:INDEX_NONE;
    }
    return FReply::Handled();
}
FReply SProjectMannequinWheel::OnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Handled();
    const int32 At=HitTest(G.AbsoluteToLocal(E.GetScreenSpacePosition()),G.GetLocalSize());
    const int32 Chosen=Pressed!=INDEX_NONE&&Pressed==At?Pressed:INDEX_NONE;Pressed=INDEX_NONE;
    if(Chosen!=INDEX_NONE&&OnChoose)OnChoose(Chosen);
    return FReply::Handled();
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Slate/WidgetRenderer.h"
#include "ProjectUIRenderCapture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
// Which wedge each direction picks, that disabled wedges are not picked, and a picture with a lit wedge (Saved/MannequinWheelQA).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMannequinWheelTest,"ProjectProject01.MannequinWheel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectMannequinWheelTest::RunTest(const FString&)
{
    TArray<SProjectMannequinWheel::FOption> Options;
    for(const TCHAR* Name:{TEXT("정찰"),TEXT("범위 추격"),TEXT("따라오기"),TEXT("정지"),TEXT("직접 조작"),TEXT("빙의"),TEXT("추종 해제"),TEXT("잡기 피해")})Options.Add({Name,true});
    Options[5].bEnabled=false; // 빙의 with no other mannequin chosen
    int32 Chosen=INDEX_NONE;bool bCancelled=false;
    const TSharedRef<SProjectMannequinWheel> Wheel=SNew(SProjectMannequinWheel).Options(Options).Title(TEXT("2번 마네킹"))
        .Locate([](FVector2D& At,float& H){At=FVector2D(.72f,.45f);H=.3f;return true;}).OnChoose([&Chosen](int32 I){Chosen=I;}).OnCancel([&bCancelled]{bCancelled=true;});
    const FVector2D Size(1280,720),C=Size*.5f;const float R=(SProjectMannequinWheel::OuterRadius+SProjectMannequinWheel::InnerRadius)*.5f;
    TestEqual(TEXT("Straight up is the first command"),Wheel->HitTest(C+FVector2D(0,-R),Size),0);
    TestEqual(TEXT("Right is the third"),Wheel->HitTest(C+FVector2D(R,0),Size),2);
    TestEqual(TEXT("Down is the fifth"),Wheel->HitTest(C+FVector2D(0,R),Size),4);
    TestEqual(TEXT("Left is the seventh"),Wheel->HitTest(C+FVector2D(-R,0),Size),6);
    TestEqual(TEXT("The middle picks nothing"),Wheel->HitTest(C,Size),INDEX_NONE);
    const FGeometry Root=FGeometry::MakeRoot(Size,FSlateLayoutTransform());
    auto Pointer=[&](FVector2D At,FKey Button){return FPointerEvent(0,At,At,TSet<FKey>(),Button,0,FModifierKeysState());};
    Wheel->OnMouseMove(Root,Pointer(C+FVector2D(R,0),EKeys::Invalid));
    TestEqual(TEXT("Hovering lights the wedge under the mouse"),Wheel->HoveredIndex(),2);
    // A press and release on a disabled wedge picks nothing; on an enabled one it picks it.
    const FVector2D Possess=C+FVector2D(FMath::Cos(FMath::DegreesToRadians(-90+5*45.f)),FMath::Sin(FMath::DegreesToRadians(-90+5*45.f)))*R;
    Wheel->OnMouseButtonDown(Root,Pointer(Possess,EKeys::LeftMouseButton));Wheel->OnMouseButtonUp(Root,Pointer(Possess,EKeys::LeftMouseButton));
    TestEqual(TEXT("A disabled command is not picked"),Chosen,INDEX_NONE);
    Wheel->OnMouseMove(Root,Pointer(C+FVector2D(R,0),EKeys::Invalid));
    // Picture: wedge 2 lit, then pressed.
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("MannequinWheelQA");IFileManager::Get().MakeDirectory(*Directory,true);
    FWidgetRenderer Renderer(false);bool bSaved=true;
    auto Capture=[&](const FString& Name)
    {
        auto* Target=ProjectUIRenderCapture::Create(FIntPoint(1280,720));
        for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,Wheel,1.f,Size,0);
        FlushRenderingCommands();TArray<FColor> Pixels;
        if(ProjectUIRenderCapture::Read(Target,Pixels)){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1280,720,Pixels,PNG);bSaved&=FFileHelper::SaveArrayToFile(PNG,*(Directory/Name));}else bSaved=false;
        Target->ReleaseResource();
    };
    Capture(TEXT("Wheel_Hover.png"));
    Wheel->OnMouseButtonDown(Root,Pointer(C+FVector2D(R,0),EKeys::LeftMouseButton));Capture(TEXT("Wheel_Pressed.png"));
    Wheel->OnMouseButtonUp(Root,Pointer(C+FVector2D(R,0),EKeys::LeftMouseButton));
    TestEqual(TEXT("Press and release on a command picks it"),Chosen,2);
    Wheel->OnMouseButtonDown(Root,Pointer(C,EKeys::RightMouseButton));
    TestTrue(TEXT("Right click closes without a command"),bCancelled);
    TestTrue(TEXT("Wheel pictures saved"),bSaved);
    return true;
}
#endif

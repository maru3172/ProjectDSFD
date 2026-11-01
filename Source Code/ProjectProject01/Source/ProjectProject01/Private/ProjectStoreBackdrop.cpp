#include "ProjectStoreBackdrop.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Math/RandomStream.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
    // Everything is laid out on a 1600x900 reference picture that covers the area (cropped, never stretched).
    struct FStorePainter
    {
        FSlateWindowElementList& Out;const FGeometry& G;FVector2D Offset;double Scale;FLinearColor Tint;
        const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        FVector2D P(double X,double Y)const{return Offset+FVector2D(X,Y)*Scale;}
        void Box(int32 Layer,double X,double Y,double W,double H,const FLinearColor& C,const FSlateBrush* Brush=nullptr)const
        {FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(W,H)*Scale,FSlateLayoutTransform(P(X,Y))),Brush?Brush:White,ESlateDrawEffect::None,C*Tint);}
        void Line(int32 Layer,TArray<FVector2D> Points,const FLinearColor& C,double Thickness=1)const
        {for(auto& Point:Points)Point=P(Point.X,Point.Y);FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,C*Tint,true,float(FMath::Max(1.,Thickness*Scale)));}
        /** A store mannequin standing in the dark: a flat silhouette with a faint rim of light on one side. */
        void Mannequin(int32 Layer,double X,double Foot,double H,const FLinearColor& Rim)const
        {
            static const FSlateRoundedBoxBrush Round(FLinearColor::White,FVector4(64.f,64.f,64.f,64.f));
            const FLinearColor Body(.0032f,.0032f,.0034f,1.f);const double Top=Foot-H;
            Box(Layer,X-H*.065,Top,H*.13,H*.13,Body,&Round);                                            // head
            Box(Layer,X-H*.022,Top+H*.12,H*.044,H*.04,Body);                                            // neck
            Box(Layer,X-H*.12,Top+H*.155,H*.24,H*.11,Body);Box(Layer,X-H*.1,Top+H*.26,H*.2,H*.12,Body); // chest
            Box(Layer,X-H*.08,Top+H*.375,H*.16,H*.1,Body);Box(Layer,X-H*.095,Top+H*.47,H*.19,H*.07,Body); // waist, hips
            Box(Layer,X-H*.085,Top+H*.535,H*.07,H*.465,Body);Box(Layer,X+H*.015,Top+H*.535,H*.07,H*.465,Body); // legs
            Box(Layer,X-H*.165,Top+H*.165,H*.05,H*.36,Body);Box(Layer,X+H*.118,Top+H*.165,H*.05,H*.34,Body);   // arms, hanging stiff
            Line(Layer+1,{{X+H*.065,Top+H*.03},{X+H*.07,Top+H*.1}},Rim,1.2);
            Line(Layer+1,{{X+H*.168,Top+H*.17},{X+H*.168,Top+H*.5}},Rim,1.2);
            Line(Layer+1,{{X+H*.085,Top+H*.56},{X+H*.085,Foot}},Rim,1.2);
        }
    };
}

int32 ProjectDFSDUI::PaintStoreBackdrop(FSlateWindowElementList& Out,const FGeometry& G,int32 Layer,const FLinearColor& Tint)
{
    const FVector2D Size=G.GetLocalSize();if(Size.X<=0||Size.Y<=0)return Layer;
    const double Scale=FMath::Max(Size.X/1600.,Size.Y/900.);
    const FStorePainter S{Out,G,(Size-FVector2D(1600,900)*Scale)*.5,Scale,Tint};
    const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FVector2D Vanish(800,430);

    // Room: black ceiling, a slightly lighter far wall, and a dull, grimy floor that brightens a little toward us.
    FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),White,ESlateDrawEffect::None,FLinearColor(.0085f,.0085f,.009f,1.f)*Tint);
    S.Box(Layer,-400,-200,2400,500,FLinearColor(.005f,.005f,.0055f,1.f));
    S.Box(Layer,500,250,600,270,FLinearColor(.011f,.0105f,.0105f,1.f));
    for(int32 Band=0;Band<6;++Band){const float T=Band/5.f;S.Box(Layer,-400,520+Band*80,2400,82,FMath::Lerp(FLinearColor(.010f,.0095f,.009f,1.f),FLinearColor(.018f,.016f,.013f,1.f),T));}

    const int32 Structure=Layer+1;
    // Floor tiles running to the far end of the aisle.
    for(double X=-1000;X<=2600;X+=200){const double T=(520-Vanish.Y)/(900-Vanish.Y);S.Line(Structure,{{X,900},{Vanish.X+(X-Vanish.X)*T,520}},FLinearColor(.03f,.028f,.024f,.35f));}
    for(int32 K=1;K<=7;++K){const double Y=520+380*FMath::Square(K/7.);S.Line(Structure,{{-400,Y},{2000,Y}},FLinearColor(.026f,.024f,.02f,.3f));}

    // Ceiling lights in two rows, all dead except one far on the right; one near fixture hangs loose.
    static const FSlateRoundedBoxBrush Glow(FLinearColor::White,FVector4(90.f,90.f,90.f,90.f));
    for(int32 K=0;K<6;++K)for(int32 Side=-1;Side<=1;Side+=2)
    {
        const double D=1/(1+.75*K),Y=Vanish.Y-400*D,X=Vanish.X+Side*420*D,W=260*D,H=FMath::Max(2.,14*D);
        if(Side<0&&K==1){S.Line(Structure,{{X-W*.5,Y-6},{X+W*.45,Y+46}},FLinearColor(.024f,.024f,.026f,1.f),10*D);S.Line(Structure,{{X+W*.45,Y+46},{X+W*.45,Y-30}},FLinearColor(.02f,.02f,.02f,.8f),1.5);continue;}
        const bool Lit=Side>0&&K==3;
        if(Lit)for(int32 Ring=3;Ring>=1;--Ring)S.Box(Structure,X-W*.5-30*Ring,Y-14*Ring,W+60*Ring,H+28*Ring,FLinearColor(.22f,.19f,.13f,.016f*(4-Ring)),&Glow);
        S.Box(Structure,X-W*.5,Y,W,H,Lit?FLinearColor(.17f,.155f,.11f,1.f):FLinearColor(.024f,.024f,.026f,1.f));
    }

    // Empty shelving on the left, two more shelves and a half-lowered fire shutter on the right.
    const FLinearColor Shelf(.0045f,.0045f,.0048f,1.f),Board(.02f,.019f,.017f,.6f);
    struct FRect{double X,Y,W,H;};
    for(const FRect& R:{FRect{0,180,170,560},FRect{190,250,120,400},FRect{330,300,90,300},FRect{440,335,60,215},FRect{515,360,38,165},FRect{1062,360,38,165},FRect{1140,335,60,215}})
    {
        S.Box(Structure,R.X,R.Y,R.W,R.H,Shelf);
        for(int32 I=1;I<=4;++I){const double Y=R.Y+R.H*I/5.;S.Line(Structure,{{R.X,Y},{R.X+R.W,Y}},Board,FMath::Max(1.,R.W/80.));}
    }
    S.Box(Structure,1230,230,370,290,FLinearColor(.0105f,.010f,.0095f,1.f));
    for(double Y=240;Y<520;Y+=14)S.Line(Structure,{{1230,Y},{1600,Y}},FLinearColor(.02f,.019f,.018f,.7f));
    S.Box(Structure,1230,516,370,8,FLinearColor(.03f,.028f,.025f,1.f));
    S.Box(Structure,1230,524,370,176,FLinearColor(.003f,.003f,.003f,1.f));

    // A "폐점" sign hanging over the aisle.
    S.Line(Structure,{{702,-10},{706,152}},FLinearColor(.02f,.02f,.02f,.9f),1.5);S.Line(Structure,{{828,-10},{824,152}},FLinearColor(.02f,.02f,.02f,.9f),1.5);
    S.Box(Structure,690,150,150,58,FLinearColor(.012f,.011f,.010f,1.f));
    FSlateDrawElement::MakeText(Out,Structure+1,G.ToPaintGeometry(FVector2D(150,58)*Scale,FSlateLayoutTransform(S.P(722,158))),FString(TEXT("폐 점")),
        FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),FMath::Max(8,int32(26*Scale))),ESlateDrawEffect::None,FLinearColor(.09f,.025f,.02f,1.f)*Tint);

    // Mannequins: one by the shelves, one far down the aisle under the last light, one close on the right.
    S.Mannequin(Structure,600,520,150,FLinearColor(.035f,.03f,.026f,.5f));
    S.Mannequin(Structure,905,468,72,FLinearColor(.09f,.08f,.055f,.7f));
    S.Mannequin(Structure,1490,930,600,FLinearColor(.03f,.027f,.024f,.45f));

    // Dust in the air, then heavy darkness toward the edges.
    const int32 Air=Layer+2;FRandomStream Dust(1337);
    for(int32 I=0;I<260;++I){const double D=Dust.FRandRange(1.,2.5);S.Box(Air,Dust.FRandRange(0.,1600.),Dust.FRandRange(0.,900.),D,D,FLinearColor(.35f,.34f,.32f,Dust.FRandRange(.04f,.12f)));}
    for(int32 Ring=0;Ring<12;++Ring)
    {
        const float Band=(Ring+1)*FMath::Min(Size.X,Size.Y)*.018f;const FLinearColor Dark(0,0,0,.055f);
        FSlateDrawElement::MakeBox(Out,Air,G.ToPaintGeometry(FVector2D(Size.X,Band),FSlateLayoutTransform(FVector2D(0,0))),White,ESlateDrawEffect::None,Dark*Tint);
        FSlateDrawElement::MakeBox(Out,Air,G.ToPaintGeometry(FVector2D(Size.X,Band),FSlateLayoutTransform(FVector2D(0,Size.Y-Band))),White,ESlateDrawEffect::None,Dark*Tint);
        FSlateDrawElement::MakeBox(Out,Air,G.ToPaintGeometry(FVector2D(Band,Size.Y),FSlateLayoutTransform(FVector2D(0,0))),White,ESlateDrawEffect::None,Dark*Tint);
        FSlateDrawElement::MakeBox(Out,Air,G.ToPaintGeometry(FVector2D(Band,Size.Y),FSlateLayoutTransform(FVector2D(Size.X-Band,0))),White,ESlateDrawEffect::None,Dark*Tint);
    }
    return Air;
}

class SProjectStoreBackdrop final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectStoreBackdrop){}SLATE_END_ARGS()
    void Construct(const FArguments&){}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(1600,900);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool)const override
    {return ProjectDFSDUI::PaintStoreBackdrop(Out,G,Layer,Style.GetColorAndOpacityTint());}
};

TSharedRef<SWidget> UProjectDFSDStoreBackdrop::RebuildWidget(){return SNew(SProjectStoreBackdrop);}

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
// Draws the backdrop alone at a wide and a 4:3 size to Saved/UIThemeQA, to review what shows around the menus.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectStoreBackdropRenderTest,"ProjectProject01.UITheme.StoreBackdrop",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectStoreBackdropRenderTest::RunTest(const FString&)
{
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("UIThemeQA");IFileManager::Get().MakeDirectory(*Directory,true);
    FWidgetRenderer Renderer(false);bool bSaved=true;const auto Backdrop=SNew(SProjectStoreBackdrop);
    for(const FIntPoint Size:{FIntPoint(1600,900),FIntPoint(800,600)})
    {
        auto* Target=ProjectUIRenderCapture::Create(Size);
        for(int32 Pass=0;Pass<2;++Pass)Renderer.DrawWidget(Target,Backdrop,1.f,FVector2D(Size),0);
        FlushRenderingCommands();TArray<FColor> Pixels;
        if(ProjectUIRenderCapture::Read(Target,Pixels)){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);bSaved&=FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("StoreBackdrop_%dx%d.png"),Size.X,Size.Y)));}else bSaved=false;
        Target->ReleaseResource();
    }
    TestTrue(TEXT("The store backdrop renders"),bSaved);
    return true;
}
#endif

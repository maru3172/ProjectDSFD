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
        /** A filled circle (stacked slices, so it stays round at any size). */
        void Disc(int32 Layer,double X,double Y,double R,const FLinearColor& C)const
        {const int32 Slices=FMath::Clamp(int32(R*Scale),6,48);for(int32 I=0;I<Slices;++I){const double Y0=-R+2*R*I/Slices,Mid=Y0+R/Slices,W=2*FMath::Sqrt(FMath::Max(0.,R*R-Mid*Mid));Box(Layer,X-W*.5,Y+Y0,W,2*R/Slices+.5,C);}}
        /** A soft stain: rings of the same colour that build up toward the centre (soot, scorch, rust water). */
        void Stain(int32 Layer,double X,double Y,double R,const FLinearColor& C,double Squash=1)const
        {const int32 Slices=FMath::Clamp(int32(R*Squash*Scale*.5),8,40);for(int32 Ring=8;Ring>=1;--Ring){const double Rr=R*Ring/8;for(int32 I=0;I<Slices;++I){const double Y0=-Rr+2*Rr*I/Slices,Mid=Y0+Rr/Slices,W=2*FMath::Sqrt(FMath::Max(0.,Rr*Rr-Mid*Mid));Box(Layer,X-W*.5,Y+Y0*Squash,W,2*Rr/Slices*Squash+.5,C*FLinearColor(1,1,1,.18f));}}}
        /** A store mannequin standing in the dark: a flat silhouette with a faint rim of light on one side. */
        void Mannequin(int32 Layer,double X,double Foot,double H,const FLinearColor& Rim,bool bMissingArm=false)const
        {
            const FLinearColor Body(.0032f,.0032f,.0034f,1.f);const double Top=Foot-H;
            Disc(Layer,X,Top+H*.065,H*.065,Body);                                                         // head
            Box(Layer,X-H*.022,Top+H*.12,H*.044,H*.04,Body);                                            // neck
            Box(Layer,X-H*.12,Top+H*.155,H*.24,H*.11,Body);Box(Layer,X-H*.1,Top+H*.26,H*.2,H*.12,Body); // chest
            Box(Layer,X-H*.08,Top+H*.375,H*.16,H*.1,Body);Box(Layer,X-H*.095,Top+H*.47,H*.19,H*.07,Body); // waist, hips
            Box(Layer,X-H*.085,Top+H*.535,H*.07,H*.465,Body);Box(Layer,X+H*.015,Top+H*.535,H*.07,H*.465,Body); // legs
            Box(Layer,X-H*.165,Top+H*.165,H*.05,H*.36,Body);                                            // arms, hanging stiff
            if(!bMissingArm)Box(Layer,X+H*.118,Top+H*.165,H*.05,H*.34,Body);
            Line(Layer+1,{{X+H*.05,Top+H*.02},{X+H*.065,Top+H*.09}},Rim,1.2);
            if(!bMissingArm)Line(Layer+1,{{X+H*.168,Top+H*.17},{X+H*.168,Top+H*.5}},Rim,1.2);
            else Line(Layer+1,{{X+H*.12,Top+H*.16},{X+H*.12,Top+H*.26}},Rim,1.2);
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
    FRandomStream Wear(2024);

    // Room: black ceiling, a slightly lighter far wall, and a dull, grimy floor that brightens a little toward us.
    FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),White,ESlateDrawEffect::None,FLinearColor(.0085f,.0085f,.009f,1.f)*Tint);
    S.Box(Layer,-400,-200,2400,500,FLinearColor(.005f,.005f,.0055f,1.f));
    S.Box(Layer,500,250,600,270,FLinearColor(.011f,.0105f,.0105f,1.f));
    for(int32 Band=0;Band<6;++Band){const float T=Band/5.f;S.Box(Layer,-400,520+Band*80,2400,82,FMath::Lerp(FLinearColor(.010f,.0095f,.009f,1.f),FLinearColor(.017f,.015f,.012f,1.f),T));}
    // An old fire: soot climbing the walls and ceiling, scorched floor, and the far end still faintly red.
    for(const FVector4& Soot:{FVector4(150,300,190,1),FVector4(420,230,120,1),FVector4(1300,210,170,1),FVector4(980,120,140,.6),FVector4(240,40,220,.5),FVector4(1450,60,200,.5)})
        S.Stain(Layer,Soot.X,Soot.Y,Soot.Z,FLinearColor(.0f,.0f,.0f,.55f),Soot.W);
    for(const FVector4& Scorch:{FVector4(560,640,150,.35),FVector4(1080,760,210,.3),FVector4(260,820,170,.3)})
        S.Stain(Layer,Scorch.X,Scorch.Y,Scorch.Z,FLinearColor(.002f,.0015f,.001f,.8f),Scorch.W);
    S.Stain(Layer,800,445,170,FLinearColor(.06f,.012f,.006f,.18f),.55); // embers' glow, far down the aisle

    const int32 Structure=Layer+1;
    // Floor tiles to the far end of the aisle: faint, uneven, some lines worn away, two cracks.
    for(double X=-1000;X<=2600;X+=200)
    {
        if(Wear.FRand()<.25f)continue;const double T=(520-Vanish.Y)/(900-Vanish.Y);
        S.Line(Structure,{{X,900},{Vanish.X+(X-Vanish.X)*T,520}},FLinearColor(.026f,.024f,.02f,Wear.FRandRange(.08f,.17f)));
    }
    for(int32 K=1;K<=7;++K){const double Y=520+380*FMath::Square(K/7.);if(Wear.FRand()<.2f)continue;S.Line(Structure,{{-400,Y},{2000,Y}},FLinearColor(.024f,.022f,.018f,Wear.FRandRange(.07f,.15f)));}
    S.Line(Structure,{{430,900},{470,820},{455,760},{510,700},{500,650}},FLinearColor(.002f,.002f,.002f,.9f),2);
    S.Line(Structure,{{1180,900},{1150,850},{1170,800},{1120,760}},FLinearColor(.002f,.002f,.002f,.9f),2);

    // Ceiling lights in two rows: dead, one fallen to the floor, one hanging by a wire, and one far tube still on.
    static const FSlateRoundedBoxBrush Glow(FLinearColor::White,FVector4(90.f,90.f,90.f,90.f));
    for(int32 K=0;K<6;++K)for(int32 Side=-1;Side<=1;Side+=2)
    {
        const double D=1/(1+.75*K),Y=Vanish.Y-400*D,X=Vanish.X+Side*420*D,W=260*D,H=FMath::Max(2.,14*D);
        if(Side<0&&K==1){S.Line(Structure,{{X-W*.5,Y-6},{X+W*.45,Y+46}},FLinearColor(.024f,.024f,.026f,1.f),10*D);S.Line(Structure,{{X+W*.45,Y+46},{X+W*.45,Y-30}},FLinearColor(.02f,.02f,.02f,.8f),1.5);continue;}
        if(Side>0&&K==0){S.Line(Structure,{{X-W*.3,Y+4},{X-W*.3,Y+60},{X-W*.25,Y+90}},FLinearColor(.02f,.02f,.02f,.8f),1.5);continue;} // empty socket, cut wires
        const bool Lit=Side>0&&K==3;
        if(Lit)for(int32 Ring=3;Ring>=1;--Ring)S.Box(Structure,X-W*.5-30*Ring,Y-14*Ring,W+60*Ring,H+28*Ring,FLinearColor(.22f,.19f,.13f,.016f*(4-Ring)),&Glow);
        S.Box(Structure,X-W*.5,Y,W,H,Lit?FLinearColor(.17f,.155f,.11f,1.f):FLinearColor(.022f,.021f,.022f,1.f));
    }
    S.Line(Structure,{{1080,860},{1290,820}},FLinearColor(.018f,.017f,.017f,1.f),9); // the fallen tube, lying across the aisle

    // Empty, burnt shelving on the left (one unit collapsed), two shelves and a buckled fire shutter on the right.
    const FLinearColor Shelf(.0045f,.0045f,.0048f,1.f),Board(.02f,.019f,.017f,.55f),Char(.0025f,.002f,.0018f,1.f);
    struct FRect{double X,Y,W,H;};
    for(const FRect& R:{FRect{0,180,170,560},FRect{330,300,90,300},FRect{440,335,60,215},FRect{515,360,38,165},FRect{1062,360,38,165},FRect{1140,335,60,215}})
    {
        S.Box(Structure,R.X,R.Y,R.W,R.H,Shelf);
        for(int32 I=1;I<=4;++I){const double Y=R.Y+R.H*I/5.;if(Wear.FRand()<.3f)S.Line(Structure,{{R.X,Y},{R.X+R.W*.55,Y+R.H*.06}},Board,FMath::Max(1.,R.W/80.));else S.Line(Structure,{{R.X,Y},{R.X+R.W,Y}},Board,FMath::Max(1.,R.W/80.));}
        S.Box(Structure,R.X,R.Y,R.W,R.H*.18,Char); // charred tops
    }
    S.Line(Structure,{{190,650},{330,300}},Shelf,26);S.Line(Structure,{{215,650},{300,420}},Board,2); // the second unit leaning on the next one
    for(int32 I=0;I<6;++I)S.Box(Structure,170+Wear.FRandRange(0.,150.),630+Wear.FRandRange(0.,40.),Wear.FRandRange(10.,34.),Wear.FRandRange(5.,12.),Shelf); // debris at its foot
    S.Box(Structure,1230,230,370,290,FLinearColor(.0105f,.010f,.0095f,1.f));
    for(double Y=240;Y<520;Y+=14){const double Bend=Y>400?FMath::Sin((Y-400)/120*PI)*18:0;S.Line(Structure,{{1230,Y},{1400,Y+Bend},{1600,Y}},FLinearColor(.02f,.019f,.018f,.7f));}
    S.Stain(Structure,1330,330,90,FLinearColor(0,0,0,.6f),1.2); // heat-blackened panel
    S.Box(Structure,1230,516,370,8,FLinearColor(.03f,.028f,.025f,1.f));
    S.Box(Structure,1230,524,370,176,FLinearColor(.003f,.003f,.003f,1.f));

    // The "폐점" sign, one wire snapped so it hangs crooked, its lower edge burnt.
    S.Line(Structure,{{702,-10},{706,152}},FLinearColor(.02f,.02f,.02f,.9f),1.5);S.Line(Structure,{{828,-10},{830,40}},FLinearColor(.02f,.02f,.02f,.9f),1.5);
    S.Line(Structure,{{700,165},{838,195}},FLinearColor(.012f,.011f,.010f,1.f),58);
    S.Line(Structure,{{700,193},{838,223}},FLinearColor(.002f,.0018f,.0015f,1.f),6);
    FSlateDrawElement::MakeText(Out,Structure+1,G.ToPaintGeometry(FVector2D(150,58)*Scale,FSlateLayoutTransform(S.P(724,160))),FString(TEXT("폐 점")),
        FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),FMath::Max(8,int32(26*Scale))),ESlateDrawEffect::None,FLinearColor(.09f,.025f,.02f,1.f)*Tint);
    // A dead exit sign by the shutter, still faintly red.
    S.Box(Structure,1255,190,64,22,FLinearColor(.004f,.004f,.004f,1.f));S.Box(Structure,1259,194,56,14,FLinearColor(.09f,.012f,.01f,.55f));

    // What the shops left behind: crooked zone plates, a clothes rack knocked over with its clothes slid off, a cracked
    // cosmetics counter, a display table, and goods spilled across the floor.
    const FLinearColor Plate(.013f,.012f,.011f,1.f),PlateText(.055f,.05f,.042f,1.f),Metal(.03f,.029f,.028f,1.f),Wire(.02f,.02f,.02f,.8f);
    const auto ZonePlate=[&](double X,double Y,double W,double Tilt,const TCHAR* Name,double Size)
    {
        S.Line(Structure,{{X+W*.15,Y-60},{X+W*.15,Y}},Wire,1.2);
        if(Tilt<8)S.Line(Structure,{{X+W*.85,Y-60},{X+W*.85,Y+Tilt}},Wire,1.2); // a steep tilt means that wire snapped
        S.Line(Structure,{{X,Y+Size*.7},{X+W,Y+Size*.7+Tilt}},Plate,Size*1.4);
        FSlateDrawElement::MakeText(Out,Structure+1,G.ToPaintGeometry(FVector2D(W,Size*1.6)*Scale,FSlateLayoutTransform(S.P(X+W*.2,Y+Tilt*.35))),FString(Name),
            FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),FMath::Max(6,int32(Size*Scale))),ESlateDrawEffect::None,PlateText*Tint);
    };
    ZonePlate(235,215,120,5,TEXT("의 류"),18);
    ZonePlate(565,290,58,0,TEXT("잡 화"),9);
    ZonePlate(1075,240,112,16,TEXT("화장품"),16);

    // The clothes rack lies across the left foreground; hangers still on the bar, garments slid down beside it.
    S.Line(Structure,{{60,712},{318,838}},Metal,5);S.Line(Structure,{{48,700},{84,760}},Metal,4);S.Line(Structure,{{300,828},{336,880}},Metal,4);
    const FLinearColor Cloth[]={FLinearColor(.013f,.0075f,.0075f,1.f),FLinearColor(.0075f,.0085f,.012f,1.f),FLinearColor(.011f,.011f,.008f,1.f),FLinearColor(.009f,.009f,.0095f,1.f)};
    for(int32 I=0;I<6;++I)
    {
        const double T=(I+.5)/6,X=60+258*T,Y=712+126*T;
        S.Line(Structure,{{X-9,Y+12},{X,Y+2},{X+9,Y+12},{X-9,Y+12}},FLinearColor(.035f,.034f,.032f,.8f),1.2);
        S.Line(Structure,{{X,Y+12},{X+Wear.FRandRange(-24.,24.),Y+Wear.FRandRange(42.,70.)}},Cloth[I%4],Wear.FRandRange(18.,28.));
    }
    S.Stain(Structure,150,860,90,Cloth[0]*FLinearColor(1,1,1,.6f),.28);S.Stain(Structure,370,872,70,Cloth[1]*FLinearColor(1,1,1,.6f),.25);

    // Shoe boxes spilled from a stack by the shelves, lids off.
    const FLinearColor BoxSide(.014f,.0115f,.009f,1.f),BoxLid(.019f,.016f,.012f,1.f);
    for(const FVector4& B:{FVector4(360,700,64,30),FVector4(392,672,64,30),FVector4(440,712,58,26),FVector4(330,744,52,24)})
    {S.Box(Structure,B.X,B.Y,B.Z,B.W,BoxSide);S.Box(Structure,B.X,B.Y,B.Z,4,BoxLid);}
    S.Line(Structure,{{470,752},{514,738}},BoxLid,7);S.Line(Structure,{{300,780},{332,800}},BoxLid,6); // lids kicked aside

    // A display table in the aisle, its stacked boxes half fallen.
    S.Box(Structure,452,582,92,5,Metal);S.Box(Structure,494,587,8,40,FLinearColor(.012f,.012f,.012f,1.f));S.Box(Structure,476,625,44,4,FLinearColor(.012f,.012f,.012f,1.f));
    S.Box(Structure,462,562,26,20,BoxSide);S.Box(Structure,490,568,22,14,BoxLid);S.Line(Structure,{{526,622},{548,630}},BoxSide,12);

    // The cosmetics counter: glass top cracked, a few bottles still standing, one rolled off into a dried spill.
    S.Box(Structure,1092,572,132,74,FLinearColor(.007f,.007f,.0075f,1.f));
    S.Box(Structure,1092,568,132,5,FLinearColor(.045f,.047f,.05f,.8f));
    S.Line(Structure,{{1130,570},{1146,600},{1139,626}},FLinearColor(.07f,.072f,.075f,.5f),1);S.Line(Structure,{{1146,600},{1170,592},{1186,612}},FLinearColor(.07f,.072f,.075f,.5f),1);
    for(const FVector2D& Bottle:{FVector2D(1102,552),FVector2D(1118,556),FVector2D(1196,550),FVector2D(1210,558)})S.Box(Structure,Bottle.X,Bottle.Y,8,568-Bottle.Y,FLinearColor(.03f,.022f,.026f,1.f));
    S.Stain(Structure,1180,668,46,FLinearColor(.02f,.008f,.011f,.5f),.3);S.Line(Structure,{{1168,664},{1184,668}},FLinearColor(.03f,.022f,.026f,1.f),6);

    // Shopping bags dropped by the fallen mannequin, one on its side.
    S.Box(Structure,946,688,36,42,FLinearColor(.015f,.013f,.011f,1.f));S.Line(Structure,{{954,688},{958,672},{970,672},{974,688}},FLinearColor(.02f,.018f,.016f,1.f),1.5);
    S.Line(Structure,{{990,730},{1040,738}},FLinearColor(.012f,.0085f,.0095f,1.f),24);S.Line(Structure,{{1040,726},{1054,730},{1052,746}},FLinearColor(.016f,.012f,.013f,1.f),1.5);

    // Small goods far down the aisle, and price tags, receipts and loose hangers all over the floor (smaller further away).
    for(int32 I=0;I<9;++I){const double X=Wear.FRandRange(650.,960.),Y=Wear.FRandRange(530.,575.),D=Wear.FRandRange(5.,11.);S.Box(Structure,X,Y,D*1.4,D,I%2?BoxSide:BoxLid);}
    for(int32 I=0;I<28;++I)
    {
        const double Y=Wear.FRandRange(545.,890.),K=(Y-430)/470,X=Wear.FRandRange(0.,1600.);
        if(X>1380)continue; // hidden behind the near mannequin anyway
        if(I%4==0)S.Line(Structure,{{X-10*K,Y},{X,Y-8*K},{X+10*K,Y},{X-10*K,Y}},FLinearColor(.034f,.033f,.031f,.7f),1);
        else S.Box(Structure,X,Y,Wear.FRandRange(9.,18.)*K,Wear.FRandRange(5.,10.)*K,FLinearColor(.05f,.047f,.04f,Wear.FRandRange(.12f,.28f)));
    }

    // Mannequins: one by the shelves, one far down the aisle under the last light, one close on the right missing an
    // arm, and one fallen face down in the aisle.
    S.Mannequin(Structure,600,520,150,FLinearColor(.035f,.03f,.026f,.5f));
    S.Mannequin(Structure,905,468,72,FLinearColor(.09f,.08f,.055f,.7f));
    S.Mannequin(Structure,1490,930,600,FLinearColor(.03f,.027f,.024f,.45f),true);
    S.Line(Structure,{{690,690},{860,712}},FLinearColor(.0032f,.0032f,.0034f,1.f),20);S.Disc(Structure,676,688,13,FLinearColor(.0032f,.0032f,.0034f,1.f));
    S.Line(Structure,{{860,712},{930,700}},FLinearColor(.0032f,.0032f,.0034f,1.f),9);

    // Smoke still hanging under the ceiling, ash in the air, then heavy darkness toward the edges.
    const int32 Air=Layer+2;
    for(int32 Band=0;Band<5;++Band)S.Box(Air,-400,Band*40,2400,42,FLinearColor(.02f,.019f,.018f,.07f-Band*.012f));
    FRandomStream Dust(1337);
    for(int32 I=0;I<260;++I){const double D=Dust.FRandRange(1.,2.5);S.Box(Air,Dust.FRandRange(0.,1600.),Dust.FRandRange(0.,900.),D,D,FLinearColor(.35f,.34f,.32f,Dust.FRandRange(.04f,.12f)));}
    for(int32 I=0;I<18;++I){const double D=Dust.FRandRange(1.5,2.5);S.Box(Air,Dust.FRandRange(650.,950.),Dust.FRandRange(420.,520.),D,D,FLinearColor(.5f,.12f,.04f,Dust.FRandRange(.15f,.35f)));} // last embers
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

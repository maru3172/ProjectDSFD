#include "ProjectDFSDUITheme.h"
#include "ProjectProject01LoginWidget.h"
#include "ProjectStoreBackdrop.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Sound/SoundWave.h"
#include "ProjectUIClickSound.h"
#include "UObject/StrongObjectPtr.h"
#include "ProjectProject01GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"

namespace
{
    // Static Slate geometry: no texture imports, timers, input surfaces or per-frame randomness.
    struct FDocumentWear
    {
        const FGeometry& Geometry;
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
        FLinearColor Tint;

        int32 Vertex(FVector2D P,FLinearColor C)
        {
            return Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                Geometry.GetAccumulatedRenderTransform(),FVector2f(P),FVector2f(.5f,.5f),(C*Tint).ToFColor(true)));
        }
        void Triangle(FVector2D A,FVector2D B,FVector2D C,FLinearColor Color)
        {
            const int32 I=Vertex(A,Color);Vertex(B,Color);Vertex(C,Color);
            Indices.Append({SlateIndex(I),SlateIndex(I+1),SlateIndex(I+2)});
        }
        // Axis-aligned quad; Near/Far colours let one edge fade out (vignette bands).
        void Quad(FVector2D Origin,FVector2D Size,FLinearColor A,FLinearColor B,FLinearColor C,FLinearColor D)
        {
            const int32 I=Vertex(Origin,A);Vertex(Origin+FVector2D(Size.X,0),B);Vertex(Origin+Size,C);Vertex(Origin+FVector2D(0,Size.Y),D);
            Indices.Append({SlateIndex(I),SlateIndex(I+1),SlateIndex(I+2),SlateIndex(I),SlateIndex(I+2),SlateIndex(I+3)});
        }
        void Quad(FVector2D Origin,FVector2D Size,FLinearColor Color){Quad(Origin,Size,Color,Color,Color,Color);}
        void Surface(FVector2D Origin,FVector2D Size)
        {
            // A soft, static light falloff gives the paper depth without adding objects behind it.
            const int32 I=Vertex(Origin,FLinearColor(.115f,.115f,.115f,.10f));
            Vertex(Origin+FVector2D(Size.X,0),FLinearColor(.085f,.085f,.085f,.025f));
            Vertex(Origin+Size,FLinearColor(0,0,0,.18f));
            Vertex(Origin+FVector2D(0,Size.Y),FLinearColor(0,0,0,.04f));
            Indices.Append({SlateIndex(I),SlateIndex(I+1),SlateIndex(I+2),SlateIndex(I),SlateIndex(I+2),SlateIndex(I+3)});
        }
        void Stain(FVector2D Center,FVector2D Radius,FLinearColor Color,int32 Seed)
        {
            FRandomStream Random(Seed);constexpr int32 Steps=18;
            const int32 Middle=Vertex(Center,Color);
            for(int32 I=0;I<Steps;++I)
            {
                const float Angle=I*2.f*PI/Steps,R=Random.FRandRange(.65f,1.15f);
                Vertex(Center+FVector2D(FMath::Cos(Angle)*Radius.X,FMath::Sin(Angle)*Radius.Y)*R,Color.CopyWithNewOpacity(0));
            }
            for(int32 I=0;I<Steps;++I)Indices.Append({SlateIndex(Middle),SlateIndex(Middle+1+I),SlateIndex(Middle+1+(I+1)%Steps)});
        }
        void Draw(FSlateWindowElementList& Out,int32 Layer)
        {
            if(Vertices.IsEmpty())return;
            const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
            FSlateDrawElement::MakeCustomVerts(Out,Layer,Resource,Vertices,Indices,nullptr,0,0);
        }
    };

    void PaintPaperWear(FSlateWindowElementList& Out,const FGeometry& G,int32 Layer,
        FVector2D Origin,FVector2D Size,float Scale,FLinearColor Tint,bool Folder,int32 Seed=419)
    {
        if(Size.X<40||Size.Y<60)return;
        FDocumentWear Wear{G,{}, {},Tint};FRandomStream Random(Seed);
        const float Top=(Folder?22.f:20.f)*Scale;
        if(Size.Y>Top+2*Scale)
            Wear.Surface(Origin+FVector2D(Scale,Top),FVector2D(Size.X-2*Scale,Size.Y-Top-Scale));
        const FLinearColor Soot(.003f,.003f,.003f,.72f),Water(.125f,.125f,.125f,.14f);
        // Marks live in the binding, header and outer margins, below all text and controls.
        const int32 Marks=Random.RandRange(12,19);
        for(int32 I=0;I<Marks;++I)
        {
            const bool Left=Random.FRand()<.55f;
            const FVector2D P=Origin+FVector2D(Left?7*Scale:Size.X-7*Scale,Size.Y*Random.FRandRange(.12f,.93f));
            const auto Color=Random.FRand()<.65f?Soot:Water;
            Wear.Stain(P,FVector2D(Random.FRandRange(9,32)*Scale,Random.FRandRange(18,92)*Scale),Color.CopyWithNewOpacity(Color.A*Random.FRandRange(.5f,1.f)),Seed+110+I);
        }
        for(int32 I=0;I<5;++I)
            Wear.Stain(Origin+FVector2D(Size.X*Random.FRandRange(.05f,.94f),Size.Y-4*Scale),FVector2D(Random.FRandRange(35,90)*Scale,15*Scale),Water,240+I);
        const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        auto Lines=[&](TArray<FVector2D> Points,FLinearColor Color,float Width=1.f)
        {for(auto& P:Points)P=Origin+P;FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color*Tint,true,Width*Scale);};
        const FLinearColor Fiber(.23f,.23f,.23f,.38f),Cut=ProjectDFSDUI::Canvas();
        // Small irregular missing sections, rather than decorative sheets beside the menu.
        TArray<float> LeftCuts,RightCuts;
        const int32 Tears=Random.RandRange(3,6);
        for(int32 I=0;I<Tears;++I)
        {
            const bool Left=Random.FRand()<.5f;const float X=Left?0:Size.X,Sign=Left?1.f:-1.f;
            const float Fraction=Random.FRandRange(.15f,.94f);auto& Cuts=Left?LeftCuts:RightCuts;
            if(Cuts.ContainsByPredicate([Fraction](float Previous){return FMath::Abs(Previous-Fraction)<.08f;}))continue;
            Cuts.Add(Fraction);
            const float Y=Size.Y*Fraction,Depth=Random.FRandRange(4.f,Folder?11.f:8.f)*Scale;
            const FVector2D A(X,Y-Random.FRandRange(5,10)*Scale),B(X+Sign*Depth,Y-2*Scale),C(X+Sign*Depth*Random.FRandRange(.2f,.6f),Y+3*Scale),D(X,Y+Random.FRandRange(5,13)*Scale);
            Wear.Stain(Origin+FVector2D(X,Y),FVector2D(Depth*1.6f,17*Scale),FLinearColor(0,0,0,.48f),Seed+530+I);
            Wear.Triangle(Origin+A,Origin+B,Origin+C,Cut);Wear.Triangle(Origin+A,Origin+C,Origin+D,Cut);
            Lines({A,B,C,D},Fiber,.65f);
        }
        const FVector2D Corner(Size.X,Folder?22*Scale:0);
        Wear.Triangle(Origin+Corner+FVector2D(-18*Scale,0),Origin+Corner,Origin+Corner+FVector2D(0,17*Scale),FLinearColor(.066f,.066f,.066f,.7f));
        Lines({Corner+FVector2D(-18*Scale,0),Corner+FVector2D(0,17*Scale)},Fiber,.8f);
        // Worn paper fibres and short water trails are deterministic and deliberately sparse.
        const int32 Fibers=Random.RandRange(16,25);
        for(int32 I=0;I<Fibers;++I)
        {
            const float Y=Random.FRandRange(.08f,.94f)*Size.Y;
            const float X=Random.FRand()<.5f?Size.X-Random.FRandRange(4,12)*Scale:Random.FRandRange(4,12)*Scale;
            Lines({{X,Y},{X+Random.FRandRange(-3,3)*Scale,Y+Random.FRandRange(3,11)*Scale}},Fiber.CopyWithNewOpacity(.13f),.6f);
        }
        Wear.Draw(Out,Layer);
    }


}

namespace ProjectDFSDUI
{
    static FSlateSound ClickSound()
    {
        static TStrongObjectPtr<UProjectUIClickSound> Wave;
        if(!Wave.IsValid())
        {
            Wave.Reset(NewObject<UProjectUIClickSound>(GetTransientPackage()));
        }
        if(auto* Settings=UProjectProject01GameUserSettings::Get())Wave->SoundClassObject=Settings->GetUISoundClass();
        FSlateSound Sound;Sound.SetResourceObject(Wave.Get());return Sound;
    }
	FLinearColor Canvas(){return FLinearColor(.008f,.008f,.008f,1);}
	FLinearColor Panel(){return FLinearColor(.024f,.024f,.024f,1);}
    static FLinearColor Paper(){return FLinearColor(.030f,.030f,.030f,1);}
    static FLinearColor PaperEdge(){return FLinearColor(.15f,.15f,.15f,.65f);}
	FLinearColor Text(){return FLinearColor(.90f,.90f,.90f,1);}
	FLinearColor Muted(){return FLinearColor(.50f,.50f,.50f,1);}
	// Slightly faded tape red: a little less saturated than the title's original response colour.
	FLinearColor Accent(){return FLinearColor(.40f,.12f,.09f,1);}
	FLinearColor Danger(){return FLinearColor(1,.32f,.22f,1);}
	const FButtonStyle& TitleButtonStyle()
	{
		static const FButtonStyle Style=[]
		{
			auto S=FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
			// The title's original quiet translucent controls are the reference, not gray boxed buttons.
			S.SetNormal(FSlateColorBrush(FLinearColor(.014f,.014f,.014f,.25f)))
			 .SetHovered(FSlateColorBrush(FLinearColor(.10f,.10f,.10f,.85f)))
			 .SetPressed(FSlateColorBrush(FLinearColor(.065f,.065f,.065f,.92f)))
			 .SetDisabled(FSlateColorBrush(FLinearColor(.014f,.014f,.014f,.1f)))
			 .SetNormalForeground(Text()).SetHoveredForeground(Text())
			 .SetPressedForeground(Text()).SetDisabledForeground(Muted())
             .SetNormalPadding(FMargin(14,8)).SetPressedPadding(FMargin(15,9));S.SetPressedSound(ClickSound());return S;
		}();
		return Style;
	}
	const FButtonStyle& ButtonStyle(bool bSelected)
	{
		static const FButtonStyle Normal=[]
		{
			auto S=TitleButtonStyle();
			// Echo the title's charcoal surfaces and warm red response, with a readable resting edge.
			S.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.054f,.054f,.054f,.98f),2.f,FLinearColor(.145f,.145f,.145f,.8f),1.f))
             .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.09f,.09f,.09f),2.f,FLinearColor(.23f,.23f,.23f),1.f))
             .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.065f,.065f,.065f),2.f,Accent(),1.f))
			 .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.0202f,0.0202f,0.0202f,.85f),2.f,FLinearColor(0.0560f,0.0560f,0.0560f,.6f),1.f));
			return S;
		}();
		static const FButtonStyle Selected=[]
		{
			auto S=Normal;
			S.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.075f,.075f,.075f),2.f,Accent(),1.5f));return S;
		}();
		return bSelected?Selected:Normal;
	}
	const FEditableTextBoxStyle& InputStyle()
	{
		static const FEditableTextBoxStyle Style=[]
		{
			auto S=FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
			S.SetBackgroundImageNormal(FSlateRoundedBoxBrush(FLinearColor(0.0110f,0.0110f,0.0110f),2.f,FLinearColor(0.1221f,0.1221f,0.1221f),1.f))
			 .SetBackgroundImageHovered(FSlateRoundedBoxBrush(Panel(),2.f,FLinearColor(.15f,.15f,.15f),1.f))
			 .SetBackgroundImageFocused(FSlateRoundedBoxBrush(Panel(),2.f,Accent(),1.5f))
			 .SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(FLinearColor(0.0142f,0.0142f,0.0142f),2.f,FLinearColor(0.0660f,0.0660f,0.0660f),1.f))
			 .SetForegroundColor(Text()).SetFocusedForegroundColor(Text()).SetReadOnlyForegroundColor(Text())
			 .SetBackgroundColor(FLinearColor::White).SetPadding(FMargin(12,10));
			S.TextStyle.SetColorAndOpacity(Text()).SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18))
			 .SetHighlightColor(Accent()).SetShadowOffset(FVector2D::ZeroVector);return S;
		}();return Style;
	}
	const FComboButtonStyle& ComboButtonStyle()
	{
		static const FComboButtonStyle Style=[]
		{
			auto S=FCoreStyle::Get().GetWidgetStyle<FComboButtonStyle>(TEXT("ComboButton"));
			S.SetButtonStyle(ButtonStyle()).SetMenuBorderBrush(FSlateRoundedBoxBrush(Panel(),2.f,FLinearColor(0.1474f,0.1474f,0.1474f),1.f));
			S.DownArrowImage.TintColor=Text();return S;
		}();return Style;
	}
	const FTableRowStyle& RowStyle()
	{
		static const FTableRowStyle Style=[]
		{
			auto S=FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>(TEXT("TableView.Row"));
			S.SetEvenRowBackgroundBrush(FSlateColorBrush(Panel())).SetOddRowBackgroundBrush(FSlateColorBrush(Panel()))
			 .SetEvenRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(.10f,.055f,.045f)))
			 .SetOddRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(.10f,.055f,.045f)))
			 .SetActiveBrush(FSlateColorBrush(FLinearColor(.15f,.045f,.035f)))
			 .SetInactiveBrush(FSlateColorBrush(FLinearColor(.075f,.075f,.075f)))
			 .SetActiveHoveredBrush(FSlateColorBrush(FLinearColor(.18f,.06f,.04f)))
			 .SetInactiveHoveredBrush(FSlateColorBrush(FLinearColor(.15f,.05f,.035f)))
			 .SetTextColor(Text()).SetSelectedTextColor(Text());return S;
		}();return Style;
	}
	void Apply(UButton* B,bool bSelected)
	{
		if(!B)return;B->SetStyle(ButtonStyle(bSelected));B->SetBackgroundColor(FLinearColor::White);
		B->SetColorAndOpacity(FLinearColor::White);
	}
	void Apply(UEditableTextBox* I)
	{
		if(!I)return;auto S=InputStyle();S.TextStyle.SetFont(I->GetWidgetStyle().TextStyle.Font);
		I->SetWidgetStyle(S);I->SetForegroundColor(Text());
	}
	void Apply(UMultiLineEditableTextBox* I)
	{
		if(!I)return;auto S=InputStyle();S.TextStyle.SetFont(I->WidgetStyle.TextStyle.Font);
		I->WidgetStyle=S;I->SetTextStyle(S.TextStyle);I->SetForegroundColor(Text());
	}
	void Apply(UComboBoxString* C)
	{
		if(!C)return;auto S=C->GetWidgetStyle();S.SetComboButtonStyle(ComboButtonStyle());
		C->SetWidgetStyle(S);C->SetItemStyle(RowStyle());
	}
	void Apply(UCheckBox* C)
	{
		if(!C)return;auto S=C->GetWidgetStyle();
		S.UncheckedImage=FSlateRoundedBoxBrush(FLinearColor(0.0110f,0.0110f,0.0110f),2.f,FLinearColor(0.2127f,0.2127f,0.2127f),1.f,FVector2D(20,20));
		S.UncheckedHoveredImage=FSlateRoundedBoxBrush(Panel(),2.f,Accent(),1.5f,FVector2D(20,20));
		S.UncheckedPressedImage=S.UncheckedHoveredImage;
		S.CheckedImage.TintColor=FLinearColor(.85f,.42f,.29f);S.CheckedHoveredImage.TintColor=Danger();
		S.CheckedPressedImage.TintColor=Danger();C->SetWidgetStyle(S);
	}
	void ApplyTree(UWidgetTree* Tree)
	{
		if(!Tree)return;
        Tree->ForEachWidget([](UWidget* W)
		{
			if(auto* B=Cast<UButton>(W)){if(!Cast<UBorder>(B->GetContent()))Apply(B);}
			else if(auto* E=Cast<UEditableTextBox>(W))Apply(E);
			else if(auto* M=Cast<UMultiLineEditableTextBox>(W))Apply(M);
			else if(auto* Combo=Cast<UComboBoxString>(W))Apply(Combo);
			else if(auto* Check=Cast<UCheckBox>(W))Apply(Check);
			else if(auto* S=Cast<USlider>(W)){S->SetSliderBarColor(FLinearColor(.08f,.09f,.11f));S->SetSliderHandleColor(FLinearColor(.65f,.28f,.20f));}
			else if(auto* P=Cast<UProgressBar>(W))P->SetFillColorAndOpacity(FLinearColor(.65f,.28f,.20f));
			else if(auto* T=Cast<UTextBlock>(W))
			{
				const auto C=T->GetColorAndOpacity().GetSpecifiedColor();
				const float Max=FMath::Max3(C.R,C.G,C.B),Min=FMath::Min3(C.R,C.G,C.B);
				if(Max<.01f||(Max>.8f&&Max-Min<.08f))T->SetColorAndOpacity(
					Text().CopyWithNewOpacity(C.A));
				else if(C.R>.9f&&C.G<.01f&&C.B<.01f)T->SetColorAndOpacity(Danger().CopyWithNewOpacity(C.A));
			}
			else if(auto* Border=Cast<UBorder>(W))
			{
				const auto C=Border->GetBrushColor();
				// Keep exact black/transparency: these include full-screen fades and dimmers.
				const float Max=FMath::Max3(C.R,C.G,C.B);
				if(C.A>0&&Max>.001f&&Max-FMath::Min3(C.R,C.G,C.B)<Max*.25f)
				{
					// Quiet glass / brushed-metal panels; full-screen black fades and
					// transparent text containers keep their exact original brushes.
					Border->SetBrush(FSlateRoundedBoxBrush(Panel().CopyWithNewOpacity(C.A),2.f,FLinearColor(0.1474f,0.1474f,0.1474f,.7f),1.f));
					Border->SetBrushColor(FLinearColor::White);
				}
			}
		});
	}
	void FramePage(UWidgetTree* Tree,float Width)
	{
		if(!Tree||!Tree->RootWidget||Cast<UProjectDFSDPageFrame>(Tree->RootWidget))return;
		auto* OldRoot=Tree->RootWidget.Get();auto* Frame=Tree->ConstructWidget<UProjectDFSDPageFrame>();
		Frame->ContentWidth=Width;
        Frame->BottomClearance=Tree->GetTypedOuter<UProjectProject01LobbyWidget>()?128.f:32.f;
		if(auto* Overlay=Cast<UOverlay>(OldRoot);Overlay&&Overlay->GetChildrenCount()>0)
		{
			// Frame only the menu layer. Countdown fades, social panels and confirmation overlays
			// remain siblings covering the entire viewport, not a clipped content rectangle.
			auto* Content=Overlay->GetChildAt(0);Overlay->RemoveChildAt(0);Frame->SetContent(Content);
			auto* Slot=CastChecked<UOverlaySlot>(Overlay->InsertChildAt(0,Frame));
			Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);
		}
		else{Frame->SetContent(OldRoot);Tree->RootWidget=Frame;}
	}

	FSlateFontInfo Mono(int32 Size){return FCoreStyle::GetDefaultFontStyle(TEXT("Mono"),Size);}
	float TapeScanlineOpacity(){return .028f;}
	FString RecordStamp(){return TEXT("[RECORD 0] 202X-XX-XX 18:24:01");}
	FString ChannelLabel(int32 Channel){return FString::Printf(TEXT("REC  CH-%02d"),FMath::Clamp(Channel,1,99));}
	void PaintTape(FSlateWindowElementList& Out,const FGeometry& G,int32 Layer,FVector2D Origin,FVector2D Size,float Scale,FLinearColor Tint,int32 Seed,float Strength)
	{
		if(Size.X<4||Size.Y<4||Strength<=0)return;
		// Strength < 1 for surfaces over pure black, where the same rules would read much harsher.
		Tint.A*=FMath::Min(Strength,1.f);
		FDocumentWear Tape{G,{},{},Tint};
		// Fixed horizontal scanlines: faint light rules read on the near-black surfaces.
		const float Step=FMath::Max(2.f,3.f*Scale),Thick=FMath::Max(.6f,Scale);
		const FLinearColor Line(1,1,1,TapeScanlineOpacity());
		for(float Y=Step*.5f;Y<Size.Y-Thick;Y+=Step)Tape.Quad(Origin+FVector2D(0,Y),FVector2D(Size.X,Thick),Line);
		// Film grain from a fixed seed: the same specks every frame, so nothing crawls or blinks.
		FRandomStream Random(Seed);
		const int32 Specks=FMath::Clamp(int32(Size.X*Size.Y/(1500.f*FMath::Max(.25f,Scale*Scale))),0,1200);
		for(int32 I=0;I<Specks;++I)
		{
			const FVector2D P(Random.FRandRange(0,Size.X-2*Scale),Random.FRandRange(0,Size.Y-2*Scale));
			const float S=Random.FRandRange(.8f,1.7f)*Scale;
			const bool Light=Random.FRand()<.45f;
			Tape.Quad(Origin+P,FVector2D(S,S),Light?FLinearColor(1,1,1,Random.FRandRange(.025f,.06f)):FLinearColor(0,0,0,Random.FRandRange(.10f,.22f)));
		}
		Tape.Draw(Out,Layer);
	}
	void PaintVignette(FSlateWindowElementList& Out,const FGeometry& G,int32 Layer,FVector2D Size,FLinearColor Tint)
	{
		if(Size.X<8||Size.Y<8)return;
		FDocumentWear Edge{G,{},{},Tint};
		const FLinearColor Dark(0,0,0,.42f),Clear(0,0,0,0);
		const float H=Size.Y*.16f,W=Size.X*.10f;
		Edge.Quad(FVector2D::ZeroVector,FVector2D(Size.X,H),Dark,Dark,Clear,Clear);
		Edge.Quad(FVector2D(0,Size.Y-H),FVector2D(Size.X,H),Clear,Clear,Dark,Dark);
		Edge.Quad(FVector2D::ZeroVector,FVector2D(W,Size.Y),Dark,Clear,Clear,Dark);
		Edge.Quad(FVector2D(Size.X-W,0),FVector2D(W,Size.Y),Clear,Dark,Dark,Clear);
		Edge.Draw(Out,Layer);
	}
	void PaintRecordMarks(FSlateWindowElementList& Out,const FGeometry& G,int32 Layer,float Left,float Right,float Y,float Scale,FLinearColor Tint,int32 Channel,int32 FontSize)
	{
		if(!FSlateApplication::IsInitialized()||Right-Left<40*Scale)return;
		const FSlateFontInfo Font=Mono(FontSize);const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FString Label=ChannelLabel(Channel),Stamp=RecordStamp();
		const FVector2D LabelSize=Measure->Measure(Label,Font),StampSize=Measure->Measure(Stamp,Font);
		const FLinearColor Ink=FLinearColor(.62f,.62f,.62f,1)*Tint;
		// A steady red recording dot: static, never blinking.
		static const FSlateRoundedBoxBrush Dot(FLinearColor::White,4.f);
		const float DotSize=FMath::Max(4.f,FontSize*.62f)*Scale;
		FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(DotSize,DotSize),FSlateLayoutTransform(FVector2D(Left,Y+(LabelSize.Y*Scale-DotSize)*.5f))),&Dot,ESlateDrawEffect::None,FLinearColor(.58f,.10f,.08f,1)*Tint);
		const float LabelX=Left+DotSize+5*Scale;
		FSlateDrawElement::MakeText(Out,Layer,G.ToPaintGeometry(LabelSize,FSlateLayoutTransform(Scale,FVector2D(LabelX,Y))),Label,Font,ESlateDrawEffect::None,Ink);
		// The stamp is right-aligned and dropped when it would touch the channel label.
		const float StampX=Right-StampSize.X*Scale;
		if(StampX>LabelX+LabelSize.X*Scale+16*Scale)
			FSlateDrawElement::MakeText(Out,Layer,G.ToPaintGeometry(StampSize,FSlateLayoutTransform(Scale,FVector2D(StampX,Y))),Stamp,Font,ESlateDrawEffect::None,Ink);
	}
}

class SProjectDFSDTapeCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProjectDFSDTapeCard):_Channel(1),_Background(FLinearColor::Transparent),_Padding(FMargin(14,10)),_FollowChild(false){}
		SLATE_ARGUMENT(int32,Channel) SLATE_ARGUMENT(FLinearColor,Background) SLATE_ARGUMENT(FMargin,Padding) SLATE_ARGUMENT(bool,FollowChild)
		SLATE_DEFAULT_SLOT(FArguments,Content)
	SLATE_END_ARGS()
	static constexpr float Header=16.f;
	void Construct(const FArguments& A)
	{
		Channel=A._Channel;Background=A._Background;Padding=A._Padding;bFollowChild=A._FollowChild;
		// An opaque card absorbs clicks on its own surface exactly like the SBorder it replaces.
		// A transparent frame never takes input itself; its content keeps its own hit testing.
		SetVisibility(Background.A>0?EVisibility::Visible:EVisibility::SelfHitTestInvisible);
		FMargin Inner=Padding;Inner.Top+=Header;
		ChildSlot.Padding(Inner)[A._Content.Widget];
	}
	virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bEnabled)const override
	{
		FLinearColor Tint=Style.GetColorAndOpacityTint();
		if(bFollowChild)
		{
			// Follow the wrapped menu's own fade: hidden while it is hidden, as transparent as it is.
			const auto& Child=ChildSlot.GetWidget();
			if(!Child->GetVisibility().IsVisible())return SCompoundWidget::OnPaint(Args,G,Clip,Out,Layer,Style,bEnabled);
			Tint.A*=Child->GetRenderOpacity();
		}
		const FVector2D Size=G.GetLocalSize();
		if(Background.A>0)FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,Background*Tint);
		const bool bOpaque=Background.A>0;
		ProjectDFSDUI::PaintTape(Out,G,Layer+1,FVector2D::ZeroVector,Size,1.f,Tint,Channel*977+31,bOpaque?1.f:.35f);
		ProjectDFSDUI::PaintRecordMarks(Out,G,Layer+2,Padding.Left,float(Size.X)-Padding.Right,FMath::Max(2.f,Padding.Top*.5f),1.f,Tint,Channel,bOpaque?9:10);
		return SCompoundWidget::OnPaint(Args,G,Clip,Out,Layer+3,Style,bEnabled);
	}
private:
	int32 Channel=1;FLinearColor Background;FMargin Padding;bool bFollowChild=false;
};
TSharedRef<SWidget> ProjectDFSDUI::TapeCard(TSharedRef<SWidget> Content,int32 Channel,FLinearColor Background,FMargin Padding)
{
	return SNew(SProjectDFSDTapeCard).Channel(Channel).Background(Background).Padding(Padding)[Content];
}
TSharedRef<SWidget> UProjectDFSDTapeFrame::RebuildWidget()
{
	return SNew(SProjectDFSDTapeCard).Channel(Channel).Padding(FMargin(12,8)).FollowChild(true)[GetContent()?GetContent()->TakeWidget():SNullWidget::NullWidget];
}

class SProjectDFSDPage final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProjectDFSDPage):_Width(1120),_Bottom(32),_WearSeed(419),_Dossier(false){} SLATE_ARGUMENT(float,Width) SLATE_ARGUMENT(float,Bottom) SLATE_ARGUMENT(int32,WearSeed) SLATE_ARGUMENT(bool,Dossier) SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
	void Construct(const FArguments& Args){Width=Args._Width*1.20f;Bottom=Args._Bottom;WearSeed=Args._WearSeed;Dossier=Args._Dossier;ChildSlot[Args._Content.Widget];}
	virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(Width+64,720);}
	virtual void OnArrangeChildren(const FGeometry& G,FArrangedChildren& Children)const override
	{
		const FVector2D Size=G.GetLocalSize();const float Dpi=FMath::Max(.01f,G.GetAccumulatedLayoutTransform().GetScale());
		// Size the menu like the title's fitted canvas instead of shrinking it twice through viewport DPI.
		const float Fit=FMath::Clamp(static_cast<float>(Size.Y)*Dpi/900.f,.75f,1.1f)/Dpi;
		const float Margin=(Size.X*Dpi<600?16.f:32.f)/Dpi;
		const FVector2D Inner(FMath::Max(1.f,FMath::Min(Width,(static_cast<float>(Size.X)-2*Margin)/Fit)),FMath::Max(1.0,Size.Y/Fit-52-Bottom));
		Children.AddWidget(G.MakeChild(ChildSlot.GetWidget(),Inner,FSlateLayoutTransform(Fit,FVector2D((Size.X-Inner.X*Fit)/2,52*Fit))));
	}
	virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Rect,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bEnabled)const override
	{
		const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));const auto Size=G.GetLocalSize();
		const float Dpi=FMath::Max(.01f,G.GetAccumulatedLayoutTransform().GetScale());
		const float Fit=FMath::Clamp(static_cast<float>(Size.Y)*Dpi/900.f,.75f,1.1f)/Dpi;
		const auto Tint=Style.GetColorAndOpacityTint();
		ProjectDFSDUI::PaintStoreBackdrop(Out,G,Layer,Tint); // Same abandoned-store backdrop as the single-player save screen.
		const float Left=FMath::Max(16.f/Dpi,(static_cast<float>(Size.X)-Width*Fit)/2);
		// Dossiers carry the recording dot in their tab instead of the short accent rule.
		if(!Dossier)FSlateDrawElement::MakeBox(Out,Layer+4,G.ToPaintGeometry(FVector2D(64*Fit,2*Fit),FSlateLayoutTransform(FVector2D(Left+6*Fit,28*Fit))),White,ESlateDrawEffect::None,ProjectDFSDUI::Accent()*Tint);
        // Restrained file-folder rules and a document header, not a bright paper texture.
        const float Right=FMath::Min(float(Size.X)-16.f/Dpi,Left+Width*Fit);
        const float FrameLeft=FMath::Max(8.f/Dpi,Left-10*Fit),FrameRight=FMath::Min(float(Size.X)-8.f/Dpi,Right+10*Fit);
        const float FrameBottom=FMath::Max(60.f*Fit,float(Size.Y)-Bottom*Fit);
        // Dossiers share the save covers' raised left tab; settings/login use a simpler sheet.
        const float Top=16*Fit,BodyTop=Dossier?38*Fit:Top;
        const TArray<FVector2D> Folder=Dossier
            ?TArray<FVector2D>{{FrameLeft,Top},{FrameLeft+125*Fit,Top},{FrameLeft+143*Fit,BodyTop},{FrameRight,BodyTop},{FrameRight,FrameBottom},{FrameLeft,FrameBottom},{FrameLeft,Top}}
            :TArray<FVector2D>{{FrameLeft,Top},{FrameRight-16*Fit,Top},{FrameRight,Top+16*Fit},{FrameRight,FrameBottom},{FrameLeft,FrameBottom},{FrameLeft,Top}};
        const FLinearColor Paper=ProjectDFSDUI::Paper();
        FSlateDrawElement::MakeBox(Out,Layer+3,G.ToPaintGeometry(FVector2D(FrameRight-FrameLeft,FrameBottom-BodyTop),FSlateLayoutTransform(FVector2D(FrameLeft,BodyTop))),White,ESlateDrawEffect::None,Paper*Tint);
        if(Dossier)FSlateDrawElement::MakeBox(Out,Layer+3,G.ToPaintGeometry(FVector2D(125*Fit,22*Fit),FSlateLayoutTransform(FVector2D(FrameLeft,Top))),White,ESlateDrawEffect::None,Paper*Tint);
        FSlateDrawElement::MakeLines(Out,Layer+4,G.ToPaintGeometry(),Folder,ESlateDrawEffect::None,ProjectDFSDUI::PaperEdge()*Tint,true,1.f/Dpi);
        FSlateDrawElement::MakeBox(Out,Layer+4,G.ToPaintGeometry(FVector2D(2*Fit,FMath::Max(1.f,FrameBottom-60*Fit)),FSlateLayoutTransform(FVector2D(FrameLeft+5*Fit,60*Fit))),White,ESlateDrawEffect::None,FLinearColor(.12f,.045f,.035f,.6f)*Tint);
        FSlateDrawElement::MakeBox(Out,Layer+4,G.ToPaintGeometry(FVector2D(FMath::Max(1.f,Right-Left),1.f/Dpi),FSlateLayoutTransform(FVector2D(Left,47*Fit))),White,ESlateDrawEffect::None,FLinearColor(.10f,.105f,.11f,.8f)*Tint);
        // Analog tape: fixed scanlines/grain on the sheet, darkened screen edges, and a recording
        // header in the band above the content (content starts at 52*Fit, so nothing overlaps).
        ProjectDFSDUI::PaintTape(Out,G,Layer+4,FVector2D(FrameLeft,BodyTop),FVector2D(FrameRight-FrameLeft,FrameBottom-BodyTop),Fit,Tint,WearSeed+71);
        PaintPaperWear(Out,G,Layer+5,FVector2D(FrameLeft,16*Fit),FVector2D(FrameRight-FrameLeft,FrameBottom-16*Fit),Fit,Tint,Dossier,WearSeed);
        ProjectDFSDUI::PaintVignette(Out,G,Layer+6,Size,Tint);
        const int32 Channel=int32(uint32(WearSeed)%12u)+1;
        if(Dossier)ProjectDFSDUI::PaintRecordMarks(Out,G,Layer+6,FrameLeft+12*Fit,Right,19*Fit,Fit,Tint,Channel);
        else ProjectDFSDUI::PaintRecordMarks(Out,G,Layer+6,Left+80*Fit,Right,21*Fit,Fit,Tint,Channel);
		return SCompoundWidget::OnPaint(Args,G,Rect,Out,Layer+7,Style,bEnabled);
	}
private:float Width=1120,Bottom=32;int32 WearSeed=419;bool Dossier=false;
};

ProjectDFSDUI::FPageMetrics ProjectDFSDUI::PageMetrics(const FGeometry& G,float Width)
{
    const FVector2D Size=G.GetLocalSize();const float Dpi=FMath::Max(.01f,G.GetAccumulatedLayoutTransform().GetScale());
    FPageMetrics M;M.Scale=FMath::Clamp(float(Size.Y)*Dpi/900.f,.75f,1.1f)/Dpi;
    const float Margin=(Size.X*Dpi<600?16.f:32.f)/Dpi;
    M.Width=FMath::Max(1.f,FMath::Min(Width*1.20f,(float(Size.X)-2*Margin)/M.Scale));
    M.Left=(Size.X-M.Width*M.Scale)*.5f;return M;
}
ProjectDFSDUI::FPageMetrics ProjectDFSDUI::FooterMetrics(const FGeometry& G)
{
    auto M=PageMetrics(G);
    const float Dpi=FMath::Max(.01f,G.GetAccumulatedLayoutTransform().GetScale());
    const float ScreenWidth=G.GetLocalSize().X;
    // Match the existing upper frame's outer edges. Its size and layout are unchanged.
    const float Left=FMath::Max(16.f/Dpi,(ScreenWidth-1120.f*1.20f*M.Scale)*.5f);
    const float Right=FMath::Min(ScreenWidth-16.f/Dpi,Left+1120.f*1.20f*M.Scale);
    M.Left=FMath::Max(8.f/Dpi,Left-10*M.Scale);
    M.Width=(FMath::Min(ScreenWidth-8.f/Dpi,Right+10*M.Scale)-M.Left)/M.Scale;
    return M;
}
class SProjectDFSDFooter final:public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectDFSDFooter){} SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
    void Construct(const FArguments& A){SetVisibility(EVisibility::SelfHitTestInvisible);ChildSlot[A._Content.Widget];}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(1120,104);}
    virtual void OnArrangeChildren(const FGeometry& G,FArrangedChildren& Children)const override
    {
        const auto M=ProjectDFSDUI::FooterMetrics(G);
        Children.AddWidget(G.MakeChild(ChildSlot.GetWidget(),FVector2D(M.Width,104),FSlateLayoutTransform(M.Scale,FVector2D(M.Left,G.GetLocalSize().Y-114*M.Scale))));
    }
};
TSharedRef<SWidget> ProjectDFSDUI::FooterSlate(TSharedRef<SWidget> Content){return SNew(SProjectDFSDFooter)[Content];}

class SProjectDFSDIcon final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectDFSDIcon):_Kind(ProjectDFSDUI::EIcon::Profile),_Size(24){} SLATE_ARGUMENT(ProjectDFSDUI::EIcon,Kind) SLATE_ARGUMENT(float,Size) SLATE_END_ARGS()
    void Construct(const FArguments& A){Kind=A._Kind;Size=A._Size;SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(Size,Size);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const override
    {
        const float Scale=FMath::Min(float(G.GetLocalSize().X),float(G.GetLocalSize().Y))/24.f;
        const auto Offset=(G.GetLocalSize()-FVector2D(24*Scale,24*Scale))*.5;
        const FLinearColor Color=ProjectDFSDUI::Text()*Style.GetColorAndOpacityTint()*(Enabled?FLinearColor::White:FLinearColor(.35f,.35f,.35f,.6f));
        auto Lines=[&](TArray<FVector2D> Points){for(auto& P:Points)P=Offset+P*Scale;FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,1.6f*Scale);};
        auto Circle=[&](FVector2D C,float R){TArray<FVector2D> P;for(int32 I=0;I<=16;++I){const float A=I*2.f*PI/16;P.Add(C+FVector2D(FMath::Cos(A)*R,FMath::Sin(A)*R));}Lines(P);};
        switch(Kind)
        {
        case ProjectDFSDUI::EIcon::Add:Lines({{4,12},{20,12}});Lines({{12,4},{12,20}});break;
        case ProjectDFSDUI::EIcon::Phone:Lines({{7,2},{17,2},{17,22},{7,22},{7,2}});Lines({{10,4.5},{14,4.5}});Circle({12,19},.8f);break;
        case ProjectDFSDUI::EIcon::Whisper:Lines({{3,4},{21,4},{21,17},{12,17},{6,21},{6,17},{3,17},{3,4}});Lines({{7,9},{17,9}});Lines({{7,13},{14,13}});break;
        case ProjectDFSDUI::EIcon::Report:Lines({{12,2},{23,21},{1,21},{12,2}});Lines({{12,8},{12,14}});Circle({12,18},.6f);break;
        case ProjectDFSDUI::EIcon::Bell:Lines({{4,18},{7,14},{7,9},{8,6},{12,4},{16,6},{17,9},{17,14},{20,18},{4,18}});Lines({{10,21},{14,21}});break;
        case ProjectDFSDUI::EIcon::Party:
        case ProjectDFSDUI::EIcon::Friends:Circle({8,7},3.5);Circle({17,8},3);Lines({{1,21},{2,17},{5,14},{11,14},{14,17},{15,21}});Lines({{15,14},{20,14},{23,18},{23,21}});break;
        default:Circle({12,7},4);Lines({{3,22},{4,17},{8,14},{16,14},{20,17},{21,22}});break;
        }
        return Layer;
    }
private:ProjectDFSDUI::EIcon Kind;float Size=24;
};
TSharedRef<SWidget> ProjectDFSDUI::Icon(EIcon Kind,float Size){return SNew(SProjectDFSDIcon).Kind(Kind).Size(Size);}

TSharedRef<SWidget> UProjectDFSDPageFrame::RebuildWidget()
{
    const auto* Owner=GetTypedOuter<UUserWidget>();
    const int32 Seed=Owner?int32(FCrc::StrCrc32(*Owner->GetClass()->GetPathName())):419;
	return SNew(SProjectDFSDPage).Width(ContentWidth).Bottom(BottomClearance).WearSeed(Seed).Dossier(Owner&&Owner->IsA<UProjectProject01LobbyWidget>())[GetContent()?GetContent()->TakeWidget():SNullWidget::NullWidget];
}
TSharedRef<SWidget> ProjectDFSDUI::FrameSlate(TSharedRef<SWidget> Content,float Width,float Bottom)
{
    return SNew(SProjectDFSDPage).Width(Width).Bottom(Bottom).Dossier(true)[Content];
}

UProjectDFSDComboBox::UProjectDFSDComboBox(){InitForegroundColor(ProjectDFSDUI::Text());}

class SProjectDFSDCaseFolder final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProjectDFSDCaseFolder):_WearSeed(731){} SLATE_ARGUMENT(int32,WearSeed) SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
	void Construct(const FArguments& Args){WearSeed=Args._WearSeed;ChildSlot.Padding(FMargin(22,42,22,20))[Args._Content.Widget];}
	virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Rect,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const override
	{
		const auto Size=G.GetLocalSize();const auto Tint=Style.GetColorAndOpacityTint();const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
		const float Width=float(Size.X),Height=float(Size.Y),Tab=FMath::Min(135.f,Width*.45f);
		const FLinearColor Paper=ProjectDFSDUI::Paper(),Edge=ProjectDFSDUI::PaperEdge();
		FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(Width,FMath::Max(0.f,Height-22)),FSlateLayoutTransform(FVector2D(0,22))),White,ESlateDrawEffect::None,Paper*Tint);
		FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(Tab,24),FSlateLayoutTransform()),White,ESlateDrawEffect::None,Paper*Tint);
		const TArray<FVector2D> Outline{{0,0},{Tab,0},{Tab+18,22},{Width,22},{Width,Height},{0,Height},{0,0}};
		ProjectDFSDUI::PaintTape(Out,G,Layer+1,FVector2D(0,22),FVector2D(Width,FMath::Max(0.f,Height-22)),1.f,Tint,WearSeed+5);
		FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Outline,ESlateDrawEffect::None,Edge*Tint,true,1);
		FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(3,FMath::Max(0.f,Height-48)),FSlateLayoutTransform(FVector2D(9,34))),White,ESlateDrawEffect::None,ProjectDFSDUI::Accent()*Tint);
		FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(FMath::Max(0.f,Width-44),1),FSlateLayoutTransform(FVector2D(22,34))),White,ESlateDrawEffect::None,Edge*Tint);
        PaintPaperWear(Out,G,Layer+2,FVector2D::ZeroVector,Size,1.f,Tint,true,WearSeed);
		return SCompoundWidget::OnPaint(Args,G,Rect,Out,Layer+4,Style,Enabled);
	}
private:int32 WearSeed=731;
};
TSharedRef<SWidget> UProjectDFSDCaseFolder::RebuildWidget()
{
	return SNew(SProjectDFSDCaseFolder).WearSeed(int32(FCrc::StrCrc32(*GetName())))[GetContent()?GetContent()->TakeWidget():SNullWidget::NullWidget];
}

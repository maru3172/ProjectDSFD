#include "ProjectDFSDUITheme.h"
#include "ProjectProject01LoginWidget.h"
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
        const FLinearColor Fiber(.25f,.25f,.25f,.55f),Cut=ProjectDFSDUI::Canvas();
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
            Wear.Triangle(Origin+A,Origin+B,Origin+C,Cut);Wear.Triangle(Origin+A,Origin+C,Origin+D,Cut);
            Lines({A,B,C,D},Fiber,.65f);
        }
        const FVector2D Corner(Size.X,Folder?22*Scale:0);
        Wear.Triangle(Origin+Corner+FVector2D(-18*Scale,0),Origin+Corner,Origin+Corner+FVector2D(0,17*Scale),FLinearColor(0.0912f,0.0912f,0.0912f,.9f));
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
	FLinearColor Canvas(){return FLinearColor(.006f,.008f,.012f,1);}
	FLinearColor Panel(){return FLinearColor(0.0230f,0.0230f,0.0230f,1);}
	FLinearColor Text(){return FLinearColor(.86f,.87f,.89f,1);}
	FLinearColor Muted(){return FLinearColor(.48f,.51f,.56f,1);}
	FLinearColor Accent(){return FLinearColor(.45f,.12f,.08f,1);}
	FLinearColor Danger(){return FLinearColor(1,.32f,.22f,1);}
	const FButtonStyle& TitleButtonStyle()
	{
		static const FButtonStyle Style=[]
		{
			auto S=FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
			// The title's original quiet translucent controls are the reference, not gray boxed buttons.
			S.SetNormal(FSlateColorBrush(FLinearColor(.01f,.014f,.02f,.25f)))
			 .SetHovered(FSlateColorBrush(FLinearColor(.18f,.12f,.10f,.85f)))
			 .SetPressed(FSlateColorBrush(FLinearColor(.27f,.08f,.05f,.92f)))
			 .SetDisabled(FSlateColorBrush(FLinearColor(.01f,.014f,.02f,.1f)))
			 .SetNormalForeground(Text()).SetHoveredForeground(FLinearColor(.98f,.94f,.88f))
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
			// Neutral graphite controls remain lighter than the aged file and footer surfaces.
			S.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.0553f,0.0553f,0.0553f,.98f),2.f,FLinearColor(0.2027f,0.2027f,0.2027f,.85f),1.f))
             .SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.1039f,0.1039f,0.1039f),2.f,FLinearColor(0.2784f,0.2784f,0.2784f),1.f))
             .SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.0694f,0.0694f,0.0694f),2.f,Accent(),1.f))
			 .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.0202f,0.0202f,0.0202f,.85f),2.f,FLinearColor(0.0560f,0.0560f,0.0560f,.6f),1.f));
			return S;
		}();
		static const FButtonStyle Selected=[]
		{
			auto S=Normal;
			S.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.11f,.04f,.03f),2.f,Accent(),1.5f));return S;
		}();
		return bSelected?Selected:Normal;
	}
	const FEditableTextBoxStyle& InputStyle()
	{
		static const FEditableTextBoxStyle Style=[]
		{
			auto S=FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
			S.SetBackgroundImageNormal(FSlateRoundedBoxBrush(FLinearColor(0.0110f,0.0110f,0.0110f),2.f,FLinearColor(0.1221f,0.1221f,0.1221f),1.f))
			 .SetBackgroundImageHovered(FSlateRoundedBoxBrush(Panel(),2.f,FLinearColor(.15f,.12f,.11f),1.f))
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
			 .SetInactiveBrush(FSlateColorBrush(FLinearColor(.11f,.04f,.03f)))
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
		if(!Tree)return;Tree->ForEachWidget([](UWidget* W)
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
					(T->GetFont().Size>=28?FLinearColor(.94f,.92f,.87f):Text()).CopyWithNewOpacity(C.A));
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
}

class SProjectDFSDPage final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProjectDFSDPage):_Width(1120),_Bottom(32){} SLATE_ARGUMENT(float,Width) SLATE_ARGUMENT(float,Bottom) SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
	void Construct(const FArguments& Args){Width=Args._Width*1.20f;Bottom=Args._Bottom;ChildSlot[Args._Content.Widget];}
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
		FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),White,ESlateDrawEffect::None,ProjectDFSDUI::Canvas()*Tint);
		const float Left=FMath::Max(16.f/Dpi,(static_cast<float>(Size.X)-Width*Fit)/2);
		FSlateDrawElement::MakeBox(Out,Layer+4,G.ToPaintGeometry(FVector2D(64*Fit,2*Fit),FSlateLayoutTransform(FVector2D(Left+6*Fit,28*Fit))),White,ESlateDrawEffect::None,ProjectDFSDUI::Accent()*Tint);
        // Restrained file-folder rules and a document header, not a bright paper texture.
        const float Right=FMath::Min(float(Size.X)-16.f/Dpi,Left+Width*Fit);
        const float FrameLeft=FMath::Max(8.f/Dpi,Left-10*Fit),FrameRight=FMath::Min(float(Size.X)-8.f/Dpi,Right+10*Fit);
        const float FrameBottom=FMath::Max(60.f*Fit,float(Size.Y)-Bottom*Fit);
        // A quiet folder tab, clipped corner and filing rules communicate the case-file
        // theme without replacing page headings or adding a literal document slogan.
        const TArray<FVector2D> Folder{{FrameLeft,42*Fit},{FrameLeft,FrameBottom},{FrameRight,FrameBottom},{FrameRight,16*Fit},{FrameLeft+150*Fit,16*Fit},{FrameLeft+125*Fit,36*Fit},{FrameLeft,36*Fit},{FrameLeft,42*Fit}};
        FSlateDrawElement::MakeBox(Out,Layer+3,G.ToPaintGeometry(FVector2D(FrameRight-FrameLeft,FrameBottom-36*Fit),FSlateLayoutTransform(FVector2D(FrameLeft,36*Fit))),White,ESlateDrawEffect::None,ProjectDFSDUI::Panel()*Tint);
        FSlateDrawElement::MakeBox(Out,Layer+3,G.ToPaintGeometry(FVector2D(125*Fit,20*Fit),FSlateLayoutTransform(FVector2D(FrameLeft,16*Fit))),White,ESlateDrawEffect::None,ProjectDFSDUI::Panel()*Tint);
        FSlateDrawElement::MakeLines(Out,Layer+4,G.ToPaintGeometry(),Folder,ESlateDrawEffect::None,FLinearColor(0.1596f,0.1596f,0.1596f,.72f)*Tint,true,1.f/Dpi);
        FSlateDrawElement::MakeBox(Out,Layer+4,G.ToPaintGeometry(FVector2D(2*Fit,FMath::Max(1.f,FrameBottom-60*Fit)),FSlateLayoutTransform(FVector2D(FrameLeft+5*Fit,60*Fit))),White,ESlateDrawEffect::None,FLinearColor(.12f,.045f,.035f,.6f)*Tint);
        FSlateDrawElement::MakeBox(Out,Layer+4,G.ToPaintGeometry(FVector2D(FMath::Max(1.f,Right-Left),1.f/Dpi),FSlateLayoutTransform(FVector2D(Left,47*Fit))),White,ESlateDrawEffect::None,FLinearColor(.10f,.105f,.11f,.8f)*Tint);
        PaintPaperWear(Out,G,Layer+5,FVector2D(FrameLeft,16*Fit),FVector2D(FrameRight-FrameLeft,FrameBottom-16*Fit),Fit,Tint,false);
		return SCompoundWidget::OnPaint(Args,G,Rect,Out,Layer+7,Style,bEnabled);
	}
private:float Width=1120,Bottom=32;
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
	return SNew(SProjectDFSDPage).Width(ContentWidth).Bottom(BottomClearance)[GetContent()?GetContent()->TakeWidget():SNullWidget::NullWidget];
}
TSharedRef<SWidget> ProjectDFSDUI::FrameSlate(TSharedRef<SWidget> Content,float Width,float Bottom)
{
    return SNew(SProjectDFSDPage).Width(Width).Bottom(Bottom)[Content];
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
		const FLinearColor Paper(.038f,.038f,.038f,1),Edge(.22f,.22f,.22f,.85f);
		FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(Width,FMath::Max(0.f,Height-22)),FSlateLayoutTransform(FVector2D(0,22))),White,ESlateDrawEffect::None,Paper*Tint);
		FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(Tab,24),FSlateLayoutTransform()),White,ESlateDrawEffect::None,Paper*Tint);
		const TArray<FVector2D> Outline{{0,0},{Tab,0},{Tab+18,22},{Width,22},{Width,Height},{0,Height},{0,0}};
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

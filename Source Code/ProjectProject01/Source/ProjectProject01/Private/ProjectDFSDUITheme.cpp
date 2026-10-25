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

namespace ProjectDFSDUI
{
	FLinearColor Canvas(){return FLinearColor(.006f,.008f,.012f,1);}
	FLinearColor Panel(){return FLinearColor(.018f,.025f,.03f,1);}
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
			 .SetNormalPadding(FMargin(14,8)).SetPressedPadding(FMargin(15,9));return S;
		}();
		return Style;
	}
	const FButtonStyle& ButtonStyle(bool bSelected)
	{
		static const FButtonStyle Normal=[]
		{
			auto S=TitleButtonStyle();
			// Non-title windows need a visible resting surface, without lifting the whole horror backdrop.
			S.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.026f,.037f,.042f,.96f),2.f,FLinearColor(.11f,.15f,.16f,.7f),1.f))
			 .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.012f,.015f,.020f,.85f),2.f,FLinearColor(.035f,.042f,.055f,.6f),1.f));
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
			S.SetBackgroundImageNormal(FSlateRoundedBoxBrush(FLinearColor(.009f,.013f,.018f),2.f,FLinearColor(.065f,.075f,.09f),1.f))
			 .SetBackgroundImageHovered(FSlateRoundedBoxBrush(Panel(),2.f,FLinearColor(.15f,.12f,.11f),1.f))
			 .SetBackgroundImageFocused(FSlateRoundedBoxBrush(Panel(),2.f,Accent(),1.5f))
			 .SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(FLinearColor(.01f,.013f,.018f),2.f,FLinearColor(.035f,.04f,.05f),1.f))
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
			S.SetButtonStyle(ButtonStyle()).SetMenuBorderBrush(FSlateRoundedBoxBrush(Panel(),2.f,FLinearColor(.08f,.09f,.11f),1.f));
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
		S.UncheckedImage=FSlateRoundedBoxBrush(FLinearColor(.009f,.013f,.018f),2.f,FLinearColor(.16f,.18f,.21f),1.f,FVector2D(20,20));
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
					Border->SetBrush(FSlateRoundedBoxBrush(Panel().CopyWithNewOpacity(C.A),2.f,FLinearColor(.075f,.11f,.12f,.7f),1.f));
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
		FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(64*Fit,2*Fit),FSlateLayoutTransform(FVector2D(Left+6*Fit,28*Fit))),White,ESlateDrawEffect::None,ProjectDFSDUI::Accent()*Tint);
        // Restrained file-folder rules and a document header, not a bright paper texture.
        const float Right=FMath::Min(float(Size.X)-16.f/Dpi,Left+Width*Fit);
        const float FrameLeft=FMath::Max(8.f/Dpi,Left-10*Fit),FrameRight=FMath::Min(float(Size.X)-8.f/Dpi,Right+10*Fit);
        const float FrameBottom=FMath::Max(60.f*Fit,float(Size.Y)-Bottom*Fit);
        // A quiet folder tab, clipped corner and filing rules communicate the case-file
        // theme without replacing page headings or adding a literal document slogan.
        const TArray<FVector2D> Folder{{FrameLeft,42*Fit},{FrameLeft,FrameBottom},{FrameRight,FrameBottom},{FrameRight,16*Fit},{FrameLeft+150*Fit,16*Fit},{FrameLeft+125*Fit,36*Fit},{FrameLeft,36*Fit},{FrameLeft,42*Fit}};
        FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Folder,ESlateDrawEffect::None,FLinearColor(.085f,.09f,.105f,.72f)*Tint,true,1.f/Dpi);
        FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(2*Fit,FMath::Max(1.f,FrameBottom-60*Fit)),FSlateLayoutTransform(FVector2D(FrameLeft+5*Fit,60*Fit))),White,ESlateDrawEffect::None,FLinearColor(.12f,.045f,.035f,.6f)*Tint);
        FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(FMath::Max(1.f,Right-Left),1.f/Dpi),FSlateLayoutTransform(FVector2D(Left,47*Fit))),White,ESlateDrawEffect::None,FLinearColor(.10f,.105f,.11f,.8f)*Tint);
		return SCompoundWidget::OnPaint(Args,G,Rect,Out,Layer+2,Style,bEnabled);
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
	SLATE_BEGIN_ARGS(SProjectDFSDCaseFolder){} SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
	void Construct(const FArguments& Args){ChildSlot.Padding(FMargin(22,42,22,20))[Args._Content.Widget];}
	virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Rect,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const override
	{
		const auto Size=G.GetLocalSize();const auto Tint=Style.GetColorAndOpacityTint();const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
		const float Width=float(Size.X),Height=float(Size.Y),Tab=FMath::Min(135.f,Width*.45f);
		const FLinearColor Paper(.035f,.039f,.045f,1),Edge(.20f,.20f,.195f,.85f);
		FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(Width,FMath::Max(0.f,Height-22)),FSlateLayoutTransform(FVector2D(0,22))),White,ESlateDrawEffect::None,Paper*Tint);
		FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(Tab,24),FSlateLayoutTransform()),White,ESlateDrawEffect::None,Paper*Tint);
		const TArray<FVector2D> Outline{{0,0},{Tab,0},{Tab+18,22},{Width,22},{Width,Height},{0,Height},{0,0}};
		FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Outline,ESlateDrawEffect::None,Edge*Tint,true,1);
		FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(3,FMath::Max(0.f,Height-48)),FSlateLayoutTransform(FVector2D(9,34))),White,ESlateDrawEffect::None,ProjectDFSDUI::Accent()*Tint);
		FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(FMath::Max(0.f,Width-44),1),FSlateLayoutTransform(FVector2D(22,34))),White,ESlateDrawEffect::None,Edge*Tint);
		return SCompoundWidget::OnPaint(Args,G,Rect,Out,Layer+2,Style,Enabled);
	}
};
TSharedRef<SWidget> UProjectDFSDCaseFolder::RebuildWidget()
{
	return SNew(SProjectDFSDCaseFolder)[GetContent()?GetContent()->TakeWidget():SNullWidget::NullWidget];
}

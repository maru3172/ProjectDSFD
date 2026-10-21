#include "ProjectDFSDUITheme.h"
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

namespace ProjectDFSDUI
{
	FLinearColor Canvas(){return FLinearColor(.006f,.008f,.012f,1);}
	FLinearColor Panel(){return FLinearColor(.018f,.022f,.029f,1);}
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
			S.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.026f,.032f,.041f,.96f),2.f,FLinearColor(.08f,.095f,.12f,.7f),1.f))
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
					Border->SetBrushColor(Panel().CopyWithNewOpacity(C.A));
			}
		});
	}
	void FramePage(UWidgetTree* Tree,float Width)
	{
		if(!Tree||!Tree->RootWidget||Cast<UProjectDFSDPageFrame>(Tree->RootWidget))return;
		auto* OldRoot=Tree->RootWidget.Get();auto* Frame=Tree->ConstructWidget<UProjectDFSDPageFrame>();
		Frame->ContentWidth=Width;
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
	SLATE_BEGIN_ARGS(SProjectDFSDPage):_Width(1120){} SLATE_ARGUMENT(float,Width) SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
	void Construct(const FArguments& Args){Width=Args._Width;ChildSlot[Args._Content.Widget];}
	virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(Width+64,720);}
	virtual void OnArrangeChildren(const FGeometry& G,FArrangedChildren& Children)const override
	{
		const FVector2D Size=G.GetLocalSize();const float Dpi=FMath::Max(.01f,G.GetAccumulatedLayoutTransform().GetScale());
		// Size the menu like the title's fitted canvas instead of shrinking it twice through viewport DPI.
		const float Fit=FMath::Clamp(static_cast<float>(Size.Y)*Dpi/900.f,.75f,1.1f)/Dpi;
		const float Margin=(Size.X*Dpi<600?16.f:32.f)/Dpi;
		const FVector2D Inner(FMath::Max(1.f,FMath::Min(Width,(static_cast<float>(Size.X)-2*Margin)/Fit)),FMath::Max(1.0,Size.Y/Fit-80));
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
		return SCompoundWidget::OnPaint(Args,G,Rect,Out,Layer+2,Style,bEnabled);
	}
private:float Width=1120;
};

TSharedRef<SWidget> UProjectDFSDPageFrame::RebuildWidget()
{
	return SNew(SProjectDFSDPage).Width(ContentWidth)[GetContent()?GetContent()->TakeWidget():SNullWidget::NullWidget];
}

UProjectDFSDComboBox::UProjectDFSDComboBox(){InitForegroundColor(ProjectDFSDUI::Text());}

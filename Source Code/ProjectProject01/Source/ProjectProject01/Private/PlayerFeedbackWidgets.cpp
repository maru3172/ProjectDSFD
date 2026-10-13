#include "PlayerFeedbackWidgets.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

bool UProjectProject01DamageFeedbackWidget::Initialize()
{
	const bool bInitializationSucceeded = Super::Initialize();
	if (bInitializationSucceeded && !IsValid(WidgetTree->RootWidget))
	{
		BuildWidgetTree();
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return bInitializationSucceeded;
}

void UProjectProject01DamageFeedbackWidget::BuildWidgetTree()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;
}

void UProjectProject01DamageFeedbackWidget::Configure(
	const float InIntroSeconds, const float InFadeSeconds, const float InMaxOpacity,
	const float InMaxThicknessPixels)
{
	IntroSeconds = FMath::Max(InIntroSeconds, 0.01f);
	FadeSeconds = FMath::Max(InFadeSeconds, 0.01f);
	MaxOpacity = FMath::Clamp(InMaxOpacity, 0.0f, 1.0f);
	MaxThicknessPixels = FMath::Max(InMaxThicknessPixels, 1.0f);
	ElapsedSeconds = 0.0f;
	ApplyFrame(0.0f, 0.0f);
}

void UProjectProject01DamageFeedbackWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ElapsedSeconds += FMath::Max(InDeltaTime, 0.0f);
	const float TotalSeconds = IntroSeconds + FadeSeconds;
	if (ElapsedSeconds >= TotalSeconds)
	{
		RemoveFromParent();
		return;
	}

	float ThicknessAlpha = 0.0f;
	float OpacityAlpha = 0.0f;
	if (ElapsedSeconds <= IntroSeconds)
	{
		const float Alpha = FMath::Clamp(ElapsedSeconds / IntroSeconds, 0.0f, 1.0f);
		ThicknessAlpha = FMath::InterpEaseOut(0.0f, 1.0f, Alpha, 3.0f);
		OpacityAlpha = Alpha;
	}
	else
	{
		const float Alpha = FMath::Clamp((ElapsedSeconds - IntroSeconds) / FadeSeconds, 0.0f, 1.0f);
		ThicknessAlpha = 1.0f - FMath::InterpEaseIn(0.0f, 1.0f, Alpha, 2.2f);
		OpacityAlpha = 1.0f - FMath::SmoothStep(0.0f, 1.0f, Alpha);
	}
	ApplyFrame(MaxThicknessPixels * ThicknessAlpha, MaxOpacity * OpacityAlpha);
}

void UProjectProject01DamageFeedbackWidget::ApplyFrame(const float ThicknessPixels, const float Opacity)
{
	CurrentThicknessPixels = FMath::Max(ThicknessPixels, 0.0f);
	CurrentOpacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UProjectProject01DamageFeedbackWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (CurrentOpacity <= UE_KINDA_SMALL_NUMBER || CurrentThicknessPixels <= UE_KINDA_SMALL_NUMBER)
	{
		return BaseLayer;
	}

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (Size.X <= 1.0f || Size.Y <= 1.0f)
	{
		return BaseLayer;
	}

	// 네 개의 사각형을 포개지 않고, 세로 조각마다 타원 바깥쪽만 정확히 한 번 그린다.
	// 따라서 중앙 시야는 화면 비율에 맞는 원형(타원형)이 되고 모서리의 중첩 면도 생기지 않는다.
	const FVector2D Center = Size * 0.5f;
	const float RadiusX = FMath::Max(Center.X - CurrentThicknessPixels, 1.0f);
	const float RadiusY = FMath::Max(Center.Y - CurrentThicknessPixels, 1.0f);
	const int32 StripCount = FMath::Clamp(FMath::CeilToInt(Size.X / 8.0f), 96, 320);
	const float StripWidth = Size.X / static_cast<float>(StripCount);
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FLinearColor MainColor(0.72f, 0.0f, 0.0f, CurrentOpacity);
	const FLinearColor FeatherColor(0.72f, 0.0f, 0.0f, CurrentOpacity * 0.32f);
	const float FeatherPixels = FMath::Min(18.0f, CurrentThicknessPixels * 0.2f);
	const int32 PaintLayer = BaseLayer + 1;

	auto DrawRect = [&](const float X, const float Y, const float Width, const float Height, const FLinearColor& Color)
	{
		if (Width <= 0.0f || Height <= 0.0f)
		{
			return;
		}
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			PaintLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(Width, Height), FSlateLayoutTransform(FVector2D(X, Y))),
			WhiteBrush,
			ESlateDrawEffect::None,
			Color);
	};

	for (int32 StripIndex = 0; StripIndex < StripCount; ++StripIndex)
	{
		const float X0 = StripWidth * static_cast<float>(StripIndex);
		const float X1 = StripIndex + 1 == StripCount
			? Size.X
			: StripWidth * static_cast<float>(StripIndex + 1);
		const float SampleX = (X0 + X1) * 0.5f;
		const float NormalizedX = (SampleX - Center.X) / RadiusX;
		if (FMath::Abs(NormalizedX) >= 1.0f)
		{
			DrawRect(X0, 0.0f, X1 - X0, Size.Y, MainColor);
			continue;
		}

		const float HalfOpeningHeight = RadiusY * FMath::Sqrt(FMath::Max(0.0f, 1.0f - FMath::Square(NormalizedX)));
		const float OpeningTop = Center.Y - HalfOpeningHeight;
		const float OpeningBottom = Center.Y + HalfOpeningHeight;
		const float OpeningHeight = OpeningBottom - OpeningTop;
		const float SafeFeatherPixels = FMath::Min(FeatherPixels, OpeningHeight * 0.5f);
		DrawRect(X0, 0.0f, X1 - X0, OpeningTop, MainColor);
		DrawRect(X0, OpeningBottom, X1 - X0, Size.Y - OpeningBottom, MainColor);
		DrawRect(X0, OpeningTop, X1 - X0, SafeFeatherPixels, FeatherColor);
		DrawRect(X0, OpeningBottom - SafeFeatherPixels, X1 - X0, SafeFeatherPixels, FeatherColor);
	}

	return PaintLayer;
}

bool UProjectProject01DeathFeedbackWidget::Initialize()
{
	const bool bInitializationSucceeded = Super::Initialize();
	if (bInitializationSucceeded && !IsValid(WidgetTree->RootWidget))
	{
		BuildWidgetTree();
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return bInitializationSucceeded;
}

void UProjectProject01DeathFeedbackWidget::BuildWidgetTree()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
	BlackBackground = WidgetTree->ConstructWidget<UBorder>();
	BlackBackground->SetBrushColor(FLinearColor::Black);
	BlackBackground->SetRenderOpacity(0.0f);
	if (UOverlaySlot* BackgroundSlot = Root->AddChildToOverlay(BlackBackground))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}
}

void UProjectProject01DeathFeedbackWidget::Configure(
	const float InCameraSeconds, const float InBlackFadeSeconds)
{
	CameraSeconds = FMath::Max(InCameraSeconds, 0.0f);
	BlackFadeSeconds = FMath::Max(InBlackFadeSeconds, 0.01f);
	ElapsedSeconds = 0.0f;
	if (IsValid(BlackBackground)) BlackBackground->SetRenderOpacity(0.0f);
}

void UProjectProject01DeathFeedbackWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ElapsedSeconds += FMath::Max(InDeltaTime, 0.0f);
	const float FadeAlpha = FMath::Clamp((ElapsedSeconds - CameraSeconds) / BlackFadeSeconds, 0.0f, 1.0f);
	if (IsValid(BlackBackground)) BlackBackground->SetRenderOpacity(FadeAlpha);
	if (ElapsedSeconds >= CameraSeconds + BlackFadeSeconds)
	{
		RemoveFromParent();
	}
}

#include "PlayerFeedbackWidgets.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Rendering/RenderingCommon.h"
#include "ProjectProject01GameInstance.h"

void UProjectProject01MannequinCaptureCameraModifier::PlayCaptureShake(
	const float InDurationSeconds, const float InYawAmplitudeDegrees)
{
	DurationSeconds = FMath::Max(InDurationSeconds, 0.01f);
	YawAmplitudeDegrees = FMath::Max(InYawAmplitudeDegrees, 0.0f);
	ElapsedSeconds = 0.0f;
	bPlaying = YawAmplitudeDegrees > UE_KINDA_SMALL_NUMBER;
	EnableModifier();
}

bool UProjectProject01MannequinCaptureCameraModifier::ModifyCamera(
	const float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	if (!bPlaying)
	{
		return false;
	}
	ElapsedSeconds += FMath::Max(DeltaTime, 0.0f);
	const float Progress = FMath::Clamp(ElapsedSeconds / DurationSeconds, 0.0f, 1.0f);
	const float Envelope = FMath::Square(1.0f - Progress);
	const float Wave = FMath::Sin(Progress * UE_PI * 8.0f);
	InOutPOV.Rotation.Yaw += Wave * YawAmplitudeDegrees * Envelope;
	InOutPOV.Rotation.Roll += Wave * YawAmplitudeDegrees * Envelope * 0.22f;
	if (Progress >= 1.0f)
	{
		bPlaying = false;
	}
	return false;
}

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

float UProjectProject01DamageFeedbackWidget::CalculateDamageSeverity(
	const int32 RemainingDeathCount, const int32 StartingDeathCount)
{
	const int32 Starting = FMath::Max(StartingDeathCount, 0);
	const int32 Remaining = FMath::Clamp(RemainingDeathCount, 0, Starting);
	if (Starting <= 1) return Remaining == 0 ? 1.0f : 0.0f;
	// Normalize from the first lost count to zero remaining, not from hits since spawning/loading.
	return FMath::Clamp(static_cast<float>(Starting - Remaining - 1) / static_cast<float>(Starting - 1), 0.0f, 1.0f);
}

void UProjectProject01DamageFeedbackWidget::Configure(
	const float InIntroSeconds, const float InFadeSeconds, const float InMaxOpacity,
	const float InMaxThicknessPixels, const float DamageSeverity)
{
	const float Severity = FMath::IsFinite(DamageSeverity) ? FMath::Clamp(DamageSeverity, 0.0f, 1.0f) : 0.0f;
	IntroSeconds = FMath::Max(InIntroSeconds, 0.01f);
	FadeSeconds = FMath::Max(InFadeSeconds, 0.01f);
	MaxOpacity = FMath::Clamp(InMaxOpacity * FMath::Lerp(1.0f, 1.3f, Severity), 0.0f, 1.0f);
	MaxThicknessPixels = FMath::Max(InMaxThicknessPixels, 1.0f) * FMath::Lerp(1.0f, 1.35f, Severity);
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
		ApplyFrame(0.0f, 0.0f);
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

	// One non-overlapping elliptical mesh: transparent sight -> blood-red pressure -> dark outer rim.
	// No repeated overlay planes, flashing, blur, or new camera movement.
	const UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get();
	const float AccessibilityScale = Settings ? Settings->GetLocalFlashScale() : 1.0f;
	const float Opacity = CurrentOpacity * AccessibilityScale * InWidgetStyle.GetColorAndOpacityTint().A;
	if (Opacity <= UE_KINDA_SMALL_NUMBER || !FSlateApplication::IsInitialized()) return BaseLayer;
	const FVector2f Center(Size.X * 0.5f, Size.Y * 0.5f);
	const float RadiusX = FMath::Max(Center.X - CurrentThicknessPixels * 1.12f, Center.X * 0.24f);
	const float RadiusY = FMath::Max(Center.Y - CurrentThicknessPixels * 1.12f, Center.Y * 0.30f);
	constexpr int32 Segments = 160;
	constexpr float RingFractions[] = {0.0f, 0.10f, 0.26f, 0.48f, 0.72f, 1.0f};
	constexpr int32 RingCount = UE_ARRAY_COUNT(RingFractions);
	TArray<float> Angles;
	for (int32 Index = 0; Index < Segments; ++Index) Angles.Add(2.0f * UE_PI * Index / Segments);
	// Include exact corner rays so no tiny uncovered triangles remain at screen corners.
	const float CornerAngle = FMath::Atan2(Center.Y, Center.X);
	Angles.Append({CornerAngle, UE_PI - CornerAngle, UE_PI + CornerAngle, 2.0f * UE_PI - CornerAngle, 2.0f * UE_PI});
	Angles.Sort();
	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;
	Vertices.Reserve(Angles.Num() * RingCount);
	Indices.Reserve((Angles.Num() - 1) * (RingCount - 1) * 6);
	for (const float Angle : Angles)
	{
		const FVector2f Direction(FMath::Cos(Angle), FMath::Sin(Angle));
		const float OuterDistance = FMath::Min(Center.X / FMath::Max(FMath::Abs(Direction.X), 0.00001f),
			Center.Y / FMath::Max(FMath::Abs(Direction.Y), 0.00001f));
		const float OrganicEdge = 1.0f + 0.012f * FMath::Sin(Angle * 5.0f) + 0.006f * FMath::Sin(Angle * 9.0f);
		const float InnerDistance = FMath::Min(OuterDistance, OrganicEdge / FMath::Sqrt(
			FMath::Square(Direction.X / RadiusX) + FMath::Square(Direction.Y / RadiusY)));
		for (const float Fraction : RingFractions)
		{
			const FVector2f Position = Center + Direction * FMath::Lerp(InnerDistance, OuterDistance, Fraction);
			const float Density = 0.45f * FMath::SmoothStep(0.0f, 0.55f, Fraction) + 0.55f * FMath::Pow(Fraction, 0.7f);
			const float Red = FMath::Lerp(0.60f, 0.20f, FMath::Pow(Fraction, 0.8f)) *
				(0.95f + 0.05f * FMath::Sin(Angle * 7.0f + Fraction));
			const FLinearColor Color(Red, 0.006f, 0.012f, Opacity * Density);
			Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
				AllottedGeometry.GetAccumulatedRenderTransform(), Position, FVector2f(0.5f, 0.5f), Color.ToFColorSRGB()));
		}
	}
	for (int32 Segment = 0; Segment + 1 < Angles.Num(); ++Segment)
		for (int32 Ring = 0; Ring + 1 < RingCount; ++Ring)
		{
			const SlateIndex A = Segment * RingCount + Ring;
			const SlateIndex B = A + RingCount;
			Indices.Append({A, B, static_cast<SlateIndex>(A + 1), B, static_cast<SlateIndex>(B + 1), static_cast<SlateIndex>(A + 1)});
		}
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const auto Resource = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
	FSlateDrawElement::MakeCustomVerts(OutDrawElements, BaseLayer + 1, Resource, Vertices, Indices, nullptr, 0, 0);
	return BaseLayer + 1;
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

bool UProjectProject01MannequinCaptureFeedbackWidget::Initialize()
{
	const bool bInitializationSucceeded = Super::Initialize();
	if (bInitializationSucceeded && !IsValid(WidgetTree->RootWidget))
	{
		BuildWidgetTree();
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return bInitializationSucceeded;
}

void UProjectProject01MannequinCaptureFeedbackWidget::BuildWidgetTree()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;
}

void UProjectProject01MannequinCaptureFeedbackWidget::Configure(
	const bool bInElimination, const float InIntensityScale)
{
	DurationSeconds = bInElimination ? 0.95f : 0.68f;
	ElapsedSeconds = 0.0f;
	IntensityScale = FMath::Clamp(InIntensityScale, 0.0f, 1.0f);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UProjectProject01MannequinCaptureFeedbackWidget::NativeTick(
	const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ElapsedSeconds += FMath::Max(InDeltaTime, 0.0f);
	if (ElapsedSeconds >= DurationSeconds)
	{
		RemoveFromParent();
		return;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UProjectProject01MannequinCaptureFeedbackWidget::NativePaint(
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
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (Size.X <= 1.0f || Size.Y <= 1.0f || IntensityScale <= UE_KINDA_SMALL_NUMBER)
	{
		return BaseLayer;
	}

	const float Progress = FMath::Clamp(ElapsedSeconds / FMath::Max(DurationSeconds, 0.01f), 0.0f, 1.0f);
	const float Fade = (1.0f - FMath::SmoothStep(0.0f, 1.0f, Progress)) * IntensityScale;
	const float Expansion = FMath::InterpEaseOut(0.35f, 1.0f, FMath::Min(Progress * 5.0f, 1.0f), 2.0f);
	const FVector2D Center = Size * 0.5f;
	const FLinearColor BloodColor(0.48f, 0.005f, 0.008f, Fade * 0.82f);
	const FVector2D Directions[] = {
		FVector2D(-0.39f, -0.24f), FVector2D(-0.31f, 0.17f), FVector2D(-0.18f, -0.36f),
		FVector2D(0.14f, -0.33f), FVector2D(0.34f, -0.13f), FVector2D(0.38f, 0.22f),
		FVector2D(0.12f, 0.34f), FVector2D(-0.23f, 0.31f)
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Directions); ++Index)
	{
		const FVector2D Direction = Directions[Index];
		const FVector2D End = Center + FVector2D(Direction.X * Size.X, Direction.Y * Size.Y) * Expansion;
		const FVector2D Start = FMath::Lerp(Center, End, 0.70f);
		const FVector2D Normal(-Direction.Y, Direction.X);
		TArray<FVector2d> Streak;
		Streak.Reserve(3);
		Streak.Add(FVector2d(Start));
		Streak.Add(FVector2d(FMath::Lerp(Start, End, 0.55f) + Normal * (Index % 2 == 0 ? 7.0f : -6.0f)));
		Streak.Add(FVector2d(End));
		FSlateDrawElement::MakeLines(
			OutDrawElements, BaseLayer + 1, AllottedGeometry.ToPaintGeometry(), Streak,
			ESlateDrawEffect::None, BloodColor, true, 2.5f + static_cast<float>(Index % 3) * 1.5f);
	}
	return BaseLayer + 1;
}

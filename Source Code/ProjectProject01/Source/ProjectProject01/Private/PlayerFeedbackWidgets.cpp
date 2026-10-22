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
	constexpr float RingFractions[] = {0.0f, 0.10f, 0.26f, 0.48f, 0.72f, 1.0f};
	if (CachedPaintSize != Size || CachedAngles.IsEmpty()) RebuildPaintGeometry(Size);
	TArray<FSlateVertex>& Vertices = ScratchVertices;
	Vertices.Reset(CachedAngles.Num() * UE_ARRAY_COUNT(RingFractions));
	for (int32 AngleIndex = 0; AngleIndex < CachedAngles.Num(); ++AngleIndex)
	{
		const float Angle = CachedAngles[AngleIndex];
		const FVector2f Direction = CachedDirections[AngleIndex];
		const float OuterDistance = CachedOuterDistances[AngleIndex];
		const float OrganicEdge = CachedOrganicEdges[AngleIndex];
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
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const auto Resource = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
	FSlateDrawElement::MakeCustomVerts(OutDrawElements, BaseLayer + 1, Resource, Vertices, CachedIndices, nullptr, 0, 0);
	return BaseLayer + 1;
}

void UProjectProject01DamageFeedbackWidget::RebuildPaintGeometry(const FVector2D& Size) const
{
	const FVector2f Center(Size.X * 0.5f, Size.Y * 0.5f);
	constexpr int32 Segments = 160;
	constexpr int32 RingCount = 6; // matches RingFractions in NativePaint
	CachedAngles.Reset(Segments + 5);
	for (int32 Index = 0; Index < Segments; ++Index) CachedAngles.Add(2.0f * UE_PI * Index / Segments);
	// Include exact corner rays so no tiny uncovered triangles remain at screen corners.
	const float CornerAngle = FMath::Atan2(Center.Y, Center.X);
	CachedAngles.Append({CornerAngle, UE_PI - CornerAngle, UE_PI + CornerAngle, 2.0f * UE_PI - CornerAngle, 2.0f * UE_PI});
	CachedAngles.Sort();
	CachedDirections.Reset(CachedAngles.Num());
	CachedOuterDistances.Reset(CachedAngles.Num());
	CachedOrganicEdges.Reset(CachedAngles.Num());
	for (const float Angle : CachedAngles)
	{
		const FVector2f Direction(FMath::Cos(Angle), FMath::Sin(Angle));
		CachedDirections.Add(Direction);
		CachedOuterDistances.Add(FMath::Min(Center.X / FMath::Max(FMath::Abs(Direction.X), 0.00001f),
			Center.Y / FMath::Max(FMath::Abs(Direction.Y), 0.00001f)));
		CachedOrganicEdges.Add(1.0f + 0.012f * FMath::Sin(Angle * 5.0f) + 0.006f * FMath::Sin(Angle * 9.0f));
	}
	CachedIndices.Reset((CachedAngles.Num() - 1) * (RingCount - 1) * 6);
	for (int32 Segment = 0; Segment + 1 < CachedAngles.Num(); ++Segment)
		for (int32 Ring = 0; Ring + 1 < RingCount; ++Ring)
		{
			const SlateIndex A = Segment * RingCount + Ring;
			const SlateIndex B = A + RingCount;
			CachedIndices.Append({A, B, static_cast<SlateIndex>(A + 1), B, static_cast<SlateIndex>(B + 1), static_cast<SlateIndex>(A + 1)});
		}
	CachedPaintSize = Size;
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
	const float InCameraSeconds, const float InBlackFadeSeconds, const bool bInHoldBlackUntilReleased)
{
	bHoldBlackUntilReleased = bInHoldBlackUntilReleased;
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
	if (!bHoldBlackUntilReleased && ElapsedSeconds >= CameraSeconds + BlackFadeSeconds)
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
	IntensityScale = FMath::IsFinite(InIntensityScale) ? FMath::Clamp(InIntensityScale, 0.0f, 1.0f) : 0.0f;
	PatternSeed = FMath::Rand();
	bElimination = bInElimination;
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
	// Fast, soft-edged impact followed by a slower release; no full-screen flash or central obstruction.
	const float Attack = FMath::SmoothStep(0.0f, 0.025f, ElapsedSeconds);
	const float Fade = Attack * (1.0f - FMath::SmoothStep(0.06f, DurationSeconds, ElapsedSeconds)) *
		IntensityScale * InWidgetStyle.GetColorAndOpacityTint().A;
	if (Fade <= UE_KINDA_SMALL_NUMBER || !FSlateApplication::IsInitialized()) return BaseLayer;
	const float Burst = FMath::InterpEaseOut(0.68f, 1.0f, FMath::Clamp(ElapsedSeconds / 0.12f, 0.0f, 1.0f), 3.0f);
	const float PixelScale = FMath::Min(Size.X, Size.Y) / 720.0f;
	const FVector2f Center(Size.X * 0.5f, Size.Y * 0.5f);
	const FVector2f Drift(0.0f, 12.0f * PixelScale * Progress * Progress);
	FRandomStream Pattern(PatternSeed);
	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;
	Vertices.Reserve(1000);
	Indices.Reserve(4000);
	const auto AddVertex = [&](const FVector2f& Position, float Alpha, float Red)
	{
		Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
			AllottedGeometry.GetAccumulatedRenderTransform(), Position, FVector2f(0.5f, 0.5f),
			FLinearColor(Red, 0.005f, 0.012f, Fade * Alpha).ToFColorSRGB()));
	};
	constexpr int32 Slices = 10;
	constexpr float Across[] = {-1.0f, -0.62f, 0.62f, 1.0f};
	const int32 SplashCount = bElimination ? 11 : 9;
	const float Rotation = Pattern.FRandRange(-UE_PI, UE_PI);
	for (int32 Splash = 0; Splash < SplashCount; ++Splash)
	{
		const float Angle = Rotation + 2.0f * UE_PI * (Splash + Pattern.FRandRange(-0.3f, 0.3f)) / SplashCount;
		const FVector2f Direction(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector2f Normal(-Direction.Y, Direction.X);
		const float Reach = Pattern.FRandRange(0.31f, 0.43f);
		const FVector2f End = Center + FVector2f(Direction.X * Size.X, Direction.Y * Size.Y) * Reach * Burst + Drift;
		const FVector2f Start = Center + (End - Center) * Pattern.FRandRange(0.60f, 0.78f);
		const FVector2f Control = (Start + End) * 0.5f + Normal * Pattern.FRandRange(-18.0f, 18.0f) * PixelScale;
		const float Width = Pattern.FRandRange(3.0f, 6.8f) * PixelScale * (bElimination ? 1.12f : 1.0f);
		const float Density = Pattern.FRandRange(0.58f, 0.84f);
		const float Red = Pattern.FRandRange(0.32f, 0.51f);
		const SlateIndex First = static_cast<SlateIndex>(Vertices.Num());
		for (int32 Slice = 0; Slice <= Slices; ++Slice)
		{
			const float T = static_cast<float>(Slice) / Slices;
			const FVector2f Position = FMath::Square(1.0f - T) * Start + 2.0f * (1.0f - T) * T * Control + T * T * End;
			const FVector2f Tangent = ((1.0f - T) * (Control - Start) + T * (End - Control)).GetSafeNormal();
			const FVector2f Side(-Tangent.Y, Tangent.X);
			const float Taper = FMath::Pow(FMath::Max(0.0f, FMath::Sin(UE_PI * T)), 0.7f) * FMath::Lerp(0.55f, 1.0f, T);
			for (int32 Edge = 0; Edge < UE_ARRAY_COUNT(Across); ++Edge)
				AddVertex(Position + Side * Across[Edge] * Width * Taper,
					(Edge == 0 || Edge == 3) ? 0.0f : Density, Red);
		}
		for (int32 Slice = 0; Slice < Slices; ++Slice)
			for (int32 Band = 0; Band < 3; ++Band)
			{
				const SlateIndex A = First + Slice * 4 + Band;
				const SlateIndex B = A + 4;
				Indices.Append({A, B, static_cast<SlateIndex>(A + 1), B, static_cast<SlateIndex>(B + 1), static_cast<SlateIndex>(A + 1)});
			}
		// Small detached droplets carry the impulse; feathered rims avoid hard graphic-looking dots.
		for (int32 Drop = 0; Drop < 2; ++Drop)
		{
			const float Distance = Pattern.FRandRange(0.80f, 1.10f);
			const FVector2f Position = Center + (End - Center) * Distance + Normal * Pattern.FRandRange(-22.0f, 22.0f) * PixelScale;
			const float Radius = Pattern.FRandRange(1.5f, 3.6f) * PixelScale;
			const float Stretch = Pattern.FRandRange(1.0f, 1.65f);
			const SlateIndex Middle = static_cast<SlateIndex>(Vertices.Num());
			AddVertex(Position, Density * 0.85f, Red);
			constexpr int32 Sides = 12;
			for (int32 Ring = 0; Ring < 2; ++Ring)
				for (int32 Side = 0; Side < Sides; ++Side)
				{
					const float A = 2.0f * UE_PI * Side / Sides;
					const float R = Radius * (Ring == 0 ? 0.65f : 1.0f);
					AddVertex(Position + Direction * FMath::Cos(A) * R * Stretch + Normal * FMath::Sin(A) * R,
						Ring == 0 ? Density * 0.85f : 0.0f, Red);
				}
			for (int32 Side = 0; Side < Sides; ++Side)
			{
				const SlateIndex A = Middle + 1 + Side, B = Middle + 1 + (Side + 1) % Sides;
				const SlateIndex C = A + Sides, D = B + Sides;
				Indices.Append({Middle, A, B, A, C, B, B, C, D});
			}
		}
	}
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const auto Resource = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
	FSlateDrawElement::MakeCustomVerts(OutDrawElements, BaseLayer + 1, Resource, Vertices, Indices, nullptr, 0, 0);
	return BaseLayer + 1;
}

#include "ProjectDFSDStartupIntro.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/TextBlock.h"
#include "ImageUtils.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"

float FProjectDFSDIntroTimeline::Opacity() const
{
	if (Stage >= 3) return 0.0f;
	if (bSkipping) return SkipOpacity * (1.0f - FMath::SmoothStep(0.0f, SkipFade, Elapsed));
	if (Stage == 2) return FMath::SmoothStep(0.0f, Reveal, Elapsed);
	if (Elapsed < FadeIn) return FMath::SmoothStep(0.0f, FadeIn, Elapsed);
	return 1.0f - FMath::SmoothStep(FadeIn + Hold, FadeIn + Hold + FadeOut, Elapsed);
}

void FProjectDFSDIntroTimeline::Advance(float DeltaSeconds)
{
	if (Stage >= 3 || !FMath::IsFinite(DeltaSeconds)) return;
	Elapsed += FMath::Clamp(DeltaSeconds, 0.0f, 0.05f);
	const float Duration = bSkipping ? SkipFade : (Stage == 2 ? Reveal : FadeIn + Hold + FadeOut);
	if (Elapsed + UE_KINDA_SMALL_NUMBER >= Duration)
	{
		++Stage;
		Elapsed = 0.0f;
		bSkipping = false;
	}
}

void FProjectDFSDIntroTimeline::Skip()
{
	if (Stage == 2)
	{
		// The final reveal may complete immediately; logo skips still fade through black.
		Stage = 3;
		bSkipping = false;
		return;
	}
	if (Stage >= 3 || bSkipping) return;
	SkipOpacity = Opacity();
	Elapsed = 0.0f;
	bSkipping = true;
}

bool UProjectDFSDStartupIntro::Initialize()
{
	if (!Super::Initialize()) return false;
	SetIsFocusable(true);
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;
	UBorder* Black = WidgetTree->ConstructWidget<UBorder>();
	Black->SetBrushColor(FLinearColor::Black);
	auto* BlackSlot = Root->AddChildToCanvas(Black);
	BlackSlot->SetAnchors(FAnchors(0, 0, 1, 1)); BlackSlot->SetOffsets(FMargin(0));

	LogoBox = WidgetTree->ConstructWidget<UScaleBox>();
	LogoBox->SetStretch(EStretch::ScaleToFit);
	LogoBox->SetStretchDirection(EStretchDirection::Both);
	auto* LogoSlot = Root->AddChildToCanvas(LogoBox);
	// Equal margins around a centered fitting rectangle, NOT stretch-to-fill or cropping.
	LogoSlot->SetAnchors(FAnchors(0.1f, 0.2f, 0.9f, 0.8f)); LogoSlot->SetOffsets(FMargin(0));
	UImage* Logo = WidgetTree->ConstructWidget<UImage>();
	LogoTexture = FImageUtils::ImportFileAsTexture2D(FPaths::ProjectContentDir() / TEXT("Branding/UniversityLogo.png"));
	if (IsValid(LogoTexture))
	{
		// Runtime PNG import creates a transient texture: explicitly use the image's sRGB branding colors.
		LogoTexture->SRGB = true;
		LogoTexture->UpdateResource();
		Logo->SetBrushFromTexture(LogoTexture, true);
	}
	else UE_LOG(LogTemp, Error, TEXT("ProjectDFSD startup logo missing: Content/Branding/UniversityLogo.png"));
	auto* Fit = Cast<UScaleBoxSlot>(LogoBox->AddChild(Logo));
	Fit->SetHorizontalAlignment(HAlign_Center); Fit->SetVerticalAlignment(VAlign_Center);

	TeamText = WidgetTree->ConstructWidget<UTextBlock>();
	TeamText->SetText(FText::FromString(TEXT("Team DSFD")));
	auto Font = TeamText->GetFont(); Font.Size = 50; TeamText->SetFont(Font);
	TeamText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	TeamText->SetJustification(ETextJustify::Center);
	auto* TeamSlot = Root->AddChildToCanvas(TeamText);
	TeamSlot->SetAnchors(FAnchors(0.5f, 0.5f)); TeamSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	TeamSlot->SetAutoSize(true);
	ApplyFrame();
	return true;
}

void UProjectDFSDStartupIntro::ApplyFrame()
{
	const float Alpha = Timeline.Opacity();
	LogoBox->SetVisibility(Timeline.Stage == 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	TeamText->SetVisibility(Timeline.Stage == 1 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	LogoBox->SetRenderOpacity(Alpha); TeamText->SetRenderOpacity(Alpha);
	SetRenderOpacity(Timeline.Stage >= 3 ? 0.0f : (Timeline.Stage == 2 ? 1.0f - Alpha : 1.0f));
}

void UProjectDFSDStartupIntro::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	Timeline.Advance(DeltaSeconds);
	ApplyFrame();
	CompleteIfFinished();
}

void UProjectDFSDStartupIntro::CompleteIfFinished()
{
	if (Timeline.Stage >= 3)
	{
		FSimpleDelegate Finished = OnFinished;
		OnFinished.Unbind();
		if (IsInViewport() || GetParent()) RemoveFromParent();
		Finished.ExecuteIfBound();
	}
}

FReply UProjectDFSDStartupIntro::NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Timeline.Skip();
		ApplyFrame();
		CompleteIfFinished();
	}
	// Consume the mouse-down even when removing the overlay: never press the title button behind it.
	return FReply::Handled();
}

FReply UProjectDFSDStartupIntro::NativeOnKeyDown(const FGeometry&, const FKeyEvent&)
{
	return FReply::Handled();
}

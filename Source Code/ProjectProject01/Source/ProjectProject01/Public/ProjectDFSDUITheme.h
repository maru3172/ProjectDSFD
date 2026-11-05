#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Components/ComboBoxString.h"
#include "Styling/SlateTypes.h"
#include "ProjectDFSDUITheme.generated.h"

class UButton;
class UEditableTextBox;
class UMultiLineEditableTextBox;
class UComboBoxString;
class UCheckBox;
class UWidgetTree;
struct FGeometry;
class FSlateWindowElementList;

/** UComboBoxString exposes its caption foreground only at construction. */
UCLASS()
class PROJECTPROJECT01_API UProjectDFSDComboBox final : public UComboBoxString
{
	GENERATED_BODY()
public:
	UProjectDFSDComboBox();
};

/** Responsive page chrome only. Never owns input, game state, transitions or animations. */
UCLASS()
class PROJECTPROJECT01_API UProjectDFSDPageFrame final : public UContentWidget
{
	GENERATED_BODY()
public:
	float ContentWidth = 1120;
    float BottomClearance=32;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};

/** Portrait case-file cover used by the three campaign save slots. */
UCLASS()
class PROJECTPROJECT01_API UProjectDFSDCaseFolder final : public UContentWidget
{
	GENERATED_BODY()
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};

/**
 * Static recording-tape chrome around one UMG child (scanlines, grain and REC/timestamp marks).
 * Paint only: never takes input itself, follows the child's opacity/visibility and owns no timers.
 */
UCLASS()
class PROJECTPROJECT01_API UProjectDFSDTapeFrame final : public UContentWidget
{
	GENERATED_BODY()
public:
	int32 Channel = 1;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};

namespace ProjectDFSDUI
{
    enum class EIcon : uint8 { Profile, Party, Friends, Whisper, Report, Bell, Add, Phone };
    PROJECTPROJECT01_API TSharedRef<class SWidget> Icon(EIcon Kind,float Size=24.f);
    struct FPageMetrics { float Left=0,Width=0,Scale=1; };
    PROJECTPROJECT01_API FPageMetrics PageMetrics(const FGeometry& Geometry,float Width=1120.f);
    PROJECTPROJECT01_API FPageMetrics FooterMetrics(const FGeometry& Geometry);
    PROJECTPROJECT01_API TSharedRef<class SWidget> FooterSlate(TSharedRef<class SWidget> Content);
	PROJECTPROJECT01_API FLinearColor Canvas();
	PROJECTPROJECT01_API FLinearColor Panel();
	PROJECTPROJECT01_API FLinearColor Text();
	PROJECTPROJECT01_API FLinearColor Muted();
	PROJECTPROJECT01_API FLinearColor Accent();
	PROJECTPROJECT01_API FLinearColor Danger();
	PROJECTPROJECT01_API const FButtonStyle& TitleButtonStyle();
	PROJECTPROJECT01_API const FButtonStyle& ButtonStyle(bool bSelected = false);
	PROJECTPROJECT01_API const FEditableTextBoxStyle& InputStyle();
	PROJECTPROJECT01_API const FComboButtonStyle& ComboButtonStyle();
	PROJECTPROJECT01_API const FTableRowStyle& RowStyle();
	PROJECTPROJECT01_API void Apply(class UButton* Button, bool bSelected = false);
	PROJECTPROJECT01_API void Apply(class UEditableTextBox* Input);
	PROJECTPROJECT01_API void Apply(class UMultiLineEditableTextBox* Input);
	PROJECTPROJECT01_API void Apply(class UComboBoxString* Combo);
	PROJECTPROJECT01_API void Apply(class UCheckBox* Check);
	PROJECTPROJECT01_API void ApplyTree(class UWidgetTree* Tree);
	PROJECTPROJECT01_API void FramePage(class UWidgetTree* Tree, float Width);
	PROJECTPROJECT01_API TSharedRef<class SWidget> FrameSlate(TSharedRef<class SWidget> Content,float Width=1120,float Bottom=32);

	// Analog-horror recording cues. All are static: fixed seeds, no timers, no blinking.
	PROJECTPROJECT01_API FSlateFontInfo Mono(int32 Size);
	PROJECTPROJECT01_API float TapeScanlineOpacity();
	PROJECTPROJECT01_API FString RecordStamp();
	PROJECTPROJECT01_API FString ChannelLabel(int32 Channel);
	/** Fixed scanlines and film grain over a rectangle, drawn below content. */
	PROJECTPROJECT01_API void PaintTape(FSlateWindowElementList& Out,const FGeometry& Geometry,int32 Layer,FVector2D Origin,FVector2D Size,float Scale,FLinearColor Tint,int32 Seed,float Strength=1.f);
	/** Darkened viewport edges; content painted above it is not dimmed. */
	PROJECTPROJECT01_API void PaintVignette(FSlateWindowElementList& Out,const FGeometry& Geometry,int32 Layer,FVector2D Size,FLinearColor Tint);
	/** "REC CH-nn" at Left, "[RECORD 0] date time" ending at Right, on one baseline row at Y. */
	PROJECTPROJECT01_API void PaintRecordMarks(FSlateWindowElementList& Out,const FGeometry& Geometry,int32 Layer,float Left,float Right,float Y,float Scale,FLinearColor Tint,int32 Channel,int32 FontSize=11);
	/** Card surface with tape texture and a reserved REC header row above Content. An opaque card takes clicks on its own rectangle (like an SBorder); a transparent one never does. */
	PROJECTPROJECT01_API TSharedRef<class SWidget> TapeCard(TSharedRef<class SWidget> Content,int32 Channel,FLinearColor Background,FMargin Padding=FMargin(14,10));
}

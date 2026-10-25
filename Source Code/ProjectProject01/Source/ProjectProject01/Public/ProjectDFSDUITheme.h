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

namespace ProjectDFSDUI
{
    enum class EIcon : uint8 { Profile, Party, Friends, Whisper, Report, Bell, Add };
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
}

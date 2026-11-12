#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "ProjectGamepad.h"
#include "ProjectPadSettingsPanel.generated.h"

/**
 * 환경설정 › 조작 › 게임패드 (Docs/Gamepad.md 5): look and cursor speed, vertical invert, dead zone, button labels,
 * vibration, and one row per rebindable button. Values are pending like the rest of the settings screen: Load when the
 * screen opens, Store on 적용, Defaults on 설정 초기화.
 */
UCLASS()
class PROJECTPROJECT01_API UProjectPadSettingsPanel final : public UWidget
{
    GENERATED_BODY()
    friend class FProjectGamepadMappingTest;
public:
    void Load(const class UProjectProject01GameUserSettings* Settings);
    void Store(class UProjectProject01GameUserSettings* Settings) const;
    void Defaults();
    /** A button that does two things on the same screen, as a warning to show (empty when none). */
    FString ConflictWarning() const;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
    void BeginCapture(int32 Action);
    TArray<FKey> Bindings;
    float LookX = 1.f, LookY = 1.f, Cursor = 1.f, DeadZone = .2f, Vibration = .8f, LookSmoothing = .5f, LookCurve = 2.f;
    bool bInvertY = false, bVibration = true;
    int32 Glyphs = 0;
    int32 Capturing = INDEX_NONE;
    TSharedPtr<class STextBlock> WarningText;
};

#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "ProjectGamepad.generated.h"

class UProjectProject01GameUserSettings;

/**
 * Gamepad support (Docs/Gamepad.md). One input handler in front of the game turns pad input into what the game already
 * understands: the right stick becomes mouse movement (every camera works as with a mouse), pad buttons become the
 * player's keyboard keys or mouse clicks for the current mode, and in menus the left stick moves the mouse cursor.
 * Walking uses the left stick as real analog input (added to the survivor and mannequin input mappings).
 */

/** Pad buttons the player can rebind (settings). Menu cursor buttons (A click, B back, ...) are fixed. */
enum class EProjectPadAction : uint8
{
    Sprint, Interact, Flashlight, PingHelp, PingDanger, PingLocation, PushToTalk, VoiceToggle, SpectatePrev, SpectateNext, Menu,
    MannequinPrev, MannequinNext, MannequinWheel, MannequinAttack,
    Card1, Card2, Card3, Reroll,
    Count
};
enum class EProjectInputDevice : uint8 { KeyboardMouse, Gamepad };
/** How pad buttons are written on screen. Auto: Xbox letters (Steam Deck shoulder names on a Deck). */
enum class EProjectPadGlyphs : uint8 { Auto, Xbox, PlayStation };
/** What the pad does right now, from the local player's situation. */
enum class EProjectPadMode : uint8 { Menu, Wheel, Survivor, Mannequin, Observer, Replay };
/** What a pad button turns into: a keyboard key or mouse button to press, or a step through the mannequin numbers. */
struct FProjectPadTranslation { FKey Target; int32 Cycle = 0; };

namespace ProjectGamepad
{
    constexpr int32 ActionCount = int32(EProjectPadAction::Count);
    PROJECTPROJECT01_API FKey DefaultKey(EProjectPadAction Action);
    /** Korean name for the settings screen. */
    PROJECTPROJECT01_API const TCHAR* ActionName(EProjectPadAction Action);
    /** Actions used together (a button should not do two of them): 0 survivor, 1 mannequin player, 2 card picks. Menu is in 0 and 1. */
    PROJECTPROJECT01_API bool SameScreen(EProjectPadAction A, EProjectPadAction B);
    PROJECTPROJECT01_API FString ButtonLabel(const FKey& Key, EProjectPadGlyphs Style, bool bSteamDeck);
    /** Steam sets SteamDeck=1 for games it runs on a Steam Deck. */
    PROJECTPROJECT01_API bool IsSteamDeck();
    /** The meaning of a pad button in a mode, from the player's bindings (null settings: defaults). */
    PROJECTPROJECT01_API FProjectPadTranslation Translate(EProjectPadMode Mode, const FKey& PadKey, bool bCardsOpen, const UProjectProject01GameUserSettings* Settings);
    /** Stick tilt after the dead zone, bent by Exponent (1 = linear): small tilts move slowly, full tilt is full speed. */
    PROJECTPROJECT01_API float Shape(float Value, float DeadZone, float Exponent = 2.f);
    /**
     * One step of the right stick look speed towards Target. Smoothing 0 follows at once; 1 builds up over about 0.3 s.
     * Slowing down or letting go is three times quicker, so the camera never drifts on after the stick is released.
     */
    PROJECTPROJECT01_API FVector2D SmoothLook(const FVector2D& Current, const FVector2D& Target, float Smoothing, float Delta);
    /** The pad is the device in use (for on-screen hints). Context may be null: then the game viewport's game is used. */
    PROJECTPROJECT01_API bool UsingPad(const UObject* Context = nullptr);
    /** A fixed pad button as written on screen with the player's label setting (menus, mini-games). */
    PROJECTPROJECT01_API FString FixedLabel(const FKey& PadKey);
    /** An on-screen key hint: the action's pad button while a pad is in use, otherwise the keyboard key. */
    PROJECTPROJECT01_API FString Hint(EProjectPadAction Action, const FKey& KeyboardKey, const UObject* Context = nullptr);
}

UCLASS()
class PROJECTPROJECT01_API UProjectGamepadSubsystem final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
    friend class FProjectGamepadInput;
    friend class FProjectGamepadInputTest;
public:
    static UProjectGamepadSubsystem* Get(const UObject* WorldContext);
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    EProjectInputDevice GetLastDevice() const { return LastDevice; }
    bool IsUsingGamepad() const { return LastDevice == EProjectInputDevice::Gamepad; }
    /** The pad button for an action as written on screen (labels follow the settings and the Steam Deck). */
    FString PadLabel(EProjectPadAction Action) const;
    /** An on-screen key hint: the pad button while the pad is in use, otherwise the keyboard key. */
    FString Hint(EProjectPadAction Action, const FKey& KeyboardKey) const;
    EProjectPadMode GetMode() const;
    /** Settings: the next pad button pressed is handed to OnKey instead of the game (keyboard Esc cancels). */
    void BeginPadCapture(TFunction<void(FKey)> OnKey, TFunction<void()> OnCancel);
    void CancelPadCapture();
    bool IsCapturingPad() const { return (bool)CaptureKey; }
    FSimpleMulticastDelegate OnDeviceChanged;

    // Vibration (Docs/Gamepad.md 7). Ignored while the keyboard is in use or vibration is off in the settings.
    /** A one-off pulse: Strength 0..1 for Seconds; Heavy uses the large motors. */
    void Pulse(float Strength, float Seconds, bool bHeavy);
    /** The heartbeat while a mannequin is near: Closeness 0 (none) .. 1 (right there). */
    void SetHeartbeat(float Closeness);
    /** Steady rumble (in fire): 0 stops it. */
    void SetSteady(float Strength);

private:
    void SetLastDevice(EProjectInputDevice Device);
    void OnConnectionChange(EInputDeviceConnectionState State, FPlatformUserId User, FInputDeviceId Device);
    void ShowDisconnectNotice(bool bShow);
    bool TickVibration(float Delta);
    TSharedPtr<class FProjectGamepadInput> Input;
    TFunction<void(FKey)> CaptureKey;
    TFunction<void()> CaptureCancel;
    EProjectInputDevice LastDevice = EProjectInputDevice::KeyboardMouse;
    FDelegateHandle ConnectionHandle;
    FTSTicker::FDelegateHandle VibrationTicker;
    float HeartbeatCloseness = 0.f, HeartbeatPhase = 0.f, SteadyStrength = 0.f;
    float PulseStrength = 0.f, PulseLeft = 0.f; bool bPulseHeavy = false;
    bool bDisconnectNoticeShown = false, bPausedForDisconnect = false;
    TSharedPtr<class SWidget> DisconnectNotice;
    TWeakObjectPtr<class UGameViewportClient> NoticeViewport;
};

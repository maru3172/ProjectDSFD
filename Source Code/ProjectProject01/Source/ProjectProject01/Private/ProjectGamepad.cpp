#include "ProjectGamepad.h"
#include "ProjectProject01GameInstance.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectOpenObserver.h"
#include "ProjectMatchReplay.h"
#include "ProjectRoguePick.h"
#include "ProjectRuleVote.h"
#include "PlayerCharacter.h"
#include "MannequinAICharacter.h"
#include "ProjectDFSDUITheme.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

// ---------------------------------------------------------------------------------------------------------------------
// Actions, defaults, labels
// ---------------------------------------------------------------------------------------------------------------------
FKey ProjectGamepad::DefaultKey(EProjectPadAction Action)
{
    switch (Action)
    {
    case EProjectPadAction::Sprint: return EKeys::Gamepad_LeftTrigger;
    case EProjectPadAction::Interact: return EKeys::Gamepad_FaceButton_Bottom;
    case EProjectPadAction::Flashlight: return EKeys::Gamepad_FaceButton_Top;
    case EProjectPadAction::PingHelp: return EKeys::Gamepad_DPad_Left;
    case EProjectPadAction::PingDanger: return EKeys::Gamepad_DPad_Right;
    case EProjectPadAction::PingLocation: return EKeys::Gamepad_DPad_Up;
    case EProjectPadAction::PushToTalk: return EKeys::Gamepad_RightTrigger;
    case EProjectPadAction::VoiceToggle: return EKeys::Gamepad_DPad_Down;
    case EProjectPadAction::SpectatePrev: return EKeys::Gamepad_LeftShoulder;
    case EProjectPadAction::SpectateNext: return EKeys::Gamepad_RightShoulder;
    case EProjectPadAction::Menu: return EKeys::Gamepad_Special_Right;
    case EProjectPadAction::MannequinPrev: return EKeys::Gamepad_LeftShoulder;
    case EProjectPadAction::MannequinNext: return EKeys::Gamepad_RightShoulder;
    case EProjectPadAction::MannequinWheel: return EKeys::Gamepad_LeftTrigger;
    case EProjectPadAction::MannequinAttack: return EKeys::Gamepad_RightTrigger;
    case EProjectPadAction::Card1: return EKeys::Gamepad_DPad_Left;
    case EProjectPadAction::Card2: return EKeys::Gamepad_DPad_Up;
    case EProjectPadAction::Card3: return EKeys::Gamepad_DPad_Right;
    case EProjectPadAction::Reroll: return EKeys::Gamepad_FaceButton_Left;
    default: return EKeys::Invalid;
    }
}
const TCHAR* ProjectGamepad::ActionName(EProjectPadAction Action)
{
    static const TCHAR* Names[] = {
        TEXT("달리기"), TEXT("상호작용"), TEXT("손전등 (사용자 지정 게임)"), TEXT("도움 핑"), TEXT("위험 핑"), TEXT("위치 핑"),
        TEXT("누르는 동안 음성 송신"), TEXT("음성 송신 고정 전환"), TEXT("관전 대상 이전"), TEXT("관전 대상 다음"), TEXT("게임 메뉴"),
        TEXT("마네킹 이전 번호"), TEXT("마네킹 다음 번호"), TEXT("명령 휠"), TEXT("직접 조작 공격"),
        TEXT("카드 1"), TEXT("카드 2"), TEXT("카드 3"), TEXT("카드 다시 뽑기") };
    static_assert(UE_ARRAY_COUNT(Names) == ProjectGamepad::ActionCount, "Every pad action needs a name");
    return Names[FMath::Clamp(int32(Action), 0, ActionCount - 1)];
}
bool ProjectGamepad::SameScreen(EProjectPadAction A, EProjectPadAction B)
{
    auto Groups = [](EProjectPadAction X) -> uint8
    {
        if (X == EProjectPadAction::Menu) return 1 | 2;
        if (X >= EProjectPadAction::Card1) return 4;
        if (X >= EProjectPadAction::MannequinPrev) return 2;
        return 1;
    };
    return A != B && (Groups(A) & Groups(B)) != 0;
}
bool ProjectGamepad::IsSteamDeck() { return FPlatformMisc::GetEnvironmentVariable(TEXT("SteamDeck")) == TEXT("1"); }
bool ProjectGamepad::UsingPad(const UObject* Context)
{
    const UProjectGamepadSubsystem* Sub = Context ? UProjectGamepadSubsystem::Get(Context) : nullptr;
    if (!Sub && GEngine && GEngine->GameViewport) Sub = UProjectGamepadSubsystem::Get(GEngine->GameViewport->GetGameInstance());
    return Sub && Sub->IsUsingGamepad();
}
FString ProjectGamepad::Hint(EProjectPadAction Action, const FKey& KeyboardKey, const UObject* Context)
{
    if (UsingPad(Context))
    {
        const UProjectProject01GameUserSettings* S = UProjectProject01GameUserSettings::Get();
        return FixedLabel(S ? S->GetPadBinding(Action) : DefaultKey(Action));
    }
    return KeyboardKey.IsValid() ? KeyboardKey.GetDisplayName().ToString() : FString(TEXT("미지정"));
}
FString ProjectGamepad::FixedLabel(const FKey& PadKey)
{
    const UProjectProject01GameUserSettings* S = UProjectProject01GameUserSettings::Get();
    return ButtonLabel(PadKey, S ? S->GetPadGlyphs() : EProjectPadGlyphs::Auto, IsSteamDeck());
}
FString ProjectGamepad::ButtonLabel(const FKey& Key, EProjectPadGlyphs Style, bool bSteamDeck)
{
    const bool Ps = Style == EProjectPadGlyphs::PlayStation;
    const bool Deck = !Ps && bSteamDeck;
    if (Key == EKeys::Gamepad_FaceButton_Bottom) return Ps ? TEXT("×") : TEXT("A");
    if (Key == EKeys::Gamepad_FaceButton_Right) return Ps ? TEXT("○") : TEXT("B");
    if (Key == EKeys::Gamepad_FaceButton_Left) return Ps ? TEXT("□") : TEXT("X");
    if (Key == EKeys::Gamepad_FaceButton_Top) return Ps ? TEXT("△") : TEXT("Y");
    if (Key == EKeys::Gamepad_LeftShoulder) return Ps || Deck ? TEXT("L1") : TEXT("LB");
    if (Key == EKeys::Gamepad_RightShoulder) return Ps || Deck ? TEXT("R1") : TEXT("RB");
    if (Key == EKeys::Gamepad_LeftTrigger) return Ps || Deck ? TEXT("L2") : TEXT("LT");
    if (Key == EKeys::Gamepad_RightTrigger) return Ps || Deck ? TEXT("R2") : TEXT("RT");
    if (Key == EKeys::Gamepad_LeftThumbstick) return Ps ? TEXT("L3") : TEXT("LS");
    if (Key == EKeys::Gamepad_RightThumbstick) return Ps ? TEXT("R3") : TEXT("RS");
    if (Key == EKeys::Gamepad_Special_Right) return Ps ? TEXT("OPTIONS") : Deck ? TEXT("☰") : TEXT("메뉴");
    if (Key == EKeys::Gamepad_Special_Left) return Ps ? TEXT("터치패드") : Deck ? TEXT("⧉") : TEXT("보기");
    if (Key == EKeys::Gamepad_DPad_Up) return TEXT("십자 위");
    if (Key == EKeys::Gamepad_DPad_Down) return TEXT("십자 아래");
    if (Key == EKeys::Gamepad_DPad_Left) return TEXT("십자 왼쪽");
    if (Key == EKeys::Gamepad_DPad_Right) return TEXT("십자 오른쪽");
    return Key.IsValid() ? Key.GetDisplayName().ToString() : FString(TEXT("미지정"));
}

// ---------------------------------------------------------------------------------------------------------------------
// What a pad button means (pure, so it is tested without sending input anywhere)
// ---------------------------------------------------------------------------------------------------------------------
FProjectPadTranslation ProjectGamepad::Translate(EProjectPadMode M, const FKey& PadKey, bool bCardsOpen, const UProjectProject01GameUserSettings* S)
{
    auto Is = [&](EProjectPadAction A) { return (S ? S->GetPadBinding(A) : DefaultKey(A)) == PadKey; };
    FProjectPadTranslation Out;
    // Card picks (아이템 로그라이크, 규칙 카드 투표) take their buttons first, whatever else is on screen.
    if (bCardsOpen)
    {
        Out.Target = Is(EProjectPadAction::Card1) ? EKeys::One : Is(EProjectPadAction::Card2) ? EKeys::Two : Is(EProjectPadAction::Card3) ? EKeys::Three
            : Is(EProjectPadAction::Reroll) ? EKeys::R : EKeys::Invalid;
        if (Out.Target.IsValid()) return Out;
    }
    if (M == EProjectPadMode::Menu)
    {
        // Fixed buttons: the cursor click, back, and the keys menus and mini-games use.
        if (PadKey == EKeys::Gamepad_FaceButton_Bottom) Out.Target = EKeys::LeftMouseButton;
        else if (PadKey == EKeys::Gamepad_FaceButton_Right || PadKey == EKeys::Gamepad_Special_Right) Out.Target = EKeys::Escape;
        else if (PadKey == EKeys::Gamepad_DPad_Up) Out.Target = EKeys::Up;
        else if (PadKey == EKeys::Gamepad_DPad_Down) Out.Target = EKeys::Down;
        else if (PadKey == EKeys::Gamepad_DPad_Left) Out.Target = EKeys::Left;
        else if (PadKey == EKeys::Gamepad_DPad_Right) Out.Target = EKeys::Right;
        else if (PadKey == EKeys::Gamepad_FaceButton_Left) Out.Target = EKeys::SpaceBar;
        else if (PadKey == EKeys::Gamepad_FaceButton_Top) Out.Target = EKeys::R;
        else if (PadKey == EKeys::Gamepad_LeftShoulder) Out.Target = EKeys::Q;
        else if (PadKey == EKeys::Gamepad_RightShoulder) Out.Target = EKeys::E;
        else if (PadKey == EKeys::Gamepad_Special_Left) Out.Target = EKeys::Z;
        else if (PadKey == EKeys::Gamepad_RightTrigger) Out.Target = EKeys::RightMouseButton; // e.g. the 정전 복구 flashlight switch.
        return Out;
    }
    if (M == EProjectPadMode::Wheel)
    {
        // The command wheel: A or the attack trigger runs the highlighted command, B or the wheel trigger closes it.
        if (PadKey == EKeys::Gamepad_FaceButton_Bottom || Is(EProjectPadAction::MannequinAttack)) Out.Target = EKeys::LeftMouseButton;
        else if (PadKey == EKeys::Gamepad_FaceButton_Right || Is(EProjectPadAction::MannequinWheel)) Out.Target = EKeys::RightMouseButton;
        return Out;
    }
    if (M == EProjectPadMode::Mannequin)
    {
        if (Is(EProjectPadAction::MannequinPrev)) Out.Cycle = -1;
        else if (Is(EProjectPadAction::MannequinNext)) Out.Cycle = 1;
        else if (Is(EProjectPadAction::MannequinWheel)) Out.Target = EKeys::RightMouseButton;
        else if (Is(EProjectPadAction::MannequinAttack)) Out.Target = EKeys::LeftMouseButton;
        else if (Is(EProjectPadAction::Menu)) Out.Target = EKeys::Escape;
        return Out;
    }
    // The wheel trigger is the open observer's free look (held Alt) and the replay's camera switch (right click); it is
    // checked first there because the survivor layout uses the same trigger for sprinting.
    if ((M == EProjectPadMode::Observer || M == EProjectPadMode::Replay) && Is(EProjectPadAction::MannequinWheel))
    {
        Out.Target = M == EProjectPadMode::Observer ? EKeys::LeftAlt : EKeys::RightMouseButton;
        return Out;
    }
    // Survivors, spectators, the open observer and replays: the player's own keyboard keys.
    if (Is(EProjectPadAction::Sprint)) Out.Target = S ? S->GetSprintKey() : EKeys::LeftShift;
    else if (Is(EProjectPadAction::Interact)) Out.Target = EKeys::F;
    else if (Is(EProjectPadAction::Flashlight)) Out.Target = S ? S->GetFlashlightKey() : EKeys::F;
    else if (Is(EProjectPadAction::PingHelp)) Out.Target = S ? S->GetHelpPingKey() : EKeys::E;
    else if (Is(EProjectPadAction::PingDanger)) Out.Target = S ? S->GetDangerPingKey() : EKeys::R;
    else if (Is(EProjectPadAction::PingLocation)) Out.Target = S ? S->GetLocationPingKey() : EKeys::T;
    else if (Is(EProjectPadAction::PushToTalk)) Out.Target = S ? S->GetVoicePushToTalkKey() : EKeys::F2;
    else if (Is(EProjectPadAction::VoiceToggle)) Out.Target = S ? S->GetVoiceToggleKey() : EKeys::U;
    else if (Is(EProjectPadAction::SpectatePrev)) Out.Target = EKeys::Left;
    else if (Is(EProjectPadAction::SpectateNext)) Out.Target = EKeys::Right;
    else if (Is(EProjectPadAction::Menu)) Out.Target = EKeys::Escape;
    // The wheel trigger is the open observer's free look (held Alt) and the replay's camera switch (right click).
    else if (Is(EProjectPadAction::MannequinWheel) && (M == EProjectPadMode::Observer || M == EProjectPadMode::Replay))
        Out.Target = M == EProjectPadMode::Observer ? EKeys::LeftAlt : EKeys::RightMouseButton;
    return Out;
}

float ProjectGamepad::Shape(const float Value, const float DeadZone, const float Exponent)
{
    const float A = FMath::Abs(Value);
    if (A <= DeadZone) return 0.f;
    const float T = FMath::Clamp((A - DeadZone) / FMath::Max(.01f, 1.f - DeadZone), 0.f, 1.f);
    return FMath::Sign(Value) * FMath::Pow(T, FMath::Clamp(Exponent, 1.f, 3.f));
}
FVector2D ProjectGamepad::SmoothLook(const FVector2D& Current, const FVector2D& Target, const float Smoothing, const float Delta)
{
    const float Ease = FMath::Clamp(Smoothing, 0.f, 1.f) * .1f; // Time constant: about 3x this reaches the target (0.3 s at 1).
    if (Ease <= KINDA_SMALL_NUMBER || Delta <= 0.f) return Target;
    // Slowing down (smaller target, or the other way) eases three times quicker than speeding up.
    const bool bSlower = Target.SizeSquared() < Current.SizeSquared() || (Target | Current) < 0.f;
    const float Alpha = 1.f - FMath::Exp(-Delta / (bSlower ? Ease / 3.f : Ease));
    const FVector2D Next = FMath::Lerp(Current, Target, Alpha);
    return Target.IsNearlyZero() && Next.IsNearlyZero(.002f) ? FVector2D::ZeroVector : Next;
}

// ---------------------------------------------------------------------------------------------------------------------
// The input handler
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
    constexpr float LookPixelsPerSecond = 950.f;   // Full right stick, speed 1.0 (before the player's mouse sensitivity).
    constexpr float CursorPixelsPerSecond = 1300.f; // Full left stick in menus, speed 1.0.
    using ProjectGamepad::Shape;
    UProjectProject01GameUserSettings* Settings() { return UProjectProject01GameUserSettings::Get(); }
}

/** One entry per pad button held down: what it was turned into, so the matching release goes to the same place. */
struct FProjectPadTarget
{
    FKey Key;                // A keyboard key or a mouse button to release.
    bool bNothing = false;   // Consumed with no effect (or a direct call that has no release).
};

class FProjectGamepadInput final : public IInputProcessor
{
public:
    explicit FProjectGamepadInput(UProjectGamepadSubsystem* InOwner) : Owner(InOwner) {}
    TWeakObjectPtr<UProjectGamepadSubsystem> Owner;
    FVector2D Left = FVector2D::ZeroVector, Right = FVector2D::ZeroVector;
    TMap<FKey, FProjectPadTarget> Held;
    TSet<FKey> HeldMoveKeys;
    bool bInjecting = false;
    float ScrollWait = 0.f;
    EProjectPadMode LastMode = EProjectPadMode::Menu;
    FVector2D LookVelocity = FVector2D::ZeroVector; // Eased right stick look speed (-1..1 per axis times the look speed).

    APlayerController* LocalController() const
    {
        const UProjectGamepadSubsystem* Sub = Owner.Get();
        const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
        return GI ? GI->GetFirstLocalPlayerController() : nullptr;
    }
    EProjectPadMode Mode() const
    {
        APlayerController* PC = LocalController();
        if (!IsValid(PC)) return EProjectPadMode::Menu;
        if (const auto* M = Cast<AMultiplayTestPlayerController>(PC); M && M->IsMannequinWheelOpen()) return EProjectPadMode::Wheel;
        if (PC->ShouldShowMouseCursor() || UGameplayStatics::IsGamePaused(PC)) return EProjectPadMode::Menu;
        if (PC->IsA<AProjectReplayController>()) return EProjectPadMode::Replay;
        if (PC->IsA<AProjectOpenObserverController>()) return EProjectPadMode::Observer;
        if (const auto* M = Cast<AMultiplayTestPlayerController>(PC); M && M->IsLocalMannequinPlayer()) return EProjectPadMode::Mannequin;
        return EProjectPadMode::Survivor;
    }
    bool CardsOpen() const
    {
        const APlayerController* PC = LocalController();
        if (!IsValid(PC)) return false;
        const auto* Pick = PC->FindComponentByClass<UProjectRoguePickComponent>();
        const auto* Vote = PC->FindComponentByClass<UProjectRuleVoteComponent>();
        return (Pick && Pick->IsPickOpen()) || (Vote && Vote->IsVoteOpen());
    }
    /** The walking pawns read the left stick themselves (analog); elsewhere it presses the movement keys. */
    bool PawnReadsStick() const
    {
        const APlayerController* PC = LocalController();
        const APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
        return Pawn && (Pawn->IsA<APlayerCharacter>() || Pawn->IsA<AMannequinAICharacter>());
    }

    // ---- Sending what the game understands ----
    void Key(FSlateApplication& App, const FKey& K, bool bDown, bool bRepeat = false)
    {
        TGuardValue<bool> Guard(bInjecting, true);
        const uint32* Code = nullptr; const uint32* Char = nullptr;
        FInputKeyManager::Get().GetCodesFromKey(K, Code, Char);
        const FKeyEvent Event(K, App.GetModifierKeys(), 0, bRepeat, Char ? *Char : 0, Code ? *Code : 0);
        bDown ? App.ProcessKeyDownEvent(Event) : App.ProcessKeyUpEvent(Event);
    }
    void Mouse(FSlateApplication& App, const FKey& Button, bool bDown)
    {
        TGuardValue<bool> Guard(bInjecting, true);
        TSet<FKey> Pressed; if (bDown) Pressed.Add(Button);
        const FVector2D At = App.GetCursorPos();
        const FPointerEvent Event(FSlateApplicationBase::CursorPointerIndex, At, At, Pressed, Button, 0, App.GetModifierKeys());
        if (bDown)
        {
            const TSharedPtr<SWindow> Window = App.GetActiveTopLevelWindow();
            App.ProcessMouseButtonDownEvent(Window.IsValid() ? Window->GetNativeWindow() : nullptr, Event);
        }
        else App.ProcessMouseButtonUpEvent(Event);
    }
    void Press(FSlateApplication& App, const FKey& PadKey, const FKey& Target)
    {
        Held.Add(PadKey, {Target, !Target.IsValid()});
        if (!Target.IsValid()) return;
        Target.IsMouseButton() ? Mouse(App, Target, true) : Key(App, Target, true);
    }
    void ReleaseAll(FSlateApplication& App)
    {
        for (const auto& Pair : Held) if (!Pair.Value.bNothing) Pair.Value.Key.IsMouseButton() ? Mouse(App, Pair.Value.Key, false) : Key(App, Pair.Value.Key, false);
        Held.Reset();
        for (const FKey& K : HeldMoveKeys) Key(App, K, false);
        HeldMoveKeys.Reset();
    }

    // ---- Events ----
    virtual bool HandleKeyDownEvent(FSlateApplication& App, const FKeyEvent& Event) override
    {
        if (bInjecting) return false;
        const FKey PadKey = Event.GetKey();
        if (UProjectGamepadSubsystem* Sub = Owner.Get(); Sub && Sub->CaptureKey)
        {
            // Rebinding in the settings: the next pad button is the answer; keyboard Esc gives up.
            if (PadKey.IsGamepadKey() && !PadKey.IsAnalog() && !Event.IsRepeat()) { auto Done = MoveTemp(Sub->CaptureKey); Sub->CaptureKey = nullptr; Sub->CaptureCancel = nullptr; Done(PadKey); Held.Add(PadKey, {FKey(), true}); return true; }
            if (PadKey == EKeys::Escape) { Sub->CancelPadCapture(); return true; }
        }
        if (!PadKey.IsGamepadKey()) { Device(EProjectInputDevice::KeyboardMouse); return false; }
        Device(EProjectInputDevice::Gamepad);
        if (Event.IsRepeat())
        {
            // Held D-pad repeats in menus (lists, mini-games); everything else ignores repeats.
            if (const FProjectPadTarget* T = Held.Find(PadKey); T && !T->bNothing && T->Key.IsValid() && !T->Key.IsMouseButton() &&
                (T->Key == EKeys::Up || T->Key == EKeys::Down || T->Key == EKeys::Left || T->Key == EKeys::Right)) Key(App, T->Key, true, true);
            return true;
        }
        if (Held.Contains(PadKey)) return true;
        const FProjectPadTranslation T = ProjectGamepad::Translate(Mode(), PadKey, CardsOpen(), Settings());
        if (T.Cycle != 0)
            if (auto* PC = Cast<AMultiplayTestPlayerController>(LocalController())) PC->CycleMannequinSlot(T.Cycle);
        Press(App, PadKey, T.Target);
        return true;
    }
    virtual bool HandleKeyUpEvent(FSlateApplication& App, const FKeyEvent& Event) override
    {
        if (bInjecting || !Event.GetKey().IsGamepadKey()) return false;
        if (FProjectPadTarget T; Held.RemoveAndCopyValue(Event.GetKey(), T) && !T.bNothing)
            T.Key.IsMouseButton() ? Mouse(App, T.Key, false) : Key(App, T.Key, false);
        return true;
    }
    virtual bool HandleAnalogInputEvent(FSlateApplication& App, const FAnalogInputEvent& Event) override
    {
        if (bInjecting) return false;
        const FKey K = Event.GetKey();const float V = Event.GetAnalogValue();
        const float DeadZone = Settings() ? Settings()->GetPadDeadZone() : .2f;
        if (FMath::Abs(V) > DeadZone + .05f) Device(EProjectInputDevice::Gamepad);
        if (K == EKeys::Gamepad_LeftX) Left.X = V; else if (K == EKeys::Gamepad_LeftY) Left.Y = V;
        else if (K == EKeys::Gamepad_RightX) Right.X = V; else if (K == EKeys::Gamepad_RightY) Right.Y = V;
        // The left stick stays analog for walking pawns; everything else (right stick, triggers, menu cursor) is handled here.
        const bool bLeft = K == EKeys::Gamepad_LeftX || K == EKeys::Gamepad_LeftY;
        return !(bLeft && Mode() != EProjectPadMode::Menu && Mode() != EProjectPadMode::Wheel && PawnReadsStick());
    }
    virtual bool HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent& Event) override
    {
        // Moving the cursor with the stick makes Windows send a mouse move a moment later; that is still the pad.
        if (bInjecting || (FPlatformTime::Seconds() - PadCursorTime < .25 && FVector2D::DistSquared(Event.GetScreenSpacePosition(), PadCursorAt) < 16.0)) return false;
        if (Event.GetCursorDelta().SizeSquared() > 9.f) Device(EProjectInputDevice::KeyboardMouse);
        return false;
    }
    void MoveCursor(FSlateApplication& App, const FVector2D& To)
    {
        TGuardValue<bool> Guard(bInjecting, true);
        App.SetCursorPos(To);
        PadCursorAt = App.GetCursorPos(); PadCursorTime = FPlatformTime::Seconds();
    }
    FVector2D PadCursorAt = FVector2D::ZeroVector;
    double PadCursorTime = -1.0;
    virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent&) override
    {
        if (!bInjecting) Device(EProjectInputDevice::KeyboardMouse);
        return false;
    }
    void Device(EProjectInputDevice D) { if (UProjectGamepadSubsystem* Sub = Owner.Get()) Sub->SetLastDevice(D); }

    virtual void Tick(const float Delta, FSlateApplication& App, TSharedRef<ICursor> Cursor) override
    {
        if (!Owner.IsValid()) return;
        const EProjectPadMode M = Mode();
        // A mode change releases whatever a held button was turned into (a menu opening while sprinting, ...).
        if (M != LastMode) { ReleaseAll(App); LastMode = M; LookVelocity = FVector2D::ZeroVector; }
        const UProjectProject01GameUserSettings* S = Settings();
        const float DeadZone = S ? S->GetPadDeadZone() : .2f;
        const float Curve = S ? S->GetPadLookCurve() : 2.f;
        const FVector2D L(Shape(Left.X, DeadZone), Shape(Left.Y, DeadZone)), R(Shape(Right.X, DeadZone, Curve), Shape(Right.Y, DeadZone, Curve));
        if (M == EProjectPadMode::Menu)
        {
            if (!L.IsNearlyZero())
            {
                const FVector2D Step(L.X, -L.Y);
                MoveCursor(App, App.GetCursorPos() + Step * CursorPixelsPerSecond * (S ? S->GetPadCursorSpeed() : 1.f) * Delta);
            }
            ScrollWait -= Delta;
            if (FMath::Abs(R.Y) > .3f && ScrollWait <= 0.f)
            {
                TGuardValue<bool> Guard(bInjecting, true);
                const FVector2D At = App.GetCursorPos();
                const FPointerEvent Wheel(FSlateApplicationBase::CursorPointerIndex, At, At, TSet<FKey>(), EKeys::MouseWheelAxis, FMath::Sign(R.Y), App.GetModifierKeys());
                App.ProcessMouseWheelOrGestureEvent(Wheel, nullptr);
                ScrollWait = FMath::Lerp(.16f, .05f, FMath::Abs(R.Y));
            }
            return;
        }
        if (M == EProjectPadMode::Wheel)
        {
            // Point at a command with the right stick (the cursor goes to that direction from the screen centre).
            if (R.Size() > .35f)
                if (const UGameViewportClient* VP = GEngine ? GEngine->GameViewport : nullptr; VP && VP->GetWindow().IsValid())
                {
                    const FGeometry G = VP->GetWindow()->GetWindowGeometryInScreen();
                    const FVector2D Size = G.GetAbsoluteSize(), Center = G.GetAbsolutePosition() + Size * .5f;
                    MoveCursor(App, Center + FVector2D(R.X, -R.Y).GetSafeNormal() * FMath::Min(Size.X, Size.Y) * .2f);
                }
            return;
        }
        // Look: the right stick moves the mouse, so the player's mouse sensitivity and every camera apply as they are.
        // The speed eases towards the stick (PadLookSmoothing), so a full push does not whip the camera round in one frame.
        const bool bInvert = S && S->GetPadInvertY();
        const FVector2D Target(R.X * (S ? S->GetPadLookSpeedX() : 1.f), (bInvert ? R.Y : -R.Y) * (S ? S->GetPadLookSpeedY() : 1.f));
        LookVelocity = ProjectGamepad::SmoothLook(LookVelocity, Target, S ? S->GetPadLookSmoothing() : .5f, Delta);
        if (!LookVelocity.IsNearlyZero(.002f))
        {
            const FVector2D Move = LookVelocity;
            TGuardValue<bool> Guard(bInjecting, true);
            const FVector2D At = App.GetCursorPos();
            const FPointerEvent Event(FSlateApplicationBase::CursorPointerIndex, At, At, Move * LookPixelsPerSecond * Delta, TSet<FKey>(), App.GetModifierKeys());
            App.ProcessMouseMoveEvent(Event);
        }
        // Free cameras and spectators move with the movement keys: the left stick presses them.
        if (!PawnReadsStick())
        {
            TSet<FKey> Want;
            if (S)
            {
                if (Left.Y > .5f) Want.Add(S->GetMoveForwardKey()); else if (Left.Y < -.5f) Want.Add(S->GetMoveBackwardKey());
                if (Left.X > .5f) Want.Add(S->GetMoveRightKey()); else if (Left.X < -.5f) Want.Add(S->GetMoveLeftKey());
            }
            for (const FKey& K : HeldMoveKeys.Difference(Want)) Key(App, K, false);
            for (const FKey& K : Want.Difference(HeldMoveKeys)) Key(App, K, true);
            HeldMoveKeys = Want;
        }
        else if (!HeldMoveKeys.IsEmpty()) { for (const FKey& K : HeldMoveKeys) Key(App, K, false); HeldMoveKeys.Reset(); }
    }
    virtual const TCHAR* GetDebugName() const override { return TEXT("ProjectGamepadInput"); }
};

// ---------------------------------------------------------------------------------------------------------------------
// Subsystem
// ---------------------------------------------------------------------------------------------------------------------
UProjectGamepadSubsystem* UProjectGamepadSubsystem::Get(const UObject* WorldContext)
{
    const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
    const UGameInstance* GI = World ? World->GetGameInstance() : Cast<UGameInstance>(WorldContext);
    return GI ? GI->GetSubsystem<UProjectGamepadSubsystem>() : nullptr;
}
bool UProjectGamepadSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}
void UProjectGamepadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (FSlateApplication::IsInitialized())
    {
        Input = MakeShared<FProjectGamepadInput>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(Input, 0);
    }
    ConnectionHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddUObject(this, &ThisClass::OnConnectionChange);
    VibrationTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickVibration));
}
void UProjectGamepadSubsystem::Deinitialize()
{
    if (Input.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Input);
    Input.Reset();
    IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(ConnectionHandle);
    FTSTicker::GetCoreTicker().RemoveTicker(VibrationTicker);
    ShowDisconnectNotice(false);
    Super::Deinitialize();
}
void UProjectGamepadSubsystem::SetLastDevice(EProjectInputDevice Device)
{
    if (LastDevice == Device) return;
    LastDevice = Device;
    if (Device == EProjectInputDevice::KeyboardMouse) { HeartbeatCloseness = SteadyStrength = PulseLeft = 0.f; }
    OnDeviceChanged.Broadcast();
}
EProjectPadMode UProjectGamepadSubsystem::GetMode() const { return Input.IsValid() ? Input->Mode() : EProjectPadMode::Menu; }
void UProjectGamepadSubsystem::BeginPadCapture(TFunction<void(FKey)> OnKey, TFunction<void()> OnCancel)
{
    CancelPadCapture();
    CaptureKey = MoveTemp(OnKey); CaptureCancel = MoveTemp(OnCancel);
}
void UProjectGamepadSubsystem::CancelPadCapture()
{
    auto Cancel = MoveTemp(CaptureCancel);
    CaptureKey = nullptr; CaptureCancel = nullptr;
    if (Cancel) Cancel();
}
FString UProjectGamepadSubsystem::PadLabel(EProjectPadAction Action) const
{
    const UProjectProject01GameUserSettings* S = UProjectProject01GameUserSettings::Get();
    return ProjectGamepad::ButtonLabel(S ? S->GetPadBinding(Action) : ProjectGamepad::DefaultKey(Action), S ? S->GetPadGlyphs() : EProjectPadGlyphs::Auto, ProjectGamepad::IsSteamDeck());
}
FString UProjectGamepadSubsystem::Hint(EProjectPadAction Action, const FKey& KeyboardKey) const
{
    return IsUsingGamepad() ? PadLabel(Action) : KeyboardKey.IsValid() ? KeyboardKey.GetDisplayName().ToString() : FString(TEXT("미지정"));
}

// ---- Vibration ----
void UProjectGamepadSubsystem::Pulse(float Strength, float Seconds, bool bHeavy)
{
    if (Strength > PulseStrength || PulseLeft <= 0.f) { PulseStrength = FMath::Clamp(Strength, 0.f, 1.f); bPulseHeavy = bHeavy; }
    PulseLeft = FMath::Max(PulseLeft, Seconds);
}
void UProjectGamepadSubsystem::SetHeartbeat(float Closeness) { HeartbeatCloseness = FMath::Clamp(Closeness, 0.f, 1.f); }
void UProjectGamepadSubsystem::SetSteady(float Strength) { SteadyStrength = FMath::Clamp(Strength, 0.f, 1.f); }
bool UProjectGamepadSubsystem::TickVibration(float Delta)
{
    const UProjectProject01GameUserSettings* S = UProjectProject01GameUserSettings::Get();
    const float Scale = IsUsingGamepad() && S && S->GetPadVibration() ? S->GetPadVibrationStrength() : 0.f;
    // Heartbeat: a double beat ("lub-dub") whose rate rises from 60 to 130 per minute as the mannequin gets closer.
    float Beat = 0.f;
    if (HeartbeatCloseness > 0.f)
    {
        HeartbeatPhase = FMath::Fmod(HeartbeatPhase + Delta * FMath::Lerp(1.f, 2.17f, HeartbeatCloseness), 1.f);
        const float First = FMath::Max(0.f, 1.f - FMath::Abs(HeartbeatPhase - .05f) / .06f), Second = FMath::Max(0.f, 1.f - FMath::Abs(HeartbeatPhase - .25f) / .06f) * .6f;
        Beat = FMath::Max(First, Second) * FMath::Lerp(.25f, .7f, HeartbeatCloseness);
    }
    PulseLeft = FMath::Max(0.f, PulseLeft - Delta);
    const float PulseNow = PulseLeft > 0.f ? PulseStrength : 0.f;
    const UGameInstance* GI = GetGameInstance();
    if (APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr)
    {
        // Standing in the custom game's spreading fire: a weak steady rumble.
        const auto* Survivor = Cast<APlayerCharacter>(PC->GetPawn());
        const float Steady = FMath::Max(SteadyStrength, Survivor && Survivor->IsInPlaygroundFire() ? .35f : 0.f);
        const float Large = FMath::Max(Beat, bPulseHeavy ? PulseNow : 0.f) * Scale, Small = FMath::Max(Steady * .5f, PulseNow) * Scale;
        // Two short dynamic effects refreshed every frame: large motors carry the heartbeat and heavy hits, small ones the rest.
        if (Large > .01f) PC->PlayDynamicForceFeedback(Large, .05f, true, false, true, false);
        if (Small > .01f) PC->PlayDynamicForceFeedback(Small, .05f, false, true, false, true);
    }
    return true;
}

// ---- Disconnect (Docs/Gamepad.md 8) ----
void UProjectGamepadSubsystem::OnConnectionChange(EInputDeviceConnectionState State, FPlatformUserId, FInputDeviceId)
{
    if (State == EInputDeviceConnectionState::Disconnected && IsUsingGamepad())
    {
        ShowDisconnectNotice(true);
        // Single player stops; a match cannot, so it only shows the notice.
        if (const UGameInstance* GI = GetGameInstance(); GI && GI->GetWorld() && GI->GetWorld()->GetNetMode() == NM_Standalone)
            if (APlayerController* PC = GI->GetFirstLocalPlayerController(); PC && !UGameplayStatics::IsGamePaused(PC))
            {
                if (auto* Character = Cast<APlayerCharacter>(PC->GetPawn())) Character->OpenPauseMenuForDisconnect();
                bPausedForDisconnect = true;
            }
    }
    else if (State == EInputDeviceConnectionState::Connected) ShowDisconnectNotice(false); // Stays paused until the player resumes.
}
void UProjectGamepadSubsystem::ShowDisconnectNotice(bool bShow)
{
    if (bShow == bDisconnectNoticeShown) return;
    bDisconnectNoticeShown = bShow;
    if (!bShow)
    {
        if (UGameViewportClient* VP = NoticeViewport.Get(); VP && DisconnectNotice.IsValid()) VP->RemoveViewportWidgetContent(DisconnectNotice.ToSharedRef());
        DisconnectNotice.Reset(); NoticeViewport.Reset(); bPausedForDisconnect = false;
        return;
    }
    UGameViewportClient* VP = GEngine ? GEngine->GameViewport : nullptr;
    if (!VP) return;
    DisconnectNotice = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0, 40, 0, 0)).Visibility(EVisibility::HitTestInvisible)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(0, 0, 0, .82f)).Padding(FMargin(22, 12))
            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 18)).ColorAndOpacity(FLinearColor(.95f, .95f, .93f))
                .Text(FText::FromString(TEXT("패드 연결이 끊겼습니다. 다시 연결하거나 키보드로 계속하세요.")))]];
    VP->AddViewportWidgetContent(DisconnectNotice.ToSharedRef(), 2000);
    NoticeViewport = VP;
}

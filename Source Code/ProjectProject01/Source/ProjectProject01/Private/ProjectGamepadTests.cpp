#include "ProjectGamepad.h"
#include "ProjectPadSettingsPanel.h"
#include "ProjectProject01GameInstance.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
// Docs/Gamepad.md: the default layout, labels, settings and what each pad button means in each mode.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectGamepadMappingTest, "ProjectProject01.Gamepad.Mapping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectGamepadMappingTest::RunTest(const FString&)
{
    using namespace ProjectGamepad;
    // Defaults: every action has a pad button, and no button does two things on one screen.
    bool bAllPad = true, bNoClash = true;
    for (int32 A = 0; A < ActionCount; ++A)
    {
        bAllPad &= DefaultKey(EProjectPadAction(A)).IsGamepadKey();
        for (int32 B = A + 1; B < ActionCount; ++B) bNoClash &= !(DefaultKey(EProjectPadAction(A)) == DefaultKey(EProjectPadAction(B)) && SameScreen(EProjectPadAction(A), EProjectPadAction(B)));
    }
    TestTrue(TEXT("Every action has a default pad button"), bAllPad);
    TestTrue(TEXT("Defaults never give one button two jobs on the same screen"), bNoClash);

    // Labels: Xbox letters, PlayStation symbols, Steam Deck shoulder names.
    TestEqual(TEXT("Xbox A"), ButtonLabel(EKeys::Gamepad_FaceButton_Bottom, EProjectPadGlyphs::Xbox, false), FString(TEXT("A")));
    TestEqual(TEXT("PlayStation cross"), ButtonLabel(EKeys::Gamepad_FaceButton_Bottom, EProjectPadGlyphs::PlayStation, false), FString(TEXT("×")));
    TestEqual(TEXT("PlayStation L2"), ButtonLabel(EKeys::Gamepad_LeftTrigger, EProjectPadGlyphs::PlayStation, false), FString(TEXT("L2")));
    TestEqual(TEXT("Steam Deck shoulders read L1"), ButtonLabel(EKeys::Gamepad_LeftShoulder, EProjectPadGlyphs::Auto, true), FString(TEXT("L1")));
    TestEqual(TEXT("Steam Deck face buttons stay letters"), ButtonLabel(EKeys::Gamepad_FaceButton_Right, EProjectPadGlyphs::Auto, true), FString(TEXT("B")));
    TestEqual(TEXT("Xbox triggers"), ButtonLabel(EKeys::Gamepad_RightTrigger, EProjectPadGlyphs::Auto, false), FString(TEXT("RT")));

    // Meaning by mode (defaults, no settings object: the default keyboard keys).
    auto T = [](EProjectPadMode M, const FKey& K, bool bCards = false) { return Translate(M, K, bCards, nullptr); };
    TestTrue(TEXT("Survivor: A interacts (F)"), T(EProjectPadMode::Survivor, EKeys::Gamepad_FaceButton_Bottom).Target == EKeys::F);
    TestTrue(TEXT("Survivor: LT sprints"), T(EProjectPadMode::Survivor, EKeys::Gamepad_LeftTrigger).Target == EKeys::LeftShift);
    TestTrue(TEXT("Survivor: RT talks"), T(EProjectPadMode::Survivor, EKeys::Gamepad_RightTrigger).Target == EKeys::F2);
    TestTrue(TEXT("Survivor: D-pad pings"), T(EProjectPadMode::Survivor, EKeys::Gamepad_DPad_Left).Target == EKeys::E && T(EProjectPadMode::Survivor, EKeys::Gamepad_DPad_Right).Target == EKeys::R
        && T(EProjectPadMode::Survivor, EKeys::Gamepad_DPad_Up).Target == EKeys::T && T(EProjectPadMode::Survivor, EKeys::Gamepad_DPad_Down).Target == EKeys::U);
    TestTrue(TEXT("Survivor: Y is the flashlight, LB/RB switch spectating, menu opens the game menu"), T(EProjectPadMode::Survivor, EKeys::Gamepad_FaceButton_Top).Target == EKeys::F
        && T(EProjectPadMode::Survivor, EKeys::Gamepad_LeftShoulder).Target == EKeys::Left && T(EProjectPadMode::Survivor, EKeys::Gamepad_RightShoulder).Target == EKeys::Right
        && T(EProjectPadMode::Survivor, EKeys::Gamepad_Special_Right).Target == EKeys::Escape);
    TestFalse(TEXT("Survivor: an unbound button does nothing"), T(EProjectPadMode::Survivor, EKeys::Gamepad_FaceButton_Left).Target.IsValid());
    TestTrue(TEXT("Mannequin: LT opens the wheel, RT attacks"), T(EProjectPadMode::Mannequin, EKeys::Gamepad_LeftTrigger).Target == EKeys::RightMouseButton
        && T(EProjectPadMode::Mannequin, EKeys::Gamepad_RightTrigger).Target == EKeys::LeftMouseButton);
    TestTrue(TEXT("Mannequin: LB/RB step through the numbers"), T(EProjectPadMode::Mannequin, EKeys::Gamepad_LeftShoulder).Cycle == -1 && T(EProjectPadMode::Mannequin, EKeys::Gamepad_RightShoulder).Cycle == 1);
    TestTrue(TEXT("Wheel: A or RT runs, B or LT closes"), T(EProjectPadMode::Wheel, EKeys::Gamepad_FaceButton_Bottom).Target == EKeys::LeftMouseButton
        && T(EProjectPadMode::Wheel, EKeys::Gamepad_RightTrigger).Target == EKeys::LeftMouseButton && T(EProjectPadMode::Wheel, EKeys::Gamepad_FaceButton_Right).Target == EKeys::RightMouseButton
        && T(EProjectPadMode::Wheel, EKeys::Gamepad_LeftTrigger).Target == EKeys::RightMouseButton);
    TestTrue(TEXT("Menu: A clicks, B goes back, D-pad is the arrow keys, X is space, RT right-clicks"), T(EProjectPadMode::Menu, EKeys::Gamepad_FaceButton_Bottom).Target == EKeys::LeftMouseButton
        && T(EProjectPadMode::Menu, EKeys::Gamepad_FaceButton_Right).Target == EKeys::Escape && T(EProjectPadMode::Menu, EKeys::Gamepad_DPad_Down).Target == EKeys::Down
        && T(EProjectPadMode::Menu, EKeys::Gamepad_FaceButton_Left).Target == EKeys::SpaceBar && T(EProjectPadMode::Menu, EKeys::Gamepad_RightTrigger).Target == EKeys::RightMouseButton);
    TestTrue(TEXT("Mini-games in menus: LB/RB turn (Q/E), view undoes (Z), Y restarts (R)"), T(EProjectPadMode::Menu, EKeys::Gamepad_LeftShoulder).Target == EKeys::Q
        && T(EProjectPadMode::Menu, EKeys::Gamepad_RightShoulder).Target == EKeys::E && T(EProjectPadMode::Menu, EKeys::Gamepad_Special_Left).Target == EKeys::Z
        && T(EProjectPadMode::Menu, EKeys::Gamepad_FaceButton_Top).Target == EKeys::R);
    TestTrue(TEXT("Cards: D-pad picks 1/2/3 and X rerolls, in any mode"), T(EProjectPadMode::Menu, EKeys::Gamepad_DPad_Up, true).Target == EKeys::Two
        && T(EProjectPadMode::Survivor, EKeys::Gamepad_DPad_Left, true).Target == EKeys::One && T(EProjectPadMode::Menu, EKeys::Gamepad_FaceButton_Left, true).Target == EKeys::R);
    TestTrue(TEXT("Open observer: LT holds Alt (free look); replay: LT is the camera switch"), T(EProjectPadMode::Observer, EKeys::Gamepad_LeftTrigger).Target == EKeys::LeftAlt
        && T(EProjectPadMode::Replay, EKeys::Gamepad_LeftTrigger).Target == EKeys::RightMouseButton);

    // Rebinding: the settings decide; only pad buttons are accepted.
    auto* S = NewObject<UProjectProject01GameUserSettings>();
    S->ResetPadSettings();
    S->SetPadBinding(EProjectPadAction::Interact, EKeys::Gamepad_FaceButton_Left);
    S->SetPadBinding(EProjectPadAction::Sprint, EKeys::W);
    S->SetPadBinding(EProjectPadAction::Flashlight, EKeys::Gamepad_LeftX);
    TestTrue(TEXT("A rebound action follows its new button"), Translate(EProjectPadMode::Survivor, EKeys::Gamepad_FaceButton_Left, false, S).Target == EKeys::F
        && !Translate(EProjectPadMode::Survivor, EKeys::Gamepad_FaceButton_Bottom, false, S).Target.IsValid());
    TestTrue(TEXT("Keyboard keys and sticks cannot become pad buttons"), S->GetPadBinding(EProjectPadAction::Sprint) == EKeys::Gamepad_LeftTrigger
        && S->GetPadBinding(EProjectPadAction::Flashlight) == EKeys::Gamepad_FaceButton_Top);
    S->SetPadLookSpeed(9.f, .01f); S->SetPadDeadZone(1.f); S->SetPadVibration(true, 2.f);
    TestTrue(TEXT("Speeds, dead zone and vibration stay in range"), S->GetPadLookSpeedX() == 3.f && S->GetPadLookSpeedY() == .2f && S->GetPadDeadZone() == .45f && S->GetPadVibrationStrength() == 1.f);
    S->ResetPadSettings();
    TestTrue(TEXT("Reset brings the default layout back"), S->GetPadBinding(EProjectPadAction::Interact) == EKeys::Gamepad_FaceButton_Bottom && S->GetPadLookSpeedX() == 1.f);

    // The settings panel: pending values, conflict warning, store.
    auto* Panel = NewObject<UProjectPadSettingsPanel>();
    Panel->Defaults();
    TestTrue(TEXT("Default layout has no conflict"), Panel->ConflictWarning().IsEmpty());
    Panel->Bindings[int32(EProjectPadAction::PingHelp)] = EKeys::Gamepad_FaceButton_Bottom;
    TestTrue(TEXT("Two survivor actions on one button are warned about"), Panel->ConflictWarning().Contains(TEXT("상호작용")));
    Panel->Bindings[int32(EProjectPadAction::PingHelp)] = EKeys::Gamepad_DPad_Left;
    Panel->Bindings[int32(EProjectPadAction::MannequinAttack)] = EKeys::Gamepad_FaceButton_Bottom;
    TestTrue(TEXT("The same button in the survivor and mannequin layouts is fine"), Panel->ConflictWarning().IsEmpty());
    Panel->Store(S);
    TestTrue(TEXT("Stored on 적용"), S->GetPadBinding(EProjectPadAction::MannequinAttack) == EKeys::Gamepad_FaceButton_Bottom);
    S->ResetPadSettings();
    return true;
}

// Docs/Gamepad.md: a full right stick push eases the camera up to speed instead of whipping it round in one frame.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectGamepadLookFeelTest, "ProjectProject01.Gamepad.LookFeel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectGamepadLookFeelTest::RunTest(const FString&)
{
    using ProjectGamepad::SmoothLook; using ProjectGamepad::Shape;
    const FVector2D Full(1, 0); constexpr float Frame = 1.f / 60.f;
    TestTrue(TEXT("Smoothing 0 follows the stick at once (old behaviour)"), SmoothLook(FVector2D::ZeroVector, Full, 0.f, Frame) == Full);
    FVector2D V = FVector2D::ZeroVector; int32 Frames = 0;
    for (; Frames < 120 && V.X < .95f; ++Frames) V = SmoothLook(V, Full, .5f, Frame);
    const FVector2D First = SmoothLook(FVector2D::ZeroVector, Full, .5f, Frame);
    TestTrue(TEXT("Default: the first frame is a small part of full speed"), First.X > 0.f && First.X < .4f);
    TestTrue(TEXT("Default: about full speed within a quarter second, never longer than half"), Frames >= 6 && Frames <= 30);
    int32 Stop = 0; for (FVector2D W = Full; !W.IsZero() && Stop < 120; ++Stop) W = SmoothLook(W, FVector2D::ZeroVector, .5f, Frame);
    TestTrue(TEXT("Letting go stops much quicker than speeding up, and reaches exactly zero"), Stop < Frames && Stop < 120);
    FVector2D Slow = FVector2D::ZeroVector; for (int32 I = 0; I < 6; ++I) Slow = SmoothLook(Slow, Full, 1.f, Frame);
    FVector2D Mid = FVector2D::ZeroVector; for (int32 I = 0; I < 6; ++I) Mid = SmoothLook(Mid, Full, .5f, Frame);
    TestTrue(TEXT("More smoothing builds up more slowly"), Slow.X < Mid.X);
    TestTrue(TEXT("Frame rate does not change how fast it builds up"),
        FMath::IsNearlyEqual(SmoothLook(FVector2D::ZeroVector, Full, .5f, .1f).X, [&]{ FVector2D X = FVector2D::ZeroVector; for (int32 I = 0; I < 10; ++I) X = SmoothLook(X, Full, .5f, .01f); return X.X; }(), .01f));
    TestTrue(TEXT("Inside the dead zone nothing moves"), Shape(.15f, .2f, 2.f) == 0.f);
    TestTrue(TEXT("Full tilt is full speed on every curve"), FMath::IsNearlyEqual(Shape(1.f, .2f, 1.f), 1.f) && FMath::IsNearlyEqual(Shape(-1.f, .2f, 3.f), -1.f));
    TestTrue(TEXT("A higher curve turns slower on a small tilt"), Shape(.5f, .2f, 3.f) < Shape(.5f, .2f, 2.f) && Shape(.5f, .2f, 2.f) < Shape(.5f, .2f, 1.f));
    UProjectProject01GameUserSettings* S = UProjectProject01GameUserSettings::Get();
    if (!TestNotNull(TEXT("Settings"), S)) return false;
    S->SetPadLookFeel(5.f, 0.f);
    TestTrue(TEXT("Settings stay in range"), S->GetPadLookSmoothing() == 1.f && S->GetPadLookCurve() == 1.f);
    S->ResetPadSettings();
    TestTrue(TEXT("Reset brings the default feel back"), S->GetPadLookSmoothing() == .5f && S->GetPadLookCurve() == 2.f);
    return true;
}
#endif

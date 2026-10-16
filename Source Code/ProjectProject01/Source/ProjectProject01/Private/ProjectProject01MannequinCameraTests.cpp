#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MultiplayTestPlayerController.h"
#include "MannequinAICharacter.h"
#include "ProjectProject01LoginWidget.h"
#include "AIController.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Camera/CameraTypes.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMannequinCameraTest,"ProjectProject01.Multiplayer.MannequinFreeCamera",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMannequinCameraTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto* Pawn=World->SpawnActor<AMannequinAICharacter>();Pawn->SetActorRotation(FRotator(0,30,0));
    auto* AI=World->SpawnActor<AAIController>();AI->Possess(Pawn);
    auto* PC=World->SpawnActor<AMultiplayTestPlayerController>();
    PC->SetAsLocalPlayerController();
    // This non-begun fixture does not run the normal controller registration lifecycle.
    World->AddController(PC);
    TestTrue(TEXT("Fixture controller exercises the owning local-player path"),PC->IsLocalController());
    PC->bRoleGuideShownForThisController=true;
    PC->InputComponent=NewObject<UEnhancedInputComponent>(PC);
    PC->MatchPhase=EMultiplayTestMatchPhase::InProgress;
    PC->ClientApplyViewedMannequin_Implementation(Pawn);
    TestTrue(TEXT("Owner camera RPC supplies HUD identity before property replication"),PC->GetViewedMannequin()==Pawn);
    PC->EnsureMannequinLookBinding();PC->EnsureMannequinLookBinding();
    TestTrue(TEXT("Look binding exists without possessing the AI pawn"),PC->MannequinLookBindingHandle!=0);
    TestEqual(TEXT("Repeated replication callbacks do not duplicate look binding"),CastChecked<UEnhancedInputComponent>(PC->InputComponent)->GetActionEventBindings().Num(),1);
    const FRotator AIViewBefore=AI->GetControlRotation();
    PC->SetControlRotation(FRotator(15,120,0));
    FMinimalViewInfo View;Pawn->CalcCamera(0.016f,View);
    TestTrue(FString::Printf(TEXT("View-only camera may look away from body (actual %s)"),*View.Rotation.ToString()),View.Rotation.Equals(FRotator(15,120,0),0.1f));
    TestTrue(TEXT("Free camera never rotates AI body or AI sensing direction"),Pawn->GetActorRotation().Equals(FRotator(0,30,0))&&AI->GetControlRotation().Equals(AIViewBefore));
    PC->bShowMouseCursor=false;PC->RotationInput=FRotator::ZeroRotator;
    PC->HandleMannequinLook(FInputActionValue(FVector2D(2,1)));
    TestFalse(TEXT("Unpossessed camera receives mouse input"),PC->RotationInput.IsNearlyZero());
    PC->RotationInput=FRotator::ZeroRotator;PC->bShowMouseCursor=true;
    PC->HandleMannequinLook(FInputActionValue(FVector2D(2,1)));
    TestTrue(TEXT("Menu cursor prevents camera look input"),PC->RotationInput.IsNearlyZero());
    PC->bShowMouseCursor=false;

    // Install ownership without ClientRestart: this fixture intentionally has no ULocalPlayer/UI viewport.
    AI->UnPossess();Pawn->PossessedBy(PC);PC->SetPawn(Pawn);
    auto* Movement=Pawn->GetCharacterMovement();
    Pawn->bUseControllerRotationYaw=false;Movement->bOrientRotationToMovement=true;Movement->bUseControllerDesiredRotation=false;
    Pawn->SetManualControlEnabled(true);
    const FRotator Middle=PC->ResolveMannequinCameraRotation(Pawn,0.125f);
    TestTrue(TEXT("Manual control smoothly aligns from old free view"),Middle.Yaw>30 && Middle.Yaw<120);
    TestTrue(TEXT("Control direction starts at mannequin's original facing, not free camera"),PC->GetControlRotation().Equals(FRotator(0,30,0),0.1f));
    TestTrue(TEXT("Alignment blocks immediate body snap"),PC->IsMannequinCameraAligning());
    PC->ResolveMannequinCameraRotation(Pawn,0.125f);
    TestTrue(TEXT("Alignment is bounded to 0.25 seconds"),PC->IsManualMannequinCameraReady());
    PC->SetControlRotation(FRotator(10,90,0));Pawn->FaceRotation(PC->GetControlRotation(),0.016f);Pawn->CalcCamera(0.016f,View);
    TestTrue(TEXT("Manual body yaw and real view follow player control"),Pawn->GetActorRotation().Equals(FRotator(0,90,0),0.1f)&&Pawn->GetViewRotation().Equals(PC->GetControlRotation(),0.1f));
    Pawn->SetFrozenBySurvivorVision(true);PC->SetControlRotation(FRotator(0,-45,0));
    Pawn->FaceRotation(PC->GetControlRotation(),0.016f);Pawn->CalcCamera(0.016f,View);
    TestTrue(TEXT("Frozen body stays still while camera remains movable"),Pawn->GetActorRotation().Equals(FRotator(0,90,0),0.1f)&&FMath::IsNearlyEqual(View.Rotation.Yaw,-45.0,0.1));
    Pawn->SetFrozenBySurvivorVision(false);
    PC->ViewedMannequin=nullptr;
    TestTrue(TEXT("Owned mannequin is safe fallback while viewed property is unavailable"),PC->GetViewedMannequin()==Pawn);
    PC->ViewedMannequin=Pawn;
    Pawn->SetManualControlEnabled(false);
    TestTrue(TEXT("Releasing control restores original AI rotation settings"),!Pawn->bUseControllerRotationYaw&&Movement->bOrientRotationToMovement&&!Movement->bUseControllerDesiredRotation);
    PC->RemoveMannequinLookBinding();
    TestEqual(TEXT("Survivor/disabled roles release mannequin input binding"),CastChecked<UEnhancedInputComponent>(PC->InputComponent)->GetActionEventBindings().Num(),0);
    UUserWidget* Base=NewObject<UProjectProject01MannequinStatusWidget>();Base->Initialize();
    auto* HUD=CastChecked<UProjectProject01MannequinStatusWidget>(Base);HUD->RefreshMannequinStates();
    TestTrue(TEXT("Unresolved identity does not flash a misleading checking panel"),HUD->StatusText->GetText().IsEmpty()&&HUD->StatusText->GetParent()->GetVisibility()==ESlateVisibility::Collapsed);
    World->DestroyWorld(false);return true;
}
#endif

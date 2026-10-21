#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MultiplayTestPlayerController.h"
#include "MannequinAICharacter.h"
#include "ProjectProject01LoginWidget.h"
#include "AIController.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
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
    Pawn->SetControlSlotForGameMode(3);Pawn->SetMultiplayActivity(EMultiplayMannequinActivity::Chasing);
    FActorSpawnParameters AlertSpawn;AlertSpawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Chaser=World->SpawnActor<AMannequinAICharacter>(AlertSpawn);Chaser->SetControlSlotForGameMode(1);
    Chaser->SetMultiplayActivity(EMultiplayMannequinActivity::Chasing);
    auto* Frozen=World->SpawnActor<AMannequinAICharacter>(AlertSpawn);Frozen->SetControlSlotForGameMode(2);
    Frozen->SetMultiplayActivity(EMultiplayMannequinActivity::Chasing);Frozen->SetFrozenBySurvivorVision(true);
    PC->bLocalMultiplayerRoleKnown=true;PC->bLocalMultiplayerMannequinRole=true;
    PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();PC->PlayerCameraManager->InitializeFor(PC);
    PC->SetViewTarget(Pawn);
    auto* LocalPlayer=NewObject<ULocalPlayer>(GEngine);LocalPlayer->PlayerController=PC;PC->Player=LocalPlayer;
    HUD->SetOwningPlayer(PC);HUD->RefreshMannequinStates();
    TestTrue(TEXT("HUD preserves possessed mannequin identity"),HUD->StatusText->GetText().ToString().Contains(TEXT("현재 빙의한 마네킹: 3번")));
    TestTrue(TEXT("All chasing IDs are sorted and displayed"),HUD->StatusText->GetText().ToString().Contains(TEXT("생존자 추격 중: 1번, 3번")));
    TestTrue(TEXT("Frozen IDs appear separately, never as moving chasers"),HUD->StatusText->GetText().ToString().Contains(TEXT("생존자 시야로 정지: 2번")));
    Pawn->SetManualControlEnabled(true);PC->bManualMannequinCommandIssued=false;
    Pawn->SetMultiplayCommand(EMultiplayMannequinCommand::Patrol);HUD->RefreshMannequinStates();
    TestTrue(TEXT("Manual control without a new order displays the real release default, not a stale AI order"),HUD->StatusText->GetText().ToString().Contains(TEXT("현재 상태: 직접 조작\n명령 상태: 정지")));
    PC->bManualMannequinCommandIssued=true;
    for(auto Command:{EMultiplayMannequinCommand::Patrol,EMultiplayMannequinCommand::ChaseInDetectionRange,EMultiplayMannequinCommand::FollowMannequinPlayer,EMultiplayMannequinCommand::HoldPosition})
    {
        Pawn->SetMultiplayCommand(Command);HUD->RefreshMannequinStates();
        const FString Text=HUD->StatusText->GetText().ToString();
        TestTrue(TEXT("Manual state stays separate from the queued command"),Text.Contains(TEXT("현재 상태: 직접 조작\n명령 상태:")));
        TestFalse(TEXT("HUD no longer labels activity as after manual release"),Text.Contains(TEXT("직접 조작 해제 후")));
        const TCHAR* Expected=Command==EMultiplayMannequinCommand::Patrol?TEXT("정찰"):Command==EMultiplayMannequinCommand::ChaseInDetectionRange?TEXT("감지 범위 추격"):Command==EMultiplayMannequinCommand::FollowMannequinPlayer?TEXT("마네킹 플레이어 추종"):TEXT("정지");
        TestTrue(TEXT("Separate command row reflects each selected command"),Text.Contains(FString(TEXT("명령 상태: "))+Expected));
    }
    Pawn->SetManualControlEnabled(false);PC->bManualMannequinCommandIssued=false;
    Chaser->SetMultiplayActivity(EMultiplayMannequinActivity::Patrolling);
    Pawn->SetMultiplayActivity(EMultiplayMannequinActivity::Searching);Frozen->SetFrozenBySurvivorVision(false);
    Frozen->SetMultiplayActivity(EMultiplayMannequinActivity::Holding);HUD->RefreshMannequinStates();
    TestTrue(TEXT("Stale chase and freeze alerts clear automatically"),HUD->StatusText->GetText().ToString().Contains(TEXT("생존자 추격 중: 없음\n생존자 시야로 정지: 없음")));
    auto* Zero=World->SpawnActor<AMannequinAICharacter>(AlertSpawn);Zero->SetControlSlotForGameMode(0);
    Zero->SetMultiplayActivity(EMultiplayMannequinActivity::Chasing);
    PC->ViewedMannequin=Pawn;PC->SetPawn(nullptr);PC->SetViewTarget(Zero);HUD->RefreshMannequinStates();
    TestTrue(TEXT("HUD uses the visible camera body rather than an older selected actor reference"),HUD->StatusText->GetText().ToString().Contains(TEXT("현재 빙의한 마네킹: 0번")));
    TestTrue(TEXT("Zero is a valid chasing ID, not excluded from alerts"),HUD->StatusText->GetText().ToString().Contains(TEXT("생존자 추격 중: 0번")));
    Zero->SetFrozenBySurvivorVision(true);HUD->RefreshMannequinStates();
    TestTrue(TEXT("Zero is also a valid vision-frozen ID"),HUD->StatusText->GetText().ToString().Contains(TEXT("생존자 시야로 정지: 0번")));
    Zero->SetFrozenBySurvivorVision(false);Zero->SetMultiplayActivity(EMultiplayMannequinActivity::Holding);
    PC->ViewedMannequin=nullptr;
    TestTrue(TEXT("View-only actor remains resolvable without a possessed pawn or mapped owner property"),PC->GetViewedMannequin()==Zero);
    HUD->RefreshMannequinStates();
    TestTrue(TEXT("Resolved view-only camera cannot lose its HUD identity"),HUD->StatusText->GetText().ToString().Contains(TEXT("현재 빙의한 마네킹: 0번")));
    TArray<AMannequinAICharacter*> Bodies={Zero,Chaser,Frozen,Pawn};
    for(int32 Slot=4;Slot<10;++Slot){auto* Body=World->SpawnActor<AMannequinAICharacter>(AlertSpawn);Body->SetControlSlotForGameMode(Slot);Bodies.Add(Body);}
    for(auto* Body:Bodies)
    {
        PC->ViewedMannequin=Pawn;PC->SetViewTarget(Body);HUD->RefreshMannequinStates();
        TestTrue(FString::Printf(TEXT("View-only HUD preserves slot %d"),Body->GetControlSlot()),HUD->StatusText->GetText().ToString().Contains(FString::Printf(TEXT("현재 빙의한 마네킹: %d번"),Body->GetControlSlot())));
    }
    auto* Pending=World->SpawnActor<AMannequinAICharacter>(AlertSpawn);PC->SetViewTarget(Pending);HUD->RefreshMannequinStates();
    TestTrue(TEXT("An actor whose slot has not arrived is labelled as pending, never -1 or none"),HUD->StatusText->GetText().ToString().Contains(TEXT("현재 빙의한 마네킹: 번호 수신 중")));
    Pending->SetControlSlotForGameMode(4);HUD->RefreshMannequinStates();
    TestTrue(TEXT("HUD recovers as soon as slot replication arrives"),HUD->StatusText->GetText().ToString().Contains(TEXT("현재 빙의한 마네킹: 4번")));
    PC->ViewedMannequin=nullptr;PC->SetViewTarget(PC);HUD->RefreshMannequinStates();
    TestTrue(TEXT("A genuinely pending identity is not reduced to a misleading none-only panel"),HUD->StatusText->GetText().ToString()==TEXT("현재 빙의한 마네킹: 정보 동기화 중"));
    PC->SetViewedMannequin(Zero);
    TestTrue(TEXT("Server and local camera selection use the same actor"),PC->GetViewTarget()==Zero && PC->GetViewedMannequin()==Zero);
    Zero->SetActorLocation(FVector(1000000,0,0));Zero->bAlwaysRelevant=false;Zero->SetNetCullDistanceSquared(10000.f);
    PC->AuthenticatedRole=TEXT("Mannequin");
    TestTrue(TEXT("Mannequin role receives a far numbered body for camera selection and tactical status"),Zero->IsNetRelevantFor(PC,Pawn,FVector::ZeroVector));
    PC->AuthenticatedRole=TEXT("Survivor");
    TestFalse(TEXT("Survivor role does not gain global mannequin relevancy"),Zero->IsNetRelevantFor(PC,Pawn,FVector::ZeroVector));
    PC->AuthenticatedRole.Reset();
    TestFalse(TEXT("Single-player/unassigned controller keeps normal relevancy"),Zero->IsNetRelevantFor(PC,Pawn,FVector::ZeroVector));
    PC->bLocalMultiplayerMannequinRole=false;HUD->RefreshMannequinStates();
    TestTrue(TEXT("Survivors cannot see mannequin tactical HUD even if widget remains"),HUD->StatusText->GetText().IsEmpty()&&HUD->StatusText->GetParent()->GetVisibility()==ESlateVisibility::Collapsed);
    TestEqual(TEXT("Removed disguise is no longer an AI activity"),StaticEnum<EMultiplayMannequinActivity>()->GetIndexByNameString(TEXT("Disguised")),INDEX_NONE);
    World->DestroyWorld(false);return true;
}
#endif

#include "ProjectProject01CheckpointPreview.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "ImageUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "ProjectProject01SinglePlayerSaveGame.h"
#include "MultiplayTestGameMode.h"
#include "MannequinAICharacter.h"
#include "AIController.h"
#include "PlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInterface.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCheckpointPreviewDataTest,"ProjectProject01.SinglePlayer.CheckpointPreviewData",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCheckpointPreviewDataTest::RunTest(const FString&)
{
    TArray<FColor> Pixels; Pixels.Init(FColor(40,100,180,255),ProjectCheckpointPreview::Width*ProjectCheckpointPreview::Height);
    TArray64<uint8> Encoded;
    FImageUtils::PNGCompressImageArray(ProjectCheckpointPreview::Width,ProjectCheckpointPreview::Height,Pixels,Encoded);
    auto* Save=NewObject<UProjectProject01SinglePlayerSaveGame>();
    Save->SavedMapPackageName=TEXT("/Game/MyProject/Level/LevelTest01");
    Save->SavedAtUtc=FDateTime(2026,10,16); Save->LastPlayedAtUtc=Save->SavedAtUtc;
    Save->PreviewPng.Append(Encoded.GetData(),static_cast<int32>(Encoded.Num()));
    Save->PreviewCrc=FCrc::MemCrc32(Save->PreviewPng.GetData(),Save->PreviewPng.Num());
    Save->IntegrityHash=Save->CalculateIntegrityHash();
    TestTrue(TEXT("Bounded PNG is valid"),ProjectCheckpointPreview::IsValidImage(Save->PreviewPng,Save->PreviewCrc));
    UTexture2D* Texture=ProjectCheckpointPreview::Decode(Save->PreviewPng,Save->PreviewCrc);
    if (TestNotNull(TEXT("Valid preview decodes"),Texture))
    {
        TestEqual(TEXT("Decoded width"),Texture->GetSizeX(),320);
        TestEqual(TEXT("Decoded height"),Texture->GetSizeY(),180);
    }
    TArray<uint8> Bytes;
    TestTrue(TEXT("Save plus screenshot serializes together"),UGameplayStatics::SaveGameToMemory(Save,Bytes));
    auto* Restored=Cast<UProjectProject01SinglePlayerSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (TestNotNull(TEXT("Checkpoint roundtrips"),Restored))
    {
        TestTrue(TEXT("Screenshot bytes preserved in same save"),Restored->PreviewPng==Save->PreviewPng);
        TestTrue(TEXT("Preview and gameplay integrity preserved"),Restored->HasValidIntegrity() &&
            ProjectCheckpointPreview::IsValidImage(Restored->PreviewPng,Restored->PreviewCrc));
    }
    Save->PreviewPng[0]^=0xFF;
    TestFalse(TEXT("Damaged optional screenshot rejected"),ProjectCheckpointPreview::IsValidImage(Save->PreviewPng,Save->PreviewCrc));
    TestTrue(TEXT("Damaged optional screenshot does not invalidate gameplay save"),Save->HasValidIntegrity());
    TestNull(TEXT("Damaged preview safely falls back"),ProjectCheckpointPreview::Decode(Save->PreviewPng,Save->PreviewCrc));
    TArray<uint8> Oversized; Oversized.SetNumZeroed(ProjectCheckpointPreview::MaximumBytes+1);
    TestFalse(TEXT("Oversized image rejected before decode"),ProjectCheckpointPreview::IsValidImage(Oversized,0));
    TestFalse(TEXT("Old saves may have no image"),ProjectCheckpointPreview::IsValidImage({},0));
    TArray<uint8> NoCapture;
    ProjectCheckpointPreview::Capture(nullptr,nullptr,NoCapture);
    TestTrue(TEXT("No renderer/player cannot break save flow"),NoCapture.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMannequinDisguiseTest,"ProjectProject01.Multiplayer.MannequinDisguise",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMannequinDisguiseTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto* Mode=World->SpawnActor<AMultiplayTestGameMode>();
    auto* Pawn=World->SpawnActor<AMannequinAICharacter>();
    auto* AI=World->SpawnActor<AAIController>(); AI->Possess(Pawn);
    Pawn->SetMultiplayCommand(EMultiplayMannequinCommand::Patrol);
    Mode->MultiplayerDisguiseChance=1; Mode->MultiplayerDisguiseMinSeconds=3; Mode->MultiplayerDisguiseMaxSeconds=3;
    TestFalse(TEXT("No reachable natural place means no arbitrary stationary disguise"),Mode->TryBeginPatrolDisguise(Pawn,0));
    FMultiplayMannequinDisguiseState State;State.RemainingSeconds=3;State.NextAllowedSeconds=23;
    Mode->MultiplayerDisguiseStates.Add(Pawn,State);
    Pawn->SetMultiplayActivity(EMultiplayMannequinActivity::Disguised);
    TestTrue(TEXT("Disguise is an activity, not a changed player command"),
        Pawn->GetMultiplayActivity()==EMultiplayMannequinActivity::Disguised && Pawn->GetMultiplayCommand()==EMultiplayMannequinCommand::Patrol);
    TestTrue(TEXT("Disguise remains motionless briefly"),Mode->ContinuePatrolDisguise(Pawn));
    Pawn->SetFrozenBySurvivorVision(true);Mode->UpdateMultiplayerMannequinAI();
    TestTrue(TEXT("Survivor gaze freezes disguise and marks observation"),
        Pawn->GetMultiplayActivity()==EMultiplayMannequinActivity::FrozenByVision && Mode->MultiplayerDisguiseStates.FindChecked(Pawn).bWasObserved);
    Pawn->SetFrozenBySurvivorVision(false);
    TestFalse(TEXT("After being seen, looking away ends disguise"),Mode->ContinuePatrolDisguise(Pawn));
    TestFalse(TEXT("Cooldown prevents another immediate stop"),Mode->TryBeginPatrolDisguise(Pawn,10));
    TestFalse(TEXT("Expired cooldown still requires an actually reachable place"),Mode->TryBeginPatrolDisguise(Pawn,24));
    State.bApproaching=true;State.Destination=Pawn->GetActorLocation();State.FacingYaw=Pawn->GetActorRotation().Yaw;
    Mode->MultiplayerDisguiseStates.Add(Pawn,State);
    TestTrue(TEXT("Hidden arrival at reserved place can form pose"),Mode->ContinuePatrolDisguise(Pawn));
    TestFalse(TEXT("Arrival ends approach state"),Mode->MultiplayerDisguiseStates.FindChecked(Pawn).bApproaching);
    Mode->MultiplayerDisguiseStates.FindChecked(Pawn).RemainingSeconds=0.1f;
    TestFalse(TEXT("Unobserved disguise expires instead of waiting forever"),Mode->ContinuePatrolDisguise(Pawn));
    State.bApproaching=true;State.ApproachSeconds=16;Mode->MultiplayerDisguiseStates.Add(Pawn,State);
    TestFalse(TEXT("Approach is time bounded"),Mode->ContinuePatrolDisguise(Pawn));
    auto* Survivor=World->SpawnActor<APlayerCharacter>();Survivor->SetActorLocation(FVector(-500,0,0));
    auto* PC=World->SpawnActor<APlayerController>();PC->Possess(Survivor);
    auto* ViewCamera=World->SpawnActor<ACameraActor>();
    ViewCamera->SetActorLocationAndRotation(FVector(-500,0,60),FRotator::ZeroRotator);
    if (!PC->PlayerCameraManager) PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
    PC->PlayerCameraManager->InitializeFor(PC);PC->PlayerCameraManager->bUseClientSideCameraUpdates=false;
    PC->SetViewTarget(ViewCamera);
    PC->SetControlRotation(FRotator::ZeroRotator);PC->PlayerCameraManager->UpdateCamera(0.016f);
    TestTrue(TEXT("Fixture survivor is an active target"),Mode->IsActiveSurvivorTarget(Survivor));
    FVector TestEye;FRotator TestRotation;PC->GetPlayerViewPoint(TestEye,TestRotation);
    TestTrue(TEXT("Placement check uses the actual fixture camera"),TestEye.Equals(ViewCamera->GetActorLocation()) && TestRotation.Equals(FRotator::ZeroRotator));
    TestFalse(TEXT("Visible hypothetical display place is excluded before moving"),Mode->IsDisguiseLocationHidden(Pawn,FVector(0,0,60)));
    auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);Box->SetBoxExtent(FVector(10,400,400));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();
    Wall->SetActorLocation(FVector(-250,0,100));
    TestTrue(TEXT("A real occluding wall can hide the candidate"),Mode->IsDisguiseLocationHidden(Pawn,FVector(0,0,60)));
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ViewCamera->SetActorRotation(FRotator(0,180,0));PC->PlayerCameraManager->UpdateCamera(0.016f);
    TestTrue(TEXT("Candidate outside survivor gaze is hidden"),Mode->IsDisguiseLocationHidden(Pawn,FVector(0,0,60)));
    ViewCamera->SetActorRotation(FRotator::ZeroRotator);PC->PlayerCameraManager->UpdateCamera(0.016f);
    State.bApproaching=true;State.ApproachSeconds=0;State.Destination=FVector(0,0,0);
    Mode->MultiplayerDisguiseStates.Add(Pawn,State);
    TestFalse(TEXT("Looking toward destination during approach cancels pose formation"),Mode->ContinuePatrolDisguise(Pawn));
    Pawn->SetMultiplayCommand(EMultiplayMannequinCommand::HoldPosition);
    TestFalse(TEXT("Hold cannot start disguise"),Mode->TryBeginPatrolDisguise(Pawn,100));
    Pawn->SetMultiplayCommand(EMultiplayMannequinCommand::FollowMannequinPlayer);
    TestFalse(TEXT("Follow cannot start disguise"),Mode->TryBeginPatrolDisguise(Pawn,100));
    Mode->StopAllMannequinsForMatchEnd();
    TestTrue(TEXT("Match end clears all disguise state"),Mode->MultiplayerDisguiseStates.IsEmpty());
    World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCheckpointPreviewRenderTest,"ProjectProject01.SinglePlayer.CheckpointPreviewRender",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectCheckpointPreviewRenderTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* PC=World->SpawnActor<APlayerController>();
    auto* Camera=World->SpawnActor<ACameraActor>();
    Camera->SetActorLocationAndRotation(FVector(-250,0,100),FRotator::ZeroRotator);
    UCameraComponent* Lens=Camera->GetCameraComponent();
    Lens->PostProcessSettings.bOverride_AutoExposureMinBrightness=true;
    Lens->PostProcessSettings.bOverride_AutoExposureMaxBrightness=true;
    Lens->PostProcessSettings.AutoExposureMinBrightness=1;
    Lens->PostProcessSettings.AutoExposureMaxBrightness=1;
    auto* Cube=World->SpawnActor<AStaticMeshActor>();
    Cube->SetMobility(EComponentMobility::Movable);
    Cube->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Cube->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly.VertexColorViewMode_ColorOnly")));
    Cube->SetActorLocation(FVector(0,0,100));
    auto* Light=World->SpawnActor<ADirectionalLight>();Light->SetActorRotation(FRotator(-35,25,0));
    if (!PC->PlayerCameraManager) PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
    PC->PlayerCameraManager->InitializeFor(PC);
    PC->PlayerCameraManager->bUseClientSideCameraUpdates=false;
    PC->SetViewTarget(Camera);PC->PlayerCameraManager->UpdateCamera(0.016f);
    TestTrue(TEXT("Fixture camera updates before capture"),PC->PlayerCameraManager->GetCameraCacheView().Location.Equals(Camera->GetActorLocation()));
    TArray<uint8> Image;ProjectCheckpointPreview::Capture(World,PC,Image);
    const uint32 Crc=Image.IsEmpty()?0:FCrc::MemCrc32(Image.GetData(),Image.Num());
    TestTrue(TEXT("Real scene capture creates a bounded PNG without menu UI"),ProjectCheckpointPreview::IsValidImage(Image,Crc));
    auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    auto Wrapper=Module.CreateImageWrapper(EImageFormat::PNG);TArray64<uint8> Raw;
    int32 BrightPixels=0;
    if (Wrapper->SetCompressed(Image.GetData(),Image.Num()) && Wrapper->GetRaw(ERGBFormat::BGRA,8,Raw))
        for (int64 Index=0;Index+3<Raw.Num();Index+=4) if (FMath::Max3(Raw[Index],Raw[Index+1],Raw[Index+2])>32) ++BrightPixels;
    TestTrue(TEXT("Capture contains visible scene geometry, not an empty black frame"),BrightPixels>1000);
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("CheckpointPreviewTests");
    IFileManager::Get().MakeDirectory(*Directory,true);
    if (!Image.IsEmpty()) FFileHelper::SaveArrayToFile(Image,*(Directory/TEXT("SceneCapture.png")));
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif

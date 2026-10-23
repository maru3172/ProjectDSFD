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
#include "Async/Async.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01SaveSubsystem.h"
#include "ProjectProject01CheckpointStore.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCheckpointAsyncPreviewWriteTest,"ProjectProject01.SinglePlayer.CheckpointAsyncPreviewWrite",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCheckpointAsyncPreviewWriteTest::RunTest(const FString&)
{
    // Strong references keep the fixture alive across the latent frames below (no rooting of game objects).
    TStrongObjectPtr<UProjectProject01GameInstance> Instance(NewObject<UProjectProject01GameInstance>());
    TStrongObjectPtr<UProjectProject01SaveSubsystem> StoreRef(NewObject<UProjectProject01SaveSubsystem>(Instance.Get()));
    UProjectProject01SaveSubsystem* Store=StoreRef.Get();
    Store->TestSlotNamespace=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TArray<FColor> Pixels; Pixels.Init(FColor(40,100,180,255),ProjectCheckpointPreview::Width*ProjectCheckpointPreview::Height);
    TestTrue(TEXT("Encoder prepared on game thread"),ProjectCheckpointPreview::PrepareEncoder());
    TFuture<TArray<uint8>> Encoded=Async(EAsyncExecution::ThreadPool,[Pixels]{TArray<uint8> Png;ProjectCheckpointPreview::EncodePixels(Pixels,Png);return Png;});
    TArray<uint8> Png=Encoded.Get();
    TestTrue(TEXT("Worker-thread PNG is a valid bounded preview"),
        ProjectCheckpointPreview::IsValidImage(Png,Png.IsEmpty()?0:FCrc::MemCrc32(Png.GetData(),Png.Num())));

    auto* Save=NewObject<UProjectProject01SinglePlayerSaveGame>();
    Save->SavedMapPackageName=TEXT("/Game/ReviewCheckpointFixture");Save->CheckpointId=TEXT("AsyncPreviewFixture");
    Save->SavedAtUtc=FDateTime(2026,10,23);Save->LastPlayedAtUtc=Save->SavedAtUtc;Save->SaveRevision=1;
    Store->bSaveInProgress=true;
    Store->PendingWriteSlot=ProjectCheckpointStore::NextWriteSlot(Store->GetPrimarySlotName(1),ProjectCheckpointStore::FSelection());
    Store->PendingWriteSave=Save;Store->PendingSaveGeneration=2;
    Store->WriteCheckpoint(1,TArray<uint8>(Png));
    TestTrue(TEXT("Stale encode result is ignored"),Store->PendingWriteSave==Save&&Save->PreviewPng.IsEmpty()&&Store->PendingSaveHash==0);

    Store->WriteCheckpoint(2,TArray<uint8>(Png));
    TestTrue(TEXT("Preview bytes and CRC stored as before"),Save->PreviewPng==Png&&Save->PreviewCrc==FCrc::MemCrc32(Png.GetData(),Png.Num()));
    TestTrue(TEXT("Integrity hash covers the final preview"),Save->IntegrityHash==Save->CalculateIntegrityHash()&&Store->PendingSaveHash==Save->IntegrityHash);
    TestNull(TEXT("Pending save object released once written"),Store->PendingWriteSave.Get());
    // UE 5.8 delivers SaveGameAsync completion on a later core-ticker tick, so wait across frames.
    const uint32 ExpectedHash=Save->IntegrityHash;
    const double Deadline=FPlatformTime::Seconds()+10;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Instance,StoreRef,ExpectedHash,Deadline]()
    {
        UProjectProject01SaveSubsystem* Saves=StoreRef.Get();
        if(Saves->bSaveInProgress&&FPlatformTime::Seconds()<Deadline)return false;
        TestFalse(TEXT("Async file write completes"),Saves->bSaveInProgress);
        const auto Selection=ProjectCheckpointStore::SelectNewest(Saves->GetPrimarySlotName(1),
            [Saves](const FString& Slot){FString Error;return Saves->LoadValidatedSlot(Slot,Error);});
        const auto* Written=Selection.Save;
        TestTrue(TEXT("Committed generation is the one just written"),Written&&Written->SaveRevision==1&&Written->IntegrityHash==ExpectedHash);
        if(TestNotNull(TEXT("Written checkpoint passes integrity validation"),Written))
            TestTrue(TEXT("Saved preview decodes after reload"),ProjectCheckpointPreview::IsValidImage(Written->PreviewPng,Written->PreviewCrc));
        for(int32 Index=1;Index<=3;++Index)Saves->DeleteSaveSlot(Index);
        return true;
    }));
    return true;
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

    // Async readback path used by checkpoint saves: same view, completed over later frames.
    struct FAsyncResult { bool bDone=false; TArray<FColor> Pixels; double GameThreadMs=0; };
    const TSharedRef<FAsyncResult> Async=MakeShared<FAsyncResult>();
    const double Start=FPlatformTime::Seconds();
    const bool bStarted=ProjectCheckpointPreview::CapturePixelsAsync(World,PC,[Async](TArray<FColor>&& Pixels)
    {
        check(IsInGameThread());
        Async->Pixels=MoveTemp(Pixels);Async->bDone=true;
    });
    Async->GameThreadMs=(FPlatformTime::Seconds()-Start)*1000.0;
    TestTrue(TEXT("Async capture starts in a standalone render world"),bStarted);
    TestFalse(TEXT("Async capture never completes inside the request call"),Async->bDone);
    const int32 SyncBright=BrightPixels;
    TWeakObjectPtr<UWorld> WeakWorld(World);
    const double Deadline=FPlatformTime::Seconds()+10;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Async,WeakWorld,SyncBright,Deadline,bStarted]()
    {
        if (bStarted && !Async->bDone && FPlatformTime::Seconds()<Deadline) return false;
        TestTrue(TEXT("Async readback completes"),Async->bDone);
        TestEqual(TEXT("Async readback returns a full preview"),Async->Pixels.Num(),ProjectCheckpointPreview::Width*ProjectCheckpointPreview::Height);
        int32 AsyncBright=0;
        for (const FColor& Pixel:Async->Pixels) { if (FMath::Max3(Pixel.R,Pixel.G,Pixel.B)>32) ++AsyncBright; }
        TestTrue(TEXT("Async capture contains the same visible geometry"),AsyncBright>1000 && FMath::Abs(AsyncBright-SyncBright)<=SyncBright/10);
        TestTrue(TEXT("Async pixels are opaque"),Async->Pixels.Num()>0 && Async->Pixels[0].A==255);
        TArray<uint8> Png;ProjectCheckpointPreview::EncodePixels(Async->Pixels,Png);
        TestTrue(TEXT("Async pixels encode to a valid preview"),ProjectCheckpointPreview::IsValidImage(Png,Png.IsEmpty()?0:FCrc::MemCrc32(Png.GetData(),Png.Num())));
        AddInfo(FString::Printf(TEXT("Async capture request game-thread time: %.2f ms"),Async->GameThreadMs));
        if (UWorld* Fixture=WeakWorld.Get()) { Fixture->DestroyWorld(false);GEngine->DestroyWorldContext(Fixture); }
        return true;
    }));
    return true;
}
#endif

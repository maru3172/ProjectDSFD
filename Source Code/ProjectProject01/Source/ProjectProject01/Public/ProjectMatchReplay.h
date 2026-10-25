#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectMatchReplay.generated.h"

struct PROJECTPROJECT01_API FProjectLocalReplay
{
    FString Name;
    FString Description;
    FDateTime RecordedAt;
    int32 DurationMs = 0;
    int64 Bytes = 0;
};

/** Local UE network replays. No voice, credentials, result submission or server rewind. */
UCLASS()
class PROJECTPROJECT01_API UProjectMatchReplay final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
    friend class FProjectReplayContractTest;
    friend class FProjectReplayPlayTest;
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void RefreshList();
    bool Play(const FString& Name);
    void Delete(const FString& Name);
    void ExitPlayback();
    void SetRecordingEnabled(bool Enabled);
    bool IsRecordingEnabled() const { return bRecordingEnabled; }
    bool IsBusy() const { return bListing || bDeleting || bPlaybackPending; }
    bool IsPlaybackRequested() const { return bPlaybackPending; }
    const TArray<FProjectLocalReplay>& GetEntries() const { return Entries; }
    const FString& GetStatus() const { return Status; }
    FSimpleMulticastDelegate OnChanged;
    static bool IsOwnedName(const FString& Name);
private:
    bool Tick(float Delta);
    void StopRecording();
    void SessionCleared();
    TArray<FString> StreamOptions(bool RecordMapChanges=false)const;
    TSharedPtr<class INetworkReplayStreamer> Streamer;
    TWeakObjectPtr<class UDemoNetDriver> RecordingDriver;
    TWeakObjectPtr<UWorld> PlaybackDepartureWorld;
    FTSTicker::FDelegateHandle Ticker;
    TArray<FProjectLocalReplay> Entries;
    FString RecordingName, RecordingMatch, BlockedMatch, Status;
    double PlaybackStartedAt = 0;
    bool bRecordingEnabled = false, bListing = false, bDeleting = false, bPlaybackPending = false;
    bool bCatalogueReady = false;
    int64 ListedBytes = 0;
    bool bAllowPersistence = false;
};

/** Read-only replay spectator: has no multiplayer input/RPC bindings. */
UCLASS()
class PROJECTPROJECT01_API AProjectReplayController final : public APlayerController
{
    GENERATED_BODY()
    friend class FProjectReplayPlayTest;
public:
    AProjectReplayController();
    virtual void BeginPlay() override;
    virtual void ReceivedPlayer() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void PlayerTick(float Delta) override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
private:
    void EnsureReplayUI();
    void Seek(float Seconds);
    TSharedPtr<class SWidget> Controls;
    bool bPaused = false, bSeeking = false;
    bool bScrubbing = false;
    float ScrubTarget = 0;
    float PlaybackSpeed = 1.f;
    double SeekStartedAt = 0;
};

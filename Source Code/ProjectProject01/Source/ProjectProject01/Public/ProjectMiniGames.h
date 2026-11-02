#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectMiniGames.generated.h"

class FJsonObject;

/**
 * Talks to /api/minigames (Docs/MiniGames.md): open runs to continue, personal bests, the 한 칸씩 stages, saves,
 * 움직였다 grading, finishes and the boards. Requests run side by side (a save while a board loads) and every answer
 * goes back to the caller only if the same account is still signed in.
 *
 * The game being played registers a pause-and-save callback; it runs when the phone closes, the level changes (a
 * match starts) or the account signs out, so a run is never lost and never keeps running unseen.
 */
UCLASS()
class PROJECTPROJECT01_API UProjectMiniGames final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    using FReply=TFunction<void(TSharedPtr<FJsonObject> Json,const FString& Error)>;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    void Refresh(FReply Done);
    void Start(const FString& Game,const FString& Stage,FReply Done);
    void Save(const FString& RunId,int32 ActiveMs,const TSharedRef<FJsonObject>& State,FReply Done=nullptr);
    void SpotAnswer(const FString& RunId,int32 Round,const TArray<int32>& Picks,int32 ActiveMs,FReply Done);
    void Finish(const FString& RunId,bool bCleared,int32 ActiveMs,int32 Battery,const TArray<FString>& Moves,FReply Done);
    void Abandon(const FString& Game,FReply Done=nullptr);
    void Leaderboard(const FString& Game,const FString& Board,bool bWeekly,FReply Done);

    /** The phone is sideways (games) until the player turns it back, a match starts or the account signs out. */
    bool bPhoneLandscape=false;
    /** The last /api/minigames answer (runs, best, stages, week). */
    TSharedPtr<FJsonObject> Home;
    /** The playing game's pause-and-save, called before the session ends or the level changes. */
    void SetActiveGame(const void* Owner,TFunction<void()> PauseAndSave);
    void ClearActiveGame(const void* Owner);
    void SuspendActiveGame();
    /** The last state sent for each run, kept until the server has it (continue offline-safe within this session). */
    TMap<FString,TPair<int32,TSharedPtr<FJsonObject>>> LocalSaves;
private:
    void Send(const FString& Verb,const FString& Path,const TSharedPtr<FJsonObject>& Body,FReply Done);
    void HandlePreLoadMap(const FString& Map);
    FDelegateHandle PreLoadMapHandle,ClearingHandle;
    const void* ActiveOwner=nullptr;
    TFunction<void()> ActivePause;
};

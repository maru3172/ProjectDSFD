// File: Source/ProjectProject01/Public/ProjectMatchWait.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "ProjectMatchWait.generated.h"

DECLARE_MULTICAST_DELEGATE(FProjectMatchWaitOpenView);

/**
 * Waiting for a match while doing something else (Docs/MatchWait.md).
 * While the player is random matching or sits in an open match room, a small panel on the left of the screen shows the
 * state (time and head count for random matching; names, readiness and the room's buttons for an open room) in the
 * lobby, the tutorial and practice. Outside the lobby it keeps the matchmaking or room state fresh itself, and when the
 * match starts it says so, fades to black and goes to the lobby, whose usual start flow takes the player into the game.
 * Not shown in a match, for bot matches, or while the lobby itself shows the room or matching screen.
 */
UCLASS()
class PROJECTPROJECT01_API UProjectMatchWait final:public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer)const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection)override;
    virtual void Deinitialize()override;

    /** The lobby tells whether its room / matching screen is up (then the panel would only repeat it). */
    void SetLobbyMatchViewShown(bool bShown){bLobbyMatchViewShown=bShown;}
    /** "방 확인" in the lobby: the lobby brings its room or matching screen back. */
    FProjectMatchWaitOpenView OnOpenMatchView;

    /** True while there is something to wait for: random matching, or an open (not bot) match room. */
    bool IsWaiting()const;
    /** True once that wait has turned into a match that is starting (the lobby's start flow takes over). */
    bool IsStarting()const;
    /** Away from the lobby the start is counted there (5..1) before going to the lobby; this is the notice it shows. */
    FString StartNoticeText()const;
    /** True once, right after that count: the lobby then goes straight into the match instead of counting again. */
    bool ConsumeCountedDownAway();

    enum class EPlace:uint8{Elsewhere,Lobby,Practice,Match};
    EPlace Place()const;

    // Panel actions (also used by the panel's buttons).
    void OpenMatchView();
    void ToggleReady();
    void StartRoom();
    void LeaveOrCancel();

private:
    bool Tick(float Delta);
    void EnsurePanel();
    void BeginStartTransition();
    void ReleaseCursor();
    class UProjectProject01LobbySubsystem* Lobby()const;

    FTSTicker::FDelegateHandle TickHandle;
    TWeakObjectPtr<class UGameViewportClient> Viewport;
    TSharedPtr<class SWidget> Panel;
    bool bLobbyMatchViewShown=false,bTransition=false,bCursorHeld=false,bCursorWasShown=false;
    double NextPollAt=0,TransitionAt=0;
    int32 TransitionStage=0;
    // The lobby sends one request at a time and refreshes the room often. A panel action pressed during a refresh waits
    // here (latest wins, up to 4 s) and runs as soon as the lobby is free, instead of being dropped.
    TFunction<void()> PendingAction;double PendingActionUntil=0;
    // "로비로" from practice darkens the screen and loads the lobby; the lobby itself never lifts that dark, so we do on arrival.
    bool bRevealOnLobby=false;
    double CountedAwayAt=-100;bool bHoldDarkInLobby=false;
    void RunWhenFree(TFunction<void()> Action);
};

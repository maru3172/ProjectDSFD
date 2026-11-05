// File: Source/ProjectProject01/Public/ProjectRuleVote.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProjectRuleVote.generated.h"

/** What one player sees of the open rule-card vote (the server keeps the votes). */
USTRUCT()
struct FProjectRuleVoteView
{
    GENERATED_BODY()
    UPROPERTY() int32 Serial=0;
    UPROPERTY() TArray<FString> Cards;   // ProjectPlaygroundVote card ids.
    UPROPERTY() TArray<int32> Counts;    // Votes per card so far (the vote is open).
    UPROPERTY() int32 MyVote=-1;
    UPROPERTY() int32 Voted=0;
    UPROPERTY() int32 Voters=0;
    UPROPERTY() float SecondsLeft=0;
};

/**
 * The custom game rule-card vote on the player's screen (Docs/CustomGamePlayground.md 12.2). Lives on the match player
 * controller and works like the item card pick: while the vote is open the loading screen stays black, the panel takes
 * the input (mouse or 1/2/3) and the client only names one of the three cards. A vote can be changed until it closes.
 */
UCLASS(ClassGroup=(Multiplayer))
class PROJECTPROJECT01_API UProjectRuleVoteComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UProjectRuleVoteComponent();
    UFUNCTION(Client,Reliable) void ClientShowVote(const FProjectRuleVoteView& InView);
    UFUNCTION(Client,Reliable) void ClientCloseVote();
    UFUNCTION(Server,Reliable,WithValidation) void ServerVote(int32 Serial,int32 Index);

    bool IsVoteOpen()const{return Panel.IsValid();}
    const FProjectRuleVoteView& GetView()const{return View;}
    void VoteLocal(int32 Index);
    /** Builds the panel for a view without a player (screenshots and the automation test). */
    static TSharedRef<class SWidget> MakePanelForTest(const FProjectRuleVoteView& View);

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason)override;
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTick)override;

private:
    FProjectRuleVoteView View;
    double ViewReceivedAt=0;
    bool bInputHeld=false,bCursorWasShown=false;
    TSharedPtr<class SProjectRuleVotePanel> Panel;
    TWeakObjectPtr<class UGameViewportClient> Viewport;
    void OpenPanel();
    void ClosePanel();
    void HoldInput(bool bHold);
};

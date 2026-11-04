// File: Source/ProjectProject01/Public/ProjectRoguePick.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProjectItemRoguelike.h"
#include "ProjectRoguePick.generated.h"

/**
 * The item roguelike card pick on the player's screen (Docs/ItemRoguelikeMode.md). Lives on the match player controller.
 * The server sends the offer; while it is open the screen stays black, the pick panel takes all input (mouse, 1/2/3,
 * R for the one reroll) and the game ignores the usual controls. The client only names one of the offered cards.
 */
UCLASS(ClassGroup=(Multiplayer))
class PROJECTPROJECT01_API UProjectRoguePickComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UProjectRoguePickComponent();
    UFUNCTION(Client,Reliable) void ClientShowOffer(const FProjectRogueOffer& InOffer);
    UFUNCTION(Client,Reliable) void ClientClosePick();
    UFUNCTION(Server,Reliable,WithValidation) void ServerPickCard(int32 Serial,int32 Index);
    UFUNCTION(Server,Reliable,WithValidation) void ServerRerollCards(int32 Serial);

    bool IsPickOpen()const{return Panel.IsValid();}
    const FProjectRogueOffer& GetOffer()const{return Offer;}
    /** Local choice from the panel (mouse or keys). */
    void ChooseLocal(int32 Index);
    void RerollLocal();
    /** Builds the panel widget for an offer without a player (screenshots and the automation test). */
    static TSharedRef<class SWidget> MakePanelForTest(const FProjectRogueOffer& Offer,const FString& Role);

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason)override;
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTick)override;

private:
    FProjectRogueOffer Offer;
    double OfferReceivedAt=0;
    bool bRequestSent=false,bInputHeld=false,bCursorWasShown=false;
    TSharedPtr<class SProjectRoguePickPanel> Panel;
    TWeakObjectPtr<class UGameViewportClient> Viewport;
    void OpenPanel();
    void ClosePanel();
    void HoldInput(bool bHold);
    FString LocalRole()const;
};

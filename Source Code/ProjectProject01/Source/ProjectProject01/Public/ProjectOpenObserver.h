#pragma once
#include "CoreMinimal.h"
#include "MultiplayTestPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "ProjectOpenObserver.generated.h"

USTRUCT()
struct FProjectOpenObserverEntry
{
    GENERATED_BODY()
    UPROPERTY() FString PlayerId;
    UPROPERTY() FString Name;
    UPROPERTY() FString Role;
    UPROPERTY() FString State;
};

/** A separately admitted spectator: never a survivor, mannequin, ready vote or voice participant. */
UCLASS()
class PROJECTPROJECT01_API AProjectOpenObserverController final : public AMultiplayTestPlayerController
{
    GENERATED_BODY()
public:
    void Admit();
    virtual void PlayerTick(float Delta) override;
    virtual void TickActor(float Delta,ELevelTick Kind,FActorTickFunction& TickFunction) override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Select(const FString& PlayerId,bool Locked);
    AActor* GetObservedActor()const{return LocalTarget.Get();}
    UFUNCTION(Server,Reliable) void ServerSelect(const FString& PlayerId,bool Locked);
    UFUNCTION(Client,Reliable) void ClientEnterObservation();
    UFUNCTION(Client,Unreliable) void ClientObservationState(const TArray<FProjectOpenObserverEntry>& Entries,const FString& Summary,bool Playing);
    UFUNCTION(Client,Reliable) void ClientObservationTarget(AActor* Target,bool Locked,FRotator Rotation);
    UFUNCTION(Client,Unreliable) void ClientObservedRotation(FRotator Rotation);
    UFUNCTION(Client,Reliable) void ClientCloseObservation();
    UFUNCTION(Client,Reliable) void ClientFinishObservation(const FMultiplayTestMatchResult& Result);
private:
    UPROPERTY(Transient) TObjectPtr<class UProjectOpenObserverWidget> ObserverWidget;
    TWeakObjectPtr<AActor> LocalTarget,ServerTarget;
    TWeakObjectPtr<AMultiplayTestPlayerController> StreamedPlayer;
    FString Selected;
    double NextStateAt=0,NextPanelAt=0,LastSelectionAt=-100;
    bool bLocked=false,bRevealed=false,bResultDelivered=false,bTargetDirty=true;
    FRotator ReceivedRotation=FRotator::ZeroRotator;
    FTimerHandle ObserverEndTimer;
};

UCLASS()
class PROJECTPROJECT01_API UProjectObserverTargetBinding final : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<class UProjectOpenObserverWidget> Owner;
    FString Id;
    UFUNCTION() void Click();
};

UCLASS()
class PROJECTPROJECT01_API UProjectOpenObserverWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual bool Initialize() override;
    void Refresh(const TArray<FProjectOpenObserverEntry>& Entries,const FString& Summary);
    void Choose(const FString& Id);
    UFUNCTION() void FoldViews();
    UFUNCTION() void FoldProgress();
    UFUNCTION() void LockChanged(bool Checked);
private:
    UPROPERTY() TObjectPtr<class UVerticalBox> Views;
    UPROPERTY() TObjectPtr<class UTextBlock> Progress;
    UPROPERTY() TObjectPtr<class UCheckBox> Lock;
    UPROPERTY() TArray<TObjectPtr<UProjectObserverTargetBinding>> Bindings;
    UPROPERTY() TArray<TObjectPtr<class UTextBlock>> Names;
    TArray<FString> Keys;
    FString Selected;
};

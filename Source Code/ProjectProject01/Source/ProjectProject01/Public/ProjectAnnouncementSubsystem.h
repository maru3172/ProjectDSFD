#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "ProjectAnnouncementSubsystem.generated.h"

/** Public operator notices survive UI, role and level changes. */
UCLASS()
class PROJECTPROJECT01_API UProjectAnnouncementSubsystem final:public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer)const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection)override;
    virtual void Deinitialize()override;
    void NotifyAchievement(const FString& Code);
    void NotifyQuest(const FString& Name,const FString& Kind,const FString& Detail,const FString& EventId);
private:
    void EnsurePresentation();
    bool Poll(float Delta);
    UFUNCTION() void ServiceChanged(bool Maintenance,const FString& Notice,const FString& ShutdownAt,const FString& Message);
    FTSTicker::FDelegateHandle PollHandle;
    TWeakObjectPtr<class UGameViewportClient> Viewport;
    TSharedPtr<class SProjectAnnouncementTicker> Overlay;
    TSharedPtr<class SWidget> AchievementOverlay;
    FString SeenBase,SeenBroadcast;
    bool bAwaitBroadcastBaseline=true;
    double NextPoll=0;
};

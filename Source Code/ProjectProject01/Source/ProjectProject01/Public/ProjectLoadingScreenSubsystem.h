#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "ProjectLoadingScreenSubsystem.generated.h"

USTRUCT()
struct FProjectLoadingParticipant
{
	GENERATED_BODY()
	UPROPERTY() FString UserId;
	UPROPERTY() FString DisplayName;
	UPROPERTY() uint8 Progress=0;
};

struct FProjectLoadingScreenState;
class SWidget;
class UGameViewportClient;

/** Owns loading presentation across world replacement. No gameplay authority lives here. */
UCLASS()
class PROJECTPROJECT01_API UProjectLoadingScreenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void BeginLoading(bool bMultiplayer);
	/** Lobby allocation and ticket checks stay in this same loading presentation. */
	void ShowSessionPreparation(const FString& Stage,uint8 Progress,bool bCanCancel,FSimpleDelegate OnCancel=FSimpleDelegate());
	void BeginServerTravel();
	void BeginMultiplayerGate(const FString& Epoch,const FString& OwnId,const FString& OwnName,int32 Expected,bool bReleased);
	void UpdateParticipants(const FString& Epoch,const TArray<FProjectLoadingParticipant>& Participants);
	void ReleaseMultiplayerGate(const FString& Epoch);
	void NotifyViewReady();
	uint8 GetLocalProgress() const;
	bool FinishPresentation(float RevealSeconds);
	void CancelLoading();
	bool IsLoading() const;
private:
	friend class FProjectLoadingPresentationTest;
	friend class FProjectLobbySessionPresentationTest;
	void RequestCancelPreparation();
	void PreLoadMap(const struct FWorldContext& Context,const FString& Map);
	void PostLoadMap(UWorld* World);
	bool TickLoading(float Delta);
	void AttachOverlay();
	void FailLoading(const FString& Message);
	TSharedPtr<FProjectLoadingScreenState,ESPMode::ThreadSafe> State;
	TSharedPtr<SWidget> Overlay;
	TWeakObjectPtr<UGameViewportClient> AttachedViewport;
	TWeakObjectPtr<UWorld> DepartureWorld;
	FTSTicker::FDelegateHandle Ticker;
	FDelegateHandle PreLoadHandle,PostLoadHandle;
	FString EpochId;
	bool bAllowCurrentWorld=false;
	float PendingRevealSeconds=1.5f;
	bool bAwaitingServer=false;
	uint8 PreparationProgress=0;
	double PhaseStartedAt=0;
	double WorldLoadingStartedAt=0;
	FSimpleDelegate PreparationCancelAction;
};

#pragma once

#include "InteractionActorBase.h"
#include "MultiplayTestGameMode.h"
#include "ProjectMultiplayerRescue.generated.h"

class AMannequinAICharacter;
class APlayerCharacter;

UCLASS()
class PROJECTPROJECT01_API AProjectMultiplayerRescueStation : public AInteractionActorBase
{
	GENERATED_BODY()
	friend class AProjectMultiplayerRescueGameMode;

public:
	AProjectMultiplayerRescueStation();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual FText GetInteractionText_Implementation(APawn* Interactor) const override;
	bool TryInteractFromView(APawn* Interactor, const FVector& Origin, const FVector& Direction);
	APlayerCharacter* GetDetainedSurvivor() const { return DetainedSurvivor; }
	bool IsAttemptInProgress() const { return AttemptingSurvivor != nullptr; }
	double GetAttemptAge() const;
	int32 GetAlarmCount() const { return AlarmCount; }
	bool IsGateOpen() const { return bGateOpen; }

protected:
	virtual void BeginPlay() override;
	virtual void HandleInteraction(APawn* Interactor) override;

private:
	UPROPERTY(EditInstanceOnly, Category="Rescue") FVector DetentionOffset = FVector(230,0,-34);
	UPROPERTY(Replicated) TObjectPtr<APlayerCharacter> DetainedSurvivor;
	UPROPERTY(Replicated) TObjectPtr<APlayerCharacter> AttemptingSurvivor;
	UPROPERTY(Replicated) double AttemptStartedAt = 0.;
	UFUNCTION(NetMulticast, Unreliable) void MulticastRescueAlarm();
	UFUNCTION() void OnRep_GateOpen();
	void SetGateOpen(bool bOpen);
	UPROPERTY(ReplicatedUsing=OnRep_GateOpen) bool bGateOpen = false;
	UPROPERTY(Transient) TObjectPtr<AActor> Gate;
	FVector ClosedGateLocation = FVector::ZeroVector;
	int32 AlarmCount = 0;
};

UCLASS()
class PROJECTPROJECT01_API AProjectMultiplayerRescueGameMode : public AMultiplayTestGameMode
{
	GENERATED_BODY()

public:
	bool TryDetain(APlayerCharacter& Survivor, AMannequinAICharacter& Catcher);
	bool CanFree(const AProjectMultiplayerRescueStation& Source, const APlayerCharacter& Rescuer) const;
	void UseStation(AProjectMultiplayerRescueStation& Source, APlayerCharacter& Rescuer);
	virtual void Logout(AController* Exiting) override;

protected:
	virtual void BeginPlay() override;

private:
	void FinishCapturePresentation();
	void ResolveDetentionWithoutRescuer();
	void ExpireAttempt();
	void ClearAttempt();
	void RaiseAlarm(APlayerCharacter* Rescuer);
	void Free(AProjectMultiplayerRescueStation& Source, APlayerCharacter& Rescuer);
	void ClearDetention();
	UPROPERTY(Transient) TObjectPtr<AProjectMultiplayerRescueStation> Station;
	TSet<TWeakObjectPtr<APlayerCharacter>> PreviouslyDetained;
	TWeakObjectPtr<APlayerCharacter> PendingDetention;
	TWeakObjectPtr<AMannequinAICharacter> LastCatcher;
	FVector CaptureOrigin = FVector::ZeroVector;
	FTimerHandle CaptureHandoffTimer;
	FTimerHandle RescuerCheckTimer;
	FTimerHandle AttemptTimer;
};

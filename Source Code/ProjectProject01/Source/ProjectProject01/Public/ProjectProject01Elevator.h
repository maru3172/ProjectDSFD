#pragma once

#include "CoreMinimal.h"
#include "ProjectProject01LevelTransitionTrigger.h"
#include "InteractionInterface.h"
#include "ProjectProject01Elevator.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UProjectElevatorSynthComponent;
class APlayerStart;

UENUM(BlueprintType)
enum class EProjectElevatorPhase : uint8
{
	Locked, Ready, CallDelay, Rising, Opening, Open, Closing, Travelling,
	WaitingForArrival, ArrivalRide, Disabled, Descending
};

USTRUCT()
struct FProjectElevatorState
{
	GENERATED_BODY()
	UPROPERTY() EProjectElevatorPhase Phase = EProjectElevatorPhase::Locked;
	UPROPERTY() double StartedAt = 0;
	UPROPERTY() float DoorStart = 0;
};

/** 배치한 버튼 볼륨에 상호작용 시선이 적중해야 작동하는 서버 권위 엘리베이터. */
UCLASS(Blueprintable)
class PROJECTPROJECT01_API AProjectProject01Elevator final : public AProjectProject01LevelTransitionTrigger, public IInteractionInterface
{
	GENERATED_BODY()
	friend class FProjectElevatorTest;
public:
	AProjectProject01Elevator();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual FText GetInteractionText_Implementation(APawn* Interactor) const override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual void Interact_Implementation(APawn* Interactor) override;
	UFUNCTION(BlueprintPure, Category="Elevator") EProjectElevatorPhase GetElevatorPhase() const { return State.Phase; }
	/** Validates the click-time view on the server, not the remote camera's stale cache. */
	bool TryInteractFromView(APawn* Interactor, const FVector& ViewOrigin, const FVector& ViewDirection, float Range, ECollisionChannel Channel);
	/** GameMode가 Pawn 생성 전에 호출하므로 화면 공개 후 순간이동하지 않습니다. */
	static AActor* FindArrivalStart(UWorld* World, const FString& Options);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void RollbackTransition(const FString& Reason) override;
	virtual float GetTransitionFadeSeconds() const override { return FMath::Clamp(DepartureFadeSeconds,0.1f,10.0f); }
	virtual bool ShouldFadeToWaitingSpectator() const override { return false; }
	virtual float GetTravelDelaySeconds() const override { return GetTransitionFadeSeconds()+0.05f; }
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator|Components") TObjectPtr<USceneComponent> CabinRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator|Components") TObjectPtr<UStaticMeshComponent> CabinMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator|Components") TObjectPtr<UStaticMeshComponent> LeftDoor;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator|Components") TObjectPtr<UStaticMeshComponent> RightDoor;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator|Components") TArray<TObjectPtr<UStaticMeshComponent>> CabinWalls;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator|Components") TObjectPtr<UBoxComponent> CallButtonVolume;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator|Components") TObjectPtr<UBoxComponent> InsideButtonVolume;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator|Components") TObjectPtr<UBoxComponent> CabinVolume;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator|Components") TObjectPtr<USceneComponent> ArrivalSpawn;
	UPROPERTY(VisibleAnywhere, Category="Elevator|Audio") TObjectPtr<UProjectElevatorSynthComponent> ElevatorAudio;
	/** 목적지에는 동일한 ArrivalId의 엘리베이터를 한 개만 배치합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Arrival") FName ArrivalId = TEXT("Elevator_1F");
	/** 도착 맵에서는 켜 두세요. 목적지 지정/호출을 요구하지 않습니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Arrival") bool bArrivalOnly = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Timing", meta=(ClampMin="0", ClampMax="300", Units="s")) float CallDelaySeconds = 20;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Timing", meta=(ClampMin="0.1", ClampMax="60", Units="s")) float RiseSeconds = 4;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Timing", meta=(ClampMin="0.1", ClampMax="15", Units="s")) float DoorSeconds = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Timing", meta=(ClampMin="0.1", ClampMax="15", Units="s")) float CloseDoorSeconds = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Timing", meta=(ClampMin="0.1", ClampMax="30", Units="s")) float DescentSoundSeconds = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Timing", meta=(ClampMin="0.1", ClampMax="10", Units="s")) float DepartureFadeSeconds = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Timing", meta=(ClampMin="0.7", ClampMax="30", Units="s")) float ArrivalRideSeconds = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Movement") FVector BelowLandingOffset = FVector(0,0,-800);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Movement") FVector LeftDoorOpenOffset = FVector(0,-150,0);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Movement") FVector RightDoorOpenOffset = FVector(0,150,0);
private:
	UPROPERTY(ReplicatedUsing=OnRep_State) FProjectElevatorState State;
	UFUNCTION() void OnRep_State();
	void SetPhase(EProjectElevatorPhase NewPhase);
	void AdvanceServerState();
	void UpdateVisuals();
	bool IsEligibleInteractor(const APawn* Pawn) const;
	bool IsInsideCabin(const APawn* Pawn) const;
	bool CanUseButton(APawn* Interactor, const UPrimitiveComponent* Button) const;
	void BeginButtonInteraction(APawn* Interactor);
	UPrimitiveComponent* TraceButton(APawn* Pawn) const;
	double ServerNow() const;
	float PhaseElapsed() const;
	float MotorGainForPhase() const;
	float DoorFraction() const;
	UPROPERTY(Transient) TObjectPtr<APlayerStart> ArrivalStart;
	TWeakObjectPtr<APlayerCharacter> Passenger;
	FVector CabinHome = FVector::ZeroVector, LeftDoorHome = FVector::ZeroVector, RightDoorHome = FVector::ZeroVector;
	bool bPresentationReady = false;
	bool bMotorPlaying = false;
	float LastMotorGain = -1;
	double LastDingAt = -100;
};

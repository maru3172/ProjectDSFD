#pragma once

#include "CoreMinimal.h"
#include "InteractionActorBase.h"
#include "ProjectPracticeGameModeBase.h"
#include "ProjectSingleIntro.generated.h"

class ACameraActor;
class ACharacter;
class ATriggerBox;
class SWidget;
class UAnimSequence;
class UGameViewportClient;
class ULightComponent;
class UPoseableMeshComponent;
class UPlayerInteractionComponent;
class USkeletalMeshComponent;
class USoundBase;
class USoundWaveProcedural;

UCLASS()
class PROJECTPROJECT01_API AProjectSingleIntroTarget : public AInteractionActorBase
{
	GENERATED_BODY()

public:
	AProjectSingleIntroTarget();
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual FText GetInteractionText_Implementation(APawn* Interactor) const override;
	void MoveToward(const FVector& Destination, float Speed, float DeltaSeconds);
	void SetWalking(bool bWalking);

	UPROPERTY(EditInstanceOnly, Category="Single Intro") FName Action;
	UPROPERTY(EditInstanceOnly, Category="Single Intro") FName ItemId;
	UPROPERTY(EditInstanceOnly, Category="Single Intro") FText ItemName;
	UPROPERTY(VisibleAnywhere, Category="Single Intro") TObjectPtr<USkeletalMeshComponent> CharacterMesh;
	UPROPERTY(EditInstanceOnly, Category="Single Intro") TObjectPtr<UAnimSequence> IdleAnimation;
	UPROPERTY(EditInstanceOnly, Category="Single Intro") TObjectPtr<UAnimSequence> WalkAnimation;

protected:
	virtual void BeginPlay() override;
	virtual void HandleInteraction(APawn* Interactor) override;

private:
	bool bWalking = false;
};

UCLASS()
class PROJECTPROJECT01_API AProjectSingleIntroGameMode : public AProjectPracticeGameModeBase
{
	GENERATED_BODY()
	friend class FProjectSingleIntroPIETest;

public:
	AProjectSingleIntroGameMode();
	virtual void Tick(float DeltaSeconds) override;
	virtual bool HasCustomOpeningPresentation() const override { return true; }
	bool CanUse(const AProjectSingleIntroTarget& Target, const APawn* Player) const;
	FText GetUseText(const AProjectSingleIntroTarget& Target) const;
	void Use(AProjectSingleIntroTarget& Target, APawn* Player);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	enum class EPhase : uint8
	{
		Waiting, Phone, EnterStore, Shopping, InspectHat, Fire, Meet, Talk, LeaveTogether, Agree,
		WalkToShutter, PartnerInjured,
		ReadyToLeave, Finale, TitleCard
	};
	void Enter(EPhase Next);
	void LockControls(bool bLock);
	void TurnToward(const FVector& Point, float MinimumSeconds);
	void UpdateHatShot(APawn& Player);
	void PoseHatReach(ACharacter& Player, float Reach);
	void FinishHatShot();
	void WalkIntoStore(APawn& Player);
	void SetPhonePose(ACharacter& Player, UAnimSequence* Animation);
	FVector UpdateHeldPhone(ACharacter& Player);
	float GetOpeningFade() const;
	void FollowPartner(const APawn& Player, float DeltaSeconds);
	float GetLastOutageTime() const;
	void PlayAlarm();
	void PlayPartnerScream();
	void PlayImpact(const FVector& Position, float Pitch, float Volume) const;
	void BuildOverlay();
	TSharedRef<SWidget> BuildCinematicMask();
	FText GetSubtitle() const;
	FText GetShoppingList() const;
	FText GetInteractionPrompt() const;
	UFUNCTION() void OnPassMannequin(AActor* Trigger, AActor* Other);

	EPhase Phase = EPhase::Waiting;
	float Elapsed = 0.f;
	bool bControlsLocked = false;
	bool bUsingPhonePose = false;
	uint8 PreviousAnimationMode = 0;
	FRotator TurnStart = FRotator::ZeroRotator;
	FRotator TurnDelta = FRotator::ZeroRotator;
	float TurnDuration = 0.f;
	float OpeningYaw = 0.f;
	FTransform OpeningCameraStart;
	float WalkSpeedBeforeIntro = 0.f;
	FVector ShutterOpen = FVector::ZeroVector;
	FVector HatStart = FVector::ZeroVector;
	FVector HatApproach = FVector::ForwardVector;
	bool bHatMeshWasVisible = true;
	int32 NextFinaleLight = 0;
	TArray<float> FinaleOutageTimes;
	TArray<float> FinaleLightIntensities;
	float ShutterDrop = 320.f;
	TSet<FName> PickedItems;
	UPROPERTY(Transient) TObjectPtr<AProjectSingleIntroTarget> Hat;
	UPROPERTY(Transient) TObjectPtr<AProjectSingleIntroTarget> Partner;
	UPROPERTY(Transient) TObjectPtr<AProjectSingleIntroTarget> Mannequin;
	UPROPERTY(Transient) TObjectPtr<AActor> Shutter;
	UPROPERTY(Transient) TObjectPtr<AActor> OpeningDestination;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> OpeningCamera;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> HatCamera;
	UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> HatReachMesh;
	UPROPERTY(Transient) TObjectPtr<AActor> OpeningCameraEnd;
	UPROPERTY(Transient) TObjectPtr<AActor> PhoneProp;
	UPROPERTY(Transient) TObjectPtr<AActor> PhoneDisplay;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> PhoneWalkPose;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> PhoneIdlePose;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> FinaleCamera;
	UPROPERTY(Transient) TObjectPtr<AProjectSingleIntroTarget> ShutterHandle;
	UPROPERTY(Transient) TArray<TObjectPtr<AProjectSingleIntroTarget>> FinaleCrowd;
	UPROPERTY(Transient) TArray<TObjectPtr<ULightComponent>> FinaleLights;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> FinaleDiffusers;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> FinaleShoppingSigns;
	UPROPERTY(Transient) TArray<TObjectPtr<ULightComponent>> StoreLights;
	UPROPERTY(Transient) TObjectPtr<ATriggerBox> Passage;
	UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> PartnerScream;
	UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> Alarm;
	UPROPERTY(Transient) TObjectPtr<USoundBase> PhoneClick;
	UPROPERTY(Transient) TObjectPtr<UPlayerInteractionComponent> Interaction;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<UGameViewportClient> AttachedViewport;
	TSharedPtr<SWidget> Overlay;
};

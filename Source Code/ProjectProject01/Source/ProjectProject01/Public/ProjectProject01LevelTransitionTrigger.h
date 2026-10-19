// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineBaseTypes.h"
#include "ProjectProject01LevelTransitionTrigger.generated.h"

class APlayerCharacter;
class AMultiplayTestPlayerController;
class UBoxComponent;
class UWorld;

/**
 * 레벨에 배치하고 DestinationLevel만 지정하면 사용하는 맵 이동 영역입니다.
 * 싱글은 플레이어가 들어오는 즉시 이동하며, 멀티는 기본적으로 살아 있는 생존자 전원이
 * 영역 안에 모였을 때 권위 서버가 모든 접속자를 다음 맵으로 이동시킵니다.
 */
UCLASS(BlueprintType, Blueprintable)
class PROJECTPROJECT01_API AProjectProject01LevelTransitionTrigger : public AActor
{
	GENERATED_BODY()
	friend class FProjectReviewTravelRollbackTest;
	friend class FProjectElevatorTest;

public:
	AProjectProject01LevelTransitionTrigger();

	/** 현재 영역 안의 조건 충족 인원과 전체 필요 인원을 에디터/블루프린트에서 확인합니다. */
	UFUNCTION(BlueprintPure, Category = "Level Transition")
	void GetTransitionProgress(int32& OutPlayersInside, int32& OutRequiredPlayers) const;

	/** 버튼 등 서버 권위 장치에서도 기존 이동/복구 경로를 재사용합니다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Level Transition")
	bool RequestTransition(APlayerCharacter* Survivor);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void RollbackTransition(const FString& Reason);
	virtual float GetTransitionFadeSeconds() const { return 0.6f; }
	virtual bool ShouldFadeToWaitingSpectator() const { return true; }
	virtual float GetTravelDelaySeconds() const { return TravelDelaySeconds; }
	bool IsDestinationAvailable() const;
	bool HasStartedTransition() const { return bTransitionStarted; }
	/** 파생 장치는 진입만으로 이동하지 않고 버튼으로만 확정할 수 있습니다. */
	bool bUseOverlapActivation = true;
	/** 목적지 엘리베이터 내부 스폰을 선택하는 URL 옵션입니다. 일반 트리거는 비워둡니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Transition")
	FName DestinationArrivalId;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);

	void EvaluateTransition();
	void BeginTransition();
	void ExecuteTransition();
	bool IsEligibleLivingPlayer(const APlayerCharacter* Player) const;
	FString GetDestinationPackageName() const;
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);
	void HandleTravelTimeout();
    bool ExtendAsyncTravelGrace(bool bEngineOwnsTransition);
    void AbortUnresponsiveTravel();
    int32 AsyncTravelWatchdogChecks=0;

	/** 맵에서 크기와 위치를 조절하는 진입 영역입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Level Transition", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> TransitionVolume;

	/** 이동할 레벨 에셋을 지정합니다. 비워두면 작동하지 않습니다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Level Transition", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UWorld> DestinationLevel;

	/** 멀티에서 켜면 살아 있는 생존자 전원이 들어와야 이동합니다. 끄면 한 명만 들어와도 이동합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Transition|Multiplayer", meta = (AllowPrivateAccess = "true"))
	bool bRequireAllLivingSurvivors = true;

	/** 조건 달성 후 실제 이동까지의 짧은 지연입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Transition", meta = (ClampMin = "0.0", Units = "s", AllowPrivateAccess = "true"))
	float TravelDelaySeconds = 0.65f;

	/** 영역 안에 누군가 있는 동안 사망·이탈로 필요 인원이 변하는 상황을 다시 검사하는 간격입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Transition|Multiplayer", meta = (ClampMin = "0.05", Units = "s", AllowPrivateAccess = "true"))
	float EvaluationIntervalSeconds = 0.25f;

	bool bTransitionStarted = false;
	TSet<TWeakObjectPtr<AMultiplayTestPlayerController>> AcceptedSurvivors;
	FTimerHandle EvaluationTimer;
	FTimerHandle TravelTimer;
	FTimerHandle TravelWatchdogTimer;
	FDelegateHandle TravelFailureHandle;
	TWeakObjectPtr<class APlayerController> SingleTransitionController;
	bool bSingleMoveIgnored = false;
	bool bSingleLookIgnored = false;
};

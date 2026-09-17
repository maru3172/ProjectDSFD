// File: Source/ProjectProject01/Public/HelperRearGuardCharacter.h
// Target: ProjectProject01Editor Win64, Unreal Engine 5.8

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HelperRearGuardCharacter.generated.h"

class APawn;

/**
 * 모델이 연결되기 전에도 독립적으로 동작하는 후방 경계 조력자 프로토타입입니다.
 * 플레이어 뒤를 따라가며 자신의 시야에서 천사를 감지합니다.
 */
UCLASS(BlueprintType, Blueprintable)
class PROJECTPROJECT01_API AHelperRearGuardCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AHelperRearGuardCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** 조력자의 후방 부채꼴 시야와 장애물 가림을 모두 통과했는지 확인합니다. */
	UFUNCTION(BlueprintPure, Category = "Helper Rear Guard")
	bool CanObserveActor(const AActor* TargetActor) const;

	/** 디버그 표시와 시야 판정에 사용하는 조력자의 월드 위치입니다. */
	UFUNCTION(BlueprintPure, Category = "Helper Rear Guard")
	FVector GetGuardViewOrigin() const;

	/** AIController가 NavMesh 이동에 사용할 플레이어 이동 방향 반대편 목표 위치를 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "Helper Rear Guard")
	bool GetFollowTargetLocation(FVector& OutFollowTarget) const;

	/** 최소 이격 거리를 지키기 위해 AIController가 사용할 수 있는 최대 도착 허용 반경입니다. */
	UFUNCTION(BlueprintPure, Category = "Helper Rear Guard")
	float GetMaximumFollowAcceptanceRadius() const;

protected:
	/** 최소 50m 이격을 위한 목표 수평 거리(cm)입니다. 50cm 도착 허용 여유를 포함합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FollowDistance = 5050.0f;

	/** 조력자가 플레이어에게 접근할 수 있는 최소 수평 거리(cm)입니다. 5000cm는 50m입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MinimumFollowSeparation = 5000.0f;

	/** 조력자 시야의 최대 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float GuardSightRadius = 1500.0f;

	/** 후방 중심선 한쪽의 시야각입니다. 기본값 70도는 총 140도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard", meta = (ClampMin = "1.0", ClampMax = "89.0", UIMin = "1.0", UIMax = "89.0"))
	float GuardHalfAngleDegrees = 70.0f;

	/** 게임 중 조력자 시야 부채꼴을 노란색으로 표시합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard")
	bool bDrawGuardDebug = true;

private:
	bool RefreshGuardedPlayer();
	bool GetPlayerMovementDirection(FVector& OutMovementDirection) const;
	void UpdateLastPlayerMovementDirection();
	void DrawGuardDebug() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> GuardedPlayer;

	/** 플레이어가 정지했을 때 사용할 마지막 유효 수평 이동 방향입니다. */
	FVector LastPlayerMovementDirection = FVector::ZeroVector;
};

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

protected:
	/** 플레이어와 유지할 수평 거리(cm)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FollowDistance = 200.0f;

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
	FVector GetPlayerViewForward() const;
	void UpdateFollowTransform();
	void DrawGuardDebug() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> GuardedPlayer;
};

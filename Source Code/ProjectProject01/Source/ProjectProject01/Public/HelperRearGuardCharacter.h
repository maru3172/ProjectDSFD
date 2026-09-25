// File: Source/ProjectProject01/Public/HelperRearGuardCharacter.h
// Target: ProjectProject01Editor Win64, Unreal Engine 5.8

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HelperRearGuardCharacter.generated.h"

class APawn;
struct FHelperTuningRow;

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

	/** 서버가 DataTable의 검증된 조력자 수치를 적용한다. */
	void ApplyHelperTuning(const FHelperTuningRow& Tuning);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** 조력자의 이동 반대 방향 커버 범위 안 실제 시야와 장애물 가림을 모두 통과했는지 확인합니다. */
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

	/** 현재 실제 감시 시야의 중심 방향을 반환합니다. 시야는 이동 반대 방향의 커버 범위를 넘지 않습니다. */
	UFUNCTION(BlueprintPure, Category = "Helper Rear Guard")
	bool GetGuardViewDirection(FVector& OutViewDirection) const;

	/** 지정한 플레이어를 실제로 따르는 조력자인지 반환합니다. */
	bool IsGuardingPlayer(const APawn* PlayerPawn) const;

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

	/** 실제 감시 부채꼴 한쪽의 시야각입니다. 기본값 60도는 총 120도이며 커버 범위를 넘지 않게 제한됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float GuardHalfAngleDegrees = 60.0f;

	/** 실제 시야 중심이 이동 반대 방향으로부터 회전할 수 있는 커버 범위의 한쪽 각도입니다. 기본값 90도는 총 180도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard", meta = (ClampMin = "1.0", ClampMax = "180.0", UIMin = "1.0", UIMax = "180.0"))
	float GuardSearchHalfAngleDegrees = 90.0f;

	/** 실제 시야 중심 변경을 누적하는 시간창입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard|Vision Retreat", meta = (ClampMin = "0.0", Units = "s"))
	float VisionDirectionChangeWindowSeconds = 3.0f;

	/** 시간창 안에서 후방 이격을 시작할 유효 시야 중심 변경 최소 횟수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard|Vision Retreat", meta = (ClampMin = "1"))
	int32 VisionDirectionChangeRequiredCount = 5;

	/** 이 각도 이상 실제 시야 중심이 바뀔 때만 유효 변경 한 번으로 기록합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard|Vision Retreat", meta = (ClampMin = "0.0", Units = "deg"))
	float VisionDirectionChangeThresholdDegrees = 5.0f;

	/** 게임 중 조력자 시야 부채꼴을 노란색으로 표시합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Helper Rear Guard")
	bool bDrawGuardDebug = true;

private:
	bool RefreshGuardedPlayer();
	bool GetPlayerMovementDirection(FVector& OutMovementDirection) const;
	bool CalculateGuardViewDirection(FVector& OutViewDirection) const;
	void UpdateGuardViewDirection();
	float GetEffectiveGuardSightHalfAngleDegrees() const;
	float GetEffectiveGuardSearchHalfAngleDegrees() const;
	bool IsGuardedPlayerStationary() const;
	bool HasGuardedPlayerMovementInput() const;
	void UpdateVisionRetreatState();
	void BeginVisionRetreat(const FVector& CurrentViewDirection);
	bool IsGuardedPlayerOnRetreatPath(const FVector& RetreatTarget) const;
	void ResetVisionDirectionChangeTracking();
	void UpdateLastPlayerMovementDirection();
	void DrawGuardDebug() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> GuardedPlayer;

	/** 플레이어가 정지했을 때 사용할 마지막 유효 수평 이동 방향입니다. */
	FVector LastPlayerMovementDirection = FVector::ZeroVector;

	/** 플레이어가 정지해도 매 틱 마네킹 위치로 갱신하는 실제 노란색 시야 중심입니다. */
	FVector CurrentGuardViewDirection = FVector::ZeroVector;

	/** 다음 플레이어 이동 전까지 일반 추적 거리 갱신 대신 유지할 후방 이격 목표입니다. */
	FVector LockedRetreatTarget = FVector::ZeroVector;

	FVector LastRecordedGuardViewDirection = FVector::ZeroVector;
	TArray<double> VisionDirectionChangeTimes;
	bool bFollowDistanceLocked = false;
};

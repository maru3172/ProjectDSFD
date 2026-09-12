// File: Source/ProjectProject01/Public/HelperRearGuardAIController.h
// Target: ProjectProject01Editor Win64 DebugGame, Unreal Engine 5.8

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "HelperRearGuardAIController.generated.h"

class AHelperRearGuardCharacter;

/** 플레이어 후방을 NavMesh 경로로 지키는 조력자 전용 AIController입니다. */
UCLASS()
class PROJECTPROJECT01_API AHelperRearGuardAIController : public AAIController
{
	GENERATED_BODY()

public:
	AHelperRearGuardAIController();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "Helper Rear Guard|Navigation", meta = (ClampMin = "0.05", UIMin = "0.05", Units = "s"))
	float RepathInterval = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Helper Rear Guard|Navigation", meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float RepathDistance = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Helper Rear Guard|Navigation", meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float AcceptanceRadius = 75.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Helper Rear Guard|Navigation", meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s"))
	float StuckTimeout = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Helper Rear Guard|Navigation", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float StuckWaitTime = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Helper Rear Guard|Navigation", meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float ProgressDistance = 20.0f;

	UPROPERTY(Transient)
	TWeakObjectPtr<AHelperRearGuardCharacter> ControlledHelper;

	FVector LastMoveTarget = FVector::ZeroVector;
	FVector LastProgressLocation = FVector::ZeroVector;
	double LastProgressTime = 0.0;
	double WaitUntilTime = 0.0;
	float TimeSinceLastPathRequest = 0.0f;
	bool bHasMoveTarget = false;
	bool bWaitingForRepath = false;
};

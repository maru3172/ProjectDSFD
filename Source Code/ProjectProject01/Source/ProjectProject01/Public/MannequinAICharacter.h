// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"
#include "MannequinAICharacter.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS()
class PROJECTPROJECT01_API AMannequinAICharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AMannequinAICharacter();

	// 숫자 키 0~9에 대응하는 서버 권한 슬롯이다. -1은 선택 대상이 아님을 뜻한다.
	UFUNCTION(BlueprintPure, Category = "Multiplayer|Mannequin")
	int32 GetControlSlot() const;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 기존 AI 서비스가 사용하는 애니메이션 정지 상태다. 이동 권한은 변경하지 않는다.
	void SetFrozen(bool bFrozen);

	// MultiplayTest 서버만 호출한다. 생존자 시야 정지는 이동 권한까지 중지하고 모든 클라이언트에 복제한다.
	void SetFrozenBySurvivorVision(bool bFrozen);

	UFUNCTION(BlueprintPure, Category = "Multiplayer|Mannequin")
	bool IsFrozenBySurvivorVision() const { return bFrozenBySurvivorVision; }

private:
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Multiplayer|Mannequin",
		meta = (AllowPrivateAccess = "true", ClampMin = "-1", ClampMax = "9", UIMin = "-1", UIMax = "9"))
	int32 ControlSlot = -1;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> IMC_MannequinControl;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_MannequinMove;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_MannequinLook;

	UPROPERTY(ReplicatedUsing = OnRep_SurvivorVisionFrozen, VisibleInstanceOnly,
		Category = "Multiplayer|Mannequin")
	bool bFrozenBySurvivorVision = false;

	UPROPERTY(Transient)
	bool bLegacyAnimationFrozen = false;

	UPROPERTY(Transient)
	TEnumAsByte<EMovementMode> MovementModeBeforeSurvivorVisionFreeze = MOVE_Walking;

	UPROPERTY(Transient)
	uint8 CustomMovementModeBeforeSurvivorVisionFreeze = 0;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void RegisterLocalInputMapping();
	void RefreshFrozenAnimationState();

	UFUNCTION()
	void OnRep_SurvivorVisionFrozen();
};

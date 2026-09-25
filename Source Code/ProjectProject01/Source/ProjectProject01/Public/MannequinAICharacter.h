// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"
#include "MannequinAICharacter.generated.h"

class UInputAction;
class UInputMappingContext;
class APlayerCharacter;
class UPrimitiveComponent;
struct FMannequinAITuningRow;

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
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
		bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 기존 AI 서비스가 사용하는 애니메이션 정지 상태다. 이동 권한은 변경하지 않는다.
	void SetFrozen(bool bFrozen);

	/** 서버가 DataTable의 검증된 마네킹 수치를 적용한다. */
	void ApplyMannequinTuning(const FMannequinAITuningRow& Tuning);

	// MultiplayTest 서버만 호출한다. 생존자 시야 정지는 이동 권한까지 중지하고 모든 클라이언트에 복제한다.
	void SetFrozenBySurvivorVision(bool bFrozen);

	UFUNCTION(BlueprintPure, Category = "Multiplayer|Mannequin")
	bool IsFrozenBySurvivorVision() const { return bFrozenBySurvivorVision; }

	/** MultiplayTest 서버가 직접 조작 시작과 종료를 관리합니다. */
	void SetManualControlEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Multiplayer|Mannequin")
	bool IsManualControlEnabled() const { return bManualControlEnabled; }

	/** 직접 조작 중 E 입력으로, 다음 빙의 해제 시 실행할 추격 명령을 기록합니다. */
	bool QueuePostPossessionChaseCommand();

	/** 빙의 해제 직후 서버가 기록된 추격 또는 정지 명령을 활성화합니다. */
	void ActivatePostPossessionCommand();

	bool ShouldHoldPostPossessionCommand(double ServerTimeSeconds) const;
	bool ShouldChasePostPossessionCommand(double ServerTimeSeconds) const;

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

	// 서버에서 DataTable로 설정하고, 조종 중인 클라이언트에도 복제하는 이동 속도(cm/s)다.
	UPROPERTY(ReplicatedUsing = OnRep_MannequinWalkSpeed, VisibleInstanceOnly,
		Category = "Multiplayer|Mannequin", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MannequinWalkSpeed = 600.0f;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Multiplayer|Mannequin")
	bool bManualControlEnabled = false;

	enum class EPostPossessionCommand : uint8
	{
		None,
		HoldPosition,
		ChaseNearestSurvivor
	};

	/** E 입력이 있었는지 서버에서만 보관하는 다음 빙의 해제용 명령입니다. */
	bool bPostPossessionChaseCommandQueued = false;
	EPostPossessionCommand ActivePostPossessionCommand = EPostPossessionCommand::None;
	double PostPossessionCommandStartTimeSeconds = 0.0;
	double PostPossessionCommandEndTimeSeconds = 0.0;
	float PostPossessionCommandDurationSeconds = 5.0f;

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
	void ApplyMannequinWalkSpeed();
	void TryCatchSurvivor(AActor* OtherActor);
	void RemoveSeparatedCatchContacts();
	bool IsPostPossessionCommandActive(double ServerTimeSeconds) const;

	UFUNCTION()
	void OnRep_SurvivorVisionFrozen();

	UFUNCTION()
	void OnRep_MannequinWalkSpeed();

	/** 같은 접촉 동안 포획이 반복 차감되지 않도록 서버에서만 보관합니다. */
	TArray<TWeakObjectPtr<APlayerCharacter>> CaughtSurvivorsInCurrentContact;
};

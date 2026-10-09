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
class UFootstepSynthComponent;
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
	FString GetDiagnosticCommandState(double ServerTimeSeconds) const;
	void GetDiagnosticAppliedTuning(struct FMannequinAITuningRow& OutTuning) const;
	
	// 발소리 함수
	UFUNCTION(BlueprintCallable, Category = "Audio|Footstep")
	void PlayFootstep(FName FootBoneName);

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

	/** 단단한 플라스틱 물체가 대리석을 밟는 합성 공간 효과음입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio|Footstep", meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UFootstepSynthComponent> FootstepSFXComponent;
	
	// 사운드 트레이스 설정 변수들
	// 기본 발소리 사운드
	UPROPERTY(EditDefaultsOnly, Category = "Audio|Footstep")
	TObjectPtr<class USoundBase> DefaultFootstepSound;
	
	// 지금은 사용할 필요가 없다.
	// 발소리의 거리별 감소 설정
	// 사운드의 개수와 관계없이 소리가 발생한 위치와 청취자 사이의 거리에 따라 볼륨이 감소하는 방식 등을 설정
	UPROPERTY(EditDefaultsOnly, Category = "Audio|Footstep")
	TObjectPtr<class USoundAttenuation> FootstepAttenuation;
	
	// 발 위치에서 위쪽으로 트레이스를 시작할 거리
	UPROPERTY(EditDefaultsOnly, Category = "Audio|Footstep", meta = (ClampMin = "0.0", Units = "cm"))
	float FootstepTraceUpDistance = 20.0f;
	
	// 트레이스 시작 위치에서 아래쪽으로 검사할 거리
	UPROPERTY(EditDefaultsOnly, Category = "Audio|Footstep", meta = (ClampMin = "0.0", Units = "cm"))
	float FootstepTraceDownDistance = 60.0f;
	
	// 발소리가 연속으로 재생되지 않도록 하는 최소 시간 간격 - 애니메이션 등의 영향으로 발소리가 너무 짧은 간격으로 중복 재생되는 현상을 방지하기 위한 값
	UPROPERTY(EditDefaultsOnly, Category = "Audio|Footstep", meta = (ClampMin = "0.0", Units = "s"))
	float MinimumFootstepInterval = 0.05f;
	
	// 마지막으로 발소리를 재생한 시간
	double LastFootstepTimeSeconds = -1.0;
};

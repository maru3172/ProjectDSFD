// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "PlayerCharacter.generated.h"

class UInputAction;
class UInputMappingContext;
struct FMannequinAITuningRow;

UCLASS()
class PROJECTPROJECT01_API APlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	APlayerCharacter();

	// AI가 직접 추격을 시작할 내부 반경(cm)이다.
	UFUNCTION(BlueprintPure, Category = "AI|Range")
	float GetDirectChaseRadius() const;

	// AI가 배회할 고리 영역의 외부 반경(cm)이다.
	UFUNCTION(BlueprintPure, Category = "AI|Range")
	float GetRoamingOuterRadius() const;
	
	UFUNCTION(BlueprintPure, Category = "AI|Direction")
	float GetDirectChaseHalfAngleDegrees() const;

	// 플레이어 중심에서 직접 추격 부채꼴 끝까지의 반경(cm)이다.
	UFUNCTION(BlueprintPure, Category = "AI|Direction")
	float GetDirectChaseSectorRadius() const;
	
	UFUNCTION(BlueprintPure, Category = "AI|Direction")
	FVector GetAIMovementDirection() const;

	/** 서버가 DataTable의 검증된 마네킹 추적 값을 적용한다. */
	void ApplyMannequinTuning(const FMannequinAITuningRow& Tuning);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Component")
	class USpringArmComponent* SpringArm;
	UPROPERTY(VisibleAnywhere, Category = "Component")
	class UCameraComponent* Camera;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputMappingContext* IMC_PlayerInput;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputAction* IA_Move;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputAction* IA_Look;

	// 입력 이벤트 발생 시 실행할 함수
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	// 작은 원: 이 반경 안에서는 마네킹 AI가 직접 추격하는 용도로 사용한다.
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "AI|Range",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float DirectChaseRadius = 500.0f;

	// 큰 원: DirectChaseRadius와 이 반경 사이를 마네킹 AI의 랜덤 이동 영역으로 사용한다.
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "AI|Range",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float RoamingOuterRadius = 1500.0f;

	// 서버에서 DataTable로 설정하고 소유 클라이언트에도 복제하는 이동 속도(cm/s)다.
	UPROPERTY(ReplicatedUsing = OnRep_PlayerWalkSpeed, VisibleInstanceOnly, Category = "Player",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm/s"))
	float PlayerWalkSpeed = 600.0f;

	// PIE/Development 플레이 중 두 범위를 디버그 원으로 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Range|Debug",
		meta = (AllowPrivateAccess = "true"))
	bool bShowAIRangeDebug = true;

	void DrawAIRangeDebug();
	
	// 이동 방향을 중심으로 직접 추격 영역이 펼쳐지는 좌우 반각이다.
	// 90도면 현재와 동일하게 앞뒤 반원으로 나뉜다.
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "AI|Direction", 
		meta = (
			AllowPrivateAccess = "true",
			ClampMin = "0.0",
			ClampMax = "180.0",
			UIMin = "0.0",
			UIMax = "180.0",
			Units = "deg"
		))
	float DirectChaseHalfAngleDegrees = 90.0f;

	// 직접 추격 부채꼴은 내부원 밖에서 시작해 이 반경까지 이어진다.
	// 네트워크 설정은 변경하지 않고 Player BP의 클래스 기본값으로 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Direction",
		meta = (
			AllowPrivateAccess = "true",
			ClampMin = "0.0",
			UIMin = "0.0",
			Units = "cm"
		))
	float DirectChaseSectorRadius = 1000.0f;

	void ApplyPlayerWalkSpeed();

	UFUNCTION()
	void OnRep_PlayerWalkSpeed();
	
	FVector LastAIMovementDirection = FVector::ZeroVector;

	// =========================================================================================================================
	// 카메라 B 키 디버깅 관련
	// =========================================================================================================================

	// PIE 표시 화면만 탑뷰로 전환한다. AI 시야 카메라는 변경하지 않는다.
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "Debug|Top View")
    bool bUseTopViewInPIE = false;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_Player_DebugCamera;

    UPROPERTY(VisibleAnywhere, Category = "Debug|Top View")
    TObjectPtr<class UCameraComponent> DebugTopViewCamera;

	friend class FProjectProject01TopViewExtension;
    void ToggleDebugCamera();
    void RestoreDebugControls();
    void UpdateDebugCursorAim();
    TWeakObjectPtr<class APlayerController> DebugInputController;
    FMatrix DebugClipToWorld = FMatrix::Identity;
    FIntRect DebugViewRect = FIntRect(0, 0, 0, 0);
    FRotator SavedControlRotation = FRotator::ZeroRotator;
    FRotator SavedCameraRelativeRotation = FRotator::ZeroRotator;
    bool bHasDebugView = false;
    bool bDebugControlsActive = false;
    bool bSkipNextLookInput = false;
    bool bSavedUseControllerRotationYaw = false;
    bool bSavedOrientRotationToMovement = false;
    bool bSavedUseControllerDesiredRotation = false;
    bool bSavedCameraUsePawnControlRotation = false;
    TSharedPtr<class FProjectProject01TopViewExtension, ESPMode::ThreadSafe> TopViewExtension;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "PlayerCharacter.generated.h"

class UInputAction;
class UInputMappingContext;

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

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Range",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float DirectChaseRadius = 500.0f;

	// 큰 원: DirectChaseRadius와 이 반경 사이를 마네킹 AI의 랜덤 이동 영역으로 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Range",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float RoamingOuterRadius = 1500.0f;

	// PIE/Development 플레이 중 두 범위를 디버그 원으로 표시한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Range|Debug",
		meta = (AllowPrivateAccess = "true"))
	bool bShowAIRangeDebug = true;

	void DrawAIRangeDebug() const;

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
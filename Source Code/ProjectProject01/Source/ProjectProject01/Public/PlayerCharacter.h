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
};
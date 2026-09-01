// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeepingAngelPath.generated.h"

class UBillboardComponent;

UCLASS()
class PROJECTPROJECT01_API AWeepingAngelPath : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AWeepingAngelPath();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	float GetWeight() const
	{
		return this->Weight;
	}

	// 플레이어의 현재 시야를 기준으로 Weight 계산
	void CalculateWeight();

	// 실제 천사가 이동할 위치
	FVector GetAngelPathLocation() const
	{
		return AngelPathPoint->GetComponentLocation();
	}

	// 이 Path와 연결된 다른 Path들
	const TArray<TObjectPtr<AWeepingAngelPath>>& GetConnectedPaths() const
	{
		return ConnectedPaths;
	}

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Surround")
	bool bSurroundAnchor = false;

	// 같은 복도에 속한 포인트는 같은 이름 사용
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Surround")
	FName SurroundGroup = NAME_None;

	// 각 AWeepingAngelPath 가 자신의 시야 노출 상태를 저장하는 변수
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Surround")
	bool bVisibleToPlayer = false;

	bool IsSurroundAnchor() const
	{
		return bSurroundAnchor;
	}

	FName GetSurroundGroup() const
	{
		return SurroundGroup;
	}

	bool IsVisibleToPlayer() const
	{
		return bVisibleToPlayer;
	}

private:
	UPROPERTY(VisibleAnywhere)
	UBillboardComponent* Billboard;

	// 바닥 기준점
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weeping Angel Path", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> Root;

	// 플레이어의 시야 감지 지점 -> 아마 바닥 기준점으로부터 +100 정도?
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weeping Angel Path", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> PlayerViewPoint;

	// 바닥 기준점 + 천사의 경로 지점
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weeping Angel Path", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> AngelPathPoint;

	// 가중치
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weeping Angel Path", meta = (AllowPrivateAccess = "true"))
	float Weight = 0.0f;

	// 서로 연결된 Path
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Weeping Angel Path", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<AWeepingAngelPath>> ConnectedPaths;

};
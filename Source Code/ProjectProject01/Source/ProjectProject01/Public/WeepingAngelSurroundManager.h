// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeepingAngelSurroundManager.generated.h"

class AWeepingAngelCharacter;
class AWeepingAngelPath;

UCLASS()
class PROJECTPROJECT01_API AWeepingAngelSurroundManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AWeepingAngelSurroundManager();

	// 두 통로 사이의 그래프 거리 계산
	float GetGraphDistance(AWeepingAngelPath* StartPath, AWeepingAngelPath* GoalPath) const;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// 포위 배정을 갱신하는 시간 간격
	UPROPERTY(EditAnywhere, Category = "Surround")
	float AssignmentInterval = 0.25f;

	// 플레이어 주변에서 입구를 찾을 범위
	UPROPERTY(EditAnywhere, Category = "Surround")
	float EntranceSearchRadius = 3000.0f;

	// 기존 담당 입구를 유지하도록 하는 보너스
	UPROPERTY(EditAnywhere, Category = "Surround")
	float KeepAssignmentBonus = 1500.0f;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	FTimerHandle AssignmentTimerHandle;

	void UpdateAssignments();

	// 천사가 추격 중인지 확인
	bool IsAngelChasing(AWeepingAngelCharacter* Angel) const;

	// 천사에게 접근 통로 배정
	void SetAngelAssignment(AWeepingAngelCharacter* Angel, AWeepingAngelPath* AssignedPath);

	// 현재 플레이어와 가장 가까운 Path
	UPROPERTY(VisibleInstanceOnly, Category = "Surround")
	TObjectPtr<AWeepingAngelPath> PlayerCurrentPath = nullptr;

	// 플레이어가 Path 경계에서 움직일 때 CurrentPath가 계속 바뀌는 현상 방지
	UPROPERTY(EditAnywhere, Category = "Surround")
	float PlayerPathSwitchMargin = 100.0f;
};
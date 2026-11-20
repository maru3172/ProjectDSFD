// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectShutterAreaGenerator.generated.h"

class USceneComponent;
class UBoxComponent;

UCLASS()
class PROJECTPROJECT01_API AProjectShutterAreaGenerator : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AProjectShutterAreaGenerator();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	// 에디터에서 설정이 변경될 때 구역 표시를 갱신한다.
	virtual void OnConstruction(const FTransform& Transform) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	// 현재 구역 크기에 맞춰 박스 표시를 갱신한다.
	void RefreshAreaVisualization();
	
	// 생성기 컴포넌트의 기준이 되는 루트다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shutter Generator", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;
	
	// 에디터에서 방화셔터 생성 구역을 보여주는 박스다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shutter Generator", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> GenerationArea;
	
	// 방화셔터가 생성될 구역의 가로와 세로 크기다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Area", meta = (AllowPrivateAccess = "true", ClampMin = "300.0", Units = "cm"))
	FVector2D AreaSize = FVector2D(3000.0, 3000.0);
	
	// 에디터에서 표시할 생성 구역의 높이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Area", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", Units = "cm"))
	float VisualizationHeight = 300.0f;
	
	// 에디터에서 생성 구역 박스를 표시할지 결정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Area", meta = (AllowPrivateAccess = "true"))
	bool bShowGenerationArea = true;
};

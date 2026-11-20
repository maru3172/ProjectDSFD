// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectShutterAreaGenerator.generated.h"

class USceneComponent;
class UBoxComponent;

UENUM(BlueprintType)
enum class EProjectShutterGridDirection : uint8
{
	Horizontal,		// 가로 방향
	Vertical		// 세로 방향
};

USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectShutterGridSegment
{
	GENERATED_BODY()
	
	// 셔터가 싲가되는 격자 좌표다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shutter")
	FIntPoint Start = FIntPoint::ZeroValue;
	
	// 셔터가 배치되는 가로 또는 세로 방향이다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shutter")
	EProjectShutterGridDirection Direction = EProjectShutterGridDirection::Horizontal;
	
	// 두 셔터 데이터가 같은 위치와 방향을 사용하는지 비교한다.
	bool operator==(const FProjectShutterGridSegment& Other) const
	{
		return Start == Other.Start && Direction == Other.Direction;
	}
};

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
	
	// 현재 구역에 들어가는 격자 칸 수를 반환한다.
	FIntPoint GetGridCellCount() const;
	
	// 셔터 선분이 생성 구역 내부에 있는지 검사한다.
	bool IsSegmentInsideArea(
		const FProjectShutterGridSegment& Segment) const;
	
	// 격자 선분을 실제 월드 트랜스폼으로 변환한다.
	FTransform MakeSegmentWorldTransform(
		const FProjectShutterGridSegment& Segment) const;
	
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
	
	// 방화셔터 1 개가 차지하는 격자 길이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Grid", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", Units = "cm"))
	float ShutterLength = 300.0f;
};

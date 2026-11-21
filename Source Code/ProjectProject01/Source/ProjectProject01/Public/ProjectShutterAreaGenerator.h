// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProjectShutterAreaGenerator.generated.h"

class USceneComponent;
class UBoxComponent;
class AFireShutterActor;

// 셔터가 배치될 수 있는 가로와 세로 방향을 나타낸다.
UENUM(BlueprintType)
enum class EProjectShutterGridDirection : uint8
{
	// 생성기 액터의 로컬 X축 방향이다.
	Horizontal,

	// 생성기 액터의 로컬 Y축 방향이다.
	Vertical
};

// 격자 위에 배치할 셔터 한 개의 시작 위치와 방향을 나타낸다.
USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectShutterGridSegment
{
	GENERATED_BODY()

	// 셔터가 시작되는 격자 좌표다.
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

// 설정된 구역 안에 방화셔터를 자동으로 배치하는 생성기 액터다.
UCLASS()
class PROJECTPROJECT01_API AProjectShutterAreaGenerator : public AActor
{
	GENERATED_BODY()

public:
	// 생성기 액터의 기본 컴포넌트와 설정값을 구성한다.
	AProjectShutterAreaGenerator();

	// 현재 구역 설정을 기준으로 미리보기 셔터를 생성한다.
	UFUNCTION(CallInEditor, Category = "Shutter Generator")
	void GeneratePreview();

	// 이 생성기가 만든 미리보기 셔터를 모두 제거한다.
	UFUNCTION(CallInEditor, Category = "Shutter Generator")
	void ClearGeneratedShutters();

protected:
	// 에디터에서 설정이 변경될 때 구역 표시를 갱신한다.
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	// 현재 구역 크기에 맞춰 박스 표시를 갱신한다.
	void RefreshAreaVisualization();
	
	// 현재 구역에 들어가는 격자 칸 수를 반환한다.
	FIntPoint GetGridCellCount() const;
	
	// 생성 영역에서 더 긴 축의 셔터 방향을 반환한다.
	EProjectShutterGridDirection GetLongAxisDirection() const;
	
	// 생성 영역의 장축 길이를 cm 단위로 반환한다.
	float GetLongAxisLength() const;
	
	// 생성 영역의 단축 길이를 cm 단위로 반환한다.
	float GetShortAxisLength() const;
	
	// 단축 방향에 배치할 시작 후보 개수를 반환한다.
	int32 GetShortAxisCandidateCount() const;
	
	// 단축 후보 위치에서 장축 방향의 1 번째 셔터를 만든다.
	FProjectShutterGridSegment MakeLongStartSegment(
		int32 ShortAxisCandidateIndex) const;
	
	// 셔터 선분이 생성 구역 내부에 있는지 검사한다.
	bool IsSegmentInsideArea(
		const FProjectShutterGridSegment& Segment) const;
	
	// 격자 선분을 실제 월드 트랜스폼으로 변환한다.
	FTransform MakeSegmentWorldTransform(
		const FProjectShutterGridSegment& Segment) const;
	
	// 셔터 선분의 시작점과 끝점을 격자 좌표로 반환한다.
	void GetSegmentEndpoints(
		const FProjectShutterGridSegment& Segment,
		FIntPoint& OutStart,
		FIntPoint& OutEnd) const;
	
	// 두 셔터 선분이 같은 격자 위치와 방향인지 확인한다.
	bool AreSegmentsEqual(
		const FProjectShutterGridSegment& First,
		const FProjectShutterGridSegment& Second) const;
	
	// 지정된 격자점에 연결된 기존 셔터 개수를 반환한다.
	int32 CountSegmentsAtPoint(
		const FIntPoint& Point,
		const TArray<FProjectShutterGridSegment>& Segments) const;
	
	// 후보 셔터가 기존 셔터의 내부를 통과하는지 검사한다.
	bool DoesSegmentCrossExistingSegment(
		const FProjectShutterGridSegment& Candidate,
		const TArray<FProjectShutterGridSegment>& Segments) const;
	
	// 후보 셔터가 현재 배치 규칙을 모두 만족하는지 검사한다.
	bool CanPlaceSegment(
		const FProjectShutterGridSegment& Candidate,
		const TArray<FProjectShutterGridSegment>& Segments) const;
	
	// 지정된 끝점에서 선택한 방향으로 이어지는 셔터 선분을 만든다,
	FProjectShutterGridSegment MakeSegmentFromEndpoint(
		const FIntPoint& Endpoint,
		EProjectShutterGridDirection Direction,
		int32 DirectionSign) const;
	
	// 현재 연속 개수에 맞는 직선 연속 확률을 반환한다.
	float GetContinuationChance(
		int32 StraightRunLength) const;
	
	// 현재 배치와 연결되지 않은 새로운 시작 셔터 후보를 만든다.
	FProjectShutterGridSegment MakeRandomStartSegment(
		FRandomStream& RandomStream) const;
	
	// 검증이 끝난 셔터 선분 하나를 실제 셔터 액터로 생성한다.
	bool SpawnPreviewSegment(
		const FProjectShutterGridSegment& Segment,
		int32 SegmentIndex);
	
	// 생성기 컴포넌트의 기준이 되는 루트다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shutter Generator", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;
	
	// 에디터에서 방화셔터 생성 구역을 보여주는 박스다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shutter Generator", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> GenerationArea;
	
	// 방화셔터가 생성될 구역의 가로와 세로 크기다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Area", meta = (AllowPrivateAccess = "true", Units = "cm"))
	FVector2D AreaEndOffset = FVector2D(3000.0, 3000.0);
	
	// 에디터에서 표시할 생성 구역의 높이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Area", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", Units = "cm"))
	float VisualizationHeight = 300.0f;
	
	// 에디터에서 생성 구역 박스를 표시할지 결정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Area", meta = (AllowPrivateAccess = "true"))
	bool bShowGenerationArea = true;
	
	// 방화셔터 1 개가 차지하는 격자 길이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Grid", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", Units = "cm"))
	float ShutterLength = 300.0f;

	// 생성기가 실제로 생성할 방화셔터 액터 또는 BP 클래스다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Spawn", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AFireShutterActor> ShutterClass;

	// 1 일 차 미리보기에서 생성할 최대 셔터 개수다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Spawn", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 PreviewShutterCount = 12;
	
	// 높은 연속 생성 확률을 적용할 최대 직선 셔터 개수다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Generation", meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	int32 ContinuationThreshold = 4;
	
	// 기준 개수에 도달하기 전에 직선 생성을 계속할 확률이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Generation", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float EarlyContinuationChance = 0.75;
	
	// 기준 개수에 도달한 뒤 직선 생성을 계속할 확률이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Generation", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float LateContinuationChance = 0.25;
	
	// 같은 값에서 같은 배치 결과를 만들기 위한 랜덤 시드다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Generation", meta = (AllowPrivateAccess = "true"))
	int32 RandomSeed = 12345;
	
	// 유효한 위치를 찾지 못할 때 무한 반복을 막는 최대 시도 횟수다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter Generator|Generation", meta = (AllowPrivateAccess = "true", ClampMin = "1"))
	int32 MaxPlacementAttempts = 1000;
};

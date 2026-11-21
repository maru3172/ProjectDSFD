// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectShutterAreaGenerator.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FireShutterActor.h"

namespace
{
	// 생성기에서 만들어진 셔터를 식별하는 전용 태그다.
	const FName GeneratedShutterTag(TEXT("ShutterAreaGeneratorGenerated"));
}

// 생성기 액터의 기본 컴포넌트와 설정값을 구성한다.
AProjectShutterAreaGenerator::AProjectShutterAreaGenerator()
{
	// 버튼을 눌렀을 때만 동작하므로 매 프레임 Tick을 사용하지 않는다.
	PrimaryActorTick.bCanEverTick = false;

	// 생성기 컴포넌트의 기준이 되는 루트를 만든다.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 생성기 액터는 피해를 받을 필요가 없다.
	SetCanBeDamaged(false);

	// 에디터에서 생성 구역을 표시할 박스를 만든다.
	GenerationArea = CreateDefaultSubobject<UBoxComponent>(TEXT("GenerationArea"));

	// 생성 구역 박스를 루트 컴포넌트 아래에 연결한다.
	GenerationArea->SetupAttachment(SceneRoot);

	// 표시 전용 박스이므로 충돌과 오버랩 이벤트를 사용하지 않는다.
	GenerationArea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GenerationArea->SetGenerateOverlapEvents(false);

	// 생성 구역 표시가 내비게이션에 영향을 주지 않도록 한다.
	GenerationArea->SetCanEverAffectNavigation(false);

	// 실제 게임에서는 생성 구역 박스를 숨긴다.
	GenerationArea->SetHiddenInGame(true);

	// 에디터에서 박스 외곽선이 잘 보이도록 두께와 색상을 설정한다.
	GenerationArea->SetLineThickness(2.0f);
	GenerationArea->ShapeColor = FColor(80, 180, 255);
}

// 에디터에서 설정이 변경될 때 구역 표시를 갱신한다.
void AProjectShutterAreaGenerator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 현재 구역 설정을 박스 컴포넌트에 반영한다.
	RefreshAreaVisualization();
}

// 현재 구역 크기에 맞춰 박스 표시를 갱신한다.
void AProjectShutterAreaGenerator::RefreshAreaVisualization()
{
	// 박스 컴포넌트가 없으면 갱신하지 않는다.
	if (!GenerationArea)
	{
		return;
	}

	// 시작점과 끝점 사이의 안전한 X 축 길이를 계산한다.
	const float SafeSizeX = FMath::Max(FMath::Abs(AreaEndOffset.X), ShutterLength);

	// 시작점과 끝점 사이의 안전한 Y 축 길이를 계산한다.
	const float SafeSizeY = FMath::Max(FMath::Abs(AreaEndOffset.Y), ShutterLength);

	// 구역 표시의 안전한 높이를 계산한다.
	const float SafeHeight = FMath::Max(VisualizationHeight, 1.0f);

	// 박스 컴포넌트는 전체 크기의 절반을 Extent로 사용한다.
	GenerationArea->SetBoxExtent(FVector(SafeSizeX * 0.5f, SafeSizeY * 0.5f, SafeHeight * 0.5f));

	// 시작점과 끝점의 중앙으로 영역 박스를 이동한다.
	GenerationArea->SetRelativeLocation(FVector(AreaEndOffset.X * 0.5f, AreaEndOffset.Y * 0.5f, SafeHeight * 0.5f));

	// 에디터 설정에 따라 구역 박스를 표시하거나 숨긴다.
	GenerationArea->SetVisibility(bShowGenerationArea);
}

// 현재 구역에 들어가는 격자 칸 수를 반환한다.
FIntPoint AProjectShutterAreaGenerator::GetGridCellCount() const
{
	// 0 또는 음수 길이로 나누는 상황을 방지한다.
	const float SafeLength = FMath::Max(ShutterLength, 1.0f);

	// 구역 크기를 셔터 길이로 나눠 가로와 세로 칸 수를 계산한다.
	return FIntPoint(
		FMath::Max(1, FMath::FloorToInt(FMath::Abs(AreaEndOffset.X) / SafeLength)),
		FMath::Max(1, FMath::FloorToInt(FMath::Abs(AreaEndOffset.Y) / SafeLength)));
}

EProjectShutterGridDirection AProjectShutterAreaGenerator::GetLongAxisDirection() const
{
	// X 축이 같거나 더 길면 가로 방향을 장축으로 사용한다.
	if (FMath::Abs(AreaEndOffset.X) >= FMath::Abs(AreaEndOffset.Y))
	{
		return EProjectShutterGridDirection::Horizontal;
	}
	
	// Y 축이 더 길면 세로 방향을 장축으로 사용한다.
	return EProjectShutterGridDirection::Vertical;
}

float AProjectShutterAreaGenerator::GetLongAxisLength() const
{
	// X 와 Y 중 더 긴 영역의 길이를 반환한다.
	return FMath::Max(
		FMath::Abs(AreaEndOffset.X),
		FMath::Abs(AreaEndOffset.Y));
}

float AProjectShutterAreaGenerator::GetShortAxisLength() const
{
	// X 와 Y 중 더 짧은 영역 길이를 반환한다.
	return FMath::Min(
		FMath::Abs(AreaEndOffset.X),
		FMath::Abs(AreaEndOffset.Y));
}

float AProjectShutterAreaGenerator::GetLongSectionGenerationChance() const
{
	// 0 으로 나누는 상황을 막기 위해 안전한 기준 폭을 사용한다.
	const float SafeReferenceWidth = FMath::Max(ReferenceShortAxisWidth, 1.0f);
	
	// 확률 계산에 사용할 안전한 후보 개수를 가져온다.
	const int32 CandidateCount = FMath::Max(GetShortAxisCandidateCount(), 1);
	
	// 현재 단축 폭에서 기대하는 평균 긴 구간 개수를 계산한다.
	const float TargetAverageCount = ReferenceAverageLongSectionCount * (GetShortAxisLength() / SafeReferenceWidth);
	
	// 목표 평균을 후보 수로 나눠 후보 하나의 확률을 계산한다.
	return FMath::Clamp(	
		TargetAverageCount / static_cast<float>(CandidateCount),
		0.0f,
		1.0f);
}

int32 AProjectShutterAreaGenerator::GetShortAxisCandidateCount() const
{
	// 0 으로 나누는 상황을 막기 위해 안전한 셔터 길이를 사용한다.
	const float SafeShutterLength = FMath::Max(ShutterLength, 1.0f);
	
	// 단축의 격자점 개수에 시작점을 포함한다.
	return FMath::FloorToInt(GetShortAxisLength() / SafeShutterLength) + 1;
}

FProjectShutterGridSegment AProjectShutterAreaGenerator::MakeLongStartSegment(int32 ShortAxisCandidateIndex) const
{
	// 첫 장축 셔터 데이터를 준비한다.
	FProjectShutterGridSegment Segment;
	
	// 현재 영역의 장축 방향을 적용한다.
	Segment.Direction = GetLongAxisDirection();
	
	// 가로가 장축이면 X 축 시작점에서 Y 후보를 사용한다.
	if (Segment.Direction == EProjectShutterGridDirection::Horizontal)
	{
		Segment.Start = FIntPoint(0, ShortAxisCandidateIndex);
		
		return Segment;
	}
	
	// 세로가 장축이면 Y 축 시작점에서 X 후보를 사용한다.
	Segment.Start = FIntPoint(ShortAxisCandidateIndex, 0);
	
	return Segment;
}

// 셔터 선분이 생성 구역 내부에 있는지 검사한다.
bool AProjectShutterAreaGenerator::IsSegmentInsideArea(const FProjectShutterGridSegment& Segment) const
{
	// 현재 생성 구역의 격자 크기를 가져온다.
	const FIntPoint GridSize = GetGridCellCount();
	
	// 가로 셔터: X 방향 한 칸을 사용
	if (Segment.Direction == EProjectShutterGridDirection::Horizontal)
	{
		return Segment.Start.X >= 0 &&
			Segment.Start.X < GridSize.X &&
			Segment.Start.Y >= 0 &&
			Segment.Start.Y <= GridSize.Y;
	}
	
	// 세로 셔터: Y 방향 한 칸을 사용
	return Segment.Start.X >= 0 &&
		Segment.Start.X <= GridSize.X &&
		Segment.Start.Y >= 0 &&
		Segment.Start.Y < GridSize.Y;
}

// 격자 선분을 실제 월드 트랜스폼으로 변환한다.
FTransform AProjectShutterAreaGenerator::MakeSegmentWorldTransform(const FProjectShutterGridSegment& Segment) const
{
	// 끝점이 위치한 X 축 방향을 구한다.
	const float GridSignX = 
		AreaEndOffset.X >= 0.0f
			? 1.0f
			: -1.0f;
	
	// 끝점이 위치한 Y 축 방향을 구한다.
	const float GridSignY =
		AreaEndOffset.Y >= 0.0f
			? 1.0f
			: -1.0f;
	
	// 셔터의 생성기 기준 로컬 위치를 준비한다.
	FVector LocalLocation = FVector::ZeroVector;
	
	// 셔터의 생성기 기준 로컬 회전을 준비한다.
	float LocalYaw = 0.0f;
	
	// 가로 셔터는 X 축 선분 중앙에 배치한다.
	if (Segment.Direction == EProjectShutterGridDirection::Horizontal)
	{
		LocalLocation = FVector(
			(Segment.Start.X + 0.5f) * ShutterLength * GridSignX,
			Segment.Start.Y * ShutterLength * GridSignY, 
			0.0f);
		
		// 음수 X 축 영역에서는 셔터를 반대 방향으로 회전한다.
		LocalYaw = 
			GridSignX >= 0.0f
				? 0.0f
				: 180.0f;
	}
	else
	{
		// 세로 셔터는 Y 축 선분 중앙에 배치한다.
		LocalLocation = FVector(
			Segment.Start.X * ShutterLength * GridSignX,
			(Segment.Start.Y + 0.5f) * ShutterLength * GridSignY,
			0.0f);
		
		// 끝점의 Y 축 방향에 맞춰 셔터를 회전한다.
		LocalYaw = 
			GridSignY >= 0.0f
				? 90.0f
				: -90.0f;
	}
	
	// 계산한 위치와 회전으로 로컬 트랜스폼을 만든다.
	const FTransform LocalTransform(
		FRotator(0.0f, LocalYaw, 0.0f), 
		LocalLocation);
	
	// 생성기 액터의 위치와 회전을 적용한다.
	return LocalTransform * GetActorTransform();
}

void AProjectShutterAreaGenerator::GetSegmentEndpoints(const FProjectShutterGridSegment& Segment, FIntPoint& OutStart, FIntPoint& OutEnd) const
{
	// 구조체에 저장된 좌표를 선분의 시작점으로 사용한다.
	OutStart = Segment.Start;
	
	// 가로 셔터는 시작점에서 X 축으로 1 칸 이동한다.
	if (Segment.Direction == EProjectShutterGridDirection::Horizontal)
	{
		OutEnd = Segment.Start + FIntPoint(1, 0);
		return;
	}
	
	// 세로 셔터는 시작점에서 Y 축으로 한 칸 이동한다.
	OutEnd = Segment.Start + FIntPoint(0, 1);
}

bool AProjectShutterAreaGenerator::AreSegmentsEqual(const FProjectShutterGridSegment& First, const FProjectShutterGridSegment& Second) const
{
	// 시작 위치와 방향이 모두 같아야 같은 선분이다.
	return First.Start == Second.Start &&
		First.Direction == Second.Direction;
}

int32 AProjectShutterAreaGenerator::CountSegmentsAtPoint(const FIntPoint& Point, const TArray<FProjectShutterGridSegment>& Segments) const
{
	// 지정된 점에 연결된 셔터 개수를 기록한다.
	int32 ConnectedCount = 0;
	
	// 현재 배치된 모든 셔터를 확인한다.
	for (const FProjectShutterGridSegment& Segment : Segments)
	{
		// 현저 셔터의 시작점과 끝점을 가져온다.
		FIntPoint SegmentStart;
		FIntPoint SegmentEnd;
		GetSegmentEndpoints(Segment, SegmentStart, SegmentEnd);
		
		// 지정된 점이 시작점 또는 끝점이면 연결된 셔터다.
		if (Point == SegmentStart || Point == SegmentEnd)
		{
			++ConnectedCount;
		}
	}
	
	// 해당 점에 연결된 최종 셔터 개수를 반환한다.
	return ConnectedCount;
}

bool AProjectShutterAreaGenerator::DoesSegmentCrossExistingSegment(const FProjectShutterGridSegment& Candidate, const TArray<FProjectShutterGridSegment>& Segments) const
{
	// 후보 셔터의 시작점과 끝점을 가져온다.
	FIntPoint CandidateStart;
	FIntPoint CandidateEnd;
	GetSegmentEndpoints(Candidate, CandidateStart, CandidateEnd);
	
	// 현재 배치된 모든 셔터와 비교한다.
	for (const FProjectShutterGridSegment& Existing : Segments)
	{
		// 같은 방향의 셔터끼리는 중심 교차하지 않는다.
		if (Candidate.Direction == Existing.Direction)
		{
			continue;
		}
		
		// 기존 셔터의 시작점과 끝점을 가져온다.
		FIntPoint ExistingStart;
		FIntPoint ExistingEnd;
		GetSegmentEndpoints(Existing, ExistingStart, ExistingEnd);
		
		// 가로 선분과 세로 선분을 구분해 참조한다.
		const FProjectShutterGridSegment& Horizontal = Candidate.Direction == EProjectShutterGridDirection::Horizontal ? Candidate : Existing;
		
		// 세로 방향 선분을 참조한다.
		const FProjectShutterGridSegment& Vertical = Candidate.Direction == EProjectShutterGridDirection::Vertical ? Candidate : Existing;
		
		// 가로 선분의 X 좌표가 세로 선분 내부에 있는지 확인한다.
		const bool bInsideHorizontal = 
			Vertical.Start.X > Horizontal.Start.X &&
			Vertical.Start.X < Horizontal.Start.X + 1; 
		
		// 가로 선분의 X 좌표가 세로 선분 내부에 있는지 확인한다.
		const bool bInsideVertical = 
			Horizontal.Start.Y > Vertical.Start.Y &&
			Horizontal.Start.Y < Vertical.Start.Y + 1;
		
		// 양쪽 내부를 모두 통과하면 중심 교차다.
		if (bInsideHorizontal && bInsideVertical)
		{
			return true;
		}
	}
	
	// 어떤 기존 셔터의 내부도 통과하지 않는다.
	return false;
}

bool AProjectShutterAreaGenerator::CanPlaceSegment(const FProjectShutterGridSegment& Candidate, const TArray<FProjectShutterGridSegment>& Segments) const
{
	// 생성 구역 밖으로 나가는 셔터는 배치하지 않는다.
	if (!IsSegmentInsideArea(Candidate))
	{
		return false;
	}
	
	// 같은 위치와 방향의 셔터가 이미 있는지 확인한다.
	for (const FProjectShutterGridSegment& Existing : Segments)
	{
		if (AreSegmentsEqual(Candidate, Existing))
		{
			return false;
		}
	}
	
	// 기존 셔터의 중심을 통과하는 후보는 배치하지 않는다.
	if (DoesSegmentCrossExistingSegment(Candidate, Segments))
	{
		return false;
	}
	
	// 후보 셔터의 시작점과 끝점을 가져온다.
	FIntPoint CandidateStart;
	FIntPoint CandidateEnd;
	GetSegmentEndpoints(Candidate, CandidateStart, CandidateEnd);
	
	// 시작점에 이미 셔터 2 개가 있다면 T 자 또는 십자가 된다.
	if (CountSegmentsAtPoint(CandidateStart, Segments) >= 2)
	{
		return false;
	}
	
	// 끝점에 이미 셔터 2 개가 있다면 T 자 또는 십자가 된다.
	if (CountSegmentsAtPoint(CandidateEnd, Segments) >= 1)
	{
		return false;
	}
	
	// 모든 배치 규칙을 통과한 후보만 허용한다.
	return true;
}

FProjectShutterGridSegment AProjectShutterAreaGenerator::MakeSegmentFromEndpoint(const FIntPoint& Endpoint, EProjectShutterGridDirection Direction, int32 DirectionSign) const
{
	// 새로 만들 후보 셔터 데이터를 준비한다.
	FProjectShutterGridSegment Segment;
	
	// 선택한 가로 또는 세로 방향을 후보에 적용한다.
	Segment.Direction = Direction;
	
	// 양수 방향이면 현재 끝점을 그대로 시작점으로 사용한다.
	if (DirectionSign >= 0)
	{
		Segment.Start = Endpoint;
		return Segment;
	}
	
	// 가로 음수 방향은 끝점 왼쪽을 시작점으로 사용한다.
	if (Direction == EProjectShutterGridDirection::Horizontal)
	{
		Segment.Start = Endpoint - FIntPoint(1, 0);
		return Segment;
	}
	
	// 세로 음수 방향은 끝점 아래를 시작점으로 사용한다.
	Segment.Start = Endpoint - FIntPoint(0, 1);
	return Segment;
}

float AProjectShutterAreaGenerator::GetContinuationChance(int32 StraightRunLength) const
{
	// 기준 개수 이전에는 높은 연속 생성 확률을 사용한다.
	if (StraightRunLength < ContinuationThreshold)
	{
		return FMath::Clamp(EarlyContinuationChance, 0.0f, 1.0f);
	}
	
	// 기준 개수 이후에는 낮은 연속 생성 확률을 사용한다.
	return FMath::Clamp(LateContinuationChance, 0.0f, 1.0f);
}

float AProjectShutterAreaGenerator::GetLengthBasedContinuationChance(float CurrentLength) const
{
	// 감소 시작 길이 이전에는 기본 연장 확률을 사용한다.
	if (CurrentLength < ContinuationReductionStartLength)
	{
		return FMath::Clamp(
			EarlyContinuationChance, 
			0.0f, 
			1.0f);
	}
	
	// 감소 시작 길이 이후에는 낮아진 연장 확률을 사용한다.
	return FMath::Clamp(
		LateContinuationChance,
		0.0f,
		1.0f);
}

FProjectShutterGridSegment AProjectShutterAreaGenerator::MakeRandomStartSegment(FRandomStream& RandomStream) const
{
	// 현재 생성 구역의 격자 크기를 가져온다.
	const FIntPoint GridSize = GetGridCellCount();
	
	// 새로운 시작 셔터 데이터를 준비한다.
	FProjectShutterGridSegment Segment;
	
	// 가로 또는 세로 방향을 무작위로 선택한다.
	Segment.Direction = 
		RandomStream.RandRange(0, 1) == 0
			? EProjectShutterGridDirection::Horizontal
			: EProjectShutterGridDirection::Vertical;
	
	// 가로 셔터가 들어갈 수 있는 좌표를 무작위로 선택한다.
	if (Segment.Direction == EProjectShutterGridDirection::Horizontal)
	{
		Segment.Start = FIntPoint(
			RandomStream.RandRange(0, GridSize.X - 1),
			RandomStream.RandRange(0, GridSize.Y));
		
		return Segment;
	}
	
	// 세로 셔터가 들어갈 수 있는 좌표를 무작위로 선택한다.
	Segment.Start = FIntPoint(
		RandomStream.RandRange(0, GridSize.X),
		RandomStream.RandRange(0, GridSize.Y - 1));
	
	return Segment;
}

bool AProjectShutterAreaGenerator::SpawnPreviewSegment(const FProjectShutterGridSegment& Segment, int32 SegmentIndex)
{
	// 현재 생성기 액터가 속한 월드를 가져온다.
	UWorld* World = GetWorld();
	
	// 월드와 생성할 셔터 클래스가 없으면 실패한다.
	if (!IsValid(World) || !ShutterClass)
	{
		return false;
	}
	
	// 격자 선분을 실제 월드 위치와 회전으로 변환한다.
	const FTransform SpawnTransform = MakeSegmentWorldTransform(Segment);
	
	// 생성할 셔터 액터의 기본 설정을 준비한다.
	FActorSpawnParameters SpawnParameters;
	
	// 생성된 셔터가 이 생성기 소유임을 기록한다.
	SpawnParameters.Owner = this;
	
	// 미리보기에서는 기존 충돌 여부와 관계없이 생성한다.
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
#if WITH_EDITOR
	// 에디터 실행 취소 시스템에서 생성을 추적하도록 설정한다.
	SpawnParameters.ObjectFlags |= RF_Transactional;
#endif
	// 지정된 셔터 BP 클래스로 실제 액터를 생성한다.
	AFireShutterActor* SpawnedShutter = World->SpawnActor<AFireShutterActor>(ShutterClass, SpawnTransform, SpawnParameters);
	
	// 액터 생성에 실패하면 false 를 반환한다.
	if (!IsValid(SpawnedShutter))
	{
		return false;
	}
	
	// 생성기에서 만든 셔터임을 나타내는 태그를 추가한다.
	SpawnedShutter->Tags.AddUnique(GeneratedShutterTag);
	
#if WITH_EDITOR
	// 월드 아웃라이너에서 확인할 수 있는 이름을 지정한다.
	SpawnedShutter->SetActorLabel(FString::Printf(TEXT("GeneratedShutter_%03d"), SegmentIndex));
#endif
	// 셔터 액터가 정상적으로 생성되었다.
	return true;
}

// ===================================================== 디버깅 관련 =====================================================

// 현재 구역 설정을 기준으로 미리보기 셔터를 생성한다.
void AProjectShutterAreaGenerator::GeneratePreview()
{
	// 현재 생성기 액터가 속한 월드를 가져온다.
	UWorld* World = GetWorld();

	// 유효한 월드가 없으면 생성을 진행하지 않는다.
	if (!IsValid(World))
	{
		return;
	}

	// 생성할 셔터 클래스가 지정되지 않았다면 생성을 중단한다.
	if (!ShutterClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: ShutterClass가 지정되지 않았습니다."), *GetName());
		return;
	}

	// 다시 생성하기 전에 기존 미리보기 셔터를 제거한다.
	ClearGeneratedShutters();

	// 설정된 시드로 재현 가능한 랜덤 스트림을 만든다.
	FRandomStream RandomStream(RandomSeed);
	
	// 규칙 검사를 통과한 셔터 선분 데이터를 보관한다.
	TArray<FProjectShutterGridSegment> GeneratedSegments;
	
	// 현재 연결을 확장할 끝점이다.
	FIntPoint CurrentEndpoint = FIntPoint::ZeroValue;
	
	// 현재 셔터가 진행 중인 가로 또는 세로 방향이다.
	EProjectShutterGridDirection CurrentDirection = EProjectShutterGridDirection::Horizontal;
	
	// 현재 방향에서 양수 또는 음수 방향으로 진행하는지 나타낸다.
	int32 CurrentDirectionSign = 1;
	
	// 같은 방향으로 연속 배치된 셔터 개수다.
	int32 StraightRunLength = 1;
	
	// 유효한 셔터 위치를 찾기 위해 시도한 총횟수다.
	int32 PlacementAttempts = 0;
	
	// 현재 연결을 계속 확장할 수 있는지 나타낸다.
	bool bHasActiveChain = false;
	
	// 목표 개수 또는 최대 시도 횟수에 도달할 때까지 후보를 만든다.
	while (GeneratedSegments.Num() < PreviewShutterCount && PlacementAttempts < MaxPlacementAttempts)
	{
		// 이번 후보 생성 시도를 기록한다.
		++PlacementAttempts;
		
		// 현재 확장 중인 연결이 없으면 새로운 시작 셔터를 찾는다.
		if (!bHasActiveChain)
		{
			// 구역 안에서 무작위 셔터 후보를 만든다.
			const FProjectShutterGridSegment StartCandidate = MakeRandomStartSegment(RandomStream);
			
			// 시작 후보가 배치 규칙을 만족하지 않으면 다시 시도한다.
			if (!CanPlaceSegment(StartCandidate, GeneratedSegments))
			{
				continue;
			}
			
			// 검사를 통과한 시작 셔터를 결과 배열에 추가한다.
			GeneratedSegments.Add(StartCandidate);
			
			// 시작 셔터의 양쪽 끝점을 가져온다.
			FIntPoint SegmentStart;
			FIntPoint SegmentEnd;
			GetSegmentEndpoints(StartCandidate, SegmentStart, SegmentEnd);
			
			// 시작 셔터를 어느 쪽 끝으로 확장할지 무작위로 선택한다.
			const bool bGrowFromEnd = RandomStream.RandRange(0, 1) == 1;
			
			// 선택한 성장 방향에 맞춰 현재 끝점을 저장한다.
			CurrentEndpoint = bGrowFromEnd ? SegmentEnd : SegmentStart;
			
			// 시작 셔터의 방향을 현재 진행 방향으로 사용한다.
			CurrentDirection = StartCandidate.Direction;
			
			// 끝점 쪽이면 양수, 시작점 쪽이면 음수 방향으로 진행한다.
			CurrentDirectionSign = bGrowFromEnd ? 1 : -1;
			
			// 새로운 직선 연결이 시작됐으므로 연속 개수는 1 이다.
			StraightRunLength = 1;
			
			// 다음 반복부터 현재 연결을 계속 확장한다.
			bHasActiveChain = true;
			
			continue;
		}
		
		// 현재 연속 개수에 맞는 직선 생성 확률을 가져온다.
		const float ContinuationChance = GetContinuationChance(StraightRunLength);
		
		// 확률 판정으로 직진할지 90 도 회전할지 결정한다.
		const bool bContinueStraight = RandomStream.FRand() < ContinuationChance;
		
		// 다음 셔터가 배치될 방향을 저장한다.
		EProjectShutterGridDirection NextDirection = CurrentDirection;
		
		// 다음 셔터가 진행할 양수 또는 음수 방향을 지정한다.
		int32 NextDirectionSign = CurrentDirectionSign;
		
		// 직진 확률에 실패하면 현재 끝점에서 90 도 회전한다.
		if (!bContinueStraight)
		{
			// 가로 진행 중이면 세로, 세로 진행 중이면 가로로 전환한다.
			NextDirection = CurrentDirection == 
				EProjectShutterGridDirection::Horizontal
					? EProjectShutterGridDirection::Vertical
					: EProjectShutterGridDirection::Horizontal;
			
			// 회전 후 어느 쪽으로 진행할지 무작위로 선택한다.
			NextDirectionSign = 
				RandomStream.RandRange(0, 1) == 0 
					? -1 
					: 1;
		}
		
		// 현재 끝점에서 선택한 방향으로 이어지는 후보를 만든다.
		const FProjectShutterGridSegment Candidate = MakeSegmentFromEndpoint(CurrentEndpoint, NextDirection, NextDirectionSign);
		
		// 후보가 배치 규칙을 만족하지 않으면 현재 연결을 끝낸다.
		if (!CanPlaceSegment(Candidate, GeneratedSegments))
		{
			bHasActiveChain = false;
			continue;
		}
		
		// 배치 규칙을 통과한 후보를 결과 배열에 추가한다.
		GeneratedSegments.Add(Candidate);
		
		// 새로 추가한 셔터의 양쪽 끝점을 가져온다.
		FIntPoint CandidateStart;
		FIntPoint CandidateEnd;
		GetSegmentEndpoints(Candidate, CandidateStart, CandidateEnd);
		
		// 진행 방향 반대편에 있는 끝점을 다음 성장 지점으로 사용한다.
		CurrentEndpoint = 
			NextDirectionSign >= 0 
				? CandidateEnd 
				: CandidateStart;
		
		// 새 셔터의 방향을 현재 진행 방향으로 저장한다.
		CurrentDirection = NextDirection;
		
		// 새 셔터가 진행한 양수 또는 음수 방향을 저장한다.
		CurrentDirectionSign = NextDirectionSign;
		
		// 직진했다면 연속 개수를 늘리고 회전했다면 1 로 초기화한다.
		StraightRunLength = 
			bContinueStraight 
				? StraightRunLength + 1 
				: 1;
	}
	
	// 규칙 검사가 완료된 선분만 실제 셔터 액터로 생성한다.
	for (int32 SegmentIndex = 0; SegmentIndex < GeneratedSegments.Num(); ++SegmentIndex)
	{
		// 현재 선분을 실제 셔터 액터로 생성한다.
		const bool bSpawnSucceeded = SpawnPreviewSegment(GeneratedSegments[SegmentIndex], SegmentIndex);
		
		// 액터 생성에 실패하면 로그에 남기고 다음 선분을 처리한다.
		if (!bSpawnSucceeded)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: %d 번 셔터 액터 생성에 실패했습니다."), *GetName(), SegmentIndex);
		}
	}
	
	// 생성 결과와 사용한 랜덤 시드를 로그에 기록한다.
	UE_LOG(LogTemp, Log, TEXT("%s: 셔터 %d 개 생성, 시도 %d 회, 시드 %d"), *GetName(), GeneratedSegments.Num(), PlacementAttempts, RandomSeed);
	
	// 목표 개수에 도달하지 못했다면 배치 규칙 제한을 알린다.
	if (GeneratedSegments.Num() < PreviewShutterCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: 목표 %d 개 중 %d 개만 배치했습니다."), *GetName(), PreviewShutterCount, GeneratedSegments.Num());
	}
}

// 이 생성기가 만든 미리보기 셔터를 모두 제거한다.
void AProjectShutterAreaGenerator::ClearGeneratedShutters()
{
	// 현재 생성기 액터가 속한 월드를 가져온다.
	UWorld* World = GetWorld();

	// 유효한 월드가 없으면 삭제를 진행하지 않는다.
	if (!IsValid(World))
	{
		return;
	}

	// 반복 중 액터를 바로 제거하지 않도록 삭제 대상을 보관한다.
	TArray<AFireShutterActor*> ShuttersToRemove;

	// 월드에 존재하는 모든 방화셔터 액터를 확인한다.
	for (TActorIterator<AFireShutterActor> It(World); It; ++It)
	{
		// 현재 반복 중인 방화셔터 액터를 가져온다.
		AFireShutterActor* Shutter = *It;

		// 유효하지 않은 셔터는 건너뛴다.
		if (!IsValid(Shutter))
		{
			continue;
		}

		// 다른 생성기나 수동 배치 셔터는 건드리지 않는다.
		if (Shutter->GetOwner() != this)
		{
			continue;
		}

		// 생성기 전용 태그가 없는 셔터는 건드리지 않는다.
		if (!Shutter->Tags.Contains(GeneratedShutterTag))
		{
			continue;
		}

		// 조건을 만족한 셔터를 삭제 대상에 추가한다.
		ShuttersToRemove.Add(Shutter);
	}

	// 검색이 끝난 뒤 수집된 셔터를 안전하게 제거한다.
	for (AFireShutterActor* Shutter : ShuttersToRemove)
	{
#if WITH_EDITOR
		// 에디터의 실행 취소 기록에 삭제 작업을 포함한다.
		Shutter->Modify();
#endif

		// 생성된 셔터 액터를 월드에서 제거한다.
		Shutter->Destroy();
	}
}
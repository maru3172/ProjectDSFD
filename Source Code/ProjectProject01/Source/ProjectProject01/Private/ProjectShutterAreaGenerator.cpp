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
		FMath::Max(0, FMath::FloorToInt(FMath::Abs(AreaEndOffset.X) / SafeLength)),
		FMath::Max(0, FMath::FloorToInt(FMath::Abs(AreaEndOffset.Y) / SafeLength)));
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

EProjectShutterGridDirection AProjectShutterAreaGenerator::GetShortAxisDirection() const
{
	// 장축이 가로면 세로 방향을 단축으로 사용한다.
	if (GetLongAxisDirection() == EProjectShutterGridDirection::Horizontal)
	{
		return EProjectShutterGridDirection::Vertical;
	}

	// 장축이 세로면 가로 방향을 단축으로 사용한다.
	return EProjectShutterGridDirection::Horizontal;
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

int32 AProjectShutterAreaGenerator::GetShortAxisCandidateCount() const
{
	// 기존 경계 검사와 동일한 격자 칸 수를 사용한다.
	const FIntPoint GridSize = GetGridCellCount();
	
	// 장축에 수직인 축의 격자점 수를 반환한다.
	return GetLongAxisDirection() == EProjectShutterGridDirection::Horizontal
		? GridSize.Y + 1
		: GridSize.X + 1;
}

float AProjectShutterAreaGenerator::GetSectionGenerationChance() const
{
	// 기존 BP 에 저장된 기준 폭을 사용한다.
	const float SafeReferenceWidth = FMath::Max(ReferenceLongAxisWidth, 1.0f);
	
	// 현재 단축 폭에 해당하는 예상 구간 개수를 반환한다.
	const float TargetAverageCount = ReferenceAverageSectionCount * (GetShortAxisLength() / SafeReferenceWidth);
	
	// 시작 후보 하나에 적용할 확률을 반환한다.
	return FMath::Clamp(	
		TargetAverageCount / static_cast<float>(GetShortAxisCandidateCount()),
		0.0f,
		1.0f);
}

FProjectShutterGridSegment AProjectShutterAreaGenerator::MakeSectionStartSegment(int32 ShortAxisCandidateIndex) const
{
	// 첫 셔터의 방향과 격자 좌표를 담는다.
	FProjectShutterGridSegment Segment;
	Segment.Direction = GetLongAxisDirection();
	
	// 장축 좌표는 0 에서 시작하고 단축 좌표만 후보마다 바꾼다.
	Segment.Start = Segment.Direction == EProjectShutterGridDirection::Horizontal
		? FIntPoint(0, ShortAxisCandidateIndex)
		: FIntPoint(ShortAxisCandidateIndex, 0);
	
	return Segment;
}

void AProjectShutterAreaGenerator::TryAddPerpendicularEndSegment(const FIntPoint& SectionEndpoint, EProjectShutterGridDirection SectionDirection, FRandomStream& RandomStream, TArray<FProjectShutterGridSegment>& Segments) const
{
	// 주 생성 구간과 직각인 끝 셔터 방향을 선택한다.
	const EProjectShutterGridDirection PerpendicularDirection =
		SectionDirection ==
			EProjectShutterGridDirection::Horizontal
				? EProjectShutterGridDirection::Vertical
				: EProjectShutterGridDirection::Horizontal;
	
	// 끝점에서 양쪽 중 어느 방향으로 뻗을지 무작위로 선택한다.
	const int32 PerpendicularDirectionSign =
		RandomStream.RandRange(0, 1) == 0
			? -1
			: 1;
	
	// 선택한 방향으로 길이 한 칸의 직각 셔터를 만든다.
	const FProjectShutterGridSegment Candidate =
		MakeSegmentFromEndpoint(SectionEndpoint, PerpendicularDirection, PerpendicularDirectionSign);
	
	// 구역 밖이거나 배치 규칙을 위반하면 추가하지 않는다.
	if (!CanPlaceSegment(Candidate, Segments))
	{
		return;
	}
	
	// 검사를 통과한 짧은 셔터를 결과 배열에 추가한다.
	Segments.Add(Candidate);
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
		// 현재 셔터의 시작점과 끝점을 가져온다.
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
		
		// 가로 선분의 Y 좌표가 세로 선분 내부에 있는지 확인한다.
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

bool AProjectShutterAreaGenerator::WouldCreateClosedCycle(const FProjectShutterGridSegment& Candidate, const TArray<FProjectShutterGridSegment>& Segments) const
{
	// 후보 셔터의 양 끝점을 가져온다.
	FIntPoint CandidateStart;
	FIntPoint CandidateEnd;
	GetSegmentEndpoints(Candidate, CandidateStart, CandidateEnd);

	// 시작점에서 기존 셔터 경로를 따라 탐색할 격자점을 보관한다.
	TArray<FIntPoint> PendingPoints;
	PendingPoints.Add(CandidateStart);

	// 같은 격자점을 반복해서 탐색하지 않도록 방문한 점을 기록한다.
	TSet<FIntPoint> VisitedPoints;
	VisitedPoints.Add(CandidateStart);

	// 기존 셔터만 이용해 후보의 반대쪽 끝점에 도달할 수 있는지 확인한다.
	for (int32 PendingIndex = 0; PendingIndex < PendingPoints.Num(); ++PendingIndex)
	{
		// 현재 탐색 중인 격자점을 가져온다.
		const FIntPoint CurrentPoint = PendingPoints[PendingIndex];

		// 현재 격자점과 연결된 모든 기존 셔터를 확인한다.
		for (const FProjectShutterGridSegment& Existing : Segments)
		{
			// 기존 셔터의 양 끝점을 가져온다.
			FIntPoint ExistingStart;
			FIntPoint ExistingEnd;
			GetSegmentEndpoints(Existing, ExistingStart, ExistingEnd);

			// 현재 점과 연결되지 않은 셔터는 건너뛴다.
			if (CurrentPoint != ExistingStart && CurrentPoint != ExistingEnd)
			{
				continue;
			}

			// 현재 점의 반대편에 있는 이웃 격자점을 가져온다.
			const FIntPoint NeighborPoint =
				CurrentPoint == ExistingStart
					? ExistingEnd
					: ExistingStart;

			// 기존 경로로 후보 끝점에 도달하면 후보가 닫힌 고리를 만든다.
			if (NeighborPoint == CandidateEnd)
			{
				return true;
			}

			// 이미 확인한 격자점은 다시 탐색하지 않는다.
			if (VisitedPoints.Contains(NeighborPoint))
			{
				continue;
			}

			// 새로 발견한 격자점을 다음 탐색 대상으로 등록한다.
			VisitedPoints.Add(NeighborPoint);
			PendingPoints.Add(NeighborPoint);
		}
	}

	// 후보 양 끝점 사이에 기존 경로가 없으므로 닫힌 고리가 생기지 않는다.
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

	// 기존 경로의 두 점을 다시 연결해 닫힌 공간을 만드는 후보는 배치하지 않는다.
	if (WouldCreateClosedCycle(Candidate, Segments))
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
	if (CountSegmentsAtPoint(CandidateEnd, Segments) >= 2)
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
	
	// 장축 방향에 셔터 한 개가 들어갈 공간이 없으면 종료한다.
	if (GetLongAxisLength() < ShutterLength)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: 단축 길이가 셔터 길이보다 짧습니다."), *GetName());
		
		ClearGeneratedShutters();
		return;
	}

	// 다시 생성하기 전에 기존 미리보기 셔터를 제거한다.
	ClearGeneratedShutters();

	// 설정된 시드로 재현 가능한 랜덤 스트림을 만든다.
	FRandomStream RandomStream(RandomSeed);
	
	// 규칙 검사를 통과한 셔터 선분 데이터를 보관한다.
	TArray<FProjectShutterGridSegment> GeneratedSegments;
	
	// 생성된 각 주 구간의 시작점과 끝점을 보관한다.
	TArray<TPair<FIntPoint, FIntPoint>> SectionEndpoints;
	
	// 영역의 장축 방향을 주 셔터 생성 방향으로 사용한다.
	const EProjectShutterGridDirection SectionDirection = GetLongAxisDirection();
	
	// 단축을 따라 나열할 시작 후보 수를 가져온다.
	const int32 CandidateCount = GetShortAxisCandidateCount();
	
	// 후보 하나당 주 구간 생성 확률을 계산한다.
	const float GenerationChance = GetSectionGenerationChance();
	
	// 실제로 생성된 주 구간 개수를 기록한다.
	int32 GeneratedSectionCount = 0;
	
	// 단축에 따라 나열된 시작 후보를 순서대로 확인한다.
	for (int32 CandidateIndex = 0; CandidateIndex < CandidateCount; ++CandidateIndex)
	{
		// 후보별 독립 확률 판정에 실패하면 건너뛴다.
		if (RandomStream.FRand() >= GenerationChance)
		{
			continue;
		}
		
		// 현재 단축 후보에서 장축 방향으로 첫 셔터를 만든다.
		const FProjectShutterGridSegment StartSegment = MakeSectionStartSegment(CandidateIndex);
		
		// 첫 셔터를 배치할 수 없으면 해당 후보를 건너뛴다.
		if (!CanPlaceSegment(StartSegment, GeneratedSegments))
		{
			continue;
		}
		
		// 첫 주 구간 셔터를 결과에 추가한다.
		GeneratedSegments.Add(StartSegment);
		
		// 첫 셔터의 시작점과 끝점을 가져온다.
		FIntPoint SectionStart;
		FIntPoint CurrentEndpoint;
		GetSegmentEndpoints(StartSegment, SectionStart, CurrentEndpoint);
		
		// 현재 주 구간을 구성하는 셔터 개수다.
		int32 SectionSegmentCount = 1;
		
		// 연장 확률과 배치 규칙이 허용하는 동안 장축 방향으로 연장한다.
		while (true)
		{
			// 현재 주 구간의 실제 길이를 계산한다.
			const float CurrentLength = SectionSegmentCount * ShutterLength;
			
			// 현재 길이에 맞는 다음 셔터 연장 확률을 가져온다.
			const float ContinuationChance = GetLengthBasedContinuationChance(CurrentLength);
			
			// 연장 확률 판정에 실패하면 현재 주 구간을 종료한다.
			if (RandomStream.FRand() >= ContinuationChance)
			{
				break;
			}
			
			// 현재 끝점에서 장축 방향으로 다음 셔터를 만든다.
			const FProjectShutterGridSegment NextSegment = MakeSegmentFromEndpoint(CurrentEndpoint, SectionDirection, 1);
			
			// 영역 밖이거나 기존 셔터와 충돌하면 연장을 종료한다.
			if (!CanPlaceSegment(NextSegment, GeneratedSegments))
			{
				break;
			}
			
			// 검사를 통과한 장축 셔터를 결과에 추가한다.
			GeneratedSegments.Add(NextSegment);
			
			// 새로 추가한 셔터의 시작점과 끝점을 가져온다.
			FIntPoint NextStart;
			FIntPoint NextEnd;
			GetSegmentEndpoints(NextSegment, NextStart, NextEnd);
			
			// 다음 연장은 새 셔터의 끝점에서 시작한다.
			CurrentEndpoint = NextEnd;
			
			// 주 구간의 셔터 개수를 증가시킨다.
			++SectionSegmentCount;
		}
		
		// 완성된 주 구간의 양 끝점을 저장한다.
		SectionEndpoints.Add(TPair<FIntPoint, FIntPoint>(SectionStart, CurrentEndpoint));
		
		// 생성된 주 구간 개수를 증가시킨다.
		++GeneratedSectionCount;
	}
	
	// 모든 주 구간이 완성된 뒤 각 구간의 양 끝을 처리한다.
	for (const TPair<FIntPoint, FIntPoint>& Endpoints : SectionEndpoints)
	{
		// 주 구간 시작점에 직각 셔터 한 개를 시도한다.
		TryAddPerpendicularEndSegment(Endpoints.Key, SectionDirection, RandomStream, GeneratedSegments);
		
		// 주 구간 끝점에 직각 셔터 한 개를 시도한다.
		TryAddPerpendicularEndSegment(Endpoints.Value, SectionDirection, RandomStream, GeneratedSegments);
	}
	
	// 검증이 끝난 선분만 실제 셔터 액터로 생성한다.
	for (int32 SegmentIndex = 0; SegmentIndex < GeneratedSegments.Num(); ++SegmentIndex)
	{
		// 현재 선분을 실제 셔터 액터로 생성한다.
		const bool bSpawnSucceed = 
			SpawnPreviewSegment(GeneratedSegments[SegmentIndex], SegmentIndex);
		
		// 액터 생성에 실패하면 로그에 남긴다.
		if (!bSpawnSucceed)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: %d 번 셔터 생성에 실패했습니다."), *GetName(), SegmentIndex);
		}
	}
	
	// 후보 수와 실제 생성 결과를 로그에 기록한다.
	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s: 단축 후보 %d 개, 확률 %.3f, 주 구간 %d 개, 셔터 %d 개, 시드 %d"),
		*GetName(), CandidateCount, GenerationChance, GeneratedSectionCount, GeneratedSegments.Num(), RandomSeed);
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

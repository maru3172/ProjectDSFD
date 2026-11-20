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

	// 구역의 안전한 가로 크기를 계산한다.
	const float SafeSizeX = FMath::Max(AreaSize.X, 300.0f);

	// 구역의 안전한 세로 크기를 계산한다.
	const float SafeSizeY = FMath::Max(AreaSize.Y, 300.0f);

	// 구역 표시의 안전한 높이를 계산한다.
	const float SafeHeight = FMath::Max(VisualizationHeight, 1.0f);

	// 박스 컴포넌트는 전체 크기의 절반을 Extent로 사용한다.
	GenerationArea->SetBoxExtent(FVector(SafeSizeX * 0.5f, SafeSizeY * 0.5f, SafeHeight * 0.5f));

	// 생성기 위치가 구역 바닥의 중앙이 되도록 박스를 위로 올린다.
	GenerationArea->SetRelativeLocation(FVector(0.0f, 0.0f, SafeHeight * 0.5f));

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
		FMath::Max(1, FMath::FloorToInt(AreaSize.X / SafeLength)),
		FMath::Max(1, FMath::FloorToInt(AreaSize.Y / SafeLength)));
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
	// 현재 생성 구역의 격자 크기를 가져온다.
	const FIntPoint GridSize = GetGridCellCount();

	// 실제 격자가 사용하는 가로 길이를 계산한다.
	const float UsedWidth = GridSize.X * ShutterLength;

	// 실제 격자가 사용하는 세로 길이를 계산한다.
	const float UsedDepth = GridSize.Y * ShutterLength;

	// 생성 구역 중앙을 기준으로 격자의 왼쪽 아래 시작점을 계산한다.
	const FVector GridOrigin(-UsedWidth * 0.5f, -UsedDepth * 0.5f, 0.0f);

	// 셔터의 생성기 기준 로컬 위치를 보관한다.
	FVector LocalLocation = GridOrigin;

	// 셔터의 생성기 기준 로컬 회전값을 보관한다.
	float LocalYaw = 0.0f;
	
	if (Segment.Direction == EProjectShutterGridDirection::Horizontal)
	{
		LocalLocation += FVector(
			(Segment.Start.X + 0.5f) * ShutterLength,
			Segment.Start.Y * ShutterLength,
			0.0f);
		
		LocalYaw = 0.0f;
	}
	else
	{
		LocalLocation += FVector(
			Segment.Start.X * ShutterLength,
			(Segment.Start.Y + 0.5f) * ShutterLength,
			0.0f);
		
		LocalYaw = 90.0f;
	}
	
	// 계산한 위치와 회전으로 생성기 기준 로컬 트랜스폼을 만든다.
	const FTransform LocalTransform(
		FRotator(0.0f, LocalYaw, 0.0f),
		LocalLocation);

	// 생성기 액터의 트랜스폼을 적용해 월드 트랜스폼으로 변환한다.
	return LocalTransform * GetActorTransform();
}

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

	// 현재 구역에 들어가는 가로와 세로 격자 칸 수를 가져온다.
	const FIntPoint GridSize = GetGridCellCount();

	// 실제로 생성된 셔터 개수를 기록한다.
	int32 SpawnedCount = 0;

	// 격자의 세로 좌표를 순서대로 확인한다.
	for (int32 GridY = 0; GridY <= GridSize.Y && SpawnedCount < PreviewShutterCount; ++GridY)
	{
		// 격자의 가로 좌표를 순서대로 확인한다.
		for (int32 GridX = 0; GridX <= GridSize.X && SpawnedCount < PreviewShutterCount; ++GridX)
		{
			// 현재 위치에서 생성할 셔터 선분 데이터를 만든다.
			FProjectShutterGridSegment Segment;

			// 현재 반복 중인 격자 위치를 시작점으로 사용한다.
			Segment.Start = FIntPoint(GridX, GridY);

			// 행마다 가로와 세로 방향을 번갈아 사용한다.
			Segment.Direction = GridY % 2 == 0
				? EProjectShutterGridDirection::Horizontal
				: EProjectShutterGridDirection::Vertical;

			// 구역 경계를 벗어나는 선분은 생성하지 않는다.
			if (!IsSegmentInsideArea(Segment))
			{
				continue;
			}

			// 격자 좌표를 실제 월드 위치와 회전으로 변환한다.
			const FTransform SpawnTransform = MakeSegmentWorldTransform(Segment);

			// 생성할 액터의 소유자와 충돌 처리 방법을 설정한다.
			FActorSpawnParameters SpawnParameters;

			// 삭제할 때 생성자를 구분할 수 있도록 소유자를 지정한다.
			SpawnParameters.Owner = this;

			// 미리보기 배치에서는 기존 충돌과 관계없이 생성한다.
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

#if WITH_EDITOR
			// 에디터 실행 취소 시스템이 생성 작업을 기록하도록 설정한다.
			SpawnParameters.ObjectFlags |= RF_Transactional;
#endif

			// 지정된 셔터 클래스로 실제 셔터 액터를 생성한다.
			AFireShutterActor* SpawnedShutter = World->SpawnActor<AFireShutterActor>(
				ShutterClass,
				SpawnTransform,
				SpawnParameters);

			// 액터 생성에 실패했다면 다음 위치를 확인한다.
			if (!IsValid(SpawnedShutter))
			{
				UE_LOG(LogTemp, Warning, TEXT("%s: 셔터 생성에 실패했습니다."), *GetName());
				continue;
			}

			// 생성기에서 만든 셔터임을 나타내는 태그를 추가한다.
			SpawnedShutter->Tags.AddUnique(GeneratedShutterTag);

#if WITH_EDITOR
			// 월드 아웃라이너에서 위치와 방향을 확인하기 쉽게 이름을 지정한다.
			SpawnedShutter->SetActorLabel(FString::Printf(
				TEXT("GeneratedShutter_%s_%02d_%02d"),
				Segment.Direction == EProjectShutterGridDirection::Horizontal ? TEXT("H") : TEXT("V"),
				Segment.Start.X,
				Segment.Start.Y));
#endif

			// 정상적으로 생성된 셔터 개수를 증가시킨다.
			++SpawnedCount;
		}
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

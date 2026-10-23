// Fill out your copyright notice in the Description page of Project Settings.


#include "FireShutterActor.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"

AFireShutterActor::AFireShutterActor()
{
	// 셔터가 움직이는 동안 Tick 을 사용할 수 있도록 설정
	PrimaryActorTick.bCanEverTick = true;
	
	// 실제 이동이 시작되기 전까지는 Tick 을 실행하지 않는다.
	PrimaryActorTick.bStartWithTickEnabled = false;
	
	// 셔터 메시가 플레이어와 다른 물체를 막도록 충돌을 활성화한다.
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ItemMesh->SetCollisionProfileName(TEXT("BlockAll"));
	
	// UI 에 표시할 기본 상호작용 문구
	InteractionText = NSLOCTEXT("Interaction", "FireShutterInteractionText", "방화 셔터 작동");
}

void AFireShutterActor::BeginPlay()
{
	Super::BeginPlay();
	
	// 에디터에서 배치한 셔터 메시의 위치를 닫힌 위치로 사용한다.
	ClosedLocation = ItemMesh->GetRelativeLocation();
	
	// 닫힌 위치에서 OpenHeight 만큼 위쪽을 열린 위치로 사용한다.
	OpenLocation = ClosedLocation + FVector(0, 0, OpenHeight);
	
	CurrentOpenAlpha = 0.0f;
	TargetOpenAlpha = 0.0f;
	
	// 시작할 때는 닫힌 위치를 정확하게 적용한다.
	ItemMesh->SetRelativeLocation(ClosedLocation);
	
	// 매 프레임 호출되던 Tick 을 중지
	SetActorTickEnabled(false);
}

void AFireShutterActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	const float TotalDistance = FVector::Distance(ClosedLocation, OpenLocation);
	
	// 이동 거리가 없거나 속도가 0 이면 움직일 수 없으므로 Tick 을 중지한다.
	if (TotalDistance <= KINDA_SMALL_NUMBER || MoveSpeed <= KINDA_SMALL_NUMBER)
	{
		SetActorTickEnabled(false);
		return;
	}
	
	// cm/s 단위의 MoveSpeed 를 0 ~ 1 범위에서 사용할 Alpha 속도로 변환한다.
	const float AlphaSpeed = MoveSpeed / TotalDistance;
	
	// 현재 Alpha 를 목표 Alpha 방향으로 일정한 속도로 이동시킨다.
	// 목표가 바뀌면 현재 위치에서 즉시 반대 방향으로 진행한다.
	CurrentOpenAlpha = FMath::FInterpConstantTo(
		CurrentOpenAlpha,
		TargetOpenAlpha,
		DeltaTime,
		AlphaSpeed
		);
	
	// 계산 오차가 발생해도 이동 범위를 벗어나지 않게 제한한다.
	CurrentOpenAlpha = FMath::Clamp(CurrentOpenAlpha, 0.0f, 1.0f);
	
	// Alpha 가 0 이면 ClosedLocation
	// Alpha 가 1 이면 OpenLocation
	const FVector NewLocation = FMath::Lerp(ClosedLocation, OpenLocation, CurrentOpenAlpha);
	
	ItemMesh->SetRelativeLocation(NewLocation);
	
	// 목표에 도착했다면 위치를 정확하게 보정하고 Tick 을 중지한다.
	if (FMath::IsNearlyEqual(CurrentOpenAlpha, TargetOpenAlpha, ArrivalTolerance))
	{
		CurrentOpenAlpha = TargetOpenAlpha;
		
		const FVector FinalLocation = FMath::Lerp(ClosedLocation, OpenLocation, CurrentOpenAlpha);
		
		ItemMesh->SetRelativeLocation(FinalLocation);
		
		SetActorTickEnabled(false);
	}
}

void AFireShutterActor::HandleInteraction(APawn* Interator)
{
	Super::HandleInteraction(Interator);
	
	// 현재 목표가 열린 상태라면 닫힌 상태로 변경하고, 닫힌 상태라면 열린 상태로 변경한다.
	// 이동 중에 다시 상호작용해도 목표값이 반전되므로 현재 위치부터 즉시 반대 방향으로 이동한다.
	TargetOpenAlpha = TargetOpenAlpha >= 0.5f ? 0.0f : 1.0f;
	
	// 셔터 이동을 위해 Tick 을 활성화한다.
	SetActorTickEnabled(true);
}
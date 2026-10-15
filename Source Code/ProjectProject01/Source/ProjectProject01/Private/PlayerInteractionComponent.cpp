// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerInteractionComponent.h"

#include "DrawDebugHelpers.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "InteractionInterface.h"

// Sets default values for this component's properties
UPlayerInteractionComponent::UPlayerInteractionComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;
	
}

void UPlayerInteractionComponent::TryInteract()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwnerPawn))
	{
		return;
	}
	
	AController* Controller = OwnerPawn->GetController();
	if (!IsValid(Controller))
	{
		return;
	}
	
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation,ViewRotation);
	
	const FVector TraceEnd = ViewLocation + (ViewRotation.Vector() * InteractionDistance);
	
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(PlayerInteractionTrace),
		false,
		OwnerPawn
		);
	
	QueryParams.AddIgnoredActor(OwnerPawn);
	
	FHitResult HitResult;
	
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		ViewLocation,
		TraceEnd,
		TraceChannel,
		QueryParams
		);
	
#if ENABLE_DRAW_DEBUG
	if (bDrawDebugTrace)
	{
		const FColor TraceColor = bHit ? FColor::Green : FColor::Red;
		
		// 선 그리기
		DrawDebugLine(
			GetWorld(),
			ViewLocation,
			TraceEnd,
			TraceColor,
			false,
			1.0f,
			0,
			1.5f
			);
		
		if (bHit)
		{
			// 구 그리기
			DrawDebugSphere(
				GetWorld(),
				HitResult.ImpactPoint,
				8.0f,
				12,
				TraceColor,
				false,
				1.0f
				);
		}
	}
#endif
	if (!bHit)
	{
		return;
	}
	
	// Line Trace 에 맞은 액터를 가져온다.
	AActor* HitActor = HitResult.GetActor();
	if (!IsValid(HitActor))
	{
		return;
	}
	
	// 맞은 액터가 상호작용 인터페이스를 구현하는지 확인한다.
	if (!HitActor->Implements<UInteractionInterface>())
	{
		return;
	}
	
	// 현재 플레이어가 이 액터와 상호작용할 수 있는지 확인한다.
	if (!IInteractionInterface::Execute_CanInteract(HitActor, OwnerPawn))
	{
		return;
	}
	
	// 인터페이스를 통해 대상 액터의 상호작용을 실행한다.
	IInteractionInterface::Execute_Interact(HitActor, OwnerPawn);
}
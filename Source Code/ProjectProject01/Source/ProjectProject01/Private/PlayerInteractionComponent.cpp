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
	
	// 트레이스에 맞은 액터를 가져온다.
	AActor* HitActor = bHit ? HitResult.GetActor() : nullptr;

	// 유효한 액터이고 인터페이스를 구현했는지 확인합니다.
	const bool bImplementsInteraction = IsValid(HitActor) && HitActor->Implements<UInteractionInterface>();

	// 인터페이스를 구현했고 현재 실제로 상호작용 가능한지 확인합니다.
	const bool bCanInteract = bImplementsInteraction && IInteractionInterface::Execute_CanInteract(HitActor, OwnerPawn);
	
#if ENABLE_DRAW_DEBUG
	if (bDrawDebugTrace)
	{
		FColor TraceColor = FColor::Red;

		if (bCanInteract)
		{
			// 상호작용 가능한 대상
			TraceColor = FColor::Green;
		}
		else if (bHit)
		{
			// 충돌은 했지만 상호작용할 수 없는 대상
			TraceColor = FColor::Red;
		}

		DrawDebugLine(
			GetWorld(),
			ViewLocation,
			TraceEnd,
			TraceColor,
			false,
			1.0f,
			0,
			1.5f);

		if (bHit)
		{
			DrawDebugSphere(
				GetWorld(),
				HitResult.ImpactPoint,
				8.0f,
				12,
				TraceColor,
				false,
				1.0f);
		}
	}
#endif
	// 인터페이스를 통해 대상 액터의 상호작용을 실행한다.
	IInteractionInterface::Execute_Interact(HitActor, OwnerPawn);
}
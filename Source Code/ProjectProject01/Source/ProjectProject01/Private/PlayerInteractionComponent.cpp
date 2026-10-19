// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerInteractionComponent.h"

#include "DrawDebugHelpers.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "InteractionInterface.h"
#include "InteractionPromptWidget.h"

#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"

// Sets default values for this component's properties
UPlayerInteractionComponent::UPlayerInteractionComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	
	// 매 프레임이 아니라 0.1 초마다 검사
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UPlayerInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwnerPawn) ||
		!OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	APlayerController* PlayerController =
		Cast<APlayerController>(
			OwnerPawn->GetController());

	if (!IsValid(PlayerController) ||
		!InteractionWidgetClass)
	{
		return;
	}

	InteractionWidget =
		CreateWidget<UInteractionPromptWidget>(
			PlayerController,
			InteractionWidgetClass);

	if (IsValid(InteractionWidget))
	{
		InteractionWidget->AddToViewport();
		InteractionWidget->HideInteractionPrompt();
	}
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
	if (!bCanInteract)
	{
		return;
	}

	// 인터페이스를 통해 대상 액터의 상호작용을 실행한다.
	IInteractionInterface::Execute_Interact(HitActor, OwnerPawn);
}

AActor* UPlayerInteractionComponent::FindInteractableActor(const bool bDrawDebug) const
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwnerPawn) || !OwnerPawn->IsLocallyControlled())
	{
		return nullptr;
	}

	AController* Controller = OwnerPawn->GetController();
	UWorld* World = GetWorld();
	if (!IsValid(Controller) || !IsValid(World))
	{
		return nullptr;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * InteractionDistance;

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(PlayerInteractionFocusTrace),
		false,
		OwnerPawn);
	QueryParams.AddIgnoredActor(OwnerPawn);

	FHitResult HitResult;
	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		ViewLocation,
		TraceEnd,
		TraceChannel,
		QueryParams);

	AActor* HitActor = bHit ? HitResult.GetActor() : nullptr;
	const bool bCanInteract =
		IsValid(HitActor) &&
		HitActor->Implements<UInteractionInterface>() &&
		IInteractionInterface::Execute_CanInteract(
			HitActor,
			OwnerPawn);

#if ENABLE_DRAW_DEBUG
	if (bDrawDebug && bDrawDebugTrace)
	{
		const FColor TraceColor = bCanInteract ? FColor::Green : FColor::Red;
		DrawDebugLine(
			World,
			ViewLocation,
			TraceEnd,
			TraceColor,
			false,
			0.15f,
			0,
			1.5f);

		if (bHit)
		{
			DrawDebugSphere(
				World,
				HitResult.ImpactPoint,
				8.0f,
				12,
				TraceColor,
				false,
				0.15f);
		}
	}
#endif

	return bCanInteract ? HitActor : nullptr;
}

void UPlayerInteractionComponent::UpdateInteractionTarget()
{
	AActor* NewTarget = FindInteractableActor(false);
	if (CurrentInteractable == NewTarget)
	{
		return;
	}

	CurrentInteractable = NewTarget;
	if (!IsValid(InteractionWidget))
	{
		return;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(CurrentInteractable) || !IsValid(OwnerPawn))
	{
		InteractionWidget->HideInteractionPrompt();
		return;
	}

	const FText ItemName = IInteractionInterface::Execute_GetInteractionText(
		CurrentInteractable,
		OwnerPawn);
	InteractionWidget->ShowInteractionPrompt(ItemName);
}

void UPlayerInteractionComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateInteractionTarget();
}

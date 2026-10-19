// Fill out your copyright notice in the Description page of Project Settings.


#include "InteractionActorBase.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"

// Sets default values
AInteractionActorBase::AInteractionActorBase()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root"));
	// RootComponent = SceneRoot;
	// 이 버전이 더 권장되는 설정
	SetRootComponent(SceneRoot);

	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
	ItemMesh->SetupAttachment(SceneRoot);
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	
	InteractionCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("Interaction Collision"));
	InteractionCollision->SetupAttachment(SceneRoot);
	InteractionCollision->SetBoxExtent(FVector(50.0f));
	
	// 상호작용 라인 트레이스는 Visibility 채널만 감지하도록 설정한다.
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	
	InteractionText = NSLOCTEXT(
		"Interaction",
		"DefaultInteractionText",
		"상호작용"
		);
}

FText AInteractionActorBase::GetInteractionText_Implementation(APawn* Interactor) const
{
	return InteractionText;
}

bool AInteractionActorBase::CanInteract_Implementation(APawn* Interactor) const
{
	return bInteractionEnabled && IsValid(Interactor);
}

void AInteractionActorBase::Interact_Implementation(APawn* Interactor)
{
	// BP 재정의를 포함한 실제 CanInteract 구현을 호출한다.
	if (!IInteractionInterface::Execute_CanInteract(this, Interactor))
	{
		return;
	}
	
	HandleInteraction(Interactor);
}

void AInteractionActorBase::HandleInteraction(APawn* Interactor)
{
	// 현재 단계에서 상호작용 성공 여부를 확인하기 위한 기본 동작
	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s interacted with %s"),
		*GetNameSafe(Interactor),
		*GetNameSafe(this));
}
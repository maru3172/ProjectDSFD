#include "ProjectProject01Checkpoint.h"

#include "PlayerCharacter.h"
#include "ProjectProject01SaveSubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

AProjectProject01Checkpoint::AProjectProject01Checkpoint()
{
	PrimaryActorTick.bCanEverTick = false;
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("CheckpointTrigger"));
	SetRootComponent(Trigger);
	Trigger->SetBoxExtent(FVector(100.0f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AProjectProject01Checkpoint::HandleBeginOverlap);
}

void AProjectProject01Checkpoint::HandleBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const int32 OtherBodyIndex,
	const bool bFromSweep,
	const FHitResult& SweepResult)
{
	UWorld* World = GetWorld();
	APlayerCharacter* Player = Cast<APlayerCharacter>(OtherActor);
	if ((bSaveOnlyOncePerSession && bTriggered) || !IsValid(World) || World->GetNetMode() != NM_Standalone ||
		!IsValid(Player) || !Player->IsLocallyControlled())
	{
		return;
	}
	if (UGameInstance* GameInstance = World->GetGameInstance(); IsValid(GameInstance))
	{
		if (UProjectProject01SaveSubsystem* Saves = GameInstance->GetSubsystem<UProjectProject01SaveSubsystem>())
		{
			// 저장 요청 자체가 접수된 경우에만 1회용 체크포인트를 소모한다.
			// 중복 저장 등으로 요청이 거부되면 다음 Overlap에서 다시 시도할 수 있다.
			bTriggered = Saves->SaveCheckpoint(Player, CheckpointId);
		}
	}
}

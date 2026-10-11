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

FName AProjectProject01Checkpoint::GetCheckpointSaveId_Implementation() const
{
	return FName(*FString::Printf(TEXT("Checkpoint:%s:%s"),
		*CheckpointId.ToString(), *GetFName().ToString()));
}

void AProjectProject01Checkpoint::OnCheckpointStateRestored_Implementation()
{
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
			// 스냅샷 안에도 이미 통과한 체크포인트로 기록되도록 저장 요청 전에 상태를 바꾼다.
			const bool bWasTriggered = bTriggered;
			bTriggered = true;
			if (!Saves->SaveCheckpoint(Player, CheckpointId, true))
			{
				// 중복 저장 등으로 요청이 거부되면 다음 Overlap에서 다시 시도할 수 있다.
				bTriggered = bWasTriggered;
			}
		}
	}
}

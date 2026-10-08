// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimNotify_MannequinFootstep.h"

#include "MannequinAICharacter.h"

void UAnimNotify_MannequinFootstep::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	
	if (!IsValid(MeshComp))
	{
		return;
	}
	
	AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(MeshComp->GetOwner());
	
	if (!IsValid(Mannequin))
	{
		return;
	}
	
	Mannequin->PlayFootstep(FootBoneName);
}

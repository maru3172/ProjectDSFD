// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_MannequinFootstep.generated.h"

/**
 * 
 */
UCLASS(meta = (DisplayName = "Mannquin Footstep"))
class PROJECTPROJECT01_API UAnimNotify_MannequinFootstep : public UAnimNotify
{
	GENERATED_BODY()
	
public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	
	// foot_l 또는 foot_r
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep")
	FName FootBoneName;
};

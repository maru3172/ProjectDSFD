// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractionPromptWidget.generated.h"

class UTextBlock;

/**
 * 
 */
UCLASS()
class PROJECTPROJECT01_API UInteractionPromptWidget : public UUserWidget
{
	GENERATED_BODY()
	
public:
	// 상호작용 가능한 대상의 이름을 표시한다.
	void ShowInteractionPrompt(const FText& ItemName);
	
	// 현재 표시 중인 상호작용 안내를 숨긴다.
	void HideInteractionPrompt();
	
protected:
	// 자식 Widget Blueprint 에서 같은 이름의 TextBlock 과 연결된다.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemNameText;
};

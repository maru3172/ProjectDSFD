// Fill out your copyright notice in the Description page of Project Settings.


#include "InteractionPromptWidget.h"

#include "Components/TextBlock.h"


void UInteractionPromptWidget::ShowInteractionPrompt(const FText& ItemName)
{
	if (IsValid(ItemNameText))
	{
		ItemNameText->SetText(ItemName);
	}
	
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractionPromptWidget::HideInteractionPrompt()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

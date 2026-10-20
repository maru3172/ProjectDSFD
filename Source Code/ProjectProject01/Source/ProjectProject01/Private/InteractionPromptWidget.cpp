// Fill out your copyright notice in the Description page of Project Settings.


#include "InteractionPromptWidget.h"
#include "ProjectDFSDUITheme.h"

#include "Components/TextBlock.h"


void UInteractionPromptWidget::ShowInteractionPrompt(const FText& ItemName)
{
	if (IsValid(ItemNameText))
	{
		ItemNameText->SetText(ItemName);
		ItemNameText->SetColorAndOpacity(ProjectDFSDUI::Text());
		ItemNameText->SetShadowColorAndOpacity(FLinearColor(0,0,0,.85f));
		ItemNameText->SetShadowOffset(FVector2D(0,1));
	}
	
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractionPromptWidget::HideInteractionPrompt()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

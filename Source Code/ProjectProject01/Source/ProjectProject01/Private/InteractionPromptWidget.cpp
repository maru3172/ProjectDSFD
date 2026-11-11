// Fill out your copyright notice in the Description page of Project Settings.


#include "InteractionPromptWidget.h"
#include "ProjectDFSDUITheme.h"

#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "ProjectGamepad.h"


void UInteractionPromptWidget::ShowInteractionPrompt(const FText& ItemName)
{
	if (IsValid(ItemNameText))
	{
		// Texts that name the key ("F 키: …") name the pad button while a pad is in use.
		const UProjectGamepadSubsystem* PadForText = UProjectGamepadSubsystem::Get(this);
		ItemNameText->SetText(PadForText && PadForText->IsUsingGamepad()
			? FText::FromString(ItemName.ToString().Replace(TEXT("F 키"), *PadForText->PadLabel(EProjectPadAction::Interact))) : ItemName);
		ItemNameText->SetColorAndOpacity(ProjectDFSDUI::Text());
		ItemNameText->SetShadowColorAndOpacity(FLinearColor(0,0,0,.85f));
		ItemNameText->SetShadowOffset(FVector2D(0,1));
	}
	// The key letter is a fixed text block in the widget Blueprint ("F"): show the pad button instead while a pad is in use
	// (Docs/Gamepad.md 6).
	if (!KeyText.IsValid() && WidgetTree)
		WidgetTree->ForEachWidget([this](UWidget* Widget)
		{
			if (UTextBlock* Text = Cast<UTextBlock>(Widget); Text && Text != ItemNameText && Text->GetText().ToString().TrimStartAndEnd() == TEXT("F")) KeyText = Text;
		});
	if (UTextBlock* Key = KeyText.Get())
	{
		const UProjectGamepadSubsystem* Pad = UProjectGamepadSubsystem::Get(this);
		Key->SetText(FText::FromString(Pad && Pad->IsUsingGamepad() ? Pad->PadLabel(EProjectPadAction::Interact) : FString(TEXT("F"))));
	}
	
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInteractionPromptWidget::HideInteractionPrompt()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

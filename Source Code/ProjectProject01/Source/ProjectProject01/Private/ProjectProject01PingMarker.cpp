#include "ProjectProject01PingMarker.h"

#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"

AProjectProject01PingMarker::AProjectProject01PingMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	InitialLifeSpan = 4.0f;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	Text = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PingText"));
	Text->SetupAttachment(SceneRoot);
	Text->SetHorizontalAlignment(EHTA_Center);
	Text->SetVerticalAlignment(EVRTA_TextCenter);
	Text->SetWorldSize(42.0f);
	Text->SetTextRenderColor(FColor::White);
	Text->SetText(FText::FromString(TEXT("PING")));
}

void AProjectProject01PingMarker::Configure(const EProjectProject01PingType Type, const FString& SenderName)
{
	FString Label;
	FColor Color;
	switch (Type)
	{
	case EProjectProject01PingType::Help:
		Label = TEXT("도움 요청"); Color = FColor(40, 160, 255); break;
	case EProjectProject01PingType::Danger:
		Label = TEXT("위험!"); Color = FColor(255, 55, 30); break;
	default:
		Label = TEXT("위치 표시"); Color = FColor(255, 220, 40); break;
	}
	Text->SetText(FText::FromString(SenderName.IsEmpty() ? Label : FString::Printf(TEXT("%s\n%s"), *Label, *SenderName)));
	Text->SetTextRenderColor(Color);
}

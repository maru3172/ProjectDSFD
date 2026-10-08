#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectProject01PingMarker.generated.h"

UCLASS(NotBlueprintable)
class PROJECTPROJECT01_API AProjectProject01PingMarker final : public AActor
{
	GENERATED_BODY()

public:
	AProjectProject01PingMarker();
	void Configure(EProjectProject01PingType Type, const FString& SenderName);

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UTextRenderComponent> Text;
};

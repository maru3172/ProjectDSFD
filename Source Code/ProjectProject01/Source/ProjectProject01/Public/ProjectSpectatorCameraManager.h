#pragma once
#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "ProjectSpectatorCameraManager.generated.h"

/** Body-relative spectator mount with independent free look; never hides the target. */
UCLASS()
class PROJECTPROJECT01_API AProjectSpectatorCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()
protected:
	virtual void UpdateViewTargetInternal(FTViewTarget& OutVT,float DeltaTime) override;
	UPROPERTY(EditDefaultsOnly,Category="Spectator",meta=(ClampMin="0",ClampMax="150",Units="cm"))
	float SpectatorBackOffsetCentimeters=90.f;
	/** Clear the target's head when looking forward, rather than exchanging a face for a close-up nape. */
	UPROPERTY(EditDefaultsOnly,Category="Spectator",meta=(ClampMin="0",ClampMax="60",Units="cm"))
	float SpectatorHeightOffsetCentimeters=40.f;
};

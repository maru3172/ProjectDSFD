#pragma once
#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "ProjectSpectatorCameraManager.generated.h"

/** Same first-person position and local feedback as the target, with independent spectator aim. */
UCLASS()
class PROJECTPROJECT01_API AProjectSpectatorCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()
protected:
	virtual void UpdateViewTargetInternal(FTViewTarget& OutVT,float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	TWeakObjectPtr<class AActor> HiddenObservedTarget;
	bool bAddedHiddenTarget=false;
};

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "MannequinTrackingTypes.h"

#include "MannequinTrackingCoordinator.generated.h"

class AMannequinAICharacter;

UCLASS()
class PROJECTPROJECT01_API UMannequinTrackingCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 중앙 관리 대상에 마네킹을 등록한다.
	void RegisterMannequin(
		AMannequinAICharacter& Mannequin
		);

	// 제거되었거나 유효하지 않은 마네킹을 목록에서 정리한다.
	void RemoveInvalidMannequins();

	// 지정한 역할에 해당하는 마네킹들을 반환한다.
	void GetMannequinsByTier(
		ETwoStageMannequinTier Tier,
		TArray<AMannequinAICharacter*>& OutMannequins
		) const;

private:
	// 중앙 추적 관리에 등록된 마네킹 목록
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AMannequinAICharacter>> RegisteredMannequins;
};

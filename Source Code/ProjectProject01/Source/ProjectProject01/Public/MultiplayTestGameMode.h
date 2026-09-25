// File: Source/ProjectProject01/Public/MultiplayTestGameMode.h
// Build target: ProjectProject01Server / ProjectProject01

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MultiplayTestGameMode.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogProjectProject01Multiplayer, Log, All);
struct FMannequinAITuningRow;

/**
 * MultiplayTest 전용 게임 모드입니다.
 * 기존 AProjectPracticeGameModeBase를 상속하지 않으므로 조력자를 자동 생성하지 않습니다.
 */
UCLASS()
class PROJECTPROJECT01_API AMultiplayTestGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMultiplayTestGameMode();
	virtual void BeginPlay() override;

	/** 서버가 DataTable의 검증된 생존자 시야 수치를 적용한다. */
	void ApplyMannequinTuning(const FMannequinAITuningRow& Tuning);

	bool TryPossessMannequin(class AMultiplayTestPlayerController* RequestingController, int32 Slot);
	bool TryEnableMannequinManualControl(class AMultiplayTestPlayerController* RequestingController);
	bool TryQueuePostPossessionChaseCommand(class AMultiplayTestPlayerController* RequestingController);

	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	virtual bool MustSpectate_Implementation(APlayerController* NewPlayerController) const override;

private:
	class AMannequinAICharacter* FindMannequinBySlot(int32 Slot) const;
	void QueueDefaultAIControllerRestore(class AMannequinAICharacter* Mannequin) const;
	bool IsMannequinController(const class AMultiplayTestPlayerController* Controller) const;
	bool IsSurvivorController(const APlayerController* Controller) const;
	bool CanSurvivorSeeMannequin(const APlayerController* SurvivorController,
		const class AMannequinAICharacter* Mannequin) const;
	void UpdateSurvivorVisionFrozenStates();

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Survivor Vision",
		meta = (ClampMin = "0.01", UIMin = "0.01", Units = "s"))
	float SurvivorVisionCheckIntervalSeconds = 0.05f;

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer|Survivor Vision",
		meta = (ClampMin = "1.0", ClampMax = "89.0", UIMin = "1.0", UIMax = "89.0", Units = "deg"))
	float SurvivorVisionHalfAngleDegrees = 55.0f;

	float SurvivorVisionCheckAccumulatorSeconds = 0.0f;

	// 프로토타입 규칙: 서버에 먼저 접속한 한 명만 마네킹을 조종한다.
	TWeakObjectPtr<class AMultiplayTestPlayerController> MannequinController;
};

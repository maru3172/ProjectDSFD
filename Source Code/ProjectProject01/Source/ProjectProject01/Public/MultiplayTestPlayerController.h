// File: Source/ProjectProject01/Public/MultiplayTestPlayerController.h
// Build target: ProjectProject01Server / ProjectProject01

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MultiplayTestPlayerController.generated.h"

/** 마네킹 슬롯 선택 입력을 서버에 요청하는 MultiplayTest 전용 컨트롤러입니다. */
UCLASS()
class PROJECTPROJECT01_API AMultiplayTestPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	class AMannequinAICharacter* GetViewedMannequin() const;
	void SetViewedMannequin(class AMannequinAICharacter* Mannequin);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void SetupInputComponent() override;
	virtual void OnRep_Pawn() override;

private:
	void SelectMannequinSlot0();
	void SelectMannequinSlot1();
	void SelectMannequinSlot2();
	void SelectMannequinSlot3();
	void SelectMannequinSlot4();
	void SelectMannequinSlot5();
	void SelectMannequinSlot6();
	void SelectMannequinSlot7();
	void SelectMannequinSlot8();
	void SelectMannequinSlot9();
	void RequestMannequinSlot(int32 Slot);
	void RequestMannequinManualControl();
	void RequestPostPossessionChaseCommand();

	UFUNCTION(Server, Reliable)
	void ServerRequestMannequinSlot(int32 Slot);

	UFUNCTION(Server, Reliable)
	void ServerRequestMannequinManualControl();

	UFUNCTION(Server, Reliable)
	void ServerRequestPostPossessionChaseCommand();

	UFUNCTION(Client, Reliable)
	void ClientApplyViewedMannequin(class AMannequinAICharacter* Mannequin);

	UFUNCTION()
	void OnRep_ViewedMannequin();

	void ApplyViewedMannequinCamera(class AMannequinAICharacter* Mannequin);

	/** 소유 클라이언트가 Pawn 복제 이후에도 다시 적용할 수 있는 마지막 선택 시점 대상입니다. */
	UPROPERTY(ReplicatedUsing = OnRep_ViewedMannequin)
	TObjectPtr<class AMannequinAICharacter> ViewedMannequin;
};

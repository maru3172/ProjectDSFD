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

protected:
	virtual void SetupInputComponent() override;

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

	UFUNCTION(Server, Reliable)
	void ServerRequestMannequinSlot(int32 Slot);
};

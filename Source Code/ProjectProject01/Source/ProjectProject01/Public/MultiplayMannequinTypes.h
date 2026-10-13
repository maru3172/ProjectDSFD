// File: Source/ProjectProject01/Public/MultiplayMannequinTypes.h

#pragma once

#include "CoreMinimal.h"
#include "MultiplayMannequinTypes.generated.h"

/**
 * MultiplayTest에서만 사용하는 영구 마네킹 명령입니다.
 * 레벨이 바뀌면 액터와 함께 초기화되며, 같은 레벨 안에서는 다른 명령을 받을 때까지 유지됩니다.
 */
UENUM(BlueprintType)
enum class EMultiplayMannequinCommand : uint8
{
	Patrol UMETA(DisplayName = "정찰 및 자동 추격"),
	ChaseInDetectionRange UMETA(DisplayName = "감지 범위 내 추격"),
	FollowMannequinPlayer UMETA(DisplayName = "마네킹 플레이어 추종"),
	HoldPosition UMETA(DisplayName = "정지")
};


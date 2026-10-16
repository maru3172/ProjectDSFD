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

/** 서버가 판정한 각 마네킹의 실제 현재 행동입니다. */
UENUM(BlueprintType)
enum class EMultiplayMannequinActivity : uint8
{
	Patrolling UMETA(DisplayName = "정찰"),
	Observing UMETA(DisplayName = "주변 동향 관찰"),
	Chasing UMETA(DisplayName = "생존자 추격"),
	Following UMETA(DisplayName = "마네킹 플레이어 추종"),
	Holding UMETA(DisplayName = "정지"),
	ManualControl UMETA(DisplayName = "직접 조작"),
	FrozenByVision UMETA(DisplayName = "생존자 시야로 정지"),
	Searching UMETA(DisplayName = "마지막 목격 지점 수색"),
	Disguised UMETA(DisplayName = "전시물 위장")
};

/** Server-only remembered observation. Hidden targets never update LastSeenLocation. */
struct FMultiplayMannequinSearchState
{
	TWeakObjectPtr<class APlayerCharacter> Target;
	FVector LastSeenLocation = FVector::ZeroVector;
	double StartedSeconds = -1.0;
	double ArrivedSeconds = -1.0;
	float ObservationYaw = 0.0f;
};

/** Server-only presentation state, not a new command or detection source. */
struct FMultiplayMannequinDisguiseState
{
	float RemainingSeconds = 0.0f;
	double NextAllowedSeconds = 0.0;
	bool bWasObserved = false;
	bool bApproaching = false;
	FVector Destination = FVector::ZeroVector;
	float FacingYaw = 0.0f;
	float ApproachSeconds = 0.0f;

	void Cancel()
	{
		RemainingSeconds=0;
		bApproaching=false;
		bWasObserved=false;
		ApproachSeconds=0;
	}

	bool Advance(float DeltaSeconds)
	{
		RemainingSeconds = bWasObserved ? 0.0f : FMath::Max(0.0f, RemainingSeconds - FMath::Max(0.0f, DeltaSeconds));
		return RemainingSeconds > 0.0f;
	}
};


#pragma once

#include "CoreMinimal.h"
#include "MannequinTrackingTypes.generated.h"

class AMannequinAICharacter;

UENUM()
enum class ETwoStageMannequinTier : uint8
{
	// 유효한 추적 역할이나 수색 목표가 없는 기본 배회 상태
	Roaming = 0,

	// 현재 플레이어를 직접 관측하고 있는 1 차 마네킹
	Primary = 1,

	// 플레이어 시야로 정지했지만 플레이어를 직접 보지 못하는 2 차 마네킹
	Secondary = 2,

	// 전달받은 위치로 이동해 실제 수색을 수행하는 3 차 마네킹
	Tertiary = 3
};

UENUM()
enum class EMannequinTrackingEvidence : uint8
{
	None = 0,
	DirectObservation = 1,
	StoppedMannequinClue = 2,
	VelocityPrediction = 3
};

UENUM()
enum class EMannequinTrackingInformationType : uint8
{
	None = 0,
	DirectObservation = 1,
	LastObservation = 2,
	IndirectPrediction = 3
};

UENUM()
enum class EMannequinStopReason : uint8
{
	None = 0,
	PlayerSight = 1,
	Command = 2,
	NavigationFailure = 3,
	Animation = 4,
	HitReaction = 5,
	Waiting = 6,
	Scripted = 7,
	SystemForced = 8
};

USTRUCT(BlueprintType)
struct FMannequinPlayerObservation
{
	GENERATED_BODY()
	
	// 플레이어를 마지막으로 직접 확인한 위치
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	FVector LastSeenLocation = FVector::ZeroVector;
	
	// 플레이어를 마지막으로 직접 확인했을 때의 속도
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	FVector LastSeenVelocity = FVector::ZeroVector;
	
	// 마지막 관측 정보를 이용해 계산한 플레이어 예상 위치
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	FVector PredictedLocation = FVector::ZeroVector;
	
	// 플레이어를 마지막으로 직접 확인한 월드 시각
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	double LastSeenTime = -1.0;
	
	// 현재 관측 정보의 신뢰도
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	float Confidence = 0.0f;
	
	// 유효한 플레이어 관측 정보를 가지고 있는지
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	bool bHasObservation = false;
	
	// 현재 플레이어를 직접 보고 있는지
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Tracking")
	bool bDirectlyVisible = false;
};

USTRUCT()
struct FMannequinSharedTrackingInformation
{
	GENERATED_BODY()
	
	// 현재 정보의 종류
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	EMannequinTrackingInformationType InformationType = EMannequinTrackingInformationType::None;
	
	// 정보가 나타내는 플레이어 위치
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	FVector PlayerLocation = FVector::ZeroVector;
	
	// 정보를 생성했을 때 확인한 플레이어 속도
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	FVector PlayerVelocity = FVector::ZeroVector;
	
	// 정보가 생성된 월드 시각
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	double ObservationTime = -1.0;
	
	// 이 정보가 만료되는 월드 시각
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	double ExpirationTime = -1.0;
	
	// 현재 정보의 신뢰도
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	float Confidence = 0.0f;
	
	// 이 정보를 생성한 마네킹
	UPROPERTY(Transient, VisibleAnywhere, Category = "AI|Tracking")
	TWeakObjectPtr<AMannequinAICharacter> SourceMannequin;
	
	// 중앙 관리자가 발급한 정보 고유 번호
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	int32 InformationId = INDEX_NONE;
	
	// 현재 시각을 기준으로 사용할 수 있는 정보인지 반환한다.
	bool IsActive(double CurrentTime) const
	{
		return
			InformationType != EMannequinTrackingInformationType::None &&
			SourceMannequin.IsValid() &&
			Confidence > 0.0f &&
			ExpirationTime >= CurrentTime &&
			InformationId != INDEX_NONE;
	}
};

USTRUCT()
struct FMannequinSecondaryClueGroup
{
	GENERATED_BODY()
	
	// 이 그룹에 포함된 플레이어 시야 정지 마네킹들
	UPROPERTY(Transient, VisibleAnywhere, Category = "AI|Tracking")
	TArray<TWeakObjectPtr<AMannequinAICharacter>> ClueMannequins;
	
	// 그룹에 포함된 마네킹 위치의 평균
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	FVector CenterLocation = FVector::ZeroVector;
	
	// 그룹에서 가장 먼저 플레이어 시야 정지가 시작된 시각
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	double EarliestStopTime = -1.0;
	
	// 그룹에서 가장 늦게 플레이어 시야 정지가 시작된 시각
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	double LatestStopTime = -1.0;
};

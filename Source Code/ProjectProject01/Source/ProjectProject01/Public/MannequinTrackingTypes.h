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

UENUM()
enum class EMannequinTertiarySearchResult : uint8
{
	// 아직 탐색을 시작하지 않았거나 기록할 결과가 없는 상태
	None = 0,

	// 배정된 목표에 도착하여 주변을 탐색하고 있는 상태
	Searching = 1,

	// 탐색 중 플레이어를 직접 발견한 상태
	PlayerFound = 2,

	// 제한 시간 동안 플레이어를 발견하지 못한 상태
	Failed = 3
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

USTRUCT()
struct FMannequinSearchCandidate
{
	GENERATED_BODY()
	
	// NavMesh 위로 보정된 예상 플레이어 위치
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	FVector Location = FVector::ZeroVector;
	
	// 이 후보를 생성할 때 기준으로 사용한 2 차 단서 그룹 번호
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	int32 SecondaryGroupIndex = INDEX_NONE;
	
	// 이 후보를 생성할 때 사용한 마지막 관측 정보 번호
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	int32 ReferenceInformationId = INDEX_NONE;
	
	// 후보 생성 시점의 마지막 관측 정보 신뢰도
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	float Confidence = 0.0f;
	
	// 이 후보 위치로 설명할 수 있는 2 차 단서 마네킹 수
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	int32 ExplainedClueCount = 0;
	
	// 거리·이동 방향·2 차 단서 수를 조합한 최종 후보 점수
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	float Score = 0.0f;
};

USTRUCT()
struct FMannequinTertiaryAssignment
{
	GENERATED_BODY()
	
	// 3 차 마네킹이 이동할 NavMesh 위의 탐색 위치
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	FVector TargetLocation = FVector::ZeroVector;
	
	// 중앙 관리자가 발급한 배정 고유 번호
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	int32 AssignmentId = INDEX_NONE;
	
	// 배정 위치를 결정할 때 사용한 공유 추적 정보 번호
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	int32 ReferenceInformationId = INDEX_NONE;
	
	// 배정 위치로 선택된 중앙 탐색 후보 번호
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	int32 SearchCandidateIndex = INDEX_NONE;
	
	// 배정 후보가 설명하는 2 차 단서 그룹 번호
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	int32 SecondaryGroupIndex = INDEX_NONE;
	
	// 중앙 관리자가 이 배정을 생성한 월드 시각
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	double AssignmentTime = -1.0;
	
	// 이 배정을 더 이상 사용할 수 없는 월드 시각
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	double ExpirationTime = -1.0;
	
	// 배정에 사용된 추적 근거
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	EMannequinTrackingEvidence TrackingEvidence = EMannequinTrackingEvidence::None;
	
	// 배정 위치에 대한 중앙 추적 정보의 신뢰도
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	float Confidence = 0.0f;
	
	// 중앙 관리자로부터 유효한 탐색 위치를 배정받았는지
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	bool bHasAssignment = false;
	
	// 현재 시각을 기준으로 사용할 수 있는 3 차 탐색 배정인지 반환한다.
	bool IsActive(double CurrentTime) const
	{
		return
			bHasAssignment &&
			AssignmentId != INDEX_NONE &&
			ReferenceInformationId != INDEX_NONE &&
			TrackingEvidence != EMannequinTrackingEvidence::None &&
			Confidence > 0.0f &&
			ExpirationTime >= CurrentTime;
	}
};

USTRUCT()
struct FMannequinTertiarySearchState
{
	GENERATED_BODY()
	
	// 이 탐색 상태가 대상으로 하는 3 차 배정 번호
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	int32 AssignmentId = INDEX_NONE;
	
	// 현재 탐색 진행 또는 완료 결과
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	EMannequinTertiarySearchResult Result = EMannequinTertiarySearchResult::None;
	
	// 목표 위치에 도착하여 주변 탐색을 시작한 월드 시각
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	double SearchStartTime = -1.0;
	
	// 플레이어 발견 또는 탐색 실패가 확정된 월드 시각
	UPROPERTY(VisibleAnywhere, Category = "AI|Tracking")
	double CompletionTime = -1.0;
	
	// 지정한 배정에 대해 현재 주변 탐색이 진행 중인지 반환한다.
	bool IsSearchingAssignment(int32 InAssignmentId) const
	{
		return
			AssignmentId == InAssignmentId &&
			AssignmentId != INDEX_NONE &&
			Result == EMannequinTertiarySearchResult::Searching;
	}
	
	// 지정한 배정에 대해 중앙 관리자가 처리할 완료 결과가 있는지 반환한다.
	bool HasCompletedAssignment(int32 InAssignmentId) const
	{
		return
			AssignmentId == InAssignmentId &&
			AssignmentId != INDEX_NONE &&
			(Result == EMannequinTertiarySearchResult::PlayerFound ||
			 Result == EMannequinTertiarySearchResult::Failed) &&
			CompletionTime >= 0.0;
	}
};

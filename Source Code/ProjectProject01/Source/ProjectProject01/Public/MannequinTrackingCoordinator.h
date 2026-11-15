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

	// 등록된 마네킹들의 현재 관측 정보를 중앙 상태에 반영한다.
	void RefreshTrackingInformation(
		float InformationLifetime
		);

	// 현재 직접 관측 중인 가장 최신 정보를 반환한다.
	bool TryGetLatestDirectInformation(
		FMannequinSharedTrackingInformation& OutInformation
		) const;

	// 직접 시야를 잃은 뒤에도 유효한 마지막 관측 정보를 반환한다.
	bool TryGetLastKnownInformation(
		FMannequinSharedTrackingInformation& OutInformation
		) const;

	// 현재 유효한 2 차 마네킹을 수집하여 시간·거리·높이 조건으로 그룹화한다.
	void RefreshSecondaryClueGroups(
		float ClueLifetime,
		float MaximumGroupTimeDifference,
		float MaximumGroupDistance,
		float MaximumHeightDifference
		);

	// 현재 계산된 2 차 단서 그룹들을 반환한다.
	const TArray<FMannequinSecondaryClueGroup>& GetSecondaryClueGroups() const
	{
		return SecondaryClueGroups;
	}

	// 마지막 관측 정보와 2 차 단서 그룹을 이용해 NavMesh 위의 수색 후보들을 생성한다.
	void RefreshIndirectSearchCandidates(
		float RefreshInterval,
		float CandidateSpacing,
		int32 CandidateDirectionCount,
		float EstimatedMaximumSpeed,
		float MaximumSearchRadius,
		float MaximumPredictionSeconds,
		const FVector& NavigationExtent
		);

	// 현재 생성된 예상 위치 후보들을 반환한다.
	const TArray<FMannequinSearchCandidate>& GetIndirectSearchCandidates() const
	{
		return IndirectSearchCandidates;
	}

	// 생성된 후보들 중 이동 가능 범위와 2 차 단서 시야 조건을 통과한 후보만 유지한다.
	void FilterIndirectSearchCandidates(
		float EstimatedMaximumSpeed,
		float MaximumSearchRadius,
		float SecondaryClueRadius,
		float PlayerClueSightHalfAngle,
		float MannequinSightRadius,
		float MannequinSightHalfAngle,
		float MannequinSightMaximumHeightDifference,
		const FVector& NavigationExtent
		);

	// 필터를 통과한 후보들의 점수를 계산하고 가장 좋은 간접 예상 정보를 갱신한다.
	void EvaluateIndirectSearchCandidates(
		float MaximumPredictionSeconds,
		float MaximumSearchRadius,
		float DistanceScoreWeight,
		float DirectionScoreWeight,
		float ClueScoreWeight
		);

	// 현재 가장 높은 점수를 가진 간접 예상 정보를 반환한다.
	bool TryGetBestIndirectInformation(
		FMannequinSharedTrackingInformation& OutInformation
		) const;

private:
	// 후보 위치에서 지정한 2 차 단서 그룹을 설명할 수 있는지 검사한다.
	bool DoesCandidateExplainSecondaryGroup(
		const FVector& CandidateLocation,
		const FMannequinSecondaryClueGroup& Group,
		float SecondaryClueRadius,
		float PlayerClueSightHalfAngle,
		float MannequinSightRadius,
		float MannequinSightHalfAngle,
		float MannequinSightMaximumHeightDifference
		) const;

	// 후보 위치에서 2 차 마네킹까지 벽에 막히지 않았는지 검사한다.
	bool CanCandidateSeeSecondaryClue(
		const FVector& CandidateLocation,
		const AMannequinAICharacter& SecondaryMannequin
		) const;

	// 2 차 마네킹이 후보 위치를 직접 볼 수 있는지 검사한다.
	bool CanSecondaryClueSeeCandidate(
		const AMannequinAICharacter& SecondaryMannequin,
		const FVector& CandidateLocation,
		float MannequinSightRadius,
		float MannequinSightHalfAngle,
		float MannequinSightMaximumHeightDifference
		) const;

	// 중앙 추적 관리에 등록된 마네킹 목록
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AMannequinAICharacter>> RegisteredMannequins;

	// 현재 직접 관측 중인 마네킹 중 가장 최신 정보
	UPROPERTY(Transient)
	FMannequinSharedTrackingInformation LatestDirectInformation;

	// 직접 시야를 잃은 뒤 유지하는 마지막 관측 정보
	UPROPERTY(Transient)
	FMannequinSharedTrackingInformation LastKnownInformation;

	// 현재 유효한 2 차 단서들을 시간·거리·높이 조건으로 묶은 그룹들
	UPROPERTY(Transient)
	TArray<FMannequinSecondaryClueGroup> SecondaryClueGroups;

	// 마지막 관측 정보와 2 차 단서를 이용해 생성한 NavMesh 수색 후보들
	UPROPERTY(Transient)
	TArray<FMannequinSearchCandidate> IndirectSearchCandidates;

	// 현재 후보 중 가장 높은 점수를 가진 간접 예상 정보
	UPROPERTY(Transient)
	FMannequinSharedTrackingInformation BestIndirectInformation;

	// 다음 관측 정보에 부여할 번호
	int32 NextInformationId = 1;

	// 같은 프레임에 여러 서비스가 중복 계산하는 것을 막기 위한 시각
	double LastRefreshTime = -1.0;

	// 같은 프레임에 2 차 단서 그룹을 중복 계산하는 것을 막기 위한 시각
	double LastSecondaryClueRefreshTime = -1.0;

	// 간접 수색 후보를 마지막으로 계산한 월드 시각
	double LastIndirectCandidateRefreshTime = -1.0;

	// 간접 수색 후보를 마지막으로 필터링한 월드 시각
	double LastIndirectCandidateFilterTime = -1.0;

	// 간접 수색 후보 점수를 마지막으로 계산한 월드 시각
	double LastIndirectCandidateEvaluationTime = -1.0;
};

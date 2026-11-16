#include "MannequinTrackingCoordinator.h"

#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "NavigationSystem.h"

#include "MannequinAICharacter.h"

void UMannequinTrackingCoordinator::RegisterMannequin(AMannequinAICharacter& Mannequin)
{
	const TWeakObjectPtr<AMannequinAICharacter> MannequinPointer(&Mannequin);

	if (!RegisteredMannequins.Contains(MannequinPointer))
	{
		RegisteredMannequins.Add(MannequinPointer);

		// 새 마네킹이 추가된 프레임에는 중앙 정보와 2 차 단서 그룹을 다시 계산한다.
		LastRefreshTime = -1.0;
		LastSecondaryClueRefreshTime = -1.0;
		LastIndirectCandidateRefreshTime = -1.0;
		LastIndirectCandidateFilterTime = -1.0;
		LastIndirectCandidateEvaluationTime = -1.0;
		LastTertiaryAssignmentRefreshTime = -1.0;
	}
}

void UMannequinTrackingCoordinator::RemoveInvalidMannequins()
{
	RegisteredMannequins.RemoveAll(
		[](const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer)
		{
			return !MannequinPointer.IsValid();
		}
		);
}

void UMannequinTrackingCoordinator::GetMannequinsByTier(ETwoStageMannequinTier Tier, TArray<AMannequinAICharacter*>& OutMannequins) const
{
	OutMannequins.Reset();

	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : RegisteredMannequins)
	{
		AMannequinAICharacter* Mannequin = MannequinPointer.Get();

		if (!IsValid(Mannequin))
		{
			continue;
		}

		if (Mannequin->GetTrackingTier() != Tier)
		{
			continue;
		}

		OutMannequins.Add(Mannequin);
	}
}

void UMannequinTrackingCoordinator::RefreshTrackingInformation(float InformationLifetime)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();

	// 같은 프레임에 여러 마네킹 서비스가 호출하더라도 한 번만 계산한다.
	if (FMath::IsNearlyEqual(CurrentTime, LastRefreshTime))
	{
		return;
	}

	LastRefreshTime = CurrentTime;
	RemoveInvalidMannequins();

	AMannequinAICharacter* BestMannequin = nullptr;
	const FMannequinPlayerObservation* BestObservation = nullptr;
	double NewestObservationTime = -1.0;
	double ClosestObserverDistanceSquared = TNumericLimits<double>::Max();

	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : RegisteredMannequins)
	{
		AMannequinAICharacter* Mannequin = MannequinPointer.Get();

		if (!IsValid(Mannequin))
		{
			continue;
		}

		const FMannequinPlayerObservation& Observation = Mannequin->GetPlayerObservation();

		if (!Observation.bHasObservation || !Observation.bDirectlyVisible || Observation.Confidence <= 0.0f)
		{
			continue;
		}

		const double DistanceToPlayerSquared = FVector::DistSquared(
			Mannequin->GetActorLocation(),
			Observation.LastSeenLocation
			);

		const bool bNewerObservation = Observation.LastSeenTime > NewestObservationTime;
		const bool bSameObservationTime = FMath::IsNearlyEqual(Observation.LastSeenTime, NewestObservationTime);
		const bool bCloserObserver = DistanceToPlayerSquared < ClosestObserverDistanceSquared;

		if (!IsValid(BestMannequin) || bNewerObservation || (bSameObservationTime && bCloserObserver))
		{
			BestMannequin = Mannequin;
			BestObservation = &Observation;
			NewestObservationTime = Observation.LastSeenTime;
			ClosestObserverDistanceSquared = DistanceToPlayerSquared;
		}
	}

	if (IsValid(BestMannequin) && BestObservation != nullptr)
	{
		FMannequinSharedTrackingInformation NewInformation;
		NewInformation.InformationType = EMannequinTrackingInformationType::DirectObservation;
		NewInformation.PlayerLocation = BestObservation->LastSeenLocation;
		NewInformation.PlayerVelocity = BestObservation->LastSeenVelocity;
		NewInformation.ObservationTime = BestObservation->LastSeenTime;
		NewInformation.ExpirationTime =
			BestObservation->LastSeenTime +
			static_cast<double>(FMath::Max(0.0f, InformationLifetime));
		NewInformation.Confidence = BestObservation->Confidence;
		NewInformation.SourceMannequin = BestMannequin;
		NewInformation.InformationId = NextInformationId;

		if (NextInformationId == MAX_int32)
		{
			NextInformationId = 1;
		}
		else
		{
			++NextInformationId;
		}

		LatestDirectInformation = NewInformation;
		LastKnownInformation = NewInformation;

		return;
	}

	LatestDirectInformation = FMannequinSharedTrackingInformation();

	if (LastKnownInformation.InformationType == EMannequinTrackingInformationType::None)
	{
		return;
	}

	LastKnownInformation.InformationType = EMannequinTrackingInformationType::LastObservation;

	const float SafeInformationLifetime = FMath::Max(0.0f, InformationLifetime);
	if (SafeInformationLifetime > 0.0f)
	{
		const double RemainingLifetime = FMath::Max(
			0.0,
			LastKnownInformation.ExpirationTime - CurrentTime
			);

		LastKnownInformation.Confidence = FMath::Clamp(
			static_cast<float>(RemainingLifetime) / SafeInformationLifetime,
			0.0f,
			1.0f
			);
	}
	else
	{
		LastKnownInformation.Confidence = 0.0f;
	}

	if (!LastKnownInformation.IsActive(CurrentTime))
	{
		LastKnownInformation = FMannequinSharedTrackingInformation();
	}
}

bool UMannequinTrackingCoordinator::TryGetLatestDirectInformation(FMannequinSharedTrackingInformation& OutInformation) const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	if (!LatestDirectInformation.IsActive(World->GetTimeSeconds()))
	{
		return false;
	}

	OutInformation = LatestDirectInformation;

	return true;
}

bool UMannequinTrackingCoordinator::TryGetLastKnownInformation(FMannequinSharedTrackingInformation& OutInformation) const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	if (!LastKnownInformation.IsActive(World->GetTimeSeconds()))
	{
		return false;
	}

	OutInformation = LastKnownInformation;

	return true;
}

void UMannequinTrackingCoordinator::RefreshSecondaryClueGroups(
	float ClueLifetime,
	float MaximumGroupTimeDifference,
	float MaximumGroupDistance,
	float MaximumHeightDifference
	)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		SecondaryClueGroups.Reset();
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();

	// 같은 프레임에 여러 마네킹 서비스가 호출하더라도 한 번만 계산한다.
	if (FMath::IsNearlyEqual(CurrentTime, LastSecondaryClueRefreshTime))
	{
		return;
	}

	LastSecondaryClueRefreshTime = CurrentTime;
	RemoveInvalidMannequins();
	SecondaryClueGroups.Reset();

	const double SafeClueLifetime = static_cast<double>(FMath::Max(0.0f, ClueLifetime));
	const double SafeMaximumGroupTimeDifference =
		static_cast<double>(FMath::Max(0.0f, MaximumGroupTimeDifference));
	const double SafeMaximumGroupDistanceSquared =
		FMath::Square(static_cast<double>(FMath::Max(0.0f, MaximumGroupDistance)));
	const float SafeMaximumHeightDifference = FMath::Max(0.0f, MaximumHeightDifference);

	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : RegisteredMannequins)
	{
		AMannequinAICharacter* Mannequin = MannequinPointer.Get();

		if (!IsValid(Mannequin))
		{
			continue;
		}

		// 현재 역할이 2 차인 마네킹만 간접 단서 후보가 될 수 있다.
		if (Mannequin->GetTrackingTier() != ETwoStageMannequinTier::Secondary)
		{
			continue;
		}

		// 플레이어 시야 정지가 아닌 다른 정지 원인은 제외한다.
		if (!Mannequin->HasActivePlayerSightStopClue() ||
			Mannequin->GetStopReason() != EMannequinStopReason::PlayerSight)
		{
			continue;
		}

		// 자신도 플레이어를 직접 보고 있다면 2 차가 아니라 1 차 정보로 사용해야 한다.
		if (Mannequin->GetPlayerObservation().bDirectlyVisible)
		{
			continue;
		}

		const double StopStartTime = Mannequin->GetPlayerSightStopStartTime();
		const double LastStopUpdateTime = Mannequin->GetLastPlayerSightStopTime();

		if (StopStartTime < 0.0 || LastStopUpdateTime < 0.0)
		{
			continue;
		}

		// 마지막 갱신 이후 너무 오래된 정지 단서는 제외한다.
		if (CurrentTime - LastStopUpdateTime > SafeClueLifetime)
		{
			continue;
		}

		const FVector MannequinLocation = Mannequin->GetActorLocation();
		FMannequinSecondaryClueGroup* BestGroup = nullptr;
		double BestGroupDistanceSquared = TNumericLimits<double>::Max();

		for (FMannequinSecondaryClueGroup& Group : SecondaryClueGroups)
		{
			const double DifferenceFromEarliestStop = FMath::Abs(StopStartTime - Group.EarliestStopTime);
			const double DifferenceFromLatestStop = FMath::Abs(StopStartTime - Group.LatestStopTime);

			if (DifferenceFromEarliestStop > SafeMaximumGroupTimeDifference ||
				DifferenceFromLatestStop > SafeMaximumGroupTimeDifference)
			{
				continue;
			}

			const float HeightDifference = FMath::Abs(MannequinLocation.Z - Group.CenterLocation.Z);
			if (HeightDifference > SafeMaximumHeightDifference)
			{
				continue;
			}

			const double GroupDistanceSquared = FVector::DistSquared2D(
				MannequinLocation,
				Group.CenterLocation
				);

			if (GroupDistanceSquared > SafeMaximumGroupDistanceSquared ||
				GroupDistanceSquared >= BestGroupDistanceSquared)
			{
				continue;
			}

			BestGroup = &Group;
			BestGroupDistanceSquared = GroupDistanceSquared;
		}

		if (BestGroup == nullptr)
		{
			FMannequinSecondaryClueGroup NewGroup;
			NewGroup.ClueMannequins.Add(Mannequin);
			NewGroup.CenterLocation = MannequinLocation;
			NewGroup.EarliestStopTime = StopStartTime;
			NewGroup.LatestStopTime = StopStartTime;

			SecondaryClueGroups.Add(MoveTemp(NewGroup));
			continue;
		}

		const int32 ExistingMannequinCount = BestGroup->ClueMannequins.Num();
		BestGroup->CenterLocation =
			(BestGroup->CenterLocation * static_cast<float>(ExistingMannequinCount) + MannequinLocation) /
			static_cast<float>(ExistingMannequinCount + 1);
		BestGroup->EarliestStopTime = FMath::Min(BestGroup->EarliestStopTime, StopStartTime);
		BestGroup->LatestStopTime = FMath::Max(BestGroup->LatestStopTime, StopStartTime);
		BestGroup->ClueMannequins.Add(Mannequin);
	}
}

void UMannequinTrackingCoordinator::RefreshIndirectSearchCandidates(
	float RefreshInterval,
	float CandidateSpacing,
	int32 CandidateDirectionCount,
	float EstimatedMaximumSpeed,
	float MaximumSearchRadius,
	float MaximumPredictionSeconds,
	const FVector& NavigationExtent
	)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		IndirectSearchCandidates.Reset();
		BestIndirectInformation = FMannequinSharedTrackingInformation();
		LastIndirectCandidateRefreshTime = -1.0;
		LastIndirectCandidateFilterTime = -1.0;
		LastIndirectCandidateEvaluationTime = -1.0;
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();

	// 현재 직접 관측 정보가 있다면 예상 후보보다 직접 위치를 우선한다.
	if (LatestDirectInformation.IsActive(CurrentTime))
	{
		IndirectSearchCandidates.Reset();
		BestIndirectInformation = FMannequinSharedTrackingInformation();
		LastIndirectCandidateRefreshTime = -1.0;
		LastIndirectCandidateFilterTime = -1.0;
		LastIndirectCandidateEvaluationTime = -1.0;
		return;
	}

	if (!LastKnownInformation.IsActive(CurrentTime))
	{
		IndirectSearchCandidates.Reset();
		BestIndirectInformation = FMannequinSharedTrackingInformation();
		LastIndirectCandidateRefreshTime = -1.0;
		LastIndirectCandidateFilterTime = -1.0;
		LastIndirectCandidateEvaluationTime = -1.0;
		return;
	}

	const double SafeRefreshInterval = static_cast<double>(FMath::Max(0.0f, RefreshInterval));
	if (LastIndirectCandidateRefreshTime >= 0.0 &&
		CurrentTime - LastIndirectCandidateRefreshTime < SafeRefreshInterval)
	{
		return;
	}

	LastIndirectCandidateRefreshTime = CurrentTime;
	LastIndirectCandidateFilterTime = -1.0;
	LastIndirectCandidateEvaluationTime = -1.0;
	TArray<FMannequinSearchCandidate> PreviousCandidates =
		MoveTemp(IndirectSearchCandidates);
	IndirectSearchCandidates.Reset();

	UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(World);
	if (!IsValid(NavigationSystem))
	{
		return;
	}

	const float SafeCandidateSpacing = FMath::Max(1.0f, CandidateSpacing);
	const int32 SafeCandidateDirectionCount = FMath::Max(4, CandidateDirectionCount);
	const float SafeMaximumSearchRadius = FMath::Max(0.0f, MaximumSearchRadius);
	const float SafeEstimatedMaximumSpeed = FMath::Max(0.0f, EstimatedMaximumSpeed);
	const double ElapsedTime = FMath::Max(
		0.0,
		CurrentTime - LastKnownInformation.ObservationTime
		);
	const double PredictionTime = FMath::Min(
		ElapsedTime,
		static_cast<double>(FMath::Max(0.0f, MaximumPredictionSeconds))
		);
	const float MaximumReachableDistance = FMath::Min(
		SafeEstimatedMaximumSpeed * static_cast<float>(ElapsedTime),
		SafeMaximumSearchRadius
		);

	const FVector PredictedLocation =
		LastKnownInformation.PlayerLocation +
		LastKnownInformation.PlayerVelocity * PredictionTime;
	const FVector SafeNavigationExtent(
		FMath::Abs(NavigationExtent.X),
		FMath::Abs(NavigationExtent.Y),
		FMath::Abs(NavigationExtent.Z)
		);
	const double MinimumCandidateSeparationSquared =
		FMath::Square(static_cast<double>(SafeCandidateSpacing * 0.25f));

	auto AddProjectedCandidate =
		[this,
		 NavigationSystem,
		 &SafeNavigationExtent,
		 &PreviousCandidates,
		 MinimumCandidateSeparationSquared]
		(const FVector& CandidateLocation, int32 SecondaryGroupIndex)
		{
			FNavLocation NavigationLocation;

			if (!NavigationSystem->ProjectPointToNavigation(
				CandidateLocation,
				NavigationLocation,
				SafeNavigationExtent
				))
			{
				return;
			}

			for (const FMannequinSearchCandidate& ExistingCandidate : IndirectSearchCandidates)
			{
				if (FVector::DistSquared2D(
					ExistingCandidate.Location,
					NavigationLocation.Location
					) <= MinimumCandidateSeparationSquared)
				{
					return;
				}
			}

			FMannequinSearchCandidate NewCandidate;
			NewCandidate.Location = NavigationLocation.Location;
			NewCandidate.SecondaryGroupIndex = SecondaryGroupIndex;
			NewCandidate.ReferenceInformationId = LastKnownInformation.InformationId;
			NewCandidate.Confidence = LastKnownInformation.Confidence;

			const FMannequinSearchCandidate* PreviousCandidate = nullptr;
			double PreviousCandidateDistanceSquared = TNumericLimits<double>::Max();

			for (const FMannequinSearchCandidate& Candidate : PreviousCandidates)
			{
				if (Candidate.ReferenceInformationId != NewCandidate.ReferenceInformationId)
				{
					continue;
				}

				const double CandidateDistanceSquared = FVector::DistSquared2D(
					Candidate.Location,
					NewCandidate.Location
					);

				if (CandidateDistanceSquared > MinimumCandidateSeparationSquared ||
					CandidateDistanceSquared >= PreviousCandidateDistanceSquared)
				{
					continue;
				}

				PreviousCandidate = &Candidate;
				PreviousCandidateDistanceSquared = CandidateDistanceSquared;
			}

			if (PreviousCandidate != nullptr)
			{
				NewCandidate.SearchConfidenceMultiplier =
					PreviousCandidate->SearchConfidenceMultiplier;
				NewCandidate.SearchFailureCount = PreviousCandidate->SearchFailureCount;
				NewCandidate.LastSearchTime = PreviousCandidate->LastSearchTime;
				NewCandidate.bSearchCompleted = PreviousCandidate->bSearchCompleted;
			}

			IndirectSearchCandidates.Add(MoveTemp(NewCandidate));
		};

	// 마지막 직접 관측 위치와 속도 예측 위치는 항상 기본 후보로 사용한다.
	AddProjectedCandidate(LastKnownInformation.PlayerLocation, INDEX_NONE);
	AddProjectedCandidate(PredictedLocation, INDEX_NONE);

	TArray<FVector> SearchCenters;
	TArray<int32> SearchCenterGroupIndices;
	SearchCenters.Add(PredictedLocation);
	SearchCenterGroupIndices.Add(INDEX_NONE);

	for (int32 GroupIndex = 0; GroupIndex < SecondaryClueGroups.Num(); ++GroupIndex)
	{
		const FMannequinSecondaryClueGroup& Group = SecondaryClueGroups[GroupIndex];

		if (Group.ClueMannequins.IsEmpty())
		{
			continue;
		}

		AddProjectedCandidate(Group.CenterLocation, GroupIndex);
		SearchCenters.Add(Group.CenterLocation);
		SearchCenterGroupIndices.Add(GroupIndex);
	}

	const int32 RingCount = MaximumReachableDistance > 0.0f
		? FMath::CeilToInt(MaximumReachableDistance / SafeCandidateSpacing)
		: 0;

	for (int32 CenterIndex = 0; CenterIndex < SearchCenters.Num(); ++CenterIndex)
	{
		for (int32 RingIndex = 1; RingIndex <= RingCount; ++RingIndex)
		{
			const float RingRadius = FMath::Min(
				SafeCandidateSpacing * static_cast<float>(RingIndex),
				MaximumReachableDistance
				);

			for (int32 DirectionIndex = 0; DirectionIndex < SafeCandidateDirectionCount; ++DirectionIndex)
			{
				const float AngleDegrees =
					360.0f * static_cast<float>(DirectionIndex) /
					static_cast<float>(SafeCandidateDirectionCount);
				const FVector Direction = FRotator(0.0f, AngleDegrees, 0.0f).Vector();
				const FVector CandidateLocation =
					SearchCenters[CenterIndex] + Direction * RingRadius;

				AddProjectedCandidate(
					CandidateLocation,
					SearchCenterGroupIndices[CenterIndex]
					);
			}
		}
	}
}

void UMannequinTrackingCoordinator::FilterIndirectSearchCandidates(
	float EstimatedMaximumSpeed,
	float MaximumSearchRadius,
	float SecondaryClueRadius,
	float PlayerClueSightHalfAngle,
	float MannequinSightRadius,
	float MannequinSightHalfAngle,
	float MannequinSightMaximumHeightDifference,
	const FVector& NavigationExtent
	)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || IndirectSearchCandidates.IsEmpty())
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (FMath::IsNearlyEqual(CurrentTime, LastIndirectCandidateFilterTime))
	{
		return;
	}

	LastIndirectCandidateFilterTime = CurrentTime;

	if (!LastKnownInformation.IsActive(CurrentTime) || LatestDirectInformation.IsActive(CurrentTime))
	{
		IndirectSearchCandidates.Reset();
		return;
	}

	UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(World);
	if (!IsValid(NavigationSystem))
	{
		IndirectSearchCandidates.Reset();
		return;
	}

	const FVector SafeNavigationExtent(
		FMath::Abs(NavigationExtent.X),
		FMath::Abs(NavigationExtent.Y),
		FMath::Abs(NavigationExtent.Z)
		);
	FNavLocation NavigationStartLocation;

	if (!NavigationSystem->ProjectPointToNavigation(
		LastKnownInformation.PlayerLocation,
		NavigationStartLocation,
		SafeNavigationExtent
		))
	{
		IndirectSearchCandidates.Reset();
		return;
	}

	const double ElapsedTime = FMath::Max(
		0.0,
		CurrentTime - LastKnownInformation.ObservationTime
		);
	const double MaximumReachableDistance = FMath::Min(
		static_cast<double>(FMath::Max(0.0f, EstimatedMaximumSpeed)) * ElapsedTime,
		static_cast<double>(FMath::Max(0.0f, MaximumSearchRadius))
		);
	TArray<FMannequinSearchCandidate> FilteredCandidates;

	for (const FMannequinSearchCandidate& Candidate : IndirectSearchCandidates)
	{
		double NavigationPathLength = 0.0;
		const ENavigationQueryResult::Type PathResult = UNavigationSystemV1::GetPathLength(
			World,
			NavigationStartLocation.Location,
			Candidate.Location,
			NavigationPathLength
			);

		if (PathResult != ENavigationQueryResult::Success ||
			NavigationPathLength > MaximumReachableDistance)
		{
			continue;
		}

		FMannequinSearchCandidate FilteredCandidate = Candidate;

		// 2 차 단서가 없다면 마지막 관측 정보만을 사용하는 후보로 유지한다.
		if (SecondaryClueGroups.IsEmpty())
		{
			FilteredCandidate.ExplainedClueCount = 0;
			FilteredCandidates.Add(MoveTemp(FilteredCandidate));
			continue;
		}

		bool bExplainsSecondaryGroup = false;

		if (Candidate.SecondaryGroupIndex != INDEX_NONE &&
			SecondaryClueGroups.IsValidIndex(Candidate.SecondaryGroupIndex))
		{
			bExplainsSecondaryGroup = DoesCandidateExplainSecondaryGroup(
				Candidate.Location,
				SecondaryClueGroups[Candidate.SecondaryGroupIndex],
				SecondaryClueRadius,
				PlayerClueSightHalfAngle,
				MannequinSightRadius,
				MannequinSightHalfAngle,
				MannequinSightMaximumHeightDifference
				);

			if (bExplainsSecondaryGroup)
			{
				FilteredCandidate.ExplainedClueCount =
					SecondaryClueGroups[Candidate.SecondaryGroupIndex].ClueMannequins.Num();
			}
		}
		else
		{
			for (int32 GroupIndex = 0; GroupIndex < SecondaryClueGroups.Num(); ++GroupIndex)
			{
				if (!DoesCandidateExplainSecondaryGroup(
					Candidate.Location,
					SecondaryClueGroups[GroupIndex],
					SecondaryClueRadius,
					PlayerClueSightHalfAngle,
					MannequinSightRadius,
					MannequinSightHalfAngle,
					MannequinSightMaximumHeightDifference
					))
				{
					continue;
				}

				FilteredCandidate.SecondaryGroupIndex = GroupIndex;
				FilteredCandidate.ExplainedClueCount =
					SecondaryClueGroups[GroupIndex].ClueMannequins.Num();
				bExplainsSecondaryGroup = true;
				break;
			}
		}

		if (bExplainsSecondaryGroup)
		{
			FilteredCandidates.Add(MoveTemp(FilteredCandidate));
		}
	}

	IndirectSearchCandidates = MoveTemp(FilteredCandidates);
}

void UMannequinTrackingCoordinator::ProcessTertiarySearchResults(
	float SearchLocationMatchTolerance,
	float FailureConfidenceMultiplier
	)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	const double SafeMatchToleranceSquared = FMath::Square(
		static_cast<double>(FMath::Max(0.0f, SearchLocationMatchTolerance))
		);
	const float SafeFailureConfidenceMultiplier = FMath::Clamp(
		FailureConfidenceMultiplier,
		0.0f,
		1.0f
		);
	bool bCandidateStateChanged = false;

	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : RegisteredMannequins)
	{
		AMannequinAICharacter* Mannequin = MannequinPointer.Get();
		if (!IsValid(Mannequin))
		{
			continue;
		}

		const FMannequinTertiaryAssignment Assignment =
			Mannequin->GetTertiaryAssignment();
		const FMannequinTertiarySearchState SearchState =
			Mannequin->GetTertiarySearchState();

		if (!SearchState.HasCompletedAssignment(Assignment.AssignmentId))
		{
			continue;
		}

		if (SearchState.Result == EMannequinTertiarySearchResult::PlayerFound)
		{
			Mannequin->ResetTertiarySearchFailureCount();
			Mannequin->ClearTertiaryAssignment();
			continue;
		}

		// 직접 관측 목표는 새로운 직접 정보가 계속 갱신하므로 간접 후보 실패로 기록하지 않는다.
		if (Assignment.TrackingEvidence == EMannequinTrackingEvidence::DirectObservation)
		{
			Mannequin->ClearTertiaryAssignment();
			continue;
		}

		FMannequinSearchCandidate* MatchedCandidate = nullptr;
		double ClosestCandidateDistanceSquared = TNumericLimits<double>::Max();

		for (FMannequinSearchCandidate& Candidate : IndirectSearchCandidates)
		{
			const double CandidateDistanceSquared = FVector::DistSquared2D(
				Candidate.Location,
				Assignment.TargetLocation
				);

			if (CandidateDistanceSquared > SafeMatchToleranceSquared ||
				CandidateDistanceSquared >= ClosestCandidateDistanceSquared)
			{
				continue;
			}

			MatchedCandidate = &Candidate;
			ClosestCandidateDistanceSquared = CandidateDistanceSquared;
		}

		if (MatchedCandidate != nullptr)
		{
			MatchedCandidate->SearchConfidenceMultiplier = FMath::Clamp(
				MatchedCandidate->SearchConfidenceMultiplier * SafeFailureConfidenceMultiplier,
				0.0f,
				1.0f
				);
			++MatchedCandidate->SearchFailureCount;
			MatchedCandidate->LastSearchTime = CurrentTime;
			MatchedCandidate->bSearchCompleted = true;
			bCandidateStateChanged = true;
		}

		// 결과를 한 번만 처리하고 다음 중앙 배정을 받을 수 있도록 기존 배정을 제거한다.
		Mannequin->ClearTertiaryAssignment();
	}

	if (bCandidateStateChanged)
	{
		// 같은 후보 생성 주기 안에서도 실패 결과를 반영해 점수를 다시 계산한다.
		LastIndirectCandidateEvaluationTime = -1.0;
	}
}

void UMannequinTrackingCoordinator::EvaluateIndirectSearchCandidates(
	float MaximumPredictionSeconds,
	float MaximumSearchRadius,
	float DistanceScoreWeight,
	float DirectionScoreWeight,
	float ClueScoreWeight
	)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		BestIndirectInformation = FMannequinSharedTrackingInformation();
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();

	if (LatestDirectInformation.IsActive(CurrentTime) ||
		!LastKnownInformation.IsActive(CurrentTime) ||
		IndirectSearchCandidates.IsEmpty())
	{
		BestIndirectInformation = FMannequinSharedTrackingInformation();
		return;
	}

	// 새 후보 목록이 생성된 뒤에만 점수를 다시 계산한다.
	if (LastIndirectCandidateRefreshTime < 0.0 ||
		LastIndirectCandidateEvaluationTime >= LastIndirectCandidateRefreshTime)
	{
		return;
	}

	LastIndirectCandidateEvaluationTime = CurrentTime;

	const double ElapsedTime = FMath::Max(
		0.0,
		CurrentTime - LastKnownInformation.ObservationTime
		);
	const double PredictionTime = FMath::Min(
		ElapsedTime,
		static_cast<double>(FMath::Max(0.0f, MaximumPredictionSeconds))
		);
	const FVector PredictedLocation =
		LastKnownInformation.PlayerLocation +
		LastKnownInformation.PlayerVelocity * PredictionTime;
	const FVector LastMovementDirection = LastKnownInformation.PlayerVelocity.GetSafeNormal2D();
	const float SafeMaximumSearchRadius = FMath::Max(1.0f, MaximumSearchRadius);
	const float SafeDistanceScoreWeight = FMath::Max(0.0f, DistanceScoreWeight);
	const float SafeDirectionScoreWeight = FMath::Max(0.0f, DirectionScoreWeight);
	const float SafeClueScoreWeight = FMath::Max(0.0f, ClueScoreWeight);
	const float TotalScoreWeight = FMath::Max(
		KINDA_SMALL_NUMBER,
		SafeDistanceScoreWeight + SafeDirectionScoreWeight + SafeClueScoreWeight
		);
	int32 MaximumExplainedClueCount = 0;

	for (const FMannequinSearchCandidate& Candidate : IndirectSearchCandidates)
	{
		MaximumExplainedClueCount = FMath::Max(
			MaximumExplainedClueCount,
			Candidate.ExplainedClueCount
			);
	}

	for (FMannequinSearchCandidate& Candidate : IndirectSearchCandidates)
	{
		const float DistanceFromPrediction = FVector::Dist2D(
			Candidate.Location,
			PredictedLocation
			);
		const float DistanceScore = 1.0f - FMath::Clamp(
			DistanceFromPrediction / SafeMaximumSearchRadius,
			0.0f,
			1.0f
			);
		const FVector DirectionFromLastObservation =
			(Candidate.Location - LastKnownInformation.PlayerLocation).GetSafeNormal2D();
		const float DirectionScore = LastMovementDirection.IsNearlyZero()
			? 0.5f
			: FMath::Clamp(
				(FVector::DotProduct(LastMovementDirection, DirectionFromLastObservation) + 1.0f) * 0.5f,
				0.0f,
				1.0f
				);
		const float ClueScore = MaximumExplainedClueCount > 0
			? static_cast<float>(Candidate.ExplainedClueCount) /
				static_cast<float>(MaximumExplainedClueCount)
			: 0.0f;
		const float WeightedScore =
			DistanceScore * SafeDistanceScoreWeight +
			DirectionScore * SafeDirectionScoreWeight +
			ClueScore * SafeClueScoreWeight;

		Candidate.Score = FMath::Clamp(
			Candidate.Confidence *
			Candidate.SearchConfidenceMultiplier *
			WeightedScore /
			TotalScoreWeight,
			0.0f,
			1.0f
			);
	}

	IndirectSearchCandidates.Sort(
		[](const FMannequinSearchCandidate& Left, const FMannequinSearchCandidate& Right)
		{
			if (Left.bSearchCompleted != Right.bSearchCompleted)
			{
				return !Left.bSearchCompleted;
			}

			return Left.Score > Right.Score;
		}
		);

	const FMannequinSearchCandidate& BestCandidate = IndirectSearchCandidates[0];
	FMannequinSharedTrackingInformation NewBestInformation;
	NewBestInformation.InformationType = EMannequinTrackingInformationType::IndirectPrediction;
	NewBestInformation.PlayerLocation = BestCandidate.Location;
	NewBestInformation.PlayerVelocity = LastKnownInformation.PlayerVelocity;
	NewBestInformation.ObservationTime = CurrentTime;
	NewBestInformation.ExpirationTime = LastKnownInformation.ExpirationTime;
	NewBestInformation.Confidence = BestCandidate.Score;
	NewBestInformation.SourceMannequin = LastKnownInformation.SourceMannequin;
	NewBestInformation.InformationId = NextInformationId;

	if (NextInformationId == MAX_int32)
	{
		NextInformationId = 1;
	}
	else
	{
		++NextInformationId;
	}

	BestIndirectInformation = NewBestInformation;
}

bool UMannequinTrackingCoordinator::TryGetBestIndirectInformation(
	FMannequinSharedTrackingInformation& OutInformation
	) const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	if (!BestIndirectInformation.IsActive(World->GetTimeSeconds()))
	{
		return false;
	}

	OutInformation = BestIndirectInformation;

	return true;
}

void UMannequinTrackingCoordinator::RefreshTertiaryAssignments(
	float AssignmentRadius,
	float MaximumHeightDifference,
	int32 MaximumAssignedMannequins,
	float AssignmentLifetime,
	float AssignmentTargetChangeTolerance
	)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		ClearTertiaryAssignments();
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();

	// 같은 프레임에 여러 마네킹 서비스가 호출하더라도 한 번만 배정한다.
	if (FMath::IsNearlyEqual(CurrentTime, LastTertiaryAssignmentRefreshTime))
	{
		return;
	}

	LastTertiaryAssignmentRefreshTime = CurrentTime;
	RemoveInvalidMannequins();

	FMannequinSharedTrackingInformation AssignmentInformation;
	const bool bUsesDirectInformation = LatestDirectInformation.IsActive(CurrentTime);

	if (bUsesDirectInformation)
	{
		AssignmentInformation = LatestDirectInformation;
	}
	else if (BestIndirectInformation.IsActive(CurrentTime))
	{
		AssignmentInformation = BestIndirectInformation;
	}
	else
	{
		ClearTertiaryAssignments();
		return;
	}

	const int32 SafeMaximumAssignedMannequins = FMath::Max(0, MaximumAssignedMannequins);
	if (SafeMaximumAssignedMannequins == 0)
	{
		ClearTertiaryAssignments();
		return;
	}

	const double SafeAssignmentRadiusSquared =
		FMath::Square(static_cast<double>(FMath::Max(0.0f, AssignmentRadius)));
	const float SafeMaximumHeightDifference = FMath::Max(0.0f, MaximumHeightDifference);
	TArray<FVector> AssignmentAnchorLocations;

	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : RegisteredMannequins)
	{
		const AMannequinAICharacter* Mannequin = MannequinPointer.Get();
		if (!IsValid(Mannequin))
		{
			continue;
		}

		const ETwoStageMannequinTier TrackingTier = Mannequin->GetTrackingTier();
		if (TrackingTier == ETwoStageMannequinTier::Primary ||
			TrackingTier == ETwoStageMannequinTier::Secondary)
		{
			AssignmentAnchorLocations.Add(Mannequin->GetActorLocation());
		}
	}

	for (const FMannequinSecondaryClueGroup& Group : SecondaryClueGroups)
	{
		AssignmentAnchorLocations.Add(Group.CenterLocation);
	}

	if (AssignmentAnchorLocations.IsEmpty())
	{
		const AMannequinAICharacter* SourceMannequin = AssignmentInformation.SourceMannequin.Get();
		if (IsValid(SourceMannequin))
		{
			AssignmentAnchorLocations.Add(SourceMannequin->GetActorLocation());
		}
	}

	if (AssignmentAnchorLocations.IsEmpty())
	{
		AssignmentAnchorLocations.Add(AssignmentInformation.PlayerLocation);
	}

	struct FTertiaryAssignmentCandidate
	{
		AMannequinAICharacter* Mannequin = nullptr;
		double DistanceToAnchorSquared = TNumericLimits<double>::Max();
		double DistanceToTargetSquared = TNumericLimits<double>::Max();
	};

	TArray<FTertiaryAssignmentCandidate> AssignmentCandidates;

	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : RegisteredMannequins)
	{
		AMannequinAICharacter* Mannequin = MannequinPointer.Get();
		if (!IsValid(Mannequin))
		{
			continue;
		}

		const ETwoStageMannequinTier TrackingTier = Mannequin->GetTrackingTier();

		// 1 차와 2 차는 각각 직접 관측자와 정지 단서이므로 3 차 배정에서 제외한다.
		if (TrackingTier == ETwoStageMannequinTier::Primary ||
			TrackingTier == ETwoStageMannequinTier::Secondary)
		{
			continue;
		}

		// 역할 갱신 순서와 관계없이 직접 관측·플레이어 시야 정지 마네킹은 제외한다.
		if (Mannequin->GetPlayerObservation().bDirectlyVisible ||
			Mannequin->IsObservedByPlayer())
		{
			continue;
		}

		const FVector MannequinLocation = Mannequin->GetActorLocation();
		double ClosestAnchorDistanceSquared = TNumericLimits<double>::Max();

		for (const FVector& AnchorLocation : AssignmentAnchorLocations)
		{
			const float HeightDifference = FMath::Abs(
				MannequinLocation.Z - AnchorLocation.Z
				);

			if (HeightDifference > SafeMaximumHeightDifference)
			{
				continue;
			}

			ClosestAnchorDistanceSquared = FMath::Min(
				ClosestAnchorDistanceSquared,
				FVector::DistSquared2D(MannequinLocation, AnchorLocation)
				);
		}

		if (ClosestAnchorDistanceSquared > SafeAssignmentRadiusSquared)
		{
			continue;
		}

		FTertiaryAssignmentCandidate& NewCandidate = AssignmentCandidates.AddDefaulted_GetRef();
		NewCandidate.Mannequin = Mannequin;
		NewCandidate.DistanceToAnchorSquared = ClosestAnchorDistanceSquared;
		NewCandidate.DistanceToTargetSquared = FVector::DistSquared2D(
			MannequinLocation,
			AssignmentInformation.PlayerLocation
			);
	}

	AssignmentCandidates.Sort(
		[](const FTertiaryAssignmentCandidate& Left, const FTertiaryAssignmentCandidate& Right)
		{
			if (!FMath::IsNearlyEqual(Left.DistanceToAnchorSquared, Right.DistanceToAnchorSquared))
			{
				return Left.DistanceToAnchorSquared < Right.DistanceToAnchorSquared;
			}

			if (!FMath::IsNearlyEqual(Left.DistanceToTargetSquared, Right.DistanceToTargetSquared))
			{
				return Left.DistanceToTargetSquared < Right.DistanceToTargetSquared;
			}

			return
				IsValid(Left.Mannequin) &&
				IsValid(Right.Mannequin) &&
				Left.Mannequin->GetUniqueID() < Right.Mannequin->GetUniqueID();
		}
		);

	int32 AssignmentCount = FMath::Min(
		SafeMaximumAssignedMannequins,
		AssignmentCandidates.Num()
		);

	// 간접 수색에서는 서로 다른 후보를 배정할 수 있는 수만큼만 3 차를 선정한다.
	if (!bUsesDirectInformation)
	{
		AssignmentCount = FMath::Min(
			AssignmentCount,
			IndirectSearchCandidates.Num()
			);
	}

	const double SafeAssignmentLifetime =
		static_cast<double>(FMath::Max(0.0f, AssignmentLifetime));
	const double SafeTargetChangeToleranceSquared = FMath::Square(
		static_cast<double>(FMath::Max(0.0f, AssignmentTargetChangeTolerance))
		);
	TSet<AMannequinAICharacter*> AssignedMannequins;

	for (int32 AssignmentIndex = 0; AssignmentIndex < AssignmentCount; ++AssignmentIndex)
	{
		AMannequinAICharacter* AssignedMannequin =
			AssignmentCandidates[AssignmentIndex].Mannequin;

		if (!IsValid(AssignedMannequin))
		{
			continue;
		}

		FMannequinTertiaryAssignment NewAssignment;
		NewAssignment.TargetLocation = AssignmentInformation.PlayerLocation;
		NewAssignment.ReferenceInformationId = AssignmentInformation.InformationId;
		NewAssignment.ExpirationTime = FMath::Min(
			AssignmentInformation.ExpirationTime,
			CurrentTime + SafeAssignmentLifetime
			);
		NewAssignment.Confidence = AssignmentInformation.Confidence;

		if (bUsesDirectInformation)
		{
			NewAssignment.TrackingEvidence = EMannequinTrackingEvidence::DirectObservation;
		}
		else
		{
			// 후보 배열은 점수가 높은 순서이므로 각 3 차에 서로 다른 후보를 순서대로 배정한다.
			const FMannequinSearchCandidate& SearchCandidate =
				IndirectSearchCandidates[AssignmentIndex];

			NewAssignment.TargetLocation = SearchCandidate.Location;
			NewAssignment.SearchCandidateIndex = AssignmentIndex;
			NewAssignment.SecondaryGroupIndex = SearchCandidate.SecondaryGroupIndex;
			NewAssignment.Confidence = SearchCandidate.Score;

			NewAssignment.TrackingEvidence =
				NewAssignment.SecondaryGroupIndex != INDEX_NONE
				? EMannequinTrackingEvidence::StoppedMannequinClue
				: EMannequinTrackingEvidence::VelocityPrediction;
		}

		const FMannequinTertiaryAssignment& ExistingAssignment =
			AssignedMannequin->GetTertiaryAssignment();
		const bool bKeepsExistingAssignment =
			ExistingAssignment.IsActive(CurrentTime) &&
			ExistingAssignment.TrackingEvidence == NewAssignment.TrackingEvidence &&
			ExistingAssignment.SearchCandidateIndex == NewAssignment.SearchCandidateIndex &&
			ExistingAssignment.SecondaryGroupIndex == NewAssignment.SecondaryGroupIndex &&
			FVector::DistSquared2D(
				ExistingAssignment.TargetLocation,
				NewAssignment.TargetLocation
				) <= SafeTargetChangeToleranceSquared;

		if (bKeepsExistingAssignment)
		{
			NewAssignment.AssignmentId = ExistingAssignment.AssignmentId;
			NewAssignment.AssignmentTime = ExistingAssignment.AssignmentTime;
		}
		else
		{
			NewAssignment.AssignmentId = NextAssignmentId;
			NewAssignment.AssignmentTime = CurrentTime;

			if (NextAssignmentId == MAX_int32)
			{
				NextAssignmentId = 1;
			}
			else
			{
				++NextAssignmentId;
			}
		}

		AssignedMannequin->SetTertiaryAssignment(NewAssignment);
		AssignedMannequins.Add(AssignedMannequin);
	}

	// 이번 갱신에서 선택되지 않은 기존 3 차 마네킹의 이전 배정만 제거한다.
	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : RegisteredMannequins)
	{
		AMannequinAICharacter* Mannequin = MannequinPointer.Get();
		if (IsValid(Mannequin) && !AssignedMannequins.Contains(Mannequin))
		{
			Mannequin->ClearTertiaryAssignment();
		}
	}
}

void UMannequinTrackingCoordinator::ClearTertiaryAssignments()
{
	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : RegisteredMannequins)
	{
		AMannequinAICharacter* Mannequin = MannequinPointer.Get();
		if (IsValid(Mannequin))
		{
			Mannequin->ClearTertiaryAssignment();
		}
	}
}

bool UMannequinTrackingCoordinator::DoesCandidateExplainSecondaryGroup(
	const FVector& CandidateLocation,
	const FMannequinSecondaryClueGroup& Group,
	float SecondaryClueRadius,
	float PlayerClueSightHalfAngle,
	float MannequinSightRadius,
	float MannequinSightHalfAngle,
	float MannequinSightMaximumHeightDifference
	) const
{
	if (Group.ClueMannequins.IsEmpty())
	{
		return false;
	}

	const double SafeSecondaryClueRadiusSquared =
		FMath::Square(static_cast<double>(FMath::Max(0.0f, SecondaryClueRadius)));
	TArray<float> ClueAngles;

	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : Group.ClueMannequins)
	{
		const AMannequinAICharacter* Mannequin = MannequinPointer.Get();

		if (!IsValid(Mannequin) || !Mannequin->HasActivePlayerSightStopClue())
		{
			return false;
		}

		const FVector MannequinLocation = Mannequin->GetActorLocation();

		if (FVector::DistSquared2D(CandidateLocation, MannequinLocation) > SafeSecondaryClueRadiusSquared)
		{
			return false;
		}

		if (!CanCandidateSeeSecondaryClue(CandidateLocation, *Mannequin))
		{
			return false;
		}

		if (CanSecondaryClueSeeCandidate(
			*Mannequin,
			CandidateLocation,
			MannequinSightRadius,
			MannequinSightHalfAngle,
			MannequinSightMaximumHeightDifference
			))
		{
			return false;
		}

		const FVector CandidateToMannequin = MannequinLocation - CandidateLocation;
		ClueAngles.Add(FMath::Fmod(
			FMath::RadiansToDegrees(FMath::Atan2(CandidateToMannequin.Y, CandidateToMannequin.X)) + 360.0f,
			360.0f
			));
	}

	if (ClueAngles.Num() <= 1)
	{
		return true;
	}

	ClueAngles.Sort();
	float LargestAngularGap = 0.0f;

	for (int32 AngleIndex = 0; AngleIndex < ClueAngles.Num(); ++AngleIndex)
	{
		const float CurrentAngle = ClueAngles[AngleIndex];
		const float NextAngle = AngleIndex + 1 < ClueAngles.Num()
			? ClueAngles[AngleIndex + 1]
			: ClueAngles[0] + 360.0f;

		LargestAngularGap = FMath::Max(LargestAngularGap, NextAngle - CurrentAngle);
	}

	const float MinimumRequiredFieldOfView = 360.0f - LargestAngularGap;
	const float AvailableFieldOfView = FMath::Clamp(PlayerClueSightHalfAngle, 0.0f, 180.0f) * 2.0f;

	return MinimumRequiredFieldOfView <= AvailableFieldOfView;
}

bool UMannequinTrackingCoordinator::CanCandidateSeeSecondaryClue(
	const FVector& CandidateLocation,
	const AMannequinAICharacter& SecondaryMannequin
	) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	const FVector TraceStart = CandidateLocation + FVector::UpVector * 80.0f;
	FVector TraceEnd = SecondaryMannequin.GetActorLocation() + FVector::UpVector * 60.0f;
	const USkeletalMeshComponent* MannequinMesh = SecondaryMannequin.GetMesh();

	if (IsValid(MannequinMesh) && MannequinMesh->GetBoneIndex(TEXT("head")) != INDEX_NONE)
	{
		TraceEnd = MannequinMesh->GetBoneLocation(TEXT("head"));
	}

	FCollisionQueryParams QueryParams;

	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : RegisteredMannequins)
	{
		if (const AMannequinAICharacter* Mannequin = MannequinPointer.Get())
		{
			QueryParams.AddIgnoredActor(Mannequin);
		}
	}

	FHitResult HitResult;
	const bool bBlocked = World->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_GameTraceChannel1,
		QueryParams
		);

	return !bBlocked;
}

bool UMannequinTrackingCoordinator::CanSecondaryClueSeeCandidate(
	const AMannequinAICharacter& SecondaryMannequin,
	const FVector& CandidateLocation,
	float MannequinSightRadius,
	float MannequinSightHalfAngle,
	float MannequinSightMaximumHeightDifference
	) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	const FVector MannequinLocation = SecondaryMannequin.GetActorLocation();
	const FVector MannequinToCandidate = CandidateLocation - MannequinLocation;

	if (FMath::Abs(MannequinToCandidate.Z) > FMath::Max(0.0f, MannequinSightMaximumHeightDifference) ||
		MannequinToCandidate.SizeSquared2D() > FMath::Square(static_cast<double>(FMath::Max(0.0f, MannequinSightRadius))))
	{
		return false;
	}

	const FVector DirectionToCandidate = MannequinToCandidate.GetSafeNormal2D();
	if (!DirectionToCandidate.IsNearlyZero())
	{
		const FVector MannequinForward = SecondaryMannequin.GetActorForwardVector().GetSafeNormal2D();
		const float SightMinimumDot = FMath::Cos(
			FMath::DegreesToRadians(FMath::Clamp(MannequinSightHalfAngle, 0.0f, 180.0f))
			);

		if (FVector::DotProduct(MannequinForward, DirectionToCandidate) < SightMinimumDot)
		{
			return false;
		}
	}

	FVector TraceStart = MannequinLocation + FVector::UpVector * 60.0f;
	const USkeletalMeshComponent* MannequinMesh = SecondaryMannequin.GetMesh();

	if (IsValid(MannequinMesh) && MannequinMesh->GetBoneIndex(TEXT("head")) != INDEX_NONE)
	{
		TraceStart = MannequinMesh->GetBoneLocation(TEXT("head"));
	}

	const FVector TraceEnd = CandidateLocation + FVector::UpVector * 80.0f;
	FCollisionQueryParams QueryParams;

	for (const TWeakObjectPtr<AMannequinAICharacter>& MannequinPointer : RegisteredMannequins)
	{
		if (const AMannequinAICharacter* Mannequin = MannequinPointer.Get())
		{
			QueryParams.AddIgnoredActor(Mannequin);
		}
	}

	FHitResult HitResult;
	const bool bBlocked = World->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_GameTraceChannel1,
		QueryParams
		);

	return !bBlocked;
}

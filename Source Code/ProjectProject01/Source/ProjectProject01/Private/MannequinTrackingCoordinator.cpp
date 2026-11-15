#include "MannequinTrackingCoordinator.h"

#include "Engine/World.h"

#include "MannequinAICharacter.h"

void UMannequinTrackingCoordinator::RegisterMannequin(AMannequinAICharacter& Mannequin)
{
	const TWeakObjectPtr<AMannequinAICharacter> MannequinPointer(&Mannequin);

	if (!RegisteredMannequins.Contains(MannequinPointer))
	{
		RegisteredMannequins.Add(MannequinPointer);

		// 새 마네킹이 추가된 프레임에는 중앙 정보를 다시 계산한다.
		LastRefreshTime = -1.0;
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

	RemoveInvalidMannequins();
	SecondaryClueGroups.Reset();

	const double CurrentTime = World->GetTimeSeconds();
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

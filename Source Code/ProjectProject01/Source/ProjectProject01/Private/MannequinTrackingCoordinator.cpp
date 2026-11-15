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

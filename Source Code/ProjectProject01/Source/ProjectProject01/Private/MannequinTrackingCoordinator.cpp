#include "MannequinTrackingCoordinator.h"

#include "MannequinAICharacter.h"

void UMannequinTrackingCoordinator::RegisterMannequin(AMannequinAICharacter& Mannequin)
{
	const TWeakObjectPtr<AMannequinAICharacter> MannequinPointer(&Mannequin);

	RegisteredMannequins.AddUnique(MannequinPointer);
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

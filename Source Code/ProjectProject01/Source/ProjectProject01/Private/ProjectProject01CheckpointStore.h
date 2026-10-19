#pragma once

#include "CoreMinimal.h"

class UProjectProject01SinglePlayerSaveGame;
class USaveGame;

/** A valid generation is published by its checksum, not by overwriting a pointer file.
 * The newest valid snapshot is NEVER the next write target. A truncated write can
 * destroy the older generation, but cannot destroy the last known-good snapshot.
 * Legacy primary/backup slots remain readable and are never overwritten on upgrade.
 */
namespace ProjectCheckpointStore
{
	struct FSelection
	{
		UProjectProject01SinglePlayerSaveGame* Save = nullptr;
		FString Slot;
	};
	using FLoader = TFunction<UProjectProject01SinglePlayerSaveGame*(const FString&)>;
	FSelection SelectNewest(const FString& Base, const FLoader& Load);
	FString NextWriteSlot(const FString& Base, const FSelection& Current);
	TArray<FString> AllSlots(const FString& Base);
	bool Encode(USaveGame* Save, TArray<uint8>& OutData);
	USaveGame* Decode(const TArray<uint8>& Data);
}

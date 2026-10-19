#pragma once
#include "CoreMinimal.h"
class APawn;
class AActor;
class UBTService_MannequinAI;

/** Explicit single-player state, never raw BT memory or process-local pointers. */
struct FProjectCheckpointAI
{
	static TArray<uint8> Capture(APawn* Pawn, const TFunction<FName(AActor*)>& ActorId);
	static bool Restore(APawn* Pawn, const TArray<uint8>& Data, const TFunction<AActor*(FName)>& Resolve);
	static TArray<uint8> CaptureService(const UBTService_MannequinAI* Service, double Now);
	static bool RestoreService(UBTService_MannequinAI* Service, const TArray<uint8>& Data, double Now);
};

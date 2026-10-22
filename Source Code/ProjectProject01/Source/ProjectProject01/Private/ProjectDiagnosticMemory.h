#pragma once
#include "CoreMinimal.h"
#include "HAL/MemoryMisc.h"
#include "HAL/UnrealMemory.h"

namespace ProjectDiagnosticMemory
{
	/** Binned allocator accounting, NOT process working set or a measurement of all caches. */
	struct FAllocatorSnapshot
	{
		bool Available=false;
		uint64 Used=0,Committed=0,Retained=0;
	};
	inline FAllocatorSnapshot Decode(const FGenericMemoryStats& Stats)
	{
		// Verified against UE 5.8 MallocBinnedCommon: used includes bin-size padding;
		// OS allocated includes free cached blocks, pool slack and alignment.
		const auto* Used=Stats.Find(TEXT("TotalAllocated"));
		const auto* Committed=Stats.Find(TEXT("TotalOSAllocated"));
		if(!Used || !Committed || *Committed<*Used)return {};
		return {true,uint64(*Used),uint64(*Committed),uint64(*Committed-*Used)};
	}
	// UE 5.8 exposes allocator statistics through this legacy alias, not an FMemory facade.
	// Compatibility shim for read-only stats only; never allocate or replace the allocator here.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	inline FAllocatorSnapshot Capture()
	{
		if(!GMalloc)return {};
		FGenericMemoryStats Stats;GMalloc->GetAllocatorStats(Stats);return Decode(Stats);
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

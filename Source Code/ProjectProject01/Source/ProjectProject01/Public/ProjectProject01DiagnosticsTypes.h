// File: Source/ProjectProject01/Public/ProjectProject01DiagnosticsTypes.h
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"

PROJECTPROJECT01_API DECLARE_LOG_CATEGORY_EXTERN(LogProjectProject01Diagnostics, Log, All);

enum class EProjectProject01DiagnosticSeverity : uint8
{
	Normal,
	Warning,
	Risk,
	Inconclusive
};

struct FProjectProject01DiagnosticEvent
{
	FString UtcTime;
	double GameSeconds = 0.0;
	EProjectProject01DiagnosticSeverity Severity = EProjectProject01DiagnosticSeverity::Normal;
	FString Code;
	FString Message;
	FString ActorName;
};

/** Editable in Config/DefaultGame.ini. These rules describe a test; they never alter gameplay. */
struct FProjectProject01DiagnosticTestDefinition
{
	FString Name = TEXT("ManualPIE");
	FString Purpose;
	FString ReproductionSteps;
	FString ExpectedResult;
	FString FailureCriteria;
	float TimeoutSeconds = 300.0f;
};

struct FProjectProject01DiagnosticSample
{
	FString UtcTime;
	double GameSeconds = 0.0;
	float FrameMilliseconds = 0.0f;
	float FramesPerSecond = 0.0f;
	float GameThreadMilliseconds = 0.0f;
	float DrawThreadMilliseconds = 0.0f;
	float GpuMilliseconds = 0.0f;
	uint64 UsedPhysicalBytes = 0;
	uint64 UsedVirtualBytes = 0;
	double MemoryBytesPerSecondDelta = 0.0;
	int32 UObjectCount = 0;
	int32 ActorCount = 0;
	uint64 StreamingTextureMemoryBytes = 0;
	uint64 NonStreamingTextureMemoryBytes = 0;
	uint64 TexturePoolSizeBytes = 0;
	uint32 InBytesPerSecond = 0;
	uint32 OutBytesPerSecond = 0;
	int32 PlayerCount = 0;
	int32 MannequinCount = 0;
	int32 HelperCount = 0;
	int32 PathRequestCount = 0;
	int32 PossessionEventCount = 0;
	int32 PathFailureCount = 0;
	int32 VisionCheckCount = 0;
	int32 LineTraceCount = 0;
	double AITickMilliseconds = 0.0;
	double DiagnosticsOverheadMilliseconds = 0.0;
	double DiagnosticsActorAIOverheadMilliseconds = 0.0;
	double DiagnosticsNavigationOverheadMilliseconds = 0.0;
	double DiagnosticsNetworkOverheadMilliseconds = 0.0;
	double DiagnosticsFrameTimingOverheadMilliseconds = 0.0;
	double DiagnosticsMemoryOverheadMilliseconds = 0.0;
	double DiagnosticsTextureRHIOverheadMilliseconds = 0.0;
	double DiagnosticsBookkeepingOverheadMilliseconds = 0.0;
	uint64 DiagnosticsBufferedMemoryBytes = 0;
	int32 NetworkConnectionCount = 0;
	bool bHasNetDriver = false;
	bool bHasNavigationData = false;
};

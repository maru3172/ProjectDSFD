// File: Source/ProjectProject01/Public/ProjectProject01DiagnosticsSubsystem.h
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#pragma once

#include "CoreMinimal.h"
#include "ProjectProject01DiagnosticsTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectProject01DiagnosticsSubsystem.generated.h"

class AActor;
class UWorld;

class PROJECTPROJECT01_API FProjectProject01DiagnosticWorkScope final
{
public:
	explicit FProjectProject01DiagnosticWorkScope(UWorld* InWorld);
	~FProjectProject01DiagnosticWorkScope();
	void AddVisionCheck(int32 Count = 1) { VisionChecks += FMath::Max(0, Count); }
	void AddLineTrace(int32 Count = 1) { LineTraces += FMath::Max(0, Count); }
private:
	UWorld* World = nullptr;
	double StartSeconds = 0.0;
	int32 VisionChecks = 0;
	int32 LineTraces = 0;
};

/**
 * Opt-in PIE/runtime diagnostic collector. It never changes gameplay, navigation, possession, or tuning.
 * Sampling is intentionally rate limited; events are buffered and written only when the capture stops.
 */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01DiagnosticsSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void StartCapture(const FString& InTestRunId, const FString& InTestName);
	void StopCapture(const FString& Outcome);
	bool IsCaptureActive() const { return bCaptureActive; }
	const FString& GetTestRunId() const { return TestRunId; }

	void RecordEvent(EProjectProject01DiagnosticSeverity Severity, const FString& Code, const FString& Message, const AActor* Actor = nullptr);

	/** Game-thread-only convenience entry point for gameplay instrumentation. It is a no-op unless a capture is active. */
	static void RecordWorldEvent(UWorld* World, EProjectProject01DiagnosticSeverity Severity, const FString& Code, const FString& Message, const AActor* Actor = nullptr);
	static bool IsCaptureActiveForWorld(const UWorld* World);
	static void AddWorkMetrics(UWorld* World, double AITickMilliseconds, int32 VisionChecks, int32 LineTraces);

private:
	void CollectSample(float DeltaTime);
	void WriteCaptureFiles(const FString& Outcome);
	void DetectObservableRisks(int32 PlayerCount, int32 MannequinCount, int32 HelperCount, bool bHasNavigationData);
	void LoadTestDefinition();
	void ValidateAppliedTuning();
	FString GetRoleLabel() const;
	FString GetRoleInstanceLabel() const;
	int32 GetLocalPlayerNumber() const;
	FString GetOutputDirectory() const;
	FString GetFileStem() const;
	static FString EscapeCsv(const FString& Value);
	static FString EscapeXml(const FString& Value);
	static FString SeverityToString(EProjectProject01DiagnosticSeverity Severity);

	bool bCaptureActive = false;
	FString TestRunId;
	FString TestName;
	FProjectProject01DiagnosticTestDefinition TestDefinition;
	double CaptureStartSeconds = 0.0;
	float SampleAccumulatorSeconds = 0.0f;
	TArray<FProjectProject01DiagnosticEvent> Events;
	TArray<FProjectProject01DiagnosticSample> Samples;
	TSet<FString> OneShotRiskKeys;
	int32 PathRequestCountSinceLastSample = 0;
	int32 PossessionEventCountSinceLastSample = 0;
	int32 PathFailureCountSinceLastSample = 0;
	int32 VisionCheckCountSinceLastSample = 0;
	int32 LineTraceCountSinceLastSample = 0;
	double AITickMillisecondsSinceLastSample = 0.0;
	int64 LastConsumedGlobalLogSequence = 0;
	TMap<FString, FString> LastActorStateSignatures;
	TMap<FString, double> StuckStartTimes;
	TMap<FString, FVector> LastActorLocations;
	TMap<FString, double> ActorCumulativeDistances;
	int32 ConsecutiveMemoryGrowthSamples = 0;
	TMap<FString, int32> RepeatedLogCounts;
	FString LastNetworkStateSignature;
};

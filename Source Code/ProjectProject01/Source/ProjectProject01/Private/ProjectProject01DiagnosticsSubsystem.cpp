// File: Source/ProjectProject01/Private/ProjectProject01DiagnosticsSubsystem.cpp
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01DiagnosticsSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/DateTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"
#include "Serialization/Csv/CsvParser.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "NavigationSystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameModeBase.h"
#include "AITypes.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "ProjectProject01TuningData.h"
#include "HelperRearGuardAIController.h"
#include "HelperRearGuardCharacter.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"
#include "MultiplayTestGameMode.h"

DEFINE_LOG_CATEGORY(LogProjectProject01Diagnostics);

FProjectProject01DiagnosticWorkScope::FProjectProject01DiagnosticWorkScope(UWorld* InWorld)
	: World(UProjectProject01DiagnosticsSubsystem::IsCaptureActiveForWorld(InWorld) ? InWorld : nullptr),
	  StartSeconds(IsValid(World) ? FPlatformTime::Seconds() : 0.0)
{
}

FProjectProject01DiagnosticWorkScope::~FProjectProject01DiagnosticWorkScope()
{
	if (IsValid(World))
	{
		UProjectProject01DiagnosticsSubsystem::AddWorkMetrics(World, (FPlatformTime::Seconds() - StartSeconds) * 1000.0, VisionChecks, LineTraces);
	}
}

namespace ProjectProject01Diagnostics
{
	constexpr float SampleIntervalSeconds = 1.0f;
	constexpr int32 MaximumBufferedLogRecords = 512;

	struct FBufferedLogRecord { int64 Sequence = 0; FString Text; ELogVerbosity::Type Verbosity = ELogVerbosity::Log; FName Category; };
	FCriticalSection BufferedLogLock;
	TArray<FBufferedLogRecord> BufferedLogs;
	int64 NextLogSequence = 1;

	bool ShouldEvaluateAuthoritativeAI(const ENetMode NetMode)
	{
		return NetMode != NM_Client;
	}

	bool IsExpectedHeartbeatTelemetry(const FName& Category, const FString& Message)
	{
		return Category == FName(TEXT("LogTemp")) && Message.StartsWith(TEXT("[Heartbeat]"), ESearchCase::CaseSensitive);
	}

	class FDiagnosticOutputDevice final : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const ELogVerbosity::Type BaseVerbosity = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			if (BaseVerbosity != ELogVerbosity::Warning && BaseVerbosity != ELogVerbosity::Error && BaseVerbosity != ELogVerbosity::Fatal)
			{
				return;
			}
			FScopeLock Lock(&BufferedLogLock);
			if (BufferedLogs.Num() >= MaximumBufferedLogRecords) BufferedLogs.RemoveAt(0, 1, EAllowShrinking::No);
			FBufferedLogRecord& Record = BufferedLogs.AddDefaulted_GetRef();
			Record.Sequence = NextLogSequence++;
			Record.Text = Text;
			Record.Verbosity = BaseVerbosity;
			Record.Category = Category;
		}
	};
	FDiagnosticOutputDevice DiagnosticOutputDevice;
	bool bOutputDeviceRegistered = false;

	FString GetMismatchedPropertyNames(UScriptStruct* Struct, const void* Expected, const void* Actual)
	{
		if (Struct == nullptr || Expected == nullptr || Actual == nullptr)
		{
			return TEXT("Unavailable");
		}
		TArray<FString> Names;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!It->Identical_InContainer(Expected, Actual, 0, PPF_None))
			{
				Names.Add(It->GetName());
			}
		}
		return Names.Num() > 0 ? FString::Join(Names, TEXT("|")) : TEXT("None");
	}

	bool LoadVerticalCsvIntoStruct(const FString& Path, UScriptStruct* Struct, void* Destination, FString& OutError)
	{
		if (Struct == nullptr || Destination == nullptr)
		{
			OutError = TEXT("Row struct or destination is null.");
			return false;
		}
		FString CsvText;
		if (!FFileHelper::LoadFileToString(CsvText, *Path))
		{
			OutError = FString::Printf(TEXT("Could not read %s"), *Path);
			return false;
		}
		const FCsvParser Parser(CsvText);
		const FCsvParser::FRows& Rows = Parser.GetRows();
		if (Rows.Num() == 0 || Rows[0].Num() < 2 || FString(Rows[0][0]) != TEXT("Field") || FString(Rows[0][1]) != TEXT("Value"))
		{
			OutError = TEXT("Expected vertical CSV header Field,Value.");
			return false;
		}

		TSet<FName> ImportedProperties;
		for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
		{
			if (Rows[RowIndex].Num() < 2)
			{
				continue;
			}
			const FString Field = Rows[RowIndex][0];
			const FString Value = Rows[RowIndex][1];
			if (Field.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			FProperty* Property = Struct->FindPropertyByName(FName(*Field));
			if (Property == nullptr)
			{
				OutError = FString::Printf(TEXT("Unknown field '%s' at CSV row %d."), *Field, RowIndex + 1);
				return false;
			}
			if (ImportedProperties.Contains(Property->GetFName()))
			{
				OutError = FString::Printf(TEXT("Duplicate field '%s' at CSV row %d."), *Field, RowIndex + 1);
				return false;
			}
			void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Destination);
			if (Property->ImportText_Direct(*Value, ValueAddress, nullptr, PPF_None) == nullptr)
			{
				OutError = FString::Printf(TEXT("Value '%s' could not be imported for field '%s'."), *Value, *Field);
				return false;
			}
			ImportedProperties.Add(Property->GetFName());
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!ImportedProperties.Contains(It->GetFName()))
			{
				OutError = FString::Printf(TEXT("CSV is missing field '%s'."), *It->GetName());
				return false;
			}
		}
		return true;
	}

	FString NetModeToString(const ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}
}

void UProjectProject01DiagnosticsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UProjectProject01DiagnosticsSubsystem::Deinitialize()
{
	if (bCaptureActive)
	{
		StopCapture(TEXT("Inconclusive: world ended before the operator stopped the diagnostic capture."));
	}
	Super::Deinitialize();
}

TStatId UProjectProject01DiagnosticsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectProject01DiagnosticsSubsystem, STATGROUP_Tickables);
}

void UProjectProject01DiagnosticsSubsystem::StartCapture(const FString& InTestRunId, const FString& InTestName)
{
	if (bCaptureActive)
	{
		UE_LOG(LogProjectProject01Diagnostics, Warning, TEXT("Diagnostics capture is already active for %s."), *TestRunId);
		return;
	}
	if (InTestRunId.IsEmpty())
	{
		ensureMsgf(false, TEXT("Diagnostics requires a non-empty TestRunId."));
		return;
	}

	TestRunId = InTestRunId;
	LoadTestDefinition();
	TestName = InTestName.IsEmpty() ? TestDefinition.Name : InTestName;
	CaptureStartSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	SampleAccumulatorSeconds = ProjectProject01Diagnostics::SampleIntervalSeconds;
	Events.Reset();
	Samples.Reset();
	OneShotRiskKeys.Reset();
	LastActorStateSignatures.Reset();
	StuckStartTimes.Reset();
	LastActorLocations.Reset();
	ActorCumulativeDistances.Reset();
	RepeatedLogCounts.Reset();
	LastNetworkStateSignature.Reset();
	ConsecutiveMemoryGrowthSamples = 0;
	PathRequestCountSinceLastSample = 0;
	PathFailureCountSinceLastSample = 0;
	PossessionEventCountSinceLastSample = 0;
	VisionCheckCountSinceLastSample = 0;
	LineTraceCountSinceLastSample = 0;
	AITickMillisecondsSinceLastSample = 0.0;
	bCaptureActive = true;
	if (!ProjectProject01Diagnostics::bOutputDeviceRegistered && GLog != nullptr)
	{
		GLog->AddOutputDevice(&ProjectProject01Diagnostics::DiagnosticOutputDevice);
		ProjectProject01Diagnostics::bOutputDeviceRegistered = true;
	}
	{
		FScopeLock Lock(&ProjectProject01Diagnostics::BufferedLogLock);
		LastConsumedGlobalLogSequence = ProjectProject01Diagnostics::NextLogSequence - 1;
	}
	RecordEvent(EProjectProject01DiagnosticSeverity::Normal, TEXT("CaptureStarted"), TEXT("Lightweight diagnostic capture started."));
	ValidateAppliedTuning();
}

void UProjectProject01DiagnosticsSubsystem::StopCapture(const FString& Outcome)
{
	if (!bCaptureActive)
	{
		return;
	}
	FString FinalOutcome = Outcome;
	const double ElapsedSeconds = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() - CaptureStartSeconds : 0.0;
	int32 RiskCount = 0;
	int32 WarningCount = 0;
	for (const FProjectProject01DiagnosticEvent& Event : Events)
	{
		RiskCount += Event.Severity == EProjectProject01DiagnosticSeverity::Risk ? 1 : 0;
		WarningCount += Event.Severity == EProjectProject01DiagnosticSeverity::Warning ? 1 : 0;
	}
	float MinimumFps = TNumericLimits<float>::Max();
	int32 MaximumPathFailures = 0;
	double MaximumMemoryGrowthMiB = 0.0;
	uint32 MaximumBandwidth = 0;
	for (const FProjectProject01DiagnosticSample& Sample : Samples)
	{
		MinimumFps = FMath::Min(MinimumFps, Sample.FramesPerSecond);
		MaximumPathFailures = FMath::Max(MaximumPathFailures, Sample.PathFailureCount);
		MaximumMemoryGrowthMiB = FMath::Max(MaximumMemoryGrowthMiB, Sample.MemoryBytesPerSecondDelta / (1024.0 * 1024.0));
		MaximumBandwidth = FMath::Max(MaximumBandwidth, FMath::Max(Sample.InBytesPerSecond, Sample.OutBytesPerSecond));
	}
	bool bCriteriaRecognized = false;
	bool bCriteriaPassed = true;
	TArray<FString> CriteriaDetails;
	TArray<FString> Rules;
	TestDefinition.FailureCriteria.ParseIntoArray(Rules, TEXT(";"), true);
	for (FString Rule : Rules)
	{
		Rule.TrimStartAndEndInline();
		double Threshold = 0.0;
		FString Key;
		FString Operator;
		int32 OperatorIndex = Rule.Find(TEXT("=="));
		if (OperatorIndex != INDEX_NONE) Operator = TEXT("==");
		else if ((OperatorIndex = Rule.Find(TEXT("<="))) != INDEX_NONE) Operator = TEXT("<=");
		else if ((OperatorIndex = Rule.Find(TEXT(">="))) != INDEX_NONE) Operator = TEXT(">=");
		if (OperatorIndex == INDEX_NONE || !LexTryParseString(Threshold, *Rule.Mid(OperatorIndex + 2).TrimStartAndEnd()))
		{
			continue;
		}
		Key = Rule.Left(OperatorIndex).TrimStartAndEnd();
		double Actual = 0.0;
		if (Key.Equals(TEXT("RiskCount"), ESearchCase::IgnoreCase)) Actual = RiskCount;
		else if (Key.Equals(TEXT("WarningCount"), ESearchCase::IgnoreCase)) Actual = WarningCount;
		else if (Key.Equals(TEXT("MinFPS"), ESearchCase::IgnoreCase)) Actual = Samples.Num() > 0 ? MinimumFps : 0.0;
		else if (Key.Equals(TEXT("MaxPathFailures"), ESearchCase::IgnoreCase)) Actual = MaximumPathFailures;
		else if (Key.Equals(TEXT("MaxMemoryGrowthMiBPerSecond"), ESearchCase::IgnoreCase)) Actual = MaximumMemoryGrowthMiB;
		else if (Key.Equals(TEXT("MaxBandwidthBytesPerSecond"), ESearchCase::IgnoreCase)) Actual = MaximumBandwidth;
		else continue;
		bCriteriaRecognized = true;
		const bool bRulePassed = Operator == TEXT("==") ? FMath::IsNearlyEqual(Actual, Threshold) : Operator == TEXT("<=") ? Actual <= Threshold : Actual >= Threshold;
		bCriteriaPassed &= bRulePassed;
		CriteriaDetails.Add(FString::Printf(TEXT("%s actual=%.3f expected%s%.3f passed=%s"), *Key, Actual, *Operator, Threshold, bRulePassed ? TEXT("true") : TEXT("false")));
	}
	RecordEvent(bCriteriaRecognized && bCriteriaPassed ? EProjectProject01DiagnosticSeverity::Normal : EProjectProject01DiagnosticSeverity::Inconclusive,
		TEXT("FailureCriteriaEvaluation"), FString::Join(CriteriaDetails, TEXT("; ")));
	if (TestDefinition.TimeoutSeconds > 0.0f && ElapsedSeconds >= TestDefinition.TimeoutSeconds)
	{
		FinalOutcome = TEXT("TimedOut");
	}
	else if (RiskCount > 0 || (bCriteriaRecognized && !bCriteriaPassed))
	{
		FinalOutcome = TEXT("ReviewRequired");
	}
	else if (bCriteriaRecognized && bCriteriaPassed && Samples.Num() > 0)
	{
		FinalOutcome = TEXT("Pass");
	}
	else
	{
		FinalOutcome = TEXT("Inconclusive");
	}
	RecordEvent(EProjectProject01DiagnosticSeverity::Normal, TEXT("CaptureStopped"), FinalOutcome);
	WriteCaptureFiles(FinalOutcome);
	bCaptureActive = false;
}

void UProjectProject01DiagnosticsSubsystem::Tick(float DeltaTime)
{
	if (!bCaptureActive || !IsValid(GetWorld()))
	{
		return;
	}

	SampleAccumulatorSeconds += FMath::Max(0.0f, DeltaTime);
	if (SampleAccumulatorSeconds >= ProjectProject01Diagnostics::SampleIntervalSeconds)
	{
		CollectSample(DeltaTime);
		SampleAccumulatorSeconds = 0.0f;
	}
	TArray<ProjectProject01Diagnostics::FBufferedLogRecord> NewRecords;
	{
		FScopeLock Lock(&ProjectProject01Diagnostics::BufferedLogLock);
		for (const ProjectProject01Diagnostics::FBufferedLogRecord& Record : ProjectProject01Diagnostics::BufferedLogs)
		{
			if (Record.Sequence > LastConsumedGlobalLogSequence) NewRecords.Add(Record);
		}
	}
	for (const ProjectProject01Diagnostics::FBufferedLogRecord& Record : NewRecords)
	{
		LastConsumedGlobalLogSequence = Record.Sequence;
		const FString Category = Record.Category.ToString();
		if (ProjectProject01Diagnostics::IsExpectedHeartbeatTelemetry(Record.Category, Record.Text))
		{
			continue;
		}
		const bool bNetworkLog = Category.Contains(TEXT("Net")) || Record.Text.Contains(TEXT("RPC")) || Record.Text.Contains(TEXT("replic"), ESearchCase::IgnoreCase);
		const FString LogKey = Category + TEXT("|") + Record.Text;
		const int32 Count = ++RepeatedLogCounts.FindOrAdd(LogKey);
		const EProjectProject01DiagnosticSeverity Severity = Record.Verbosity == ELogVerbosity::Error || Record.Verbosity == ELogVerbosity::Fatal || Count >= 3
			? EProjectProject01DiagnosticSeverity::Risk : EProjectProject01DiagnosticSeverity::Warning;
		RecordEvent(Severity, bNetworkLog ? TEXT("NetworkRPCOrReplicationWarning") : TEXT("EngineWarningOrError"),
			FString::Printf(TEXT("Category=%s; RepeatCount=%d; %s"), *Category, Count, *Record.Text));
	}
}

void UProjectProject01DiagnosticsSubsystem::RecordEvent(
	const EProjectProject01DiagnosticSeverity Severity,
	const FString& Code,
	const FString& Message,
	const AActor* Actor)
{
	if (!bCaptureActive)
	{
		return;
	}
	FProjectProject01DiagnosticEvent& Event = Events.AddDefaulted_GetRef();
	Event.UtcTime = FDateTime::UtcNow().ToIso8601();
	Event.GameSeconds = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() - CaptureStartSeconds : 0.0;
	Event.Severity = Severity;
	Event.Code = Code;
	Event.Message = Message;
	Event.ActorName = IsValid(Actor) ? Actor->GetPathName() : TEXT("");
}

void UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(
	UWorld* World,
	const EProjectProject01DiagnosticSeverity Severity,
	const FString& Code,
	const FString& Message,
	const AActor* Actor)
{
	if (!IsValid(World))
	{
		return;
	}
	UProjectProject01DiagnosticsSubsystem* Subsystem = World->GetSubsystem<UProjectProject01DiagnosticsSubsystem>();
	if (!IsValid(Subsystem) || !Subsystem->IsCaptureActive())
	{
		return;
	}
	if (Code.Contains(TEXT("PathRequest")))
	{
		++Subsystem->PathRequestCountSinceLastSample;
		if (Message.Contains(TEXT("Result=0"))) ++Subsystem->PathFailureCountSinceLastSample;
	}
	if (Code.Contains(TEXT("Possess")))
	{
		++Subsystem->PossessionEventCountSinceLastSample;
	}
	Subsystem->RecordEvent(Severity, Code, Message, Actor);
}

bool UProjectProject01DiagnosticsSubsystem::IsCaptureActiveForWorld(const UWorld* World)
{
	const UProjectProject01DiagnosticsSubsystem* Subsystem = IsValid(World) ? World->GetSubsystem<UProjectProject01DiagnosticsSubsystem>() : nullptr;
	return IsValid(Subsystem) && Subsystem->IsCaptureActive();
}

void UProjectProject01DiagnosticsSubsystem::AddWorkMetrics(UWorld* World, const double AITickMilliseconds, const int32 VisionChecks, const int32 LineTraces)
{
	UProjectProject01DiagnosticsSubsystem* Subsystem = IsValid(World) ? World->GetSubsystem<UProjectProject01DiagnosticsSubsystem>() : nullptr;
	if (!IsValid(Subsystem) || !Subsystem->IsCaptureActive()) return;
	Subsystem->AITickMillisecondsSinceLastSample += FMath::Max(0.0, AITickMilliseconds);
	Subsystem->VisionCheckCountSinceLastSample += FMath::Max(0, VisionChecks);
	Subsystem->LineTraceCountSinceLastSample += FMath::Max(0, LineTraces);
}

void UProjectProject01DiagnosticsSubsystem::CollectSample(const float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	int32 PlayerCount = 0;
	int32 MannequinCount = 0;
	int32 HelperCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		const FString ClassName = Actor->GetClass()->GetName();
		PlayerCount += ClassName.Contains(TEXT("PlayerCharacter")) ? 1 : 0;
		MannequinCount += ClassName.Contains(TEXT("Mannequin")) ? 1 : 0;
		HelperCount += ClassName.Contains(TEXT("Helper")) ? 1 : 0;

		const FString ActorKey = Actor->GetPathName();
		const FVector CurrentActorLocation = Actor->GetActorLocation();
		if (const FVector* PreviousLocation = LastActorLocations.Find(ActorKey))
		{
			ActorCumulativeDistances.FindOrAdd(ActorKey) += FVector::Dist2D(*PreviousLocation, CurrentActorLocation);
		}
		LastActorLocations.Add(ActorKey, CurrentActorLocation);
		const double CumulativeDistance = ActorCumulativeDistances.FindRef(ActorKey);
		FString StateSignature;
		if (const AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(Actor))
		{
			const FVector Location = Mannequin->GetActorLocation();
			const AAIController* AIController = Cast<AAIController>(Mannequin->GetController());
			const UBlackboardComponent* Blackboard = IsValid(AIController) ? AIController->GetBlackboardComponent() : nullptr;
			const AActor* TargetActor = IsValid(Blackboard) ? Cast<AActor>(Blackboard->GetValueAsObject(TEXT("TargetActor"))) : nullptr;
			const FVector RoamingLocation = IsValid(Blackboard) ? Blackboard->GetValueAsVector(TEXT("RoamingLocation")) : FVector::ZeroVector;
			const bool bHasRoamingLocation = IsValid(Blackboard) && FAISystem::IsValidLocation(RoamingLocation);
			const bool bHasTarget = IsValid(TargetActor) || bHasRoamingLocation;
			const FVector DiagnosticTargetLocation = IsValid(TargetActor)
				? TargetActor->GetActorLocation()
				: (bHasRoamingLocation ? RoamingLocation : FVector::ZeroVector);
			StateSignature = FString::Printf(TEXT("Mannequin;Slot=%d;Manual=%d;Frozen=%d;Command=%s;Controller=%s;TargetActor=%s;TargetLocation=(%.1f,%.1f,%.1f);Location=(%.1f,%.1f,%.1f);Velocity=%.1f;Distance=%.1f"),
				Mannequin->GetControlSlot(), Mannequin->IsManualControlEnabled(), Mannequin->IsFrozenBySurvivorVision(), *Mannequin->GetDiagnosticCommandState(World->GetTimeSeconds()),
				*GetNameSafe(Mannequin->GetController()), *GetNameSafe(TargetActor), DiagnosticTargetLocation.X, DiagnosticTargetLocation.Y, DiagnosticTargetLocation.Z,
				Location.X, Location.Y, Location.Z, Mannequin->GetVelocity().Size2D(), CumulativeDistance);
			if (Mannequin->HasAuthority() && !IsValid(Mannequin->GetController()) && !OneShotRiskKeys.Contains(TEXT("NoController_") + ActorKey)) { OneShotRiskKeys.Add(TEXT("NoController_") + ActorKey); RecordEvent(EProjectProject01DiagnosticSeverity::Risk, TEXT("AIControllerMissing"), TEXT("Authoritative mannequin has no controller."), Mannequin); }
			const bool bShouldBeMoving = bHasTarget && IsValid(AIController) && !Mannequin->IsManualControlEnabled() && !Mannequin->IsFrozenBySurvivorVision() && !Mannequin->ShouldHoldPostPossessionCommand(World->GetTimeSeconds());
			if (bShouldBeMoving && Mannequin->GetVelocity().Size2D() < 5.0f)
			{
				double& StuckStart = StuckStartTimes.FindOrAdd(ActorKey, World->GetTimeSeconds());
				if (World->GetTimeSeconds() - StuckStart >= 3.0 && !OneShotRiskKeys.Contains(TEXT("Stuck_") + ActorKey))
				{
					OneShotRiskKeys.Add(TEXT("Stuck_") + ActorKey);
					RecordEvent(EProjectProject01DiagnosticSeverity::Risk, TEXT("AIStuckWithTarget"), FString::Printf(TEXT("Target=%s; TargetLocation=(%.1f,%.1f,%.1f); Speed=%.1f; StuckSeconds=%.1f"),
						*GetNameSafe(TargetActor), DiagnosticTargetLocation.X, DiagnosticTargetLocation.Y, DiagnosticTargetLocation.Z, Mannequin->GetVelocity().Size2D(), World->GetTimeSeconds() - StuckStart), Mannequin);
				}
			}
			else { StuckStartTimes.Remove(ActorKey); OneShotRiskKeys.Remove(TEXT("Stuck_") + ActorKey); }
		}
		else if (const AHelperRearGuardCharacter* Helper = Cast<AHelperRearGuardCharacter>(Actor))
		{
			FVector Target = FVector::ZeroVector; bool bHasTarget = false; bool bWaiting = false;
			if (const AHelperRearGuardAIController* Controller = Cast<AHelperRearGuardAIController>(Helper->GetController())) Controller->GetDiagnosticMoveState(Target, bHasTarget, bWaiting);
			const FVector Location = Helper->GetActorLocation();
			FVector ViewDirection; const bool bHasView = Helper->GetGuardViewDirection(ViewDirection);
			StateSignature = FString::Printf(TEXT("Helper;Target=%d;TargetLocation=(%.1f,%.1f,%.1f);Waiting=%d;Retreat=%d;View=(%.3f,%.3f,%.3f);Controller=%s;Location=(%.1f,%.1f,%.1f);Velocity=%.1f;Distance=%.1f"), bHasTarget, Target.X, Target.Y, Target.Z, bWaiting, Helper->IsDiagnosticRetreatLocked(), bHasView ? ViewDirection.X : 0.0f, bHasView ? ViewDirection.Y : 0.0f, bHasView ? ViewDirection.Z : 0.0f, *GetNameSafe(Helper->GetController()), Location.X, Location.Y, Location.Z, Helper->GetVelocity().Size2D(), CumulativeDistance);
			if (Helper->HasAuthority() && !IsValid(Helper->GetController()) && !OneShotRiskKeys.Contains(TEXT("NoController_") + ActorKey)) { OneShotRiskKeys.Add(TEXT("NoController_") + ActorKey); RecordEvent(EProjectProject01DiagnosticSeverity::Risk, TEXT("AIControllerMissing"), TEXT("Authoritative helper has no controller."), Helper); }
			if (bHasTarget && !bWaiting && Helper->GetVelocity().Size2D() < 5.0f)
			{
				double& StuckStart = StuckStartTimes.FindOrAdd(ActorKey, World->GetTimeSeconds());
				if (World->GetTimeSeconds() - StuckStart >= 3.0 && !OneShotRiskKeys.Contains(TEXT("Stuck_") + ActorKey)) { OneShotRiskKeys.Add(TEXT("Stuck_") + ActorKey); RecordEvent(EProjectProject01DiagnosticSeverity::Risk, TEXT("AIStuckWithTarget"), FString::Printf(TEXT("Target exists but horizontal speed stayed below 5 cm/s for %.1f seconds."), World->GetTimeSeconds() - StuckStart), Helper); }
			}
			else { StuckStartTimes.Remove(ActorKey); OneShotRiskKeys.Remove(TEXT("Stuck_") + ActorKey); }
		}
		else if (ClassName.Contains(TEXT("PlayerCharacter")))
		{
			StateSignature = FString::Printf(TEXT("Player;Controller=%s;Location=(%.1f,%.1f,%.1f);Velocity=%.1f;Distance=%.1f"), *GetNameSafe(Cast<APawn>(Actor) ? Cast<APawn>(Actor)->GetController() : nullptr), CurrentActorLocation.X, CurrentActorLocation.Y, CurrentActorLocation.Z, Actor->GetVelocity().Size2D(), CumulativeDistance);
		}
		if (!StateSignature.IsEmpty() && LastActorStateSignatures.FindRef(ActorKey) != StateSignature)
		{
			LastActorStateSignatures.Add(ActorKey, StateSignature);
			RecordEvent(EProjectProject01DiagnosticSeverity::Normal, TEXT("ActorStateChanged"), StateSignature, Actor);
		}
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const bool bHasNavigationData = IsValid(NavigationSystem) && NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr;
	UNetDriver* NetDriver = World->GetNetDriver();
	const int32 NetworkConnectionCount = IsValid(NetDriver) ? NetDriver->ClientConnections.Num() + (IsValid(NetDriver->ServerConnection) ? 1 : 0) : 0;
	const FString NetworkStateSignature = FString::Printf(TEXT("HasNetDriver=%s; Driver=%s; ClientConnections=%d; HasServerConnection=%s; State=%s"),
		IsValid(NetDriver) ? TEXT("true") : TEXT("false"), *GetNameSafe(NetDriver), IsValid(NetDriver) ? NetDriver->ClientConnections.Num() : 0,
		IsValid(NetDriver) && IsValid(NetDriver->ServerConnection) ? TEXT("true") : TEXT("false"),
		IsValid(NetDriver) ? TEXT("ConnectedOrListening") : (World->GetNetMode() == NM_Standalone ? TEXT("Standalone") : TEXT("Unavailable")));
	if (NetworkStateSignature != LastNetworkStateSignature)
	{
		LastNetworkStateSignature = NetworkStateSignature;
		RecordEvent(World->GetNetMode() != NM_Standalone && !IsValid(NetDriver) ? EProjectProject01DiagnosticSeverity::Risk : EProjectProject01DiagnosticSeverity::Normal,
			TEXT("NetworkConnectionStateChanged"), NetworkStateSignature);
	}

	FProjectProject01DiagnosticSample& Sample = Samples.AddDefaulted_GetRef();
	Sample.UtcTime = FDateTime::UtcNow().ToIso8601();
	Sample.GameSeconds = World->GetTimeSeconds() - CaptureStartSeconds;
	Sample.FrameMilliseconds = FMath::Max(0.0f, DeltaTime) * 1000.0f;
	Sample.FramesPerSecond = Sample.FrameMilliseconds > KINDA_SMALL_NUMBER ? 1000.0f / Sample.FrameMilliseconds : 0.0f;
	Sample.GameThreadMilliseconds = FPlatformTime::ToMilliseconds(GGameThreadTime);
	Sample.DrawThreadMilliseconds = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	Sample.GpuMilliseconds = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
	Sample.UsedPhysicalBytes = FPlatformMemory::GetStats().UsedPhysical;
	if (const FProjectProject01DiagnosticSample* PreviousSample = Samples.Num() > 1 ? &Samples[Samples.Num() - 2] : nullptr)
	{
		const double TimeDelta = FMath::Max(0.001, Sample.GameSeconds - PreviousSample->GameSeconds);
		Sample.MemoryBytesPerSecondDelta = static_cast<double>(Sample.UsedPhysicalBytes) / TimeDelta - static_cast<double>(PreviousSample->UsedPhysicalBytes) / TimeDelta;
		ConsecutiveMemoryGrowthSamples = Sample.MemoryBytesPerSecondDelta > 4.0 * 1024.0 * 1024.0 ? ConsecutiveMemoryGrowthSamples + 1 : 0;
		if (ConsecutiveMemoryGrowthSamples >= 5 && !OneShotRiskKeys.Contains(TEXT("SustainedMemoryGrowth")))
		{
			OneShotRiskKeys.Add(TEXT("SustainedMemoryGrowth"));
			RecordEvent(EProjectProject01DiagnosticSeverity::Risk, TEXT("SustainedMemoryGrowth"), FString::Printf(TEXT("Process physical memory grew above 4 MiB/s for %d consecutive samples; latest %.2f MiB/s."), ConsecutiveMemoryGrowthSamples, Sample.MemoryBytesPerSecondDelta / (1024.0 * 1024.0)));
		}
	}
	Sample.InBytesPerSecond = IsValid(NetDriver) ? NetDriver->InBytesPerSecond : 0;
	Sample.OutBytesPerSecond = IsValid(NetDriver) ? NetDriver->OutBytesPerSecond : 0;
	Sample.PlayerCount = PlayerCount;
	Sample.MannequinCount = MannequinCount;
	Sample.HelperCount = HelperCount;
	Sample.PathRequestCount = PathRequestCountSinceLastSample;
	Sample.PossessionEventCount = PossessionEventCountSinceLastSample;
	Sample.PathFailureCount = PathFailureCountSinceLastSample;
	Sample.VisionCheckCount = VisionCheckCountSinceLastSample;
	Sample.LineTraceCount = LineTraceCountSinceLastSample;
	Sample.AITickMilliseconds = AITickMillisecondsSinceLastSample;
	Sample.NetworkConnectionCount = NetworkConnectionCount;
	Sample.bHasNetDriver = IsValid(NetDriver);
	Sample.bHasNavigationData = bHasNavigationData;
	if (Samples.Num() > 1)
	{
		const FProjectProject01DiagnosticSample& Previous = Samples[Samples.Num() - 2];
		const bool bBandwidthSpike = (Sample.InBytesPerSecond > 65536 && Sample.InBytesPerSecond > Previous.InBytesPerSecond * 4) || (Sample.OutBytesPerSecond > 65536 && Sample.OutBytesPerSecond > Previous.OutBytesPerSecond * 4);
		if (bBandwidthSpike) RecordEvent(EProjectProject01DiagnosticSeverity::Warning, TEXT("NetworkBandwidthSpike"), FString::Printf(TEXT("In=%u B/s Out=%u B/s PreviousIn=%u PreviousOut=%u"), Sample.InBytesPerSecond, Sample.OutBytesPerSecond, Previous.InBytesPerSecond, Previous.OutBytesPerSecond));
	}
	if (Sample.PathFailureCount >= 3) RecordEvent(EProjectProject01DiagnosticSeverity::Risk, TEXT("RepeatedPathFailures"), FString::Printf(TEXT("%d path requests failed during the latest sample."), Sample.PathFailureCount));
	if (Sample.AITickMilliseconds > 8.0 || Sample.LineTraceCount > 1000) RecordEvent(EProjectProject01DiagnosticSeverity::Warning, TEXT("AIWorkloadSpike"), FString::Printf(TEXT("AITickMs=%.3f VisionChecks=%d LineTraces=%d"), Sample.AITickMilliseconds, Sample.VisionCheckCount, Sample.LineTraceCount));
	RecordEvent(EProjectProject01DiagnosticSeverity::Normal, TEXT("SamplingCounters"), FString::Printf(
		TEXT("PathRequests=%d; PossessionEvents=%d"), PathRequestCountSinceLastSample, PossessionEventCountSinceLastSample));
	PathRequestCountSinceLastSample = 0;
	PossessionEventCountSinceLastSample = 0;
	PathFailureCountSinceLastSample = 0; VisionCheckCountSinceLastSample = 0; LineTraceCountSinceLastSample = 0; AITickMillisecondsSinceLastSample = 0.0;
	DetectObservableRisks(PlayerCount, MannequinCount, HelperCount, bHasNavigationData);
}

void UProjectProject01DiagnosticsSubsystem::DetectObservableRisks(const int32 PlayerCount, const int32 MannequinCount, const int32 HelperCount, const bool bHasNavigationData)
{
	const UWorld* World = GetWorld();
	const bool bShouldEvaluateAuthoritativeAI = IsValid(World) && ProjectProject01Diagnostics::ShouldEvaluateAuthoritativeAI(World->GetNetMode());
	if (bShouldEvaluateAuthoritativeAI && (MannequinCount > 0 || HelperCount > 0) && !bHasNavigationData && !OneShotRiskKeys.Contains(TEXT("MissingNavData")))
	{
		OneShotRiskKeys.Add(TEXT("MissingNavData"));
		RecordEvent(EProjectProject01DiagnosticSeverity::Risk, TEXT("MissingNavData"), TEXT("AI-like actors exist but no navigation data was found in this world."));
	}
	if (PlayerCount == 0 && !OneShotRiskKeys.Contains(TEXT("NoPlayerCharacter")))
	{
		OneShotRiskKeys.Add(TEXT("NoPlayerCharacter"));
		RecordEvent(EProjectProject01DiagnosticSeverity::Inconclusive, TEXT("NoPlayerCharacter"), TEXT("No actor whose class name contains PlayerCharacter was observed. This can be valid for a pre-login server world."));
	}
}

void UProjectProject01DiagnosticsSubsystem::LoadTestDefinition()
{
	TestDefinition = FProjectProject01DiagnosticTestDefinition();
	if (GConfig == nullptr)
	{
		return;
	}
	const TCHAR* Section = TEXT("ProjectProject01Diagnostics.TestDefinition");
	GConfig->GetString(Section, TEXT("Name"), TestDefinition.Name, GGameIni);
	GConfig->GetString(Section, TEXT("Purpose"), TestDefinition.Purpose, GGameIni);
	GConfig->GetString(Section, TEXT("ReproductionSteps"), TestDefinition.ReproductionSteps, GGameIni);
	GConfig->GetString(Section, TEXT("ExpectedResult"), TestDefinition.ExpectedResult, GGameIni);
	GConfig->GetString(Section, TEXT("FailureCriteria"), TestDefinition.FailureCriteria, GGameIni);
	GConfig->GetFloat(Section, TEXT("TimeoutSeconds"), TestDefinition.TimeoutSeconds, GGameIni);
	TestDefinition.TimeoutSeconds = FMath::Max(0.0f, TestDefinition.TimeoutSeconds);
}

void UProjectProject01DiagnosticsSubsystem::ValidateAppliedTuning()
{
	UWorld* World = GetWorld();
	UProjectProject01TuningSubsystem* TuningSubsystem = IsValid(World) ? World->GetSubsystem<UProjectProject01TuningSubsystem>() : nullptr;
	if (!IsValid(TuningSubsystem))
	{
		RecordEvent(EProjectProject01DiagnosticSeverity::Inconclusive, TEXT("TuningSubsystemUnavailable"), TEXT("Cannot compare DataTables because the tuning subsystem is unavailable."));
		return;
	}

	const auto CompareDefaultRow = [this](const TCHAR* Label, const TCHAR* ObjectPath, const TCHAR* CsvFileName,
		UScriptStruct* RowStruct, const void* CachedRow, const bool bHasCachedRow, void* CsvRow)
	{
		UDataTable* Table = LoadObject<UDataTable>(nullptr, ObjectPath);
		const uint8* DataTableRow = IsValid(Table) ? Table->FindRowUnchecked(TEXT("Default")) : nullptr;
		if (!bHasCachedRow || DataTableRow == nullptr || RowStruct == nullptr)
		{
			RecordEvent(EProjectProject01DiagnosticSeverity::Risk, TEXT("TuningDataTableUnavailable"), FString::Printf(TEXT("%s tuning cache or DataTable Default row is unavailable."), Label));
			return;
		}
		const bool bMatches = RowStruct->CompareScriptStruct(DataTableRow, CachedRow, PPF_None);
		RecordEvent(bMatches ? EProjectProject01DiagnosticSeverity::Normal : EProjectProject01DiagnosticSeverity::Risk,
			bMatches ? TEXT("TuningCacheMatchesDataTable") : TEXT("TuningCacheMismatch"),
			FString::Printf(TEXT("Layer=DataTableToRuntimeCache; Tuning=%s; Matches=%s; MismatchedFields=%s"), Label,
				bMatches ? TEXT("true") : TEXT("false"), *ProjectProject01Diagnostics::GetMismatchedPropertyNames(RowStruct, DataTableRow, CachedRow)));

		FString CsvError;
		const FString CsvPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MyProject"), TEXT("Data"), CsvFileName);
		if (!ProjectProject01Diagnostics::LoadVerticalCsvIntoStruct(CsvPath, RowStruct, CsvRow, CsvError))
		{
			RecordEvent(EProjectProject01DiagnosticSeverity::Risk, TEXT("TuningCsvInvalid"),
				FString::Printf(TEXT("Layer=CsvToDataTable; Tuning=%s; Error=%s"), Label, *CsvError));
			return;
		}
		const bool bCsvMatches = RowStruct->CompareScriptStruct(DataTableRow, CsvRow, PPF_None);
		RecordEvent(bCsvMatches ? EProjectProject01DiagnosticSeverity::Normal : EProjectProject01DiagnosticSeverity::Risk,
			bCsvMatches ? TEXT("TuningCsvMatchesDataTable") : TEXT("TuningCsvDataTableMismatch"),
			FString::Printf(TEXT("Layer=CsvToDataTable; Tuning=%s; Matches=%s; MismatchedFields=%s"), Label,
				bCsvMatches ? TEXT("true") : TEXT("false"), *ProjectProject01Diagnostics::GetMismatchedPropertyNames(RowStruct, DataTableRow, CsvRow)));
	};

	FPlayerTuningRow PlayerRow;
	FMannequinAITuningRow MannequinRow;
	FHelperTuningRow HelperRow;
	const bool bHasPlayerRow = TuningSubsystem->GetPlayerTuning(PlayerRow);
	const bool bHasMannequinRow = TuningSubsystem->GetMannequinTuning(MannequinRow);
	const bool bHasHelperRow = TuningSubsystem->GetHelperTuning(HelperRow);
	FPlayerTuningRow CsvPlayerRow;
	FMannequinAITuningRow CsvMannequinRow;
	FHelperTuningRow CsvHelperRow;
	CompareDefaultRow(TEXT("Player"), TEXT("/Game/MyProject/Data/DT_PlayerTuning.DT_PlayerTuning"), TEXT("Player.csv"), FPlayerTuningRow::StaticStruct(), &PlayerRow, bHasPlayerRow, &CsvPlayerRow);
	CompareDefaultRow(TEXT("Mannequin"), TEXT("/Game/MyProject/Data/DT_AITuning.DT_AITuning"), TEXT("AITuning.csv"), FMannequinAITuningRow::StaticStruct(), &MannequinRow, bHasMannequinRow, &CsvMannequinRow);
	CompareDefaultRow(TEXT("Helper"), TEXT("/Game/MyProject/Data/DT_HelperTuning.DT_HelperTuning"), TEXT("HelperTuning.csv"), FHelperTuningRow::StaticStruct(), &HelperRow, bHasHelperRow, &CsvHelperRow);

	for (TActorIterator<APlayerCharacter> It(World); It && bHasPlayerRow; ++It)
	{
		FPlayerTuningRow Actual;
		It->GetDiagnosticAppliedPlayerTuning(Actual);
		const bool bMatches = FPlayerTuningRow::StaticStruct()->CompareScriptStruct(&PlayerRow, &Actual, PPF_None);
		RecordEvent(bMatches ? EProjectProject01DiagnosticSeverity::Normal : EProjectProject01DiagnosticSeverity::Risk,
			bMatches ? TEXT("ActorTuningMatches") : TEXT("ActorTuningMismatch"),
			FString::Printf(TEXT("Layer=RuntimeCacheToActor; Tuning=Player; MismatchedFields=%s"),
				*ProjectProject01Diagnostics::GetMismatchedPropertyNames(FPlayerTuningRow::StaticStruct(), &PlayerRow, &Actual)), *It);
		if (bHasMannequinRow)
		{
			TArray<FString> Mismatches;
			if (!FMath::IsNearlyEqual(It->GetDirectChaseRadius(), MannequinRow.DirectChaseRadius)) Mismatches.Add(TEXT("DirectChaseRadius"));
			if (!FMath::IsNearlyEqual(It->GetRoamingOuterRadius(), MannequinRow.RoamingOuterRadius)) Mismatches.Add(TEXT("RoamingOuterRadius"));
			if (!FMath::IsNearlyEqual(It->GetDirectChaseHalfAngleDegrees(), MannequinRow.DirectChaseHalfAngleDegrees)) Mismatches.Add(TEXT("DirectChaseHalfAngleDegrees"));
			if (!FMath::IsNearlyEqual(It->GetDirectChaseSectorRadius(), MannequinRow.DirectChaseSectorRadius)) Mismatches.Add(TEXT("DirectChaseSectorRadius"));
			RecordEvent(Mismatches.Num() == 0 ? EProjectProject01DiagnosticSeverity::Normal : EProjectProject01DiagnosticSeverity::Risk,
				Mismatches.Num() == 0 ? TEXT("ActorTuningMatches") : TEXT("ActorTuningMismatch"),
				FString::Printf(TEXT("Layer=RuntimeCacheToActor; Tuning=PlayerMannequinRanges; MismatchedFields=%s"),
					Mismatches.Num() == 0 ? TEXT("None") : *FString::Join(Mismatches, TEXT("|"))), *It);
		}
	}
	for (TActorIterator<AHelperRearGuardCharacter> It(World); It && bHasHelperRow; ++It)
	{
		FHelperTuningRow Actual;
		It->GetDiagnosticAppliedTuning(Actual);
		const bool bMatches = FHelperTuningRow::StaticStruct()->CompareScriptStruct(&HelperRow, &Actual, PPF_None);
		RecordEvent(bMatches ? EProjectProject01DiagnosticSeverity::Normal : EProjectProject01DiagnosticSeverity::Risk,
			bMatches ? TEXT("ActorTuningMatches") : TEXT("ActorTuningMismatch"),
			FString::Printf(TEXT("Layer=RuntimeCacheToActor; Tuning=Helper; MismatchedFields=%s"),
				*ProjectProject01Diagnostics::GetMismatchedPropertyNames(FHelperTuningRow::StaticStruct(), &HelperRow, &Actual)), *It);
	}
	for (TActorIterator<AMannequinAICharacter> It(World); It && bHasMannequinRow; ++It)
	{
		FMannequinAITuningRow Actual;
		It->GetDiagnosticAppliedTuning(Actual);
		TArray<FString> Mismatches;
		if (!FMath::IsNearlyEqual(Actual.MannequinWalkSpeed, MannequinRow.MannequinWalkSpeed)) Mismatches.Add(TEXT("MannequinWalkSpeed"));
		if (!FMath::IsNearlyEqual(Actual.PostPossessionCommandDurationSeconds, MannequinRow.PostPossessionCommandDurationSeconds)) Mismatches.Add(TEXT("PostPossessionCommandDurationSeconds"));
		RecordEvent(Mismatches.Num() == 0 ? EProjectProject01DiagnosticSeverity::Normal : EProjectProject01DiagnosticSeverity::Risk,
			Mismatches.Num() == 0 ? TEXT("ActorTuningMatches") : TEXT("ActorTuningMismatch"),
			FString::Printf(TEXT("Layer=RuntimeCacheToActor; Tuning=Mannequin; ComparedCopiedFields=MannequinWalkSpeed|PostPossessionCommandDurationSeconds; MismatchedFields=%s; RemainingFields=ReadFromRuntimeCache"),
				Mismatches.Num() == 0 ? TEXT("None") : *FString::Join(Mismatches, TEXT("|"))), *It);
	}
	if (const AMultiplayTestGameMode* GameMode = World->GetAuthGameMode<AMultiplayTestGameMode>(); IsValid(GameMode) && bHasMannequinRow)
	{
		float ActualInterval = 0.0f;
		float ActualHalfAngle = 0.0f;
		GameMode->GetDiagnosticSurvivorVisionTuning(ActualInterval, ActualHalfAngle);
		TArray<FString> Mismatches;
		if (!FMath::IsNearlyEqual(ActualInterval, MannequinRow.SurvivorVisionCheckIntervalSeconds)) Mismatches.Add(TEXT("SurvivorVisionCheckIntervalSeconds"));
		if (!FMath::IsNearlyEqual(ActualHalfAngle, MannequinRow.SurvivorVisionHalfAngleDegrees)) Mismatches.Add(TEXT("SurvivorVisionHalfAngleDegrees"));
		RecordEvent(Mismatches.Num() == 0 ? EProjectProject01DiagnosticSeverity::Normal : EProjectProject01DiagnosticSeverity::Risk,
			Mismatches.Num() == 0 ? TEXT("ActorTuningMatches") : TEXT("ActorTuningMismatch"),
			FString::Printf(TEXT("Layer=RuntimeCacheToActor; Tuning=MultiplayVision; MismatchedFields=%s"),
				Mismatches.Num() == 0 ? TEXT("None") : *FString::Join(Mismatches, TEXT("|"))), GameMode);
	}
}

FString UProjectProject01DiagnosticsSubsystem::GetRoleLabel() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return TEXT("Unknown");
	}
	return ProjectProject01Diagnostics::NetModeToString(World->GetNetMode());
}

FString UProjectProject01DiagnosticsSubsystem::GetRoleInstanceLabel() const
{
	const UWorld* World = GetWorld();
	const int32 PieInstanceId = IsValid(World) && IsValid(World->GetPackage()) ? World->GetPackage()->GetPIEInstanceID() : INDEX_NONE;
	const int32 LocalPlayerNumber = GetLocalPlayerNumber();
	return FString::Printf(TEXT("%s_PIE%d_P%d"), *GetRoleLabel(), PieInstanceId, LocalPlayerNumber);
}

int32 UProjectProject01DiagnosticsSubsystem::GetLocalPlayerNumber() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return INDEX_NONE;
	}
	const APlayerController* PlayerController = World->GetFirstPlayerController();
	const ULocalPlayer* LocalPlayer = IsValid(PlayerController) ? PlayerController->GetLocalPlayer() : nullptr;
	return IsValid(LocalPlayer) ? LocalPlayer->GetControllerId() : INDEX_NONE;
}

FString UProjectProject01DiagnosticsSubsystem::GetOutputDirectory() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics"), TestRunId, GetRoleInstanceLabel());
}

FString UProjectProject01DiagnosticsSubsystem::GetFileStem() const
{
	return FString::Printf(TEXT("%s_%s"), *TestRunId, *GetRoleInstanceLabel());
}

FString UProjectProject01DiagnosticsSubsystem::EscapeCsv(const FString& Value)
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

FString UProjectProject01DiagnosticsSubsystem::EscapeXml(const FString& Value)
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("&"), TEXT("&amp;"));
	Escaped.ReplaceInline(TEXT("\""), TEXT("&quot;"));
	Escaped.ReplaceInline(TEXT("<"), TEXT("&lt;"));
	Escaped.ReplaceInline(TEXT(">"), TEXT("&gt;"));
	return Escaped;
}

FString UProjectProject01DiagnosticsSubsystem::SeverityToString(const EProjectProject01DiagnosticSeverity Severity)
{
	switch (Severity)
	{
	case EProjectProject01DiagnosticSeverity::Normal: return TEXT("Normal");
	case EProjectProject01DiagnosticSeverity::Warning: return TEXT("Warning");
	case EProjectProject01DiagnosticSeverity::Risk: return TEXT("RiskCandidate");
	default: return TEXT("Inconclusive");
	}
}

void UProjectProject01DiagnosticsSubsystem::WriteCaptureFiles(const FString& Outcome)
{
	const FString OutputDirectory = GetOutputDirectory();
	if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		UE_LOG(LogProjectProject01Diagnostics, Error, TEXT("Could not create diagnostics directory: %s"), *OutputDirectory);
		return;
	}
	// Keep the human-authored tuning inputs beside this report. This is a snapshot for review;
	// it does not reimport, apply, or alter any DataTable.
	for (const TCHAR* TuningFileName : { TEXT("Player.csv"), TEXT("AITuning.csv"), TEXT("HelperTuning.csv") })
	{
		const FString SourcePath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MyProject"), TEXT("Data"), TuningFileName);
		FString SourceContents;
		if (FFileHelper::LoadFileToString(SourceContents, *SourcePath))
		{
			if (!FFileHelper::SaveStringToFile(SourceContents, *FPaths::Combine(OutputDirectory, TuningFileName)))
			{
				RecordEvent(EProjectProject01DiagnosticSeverity::Warning, TEXT("TuningSnapshotWriteFailed"), FString::Printf(TEXT("Could not save tuning snapshot: %s"), TuningFileName));
			}
		}
		else
		{
			RecordEvent(EProjectProject01DiagnosticSeverity::Inconclusive, TEXT("TuningSnapshotUnavailable"), FString::Printf(TEXT("Could not read tuning input: %s"), TuningFileName));
		}
	}

	const FString Role = GetRoleInstanceLabel();
	const FString NetMode = GetRoleLabel();
	const int32 LocalPlayerNumber = GetLocalPlayerNumber();
	FString PerformanceCsv = TEXT("UtcTime,Role,NetMode,LocalPlayerNumber,GameSeconds,FrameMilliseconds,FramesPerSecond,GameThreadMilliseconds,DrawThreadMilliseconds,GpuMilliseconds,UsedPhysicalBytes,MemoryBytesPerSecondDelta,InBytesPerSecond,OutBytesPerSecond,PlayerCount,MannequinCount,HelperCount,PathRequestCount,PathFailureCount,PossessionEventCount,VisionCheckCount,LineTraceCount,AITickMilliseconds,NetworkConnectionCount,HasNetDriver,HasNavigationData\n");
	for (const FProjectProject01DiagnosticSample& Sample : Samples)
	{
		PerformanceCsv += FString::Printf(TEXT("%s,%s,%s,%d,%.3f,%.3f,%.2f,%.3f,%.3f,%.3f,%llu,%.2f,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%d,%s,%s\n"),
			*Sample.UtcTime, *Role, *NetMode, LocalPlayerNumber, Sample.GameSeconds, Sample.FrameMilliseconds, Sample.FramesPerSecond, Sample.GameThreadMilliseconds, Sample.DrawThreadMilliseconds, Sample.GpuMilliseconds, Sample.UsedPhysicalBytes, Sample.MemoryBytesPerSecondDelta,
			Sample.InBytesPerSecond, Sample.OutBytesPerSecond, Sample.PlayerCount, Sample.MannequinCount, Sample.HelperCount, Sample.PathRequestCount, Sample.PathFailureCount, Sample.PossessionEventCount,
			Sample.VisionCheckCount, Sample.LineTraceCount, Sample.AITickMilliseconds, Sample.NetworkConnectionCount, Sample.bHasNetDriver ? TEXT("true") : TEXT("false"),
			Sample.bHasNavigationData ? TEXT("true") : TEXT("false"));
	}

	FString EventsCsv = TEXT("UtcTime,Role,NetMode,LocalPlayerNumber,GameSeconds,Severity,Code,Message,Actor\n");
	for (const FProjectProject01DiagnosticEvent& Event : Events)
	{
		EventsCsv += FString::Printf(TEXT("%s,%s,%s,%d,%.3f,%s,%s,%s,%s\n"), *Event.UtcTime, *Role, *NetMode, LocalPlayerNumber, Event.GameSeconds,
			*SeverityToString(Event.Severity), *EscapeCsv(Event.Code), *EscapeCsv(Event.Message), *EscapeCsv(Event.ActorName));
	}

	int32 RiskCount = 0;
	FString RiskXml;
	for (const FProjectProject01DiagnosticEvent& Event : Events)
	{
		if (Event.Severity == EProjectProject01DiagnosticSeverity::Risk || Event.Severity == EProjectProject01DiagnosticSeverity::Warning)
		{
			++RiskCount;
			RiskXml += FString::Printf(TEXT("    <RiskCandidate TimeUtc=\"%s\" GameSeconds=\"%.3f\" Severity=\"%s\" Code=\"%s\" Actor=\"%s\">%s</RiskCandidate>\n"),
				*EscapeXml(Event.UtcTime), Event.GameSeconds, *SeverityToString(Event.Severity), *EscapeXml(Event.Code), *EscapeXml(Event.ActorName), *EscapeXml(Event.Message));
		}
	}
	const UWorld* World = GetWorld();
	const FString MapName = IsValid(World) ? World->GetMapName() : TEXT("Unknown");
	const FString GameModeName = IsValid(World) ? GetNameSafe(World->GetAuthGameMode()) : TEXT("ClientOrUnknown");
	const int32 ConnectedPlayerCount = IsValid(World) && IsValid(World->GetGameState()) ? World->GetGameState()->PlayerArray.Num() : INDEX_NONE;
	const FString Xml = FString::Printf(TEXT("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<TestReport TestRunId=\"%s\" TestName=\"%s\" Outcome=\"%s\" Role=\"%s\" NetMode=\"%s\" LocalPlayerNumber=\"%d\" Map=\"%s\" GameMode=\"%s\" ConnectedPlayerCount=\"%d\" EngineVersion=\"%s\">\n  <TestDefinition Purpose=\"%s\" ReproductionSteps=\"%s\" ExpectedResult=\"%s\" FailureCriteria=\"%s\" TimeoutSeconds=\"%.1f\" />\n  <Collection SamplingSeconds=\"1\" Mode=\"Lightweight\" SampleCount=\"%d\" EventCount=\"%d\" RiskCandidateCount=\"%d\" />\n  <TuningSnapshot Files=\"Player.csv,AITuning.csv,HelperTuning.csv\" Source=\"Content/MyProject/Data\" />\n  <RiskCandidates>\n%s  </RiskCandidates>\n  <Limitations>Warning and Error capture uses a bounded 512-record buffer. Multiplayer mismatch analysis is an offline candidate check in the external dashboard. Process-level memory and render counters are shared when multiple PIE worlds run in one editor process.</Limitations>\n</TestReport>\n"),
		*EscapeXml(TestRunId), *EscapeXml(TestName), *EscapeXml(Outcome), *EscapeXml(Role), *EscapeXml(NetMode), LocalPlayerNumber, *EscapeXml(MapName), *EscapeXml(GameModeName), ConnectedPlayerCount, *EscapeXml(FEngineVersion::Current().ToString()),
		*EscapeXml(TestDefinition.Purpose), *EscapeXml(TestDefinition.ReproductionSteps), *EscapeXml(TestDefinition.ExpectedResult), *EscapeXml(TestDefinition.FailureCriteria), TestDefinition.TimeoutSeconds, Samples.Num(), Events.Num(), RiskCount, *RiskXml);

	const bool bPerformanceWritten = FFileHelper::SaveStringToFile(PerformanceCsv, *FPaths::Combine(OutputDirectory, TEXT("Performance.csv")));
	const bool bEventsWritten = FFileHelper::SaveStringToFile(EventsCsv, *FPaths::Combine(OutputDirectory, TEXT("Events.csv")));
	const bool bXmlWritten = FFileHelper::SaveStringToFile(Xml, *FPaths::Combine(OutputDirectory, TEXT("TestReport.xml")));
	if (!bPerformanceWritten || !bEventsWritten || !bXmlWritten)
	{
		UE_LOG(LogProjectProject01Diagnostics, Error, TEXT("Diagnostics capture %s could not write every report file."), *TestRunId);
		return;
	}
	UE_LOG(LogProjectProject01Diagnostics, Log, TEXT("Diagnostics capture saved: %s"), *OutputDirectory);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01DiagnosticsContractTest,
	"ProjectProject01.Diagnostics.LightweightCollectorContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01DiagnosticsContractTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Project saved directory must be available for diagnostic artifacts."), FPaths::ProjectSavedDir().IsEmpty());
	TestEqual(TEXT("Diagnostic sample interval must remain lightweight."), ProjectProject01Diagnostics::SampleIntervalSeconds, 1.0f);
	TestTrue(TEXT("Risk severity enum is available for report consumers."),
		static_cast<uint8>(EProjectProject01DiagnosticSeverity::Risk) > static_cast<uint8>(EProjectProject01DiagnosticSeverity::Warning));
	TestFalse(TEXT("Client proxy worlds must not require authoritative AI state."), ProjectProject01Diagnostics::ShouldEvaluateAuthoritativeAI(NM_Client));
	TestTrue(TEXT("Dedicated server worlds must evaluate authoritative AI state."), ProjectProject01Diagnostics::ShouldEvaluateAuthoritativeAI(NM_DedicatedServer));
	TestTrue(TEXT("Heartbeat telemetry must not be interpreted as a warning or risk."),
		ProjectProject01Diagnostics::IsExpectedHeartbeatTelemetry(FName(TEXT("LogTemp")), TEXT("[Heartbeat] THUMP | BPM=73.3")));
	TestFalse(TEXT("Unrelated warnings must remain observable."),
		ProjectProject01Diagnostics::IsExpectedHeartbeatTelemetry(FName(TEXT("LogTemp")), TEXT("A real warning")));
	TestFalse(TEXT("An unset Blackboard vector must not count as a valid target."), FAISystem::IsValidLocation(FAISystem::InvalidLocation));
	TestTrue(TEXT("A finite Blackboard vector must remain a valid target."), FAISystem::IsValidLocation(FVector::ZeroVector));
	FString CsvError;
	FPlayerTuningRow PlayerCsv;
	FMannequinAITuningRow MannequinCsv;
	FHelperTuningRow HelperCsv;
	TestTrue(TEXT("Player.csv must import into every FPlayerTuningRow field."), ProjectProject01Diagnostics::LoadVerticalCsvIntoStruct(
		FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MyProject/Data/Player.csv")), FPlayerTuningRow::StaticStruct(), &PlayerCsv, CsvError));
	if (!CsvError.IsEmpty()) AddError(CsvError);
	CsvError.Reset();
	TestTrue(TEXT("AITuning.csv must import into every FMannequinAITuningRow field."), ProjectProject01Diagnostics::LoadVerticalCsvIntoStruct(
		FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MyProject/Data/AITuning.csv")), FMannequinAITuningRow::StaticStruct(), &MannequinCsv, CsvError));
	if (!CsvError.IsEmpty()) AddError(CsvError);
	CsvError.Reset();
	TestTrue(TEXT("HelperTuning.csv must import into every FHelperTuningRow field."), ProjectProject01Diagnostics::LoadVerticalCsvIntoStruct(
		FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MyProject/Data/HelperTuning.csv")), FHelperTuningRow::StaticStruct(), &HelperCsv, CsvError));
	if (!CsvError.IsEmpty()) AddError(CsvError);
	return true;
}
#endif

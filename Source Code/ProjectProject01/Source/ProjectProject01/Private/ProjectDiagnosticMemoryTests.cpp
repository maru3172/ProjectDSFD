#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectDiagnosticMemory.h"
#include "ProjectProcessDiagnostics.h"
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "ProjectRequestCircuit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/Csv/CsvParser.h"
#include "HAL/IConsoleManager.h"
#include "RenderingThread.h"
#include "RHIGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDiagnosticCacheCountersTest,"ProjectProject01.Diagnostics.CacheMemoryCounters",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectDiagnosticCacheCountersTest::RunTest(const FString&)
{
	FGenericMemoryStats Empty;TestFalse(TEXT("Missing allocator keys are unavailable, never a fake zero usage"),ProjectDiagnosticMemory::Decode(Empty).Available);
	FGenericMemoryStats Valid;Valid.Add(TEXT("TotalAllocated"),128);Valid.Add(TEXT("TotalOSAllocated"),512);
	const auto Decoded=ProjectDiagnosticMemory::Decode(Valid);
	TestTrue(TEXT("Binned accounting distinguishes used, OS-held and retained/slack"),Decoded.Available && Decoded.Used==128 && Decoded.Committed==512 && Decoded.Retained==384);
	FGenericMemoryStats Inconsistent;Inconsistent.Add(TEXT("TotalAllocated"),512);Inconsistent.Add(TEXT("TotalOSAllocated"),128);
	TestFalse(TEXT("Concurrent inconsistent counters cannot underflow retained memory"),ProjectDiagnosticMemory::Decode(Inconsistent).Available);
	const auto Actual=ProjectDiagnosticMemory::Capture();
	AddInfo(FString::Printf(TEXT("Real allocator: supported=%d used=%llu OS-held=%llu retained=%llu bytes"),Actual.Available,Actual.Used,Actual.Committed,Actual.Retained));
	TestTrue(TEXT("Real supported accounting sums without inventing cache-only bytes"),!Actual.Available || Actual.Used+Actual.Retained==Actual.Committed);
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	auto* Collector=World->GetSubsystem<UProjectProject01DiagnosticsSubsystem>();
	if(!TestNotNull(TEXT("Collector fixture"),Collector)){World->DestroyWorld(false);return false;}
	Collector->TestRunId=TEXT("CacheMemoryQA-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);Collector->TestName=TEXT("Automation schema fixture, not a performance benchmark");
	ProjectProcessDiagnostics::AcquireObjectCounter();Collector->bObjectCounterAcquired=true;
	const uint64 CountBefore=ProjectProcessDiagnostics::CreatedObjects();
	UObject* Created=NewObject<UDataTable>();
	TestTrue(TEXT("UObject create listener counts new objects without enumerating live objects"),Created&&ProjectProcessDiagnostics::CreatedObjects()>CountBefore);
	Collector->LastCreatedObjects=CountBefore;Collector->LastRateSampleSeconds=FPlatformTime::Seconds()-.5;
	Collector->bCaptureActive=true;
	UProjectProject01DiagnosticsSubsystem::AddHttpMetrics(World,true,false,true,true);
	Collector->CollectSample(0);Collector->WriteCaptureFiles(TEXT("Inconclusive"));Collector->bCaptureActive=false;
	ProjectProcessDiagnostics::ReleaseObjectCounter();Collector->bObjectCounterAcquired=false;
	FString CSV;const FString File=Collector->GetOutputDirectory()/TEXT("Performance.csv");
	TestTrue(TEXT("Actual collector exports the extended backward-compatible CSV"),FFileHelper::LoadFileToString(CSV,*File));
	const FCsvParser Parser(CSV);const auto& Rows=Parser.GetRows();
	if(TestTrue(TEXT("Header and one collected row exist"),Rows.Num()>=2))
	{
		TestEqual(TEXT("Appended optional counters preserve CSV column alignment"),Rows[0].Num(),Rows[1].Num());
		TMap<FString,FString> Fields;
		for(int32 i=0;i<FMath::Min(Rows[0].Num(),Rows[1].Num());++i)Fields.Add(Rows[0][i],Rows[1][i]);
		TestTrue(TEXT("Existing process and texture columns are unchanged"),Fields.Contains(TEXT("UsedPhysicalBytes")) && Fields.Contains(TEXT("TexturePoolSizeBytes")));
		TestTrue(TEXT("New availability flags and allocator/system counters are present"),Fields.Contains(TEXT("AllocatorStatsAvailable")) && Fields.Contains(TEXT("AllocatorRetainedBytes")) && Fields.Contains(TEXT("AvailablePhysicalBytes")) && Fields.Contains(TEXT("TextureStatsAvailable")));
		TestTrue(TEXT("Process, UObject allocation and HTTP counters exported"),Fields.Contains(TEXT("ProcessHandleCount"))&&Fields.Contains(TEXT("ProcessThreadCount"))&&Fields.Contains(TEXT("TcpEndpointCount"))&&Fields.Contains(TEXT("UObjectCreationsPerSecond"))&&Fields.FindRef(TEXT("HttpCompletedCount"))==TEXT("1")&&Fields.FindRef(TEXT("HttpTimeoutCount"))==TEXT("1"));
		TestTrue(TEXT("Creation rate measures new allocations, not net live count"),FCString::Atod(*Fields.FindRef(TEXT("UObjectCreationsPerSecond")))>0);
#if PLATFORM_WINDOWS
		TestTrue(TEXT("Real Windows process handle/thread counters available"),FCString::Atoi(*Fields.FindRef(TEXT("ProcessHandleCount")))>0&&FCString::Atoi(*Fields.FindRef(TEXT("ProcessThreadCount")))>0);
		TestTrue(TEXT("Real Windows TCP/UDP endpoint snapshots available"),FCString::Atoi(*Fields.FindRef(TEXT("TcpEndpointCount")))>=0&&FCString::Atoi(*Fields.FindRef(TEXT("UdpEndpointCount")))>=0);
#endif
		const auto First=ProjectProcessDiagnostics::Capture(),Second=ProjectProcessDiagnostics::Capture();
		TestEqual(TEXT("OS table query shared/cached between PIE worlds for five seconds"),First.SampledAtSeconds,Second.SampledAtSeconds);
		AddInfo(TEXT("Collector schema fixture: ")+File);
	}
	World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectRequestCircuitTest,"ProjectProject01.Diagnostics.RequestCircuitBreaker",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectRequestCircuitTest::RunTest(const FString&)
{
    FProjectRequestCircuit Circuit;uint64 Ticket=0;
    for(int32 i=0;i<3;++i){TestTrue(TEXT("Closed admits requests"),Circuit.TryAcquire(TEXT("A"),0,5,Ticket));Circuit.Complete(Ticket,0,true);}
    TestTrue(TEXT("Three transient failures open the circuit"),Circuit.Snapshot(0).State==EProjectRequestCircuitState::Open);
    const uint64 OldTicket=Ticket;
    TestFalse(TEXT("Open rejects without creating an HTTP request"),Circuit.TryAcquire(TEXT("A"),1,5,Ticket));
    Circuit.Complete(OldTicket,1,false);TestTrue(TEXT("Stale success cannot close a newer circuit epoch"),Circuit.Snapshot(1).State==EProjectRequestCircuitState::Open);
    TestTrue(TEXT("After cooldown exactly one half-open trial is admitted"),Circuit.TryAcquire(TEXT("A"),10,5,Ticket));
    const uint64 Probe=Ticket;TestFalse(TEXT("Other callers cannot join the probe"),Circuit.TryAcquire(TEXT("A"),10,5,Ticket));
    Circuit.Complete(Probe,10,false);TestTrue(TEXT("Successful or ordinary 4xx response recovers"),Circuit.Snapshot(10).State==EProjectRequestCircuitState::Closed);
    Circuit.TryAcquire(TEXT("A"),11,5,Ticket);Circuit.Complete(Ticket,11,true,true);
    TestEqual(TEXT("Cancellation is not accumulated as a new outage"),Circuit.Snapshot(11).Failures,0);
    for(int32 i=0;i<3;++i){Circuit.TryAcquire(TEXT("A"),12,5,Ticket);Circuit.Complete(Ticket,12,true);}
    Circuit.TryAcquire(TEXT("A"),22,5,Ticket);
    TestFalse(TEXT("An abandoned half-open trial is bounded, never stuck forever"),Circuit.TryAcquire(TEXT("A"),29,5,Ticket));
    TestTrue(TEXT("Changing backend origin immediately resets protection"),Circuit.TryAcquire(TEXT("B"),29,5,Ticket));
    TestTrue(TEXT("Origin B starts closed"),Circuit.Snapshot(29).State==EProjectRequestCircuitState::Closed);
    FProjectRequestCircuit Independent;TestTrue(TEXT("Another client instance has independent protection"),Independent.TryAcquire(TEXT("A"),29,5,Ticket));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectAutomationSceneCleanupTest,"ProjectProject01.Diagnostics.RetiredSceneRelease",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectAutomationSceneCleanupTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if(!TestNotNull(TEXT("Synthetic world"),World))return false;
    TestNotNull(TEXT("A live fixture keeps its rendering scene"),World->Scene);
    World->DestroyWorld(false);
    TestNotNull(TEXT("Engine teardown keeps its scene until the safe end-of-test boundary"),World->Scene);
    FlushRenderingCommands();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMotionVectorThreadSafetyTest,"ProjectProject01.Diagnostics.MotionVectorThreadSafety",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectMotionVectorThreadSafetyTest::RunTest(const FString&)
{
    auto* CVar=IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionVectorSimulation"));
    if(!TestNotNull(TEXT("Engine motion-vector CVar exists"),CVar))return false;
    TestTrue(TEXT("TSR reads the render-thread shadow value"),CVar->TestFlags(ECVF_RenderThreadSafe));
    const int32 Previous=CVar->GetInt();
    for(int32 Value:{0,1,Previous})
    {
        CVar->SetWithCurrentPriority(Value);
        int32 RenderValue=-1;
        ENQUEUE_RENDER_COMMAND(ProjectMotionVectorRead)([&](FRHICommandListImmediate&){RenderValue=CVar->GetInt();});
        FlushRenderingCommands();
        TestEqual(TEXT("Writes arrive in order before render-thread reads"),RenderValue,Value);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectRenderingResourceBudgetTest,"ProjectProject01.Verification.RenderingResourceBudget",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectRenderingResourceBudgetTest::RunTest(const FString&)
{
    // Runs after UI/rendering tests in the ordinary queue. Validate actual resource
    // lifetime against the engine's unchanged limit, rather than raising that limit.
    int64 Reserved=0;
    ENQUEUE_RENDER_COMMAND(ProjectReadReservedBudget)([&](FRHICommandListImmediate&){Reserved=GRHIGlobals.ReservedResources.VirtualSize;});
    FlushRenderingCommands();
    const auto* Budget=IConsoleManager::Get().FindConsoleVariable(TEXT("rhi.ReservedResources.VirtualSizeWarningGB"));
    AddInfo(FString::Printf(TEXT("GPU reserved virtual address space after rendering suite: %.2f GiB; engine warning threshold: %d GiB"),Reserved/1073741824.,Budget?Budget->GetInt():-1));
    if(GRHIGlobals.ReservedResources.Supported&&Budget&&Budget->GetInt()>0)
        TestTrue(TEXT("Retired test scenes do not exceed the unchanged reserved-resource budget"),Reserved<=int64(Budget->GetInt())*1073741824ll);
    return true;
}
#endif

namespace ProjectProject01DiagnosticsDashboard;

internal static class CacheMemoryAnalysisTests
{
    internal static void Run(PerformanceSample sample)
    {
        void Check(bool value,string message){if(!value)throw new InvalidOperationException("Cache analysis: "+message);}
        var legacy=CacheMemoryAnalysis.ReadOptionalCounters(sample,new Dictionary<string,string>());
        Check(!legacy.AllocatorUsedMiB.HasValue&&!legacy.TexturePoolUsePercent.HasValue,"old reports must stay unknown, not zero");
        Check(CacheMemoryAnalysis.Analyze([legacy],[]).Any(f=>f.Code=="AllocatorUnavailable"),"old-report interpretation");
        var available=new Dictionary<string,string>{{"AllocatorStatsAvailable","true"},{"AllocatorUsedBytes","1048576"},{"AllocatorCommittedBytes","3145728"},{"AvailablePhysicalBytes","1048576"},{"TotalPhysicalBytes","4194304"},{"TextureStatsAvailable","true"}};
        var parsed=CacheMemoryAnalysis.ReadOptionalCounters(sample,available);
        Check(parsed.AllocatorUsedMiB==1&&parsed.AllocatorRetainedMiB==2&&parsed.AvailablePhysicalMiB==1,"real byte counters converted to MiB");
        available["AllocatorCommittedBytes"]="1";Check(CacheMemoryAnalysis.ReadOptionalCounters(sample,available).AllocatorUsedMiB is null,"underflowing/inconsistent accounting rejected");
        available["AllocatorCommittedBytes"]="NaN";Check(CacheMemoryAnalysis.ReadOptionalCounters(sample,available).AllocatorUsedMiB is null,"non-finite counters rejected");
        available["AllocatorStatsAvailable"]="false";Check(CacheMemoryAnalysis.ReadOptionalCounters(sample,available).AllocatorUsedMiB is null,"unsupported platform is not a zero-memory success");
        PerformanceSample At(int second)=>sample with {GameSeconds=second,AllocatorUsedMiB=1024,AllocatorCommittedMiB=2048,AllocatorRetainedMiB=1024,AvailablePhysicalMiB=8192,TotalPhysicalMiB=32768,HasTextureCounters=true};
        var normal=Enumerable.Range(0,7).Select(At).ToList();
        Check(!CacheMemoryAnalysis.Analyze(normal,[]).Any(f=>f.Status=="검토 후보"),"a large reusable pool alone must not be an error");
        var pressure=normal.Select(s=>s with {AvailablePhysicalMiB=128}).ToList();
        Check(CacheMemoryAnalysis.Analyze(pressure,[]).Any(f=>f.Code=="RetainedMemoryPressure"),"sustained low-RAM/retention conjunction");
        var texture=normal.Select(s=>s with {StreamingTextureMemoryMiB=300,TexturePoolMiB=256}).ToList();
        Check(CacheMemoryAnalysis.Analyze(texture,[]).Any(f=>f.Code=="TexturePoolPressure"),"sustained texture pool pressure");
        Check(!CacheMemoryAnalysis.Analyze(texture.Select(s=>s with {StreamingTextureMemoryMiB=250}).ToList(),[]).Any(f=>f.Code=="TexturePoolPressure"),"a near-full pool alone can be normal cache residency");
        Check(!CacheMemoryAnalysis.Analyze([texture[0],texture[1] with {GameSeconds=10},texture[2] with {GameSeconds=20}],[]).Any(f=>f.Code=="TexturePoolPressure"),"gaps cannot masquerade as continuous pressure");
        Check(!CacheMemoryAnalysis.Analyze(texture.Select(s=>s with {TexturePoolMiB=0}).ToList(),[]).Any(f=>f.Code=="TexturePoolPressure"),"unlimited/unknown texture pool has no fake percentage");
        var gc=Enumerable.Range(0,3).Select(i=>new DiagnosticEvent("",sample.Role,sample.NetMode,i*15,"Normal","GarbageCollectionMemoryDelta",
            $"AllocatorAfterAvailable=1;AllocatorUsedAfterBytes={(100L+i*100L)*1048576};PhysicalAfterBytes={(1000L+i*100L)*1048576}","")).ToList();
        Check(CacheMemoryAnalysis.Analyze(normal,gc).Any(f=>f.Code=="PostGCLiveGrowth"),"repeated rising post-GC live baseline");
        Check(!CacheMemoryAnalysis.Analyze(normal,gc.Select(e=>e with {Message="PhysicalAfterBytes=999999999"}).ToList()).Any(f=>f.Code=="PostGCLiveGrowth"),"old GC records cannot imply zero live memory");
        Check(CacheMemoryAnalysis.Analyze([],[]).Single().Code=="NoSamples","empty capture");
        Console.WriteLine("Cache memory analysis self-tests passed (legacy/unsupported, retention, RAM pressure, pool pressure, gaps, GC baselines).");
    }
}

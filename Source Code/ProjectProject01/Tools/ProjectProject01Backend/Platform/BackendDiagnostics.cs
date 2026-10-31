using System.Diagnostics;
using System.Diagnostics.Metrics;
using System.Globalization;
using System.Text.Json;

// Fixed-cardinality counters only. Never collect URLs, bodies, SQL, pool names, accounts or credentials.
internal sealed record BackendDiagnosticSnapshot(int Schema, string CaptureId, DateTimeOffset UtcTime,
    int ProcessId, double IntervalSeconds, Dictionary<string,double?> Values,
    Dictionary<string,string> States, DiagnosticFailure[] Failures);
internal sealed record DiagnosticFailure(string Source,string Kind,int Status,long Count,int? Attempt=null,int? Maximum=null,double? NextDelaySeconds=null);

internal sealed class DiagnosticCacheCounters
{
    private long hits,misses,lookups,ticks,evictions,expirations,entries,payloadBytes,rejections;
    public void Lookup(bool hit,long elapsedTicks){Interlocked.Increment(ref lookups);Interlocked.Add(ref ticks,Math.Max(0,elapsedTicks));if(hit)Interlocked.Increment(ref hits);else Interlocked.Increment(ref misses);}
    public void Size(long count,long estimatedPayloadBytes){Interlocked.Exchange(ref entries,Math.Max(0,count));Interlocked.Exchange(ref payloadBytes,Math.Max(0,estimatedPayloadBytes));}
    public void Expired()=>Interlocked.Increment(ref expirations);
    public void CapacityRejected()=>Interlocked.Increment(ref rejections);
    public void Evicted()=>Interlocked.Increment(ref evictions);
    public void Read(Dictionary<string,double?> v)
    {
        long h=Interlocked.Read(ref hits),m=Interlocked.Read(ref misses),n=Interlocked.Read(ref lookups);
        v["Cache.Hits"]=h;v["Cache.Misses"]=m;v["Cache.HitPercent"]=h+m>0?100d*h/(h+m):null;
        v["Cache.MissPercent"]=h+m>0?100d*m/(h+m):null;
        v["Cache.LookupMeanMs"]=n>0?Interlocked.Read(ref ticks)*1000d/Stopwatch.Frequency/n:null;
        v["Cache.Entries"]=Interlocked.Read(ref entries);v["Cache.EstimatedPayloadBytes"]=Interlocked.Read(ref payloadBytes);
        v["Cache.Evictions"]=Interlocked.Read(ref evictions);v["Cache.Expirations"]=Interlocked.Read(ref expirations);v["Cache.CapacityRejected"]=Interlocked.Read(ref rejections);
    }
}

internal sealed class BackendDiagnostics : IDisposable
{
    public readonly DiagnosticCacheCounters VoiceRoster=new();
    public readonly RequestCircuitBreaker ResultRecoveryCircuit=new();
    private readonly object gate=new();
    private readonly object sampleGate=new();
    private readonly object requestGate=new();
    private readonly MeterListener listener=new();
    private readonly Dictionary<string,double> current=new(StringComparer.Ordinal),sums=new(StringComparer.Ordinal);
    private readonly Dictionary<string,long> counts=new(StringComparer.Ordinal);
    private readonly Dictionary<(string Source,string Kind,int Status,int? Attempt,int? Max,double? Delay),long> failures=[];
    private long completed,serverErrors,clientErrors,canceled,active,requestTicks,peakActive;
    private long lastCompleted,lastErrors,lastClientErrors,lastCanceled,lastTicks,lastAllocated,lastThreadCompletions;
    private int[] lastCollections=new int[3];
    private double lastPause,lastSample=Stopwatch.GetTimestamp()/(double)Stopwatch.Frequency;
    private string captureId="";
    private int enabled;
    public bool CaptureEnabled=>Volatile.Read(ref enabled)!=0;
    private BackendDiagnosticSnapshot? latest;
    public BackendDiagnosticSnapshot? Latest=>Volatile.Read(ref latest);
    private static readonly HashSet<string> Allowed=new(StringComparer.Ordinal)
    {
        "MySqlConnector|db.client.connections.usage","MySqlConnector|db.client.connections.max",
        "MySqlConnector|db.client.connections.pending_requests","MySqlConnector|db.client.connections.timeouts",
        "MySqlConnector|db.client.connections.wait_time",
        "System.Net.Http|http.client.open_connections","System.Net.Http|http.client.active_requests",
        "System.Net.Http|http.client.request.time_in_queue","System.Net.Http|http.client.request.duration",
        "Microsoft.AspNetCore.Server.Kestrel|kestrel.queued_connections","Microsoft.AspNetCore.Server.Kestrel|kestrel.queued_requests"
    };
    public static readonly BackendDiagnostics Shared=new();
    public BackendDiagnostics()
    {
        listener.InstrumentPublished=(i,l)=>{if(Allowed.Contains(i.Meter.Name+"|"+i.Name))l.EnableMeasurementEvents(i);};
        listener.SetMeasurementEventCallback<long>((i,n,t,_)=>RecordInstrument(i,n,t));
        listener.SetMeasurementEventCallback<int>((i,n,t,_)=>RecordInstrument(i,n,t));
        listener.SetMeasurementEventCallback<double>((i,n,t,_)=>RecordInstrument(i,n,t));
        listener.Start(); // Attached before DB/HTTP pools are created, so up/down baselines are not missed.
    }
    private void RecordInstrument(Instrument i,double n,ReadOnlySpan<KeyValuePair<string,object?>> tags)
    {
        if(!double.IsFinite(n))return;
        string key=i.Name;
        if(key=="db.client.connections.usage"||key=="http.client.open_connections")
        {
            string? state=null;
            foreach(var tag in tags)if(tag.Key=="state"||tag.Key=="http.connection.state")state=tag.Value as string;
            if(state is not("used" or "active" or "idle"))return;
            key+="."+state;
        }
        bool histogram=i.GetType().Name.StartsWith("Histogram",StringComparison.Ordinal);
        lock(gate)
        {
            if(histogram)
            {
                if(!CaptureEnabled)return;
                sums[key]=sums.GetValueOrDefault(key)+n;counts[key]=counts.GetValueOrDefault(key)+1;
                if(i.Name=="http.client.request.duration")
                    foreach(var tag in tags)
                        if(tag.Key=="error.type"&&tag.Value is string error&&
                           (error.Contains("Timeout",StringComparison.OrdinalIgnoreCase)||error.Contains("Canceled",StringComparison.OrdinalIgnoreCase)))
                            AddFailureLocked("HTTP client","timeout-or-cancellation",0,null,null,null);
            }
            else if(i.IsObservable){observableNames.Add(key);current[key]=current.GetValueOrDefault(key)+n;}
            else current[key]=current.GetValueOrDefault(key)+n;
        }
    }
    public void SetCapture(bool value)
    {
        lock(sampleGate)
        lock(gate)
        {
            if(value==CaptureEnabled)return;
            Volatile.Write(ref enabled,value?1:0);
            if(value)
            {
                captureId=Guid.NewGuid().ToString("N");Volatile.Write(ref latest,null);
                var requests=RequestSnapshot(true);
                lastCompleted=requests.Completed;lastErrors=requests.Errors;lastClientErrors=requests.ClientErrors;
                lastCanceled=requests.Canceled;lastTicks=requests.Ticks;
                lastAllocated=GC.GetTotalAllocatedBytes(false);lastThreadCompletions=ThreadPool.CompletedWorkItemCount;
                for(int g=0;g<3;++g)lastCollections[g]=GC.CollectionCount(g);
                lastPause=GC.GetTotalPauseDuration().TotalMilliseconds;lastSample=Now();
                sums.Clear();counts.Clear();failures.Clear();
            }
        }
    }
    public void BeginRequest()
    {
        lock(requestGate){++active;if(active>peakActive)peakActive=active;}
    }
    public void EndRequest(int status,bool aborted,bool threw,long elapsedTicks)
    {
        // Keep numerator, denominator, latency and concurrency coherent at interval boundaries.
        // This tiny critical section never formats strings, queries providers/OS, writes files or awaits.
        lock(requestGate)
        {
            --active;++completed;requestTicks+=Math.Max(0,elapsedTicks);
            if(aborted)++canceled;else if(threw||status>=500)++serverErrors;else if(status>=400)++clientErrors;
        }
        if(CaptureEnabled&&(aborted||threw||status>=400))Failure("HTTP server",aborted?"client-abort":threw?"unhandled-exception":status is 408 or 504?"timeout-status":"http-status",status);
    }
    public void Failure(string source,string kind,int status=0,int? attempt=null,int? maximum=null,double? delay=null)
    {
        if(!CaptureEnabled)return;
        // Only bounded internal labels are accepted, never exception messages or request content.
        if(source is not("HTTP server" or "HTTP client" or "DB" or "Result recovery"))return;
        if(kind is not("client-abort" or "unhandled-exception" or "timeout-status" or "http-status" or "timeout" or "cancellation" or "connection-failure" or "retry" or "retry-pass" or "retry-scheduled" or "circuit-open" or "timeout-or-cancellation"))return;
        lock(gate)AddFailureLocked(source,kind,Math.Clamp(status,0,599),attempt,maximum,delay);
    }
    private void AddFailureLocked(string source,string kind,int status,int? attempt,int? maximum,double? delay)
    {
        var key=(source,kind,status,attempt,maximum,delay);
        if(failures.ContainsKey(key)||failures.Count<64)failures[key]=failures.GetValueOrDefault(key)+1;
        else {var overflow=("HTTP server","other-aggregated",0,(int?)null,(int?)null,(double?)null);failures[overflow]=failures.GetValueOrDefault(overflow)+1;}
    }
    private (long Completed,long Errors,long ClientErrors,long Canceled,long Ticks,long Active,long Peak) RequestSnapshot(bool resetPeak)
    {
        lock(requestGate)
        {
            var snapshot=(completed,serverErrors,clientErrors,canceled,requestTicks,active,peakActive);
            if(resetPeak)peakActive=active;return snapshot;
        }
    }
    public BackendDiagnosticSnapshot? Sample()
    {
        lock(sampleGate)
        {
            if(!CaptureEnabled)return null;
            long samplingStarted=Stopwatch.GetTimestamp();
            // Observable values are absolute samples; non-observable up/down totals remain cumulative.
            lock(gate)foreach(var instrument in observableNames)current.Remove(instrument);
            bool observationFailed=false;
            // Provider callbacks can acquire provider locks. Never invoke them while holding our counter lock.
            try{listener.RecordObservableInstruments();}catch(Exception ex)when(ex is AggregateException or InvalidOperationException){observationFailed=true;}
            Dictionary<string,double> observed,windowSums;Dictionary<string,long> windowCounts;DiagnosticFailure[] details;
            lock(gate)
            {
                observed=new(current,StringComparer.Ordinal);windowSums=new(sums,StringComparer.Ordinal);windowCounts=new(counts,StringComparer.Ordinal);
                details=failures.Select(x=>new DiagnosticFailure(x.Key.Source,x.Key.Kind,x.Key.Status,x.Value,x.Key.Attempt,x.Key.Max,x.Key.Delay)).ToArray();
                sums.Clear();counts.Clear();failures.Clear();
            }
            double now=Now(),seconds=Math.Max(.001,now-lastSample);lastSample=now;
            var v=new Dictionary<string,double?>(StringComparer.Ordinal);
            var requests=RequestSnapshot(true);
            long done=requests.Completed,err=requests.Errors,bad=requests.ClientErrors,abort=requests.Canceled,ticks=requests.Ticks;
            long dc=Math.Max(0,done-lastCompleted),de=Math.Max(0,err-lastErrors);
            v["HTTP.CompletedPerSecond"]=dc/seconds;v["HTTP.Completed"]=dc;v["HTTP.ServerErrorPercent"]=dc>0?100d*de/dc:null;
            v["HTTP.ClientErrorPercent"]=dc>0?100d*Math.Max(0,bad-lastClientErrors)/dc:null;
            v["HTTP.Canceled"]=Math.Max(0,abort-lastCanceled);v["HTTP.ActiveRequests"]=Math.Max(0,requests.Active);
            v["HTTP.PeakActiveRequests"]=Math.Max(requests.Active,requests.Peak);
            v["HTTP.MeanDurationMs"]=dc>0?Math.Max(0,ticks-lastTicks)*1000d/Stopwatch.Frequency/dc:null;
            lastCompleted=done;lastErrors=err;lastClientErrors=bad;lastCanceled=abort;lastTicks=ticks;
            long allocated=GC.GetTotalAllocatedBytes(false);v["Managed.AllocationBytesPerSecond"]=Math.Max(0,allocated-lastAllocated)/seconds;lastAllocated=allocated;
            for(int g=0;g<3;++g){int c=GC.CollectionCount(g);v[$"GC.Gen{g}Collections"]=Math.Max(0,c-lastCollections[g]);lastCollections[g]=c;}
            v["GC.CollectionsPerMinute"]=v["GC.Gen0Collections"]*60/seconds; // CollectionCount(0) already includes higher generations; do not sum.
            double pause=GC.GetTotalPauseDuration().TotalMilliseconds;v["GC.PauseTotalMs"]=Math.Max(0,pause-lastPause);lastPause=pause;
            var gc=GC.GetGCMemoryInfo();v["GC.LastHeapBytes"]=gc.Index>0?gc.HeapSizeBytes:null;v["GC.LastFragmentedBytes"]=gc.Index>0?gc.FragmentedBytes:null;
            v["GC.LastPauseMs"]=gc.Index>0?gc.PauseDurations.ToArray().Sum(x=>x.TotalMilliseconds):null;
            ThreadPool.GetAvailableThreads(out int available,out int ioAvailable);ThreadPool.GetMaxThreads(out int max,out int ioMax);
            v["ThreadPool.Threads"]=ThreadPool.ThreadCount;v["ThreadPool.BusyWorkers"]=Math.Max(0,max-available);v["ThreadPool.MaximumWorkers"]=max;
            v["ThreadPool.BusyOfCreatedPercent"]=ThreadPool.ThreadCount>0?100d*Math.Max(0,max-available)/ThreadPool.ThreadCount:null;
            v["ThreadPool.PendingWorkItems"]=ThreadPool.PendingWorkItemCount;v["ThreadPool.BusyIOThreads"]=Math.Max(0,ioMax-ioAvailable);
            long work=ThreadPool.CompletedWorkItemCount;v["ThreadPool.CompletedPerSecond"]=Math.Max(0,work-lastThreadCompletions)/seconds;lastThreadCompletions=work;
            using(var process=Process.GetCurrentProcess())
            {
                try{v["Process.Threads"]=process.Threads.Count;v["Process.Handles"]=process.HandleCount;v["Process.WorkingSetBytes"]=process.WorkingSet64;}catch(Exception e)when(e is System.ComponentModel.Win32Exception or InvalidOperationException or NotSupportedException){}
            }
            var sockets=ProcessSocketCounters.Capture();v["Process.TCPEndpoints"]=sockets.Tcp;v["Process.UDPEndpoints"]=sockets.Udp;v["Process.FileDescriptors"]=null;
            foreach(string key in new[]{"db.client.connections.usage.used","db.client.connections.usage.idle","db.client.connections.max","db.client.connections.pending_requests","db.client.connections.timeouts","http.client.open_connections.active","http.client.open_connections.idle","http.client.active_requests","kestrel.queued_connections","kestrel.queued_requests"})
                v[key]=observed.TryGetValue(key,out double value)&&value>=0?value:null;
            double? used=v["db.client.connections.usage.used"],capacity=v["db.client.connections.max"];
            v["DB.PoolUsePercent"]=used.HasValue&&capacity is >0?used/capacity*100:null;
            double? httpActive=v["http.client.open_connections.active"],httpIdle=v["http.client.open_connections.idle"];
            v["HTTP.PoolActiveSharePercent"]=httpActive.HasValue&&httpIdle.HasValue&&httpActive+httpIdle>0?100*httpActive/(httpActive+httpIdle):null;
            foreach(string name in new[]{"db.client.connections.wait_time","http.client.request.time_in_queue","http.client.request.duration"})
                v[name+".mean_ms"]=windowCounts.GetValueOrDefault(name)>0?windowSums[name]*1000/windowCounts[name]:null;
            VoiceRoster.Read(v);
            var circuit=ResultRecoveryCircuit.Snapshot();v["Circuit.ResultRecovery.State"]=circuit.State;
            v["Circuit.ResultRecovery.Failures"]=circuit.ConsecutiveFailures;v["Circuit.ResultRecovery.RetryAfterSeconds"]=circuit.RetryAfterSeconds;v["Circuit.ResultRecovery.Rejected"]=circuit.Rejected;
            var states=new Dictionary<string,string>
            {
                ["CircuitBreaker"]=$"ResultRecovery:State={circuit.State};Threshold=3;CooldownSeconds=10;Scope=validated local outbox delivery only",
                ["FileDescriptors"]="Unavailable on Windows; handles include more than files",
                ["Sockets"]="TCP rows + UDP endpoints owned by PID, IPv4/IPv6; not exact native socket handle count",
                ["Cache"]="VoiceParticipantDirectory only; cumulative lookups; estimated string payload excludes container overhead; expiry != eviction",
                ["GC"]="Managed CLR only; collection counts include higher generations; last-GC heap is not exact bytes reclaimed",
                ["HTTPPoolCapacity"]="No single global capacity across origins/protocols; connection counts and measured queue time only",
                ["Scope"]="5-second process aggregates; request TPS, not DB transactions/s; no input-complexity proof"
            };
            if(observationFailed)states["ObservableCounters"]="Provider callback failed; some observable values unavailable";
            v["Diagnostics.SampleMs"]=(Stopwatch.GetTimestamp()-samplingStarted)*1000d/Stopwatch.Frequency;
            var result=new BackendDiagnosticSnapshot(1,captureId,DateTimeOffset.UtcNow,Environment.ProcessId,seconds,v,states,details);
            Volatile.Write(ref latest,result);return result;
        }
    }
    private readonly HashSet<string> observableNames=[];
    private static double Now()=>Stopwatch.GetTimestamp()/(double)Stopwatch.Frequency;
    public void Dispose()=>listener.Dispose();
}

internal sealed class BackendDiagnosticsWorker(BackendDiagnostics metrics,IWebHostEnvironment environment,IConfiguration configuration,ILogger<BackendDiagnosticsWorker> logger):BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken ct)
    {
        if(configuration.GetValue("Diagnostics:Enabled",false))metrics.SetCapture(true);
        string directory=Path.GetFullPath(Path.Combine(environment.ContentRootPath,"..","..","Saved","Diagnostics","BackendMetrics"));
        var writer=new BoundedDiagnosticWriter(directory);bool warned=false;
        using var timer=new PeriodicTimer(TimeSpan.FromSeconds(5));
        while(await timer.WaitForNextTickAsync(ct))
        {
            if(metrics.Sample() is not{} sample)continue;
            try
            {
                await writer.WriteAsync(sample,ct);warned=false;
            }
            catch(Exception ex)when(ex is IOException or UnauthorizedAccessException or InvalidDataException)
            {if(!warned)logger.LogWarning("진단 기록을 저장하지 못했습니다. 게임 요청 처리는 계속됩니다. ({Type})",ex.GetType().Name);warned=true;}
        }
    }
}

internal static class BackendDiagnosticsEndpoints
{
    public static void UseBackendDiagnostics(this WebApplication app)
    {
        var metrics=app.Services.GetRequiredService<BackendDiagnostics>();
        app.Use(async(context,next)=>
        {
            // Dashboard traffic must not inflate measured gameplay/API throughput or error rate.
            if(context.Request.Path.StartsWithSegments("/api/admin/diagnostics")){await next();return;}
            long started=Stopwatch.GetTimestamp();bool threw=false;metrics.BeginRequest();
            try{await next();}catch{threw=true;throw;}
            finally{metrics.EndRequest(context.Response.StatusCode,context.RequestAborted.IsCancellationRequested,threw,Stopwatch.GetTimestamp()-started);}
        });
    }
    public static void MapBackendDiagnostics(this WebApplication app)
    {
        app.MapGet("/api/admin/diagnostics",(HttpRequest request,AdministratorOptions admin,BackendDiagnostics metrics)=>
            admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"].ToString())?Results.Ok(new{enabled=metrics.CaptureEnabled,snapshot=metrics.Latest}):Results.Unauthorized());
        app.MapPost("/api/admin/diagnostics",(bool enabled,HttpRequest request,AdministratorOptions admin,BackendDiagnostics metrics)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"].ToString()))return Results.Unauthorized();
            metrics.SetCapture(enabled);return Results.Ok(new{enabled=metrics.CaptureEnabled});
        });
    }
}

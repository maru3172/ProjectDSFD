using System.Text.Json;

internal sealed record CrashArtifactPolicy(bool AutoCleanupEnabled=false,int RetentionDays=30,
    int MaximumTotalMiB=2048,bool AllowMinidumps=false);
internal sealed record CrashArtifactStorageStatus(CrashArtifactPolicy Policy,long StoredBytes,int Files,
    string Warning="덤프는 메모리의 민감정보를 포함할 수 있습니다. 자동 정리를 켜면 기간·용량을 초과한 첨부를 영구 삭제합니다. 보고서 메타데이터는 유지합니다.");

internal sealed class CrashArtifactStore
{
    internal readonly SemaphoreSlim Gate=new(1,1);
    internal readonly string Root;
    private readonly string policyPath;
    private readonly TimeProvider clock;
    private CrashArtifactPolicy policy=new();
    public CrashArtifactStore(IHostEnvironment environment,TimeProvider clock)
        : this(environment.ContentRootPath,clock) { }
    internal CrashArtifactStore(string contentRoot,TimeProvider clock)
    {
        Root=Path.GetFullPath(Path.Combine(contentRoot,"Saved","CrashReportArtifacts"));
        policyPath=Path.Combine(contentRoot,"Saved","CrashArtifactPolicy.json");this.clock=clock;
        try
        {
            if(File.Exists(policyPath))
            {
                var loaded=JsonSerializer.Deserialize<CrashArtifactPolicy>(File.ReadAllText(policyPath));
                if(loaded is not null && IsValid(loaded))policy=loaded;
            }
        }
        catch(IOException) { } catch(JsonException) { } catch(UnauthorizedAccessException) { }
    }
    public CrashArtifactPolicy Policy=>Volatile.Read(ref policy);
    internal static bool IsValid(CrashArtifactPolicy value)=>value.RetentionDays is >=1 and <=3650 && value.MaximumTotalMiB is >=1 and <=65536;
    internal static bool IsOrdinaryPath(string path)
    {
        for(var current=new DirectoryInfo(path);current is not null;current=current.Parent)
            if(current.Exists && (current.Attributes & FileAttributes.ReparsePoint)!=0)return false;
        return true;
    }
    internal string ReportDirectory(Guid id)
    {
        string path=Path.Combine(Root,id.ToString("D"));
        if(!IsOrdinaryPath(path))throw new IOException("크래시 첨부 경로에 링크가 있어 접근을 차단했습니다.");
        return path;
    }
    internal FileInfo[] Files()
    {
        if(!Directory.Exists(Root))return [];
        if(!IsOrdinaryPath(Root))throw new IOException("크래시 첨부 저장 경로에 링크가 있습니다.");
        return Directory.EnumerateDirectories(Root).Where(path=>Guid.TryParseExact(Path.GetFileName(path),"D",out _) && IsOrdinaryPath(path))
            .SelectMany(path=>Directory.EnumerateFiles(path)).Where(path=>CrashArtifactEndpoints.Names.Contains(Path.GetFileName(path)) ||
                (Path.GetExtension(path)==".upload" && Guid.TryParseExact(Path.GetFileNameWithoutExtension(path),"N",out _)))
            .Select(path=>new FileInfo(path)).Where(file=>(file.Attributes & FileAttributes.ReparsePoint)==0).ToArray();
    }
    internal bool HasCapacity(long incomingBytes,string destination)
    {
        long used=Files().Sum(file=>file.Length);
        long replacing=File.Exists(destination)?new FileInfo(destination).Length:0;
        return incomingBytes>=0 && used-replacing+incomingBytes <= (long)Policy.MaximumTotalMiB*1024*1024;
    }
    public async Task<CrashArtifactStorageStatus> StatusAsync(CancellationToken ct)
    {
        await Gate.WaitAsync(ct);
        try {var files=Files();return new(Policy,files.Sum(f=>f.Length),files.Length);}
        finally {Gate.Release();}
    }
    public async Task SetPolicyAsync(CrashArtifactPolicy value,CancellationToken ct)
    {
        if(!IsValid(value))throw new ArgumentException("보관 기간 또는 용량 범위가 올바르지 않습니다.");
        await Gate.WaitAsync(ct);
        try
        {
            if(!IsOrdinaryPath(Path.GetDirectoryName(policyPath)!))throw new IOException("정책 저장 경로에 링크가 있습니다.");
            Directory.CreateDirectory(Path.GetDirectoryName(policyPath)!);
            string temporary=policyPath+".tmp";
            try { await File.WriteAllTextAsync(temporary,JsonSerializer.Serialize(value),ct);File.Move(temporary,policyPath,true); }
            finally {if(File.Exists(temporary))File.Delete(temporary);}
            Volatile.Write(ref policy,value);
        }
        finally {Gate.Release();}
    }
    public async Task<int> CleanupAsync(CancellationToken ct)
    {
        await Gate.WaitAsync(ct);
        try
        {
            var current=Policy;if(!current.AutoCleanupEnabled)return 0;
            var files=Files().OrderBy(f=>f.LastWriteTimeUtc).ToArray();
            long total=files.Sum(f=>f.Length), maximum=(long)current.MaximumTotalMiB*1024*1024;
            var cutoff=clock.GetUtcNow().UtcDateTime.AddDays(-current.RetentionDays);int removed=0;
            foreach(var file in files)
            {
                ct.ThrowIfCancellationRequested();
                if(file.LastWriteTimeUtc>=cutoff && total<=maximum)continue;
                // Canonical file names in validated GUID directories only. Never recursive deletion.
                if(!IsOrdinaryPath(file.DirectoryName!) || (File.GetAttributes(file.FullName)&FileAttributes.ReparsePoint)!=0)continue;
                long length=file.Length;file.Delete();total-=length;removed++;
            }
            return removed;
        }
        finally {Gate.Release();}
    }
}

internal sealed class CrashArtifactRetentionWorker(CrashArtifactStore store,ILogger<CrashArtifactRetentionWorker> logger) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer=new PeriodicTimer(TimeSpan.FromMinutes(10));
        try
        {
            while(await timer.WaitForNextTickAsync(stoppingToken))
            {
                try
                {
                    int removed=await store.CleanupAsync(stoppingToken);
                    if(removed>0)logger.LogInformation("Crash attachment retention removed {Count} files; metadata retained.",removed);
                }
                catch(IOException ex){logger.LogWarning(ex,"Crash retention could not complete.");}
                catch(UnauthorizedAccessException ex){logger.LogWarning(ex,"Crash retention access denied.");}
            }
        }
        catch(OperationCanceledException) when(stoppingToken.IsCancellationRequested) { }
    }
}

internal static class CrashRetentionChecks
{
    private sealed class Clock : TimeProvider
    {
        internal DateTimeOffset Now=DateTimeOffset.UtcNow;
        public override DateTimeOffset GetUtcNow()=>Now;
    }
    public static async Task RunAsync(Action<bool,string> check)
    {
        string fixture=Path.GetFullPath(Path.Combine("Saved","FeatureChecks","CrashRetention",Guid.NewGuid().ToString("N")));
        var clock=new Clock();var store=new CrashArtifactStore(fixture,clock);
        check(!store.Policy.AutoCleanupEnabled && !store.Policy.AllowMinidumps,"기본적으로 자동 삭제·덤프 수집 안 함");
        string directory=store.ReportDirectory(Guid.NewGuid());Directory.CreateDirectory(directory);
        string old=Path.Combine(directory,"Game.log"),recent=Path.Combine(directory,"CrashContext.xml"),unknown=Path.Combine(directory,"never-delete.txt");
        await File.WriteAllTextAsync(old,"fixture old");File.SetLastWriteTimeUtc(old,clock.Now.UtcDateTime.AddDays(-40));
        await File.WriteAllTextAsync(recent,"fixture recent");await File.WriteAllTextAsync(unknown,"fixture unknown");
        check(await store.CleanupAsync(default)==0 && File.Exists(old),"정책 비활성화 상태에서 오래된 첨부 보존");
        await store.SetPolicyAsync(new(true,30,1,false),default);
        check(await store.CleanupAsync(default)==1 && !File.Exists(old) && File.Exists(recent),"명시적으로 활성화한 정책만 기간 만료 첨부 정리");
        check(File.Exists(unknown),"알 수 없는 파일이나 보고서 메타데이터는 정리 대상 아님");
        check(!store.HasCapacity(2*1024*1024,Path.Combine(directory,"Crash.dmp")),"전체 용량 한도를 넘는 첨부 거부");
        var reload=new CrashArtifactStore(fixture,clock);check(reload.Policy==store.Policy,"보관 정책 재시작 후 유지");
        await File.WriteAllBytesAsync(old,new byte[700000]);await File.WriteAllBytesAsync(recent,new byte[700000]);
        File.SetLastWriteTimeUtc(old,clock.Now.UtcDateTime.AddHours(-2));File.SetLastWriteTimeUtc(recent,clock.Now.UtcDateTime.AddHours(-1));
        check(await store.CleanupAsync(default)==1 && !File.Exists(old) && File.Exists(recent),"용량 초과 시 오래된 첨부 우선 정리");
        check(!CrashArtifactStore.IsValid(new(true,0,1,false)) && !CrashArtifactStore.IsValid(new(false,30,0,false)),"잘못된 보관 범위 거부");
        var status=await store.StatusAsync(default);check(status.Files==1 && status.StoredBytes==700000,"첨부 용량·개수 보고");
        await store.SetPolicyAsync(new(false,30,1,true),default);
        check(store.Policy.AllowMinidumps && await store.CleanupAsync(default)==0,"덤프 수집과 자동 삭제는 독립 설정");
    }
}

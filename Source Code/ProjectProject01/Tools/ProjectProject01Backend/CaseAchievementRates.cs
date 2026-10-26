using System.Runtime.CompilerServices;

// Public aggregate counts only; individual account achievements remain private.
internal static class CaseAchievementRates
{
    internal sealed record Entry(string Code,long Achieved,double Percentage);
    internal sealed record Snapshot(long TotalPlayers,DateTimeOffset UpdatedAtUtc,Entry[] Achievements);
    private sealed class Cache
    {
        internal readonly SemaphoreSlim Gate=new(1,1);
        internal Snapshot? Value;
        internal long UpdatedAt;
    }
    private static readonly ConditionalWeakTable<AuthDatabase,Cache> Caches=new();
    internal static async Task<Snapshot> GetAsync(AuthDatabase db,CancellationToken ct)
    {
        var cache=Caches.GetValue(db,_=>new Cache());await cache.Gate.WaitAsync(ct);
        try
        {
            if(cache.Value is not null&&Environment.TickCount64-cache.UpdatedAt<30000)return cache.Value;
            cache.Value=await ReadAsync(db,ct);cache.UpdatedAt=Environment.TickCount64;return cache.Value;
        }
        finally{cache.Gate.Release();}
    }
    internal static async Task<Snapshot> ReadAsync(AuthDatabase db,CancellationToken ct)
    {
        await using var c=await db.OpenConnectionAsync(ct);
        await using var tx=await c.BeginTransactionAsync(System.Data.IsolationLevel.RepeatableRead,ct);
        var totals=await CommunityEndpoints.Rows(c,tx,"SELECT COUNT(*) total FROM users",ct);
        long total=Convert.ToInt64(totals[0]["total"]);
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT code,COUNT(*) achieved FROM case_achievements GROUP BY code",ct);
        var counts=rows.ToDictionary(r=>(string)r["code"]!,r=>Convert.ToInt64(r["achieved"]));
        var entries=CaseRecords.AchievementIds.Select(code=>
        {
            long achieved=counts.GetValueOrDefault(code);
            return new Entry(code,achieved,total==0?0:Math.Clamp(100.0*achieved/total,0,100));
        }).ToArray();
        await tx.CommitAsync(ct);return new Snapshot(total,DateTimeOffset.UtcNow,entries);
    }
    internal static void Invalidate(AuthDatabase db)=>Caches.Remove(db);
}

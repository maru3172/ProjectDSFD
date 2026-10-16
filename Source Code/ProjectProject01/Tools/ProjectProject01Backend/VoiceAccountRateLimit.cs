internal sealed class VoiceAccountRateLimit(TimeProvider clock)
{
    private readonly object gate=new();
    private readonly Dictionary<ulong,(DateTimeOffset Started,int Count)> windows=[];
    internal const int MaximumAccounts=4096;
    internal const int RequestsPerMinute=30;
    public bool TryAcquire(ulong account,out int retryAfterSeconds)
    {
        lock(gate)
        {
            var now=clock.GetUtcNow();
            retryAfterSeconds=60;
            foreach(var id in windows.Where(x=>now-x.Value.Started>=TimeSpan.FromMinutes(1)).Select(x=>x.Key).ToArray())windows.Remove(id);
            if(windows.TryGetValue(account,out var value))
            {
                retryAfterSeconds=Math.Max(1,(int)Math.Ceiling(60-(now-value.Started).TotalSeconds));
                if(value.Count>=RequestsPerMinute)return false;
                windows[account]=(value.Started,value.Count+1);return true;
            }
            if(windows.Count>=MaximumAccounts)return false;
            windows[account]=(now,1);return true;
        }
    }
}

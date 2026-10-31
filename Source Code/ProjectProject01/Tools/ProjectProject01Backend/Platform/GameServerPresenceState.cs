// File: Tools/ProjectProject01Backend/GameServerPresenceState.cs
// Keeps only short-lived dedicated-server liveness state; no secret or player data is stored here.

internal sealed class GameServerPresenceState
{
    private long _lastHeartbeatUtcTicks;

    public void MarkAlive() => Interlocked.Exchange(ref _lastHeartbeatUtcTicks, DateTime.UtcNow.Ticks);

    public void MarkOffline() => Interlocked.Exchange(ref _lastHeartbeatUtcTicks, 0);

    public bool IsAlive(TimeSpan maximumAge)
    {
        var ticks = Interlocked.Read(ref _lastHeartbeatUtcTicks);
        if (ticks <= 0)
        {
            return false;
        }

        var lastHeartbeatUtc = new DateTime(ticks, DateTimeKind.Utc);
        return DateTime.UtcNow - lastHeartbeatUtc <= maximumAge;
    }

    public DateTime? LastHeartbeatUtc
    {
        get
        {
            var ticks = Interlocked.Read(ref _lastHeartbeatUtcTicks);
            return ticks > 0 ? new DateTime(ticks, DateTimeKind.Utc) : null;
        }
    }
}

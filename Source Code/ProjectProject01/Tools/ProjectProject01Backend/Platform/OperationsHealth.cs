using System.Diagnostics;

internal sealed class OperationsHealth(ILogger<OperationsHealth> logger)
{
    internal sealed record Incident(DateTimeOffset At, string Area, string Build, string Metric, int Value, int Limit);
    private readonly object gate = new();
    private readonly Dictionary<(string Area, string Build, string Metric), (long Minute, int Count)> counts = new();
    private readonly Queue<Incident> incidents = new();
    private readonly Dictionary<(string Area, string Build, string Metric), long> lastAlert = new();

    internal void Observe(string area, string build, string metric, int threshold, int? value = null)
    {
        var now = DateTimeOffset.UtcNow;
        long minute = now.ToUnixTimeSeconds() / 60;
        var key = (area, build, metric);
        Incident? incident = null;
        lock (gate)
        {
            if (counts.Count >= 80 && !counts.ContainsKey(key))
            {
                foreach (var old in counts.Where(entry => entry.Value.Minute < minute - 5).Select(entry => entry.Key).ToArray())
                {
                    counts.Remove(old);
                    lastAlert.Remove(old);
                }
                if (counts.Count >= 80) return;
            }
            counts.TryGetValue(key, out var prior);
            int measured = value ?? (prior.Minute == minute ? prior.Count + 1 : 1);
            counts[key] = (minute, measured);
            if (measured >= threshold && (!lastAlert.TryGetValue(key, out long last) || minute - last >= 5))
            {
                lastAlert[key] = minute;
                incident = new(now, area, build, metric, measured, threshold);
                if (incidents.Count == 100) incidents.Dequeue();
                incidents.Enqueue(incident);
            }
        }
        if (incident is not null)
            logger.LogWarning("서비스 이상 감지 {Area} {Build} {Metric} {Value}/{Limit}",
                area, build, metric, incident.Value, threshold);
    }

    internal Incident[] Recent()
    {
        lock (gate) return incidents.Reverse().ToArray();
    }

    internal static string Build(VersionCompatibilityOptions version, bool dedicated = false) =>
        dedicated ? version.DedicatedServerBuildVersion : version.ClientBuildVersion;

    public static void Use(WebApplication app)
    {
        var health = app.Services.GetRequiredService<OperationsHealth>();
        var version = app.Services.GetRequiredService<VersionCompatibilityOptions>();
        app.Use(async (context, next) =>
        {
            var path = context.Request.Path;
            string? area = path.Equals("/api/auth/login", StringComparison.OrdinalIgnoreCase) ? "로그인"
                : path.StartsWithSegments("/api/client-updates") ? "런처 업데이트"
                : path.StartsWithSegments("/api/rooms/matchmaking") ? "랜덤 매칭"
                : path.StartsWithSegments("/api/server/matches") ? "경기 결과 저장" : null;
            if (area is null) { await next(); return; }

            long start = Stopwatch.GetTimestamp();
            int status = 500;
            try { await next(); status = context.Response.StatusCode; }
            finally
            {
                string build = Build(version, area == "경기 결과 저장");
                if (status >= 500 || status == 429 || (area == "로그인" && status == 401))
                    health.Observe(area, build, "1분 내 실패 수", status == 401 ? 25 : 5);
                if (area != "런처 업데이트" && Stopwatch.GetElapsedTime(start) >= TimeSpan.FromSeconds(3))
                    health.Observe(area, build, "1분 내 3초 이상 요청 수", 5);
            }
        });
    }
}

internal sealed class OperationsHealthWorker(
    AuthDatabase database, OperationsHealth health, VersionCompatibilityOptions version, ILogger<OperationsHealthWorker> logger)
    : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken ct)
    {
        if (!database.IsConfigured) return;
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(30));
        while (await timer.WaitForNextTickAsync(ct))
        {
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                var rows = await CommunityEndpoints.Rows(connection, null, """
                    SELECT COALESCE(TIMESTAMPDIFF(SECOND, MIN(created_at_utc), UTC_TIMESTAMP(6)), 0) AS seconds
                    FROM random_match_searches
                    WHERE state='Waiting' AND last_seen_utc>UTC_TIMESTAMP(6)-INTERVAL 30 SECOND
                    """, ct);
                health.Observe("랜덤 매칭", OperationsHealth.Build(version), "현재 최장 대기(초)", 180,
                    Convert.ToInt32(rows[0]["seconds"]));
            }
            catch (Exception ex) when (!ct.IsCancellationRequested)
            {
                logger.LogDebug(ex, "운영 지표를 조회하지 못했습니다.");
                health.Observe("운영 DB", OperationsHealth.Build(version), "1분 내 조회 실패 수", 2);
            }
        }
    }
}

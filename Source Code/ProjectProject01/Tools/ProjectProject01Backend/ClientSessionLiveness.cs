using System.Security.Cryptography;
using System.Text;
using MySqlConnector;

internal static class ClientSessionLiveness
{
    // Longer than the match reconnect grace and typical travel; no process can send on force-kill.
    internal const int GraceSeconds=90;
    internal static void MapClientLiveness(this WebApplication app)
    {
        app.MapPost("/api/auth/heartbeat",async(HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(req,db,ct);if(user is null)return Results.Unauthorized();
            string token=req.Headers.Authorization.ToString()[7..].Trim();
            await using var c=await db.OpenConnectionAsync(ct);
            // Never resurrect an expired/revoked lease, including a heartbeat racing the sweeper.
            int affected=await CommunityEndpoints.Exec(c,null,$"""
                UPDATE auth_session_liveness l JOIN auth_sessions s ON s.id=l.session_id
                SET l.deadline_utc=UTC_TIMESTAMP(6)+INTERVAL {GraceSeconds} SECOND
                WHERE s.access_token_hash=@hash AND s.revoked_at_utc IS NULL
                  AND s.access_expires_at_utc>UTC_TIMESTAMP(6) AND l.deadline_utc>UTC_TIMESTAMP(6)
                """,ct,("hash",SHA256.HashData(Encoding.UTF8.GetBytes(token))));
            return affected>0?Results.Ok(new{message="로그인 연결을 확인했습니다."}):Results.Unauthorized();
        }).RequireRateLimiting("session-heartbeat");
    }

    internal static async Task<int> SweepAsync(AuthDatabase db,CancellationToken ct)
    {
        if(!db.IsConfigured)return 0;
        await using var c=await db.OpenConnectionAsync(ct);
        var expired=await CommunityEndpoints.Rows(c,null,"""
            SELECT s.id,s.user_id FROM auth_sessions s JOIN auth_session_liveness l ON l.session_id=s.id
            WHERE s.revoked_at_utc IS NULL AND l.deadline_utc<=UTC_TIMESTAMP(6) ORDER BY s.id LIMIT 100
            """,ct);
        int count=0;
        foreach(var row in expired)
        {
            ulong id=Convert.ToUInt64(row["id"]),user=Convert.ToUInt64(row["user_id"]);
            await using var tx=await c.BeginTransactionAsync(ct);
            // Same account lock as login. An old process must never log out a newer login.
            await CommunityEndpoints.Rows(c,tx,"SELECT id FROM users WHERE id=@u FOR UPDATE",ct,("u",user));
            int revoked=await CommunityEndpoints.Exec(c,tx,"""
                UPDATE auth_sessions s JOIN auth_session_liveness l ON l.session_id=s.id SET s.revoked_at_utc=UTC_TIMESTAMP(6)
                WHERE s.id=@id AND s.revoked_at_utc IS NULL AND l.deadline_utc<=UTC_TIMESTAMP(6)
                """,ct,("id",id));
            if(revoked>0)
            {
                var live=await CommunityEndpoints.Rows(c,tx,"SELECT id FROM auth_sessions WHERE user_id=@u AND revoked_at_utc IS NULL AND access_expires_at_utc>UTC_TIMESTAMP(6)",ct,("u",user));
                if(live.Count==0)
                {
                    await LobbyEndpoints.RemoveUserFromCurrentRoomAsync(c,tx,user,ct);
                    await CommunityEndpoints.Exec(c,tx,"UPDATE social_presence SET presence='Offline',context='Other' WHERE user_id=@u;DELETE FROM social_room_invites WHERE sender_id=@u OR recipient_id=@u",ct,("u",user));
                }
                count++;
            }
            await tx.CommitAsync(ct);
        }
        return count;
    }
}

internal sealed class ClientSessionLivenessWorker(AuthDatabase db,ILogger<ClientSessionLivenessWorker> log):BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer=new PeriodicTimer(TimeSpan.FromSeconds(5));
        try
        {
            while(await timer.WaitForNextTickAsync(stoppingToken))
            {
                try{await ClientSessionLiveness.SweepAsync(db,stoppingToken);}
                catch(MySqlException ex){log.LogWarning("Client session cleanup deferred (database error {Code}).",ex.Number);}
            }
        }
        catch(OperationCanceledException)when(stoppingToken.IsCancellationRequested){}
    }
}

using System.Net;
using MySqlConnector;

// Explicit, process-local operator opt-in. Neither a client marker nor a Debug build is permission by itself.
internal sealed class CaseDebugCurrency
{
    private int enabled;
    internal bool Enabled=>Volatile.Read(ref enabled)==1;
    private void Set(bool value)=>Volatile.Write(ref enabled,value?1:0);
    internal sealed record Settings(bool Enabled);
    internal static bool DirectLoopback(HttpRequest request)=>request.HttpContext.Connection.RemoteIpAddress is {} remote&&IPAddress.IsLoopback(remote)
        &&(request.Host.Host.Equals("localhost",StringComparison.OrdinalIgnoreCase)||(IPAddress.TryParse(request.Host.Host.Trim('[',']'),out var host)&&IPAddress.IsLoopback(host)))
        &&!new[]{"Forwarded","X-Forwarded-For","X-Forwarded-Host","X-Forwarded-Proto"}.Any(h=>request.Headers.ContainsKey(h));
    internal bool CanGrant(HttpRequest request)=>Enabled&&DirectLoopback(request)&&LocalEditorTesting.IsLocalEditorClient(request);
    internal static async Task GrantAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string requestId,CancellationToken ct)
    {
        string key="DebugPoints:"+Guid.Parse(requestId).ToString("D");
        if((await CommunityEndpoints.Rows(c,tx,"SELECT reward_key FROM case_claims WHERE user_id=@u AND reward_key=@k",ct,("u",user),("k",key))).Count>0)return;
        await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_claims(user_id,reward_key) VALUES(@u,@k);UPDATE case_accounts SET credits=LEAST(1000000000,credits+5000) WHERE user_id=@u",ct,("u",user),("k",key));
    }
    internal static void Map(WebApplication app)
    {
        app.MapGet("/api/admin/case-records/debug-points",(HttpRequest request,AdministratorOptions admin,CaseDebugCurrency debug)=>
            admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"])&&DirectLoopback(request)?Results.Ok(new{enabled=debug.Enabled}):Results.Unauthorized()).RequireRateLimiting("admin");
        app.MapPut("/api/admin/case-records/debug-points",(Settings body,HttpRequest request,AdministratorOptions admin,CaseDebugCurrency debug)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"])||!DirectLoopback(request))return Results.Unauthorized();
            debug.Set(body.Enabled);return Results.Ok(new{enabled=debug.Enabled});
        }).RequireRateLimiting("admin");
    }
}

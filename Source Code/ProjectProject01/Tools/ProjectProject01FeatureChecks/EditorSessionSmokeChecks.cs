using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

// Called only AFTER IntegrationChecks has verified port 23307 and @@datadir.
// Uses real Unreal editor server processes, but only synthetic isolated accounts.
internal static class EditorSessionSmokeChecks
{
    private static async Task<JsonElement> Read(Task<HttpResponseMessage> pending)
    {
        using var response=await pending;
        if(!response.IsSuccessStatusCode)throw new Exception($"Editor smoke HTTP {(int)response.StatusCode}");
        using var json=JsonDocument.Parse(await response.Content.ReadAsStringAsync());return json.RootElement.Clone();
    }
    public static async Task RunAsync(AuthDatabase database,string root,string editor)
    {
        var builder=WebApplication.CreateBuilder(new WebApplicationOptions {ContentRootPath=Path.Combine(root,"Saved","FeatureChecks")});
        builder.Logging.ClearProviders();builder.Services.AddSingleton(database);
        builder.Services.AddSingleton<ServiceControlState>();
        builder.Services.AddSingleton<Microsoft.AspNetCore.Identity.IPasswordHasher<RoomPasswordRecord>,Microsoft.AspNetCore.Identity.PasswordHasher<RoomPasswordRecord>>();
        var config=new ConfigurationBuilder().AddInMemoryCollection(new Dictionary<string,string?> {
            ["Administrator:ApiKey"]=new string('a',48),["GameServer:SharedSecret"]=new string('s',48),["GameServer:TicketKey"]=Convert.ToBase64String(new byte[32])}).Build();
        builder.Services.AddSingleton(AdministratorOptions.FromConfiguration(config));
        builder.Services.AddRateLimiter(options=>{
            foreach(string name in new[]{"admin","lobby","game-server","game-ticket"})
                options.AddFixedWindowLimiter(name,limit=>{limit.PermitLimit=10000;limit.Window=TimeSpan.FromMinutes(1);});
        });
        var manager=new GameServerDemandManager(database,ProjectSecurityOptions.FromConfiguration(config),new GameServerProcessFactory(),
            Microsoft.Extensions.Logging.Abstractions.NullLogger<GameServerDemandManager>.Instance,TimeProvider.System);
        builder.Services.AddSingleton(manager);
        await using var app=builder.Build();app.Urls.Add("http://127.0.0.1:0");app.UseRateLimiter();
        app.MapGameServerDemand();app.MapProjectProject01Lobby("127.0.0.1:27777",ProjectSecurityOptions.FromConfiguration(config),VersionCompatibilityOptions.FromConfiguration(config),new GameServerPresenceState(),manager);
        await app.StartAsync();
        var players=new List<HttpClient>();var rooms=new HashSet<string>();
        using var admin=new HttpClient {BaseAddress=new Uri(app.Urls.Single())};admin.DefaultRequestHeaders.Add("X-ProjectProject01-Admin-Key",new string('a',48));
        try
        {
            for(int round=0;round<2;round++)
            {
                var configResponse=await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new {
                    enabled=true,executable=editor,projectRoot=root,editorLocalTest=true,editorBackendUrl=app.Urls.Single(),firstPort=27777,maximumConcurrent=1}));
                var group=new List<HttpClient>();
                for(int i=0;i<3;i++)
                {
                    var account=await OperationsIntegration.Player(database);
                    var client=new HttpClient {BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",account.Token);
                    players.Add(client);group.Add(client);
                }
                JsonElement room;
                if(round==0)
                {
                    room=(await Read(group[0].PostAsJsonAsync("/api/rooms",new {name="Editor real open match",isPublic=true}))).GetProperty("room");
                    string code=room.GetProperty("joinCode").GetString()!;
                    for(int i=1;i<3;i++) { await Read(group[i].PostAsJsonAsync($"/api/rooms/{code}/join",new{}));await Read(group[i].PostAsJsonAsync("/api/rooms/current/ready",new {ready=true})); }
                    room=(await Read(group[0].PostAsJsonAsync("/api/rooms/current/start",new{}))).GetProperty("room");
                }
                else
                {
                    foreach(var client in group)await Read(client.PostAsJsonAsync("/api/rooms/matchmaking/party",new{}));
                    foreach(var client in group)await Read(client.PostAsJsonAsync("/api/rooms/matchmaking/join",new {searchId=Guid.NewGuid().ToString("D")}));
                    room=(await Read(group[0].GetAsync("/api/rooms/current"))).GetProperty("room");
                    if(room.GetProperty("members").GetArrayLength()!=3)throw new Exception("Real random group did not form");
                }
                string id=room.GetProperty("roomId").GetString()!;rooms.Add(id);
                // The production countdown guard is exercised, not bypassed by aging SQL.
                using(var early=await group[0].PostAsJsonAsync("/api/rooms/current/confirm-start",new{}))
                    if(early.StatusCode!=HttpStatusCode.Conflict)throw new Exception("Premature editor admission accepted");
                await Task.Delay(5300);
                await Read(group[0].PostAsJsonAsync("/api/rooms/current/confirm-start",new{}));
                using(var early=await group[0].PostAsJsonAsync("/api/game-tickets",new{}))
                    if(early.StatusCode is not (HttpStatusCode.Conflict or HttpStatusCode.ServiceUnavailable))throw new Exception($"Unexpected pre-heartbeat ticket response {(int)early.StatusCode}");
                var deadline=DateTime.UtcNow.AddSeconds(85);
                var preparationWatch=System.Diagnostics.Stopwatch.StartNew();
                while(true)
                {
                    await manager.TickAsync(default);
                    foreach(var client in group)
                    {
                        var current=(await Read(client.GetAsync("/api/rooms/current"))).GetProperty("room");
                        if(client==group[0])room=current;
                    }
                    if(room.GetProperty("started").GetBoolean())break;
                    if(DateTime.UtcNow>deadline)throw new Exception("Real editor server readiness timed out; inspect Saved/GameServerSessions");
                    await Task.Delay(500);
                }
                int mannequins=0;
                foreach(var client in group)
                {
                    var ticket=await Read(client.PostAsJsonAsync("/api/game-tickets",new{}));
                    if(ticket.GetProperty("role").GetString()=="Mannequin")mannequins++;
                    if(ticket.GetProperty("travelUrl").GetString()!="127.0.0.1:27777")throw new Exception("Editor ticket targets wrong server");
                }
                if(mannequins!=1)throw new Exception("Editor match does not have one mannequin and two survivors");
                Console.WriteLine($"PASS real editor {(round==0?"open":"random")} flow: countdown -> managed Unreal server -> heartbeat -> 3 authenticated tickets (1:2); session preparation {preparationWatch.Elapsed.TotalSeconds:F2}s");
                await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new {enabled=false}));await manager.TickAsync(default);
                foreach(var client in group)await Read(client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            }
        }
        finally
        {
            await manager.ConfigureAsync(new(false),default);await manager.TickAsync(default);
            foreach(var client in players)client.Dispose();
            foreach(string id in rooms)await OperationsIntegration.Execute(database,$"DELETE FROM game_rooms WHERE id='{id}';");
            await app.StopAsync();
        }
    }
}

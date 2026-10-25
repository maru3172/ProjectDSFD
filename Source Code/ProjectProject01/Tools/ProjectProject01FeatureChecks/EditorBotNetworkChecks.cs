using System.Diagnostics;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;

internal static class EditorBotNetworkChecks
{
    internal static async Task RunAsync(AuthDatabase db,WebApplication app,HttpClient admin,GameServerDemandManager manager,
        IsolatedEditorGameProcessFactory factory,string root,string editor)
    {
        async Task<JsonElement> Read(Task<HttpResponseMessage> request)
        {using var response=await request;if(!response.IsSuccessStatusCode)throw new Exception($"UE bot network API {(int)response.StatusCode}");return JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement.Clone();}
        var account=await OperationsIntegration.Player(db);using var human=new HttpClient{BaseAddress=admin.BaseAddress};human.DefaultRequestHeaders.Authorization=new("Bearer",account.Token);
        var oldBots=app.Configuration["Bots:EnableDedicatedMatches"];var oldObservers=app.Configuration["Bots:EnableOpenObservers"];
        Process? client=null;string? roomId=null;
        try
        {
            app.Configuration["Bots:EnableDedicatedMatches"]="true";app.Configuration["Bots:EnableOpenObservers"]="true";
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=true,executable=editor,projectRoot=root,editorLocalTest=true,editorBackendUrl=app.Urls.Single(),firstPort=27777,maximumConcurrent=1}));
            var room=(await Read(human.PostAsJsonAsync("/api/rooms",new{name="Isolated actual observer + 3 bots",isPublic=false}))).GetProperty("room");roomId=room.GetProperty("roomId").GetString();
            await Read(human.PostAsJsonAsync("/api/rooms/current/participants",new{action="observe"}));
            for(int i=0;i<3;i++)await Read(human.PostAsJsonAsync("/api/rooms/current/bots",new{action="add",difficulty=1}));
            await Read(human.PostAsJsonAsync("/api/rooms/current/start",new{}));await Task.Delay(5200);
            await Read(human.PostAsJsonAsync("/api/rooms/current/confirm-start",new{}));
            var deadline=DateTime.UtcNow.AddSeconds(90);
            do
            {
                await manager.TickAsync(default);room=(await Read(human.GetAsync("/api/rooms/current"))).GetProperty("room");
                if(room.GetProperty("started").GetBoolean())break;
                if(DateTime.UtcNow>deadline)throw new Exception("Actual observer game server readiness timeout");await Task.Delay(500);
            }while(true);
            var ticket=await Read(human.PostAsJsonAsync("/api/game-tickets",new{}));
            string credentials=Path.Combine(factory.RunRoot,"ClientAdmission.json");
            await File.WriteAllTextAsync(credentials,JsonSerializer.Serialize(new{ticket=ticket.GetProperty("ticket").GetString(),encryptionKey=ticket.GetProperty("encryptionKey").GetString(),roomId=ticket.GetProperty("roomId").GetString(),matchId=ticket.GetProperty("matchId").GetString(),role="Observer",backend=app.Urls.Single()}));
            string clientLog=Path.Combine(factory.RunRoot,"ObserverClient.log");
            string commandEditor=Path.Combine(Path.GetDirectoryName(editor)!,"UnrealEditor-Cmd.exe");
            var start=new ProcessStartInfo(commandEditor){UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden,RedirectStandardOutput=true,RedirectStandardError=true};
            string travel=ticket.GetProperty("travelUrl").GetString()+"?EncryptionToken="+ticket.GetProperty("ticket").GetString()+"?GameTicket="+ticket.GetProperty("ticket").GetString()+"?LobbyRoomId="+ticket.GetProperty("roomId").GetString()+"?MatchId="+ticket.GetProperty("matchId").GetString();
            foreach(string argument in new[]{Path.Combine(root,"ProjectDFSD.uproject"),travel,"-ProjectProject01NetworkFixture="+credentials,
                "-game","-unattended","-nosplash","-nosound","-nullrhi","-NoCrashDialog","-ini:Game:[/Script/ProjectProject01.ProjectProject01GameInstance]:SecurityApiBaseUrl="+app.Urls.Single(),
                "-ProjectProject01ResultOutbox="+Path.Combine(factory.RunRoot,"ClientOutbox"),"-abslog="+clientLog})start.ArgumentList.Add(argument);
            client=Process.Start(start)??throw new Exception("Actual observer client did not start");
            client.BeginOutputReadLine();client.BeginErrorReadLine(); // Drain silently; never echo one-use ticket arguments.
            deadline=DateTime.UtcNow.AddSeconds(90);string serverLog=factory.ServerLogs.Last();
            while(true)
            {
                await manager.TickAsync(default);
                string text="";try
                {
                    // Unreal keeps its log writer open. The probe must permit that
                    // writer or a healthy admission is misreported as a timeout.
                    await using var file=new FileStream(serverLog,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);
                    using var reader=new StreamReader(file);text=await reader.ReadToEndAsync();
                }
                catch(IOException){}
                if(text.Contains("[OpenObserver] Admitted observer")&&text.Contains("started with 3 player(s)"))break;
                if(client.HasExited||DateTime.UtcNow>deadline)throw new Exception("Actual network observer/bot admission failed; inspect isolated EditorNetwork logs");
                await human.GetAsync("/api/rooms/current");await Task.Delay(500);
            }
            Console.WriteLine("PASS actual Unreal dedicated server + separate network client: one-use observer ticket, 3 server-owned bots, real match started; not a packaged/multi-PC or complete-map test");
        }
        finally
        {
            // These exact Process instances were created by this fixture. Never enumerate/kill user editors.
            if(client is not null){if(!client.HasExited){client.Kill(true);await client.WaitForExitAsync();}client.Dispose();}
            await manager.ConfigureAsync(new(false),default);await manager.TickAsync(default);
            app.Configuration["Bots:EnableDedicatedMatches"]=oldBots;app.Configuration["Bots:EnableOpenObservers"]=oldObservers;
            await using var c=await db.OpenConnectionAsync(default);
            await CommunityEndpoints.Exec(c,null,"DELETE FROM game_rooms WHERE id=@r;DELETE FROM users WHERE id=@u",default,("r",roomId),("u",account.Id));
        }
    }
}

using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Configuration;

// PvP match modes (Docs/ItemRoguelikeMode.md): who may set the mode, where, ready reset, everyone sees it, the frozen
// match snapshot and its ticket field, unofficial filtering, and one random-match queue per mode.
internal static class MatchModeChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin,IConfiguration config)
    {
        int count=0;void Check(bool ok,string message){if(!ok)throw new Exception(message);count++;Console.WriteLine("PASS "+message);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Match mode {(int)r.StatusCode}: {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode status,string message){using var r=await pending;Check(r.StatusCode==status,message+" "+r.StatusCode);}
        async Task Sql(string text,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,text,default,p);}
        var people=new List<(ulong Id,HttpClient Client)>();for(int i=0;i<3;i++){var p=await OperationsIntegration.Player(db);var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",p.Token);people.Add((p.Id,client));}
        using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};
        var host=people[0].Client;string? oldBots=config["Bots:EnableDedicatedMatches"];
        try
        {
            config["Bots:EnableDedicatedMatches"]="true";
            await Expect(anonymous.PutAsJsonAsync("/api/rooms/current/match-mode",new{roomId=Guid.NewGuid().ToString(),mode="ItemRoguelike"}),HttpStatusCode.Unauthorized,"미로그인 모드 변경 차단");
            var room=(await Read(host.PostAsJsonAsync("/api/bot-matches",new{role="Survivor",difficulty=2}))).GetProperty("room");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/match-mode",new{roomId=room.GetProperty("roomId").GetString(),mode="ItemRoguelike"}),HttpStatusCode.Conflict,"봇전 모드 변경 차단");
            await Read(host.PostAsJsonAsync("/api/rooms/current/leave",new{}));

            // Random matching: the solo staging room picks its queue before the search.
            room=(await Read(host.PostAsJsonAsync("/api/rooms/matchmaking/party",new{}))).GetProperty("room");
            string party=room.GetProperty("roomId").GetString()!;
            room=(await Read(host.PutAsJsonAsync("/api/rooms/current/match-mode",new{roomId=party,mode="ItemRoguelike"}))).GetProperty("room");
            Check(room.GetProperty("matchMode").GetString()=="ItemRoguelike","랜덤 매칭 대기방 모드 선택");
            await Sql("INSERT INTO random_match_searches(search_id,user_id,room_id) VALUES(@s,@u,@r)",("s",Guid.NewGuid().ToString("D")),("u",people[0].Id),("r",party));
            await Expect(host.PutAsJsonAsync("/api/rooms/current/match-mode",new{roomId=party,mode="Normal"}),HttpStatusCode.Conflict,"매칭 중 모드 변경 차단");
            await Sql("UPDATE random_match_searches SET state='Cancelled' WHERE room_id=@r",("r",party));
            await using(var c=await db.OpenConnectionAsync(default))
            {
                // One queue per mode: a roguelike staging room is never counted in the normal queue.
                var normal=await CommunityEndpoints.Rows(c,null,$"SELECT COUNT(*) n FROM game_rooms q WHERE q.id=@r AND {MatchModes.QueueFilter("q.id")}",default,("r",party),("queueMode","Normal"));
                var rogue=await CommunityEndpoints.Rows(c,null,$"SELECT COUNT(*) n FROM game_rooms q WHERE q.id=@r AND {MatchModes.QueueFilter("q.id")}",default,("r",party),("queueMode","ItemRoguelike"));
                Check(Convert.ToInt32(normal[0]["n"])==0&&Convert.ToInt32(rogue[0]["n"])==1,"랜덤 매칭은 모드별 대기열");
                string copy=Guid.NewGuid().ToString("D");
                await CommunityEndpoints.Exec(c,null,"INSERT INTO game_rooms(id,join_code,name,host_user_id,is_public) VALUES(@r,@code,'복귀 확인',@u,FALSE)",default,("r",copy),("code",Random.Shared.Next(10000000,99999999).ToString()),("u",people[0].Id));
                await using(var tx=await c.BeginTransactionAsync()){await MatchModes.CopyRoomAsync(c,tx,party,copy,default);await tx.CommitAsync();}
                Check(await MatchModes.RoomAsync(c,null,copy,default)=="ItemRoguelike","복귀한 대기방은 같은 대기열 유지");
                await CommunityEndpoints.Exec(c,null,"DELETE FROM game_rooms WHERE id=@r",default,("r",copy));
            }
            await Read(host.PostAsJsonAsync("/api/rooms/current/leave",new{}));

            // Open room: host only, valid modes only, everyone sees it, ready resets.
            room=(await Read(host.PostAsJsonAsync("/api/rooms",new{name="경기 모드 검증",isPublic=false}))).GetProperty("room");
            string roomId=room.GetProperty("roomId").GetString()!,code=room.GetProperty("joinCode").GetString()!;
            Check(room.GetProperty("matchMode").GetString()=="Normal","새 방은 일반 모드");
            foreach(var p in people.Skip(1))await Read(p.Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{}));
            await Expect(people[1].Client.PutAsJsonAsync("/api/rooms/current/match-mode",new{roomId,mode="ItemRoguelike"}),HttpStatusCode.Forbidden,"방장 외 모드 변경 차단");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/match-mode",new{roomId,mode="Chaos"}),HttpStatusCode.BadRequest,"알 수 없는 모드 거부");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/match-mode",new{roomId=Guid.NewGuid().ToString(),mode="ItemRoguelike"}),HttpStatusCode.NotFound,"다른 방 ID 차단");
            await Sql("UPDATE room_members SET is_ready=TRUE WHERE room_id=@r",("r",roomId));
            room=(await Read(host.PutAsJsonAsync("/api/rooms/current/match-mode",new{roomId,mode="ItemRoguelike"}))).GetProperty("room");
            Check(room.GetProperty("members").EnumerateArray().All(m=>!m.GetProperty("ready").GetBoolean()),"모드 변경은 전원 준비 해제");
            var peer=(await Read(people[1].Client.GetAsync("/api/rooms/current"))).GetProperty("room");
            Check(peer.GetProperty("matchMode").GetString()=="ItemRoguelike","참가자도 방 모드 확인");

            // Match start freezes the mode; the ticket carries it; the match is unofficial.
            string match=Guid.NewGuid().ToString("D");
            await Sql("UPDATE game_rooms SET status='Started',match_id=@m WHERE id=@r",("m",match),("r",roomId));
            await using(var c=await db.OpenConnectionAsync(default))
            {
                await using(var tx=await c.BeginTransactionAsync()){await BackendModules.OnMatchStartedAsync(c,tx,match,default);await tx.CommitAsync();}
                Check(await MatchModes.MatchAsync(c,null,match,default)=="ItemRoguelike","경기 시작 시 모드 고정");
                Check(await CustomRoomRules.IsCustomAsync(c,null,match,default),"아이템 로그라이크는 비공식 경기");
                var official=await CommunityEndpoints.Rows(c,null,$"SELECT COUNT(*) n FROM (SELECT @m match_id) x WHERE {CustomRoomRules.OfficialFilter("x.match_id")}",default,("m",match));
                Check(Convert.ToInt32(official[0]["n"])==0,"공식 기록 필터에서 제외");
                var fields=new Dictionary<string,object?>();
                await using(var tx=await c.BeginTransactionAsync()){await BackendModules.AddTicketFieldsAsync(c,tx,match,fields,default);await tx.CommitAsync();}
                Check(fields.TryGetValue("matchMode",out var mode)&&(string?)mode=="ItemRoguelike","입장 티켓에 모드 전달");
                var plain=new Dictionary<string,object?>();string plainMatch=Guid.NewGuid().ToString("D");
                await using(var tx=await c.BeginTransactionAsync()){await BackendModules.AddTicketFieldsAsync(c,tx,plainMatch,plain,default);await tx.CommitAsync();}
                Check((string?)plain["matchMode"]=="Normal"&&!await MatchModes.IsUnofficialAsync(c,null,plainMatch,default),"일반 경기는 일반 모드·공식");
            }
            await Sql("UPDATE game_rooms SET status='Waiting',match_id=NULL WHERE id=@r",("r",roomId));
            foreach(var p in people)await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
        }
        finally{config["Bots:EnableDedicatedMatches"]=oldBots;foreach(var p in people)p.Client.Dispose();}
        Console.WriteLine($"PASS: {count} match mode checks");
    }
}

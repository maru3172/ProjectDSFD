using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Configuration;

// Room visibility after creation: the host of a waiting room switches it between public (room list) and private
// (join code only); others, started rooms and server-made rooms (random match, bot match) are refused.
internal static class RoomVisibilityChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin,IConfiguration config)
    {
        int count=0;void Check(bool ok,string message){if(!ok)throw new Exception(message);count++;Console.WriteLine("PASS "+message);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Room visibility {(int)r.StatusCode}: {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode status,string message){using var r=await pending;Check(r.StatusCode==status,message+" "+r.StatusCode);}
        async Task Sql(string text,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,text,default,p);}
        var people=new List<(ulong Id,HttpClient Client)>();for(int i=0;i<2;i++){var p=await OperationsIntegration.Player(db);var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",p.Token);people.Add((p.Id,client));}
        using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};
        var host=people[0].Client;string? oldBots=config["Bots:EnableDedicatedMatches"];
        async Task<bool> Listed(string roomId)=>(await Read(people[1].Client.GetAsync("/api/rooms"))).GetProperty("rooms").EnumerateArray().Any(r=>r.GetProperty("roomId").GetString()==roomId);
        try
        {
            await Expect(anonymous.PutAsJsonAsync("/api/rooms/current/visibility",new{isPublic=true}),HttpStatusCode.Unauthorized,"미로그인 공개 변경 차단");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/visibility",new{isPublic=true}),HttpStatusCode.NotFound,"방 없이 공개 변경 차단");

            var room=(await Read(host.PostAsJsonAsync("/api/rooms",new{name="공개 전환 검증",isPublic=false}))).GetProperty("room");
            string roomId=room.GetProperty("roomId").GetString()!,code=room.GetProperty("joinCode").GetString()!;
            Check(!room.GetProperty("isPublic").GetBoolean()&&!await Listed(roomId),"비공개로 만든 방은 목록에 없음");
            await Read(people[1].Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{}));
            await Expect(people[1].Client.PutAsJsonAsync("/api/rooms/current/visibility",new{isPublic=true}),HttpStatusCode.Forbidden,"방장 외 공개 변경 차단");
            room=(await Read(host.PutAsJsonAsync("/api/rooms/current/visibility",new{isPublic=true}))).GetProperty("room");
            var peer=(await Read(people[1].Client.GetAsync("/api/rooms/current"))).GetProperty("room");
            Check(room.GetProperty("isPublic").GetBoolean()&&peer.GetProperty("isPublic").GetBoolean(),"공개로 바꾸면 모두에게 공개로 보임");
            await Read(people[1].Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            Check(await Listed(roomId),"공개로 바꾼 방은 목록에 보임");
            room=(await Read(host.PutAsJsonAsync("/api/rooms/current/visibility",new{isPublic=false}))).GetProperty("room");
            Check(!room.GetProperty("isPublic").GetBoolean()&&!await Listed(roomId),"다시 비공개로 바꾸면 목록에서 빠짐");
            await Read(people[1].Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{}));
            Check(true,"비공개방도 참가 코드로 입장");
            await Sql("UPDATE game_rooms SET status='Started' WHERE id=@r",("r",roomId));
            await Expect(host.PutAsJsonAsync("/api/rooms/current/visibility",new{isPublic=true}),HttpStatusCode.Conflict,"진행 중인 방은 공개 변경 차단");
            await Sql("UPDATE game_rooms SET status='Waiting' WHERE id=@r",("r",roomId));
            foreach(var p in people)await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));

            // Server-made rooms stay private.
            room=(await Read(host.PostAsJsonAsync("/api/rooms/matchmaking/party",new{}))).GetProperty("room");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/visibility",new{isPublic=true}),HttpStatusCode.Conflict,"랜덤 매칭 대기방 공개 변경 차단");
            await Read(host.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            config["Bots:EnableDedicatedMatches"]="true";
            room=(await Read(host.PostAsJsonAsync("/api/bot-matches",new{role="Survivor",difficulty=2}))).GetProperty("room");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/visibility",new{isPublic=true}),HttpStatusCode.Conflict,"봇전 방 공개 변경 차단");
            await Read(host.PostAsJsonAsync("/api/rooms/current/leave",new{}));
        }
        finally{config["Bots:EnableDedicatedMatches"]=oldBots;foreach(var p in people)p.Client.Dispose();}
        Console.WriteLine($"PASS: {count} room visibility checks");
    }
}

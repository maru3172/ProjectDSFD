using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Configuration;

// Room password after creation: the host of a waiting room sets, changes or removes it; members already inside stay,
// later joins need the new one. Others, started rooms and server-made rooms (random match, bot match) are refused.
internal static class RoomPasswordChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin,IConfiguration config)
    {
        int count=0;void Check(bool ok,string message){if(!ok)throw new Exception(message);count++;Console.WriteLine("PASS "+message);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Room password {(int)r.StatusCode}: {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode status,string message){using var r=await pending;Check(r.StatusCode==status,message+" "+r.StatusCode);}
        async Task Sql(string text,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,text,default,p);}
        var people=new List<(ulong Id,HttpClient Client)>();for(int i=0;i<3;i++){var p=await OperationsIntegration.Player(db);var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",p.Token);people.Add((p.Id,client));}
        using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};
        var host=people[0].Client;string? oldBots=config["Bots:EnableDedicatedMatches"];
        try
        {
            await Expect(anonymous.PutAsJsonAsync("/api/rooms/current/password",new{password="abcd"}),HttpStatusCode.Unauthorized,"미로그인 비밀번호 변경 차단");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/password",new{password="abcd"}),HttpStatusCode.NotFound,"방 없이 비밀번호 변경 차단");

            var room=(await Read(host.PostAsJsonAsync("/api/rooms",new{name="비밀번호 변경 검증",isPublic=false}))).GetProperty("room");
            string roomId=room.GetProperty("roomId").GetString()!,code=room.GetProperty("joinCode").GetString()!;
            Check(!room.GetProperty("hasPassword").GetBoolean(),"비밀번호 없이 만든 방은 hasPassword=false");
            await Read(people[1].Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{}));
            await Expect(people[1].Client.PutAsJsonAsync("/api/rooms/current/password",new{password="abcd"}),HttpStatusCode.Forbidden,"방장 외 비밀번호 변경 차단");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/password",new{password="abc"}),HttpStatusCode.BadRequest,"4자 미만 비밀번호 거부");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/password",new{password=new string('a',65)}),HttpStatusCode.BadRequest,"64자 초과 비밀번호 거부");

            room=(await Read(host.PutAsJsonAsync("/api/rooms/current/password",new{password="first1"}))).GetProperty("room");
            var peer=(await Read(people[1].Client.GetAsync("/api/rooms/current"))).GetProperty("room");
            Check(room.GetProperty("hasPassword").GetBoolean()&&peer.GetProperty("hasPassword").GetBoolean(),"비밀번호를 걸면 모두에게 설정됨으로 보임");
            Check(peer.GetProperty("roomId").GetString()==roomId,"이미 들어와 있던 인원은 그대로 남음");
            await Expect(people[2].Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{}),HttpStatusCode.Unauthorized,"비밀번호 없이 새 입장 차단");
            await Expect(people[2].Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{password="wrong1"}),HttpStatusCode.Unauthorized,"틀린 비밀번호 입장 차단");

            await Read(host.PutAsJsonAsync("/api/rooms/current/password",new{password="second2"}));
            await Expect(people[2].Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{password="first1"}),HttpStatusCode.Unauthorized,"바꾸기 전 비밀번호 입장 차단");
            await Read(people[2].Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{password="second2"}));
            Check(true,"바꾼 비밀번호로 입장");
            await Read(people[2].Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));

            room=(await Read(host.PutAsJsonAsync("/api/rooms/current/password",new{password=""}))).GetProperty("room");
            Check(!room.GetProperty("hasPassword").GetBoolean(),"빈 비밀번호로 해제");
            await Read(people[2].Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{}));
            Check(true,"해제 후 비밀번호 없이 입장");

            await Sql("UPDATE game_rooms SET status='Started' WHERE id=@r",("r",roomId));
            await Expect(host.PutAsJsonAsync("/api/rooms/current/password",new{password="abcd"}),HttpStatusCode.Conflict,"진행 중인 방은 비밀번호 변경 차단");
            await Sql("UPDATE game_rooms SET status='Waiting' WHERE id=@r",("r",roomId));
            foreach(var p in people)await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));

            // Server-made rooms never get a password.
            await Read(host.PostAsJsonAsync("/api/rooms/matchmaking/party",new{}));
            await Expect(host.PutAsJsonAsync("/api/rooms/current/password",new{password="abcd"}),HttpStatusCode.Conflict,"랜덤 매칭 대기방 비밀번호 차단");
            await Read(host.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            config["Bots:EnableDedicatedMatches"]="true";
            await Read(host.PostAsJsonAsync("/api/bot-matches",new{role="Survivor",difficulty=2}));
            await Expect(host.PutAsJsonAsync("/api/rooms/current/password",new{password="abcd"}),HttpStatusCode.Conflict,"봇전 방 비밀번호 차단");
            await Read(host.PostAsJsonAsync("/api/rooms/current/leave",new{}));
        }
        finally{config["Bots:EnableDedicatedMatches"]=oldBots;foreach(var p in people)p.Client.Dispose();}
        Console.WriteLine($"PASS: {count} room password checks");
    }
}

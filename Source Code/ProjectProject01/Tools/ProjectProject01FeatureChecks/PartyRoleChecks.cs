using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Configuration;

internal static class PartyRoleChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin,IConfiguration configuration)
    {
        int checks=0;void Check(bool ok,string name){if(!ok)throw new Exception(name);checks++;Console.WriteLine("PASS "+name);}
        var people=new List<(ulong Id,HttpClient Client,string Account)>();
        Task<JsonElement> Read(Task<HttpResponseMessage> pending)=>PartyAdmissionChecks.ReadWithConsent(pending,db,people);
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode expected,string name){using var r=await pending;Check(r.StatusCode==expected,name+" "+r.StatusCode);}
        async Task Sql(string sql,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,p);}
        for(int i=0;i<4;i++)
        {
            var p=await OperationsIntegration.Player(db);var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",p.Token);
            await using var c=await db.OpenConnectionAsync(default);var rows=await CommunityEndpoints.Rows(c,null,"SELECT account_id FROM users WHERE id=@u",default,("u",p.Id));
            people.Add((p.Id,client,(string)rows[0]["account_id"]!));
        }
        using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};
        var host=people[0].Client;string? oldBots=configuration["Bots:EnableDedicatedMatches"];
        Task<JsonElement> Act(HttpClient c,string action,string? target=null,string? mode=null,string? role=null)=>Read(c.PostAsJsonAsync("/api/parties/action",new{action,target,mode,role}));
        async Task<string> Invite(int index)
        {
            await Act(host,"Invite",people[index].Account);
            var snapshot=await Read(people[index].Client.GetAsync("/api/case-records"));return snapshot.GetProperty("partyState").GetProperty("invites")[0].GetProperty("id").GetString()!;
        }
        try
        {
            for(int i=1;i<4;i++)await Sql("INSERT INTO social_friends(user_low,user_high) VALUES(@lo,@hi)",("lo",Math.Min(people[0].Id,people[i].Id)),("hi",Math.Max(people[0].Id,people[i].Id)));
            await Expect(anonymous.PostAsJsonAsync("/api/parties/action",new{action="Create",mode="Open"}),HttpStatusCode.Unauthorized,"미로그인 파티 조작 차단");
            await Expect(host.PostAsJsonAsync("/api/parties/action",new{action="Create",mode="Random"}),HttpStatusCode.BadRequest,"랜덤 파티 생성 차단");
            await Act(host,"Create");
            var commonParty=(await Read(host.GetAsync("/api/case-records"))).GetProperty("partyState").GetProperty("party");
            Check(commonParty.GetProperty("mode").GetString()=="Common"&&commonParty.GetProperty("maximum").GetInt32()==3,"모드 구분 없는 공통 파티 생성");
            string invite=await Invite(1);
            await Expect(people[2].Client.PostAsJsonAsync("/api/parties/action",new{action="Accept",target=invite}),HttpStatusCode.NotFound,"타인의 파티 초대 수락 차단");
            Check((await Read(people[1].Client.GetAsync("/api/case-records"))).GetProperty("partyState").GetProperty("party").ValueKind==JsonValueKind.Null,"초대는 수락 전 자동 참가하지 않음");
            await Act(people[1].Client,"Accept",invite);
            await Expect(people[1].Client.PostAsJsonAsync("/api/parties/action",new{action="Kick",target=people[0].Id.ToString()}),HttpStatusCode.Forbidden,"파티원은 파티장을 추방할 수 없음");
            await Expect(host.PostAsJsonAsync("/api/rooms/matchmaking/party",new{}),HttpStatusCode.Conflict,"파티 상태에서는 솔로 랜덤 매칭 진입 차단");
            var room=(await Read(host.PostAsJsonAsync("/api/rooms",new{name="파티 원자 입장",isPublic=false}))).GetProperty("room");
            Check(room.GetProperty("members").GetArrayLength()==2,"파티장이 방을 만들면 파티원 동반 입장");
            await Act(host,"Role",role:"Mannequin");
            await Expect(people[1].Client.PostAsJsonAsync("/api/parties/action",new{action="Role",role="Mannequin"}),HttpStatusCode.Conflict,"오픈매칭 방 안에서 마네킹 1명 역할 정원");
            await Act(people[1].Client,"Role",role:"Survivor");
            Check((await Read(people[1].Client.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("roomId").GetString()==room.GetProperty("roomId").GetString(),"파티원도 같은 권위 방을 조회");
            foreach(var p in people.Take(2))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            // Non-friend open-room peers can invite, but membership changes only on the recipient's acceptance.
            room=(await Read(people[2].Client.PostAsJsonAsync("/api/rooms",new{name="같은 방 파티 초대",isPublic=false}))).GetProperty("room");
            await Read(people[3].Client.PostAsJsonAsync("/api/rooms/"+room.GetProperty("joinCode").GetString()+"/join",new{}));
            await Act(people[2].Client,"Invite",people[3].Account);
            var inviteState=await Read(people[3].Client.GetAsync("/api/case-records"));
            Check(inviteState.GetProperty("partyState").GetProperty("party").ValueKind==JsonValueKind.Null,"오픈매칭 파티 초대만으로 상대방을 합류시키지 않음");
            string peerInvite=inviteState.GetProperty("partyState").GetProperty("invites")[0].GetProperty("id").GetString()!;
            await Act(people[3].Client,"Decline",peerInvite);
            Check((await Read(people[3].Client.GetAsync("/api/case-records"))).GetProperty("partyState").GetProperty("party").ValueKind==JsonValueKind.Null,"파티 초대 거절 시 파티에 합류하지 않음");
            await Act(people[2].Client,"Invite",people[3].Account);
            peerInvite=(await Read(people[3].Client.GetAsync("/api/case-records"))).GetProperty("partyState").GetProperty("invites")[0].GetProperty("id").GetString()!;
            await Act(people[3].Client,"Accept",peerInvite);
            var peerParty=(await Read(people[3].Client.GetAsync("/api/case-records"))).GetProperty("partyState").GetProperty("party");
            Check(peerParty.GetProperty("members").GetArrayLength()==2,"같은 대기 오픈매칭 방의 비친구도 수락 후 공통 파티 결성");
            Check(peerParty.GetProperty("members")[0].GetProperty("level").GetInt32()>=1,"파티 카드에 서버 검증 계정 레벨 제공");
            Check((await Read(people[3].Client.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("roomId").GetString()==room.GetProperty("roomId").GetString(),"파티 수락으로 현재 오픈매칭 방을 이동시키지 않음");
            await Expect(people[3].Client.PostAsJsonAsync("/api/parties/action",new{action="Kick",target=people[2].Id.ToString()}),HttpStatusCode.Forbidden,"파티원 우클릭 조작으로 파티장 강퇴 위조 차단");
            await Act(people[2].Client,"Kick",people[3].Id.ToString());
            Check((await Read(people[3].Client.GetAsync("/api/case-records"))).GetProperty("partyState").GetProperty("party").ValueKind==JsonValueKind.Null,"파티장 강퇴 후 상대 파티 해제");
            await Act(people[2].Client,"Leave");
            foreach(var p in people.Skip(2))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            room=(await Read(people[2].Client.PostAsJsonAsync("/api/rooms",new{name="파티 동반 참가",isPublic=true}))).GetProperty("room");
            await Read(host.PostAsJsonAsync("/api/rooms/"+room.GetProperty("joinCode").GetString()+"/join",new{}));
            Check((await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("members").GetArrayLength()==3,"기존 방 참가도 파티 전원 동반 입장");
            foreach(var p in people.Take(3))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            room=(await Read(people[2].Client.PostAsJsonAsync("/api/rooms",new{name="파티 초대 수락",isPublic=false}))).GetProperty("room");
            await Read(host.PostAsJsonAsync("/api/social/action",new{operation="Presence",presence="Online",context="Lobby"}));
            await Read(people[2].Client.PostAsJsonAsync("/api/social/action",new{operation="Invite",target=people[0].Account}));
            string roomInvite;
            await using(var c=await db.OpenConnectionAsync(default)){var rows=await CommunityEndpoints.Rows(c,null,"SELECT CAST(id AS CHAR) id FROM social_room_invites WHERE recipient_id=@u",default,("u",people[0].Id));roomInvite=(string)rows[0]["id"]!;}
            await Read(host.PostAsJsonAsync("/api/social/action",new{operation="AcceptInvite",target=roomInvite}));
            Check((await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("members").GetArrayLength()==3,"친구 방 초대 수락도 파티 전원 원자 입장");
            foreach(var p in people.Take(3))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            invite=await Invite(2);await Act(people[2].Client,"Accept",invite);
            await Expect(host.PostAsJsonAsync("/api/parties/action",new{action="Invite",target=people[3].Account}),HttpStatusCode.Conflict,"파티 최대 3명 정원");
            configuration["Bots:EnableDedicatedMatches"]="true";
            await Expect(host.PostAsJsonAsync("/api/bot-matches",new{role="Any",difficulty=3}),HttpStatusCode.Conflict,"공통 파티 3명은 봇전 입장 불가");
            await Expect(host.GetAsync("/api/rooms/current"),HttpStatusCode.NotFound,"봇전 인원 초과 실패 시 방 생성 롤백");
            room=(await Read(people[3].Client.PostAsJsonAsync("/api/rooms",new{name="자리 부족",isPublic=true}))).GetProperty("room");
            await Expect(host.PostAsJsonAsync("/api/rooms/"+room.GetProperty("joinCode").GetString()+"/join",new{}),HttpStatusCode.Conflict,"자리가 모자라면 파티 일부만 입장하지 않음");
            await Expect(people[1].Client.GetAsync("/api/rooms/current"),HttpStatusCode.NotFound,"입장 실패 시 파티원 방 정보도 없음");
            await Read(people[3].Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            await Act(people[2].Client,"Leave");await Act(people[1].Client,"Leave");await Act(host,"Leave");
            configuration["Bots:EnableDedicatedMatches"]="true";
            await Act(host,"Create");invite=await Invite(1);await Act(people[1].Client,"Accept",invite);
            room=(await Read(host.PostAsJsonAsync("/api/bot-matches",new{role="Mannequin",difficulty=3}))).GetProperty("room");
            Check(room.GetProperty("members").GetArrayLength()==2&&room.GetProperty("bots").GetArrayLength()==1,"봇전 파티 인원만큼 봇 자리 대체");
            await Expect(people[1].Client.PostAsJsonAsync("/api/parties/action",new{action="Role",role="Mannequin"}),HttpStatusCode.Conflict,"봇전에서 방장의 선택 역할도 마네킹 정원에 포함");
            await Expect(host.PostAsJsonAsync("/api/parties/action",new{action="Role",role="Any"}),HttpStatusCode.Conflict,"2인 봇전은 무작위 역할로 변경 불가");
            Check((await Read(host.GetAsync("/api/case-records"))).GetProperty("partyState").GetProperty("role").GetString()=="Survivor"&&(await Read(people[1].Client.GetAsync("/api/case-records"))).GetProperty("partyState").GetProperty("role").GetString()=="Survivor","2인 봇전은 두 사람의 역할을 생존자로 고정");
            await using(var c=await db.OpenConnectionAsync(default))await using(var tx=await c.BeginTransactionAsync())
            {
                string duoRoom=room.GetProperty("roomId").GetString()!;
                await LobbyEndpoints.AssignPlayingRolesAsync(c,tx,duoRoom,people.Take(2).Select(p=>p.Id).ToArray(),default);
                var humanRoles=await CommunityEndpoints.Rows(c,tx,"SELECT assigned_role FROM room_members WHERE room_id=@r",default,("r",duoRoom));
                var botRoles=await LobbyEndpoints.LoadRoomBotsAsync(c,tx,duoRoom,default);
                Check(humanRoles.All(r=>(string)r["assigned_role"]! =="Survivor")&&botRoles.Single().AssignedRole=="Mannequin","2인 봇전 실제 확정 역할은 생존자 두 명과 마네킹 봇 한 명");await tx.RollbackAsync();
            }
            foreach(var p in people.Take(2))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            room=(await Read(host.PostAsJsonAsync("/api/rooms",new{name="같은 공통 파티로 오픈매칭",isPublic=false}))).GetProperty("room");
            Check(room.GetProperty("members").GetArrayLength()==2,"봇전에 들어갔던 같은 공통 파티로 오픈매칭 입장");
            Check((await Read(people[1].Client.GetAsync("/api/case-records"))).GetProperty("partyState").GetProperty("role").GetString()=="Any","새 방은 이전 경기 역할을 강제 예약하지 않음");
            foreach(var p in people.Take(2))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            room=(await Read(host.PostAsJsonAsync("/api/bot-matches",new{role="Any",difficulty=2}))).GetProperty("room");
            Check(room.GetProperty("members").GetArrayLength()==2&&room.GetProperty("bots").GetArrayLength()==1,"같은 파티로 무작위 역할 봇전 생성 가능");
            foreach(var p in people.Take(2))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            await Sql("INSERT INTO auth_session_liveness(session_id,deadline_utc) SELECT id,UTC_TIMESTAMP(6)-INTERVAL 1 SECOND FROM auth_sessions WHERE user_id=@u ON DUPLICATE KEY UPDATE deadline_utc=UTC_TIMESTAMP(6)-INTERVAL 1 SECOND",("u",people[0].Id));
            await ClientSessionLiveness.SweepAsync(db,default);
            var after=await Read(people[1].Client.GetAsync("/api/case-records"));
            Check(after.GetProperty("partyState").GetProperty("party").GetProperty("leaderId").GetString()==people[1].Id.ToString(),"파티장 연결 만료 시 남은 파티원에게 이전");
            for(int i=0;i<50;i++)
            {
                Check(RoomRoleChoices.PreferredMannequinSlot(["Survivor","Mannequin","Survivor"],i)==1,"마네킹 희망 우선 배정");
                int fallback=RoomRoleChoices.PreferredMannequinSlot(["Survivor","Survivor","Survivor"],i);Check(fallback>=0&&fallback<3,"전원 동일 희망이라도 역할 배정 가능");
            }
            Check(RoomRoleChoices.CanForm(["Mannequin","Survivor","Survivor"],0)&&!RoomRoleChoices.CanForm(["Mannequin","Mannequin","Survivor"],0),"오픈·봇전은 역할 정원 고정");
            Console.WriteLine($"PASS: {checks} party and role HTTP/DB checks");
        }
        finally
        {
            configuration["Bots:EnableDedicatedMatches"]=oldBots;
            foreach(var p in people){await Sql("DELETE FROM game_rooms WHERE host_user_id=@u;DELETE FROM users WHERE id=@u",("u",p.Id));p.Client.Dispose();}
        }
    }
}

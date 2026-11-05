using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Configuration;

internal static class CustomRoomRulesChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin,GameServerDemandManager manager,DemandSessionChecks.FakeFactory factory,string output,IConfiguration config)
    {
        int count=0;void Check(bool ok,string message){if(!ok)throw new Exception(message);count++;Console.WriteLine("PASS "+message);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Custom rules {(int)r.StatusCode}: {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode status,string message){using var r=await pending;Check(r.StatusCode==status,message+" "+r.StatusCode);}
        async Task Sql(string text,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,text,default,p);}
        var people=new List<(ulong Id,HttpClient Client)>();for(int i=0;i<3;i++){var p=await OperationsIntegration.Player(db);var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",p.Token);people.Add((p.Id,client));}
        using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};using var server=new HttpClient{BaseAddress=admin.BaseAddress};server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
        var host=people[0].Client;var rules=new CustomRuleSet(true,6,4,300,ItemCards:true){AffixIds=["B09","C05"]};string? oldBots=config["Bots:EnableDedicatedMatches"];
        string? match=null;
        // Docs/CustomGamePlayground.md 2.2: same ranges and combinations as ProjectCustomMatchRules.h.
        var basic=new CustomRuleSet(true,3,6,900);
        Check(CustomRoomRules.WellFormed(basic with{InfiniteLives=true})&&!CustomRoomRules.WellFormed(basic with{InfiniteLives=true,TimeLimitSeconds=240})&&!CustomRoomRules.WellFormed(basic with{InfiniteLives=true,TimeLimitSeconds=0}),"무한 목숨은 제한 시간 5분 이상");
        Check(!CustomRoomRules.WellFormed(basic with{Ghosts=true,Infection=true})&&!CustomRoomRules.WellFormed(basic with{Ghosts=true,InfiniteLives=true})&&CustomRoomRules.WellFormed(basic with{Infection=true,InfiniteLives=true}),"유령은 감염·무한 목숨과 함께 불가, 감염+무한 목숨은 가능");
        Check(!CustomRoomRules.WellFormed(basic with{Infection=true,MannequinCount=9})&&CustomRoomRules.WellFormed(basic with{Infection=true,MannequinCount=8}),"감염은 마네킹 8개 이하");
        Check(new[]{10,19,121,42}.All(s=>!CustomRoomRules.WellFormed(basic with{HideRevealSeconds=s}))&&new[]{20,40,120}.All(s=>CustomRoomRules.WellFormed(basic with{HideRevealSeconds=s})),"숨기 노출 시간 20~120초, 5초 단위");
        Check(CustomRoomRules.WellFormed(basic with{AffixIds=["A01","A02","B09","B10","C01","C05"]})&&!CustomRoomRules.WellFormed(basic with{AffixIds=["A01","A02","B09","B10","C01","C05","C06"]})
            &&!CustomRoomRules.WellFormed(basic with{AffixIds=["B01","B02","B03","B04"]})&&!CustomRoomRules.WellFormed(basic with{AffixIds=["B09","B09"]})&&!CustomRoomRules.WellFormed(basic with{AffixIds=["Z99"]})&&!CustomRoomRules.WellFormed(basic with{AffixIds=null!}),"어픽스 6개 이하, 그룹별 3개 이하, 중복·미구현 불가");
        Check(!CustomRoomRules.WellFormed(new CustomRuleSet(false) with{ItemCards=true}),"사용자 지정을 끄면 놀이터 규칙도 모두 꺼짐");
        Check(new[]{1,2,16}.All(s=>!CustomRoomRules.WellFormed(basic with{GazeLimitSeconds=s}))&&CustomRoomRules.Valid(basic with{GazeLimitSeconds=8})&&!CustomRoomRules.WellFormed(basic with{Darkness=4})&&!CustomRoomRules.WellFormed(basic with{SoundOnlyMeters=3})&&!CustomRoomRules.WellFormed(basic with{EscapeMode=4}),"2단계 수치 범위(바라보기 3~15초, 어둠 0~3, 소리 4~15m, 탈출 방식 0~3)");
        Check(CustomRoomRules.Valid(basic with{Darkness=2})==CustomRoomRules.DarknessLive&&CustomRoomRules.Valid(basic with{SoundOnlyMeters=8})==CustomRoomRules.SoundOnlyLive&&CustomRoomRules.Valid(basic with{EscapeMode=2})==CustomRoomRules.EscapeModeLive&&CustomRoomRules.Valid(basic with{SpreadingFire=true})==CustomRoomRules.SpreadingFireLive,"2단계 미구현 규칙은 적용 거부");
        var gated=new (CustomRuleSet Rules,bool Live)[]{(basic with{AffixIds=["B09"]},CustomRoomRules.AffixesLive),(basic with{ItemCards=true},CustomRoomRules.ItemCardsLive),(basic with{InfiniteLives=true},CustomRoomRules.InfiniteLivesLive),
            (basic with{Infection=true},CustomRoomRules.InfectionLive),(basic with{Ghosts=true},CustomRoomRules.GhostsLive),(basic with{HideRevealSeconds=40},CustomRoomRules.HideRevealLive)};
        Check(gated.All(g=>CustomRoomRules.Valid(g.Rules)==g.Live),"게임 서버에 아직 없는 규칙은 적용 거부");
        try
        {
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);
            config["Bots:EnableDedicatedMatches"]="true";
            await Expect(anonymous.GetAsync("/api/rooms/current/custom-rules"),HttpStatusCode.Unauthorized,"미로그인 규칙 열람 차단");
            var room=(await Read(host.PostAsJsonAsync("/api/bot-matches",new{role="Survivor",difficulty=2}))).GetProperty("room");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId=room.GetProperty("roomId").GetString(),rules}),HttpStatusCode.Conflict,"봇전 규칙 변경 차단");
            await Read(host.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            room=(await Read(host.PostAsJsonAsync("/api/rooms/matchmaking/party",new{}))).GetProperty("room");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId=room.GetProperty("roomId").GetString(),rules}),HttpStatusCode.Conflict,"랜덤 매칭 규칙 변경 차단");
            await Read(host.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            room=(await Read(host.PostAsJsonAsync("/api/rooms",new{name="커스텀 규칙 검증",isPublic=false}))).GetProperty("room");
            string roomId=room.GetProperty("roomId").GetString()!,code=room.GetProperty("joinCode").GetString()!;
            foreach(var p in people.Skip(1))await Read(p.Client.PostAsJsonAsync($"/api/rooms/{code}/join",new{}));
            await Expect(people[1].Client.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules}),HttpStatusCode.Forbidden,"방장 외 규칙 변경 차단");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId=Guid.NewGuid().ToString(),rules}),HttpStatusCode.NotFound,"다른 방 ID로 규칙 적용 차단");
            foreach(var bad in new[]{rules with{DeathCount=0},rules with{DeathCount=21},rules with{MannequinCount=0},rules with{MannequinCount=11},rules with{TimeLimitSeconds=-1},rules with{TimeLimitSeconds=7201},rules with{TimeLimitSeconds=61}})
                await Expect(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=bad}),HttpStatusCode.BadRequest,"규칙 범위·분 단위 검증");
            foreach(var (bad,live) in gated)if(!live)
                await Expect(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=bad}),HttpStatusCode.BadRequest,"미구현 놀이터 규칙 적용 차단");
            await Expect(host.PostAsJsonAsync("/api/room-rule-presets",new{action="Save",name="잘못된 조합",rules=basic with{Infection=true,Ghosts=true}}),HttpStatusCode.BadRequest,"함께 켤 수 없는 규칙 조합의 프리셋 저장 차단");
            await Sql("UPDATE room_members SET is_ready=TRUE WHERE room_id=@r",("r",roomId));
            room=(await Read(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules}))).GetProperty("room");
            Check(room.GetProperty("members").EnumerateArray().All(m=>!m.GetProperty("ready").GetBoolean()),"규칙 적용은 전원 준비 해제");
            var peer=(await Read(people[1].Client.GetAsync("/api/rooms/current"))).GetProperty("room");
            Check(peer.GetProperty("customRules").GetProperty("deathCount").GetInt32()==6&&peer.GetProperty("customRules").GetProperty("mannequinCount").GetInt32()==4,"일반 참가자도 실제 규칙 확인");
            Check(peer.GetProperty("customRules").GetProperty("affixIds").EnumerateArray().Select(v=>v.GetString()).SequenceEqual(["B09","C05"])&&peer.GetProperty("customRules").GetProperty("itemCards").GetBoolean()
                &&peer.GetProperty("affixes").GetProperty("mode").GetString()=="Custom"&&peer.GetProperty("matchMode").GetString()=="ItemRoguelike","섞기 스위치가 방 어픽스·경기 모드에 함께 반영");
            await Read(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=new CustomRuleSet(false)}));
            peer=(await Read(people[1].Client.GetAsync("/api/rooms/current"))).GetProperty("room");
            Check(peer.GetProperty("customRules").GetProperty("affixIds").GetArrayLength()==0&&!peer.GetProperty("customRules").GetProperty("itemCards").GetBoolean()&&peer.GetProperty("affixes").GetProperty("mode").GetString()=="None"
                &&peer.GetProperty("matchMode").GetString()=="ItemRoguelike","사용자 지정을 끄면 어픽스는 지우고, 따로 고르는 경기 모드는 유지");
            await Read(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=rules with{ItemCards=false,AffixIds=[]}}));
            Check((await Read(people[1].Client.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("matchMode").GetString()=="Normal","사용자 지정이 켜져 있으면 아이템 카드 끔이 일반 모드로 반영");
            await Read(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=rules with{InfiniteLives=true}}));
            Check((await Read(people[1].Client.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("customRules").GetProperty("infiniteLives").GetBoolean(),"무한 목숨 적용과 저장");
            await Read(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=rules with{GazeLimitSeconds=8}}));
            Check((await Read(people[1].Client.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("customRules").GetProperty("gazeLimitSeconds").GetInt32()==8,"바라보기 시간 제한 적용과 저장");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=rules with{InfiniteLives=true,TimeLimitSeconds=0}}),HttpStatusCode.BadRequest,"무한 목숨은 무제한 시간과 함께 불가");
            await Read(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules}));
            for(int i=0;i<20;i++)await Read(host.PostAsJsonAsync("/api/room-rule-presets",new{action="Save",name="프리셋 "+i,rules}));
            await Expect(host.PostAsJsonAsync("/api/room-rule-presets",new{action="Save",name="초과",rules}),HttpStatusCode.Conflict,"계정당 프리셋 20개 상한");
            await Read(host.PostAsJsonAsync("/api/room-rule-presets",new{action="Save",name="프리셋 0",rules=rules with{DeathCount=20,MannequinCount=1,TimeLimitSeconds=0}}));
            var saved=await Read(host.GetAsync("/api/rooms/current/custom-rules"));
            Check(saved.GetProperty("presets").GetArrayLength()==20&&saved.GetProperty("presets").EnumerateArray().Single(p=>p.GetProperty("name").GetString()=="프리셋 0").GetProperty("rules").GetProperty("timeLimitSeconds").GetInt32()==0,"같은 이름 덮어쓰기·무제한 시간 저장");
            var mixed=saved.GetProperty("presets").EnumerateArray().Single(p=>p.GetProperty("name").GetString()=="프리셋 1").GetProperty("rules");
            Check(mixed.GetProperty("affixIds").EnumerateArray().Select(v=>v.GetString()).SequenceEqual(["B09","C05"])&&mixed.GetProperty("itemCards").GetBoolean(),"프리셋에 섞기 스위치 저장");
            await Read(people[1].Client.PostAsJsonAsync("/api/room-rule-presets",new{action="Delete",name="프리셋 0"}));
            Check((await Read(host.GetAsync("/api/rooms/current/custom-rules"))).GetProperty("presets").GetArrayLength()==20,"타인의 프리셋 삭제 불가");
            await Read(host.PostAsJsonAsync("/api/room-rule-presets",new{action="Delete",name="프리셋 0"}));
            Check((await Read(host.GetAsync("/api/rooms/current/custom-rules"))).GetProperty("presets").GetArrayLength()==19,"본인 프리셋 삭제");
            await Expect(host.PostAsJsonAsync("/api/room-rule-presets",new{action="Save",name=new string('가',33),rules}),HttpStatusCode.BadRequest,"프리셋 이름 길이 검증");
            string fixture=Path.Combine(output,"CustomRuleFixture",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(fixture);string executable=Path.Combine(fixture,"ProjectDFSDServer.exe");await File.WriteAllTextAsync(executable,"FakeFactory only");
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=true,executable,arguments="MultiplayTest -log",firstPort=25077,maximumConcurrent=1,projectRoot=fixture}));
            await Read(host.PostAsJsonAsync("/api/parties/action",new{action="Role",role="Survivor"}));
            foreach(var p in people.Skip(1))await Read(p.Client.PostAsJsonAsync("/api/rooms/current/ready",new{ready=true}));
            await Read(host.PostAsJsonAsync("/api/rooms/current/start",new{}));
            await Expect(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=rules with{DeathCount=7}}),HttpStatusCode.Conflict,"카운트다운 중 규칙 변경 차단");
            await Sql("UPDATE game_rooms SET started_at_utc=UTC_TIMESTAMP(6)-INTERVAL 6 SECOND WHERE id=@r",("r",roomId));
            await Read(host.PostAsJsonAsync("/api/rooms/current/confirm-start",new{}));await manager.TickAsync(default);var process=factory.Started.Last();
            server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Port",process.Port.ToString());
            await Read(server.PostAsJsonAsync("/api/server/heartbeat",new{port=process.Port,bootId="custom-"+process.Instance,managedId=process.Instance,ready=true}));await manager.TickAsync(default);
            room=(await Read(host.GetAsync("/api/rooms/current"))).GetProperty("room");match=room.GetProperty("matchId").GetString()!;
            var ticket=await Read(host.PostAsJsonAsync("/api/game-tickets",new{}));var claim=await Read(server.PostAsJsonAsync("/api/server/game-tickets/consume",new{ticket=ticket.GetProperty("ticket").GetString()}));
            Check(claim.GetProperty("customRules").GetProperty("enabled").GetBoolean()&&claim.GetProperty("customRules").GetProperty("deathCount").GetInt32()==6&&claim.GetProperty("customRules").GetProperty("timeLimitSeconds").GetInt32()==300,"일회용 검증 티켓에서 고정된 경기 규칙 전달");
            var claimRules=claim.GetProperty("customRules");
            Check(claimRules.GetProperty("affixIds").EnumerateArray().Select(v=>v.GetString()).SequenceEqual(["B09","C05"])&&claimRules.GetProperty("itemCards").GetBoolean()&&!claimRules.GetProperty("infiniteLives").GetBoolean()
                &&!claimRules.GetProperty("infection").GetBoolean()&&!claimRules.GetProperty("ghosts").GetBoolean()&&claimRules.GetProperty("hideRevealSeconds").GetInt32()==0,"티켓 규칙에 게임 서버가 요구하는 놀이터 필드가 모두 있음");
            Check(claim.GetProperty("affixes").GetProperty("mode").GetString()=="Custom"&&claim.GetProperty("affixes").GetProperty("ids").EnumerateArray().Select(v=>v.GetString()).SequenceEqual(["B09","C05"])
                &&claim.GetProperty("matchMode").GetString()=="ItemRoguelike","티켓의 어픽스·경기 모드가 규칙의 섞기 스위치와 일치");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules}),HttpStatusCode.Conflict,"시작된 경기 규칙 변경 차단");
            await Read(server.PostAsJsonAsync("/api/server/matches/results",new{matchId=match,userId=people[0].Id.ToString(),role="Survivor",success=true,rescueCount=1,escapeSeconds=45}));
            var account=await Read(host.GetAsync("/api/case-records"));
            Check(account.GetProperty("xp").GetInt64()==0&&account.GetProperty("credits").GetInt64()==0,"커스텀 성공 경기 경험치·포인트 제외");
            Check(account.GetProperty("missions").EnumerateArray().All(m=>m.GetProperty("progress").GetInt32()==0)&&account.GetProperty("achievements").GetArrayLength()==0,"커스텀 경기 미션·업적 진행 제외");
            await Expect(host.PostAsJsonAsync("/api/leaderboards/records",new{matchId=match,role="Survivor",success=true,rescueCount=1,escapeSeconds=45}),HttpStatusCode.Conflict,"커스텀 성공 결과 공식 리더보드 등록 차단");
            // Rule codes (Docs/CustomGamePlayground.md 6): same rules, same code; the summary counts this finished match.
            var made=await Read(host.PostAsJsonAsync("/api/rule-codes",new{rules}));string shown=made.GetProperty("code").GetString()!;
            var reordered=await Read(people[1].Client.PostAsJsonAsync("/api/rule-codes",new{rules=rules with{AffixIds=[.. rules.AffixIds.Reverse()]}}));
            Check(shown.Length==9&&shown[4]=='-'&&reordered.GetProperty("code").GetString()==shown,"같은 규칙(어픽스 순서 무관)은 같은 코드");
            var other=await Read(host.PostAsJsonAsync("/api/rule-codes",new{rules=rules with{DeathCount=5}}));Check(other.GetProperty("code").GetString()!=shown,"규칙이 다르면 다른 코드");
            await Expect(host.PostAsJsonAsync("/api/rule-codes",new{rules=new CustomRuleSet(false)}),HttpStatusCode.BadRequest,"사용자 지정이 꺼진 규칙은 코드 없음");
            var looked=await Read(people[2].Client.GetAsync("/api/rule-codes/"+shown.ToLowerInvariant().Replace("-","")));
            Check(looked.GetProperty("rules").GetProperty("deathCount").GetInt32()==6&&looked.GetProperty("rules").GetProperty("itemCards").GetBoolean(),"소문자·하이픈 없이도 같은 코드로 규칙 불러오기");
            Check(looked.GetProperty("summary").GetProperty("games").GetInt32()==1&&looked.GetProperty("summary").GetProperty("survivorWins").GetInt32()==1&&looked.GetProperty("summary").GetProperty("myGames").GetInt32()==0,"코드별 기록: 판 수·진영 승리, 내 결과는 본인 것만");
            var mineView=await Read(host.GetAsync("/api/rule-codes/"+shown));
            Check(mineView.GetProperty("summary").GetProperty("myGames").GetInt32()==1&&mineView.GetProperty("summary").GetProperty("myWins").GetInt32()==1,"코드별 기록: 내 결과");
            Check(!looked.GetRawText().Contains(roomId)&&!looked.GetRawText().Contains("hostUser"),"코드 응답에 방·방장 정보 없음");
            await Expect(host.GetAsync("/api/rule-codes/AB-CD"),HttpStatusCode.BadRequest,"짧은 코드 거부");
            await Expect(host.GetAsync("/api/rule-codes/ILOU0000"),HttpStatusCode.BadRequest,"코드에 쓰지 않는 글자 거부");
            await Expect(host.GetAsync("/api/rule-codes/00000000"),HttpStatusCode.NotFound,"없는 코드 404");
            Check(RuleCodes.Normalize("zb7k-2qxm")=="ZB7K2QXM"&&RuleCodes.Encode(RuleCodes.Hash(rules),8)==RuleCodes.Encode(RuleCodes.Hash(rules with{}),8),"코드 정규화·결정성");
            var profile=await Read(host.GetAsync("/api/social/room-profiles/"+people[0].Id));Check(profile.GetProperty("statistics").GetArrayLength()==0,"개인 공식 전적 집계에서도 커스텀 경기 제외");
            await using(var c=await db.OpenConnectionAsync(default))
            {
                var receipts=await CommunityEndpoints.Rows(c,null,"SELECT match_id FROM bot_rating_receipts WHERE match_id=@m",default,("m",match));Check(receipts.Count==0,"커스텀 경기 레이팅 스냅샷 미생성");
                await using var tx=await c.BeginTransactionAsync();await CommunityEndpoints.Exec(c,tx,"UPDATE room_custom_rules SET enabled=FALSE,death_count=3 WHERE room_id=@r",default,("r",roomId));await CustomRoomRules.CaptureAsync(c,tx,match,default);
                Check((await CustomRoomRules.MatchAsync(c,tx,match,default))==rules,"나중에 방 설정이 달라도 기존 경기 스냅샷 불변");await tx.CommitAsync();
            }
            await Sql("DELETE FROM game_rooms WHERE id=@r",("r",roomId));
            await using(var c=await db.OpenConnectionAsync(default)){Check(await CustomRoomRules.IsCustomAsync(c,null,match,default),"방 삭제 이후에도 커스텀 경기 구분 유지");Check(!(await CustomRoomRules.IsCustomAsync(c,null,Guid.NewGuid().ToString(),default)),"기존 일반 경기 기본 분류 유지");}
            Console.WriteLine($"PASS: {count} custom room rules HTTP/DB checks");
        }
        finally
        {
            config["Bots:EnableDedicatedMatches"]=oldBots;
            await Read(admin.PutAsJsonAsync("/api/admin/game-server-demand",new{enabled=false}));await manager.TickAsync(default);
            foreach(var p in people){await Sql("DELETE FROM game_rooms WHERE host_user_id=@u;DELETE FROM users WHERE id=@u",("u",p.Id));p.Client.Dispose();}
            if(match!=null)await Sql("DELETE FROM match_custom_rules WHERE match_id=@m",("m",match));
        }
    }
}

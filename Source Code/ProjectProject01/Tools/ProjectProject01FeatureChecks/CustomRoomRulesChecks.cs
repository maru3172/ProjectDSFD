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
        // 4단계: 둘이 봐야 멈추는 몸체·손전등 시선 (Docs/CustomGamePlayground.md 15).
        Check(CustomRoomRules.Valid(basic with{PairGazeBodies=1})&&CustomRoomRules.Valid(basic with{PairGazeBodies=3,MannequinCount=3})&&!CustomRoomRules.WellFormed(basic with{PairGazeBodies=4})
            &&!CustomRoomRules.WellFormed(basic with{PairGazeBodies=2,MannequinCount=1})&&!CustomRoomRules.WellFormed(basic with{PairGazeBodies=-1}),"둘이 봐야 멈추는 몸체: 0~3개, 마네킹 수 이하");
        Check(CustomRoomRules.Valid(basic with{Flashlight=true})&&!CustomRoomRules.WellFormed(new CustomRuleSet(false) with{Flashlight=true}),"손전등 시선: 사용자 지정이 켜진 방에서만");
        Check(!RuleCodes.Canonical(basic).Contains("v4")&&RuleCodes.Canonical(basic with{Flashlight=true}).Contains("|v4|0|1")&&RuleCodes.Hash(basic)!=RuleCodes.Hash(basic with{PairGazeBodies=1}),
            "규칙 코드: 4단계 규칙이 꺼져 있으면 예전 코드 그대로, 켜면 다른 코드");
        // 3단계: 규칙 카드 투표·역할 바꾸기 (Docs/CustomGamePlayground.md 12.2).
        Check(CustomRoomRules.Valid(basic with{RuleVote=true})==CustomRoomRules.RuleVoteLive&&CustomRoomRules.Valid(basic with{RoleSwap=true})==CustomRoomRules.RoleSwapLive,"규칙 카드 투표·역할 바꾸기 적용 가능");
        Check(RoleSwap.NextMannequin([1,2,3],new HashSet<ulong>())==1&&RoleSwap.NextMannequin([1,2,3],new HashSet<ulong>{1})==2&&RoleSwap.NextMannequin([1,2,3],new HashSet<ulong>{1,2})==3,"역할 바꾸기: 아직 마네킹을 안 한 사람이 방 순서대로 다음 마네킹");
        Check(RoleSwap.MembersKey([3,1,2])==RoleSwap.MembersKey([2,3,1]),"역할 바꾸기: 같은 세 사람이면 같은 시리즈");
        Check(RoleSwap.BestLine([new("A",true,true,250,2),new("B",true,true,190,2),new("C",true,false,null,1)]).Contains("B (3분 10초)"),"역할 바꾸기: 가장 빨리 전원을 잡은 마네킹 표시");
        Check(RoleSwap.BestLine([new("A",true,false,null,1),new("B",true,false,null,0)]).Contains("A (1번)")&&RoleSwap.BestLine([new("A",false,false,null,0)])=="","역할 바꾸기: 아무도 전원을 못 잡으면 가장 많이 잡은 마네킹, 진행 중이면 빈칸");
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
            // Personal handicaps (12.1): only room participants, kept with the room, never part of a rule code.
            var handicapped=rules with{Handicaps=[new HandicapEntry(people[1].Id.ToString(),DeathCount:1,SpeedPct:-10)]};
            await Read(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=handicapped}));
            var stored=(await Read(people[2].Client.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("customRules").GetProperty("handicaps");
            Check(stored.GetArrayLength()==1&&stored[0].GetProperty("userId").GetString()==people[1].Id.ToString()&&stored[0].GetProperty("speedPct").GetInt32()==-10,"핸디캡 적용과 저장(모두에게 보임)");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=rules with{Handicaps=[new HandicapEntry("999999999")]}}),HttpStatusCode.BadRequest,"방 밖의 사람에게 핸디캡 거부");
            await Expect(host.PutAsJsonAsync("/api/rooms/current/custom-rules",new{roomId,rules=rules with{Handicaps=[new HandicapEntry(people[1].Id.ToString(),DeathCount:3)]}}),HttpStatusCode.BadRequest,"핸디캡 범위 검증");
            var codeWith=(await Read(host.PostAsJsonAsync("/api/rule-codes",new{rules=handicapped}))).GetProperty("code").GetString();
            var codeWithout=(await Read(host.PostAsJsonAsync("/api/rule-codes",new{rules}))).GetProperty("code").GetString();
            Check(codeWith==codeWithout,"규칙 코드에는 핸디캡이 들어가지 않음");
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
            Check(!looked.TryGetProperty("rules",out _),"규칙 코드는 기록 확인용: 소문자·하이픈 없이도 같은 코드지만 규칙은 내주지 않음");
            Check(looked.GetProperty("summary").GetProperty("games").GetInt32()==1&&looked.GetProperty("summary").GetProperty("survivorWins").GetInt32()==1&&looked.GetProperty("summary").GetProperty("myGames").GetInt32()==0,"코드별 기록: 판 수·진영 승리, 내 결과는 본인 것만");
            var mineView=await Read(host.GetAsync("/api/rule-codes/"+shown));
            Check(mineView.GetProperty("summary").GetProperty("myGames").GetInt32()==1&&mineView.GetProperty("summary").GetProperty("myWins").GetInt32()==1,"코드별 기록: 내 결과");
            Check(!looked.GetRawText().Contains(roomId)&&!looked.GetRawText().Contains("hostUser"),"코드 응답에 방·방장 정보 없음");
            await Expect(host.GetAsync("/api/rule-codes/AB-CD"),HttpStatusCode.BadRequest,"짧은 코드 거부");
            await Expect(host.GetAsync("/api/rule-codes/ILOU0000"),HttpStatusCode.BadRequest,"코드에 쓰지 않는 글자 거부");
            await Expect(host.GetAsync("/api/rule-codes/00000000"),HttpStatusCode.NotFound,"없는 코드 404");
            // Share codes (Docs/CustomGamePlayground.md 14): a fresh code per share, expiry, revoke.
            await Expect(host.PostAsJsonAsync("/api/share-codes",new{name="친구용",rules,hours=5}),HttpStatusCode.BadRequest,"공유 코드: 기한은 24시간·7일·무기한만");
            var share=await Read(host.PostAsJsonAsync("/api/share-codes",new{name="친구용",rules,hours=24}));string shareCode=share.GetProperty("code").GetString()!;
            Check(shareCode.Length==11&&shareCode[5]=='-'&&shareCode.Replace("-","")!=shown.Replace("-",""),"공유 코드: 규칙 코드와 다른 10글자 무작위 코드");
            var got=await Read(people[2].Client.GetAsync("/api/share-codes/"+shareCode.ToLowerInvariant().Replace("-","")));
            Check(got.GetProperty("name").GetString()=="친구용"&&got.GetProperty("rules").GetProperty("deathCount").GetInt32()==6&&got.GetProperty("ruleCode").GetString()==shown
                &&got.GetProperty("expiresAtUtc").ValueKind==JsonValueKind.String&&!got.GetProperty("board").GetBoolean(),"공유 코드: 받는 사람이 이름·규칙·기한을 먼저 확인");
            var again=await Read(host.PostAsJsonAsync("/api/share-codes",new{name="친구용",rules,hours=0}));string againCode=again.GetProperty("code").GetString()!;
            Check(againCode!=shareCode,"공유 코드: 같은 규칙도 공유할 때마다 새 코드");
            Check((await Read(host.GetAsync("/api/share-codes"))).GetProperty("items").GetArrayLength()==2,"공유 코드: 내 공유 코드 목록");
            await Expect(people[2].Client.DeleteAsync("/api/share-codes/"+shareCode),HttpStatusCode.Forbidden,"공유 코드: 남의 코드는 취소 불가");
            await Read(host.DeleteAsync("/api/share-codes/"+shareCode));
            await Expect(people[2].Client.GetAsync("/api/share-codes/"+shareCode),HttpStatusCode.NotFound,"공유 코드: 취소하면 더는 못 씀");
            await Sql("UPDATE rule_share_codes SET expires_at_utc=UTC_TIMESTAMP(6)-INTERVAL 1 SECOND WHERE code=@c",("c",againCode.Replace("-","")));
            await Expect(people[2].Client.GetAsync("/api/share-codes/"+againCode),HttpStatusCode.NotFound,"공유 코드: 기한이 지나면 못 씀");
            Check(RuleCodes.Normalize("zb7k-2qxm")=="ZB7K2QXM"&&RuleCodes.Encode(RuleCodes.Hash(rules),8)==RuleCodes.Encode(RuleCodes.Hash(rules with{}),8),"코드 정규화·결정성");
            var profile=await Read(host.GetAsync("/api/social/room-profiles/"+people[0].Id));Check(profile.GetProperty("statistics").GetArrayLength()==0,"개인 공식 전적 집계에서도 커스텀 경기 제외");
            await using(var c=await db.OpenConnectionAsync(default))
            {
                var receipts=await CommunityEndpoints.Rows(c,null,"SELECT match_id FROM bot_rating_receipts WHERE match_id=@m",default,("m",match));Check(receipts.Count==0,"커스텀 경기 레이팅 스냅샷 미생성");
                await using var tx=await c.BeginTransactionAsync();await CommunityEndpoints.Exec(c,tx,"UPDATE room_custom_rules SET enabled=FALSE,death_count=3 WHERE room_id=@r",default,("r",roomId));await CustomRoomRules.CaptureAsync(c,tx,match,default);
                Check((await CustomRoomRules.MatchAsync(c,tx,match,default))==rules,"나중에 방 설정이 달라도 기존 경기 스냅샷 불변");await tx.CommitAsync();
            }
            {
                // Shared preset board (Docs/CustomGamePlayground.md 13): post after a clear, clear rate, recommend, take down.
                await Expect(people[1].Client.PostAsJsonAsync("/api/shared-presets",new{name="안 깬 규칙",summary="",rules}),HttpStatusCode.Conflict,"공유 게시판: 한 판을 끝까지 마치지 않으면 올릴 수 없음");
                await Expect(host.PostAsJsonAsync("/api/shared-presets",new{name="",summary="",rules}),HttpStatusCode.BadRequest,"공유 게시판: 이름 필수");
                var posted=await Read(host.PostAsJsonAsync("/api/shared-presets",new{name="첫 공유",summary="끝까지 해 본 규칙",rules}));
                Check(posted.GetProperty("code").GetString()==shown,"공유 게시판: 끝까지 마친 규칙은 올라가고 같은 규칙 코드");
                string boardCode=posted.GetProperty("shareCode").GetString()!;
                Check((await Read(people[2].Client.GetAsync("/api/share-codes/"+boardCode))).GetProperty("board").GetBoolean(),"공유 게시판: 글마다 고정 공유 코드");
                await Expect(host.DeleteAsync("/api/share-codes/"+boardCode),HttpStatusCode.Conflict,"공유 게시판: 글의 공유 코드는 따로 취소 불가");
                var mine=await Read(host.GetAsync("/api/shared-presets/mine"));long postId=mine.GetProperty("items")[0].GetProperty("id").GetInt64();
                await Read(host.PutAsJsonAsync($"/api/shared-presets/{postId}",new{published=false}));
                Check(!(await Read(people[1].Client.GetAsync("/api/shared-presets"))).GetProperty("items").EnumerateArray().Any(i=>i.GetProperty("code").GetString()==shown)
                    &&!(await Read(host.GetAsync("/api/shared-presets/mine"))).GetProperty("items")[0].GetProperty("published").GetBoolean()
                    &&(await Read(host.GetAsync("/api/shared-presets?sort=mine"))).GetProperty("items").GetArrayLength()==1,"공유 게시판: 내리면 게시판에서 빠지고 내 글에는 남음");
                await Expect(people[2].Client.GetAsync("/api/share-codes/"+boardCode),HttpStatusCode.NotFound,"공유 게시판: 내린 글의 코드는 멈춤");
                await Expect(people[1].Client.PutAsJsonAsync($"/api/shared-presets/{postId}",new{published=true}),HttpStatusCode.Forbidden,"공유 게시판: 남의 글은 게시 불가");
                await Read(host.PutAsJsonAsync($"/api/shared-presets/{postId}",new{published=true}));
                Check((await Read(people[2].Client.GetAsync("/api/share-codes/"+boardCode))).GetProperty("code").GetString()==boardCode,"공유 게시판: 다시 게시하면 같은 코드가 다시 살아남");
                // My presets overview: several at once, one result per preset.
                var other5=rules with{DeathCount=5};
                var down=await Read(host.PostAsJsonAsync("/api/shared-presets/bulk",new{action="Unpublish",presets=new object[]{new{name="첫 공유",rules},new{name="안 올린 것",rules=other5}}}));
                Check(down.GetProperty("results")[0].GetProperty("ok").GetBoolean()&&!down.GetProperty("results")[1].GetProperty("ok").GetBoolean()
                    &&!(await Read(host.GetAsync("/api/shared-presets/mine"))).GetProperty("items")[0].GetProperty("published").GetBoolean(),"여러 개 내리기: 게시 중인 것만 내려가고 결과를 하나씩 알려 줌");
                var up=await Read(host.PostAsJsonAsync("/api/shared-presets/bulk",new{action="Publish",presets=new object[]{new{name="첫 공유 (수정)",rules},new{name="안 깬 것",rules=other5}}}));
                Check(up.GetProperty("results")[0].GetProperty("ok").GetBoolean()&&!up.GetProperty("results")[1].GetProperty("ok").GetBoolean()&&up.GetProperty("message").GetString()!.Contains("1개 게시")
                    &&(await Post(host)).GetProperty("summary").GetString()=="끝까지 해 본 규칙","여러 개 게시: 클리어한 것만 올라가고, 다시 게시하면 설명은 그대로");
                await Expect(host.PostAsJsonAsync("/api/shared-presets/bulk",new{action="Hide",presets=new object[]{new{name="x",rules}}}),HttpStatusCode.BadRequest,"여러 개 작업 종류 검증");
                var before=(await Read(host.GetAsync("/api/rooms/current/custom-rules"))).GetProperty("presets").GetArrayLength();
                var gone=await Read(host.PostAsJsonAsync("/api/room-rule-presets/delete",new{names=new[]{"프리셋 1","프리셋 2","없는 이름"}}));
                Check(gone.GetProperty("presets").GetArrayLength()==before-2&&(await Post(host)).GetProperty("published").GetBoolean(),"여러 개 삭제: 고른 내 프리셋만 지우고 게시판 글은 그대로");
                await Expect(people[1].Client.PostAsJsonAsync("/api/shared-presets",new{name="가로채기",summary="",rules}),HttpStatusCode.Conflict,"공유 게시판: 남이 올린 규칙은 다시 못 올림");
                await Read(host.PostAsJsonAsync("/api/shared-presets",new{name="첫 공유 (수정)",summary="끝까지 해 본 규칙",rules}));
                var board=await Read(people[1].Client.GetAsync("/api/shared-presets?sort=played"));
                var item=board.GetProperty("items").EnumerateArray().Single(i=>i.GetProperty("code").GetString()==shown);
                Check(item.GetProperty("name").GetString()=="첫 공유 (수정)"&&item.GetProperty("plays").GetInt32()==1&&item.GetProperty("clearRate").GetInt32()==100&&!item.GetProperty("mine").GetBoolean()
                    &&item.GetProperty("rules").GetProperty("deathCount").GetInt32()==rules.DeathCount,"공유 게시판: 판 수·클리어률·규칙을 함께 보여 줌, 같은 사람이 다시 올리면 이름만 바뀜");
                long id=item.GetProperty("id").GetInt64();
                await Expect(people[1].Client.PostAsJsonAsync($"/api/shared-presets/{id}/vote",new{vote=1}),HttpStatusCode.Conflict,"공유 게시판: 끝까지 해 보지 않으면 추천 불가");
                await Expect(host.PostAsJsonAsync($"/api/shared-presets/{id}/vote",new{vote=-1}),HttpStatusCode.Conflict,"공유 게시판: 내 글은 추천·비추천 불가");
                await Sql("INSERT INTO match_desertions(match_id,user_id,role,deserted_at_utc) VALUES(@m,@u,'Survivor',UTC_TIMESTAMP(6))",("m",match),("u",people[1].Id));
                var rate=(await Read(host.GetAsync("/api/shared-presets?sort=liked"))).GetProperty("items").EnumerateArray().Single(i=>i.GetProperty("code").GetString()==shown);
                Check(rate.GetProperty("clearRate").GetInt32()==50&&rate.GetProperty("mine").GetBoolean(),"공유 게시판: 기권한 판은 클리어률을 낮춤");
                await Sql("DELETE FROM match_desertions WHERE match_id=@m AND user_id=@u",("m",match),("u",people[1].Id));
                // 추천·비추천: people[1] finishes a match with the code (a verified result, no forfeit), then votes.
                await Sql("INSERT INTO verified_match_results(match_id,room_id,user_id,role,success) VALUES(@m,@r,@u,'Survivor',FALSE)",("m",match),("r",roomId),("u",people[1].Id));
                async Task<JsonElement> Post(HttpClient client)=>(await Read(client.GetAsync("/api/shared-presets?sort=liked"))).GetProperty("items").EnumerateArray().Single(i=>i.GetProperty("code").GetString()==shown);
                await Read(people[1].Client.PostAsJsonAsync($"/api/shared-presets/{id}/vote",new{vote=1}));
                var voted=await Post(people[1].Client);
                Check(voted.GetProperty("likes").GetInt32()==1&&voted.GetProperty("dislikes").GetInt32()==0&&voted.GetProperty("myVote").GetInt32()==1,"공유 게시판: 끝까지 해 본 사람의 추천");
                await Read(people[1].Client.PostAsJsonAsync($"/api/shared-presets/{id}/vote",new{vote=-1}));voted=await Post(people[1].Client);
                Check(voted.GetProperty("likes").GetInt32()==0&&voted.GetProperty("dislikes").GetInt32()==1&&voted.GetProperty("myVote").GetInt32()==-1,"공유 게시판: 추천을 비추천으로 바꾸면 한 표만 남음");
                await Read(people[1].Client.PostAsJsonAsync($"/api/shared-presets/{id}/vote",new{vote=0}));voted=await Post(people[1].Client);
                Check(voted.GetProperty("likes").GetInt32()==0&&voted.GetProperty("dislikes").GetInt32()==0&&voted.GetProperty("myVote").GetInt32()==0,"공유 게시판: 비추천 취소");
                await Expect(people[1].Client.PostAsJsonAsync($"/api/shared-presets/{id}/vote",new{vote=2}),HttpStatusCode.BadRequest,"공유 게시판: 추천 값 검증");
                // 신고: goes to moderators only; the post stays until a moderator acts.
                await Expect(people[1].Client.PostAsJsonAsync($"/api/shared-presets/{id}/report",new{reason="Bad"}),HttpStatusCode.BadRequest,"공유 게시판: 신고 사유 검증");
                await Expect(host.PostAsJsonAsync($"/api/shared-presets/{id}/report",new{reason="Name"}),HttpStatusCode.Conflict,"공유 게시판: 내 글은 신고 불가");
                Check((await Read(people[1].Client.PostAsJsonAsync($"/api/shared-presets/{id}/report",new{reason="Name"}))).GetProperty("message").GetString()!.Contains("접수")
                    &&(await Read(people[1].Client.PostAsJsonAsync($"/api/shared-presets/{id}/report",new{reason="Summary"}))).GetProperty("message").GetString()!.Contains("이미"),"공유 게시판: 신고는 한 사람 한 번");
                Check((await Post(people[1].Client)).GetProperty("reported").GetBoolean()&&!(await Post(host)).GetProperty("hidden").GetBoolean(),"공유 게시판: 신고만으로 숨기지 않음");
                var queue=await Read(admin.GetAsync("/api/admin/community/shared-presets"));
                Check(queue.GetProperty("posts").EnumerateArray().Any(p=>p.GetProperty("id").GetString()==id.ToString()&&p.GetProperty("reports").GetInt32()==1),"공유 게시판: 운영자 신고 목록");
                await Expect(people[1].Client.GetAsync("/api/admin/community/shared-presets"),HttpStatusCode.Unauthorized,"공유 게시판: 운영자 키 없이는 신고 목록 불가");
                await Read(admin.PutAsJsonAsync($"/api/admin/community/shared-presets/{id}",new{action="Hide"}));
                Check(!(await Read(people[1].Client.GetAsync("/api/shared-presets"))).GetProperty("items").EnumerateArray().Any(i=>i.GetProperty("code").GetString()==shown)
                    &&(await Post(host)).GetProperty("hidden").GetBoolean(),"공유 게시판: 운영자가 숨기면 올린 사람에게만 '숨겨짐'으로 보임");
                await Expect(people[1].Client.PostAsJsonAsync($"/api/shared-presets/{id}/vote",new{vote=1}),HttpStatusCode.NotFound,"공유 게시판: 숨긴 글은 추천 불가");
                await Expect(host.PostAsJsonAsync("/api/shared-presets",new{name="숨김 우회",summary="",rules}),HttpStatusCode.Conflict,"공유 게시판: 숨긴 글은 이름만 바꿔 되살릴 수 없음");
                await Read(admin.PutAsJsonAsync($"/api/admin/community/shared-presets/{id}",new{action="Restore"}));
                Check(!(await Post(people[1].Client)).GetProperty("reported").GetBoolean(),"공유 게시판: 운영자가 되살리면 신고도 정리");
                await Expect(admin.PutAsJsonAsync($"/api/admin/community/shared-presets/{id}",new{action="Ban"}),HttpStatusCode.BadRequest,"공유 게시판: 운영자 처리 종류 검증");
                await Sql("DELETE FROM verified_match_results WHERE match_id=@m AND user_id=@u",("m",match),("u",people[1].Id));
                await Expect(people[1].Client.DeleteAsync($"/api/shared-presets/{id}"),HttpStatusCode.Forbidden,"공유 게시판: 남의 글은 못 내림");
                await Read(host.DeleteAsync($"/api/shared-presets/{id}"));
                Check(!(await Read(host.GetAsync("/api/shared-presets"))).GetProperty("items").EnumerateArray().Any(i=>i.GetProperty("code").GetString()==shown),"공유 게시판: 올린 사람이 내리면 사라짐");
            }
            await using(var c=await db.OpenConnectionAsync(default))
            {
                // Role swap on the same three players (rolled back): room order picks the mannequin, a started match becomes round 1.
                await using var tx=await c.BeginTransactionAsync();
                await CommunityEndpoints.Exec(c,tx,"UPDATE room_custom_rules SET enabled=TRUE,role_swap=TRUE WHERE room_id=@r;UPDATE game_rooms SET status='Started',match_id=@m WHERE id=@r",default,("r",roomId),("m",match));
                var humans=people.Select(p=>p.Id).ToList();
                Check(await RoleSwap.AssignIfNeededAsync(c,tx,roomId,humans,0,default),"역할 바꾸기: 세 사람 방에서 역할 배정");
                async Task<ulong> Mannequin()=>Convert.ToUInt64((await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM room_members WHERE room_id=@r AND assigned_role='Mannequin'",default,("r",roomId))).Single()["user_id"]);
                Check(await Mannequin()==people[0].Id,"역할 바꾸기: 생존자 선호와 상관없이 방 순서 첫 사람이 1판 마네킹");
                await RoleSwap.CaptureAsync(c,tx,match,default);await RoleSwap.CaptureAsync(c,tx,match,default);
                Check((await CommunityEndpoints.Rows(c,tx,"SELECT round_no FROM room_role_swap_rounds WHERE room_id=@r",default,("r",roomId))).Count==1,"역할 바꾸기: 시작한 경기가 한 번만 1판으로 기록");
                Check(!await RoleSwap.AssignIfNeededAsync(c,tx,roomId,humans,1,default),"역할 바꾸기: 봇이 있는 방에는 적용 안 함");
                Check(await RoleSwap.AssignIfNeededAsync(c,tx,roomId,humans,0,default)&&await Mannequin()==people[1].Id,"역할 바꾸기: 2판은 아직 안 한 다음 사람");
                var progress=await RoleSwap.ProgressAsync(c,tx,roomId,default);
                Check(progress is {Played:1,Total:3}&&progress.Rounds.Count==1&&progress.NextMannequin!="","역할 바꾸기: 방 화면 진행 1/3과 다음 마네킹");
                await tx.RollbackAsync();
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

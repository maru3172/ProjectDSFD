using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class CaseDrawChecks
{
    internal static void Rules(Action<bool,string> check)
    {
        check(!CaseDraw.Draft.Published&&CaseDraw.Draft.Prizes.All(p=>p.Weight==0),"미정 확률의 기본 뽑기는 비공개");
        check(CaseDraw.Validate(CaseDraw.Draft,out _),"5등급 초안 저장 가능");
        check(!CaseDraw.Validate(CaseDraw.Draft with {Published=true},out _),"0 가중치로 공개 차단");
        var pool=CaseDraw.Draft with {Published=true,Prizes=CaseDraw.Draft.Prizes.Select(p=>p with {Weight=1}).ToArray()};
        check(CaseDraw.Validate(pool,out _),"5등급 유효 구성 공개 가능");
        var owned=new HashSet<string>{"banner_afterhours","banner_embers","frame_archive"};var eligible=CaseDraw.Eligible(pool,owned);
        check(eligible.All(p=>p.ItemId is null||p.Grade>=3||!owned.Contains(p.ItemId))&&eligible.Any(p=>p.ItemId=="frame_archive"),"획득한 상위 2등급 제외, 하위 꾸미기는 중복 가능");
        check(CaseDraw.Pick(pool.Prizes,true,_=>0).Grade==1,"100회 보장은 최고 등급만 추첨");
        check(CaseDraw.Pick(pool.Prizes,false,total=>total-1).Id=="points","누적 가중치 마지막 경계 정확");
        check(!CaseDraw.Validate(pool with {Prizes=pool.Prizes.Select(p=>p.Grade==1?p with {ItemId=null,Credits=5}:p).ToArray()},out _),"상위 2등급은 일회성 꾸미기만 가능");
        check(CaseDraw.TicketCost==500&&CaseDraw.DuplicateCredits==5&&CaseDraw.PityLimit==100,"교환가·중복 환급·천장 규칙");
        check(CaseDraw.AdvancePity(7,5)==8&&CaseDraw.AdvancePity(99,4)==100&&CaseDraw.AdvancePity(53,1)==0,"상위 보상 소진과 무관하게 스택 증가, 1등급에서만 초기화");
        check(CaseDraw.SoftPityLimit==50&&CaseDraw.GuaranteeGrade(pool.Prizes,49,_=>0)==1&&CaseDraw.GuaranteeGrade(pool.Prizes,49,_=>1)==2,"50회 반천장 등급 추첨은 1·2등급 각 50%");
        check(CaseDraw.GuaranteeGrade(pool.Prizes,48,_=>0)==0&&CaseDraw.GuaranteeGrade(pool.Prizes,50,_=>0)==0&&CaseDraw.GuaranteeGrade(pool.Prizes,99,_=>1)==1,"반천장은 사이클당 한 번, 100회는 1등급 확정");
        check(CaseDraw.GuaranteeGrade(pool.Prizes.Where(p=>p.Grade!=2).ToArray(),49,_=>1)==1&&CaseDraw.GuaranteeGrade(eligible.Where(p=>p.Grade>=3).ToArray(),49,_=>0)==0,"상위 상품 소진 시 중복 없이 남은 등급으로 보장");
        check(CaseDraw.Draft.Prizes.Length==29&&CaseCollection.Items.Select(p=>p.Id).Distinct().Count()==CaseCollection.Items.Length,"확장 상품 29종과 수집 ID 중복 검사");
        var oldDraft=CaseDraw.Draft with{Prizes=CaseDraw.Draft.Prizes.Where(p=>new[]{"afterhours","embers","archive","receipt","points"}.Contains(p.Id)).ToArray()};
        check(CaseDraw.ExpandLegacyDraft(oldDraft).Prizes.Length==29&&CaseDraw.ExpandLegacyDraft(oldDraft with{Published=true}).Prizes.Length==5,"미설정 옛 초안만 29종으로 확장, 공개 시즌 구성 보존");
        var context=new Microsoft.AspNetCore.Http.DefaultHttpContext();context.Connection.RemoteIpAddress=IPAddress.Loopback;context.Request.Host=new("127.0.0.1:5080");CheckLoopback();
        void CheckLoopback(){check(CaseDebugCurrency.DirectLoopback(context.Request),"로컬 직접 접속만 디버그 대상으로 인정");context.Request.Headers["X-Forwarded-For"]="192.168.1.2";check(!CaseDebugCurrency.DirectLoopback(context.Request),"프록시 경유 디버그 포인트 요청 차단");context.Request.Headers.Clear();context.Connection.RemoteIpAddress=IPAddress.Parse("192.168.1.2");check(!CaseDebugCurrency.DirectLoopback(context.Request),"외부 PC의 에디터 헤더 위조만으로 지급 불가");}
    }
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int checks=0;void Check(bool ok,string s){if(!ok)throw new Exception(s);checks++;Console.WriteLine("PASS "+s);}Rules(Check);
        async Task<JsonElement> Read(Task<HttpResponseMessage> task){using var response=await task;string text=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception($"{response.StatusCode}: {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> task,HttpStatusCode code,string s){using var response=await task;Check(response.StatusCode==code,$"{s} (expected {code}, received {response.StatusCode})");}
        async Task Sql(string sql,params(string,object?)[] args){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,args);}
        var player=await OperationsIntegration.Player(db);using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);using var anon=new HttpClient{BaseAddress=admin.BaseAddress};
        var previous=await Read(admin.GetAsync("/api/admin/case-records/draw"));
        var other=await OperationsIntegration.Player(db);
        try
        {
            Check(previous.GetProperty("defaultPrizes").GetArrayLength()==29,"서버 운영 조회에 기본 29종 전체 상품 제공");
            await Expect(anon.PostAsJsonAsync("/api/case-records/draw",new{operation="Draw",requestId=Guid.NewGuid(),count=1,revision=0}),HttpStatusCode.Unauthorized,"미로그인 뽑기 차단");
            await Expect(client.GetAsync("/api/admin/case-records/draw"),HttpStatusCode.Unauthorized,"사용자의 운영 설정 열람 차단");
            var pool=CaseDraw.Draft with {Id="check_"+Guid.NewGuid().ToString("N")[..12],Published=true,Prizes=CaseDraw.Draft.Prizes.Where(p=>new[]{"afterhours","embers","archive","receipt","points"}.Contains(p.Id)).Select(p=>p with {Weight=1}).ToArray()};
            var config=await Read(admin.PutAsJsonAsync("/api/admin/case-records/draw",new CaseDraw.Configure(previous.GetProperty("revision").GetInt64(),pool)));long revision=config.GetProperty("revision").GetInt64();
            var first=await Read(client.GetAsync("/api/case-records"));Check(first.GetProperty("collection").GetProperty("draw").GetProperty("active").GetBoolean(),"설정한 시즌 뽑기 활성");
            await Expect(client.PostAsJsonAsync("/api/case-records/draw",new CaseDraw.Command("Exchange",Guid.NewGuid().ToString(),1,revision)),HttpStatusCode.Conflict,"잔액 부족 교환은 차감 없음");
            string debugId=Guid.NewGuid().ToString();var debug=new{action="DebugAddPoints",id=debugId};
            await Expect(client.PostAsJsonAsync("/api/case-records/action",debug),HttpStatusCode.Forbidden,"운영 허용 전 포인트 디버그 차단");
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/debug-points",new{enabled=true}));
            await Expect(client.PostAsJsonAsync("/api/case-records/action",debug),HttpStatusCode.Forbidden,"허용 상태여도 일반 클라이언트는 디버그 지급 차단");
            client.DefaultRequestHeaders.Add(LocalEditorTesting.ClientHeader,"Editor");
            await Read(client.PostAsJsonAsync("/api/case-records/action",debug));await Read(client.PostAsJsonAsync("/api/case-records/action",debug));
            var debugState=await Read(client.GetAsync("/api/case-records"));Check(debugState.GetProperty("credits").GetInt64()==5000&&debugState.GetProperty("xp").GetInt64()==0&&debugState.GetProperty("missionWeeklyEarned").GetInt64()==0,"디버그 클릭당 5000포인트, 재시도 중복·경험치·미션 집계 없음");
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="DebugAddPoints",id=Guid.NewGuid().ToString()}));Check((await Read(client.GetAsync("/api/case-records"))).GetProperty("credits").GetInt64()==10000,"다시 누르면 5000포인트씩 누적");
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/debug-points",new{enabled=false}));await Expect(client.PostAsJsonAsync("/api/case-records/action",new{action="DebugAddPoints",id=Guid.NewGuid().ToString()}),HttpStatusCode.Forbidden,"디버그 허용 해제 즉시 지급 차단");client.DefaultRequestHeaders.Remove(LocalEditorTesting.ClientHeader);
            await Sql("UPDATE case_accounts SET credits=5000 WHERE user_id=@u",("u",player.Id));
            var buy=new CaseDraw.Command("Exchange",Guid.NewGuid().ToString(),5,revision);
            var concurrent=await Task.WhenAll(Read(client.PostAsJsonAsync("/api/case-records/draw",buy)),Read(client.PostAsJsonAsync("/api/case-records/draw",buy)));
            var state=await Read(client.GetAsync("/api/case-records"));Check(state.GetProperty("credits").GetInt64()==2500&&state.GetProperty("collection").GetProperty("draw").GetProperty("tickets").GetInt64()==5,"동시 재시도도 5재화·2500포인트 한 번만 처리");
            await Expect(client.PostAsJsonAsync("/api/case-records/draw",buy with {Count=1}),HttpStatusCode.Conflict,"같은 요청 ID를 다른 수량으로 변조 차단");
            await Sql("INSERT INTO case_draw_progress(user_id,pool_id,pity) VALUES(@u,@p,99) ON DUPLICATE KEY UPDATE pity=99",("u",player.Id),("p",pool.Id));
            var draw=new CaseDraw.Command("Draw",Guid.NewGuid().ToString(),1,revision);var pulled=await Read(client.PostAsJsonAsync("/api/case-records/draw",draw));
            var top=pulled.GetProperty("result").GetProperty("awards")[0];Check(top.GetProperty("grade").GetInt32()==1&&top.GetProperty("guaranteed").GetBoolean(),"100번째 추첨 최고 희귀 확정");
            var current=pulled.GetProperty("draw");Check(current.GetProperty("pity").GetInt32()==0&&!current.GetProperty("pityAvailable").GetBoolean(),"최고 희귀 획득 시 일반 카운터 초기화, 획득 최고 보상 제외");
            Check(current.GetProperty("pool").GetProperty("prizes")[0].GetProperty("probability").GetDouble()==0,"획득 상위 상품의 현재 확률 0");
            var repeated=await Read(client.PostAsJsonAsync("/api/case-records/draw",draw));Check(repeated.GetProperty("result").ToString()==pulled.GetProperty("result").ToString(),"응답 유실 후 동일 결과 복구");
            await Read(client.PostAsJsonAsync("/api/case-records/draw",new CaseDraw.Command("Exchange",Guid.NewGuid().ToString(),1,revision)));
            var five=await Read(client.PostAsJsonAsync("/api/case-records/draw",new CaseDraw.Command("Draw",Guid.NewGuid().ToString(),5,revision)));
            var awards=five.GetProperty("result").GetProperty("awards");Check(awards.GetArrayLength()==5&&awards.EnumerateArray().All(a=>a.GetProperty("grade").GetInt32()!=1),"5개 뽑기에서 이전 최고 희귀 재등장 없음");
            Check(awards.EnumerateArray().Where(a=>a.GetProperty("grade").GetInt32()==2).Count()<=1,"같은 5개 뽑기 안에서도 2등급 중복 없음");
            state=await Read(client.GetAsync("/api/case-records"));Check(state.GetProperty("collection").GetProperty("draw").GetProperty("tickets").GetInt64()==0,"총 6개 뽑기의 재화 정확 차감");
            await Expect(client.PostAsJsonAsync("/api/case-records/draw",new CaseDraw.Command("Draw",Guid.NewGuid().ToString(),5,revision-1)),HttpStatusCode.Conflict,"이전 확률 버전으로 뽑기 차단");
            await Expect(client.PostAsJsonAsync("/api/case-records/featured",new{codes=new[]{"campaign_checkpoint"},bio="소개"}),HttpStatusCode.Conflict,"미달성 업적 전시 차단");
            await Sql("INSERT INTO case_achievements(user_id,code,source) VALUES(@u,'campaign_checkpoint','Local'),(@u,'campaign_escape','Local'),(@u,'campaign_complete','Local'),(@u,'bot_first','Server')",("u",player.Id));
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimAchievement",id="campaign_escape"}));
            await Expect(client.PostAsJsonAsync("/api/case-records/featured",new{codes=new[]{"campaign_checkpoint","campaign_escape","campaign_complete","bot_first"}}),HttpStatusCode.BadRequest,"대표 업적 3개 상한");
            await Read(client.PostAsJsonAsync("/api/case-records/featured",new{codes=new[]{"campaign_escape","campaign_checkpoint","campaign_complete"},bio="첫 줄\n두 번째 줄"}));
            state=await Read(client.GetAsync("/api/case-records"));Check(state.GetProperty("collection").GetProperty("featured")[0].GetProperty("code").GetString()=="campaign_escape"&&state.GetProperty("collection").GetProperty("bio").GetString()=="첫 줄\n두 번째 줄","대표 업적 순서와 소개글 줄바꿈 저장");
            Check(state.GetProperty("inventory").EnumerateArray().Any(i=>i.GetProperty("itemId").GetString()=="badge_exit"),"기존 업적에도 전용 수집 보상 소급 지급");
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="Equip",id="badge_exit"}));
            var publicProfile=await Read(client.GetAsync("/api/social/profiles/"+player.Account));Check(publicProfile.GetProperty("progression").GetProperty("featured").GetArrayLength()==3,"다른 플레이어 열람용 프로필에도 대표 업적 노출");
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="Equip",id="banner_afterhours"}));
            var room=await Read(client.PostAsJsonAsync("/api/rooms",new{name="배너 연동 확인",isPublic=true}));
            Check(room.GetProperty("room").GetProperty("members")[0].GetProperty("bannerId").GetString()=="banner_afterhours","오픈매칭 명찰은 서버의 실제 장착 배너 사용");
            var guest=await OperationsIntegration.Player(db);
            try
            {
                using var viewer=new HttpClient{BaseAddress=admin.BaseAddress};viewer.DefaultRequestHeaders.Authorization=new("Bearer",guest.Token);
                var joined=await Read(viewer.PostAsJsonAsync("/api/rooms/"+room.GetProperty("room").GetProperty("joinCode").GetString()+"/join",new{}));
                Check(joined.GetProperty("room").GetProperty("members").EnumerateArray().Any(m=>m.GetProperty("userId").GetString()==player.Id.ToString()&&m.GetProperty("bannerId").GetString()=="banner_afterhours"),"같은 방의 다른 플레이어에게도 장착 배너 전달");
                await Expect(viewer.PostAsJsonAsync("/api/case-records/action",new{action="ShowBanner",id="banner_afterhours"}),HttpStatusCode.Conflict,"보유하지 않은 다른 계정의 배너 전시 차단");
            }
            finally{await Sql("DELETE FROM room_members WHERE user_id=@u;DELETE FROM users WHERE id=@u",("u",guest.Id));}
            await Read(client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
            await Sql("INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,'banner_embers')",("u",player.Id));
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ShowBanner",id="banner_embers"}));
            var shown=await Read(client.GetAsync("/api/social/profiles/"+player.Account));Check(shown.GetProperty("progression").GetProperty("displayedBanners").GetArrayLength()==2,"다른 플레이어가 프로필에 전시한 배너 여러 개 열람");
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="Equip",id="banner_embers"}));
            state=await Read(client.GetAsync("/api/case-records"));Check(state.GetProperty("collection").GetProperty("displayedBanners").GetArrayLength()==2&&state.GetProperty("loadout").EnumerateArray().Single(i=>i.GetProperty("slot").GetString()=="Banner").GetProperty("itemId").GetString()=="banner_embers","전시 배너를 유지하면서 대표 배너만 변경");
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="HideBanner",id="banner_embers"}));
            state=await Read(client.GetAsync("/api/case-records"));Check(state.GetProperty("loadout").EnumerateArray().Single(i=>i.GetProperty("slot").GetString()=="Banner").GetProperty("itemId").GetString()=="banner_afterhours"&&state.GetProperty("inventory").EnumerateArray().Any(i=>i.GetProperty("itemId").GetString()=="banner_embers"),"대표 전시 해제 시 남은 배너로 전환, 보유 아이템은 유지");
            await Sql("UPDATE case_accounts SET credits=1500 WHERE user_id=@u",("u",player.Id));
            var exchange=new CaseDraw.Command("Exchange",Guid.NewGuid().ToString(),3,revision);await Read(client.PostAsJsonAsync("/api/case-records/draw",exchange));await Read(client.PostAsJsonAsync("/api/case-records/draw",exchange));
            state=await Read(client.GetAsync("/api/case-records"));Check(state.GetProperty("credits").GetInt64()==0&&state.GetProperty("collection").GetProperty("draw").GetProperty("tickets").GetInt64()==3,"지정 수량 3개 교환과 재시도는 정확히 한 번 차감");
            await Expect(client.PostAsJsonAsync("/api/case-records/draw",exchange with{RequestId=Guid.NewGuid().ToString(),Count=int.MaxValue}),HttpStatusCode.BadRequest,"교환 수량 오버플로 차단");
            await Expect(client.PostAsJsonAsync("/api/case-records/draw",exchange with{RequestId=Guid.NewGuid().ToString(),Count=0}),HttpStatusCode.BadRequest,"교환 수량 0 차단");
            var reset=new CaseDraw.Command("EditorReset",Guid.NewGuid().ToString(),1,revision);
            await Expect(client.PostAsJsonAsync("/api/case-records/draw",reset),HttpStatusCode.Forbidden,"일반 클라이언트 획득 초기화 차단");
            client.DefaultRequestHeaders.Add(LocalEditorTesting.ClientHeader,"Editor");
            await Expect(client.PostAsJsonAsync("/api/case-records/draw",reset),HttpStatusCode.Forbidden,"에디터 헤더만으로 초기화 불가");
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/debug-points",new{enabled=true}));
            await Sql("INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,'stamp_store')",("u",player.Id));
            await Sql("DELETE FROM case_draw_acquisitions WHERE user_id=@u;INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,'icon_receipt')",("u",player.Id));
            await Sql("INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,'frame_carbon'),(@other,'frame_carbon');INSERT INTO case_draw_acquisitions(user_id,pool_id,item_id) VALUES(@u,'earlier_fixture','frame_carbon');INSERT INTO case_draw_progress(user_id,pool_id,pity) VALUES(@u,'earlier_fixture',33)",("u",player.Id),("other",other.Id));
            var legacy=new{requestId=Guid.NewGuid().ToString(),operation="Draw",count=1,awards=new[]{new CaseDraw.Award("receipt","오래된 영수증",4,"icon_receipt",0,false,false)}};
            await Sql("INSERT INTO case_draw_receipts(user_id,request_id,fingerprint,result_json) VALUES(@u,@r,@f,@j)",("u",player.Id),("r",legacy.requestId),("f",$"Draw:1:{revision}"),("j",JsonSerializer.Serialize(legacy,new JsonSerializerOptions(JsonSerializerDefaults.Web))));
            var cleared=await Read(client.PostAsJsonAsync("/api/case-records/draw",reset));await Read(client.PostAsJsonAsync("/api/case-records/draw",reset));
            state=await Read(client.GetAsync("/api/case-records"));var inventory=state.GetProperty("inventory").EnumerateArray().Select(p=>p.GetProperty("itemId").GetString()).ToHashSet();
            Check(!inventory.Contains("banner_afterhours")&&inventory.Contains("badge_exit")&&inventory.Contains("stamp_store"),"에디터 초기화는 해당 계정 뽑기 획득만 제거하고 업적·다른 아이템 유지");
            Check(!inventory.Contains("icon_receipt"),"획득 테이블·시즌 ID 없는 구버전 뽑기 보상도 영수증으로 초기화");
            Check(!inventory.Contains("frame_carbon"),"같은 계정의 이전 시즌 뽑기 획득도 초기화");
            using(var otherClient=new HttpClient{BaseAddress=admin.BaseAddress}){otherClient.DefaultRequestHeaders.Authorization=new("Bearer",other.Token);Check((await Read(otherClient.GetAsync("/api/case-records"))).GetProperty("inventory").EnumerateArray().Any(p=>p.GetProperty("itemId").GetString()=="frame_carbon"),"다른 계정의 보유 아이템은 초기화하지 않음");}
            Check(cleared.GetProperty("snapshot").GetProperty("inventory").EnumerateArray().All(p=>p.GetProperty("itemId").GetString()!="icon_receipt"),"초기화 응답 자체에 갱신된 보관함 포함");
            await Expect(client.PostAsJsonAsync("/api/case-records/draw",draw),HttpStatusCode.Conflict,"초기화 전 영수증을 재요청해도 옛 결과 재개봉 불가");
            Check(state.GetProperty("credits").GetInt64()==0&&state.GetProperty("collection").GetProperty("draw").GetProperty("tickets").GetInt64()==3&&state.GetProperty("collection").GetProperty("draw").GetProperty("pity").GetInt32()==0,"초기화는 포인트·재화 유지, 보장 카운터 초기화");
            Check(!state.GetProperty("loadout").EnumerateArray().Any(p=>p.GetProperty("itemId").GetString()=="banner_afterhours"),"초기화한 대표 배너도 해제");
            await Sql("INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,'icon_receipt')",("u",player.Id));
            await Read(client.PostAsJsonAsync("/api/case-records/draw",reset with{RequestId=Guid.NewGuid().ToString()}));
            Check((await Read(client.GetAsync("/api/case-records"))).GetProperty("inventory").EnumerateArray().Any(p=>p.GetProperty("itemId").GetString()=="icon_receipt"),"반복 초기화는 과거 영수증 때문에 이후 다른 경로로 얻은 아이템을 삭제하지 않음");
            await Sql("INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,'banner_embers'),(@u,'banner_lastlight');INSERT IGNORE INTO case_loadout(user_id,slot,item_id) VALUES(@u,'Banner','banner_embers');INSERT IGNORE INTO case_profile_banners(user_id,item_id,position) VALUES(@u,'banner_embers',0)",("u",player.Id));
            var legacyRareReset=await Read(client.PostAsJsonAsync("/api/case-records/draw",reset with{RequestId=Guid.NewGuid().ToString()}));
            state=legacyRareReset.GetProperty("snapshot");
            Check(state.GetProperty("inventory").EnumerateArray().All(p=>p.GetProperty("itemId").GetString() is not ("banner_embers" or "banner_lastlight")),"획득·중복방지 장부가 없는 잔불과 구버전 희귀 보상도 초기화");
            Check(state.GetProperty("loadout").EnumerateArray().All(p=>p.GetProperty("itemId").GetString()!="banner_embers")&&state.GetProperty("collection").GetProperty("displayedBanners").GetArrayLength()==0,"잔불의 대표 장착·전시 배너도 함께 해제");
            Check(legacyRareReset.GetProperty("draw").GetProperty("pool").GetProperty("prizes").EnumerateArray().Single(p=>p.GetProperty("itemId").GetString()=="banner_embers").GetProperty("owned").GetBoolean()==false,"초기화 응답에서 잔불의 획득 완료 표시도 즉시 해제");
            await Sql("INSERT INTO case_draw_progress(user_id,pool_id,pity) VALUES(@u,@p,49)",("u",player.Id),("p",pool.Id));
            var soft=await Read(client.PostAsJsonAsync("/api/case-records/draw",new CaseDraw.Command("Draw",Guid.NewGuid().ToString(),1,revision)));var softAward=soft.GetProperty("result").GetProperty("awards")[0];
            Check(softAward.GetProperty("grade").GetInt32() is 1 or 2&&softAward.GetProperty("guaranteed").GetBoolean(),"HTTP 50회 반천장은 상위 등급 실제 지급");
            await Sql("INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,'banner_afterhours'),(@u,'banner_embers');UPDATE case_draw_progress SET pity=7 WHERE user_id=@u AND pool_id=@p;UPDATE case_draw_wallet SET tickets=10 WHERE user_id=@u",("u",player.Id),("p",pool.Id));
            var exhausted=new CaseDraw.Command("Draw",Guid.NewGuid().ToString(),5,revision);var counted=await Read(client.PostAsJsonAsync("/api/case-records/draw",exhausted));
            Check(counted.GetProperty("draw").GetProperty("pity").GetInt32()==12&&counted.GetProperty("draw").GetProperty("total").GetInt64()==6,"상위 보상을 모두 가진 계정도 5개 뽑기 스택·누적 횟수 증가");
            var same=await Read(client.PostAsJsonAsync("/api/case-records/draw",exhausted));Check(same.GetProperty("draw").GetProperty("pity").GetInt32()==12&&same.GetProperty("snapshot").GetProperty("collection").GetProperty("draw").GetProperty("pity").GetInt32()==12,"재시도와 즉시 계정 상태에도 스택 일치, 중복 증가 없음");
            Check((await Read(client.GetAsync("/api/case-records"))).GetProperty("collection").GetProperty("draw").GetProperty("pity").GetInt32()==12,"계정 상태 재조회 후에도 증가한 스택 보존");
            pool=pool with{Prizes=CaseDraw.Draft.Prizes.Select(p=>p with{Weight=1}).ToArray()};
            config=await Read(admin.PutAsJsonAsync("/api/admin/case-records/draw",new CaseDraw.Configure(revision,pool)));revision=config.GetProperty("revision").GetInt64();
            Check(config.GetProperty("pool").GetProperty("prizes").GetArrayLength()==29&&(await Read(client.GetAsync("/api/case-records"))).GetProperty("collection").GetProperty("draw").GetProperty("pity").GetInt32()==12,"서버 운영에서 공개 중인 기존 시즌에 29종 추가, 계정 스택 보존");
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/draw",new CaseDraw.Configure(revision,pool with{Prizes=pool.Prizes.Where(p=>p.Id!="afterhours").ToArray()})),HttpStatusCode.Conflict,"상품 추가 이후에도 기존 공개 상품 삭제 차단");
            config=await Read(admin.PutAsJsonAsync("/api/admin/case-records/draw",new CaseDraw.Configure(revision,pool with{Published=false})));revision=config.GetProperty("revision").GetInt64();
            await Sql("UPDATE case_accounts SET credits=500 WHERE user_id=@u",("u",player.Id));
            await Read(client.PostAsJsonAsync("/api/case-records/draw",new CaseDraw.Command("Exchange",Guid.NewGuid().ToString(),1,revision)));
            await Expect(client.PostAsJsonAsync("/api/case-records/draw",new CaseDraw.Command("Draw",Guid.NewGuid().ToString(),1,revision)),HttpStatusCode.Conflict,"교환은 미공개 시즌에도 허용, 미공개 뽑기는 차감 없이 거절");
            Console.WriteLine($"PASS: {checks} collection/draw checks");
        }
        finally
        {
            // This suite is only called after the isolated datadir guard in IntegrationChecks.
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/debug-points",new{enabled=false}));
            await Sql("DELETE FROM game_rooms WHERE host_user_id=@u;DELETE FROM users WHERE id IN (@u,@other)",("u",player.Id),("other",other.Id));
            await Sql("UPDATE case_draw_settings SET revision=@r,pool_json=@j WHERE id=1",("r",previous.GetProperty("revision").GetInt64()),("j",previous.GetProperty("pool").GetRawText()));
        }
    }
}

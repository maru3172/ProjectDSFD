using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class CaseMissionRewardChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int count=0;void Check(bool ok,string name){if(!ok)throw new Exception(name);count++;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Mission API {(int)r.StatusCode}: {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode expected,string name){using var r=await pending;Check(r.StatusCode==expected,name+" "+r.StatusCode);}
        async Task Sql(string sql,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,p);}
        var player=await OperationsIntegration.Player(db);using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);
        Task<JsonElement> State()=>Read(client.GetAsync("/api/case-records"));
        JsonElement Mission(JsonElement state,string slot,string mode="Pvp")=>state.GetProperty("missions").EnumerateArray().Single(m=>m.GetProperty("id").GetString()==slot&&m.GetProperty("mode").GetString()==mode);
        try
        {
            var initial=await State();Check(initial.GetProperty("levelProgress").GetDouble()==0,"새 계정 경험치 바 0%");
            await Sql("UPDATE case_accounts SET xp=50,credits=1000 WHERE user_id=@u",("u",player.Id));
            Check(Math.Abs((await State()).GetProperty("levelProgress").GetDouble()-.5)<.00001,"레벨 1 경험치 50/100은 바 50%");
            await Sql("UPDATE case_accounts SET xp=400 WHERE user_id=@u",("u",player.Id));
            Check((await State()).GetProperty("level").GetInt32()==3&&(await State()).GetProperty("levelProgress").GetDouble()==0,"레벨 상승 경계에서 해당 구간 경험치 바 초기화");
            await Expect(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimLevelReward",id="5"}),HttpStatusCode.Conflict,"미달 레벨 보상 위조 차단");
            await Sql("UPDATE case_accounts SET xp=1600 WHERE user_id=@u",("u",player.Id));
            for(int i=0;i<2;i++)await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimLevelReward",id="5"}));
            Check((await State()).GetProperty("credits").GetInt64()==1150,"5레벨 구간 보상은 1회만 지급");
            await Sql("UPDATE case_accounts SET xp=57600 WHERE user_id=@u",("u",player.Id));
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimLevelReward",id="25"}));
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="Equip",id="level_frame_25"}));
            Check((await State()).GetProperty("loadout")[0].GetProperty("itemId").GetString()=="level_frame_25","25레벨 기념 꾸미기 지급·장착");
            var all=await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimLevelReward",id="All"}));
            long afterAll=all.GetProperty("snapshot").GetProperty("credits").GetInt64();
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimLevelReward",id="All"}));
            Check((await State()).GetProperty("credits").GetInt64()==afterAll,"레벨 보상 모두 받기도 이미 수령한 구간 재지급 없음");
            await Sql("UPDATE case_accounts SET xp=980100 WHERE user_id=@u",("u",player.Id));Check((await State()).GetProperty("levelProgress").GetDouble()==1,"최고 레벨 바는 100%로 고정");
            await Sql("UPDATE case_accounts SET xp=0,credits=1000 WHERE user_id=@u",("u",player.Id));
            var periods=CaseRecords.Periods(DateTimeOffset.UtcNow);
            await Expect(client.PostAsJsonAsync("/api/case-records/action",new{action="RerollMission",id="daily_win",mode="Pvp",expectedRerolls=0,expectedPeriod="D:2000-01-01"}),HttpStatusCode.Conflict,"이전 초기화 기간의 변경 요청으로 새 미션 비용 차감 차단");
            Check((await State()).GetProperty("credits").GetInt64()==1000,"만료된 미션 변경 요청은 재화 보존");
            await Sql("INSERT INTO case_mission_progress(user_id,mode,period_id,metric,value) VALUES(@u,'Pvp',@p,'Matches',100)",("u",player.Id),("p",periods.Daily));
            var pending=Enumerable.Range(0,8).Select(_=>client.PostAsJsonAsync("/api/case-records/action",new{action="RerollMission",id="daily_win",mode="Pvp",expectedRerolls=0,expectedPeriod=periods.Daily,price=0})).ToArray();
            foreach(var r in await Task.WhenAll(pending)){Check(r.IsSuccessStatusCode,"동시 같은 미션 변경 요청은 멱등 처리");r.Dispose();}
            var state=await State();var changed=Mission(state,"daily_win");
            Check(state.GetProperty("credits").GetInt64()==990&&changed.GetProperty("rerolls").GetInt32()==1,"동시 변경 8개에도 재화는 서버 가격 10포인트 한 번만 차감");
            Check(changed.GetProperty("definitionId").GetString()!="daily_win"&&changed.GetProperty("definitionId").GetString()!="daily_play","기존·다른 칸과 다른 미션으로 변경");
            Check(changed.GetProperty("progress").GetInt64()==0,"변경 이전 경기 기록을 새 미션에 소급 적용하지 않음");
            Check(changed.GetProperty("rerollCost").GetInt64()==20,"변경 후 다음 변경 비용 증가");
            Check(Mission(state,"daily_win","Bot").GetProperty("rerolls").GetInt32()==0,"PvP 변경 횟수가 봇전 칸에 섞이지 않음");
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="RerollMission",id="daily_win",mode="Pvp",expectedRerolls=1,expectedPeriod=periods.Daily}));
            state=await State();changed=Mission(state,"daily_win");Check(state.GetProperty("credits").GetInt64()==970&&changed.GetProperty("rerollCost").GetInt64()==30,"두 번째 변경 차감 20·다음 비용 30");
            string definition=changed.GetProperty("definitionId").GetString()!,metric=definition switch{"daily_games2"=>"Matches","daily_survivor"=>"SurvivorMatches","daily_mannequin"=>"MannequinMatches",_=>"Wins"};
            int target=changed.GetProperty("target").GetInt32();
            await Sql("INSERT INTO case_mission_progress(user_id,mode,period_id,metric,value) VALUES(@u,'Pvp',@p,@m,@n) ON DUPLICATE KEY UPDATE value=value+@n",("u",player.Id),("p",periods.Daily),("m",metric),("n",target));
            await Expect(client.PostAsJsonAsync("/api/case-records/action",new{action="RerollMission",id="daily_win",mode="Pvp",expectedRerolls=2,expectedPeriod=periods.Daily}),HttpStatusCode.Conflict,"완료했지만 아직 수령하지 않은 칸도 변경 차단");
            long reward=Mission(await State(),"daily_win").GetProperty("reward").GetInt64();
            for(int i=0;i<2;i++)await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimMission",id="daily_win",mode="Pvp"}));
            Check((await State()).GetProperty("credits").GetInt64()==970+reward,"변경한 칸의 보상은 초기화 전 한 번만 지급");
            await Expect(client.PostAsJsonAsync("/api/case-records/action",new{action="RerollMission",id="daily_win",mode="Pvp",expectedRerolls=2,expectedPeriod=periods.Daily}),HttpStatusCode.Conflict,"수령 완료한 칸은 미션 변경으로 다시 채울 수 없음");
            await using(var c=await db.OpenConnectionAsync(default))await using(var tx=await c.BeginTransactionAsync(default))
            {
                var reset=await CaseMissionSlots.LoadAsync(c,tx,player.Id,"Pvp",CaseRecords.Missions.Single(m=>m.Id=="daily_win"),DateTimeOffset.UtcNow.AddDays(1),default);
                Check(reset.Rerolls==0&&!reset.Claimed&&reset.Progress==0&&reset.Definition.Id=="daily_win","다음 일일 초기화에는 완료 잠금·변경 비용·진행도 새로 시작");
            }
            Check(CaseMissionSlots.Cost(true,0)==50&&CaseMissionSlots.Cost(true,1)==100,"주간 칸도 변경할수록 비용 증가");
            Console.WriteLine($"PASS: {count} level reward and mission reroll checks (isolated API/MySQL)");
        }
        finally{await Sql("DELETE FROM users WHERE id=@u",("u",player.Id));}
    }
}

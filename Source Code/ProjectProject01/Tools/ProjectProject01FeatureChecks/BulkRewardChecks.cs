using System.Net.Http.Json;
using System.Text.Json;

internal static class BulkRewardChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int checks=0;void Check(bool ok,string name){if(!ok)throw new Exception(name);checks++;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> task){using var r=await task;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception(text);return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Sql(string sql,params(string,object?)[] args){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,args);}
        var player=await OperationsIntegration.Player(db);using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);
        try
        {
            await Read(client.GetAsync("/api/case-records"));await Sql("UPDATE case_accounts SET xp=10000 WHERE user_id=@u",("u",player.Id));
            await Task.WhenAll(Enumerable.Range(0,4).Select(_=>Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimLevelReward",id="All"}))));
            Check((await Read(client.GetAsync("/api/case-records"))).GetProperty("credits").GetInt64()==350,"레벨 5·10 일괄 수령과 동시 중복 지급 차단");
            var periods=CaseRecords.Periods(DateTimeOffset.UtcNow);
            foreach(string mode in new[]{"Pvp","Bot"})foreach(string period in new[]{periods.Daily,periods.Weekly})
                await Sql("INSERT INTO case_mission_progress(user_id,mode,period_id,metric,value) VALUES(@u,@m,@p,'Matches',5),(@u,@m,@p,'Wins',3)",("u",player.Id),("m",mode),("p",period));
            await Task.WhenAll(new[]{"Pvp","Bot","Pvp","Bot"}.Select(mode=>Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimAllMissions",id="All",mode}))));
            var state=await Read(client.GetAsync("/api/case-records"));
            Check(state.GetProperty("credits").GetInt64()==1210&&state.GetProperty("missions").EnumerateArray().All(m=>m.GetProperty("claimed").GetBoolean()),"PvP·봇전 일일·주간 보상 일괄 수령은 완료한 미수령 항목만 지급");
            await Sql("DELETE FROM case_claims WHERE user_id=@u AND reward_key NOT LIKE 'AccountLevel:%';UPDATE case_mission_budget_usage SET earned=1990 WHERE user_id=@u AND week_id=@w",("u",player.Id),("w",periods.Weekly));
            await Task.WhenAll(new[]{"Pvp","Bot"}.Select(mode=>Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimAllMissions",id="All",mode}))));
            state=await Read(client.GetAsync("/api/case-records"));
            Check(state.GetProperty("credits").GetInt64()==1220&&state.GetProperty("missionWeeklyEarned").GetInt64()==2000,"일괄 수령도 PvP·봇전 공유 주간 상한의 남은 10포인트만 지급");
            var low=CaseMatchRewards.Calculate("Pvp","Survivor",false,0,0,0,0,0,1);var high=CaseMatchRewards.Calculate("Pvp","Survivor",true,0,3,0,0,90,1);
            Check(high.Xp>low.Xp&&high.Credits>low.Credits,"PvP 성과가 높으면 경험치·포인트 증가");
            int previous=0;for(int level=1;level<=5;level++){var reward=CaseMatchRewards.Calculate("Bot","Survivor",true,0,1,0,0,180,level);Check(reward.Xp>previous,"PvE 난이도 단계에 따른 보상 상승 "+level);previous=reward.Xp;}
            var easyBest=CaseMatchRewards.Calculate("Bot","Survivor",true,0,3,0,0,1,1);var hardWorst=CaseMatchRewards.Calculate("Bot","Survivor",false,0,0,0,0,0,5);
            Check(hardWorst.Xp>easyBest.Xp&&hardWorst.Credits>easyBest.Credits,"PvE는 성과보다 난이도 비중이 더 큼");
            Console.WriteLine($"PASS: {checks} bulk reward and match performance checks");
        }
        finally{await Sql("DELETE FROM users WHERE id=@u",("u",player.Id));}
    }
}

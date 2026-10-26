using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class MissionBudgetChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int checks=0;void Check(bool ok,string title){if(!ok)throw new Exception(title);checks++;Console.WriteLine("PASS "+title);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> task){using var r=await task;var text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception(text);return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Sql(string sql,params(string,object?)[] args){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,args);}
        var p=await OperationsIntegration.Player(db);var historical=await OperationsIntegration.Player(db);
        using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",p.Token);
        using var second=new HttpClient{BaseAddress=admin.BaseAddress};second.DefaultRequestHeaders.Authorization=new("Bearer",historical.Token);
        var old=await Read(admin.GetAsync("/api/admin/case-records/mission-budget"));var periods=CaseRecords.Periods(DateTimeOffset.UtcNow);
        try
        {
            using(var denied=await client.PutAsJsonAsync("/api/admin/case-records/mission-budget",new{weeklyLimit=9999}))Check(denied.StatusCode==HttpStatusCode.Unauthorized,"주간 상한은 운영자만 변경");
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/mission-budget",new{weeklyLimit=2000}));
            await Sql("INSERT IGNORE INTO case_accounts(user_id) VALUES(@u);INSERT INTO case_mission_budget_usage(user_id,week_id,earned) VALUES(@u,@w,1990);INSERT INTO case_mission_progress(user_id,mode,period_id,metric,value) VALUES(@u,'Pvp',@d,'Matches',1),(@u,'Bot',@d,'Matches',1)",("u",p.Id),("w",periods.Weekly),("d",periods.Daily));
            var responses=await Task.WhenAll(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimMission",id="daily_play",mode="Pvp",expectedPeriod=periods.Daily}),client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimMission",id="daily_play",mode="Bot",expectedPeriod=periods.Daily}));
            Check(responses.Count(r=>r.StatusCode==HttpStatusCode.OK)==1&&responses.Count(r=>r.StatusCode==HttpStatusCode.Conflict)==1,"PvP·봇전 동시 수령도 공통 상한 초과 불가");foreach(var r in responses)r.Dispose();
            var state=await Read(client.GetAsync("/api/case-records"));Check(state.GetProperty("missionWeeklyEarned").GetInt64()==2000&&state.GetProperty("credits").GetInt64()==10,"남은 10포인트만 부분 지급하고 정확히 2000에서 정지");
            var claimed=state.GetProperty("missions").EnumerateArray().Single(m=>m.GetProperty("id").GetString()=="daily_play"&&m.GetProperty("claimed").GetBoolean()).GetProperty("mode").GetString();
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimMission",id="daily_play",mode=claimed,expectedPeriod=periods.Daily}));
            Check((await Read(client.GetAsync("/api/case-records"))).GetProperty("credits").GetInt64()==10,"이미 수령한 미션 재시도는 한도·잔액을 재지급하지 않음");
            await Sql("UPDATE case_accounts SET credits=credits+1000 WHERE user_id=@u",("u",p.Id));
            Check((await Read(client.GetAsync("/api/case-records"))).GetProperty("missionWeeklyEarned").GetInt64()==2000,"다른 보상과 보유 잔액은 미션 주간 상한과 별개");
            await Sql("INSERT INTO case_claims(user_id,reward_key) VALUES(@u,@p),(@u,@b)",("u",historical.Id),("p",periods.Daily+":Pvp:daily_play"),("b",periods.Daily+":Bot:daily_play"));
            Check((await Read(second.GetAsync("/api/case-records"))).GetProperty("missionWeeklyEarned").GetInt64()==60,"업데이트 이전 이번 주 수령 영수증도 공통 한도에 반영");
            await using(var c=await db.OpenConnectionAsync(default))await using(var tx=await c.BeginTransactionAsync())
            {await CaseRecords.LockAccountAsync(c,tx,p.Id,default);var next=await CaseMissionBudget.StateAsync(c,tx,p.Id,DateTimeOffset.UtcNow.AddDays(7),default);Check(next.Earned==0&&next.Limit==2000,"다음 주에는 기존 잔액과 별개로 주간 획득량 초기화");await tx.RollbackAsync();}
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/mission-budget",new{weeklyLimit=2010}));
            Check((await Read(client.GetAsync("/api/case-records"))).GetProperty("missionWeeklyEarned").GetInt64()==2000,"상한 변경은 기존 주간 획득량을 지우지 않음");
            Console.WriteLine($"PASS: {checks} shared mission budget HTTP/DB checks");
        }
        finally{await Read(admin.PutAsJsonAsync("/api/admin/case-records/mission-budget",new{weeklyLimit=old.GetProperty("weeklyLimit").GetInt32()}));await Sql("DELETE FROM users WHERE id=@u OR id=@v",("u",p.Id),("v",historical.Id));}
    }
}

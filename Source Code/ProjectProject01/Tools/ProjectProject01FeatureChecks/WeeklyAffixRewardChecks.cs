using System.Net.Http.Json;
using System.Text.Json;

/// <summary>Weekly affix bonus: paid once per week on the first weekly-affix bot win, and reported in the room list.</summary>
internal static class WeeklyAffixRewardChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int count=0;void Check(bool ok,string name){if(!ok)throw new Exception(name);count++;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Weekly affix API {(int)r.StatusCode}: {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Sql(string sql,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,p);}
        var player=await OperationsIntegration.Player(db);using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);
        async Task<bool> Grant(string match,int difficulty)
        {
            await using var c=await db.OpenConnectionAsync(default);await using var tx=await c.BeginTransactionAsync(default);
            bool paid=await MatchAffixes.GrantWeeklyBonusAsync(c,tx,player.Id,match,difficulty,default);await tx.CommitAsync(default);return paid;
        }
        Task<JsonElement> State()=>Read(client.GetAsync("/api/case-records"));
        async Task<JsonElement> Reward()=>(await Read(client.GetAsync("/api/rooms"))).GetProperty("weeklyAffixes").GetProperty("reward");
        try
        {
            await State();await Sql("UPDATE case_accounts SET xp=0,credits=1000 WHERE user_id=@u",("u",player.Id));
            Check(!(await Reward()).GetProperty("received").GetBoolean(),"이번 주 보너스를 받기 전에는 '아직 받지 않음'으로 표시");
            Check(MatchAffixes.WeeklyBonus(1)==(400,200)&&MatchAffixes.WeeklyBonus(5)==(2000,1000)&&MatchAffixes.WeeklyBonus(9)==(2000,1000),"보너스 크기: 쉬움 +400·+200 ~ 마스터 +2000·+1000, 범위 밖 난이도는 잘라냄");

            Check(await Grant("weekly-check-1",3),"이번 주 첫 주간 조합 승리에 보너스 지급");
            var after=await State();
            Check(after.GetProperty("credits").GetInt64()==1600&&after.GetProperty("xp").GetInt64()==1200,"난이도 3 보너스(경험치 +1200, 포인트 +600)가 계정에 반영");
            var reward=await Reward();
            Check(reward.GetProperty("received").GetBoolean()&&reward.GetProperty("difficulty").GetInt32()==3&&reward.GetProperty("xp").GetInt32()==1200&&reward.GetProperty("credits").GetInt32()==600,"방 목록에 '받음'과 받은 난이도·보상 표시");

            Check(!await Grant("weekly-check-2",5),"같은 주 두 번째 승리는 더 높은 난이도라도 다시 지급하지 않음");
            var pending=Enumerable.Range(0,6).Select(i=>Grant("weekly-check-race-"+i,4)).ToArray();
            Check((await Task.WhenAll(pending)).All(paid=>!paid),"동시에 끝난 여러 승리도 추가 지급 없음");
            Check((await State()).GetProperty("credits").GetInt64()==1600,"재시도·동시 요청 뒤에도 포인트는 한 번 지급한 그대로");

            // A bonus from an earlier week does not count as this week's.
            await Sql("UPDATE weekly_affix_rewards SET week_start=DATE_SUB(week_start,INTERVAL 7 DAY) WHERE user_id=@u",("u",player.Id));
            Check(!(await Reward()).GetProperty("received").GetBoolean(),"지난주에 받은 보너스는 이번 주 수령으로 보지 않음");
            Check(await Grant("weekly-check-3",1),"새 주가 되면 다시 받을 수 있음");
            Console.WriteLine($"PASS: {count} weekly affix bonus checks (isolated API/MySQL)");
        }
        finally{await Sql("DELETE FROM weekly_affix_rewards WHERE user_id=@u",("u",player.Id));await Sql("DELETE FROM users WHERE id=@u",("u",player.Id));}
    }
}

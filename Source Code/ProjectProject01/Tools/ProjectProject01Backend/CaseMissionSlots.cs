using MySqlConnector;
using System.Security.Cryptography;

internal static class CaseMissionSlots
{
    private static readonly CaseRecords.Mission[] Alternatives=[
        new("daily_games2","경기 2회 완료","Matches",2,60,false),new("daily_survivor","생존자로 경기 1회 완료","SurvivorMatches",1,30,false),new("daily_mannequin","마네킹으로 경기 1회 완료","MannequinMatches",1,30,false),
        new("weekly_games8","경기 8회 완료","Matches",8,240,true),new("weekly_survivor","생존자로 경기 3회 완료","SurvivorMatches",3,120,true),new("weekly_mannequin","마네킹으로 경기 3회 완료","MannequinMatches",3,120,true)];
    internal sealed record Slot(CaseRecords.Mission Definition,long Progress,int Rerolls,long Cost,bool Claimed,string Key,string Period);
    internal static long Cost(bool weekly,int rerolls)=>(weekly?50L:10L)*(Math.Max(0,rerolls)+1L);
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS case_mission_choices(user_id BIGINT UNSIGNED NOT NULL,mode VARCHAR(8) NOT NULL,period_id VARCHAR(16) NOT NULL,slot_id VARCHAR(32) NOT NULL,
          definition_id VARCHAR(32) NOT NULL,baseline BIGINT NOT NULL DEFAULT 0,rerolls INT NOT NULL DEFAULT 0,
          PRIMARY KEY(user_id,mode,period_id,slot_id),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);
    private static IEnumerable<CaseRecords.Mission> Pool(bool weekly)=>CaseRecords.Missions.Concat(Alternatives).Where(m=>m.Weekly==weekly);
    internal static Slot FromRows(string mode,CaseRecords.Mission slot,DateTimeOffset now,IReadOnlyList<Dictionary<string,object?>> choices,IReadOnlyList<Dictionary<string,object?>> progress,ISet<string> claimed)
    {
        var periods=CaseRecords.Periods(now);string period=slot.Weekly?periods.Weekly:periods.Daily,key=period+":"+mode+":"+slot.Id;
        var row=choices.FirstOrDefault(r=>Convert.ToString(r["mode"])==mode&&Convert.ToString(r["period_id"])==period&&Convert.ToString(r["slot_id"])==slot.Id);
        var selected=row is null?slot:Pool(slot.Weekly).FirstOrDefault(m=>m.Id==Convert.ToString(row["definition_id"]))??slot;
        long baseline=row is null?0:Convert.ToInt64(row["baseline"]);int rerolls=row is null?0:Convert.ToInt32(row["rerolls"]);
        long value=progress.Where(r=>Convert.ToString(r["mode"])==mode&&Convert.ToString(r["periodId"])==period&&Convert.ToString(r["metric"])==selected.Metric).Select(r=>Convert.ToInt64(r["value"])).FirstOrDefault();
        return new(selected,Math.Max(0,value-baseline),rerolls,Cost(slot.Weekly,rerolls),claimed.Contains(key),key,period);
    }
    private static async Task<long> Counter(MySqlConnection c,MySqlTransaction tx,ulong user,string mode,string period,string metric,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT value FROM case_mission_progress WHERE user_id=@u AND mode=@mode AND period_id=@p AND metric=@m",ct,("u",user),("mode",mode),("p",period),("m",metric));
        return rows.Count==0?0:Convert.ToInt64(rows[0]["value"]);
    }
    internal static async Task<Slot> LoadAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string mode,CaseRecords.Mission slot,DateTimeOffset now,CancellationToken ct)
    {
        var periods=CaseRecords.Periods(now);string period=slot.Weekly?periods.Weekly:periods.Daily,key=period+":"+mode+":"+slot.Id;
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT definition_id,baseline,rerolls FROM case_mission_choices WHERE user_id=@u AND mode=@mode AND period_id=@p AND slot_id=@s",ct,("u",user),("mode",mode),("p",period),("s",slot.Id));
        var chosen=rows.Count==0?slot:Pool(slot.Weekly).FirstOrDefault(m=>m.Id==Convert.ToString(rows[0]["definition_id"]))??slot;
        long baseline=rows.Count==0?0:Convert.ToInt64(rows[0]["baseline"]);int rerolls=rows.Count==0?0:Convert.ToInt32(rows[0]["rerolls"]);
        bool claimed=(await CommunityEndpoints.Rows(c,tx,"SELECT reward_key FROM case_claims WHERE user_id=@u AND reward_key=@k",ct,("u",user),("k",key))).Count>0;
        return new(chosen,Math.Max(0,await Counter(c,tx,user,mode,period,chosen.Metric,ct)-baseline),rerolls,Cost(slot.Weekly,rerolls),claimed,key,period);
    }
    internal static async Task<string?> ChangeAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string mode,CaseRecords.Mission slot,DateTimeOffset now,int? expected,long balance,CancellationToken ct)
    {
        var state=await LoadAsync(c,tx,user,mode,slot,now,ct);
        if(expected is null||expected<0||expected>state.Rerolls)return "미션 변경 상태를 새로 확인하세요.";
        if(expected<state.Rerolls)return null; // A retry of the same click never spends twice.
        if(state.Claimed||state.Progress>=state.Definition.Target)return "완료한 미션 칸은 다음 초기화까지 변경할 수 없습니다.";
        if(balance<state.Cost)return "미션 변경에 필요한 기록 포인트가 부족합니다.";
        var used=new HashSet<string>{state.Definition.Id};
        foreach(var other in CaseRecords.Missions.Where(m=>m.Weekly==slot.Weekly&&m.Id!=slot.Id))used.Add((await LoadAsync(c,tx,user,mode,other,now,ct)).Definition.Id);
        var choices=Pool(slot.Weekly).Where(m=>!used.Contains(m.Id)).ToArray();if(choices.Length==0)return "변경 가능한 다른 미션이 없습니다.";
        var next=choices[RandomNumberGenerator.GetInt32(choices.Length)];long baseline=await Counter(c,tx,user,mode,state.Period,next.Metric,ct);
        await CommunityEndpoints.Exec(c,tx,"""
            UPDATE case_accounts SET credits=credits-@cost WHERE user_id=@u;
            INSERT INTO case_mission_choices(user_id,mode,period_id,slot_id,definition_id,baseline,rerolls) VALUES(@u,@mode,@p,@s,@def,@base,1)
              ON DUPLICATE KEY UPDATE definition_id=@def,baseline=@base,rerolls=rerolls+1;
            """,ct,("cost",state.Cost),("u",user),("mode",mode),("p",state.Period),("s",slot.Id),("def",next.Id),("base",baseline));
        return null;
    }
}

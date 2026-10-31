using MySqlConnector;

internal static class CaseMissionBulkRewards
{
    // Caller holds the account lock. Claims and the shared PvP/Bot weekly cap commit together.
    internal static async Task ClaimAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string mode,CancellationToken ct)
    {
        var times=await CommunityEndpoints.Rows(c,tx,"SELECT UTC_TIMESTAMP(6) utc",ct);
        var now=new DateTimeOffset(DateTime.SpecifyKind(Convert.ToDateTime(times[0]["utc"]),DateTimeKind.Utc));
        foreach(var mission in CaseRecords.Missions)
        {
            var state=await CaseMissionSlots.LoadAsync(c,tx,user,mode,mission,now,ct);
            if(state.Claimed||state.Progress<state.Definition.Target)continue;
            int granted=await CaseMissionBudget.GrantAsync(c,tx,user,state.Definition.Reward,now,ct);
            if(granted==0)break;
            await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_claims(user_id,reward_key) VALUES(@u,@k);UPDATE case_accounts SET credits=LEAST(1000000000,credits+@v) WHERE user_id=@u",ct,("u",user),("k",state.Key),("v",granted));
        }
    }
}

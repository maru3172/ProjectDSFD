using MySqlConnector;

internal static class CaseLevelRewards
{
    internal static long Credits(int level)=>100+level*10L;
    internal static string? Item(int level)=>level is 25 or 50 or 75 or 100?"level_frame_"+level:null;
    internal static bool IsItem(string id)=>id.StartsWith("level_frame_",StringComparison.Ordinal)&&int.TryParse(id[12..],out int level)&&Item(level)==id;
    internal static string Key(int level)=>"AccountLevel:"+level;
    internal static async Task<bool> ClaimAsync(MySqlConnection c,MySqlTransaction tx,ulong user,int currentLevel,string id,CancellationToken ct)
    {
        if(id!="All"&&(!int.TryParse(id,out int target)||target is <5 or >100||target%5!=0||target>currentLevel))return false;
        var targets=Enumerable.Range(1,20).Select(n=>n*5).Where(n=>n<=currentLevel&&(id=="All"||id==n.ToString()));
        long credits=0;
        foreach(int level in targets)
        {
            if((await CommunityEndpoints.Rows(c,tx,"SELECT reward_key FROM case_claims WHERE user_id=@u AND reward_key=@k",ct,("u",user),("k",Key(level)))).Count>0)continue;
            await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_claims(user_id,reward_key) VALUES(@u,@k)",ct,("u",user),("k",Key(level)));credits+=Credits(level);
            if(Item(level)is {} item)await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO case_inventory(user_id,item_id) VALUES(@u,@i)",ct,("u",user),("i",item));
        }
        if(credits>0)await CommunityEndpoints.Exec(c,tx,"UPDATE case_accounts SET credits=LEAST(1000000000,credits+@n) WHERE user_id=@u",ct,("u",user),("n",credits));
        return true;
    }
}

using MySqlConnector;
using System.Security.Cryptography;

internal static class RoomRoleChoices
{
    // Preference changes selection, never queue eligibility: three Survivor preferences must still form a match.
    internal static int PreferredMannequinSlot(IReadOnlyList<string> roles,int randomIndex)
    {
        var candidates=Enumerable.Range(0,roles.Count).Where(i=>roles[i]=="Mannequin").ToArray();
        if(candidates.Length==0)candidates=Enumerable.Range(0,roles.Count).Where(i=>roles[i]!="Survivor").ToArray();
        if(candidates.Length==0)candidates=Enumerable.Range(0,roles.Count).ToArray();
        return candidates[(int)((uint)randomIndex%(uint)candidates.Length)];
    }
    internal static async Task<int> PreferredMannequinSlotAsync(MySqlConnection c,MySqlTransaction tx,IReadOnlyList<ulong> humans,CancellationToken ct)
    {
        var roles=new List<string>();
        foreach(ulong user in humans)
        {
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT role FROM case_role_preferences WHERE user_id=@u",ct,("u",user));
            roles.Add(rows.Count==0?"Any":Convert.ToString(rows[0]["role"])!);
        }
        while(roles.Count<3)roles.Add("Any");
        return PreferredMannequinSlot(roles,RandomNumberGenerator.GetInt32(int.MaxValue));
    }
    private static async Task<List<(ulong User,string Role)>> Choices(MySqlConnection c,MySqlTransaction? tx,string room,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT m.user_id,COALESCE(p.role,'Any') role FROM room_members m LEFT JOIN case_role_preferences p ON p.user_id=m.user_id WHERE m.room_id=@r AND m.is_observer=FALSE ORDER BY m.joined_at_utc,m.user_id",ct,("r",room));
        var result=rows.Select(r=>(Convert.ToUInt64(r["user_id"]),Convert.ToString(r["role"])!)).ToList();
        var botRoom=await CommunityEndpoints.Rows(c,tx,"SELECT r.host_user_id,b.human_role FROM game_rooms r JOIN bot_rooms b ON b.room_id=r.id WHERE r.id=@r",ct,("r",room));
        if(botRoom.Count>0)
        {
            ulong host=Convert.ToUInt64(botRoom[0]["host_user_id"]);int index=result.FindIndex(p=>p.Item1==host);
            if(index>=0)result[index]=(host,Convert.ToString(botRoom[0]["human_role"])!);
        }
        return result;
    }
    internal static bool CanForm(IReadOnlyList<string> roles,int bots)=>roles.Count+bots==3&&roles.Count(r=>r=="Mannequin")<=1&&roles.Count(r=>r=="Survivor")<=2&&
        (roles.Contains("Mannequin")||roles.Contains("Any")||bots>0);
    internal static async Task<bool> CanReserveAsync(MySqlConnection c,MySqlTransaction tx,string room,CancellationToken ct)
    {
        var roles=await Choices(c,tx,room,ct);return roles.Count(r=>r.Role=="Mannequin")<=1&&roles.Count(r=>r.Role=="Survivor")<=2;
    }
    internal static async Task<bool> CanFormAsync(MySqlConnection c,MySqlTransaction? tx,string room,int bots,CancellationToken ct)
    {
        if((await CommunityEndpoints.Rows(c,tx,"SELECT room_id FROM random_match_rooms WHERE room_id=@r",ct,("r",room))).Count>0)return true;
        return CanForm((await Choices(c,tx,room,ct)).Select(x=>x.Role).ToArray(),bots);
    }
    internal static async Task<bool> ApplyIfNeededAsync(MySqlConnection c,MySqlTransaction tx,string room,IReadOnlyList<ulong> humans,IReadOnlyList<MatchBot> bots,CancellationToken ct)
    {
        var choices=await Choices(c,tx,room,ct);
        if(choices.All(x=>x.Role=="Any"))return false; // Keep the existing fair assignment when nobody selected a role.
        if(!CanForm(choices.Select(x=>x.Role).ToArray(),bots.Count))throw new InvalidOperationException("Conflicting role choices");
        ulong? mannequin=choices.Where(x=>x.Role=="Mannequin").Select(x=>(ulong?)x.User).FirstOrDefault();
        string? botMannequin=null;
        if(mannequin is null)
        {
            var available=choices.Where(x=>x.Role=="Any").Select(x=>x.User).ToArray();int chosen=RandomNumberGenerator.GetInt32(available.Length+bots.Count);
            if(chosen<available.Length)mannequin=available[chosen];else botMannequin=bots[chosen-available.Length].BotId;
        }
        await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET assigned_role=IF(is_observer,'Observer','Survivor'),returned_to_room=FALSE WHERE room_id=@r;UPDATE room_bots SET assigned_role='Survivor' WHERE room_id=@r",ct,("r",room));
        if(mannequin is not null)await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET assigned_role='Mannequin' WHERE room_id=@r AND user_id=@u",ct,("r",room),("u",mannequin));
        else await CommunityEndpoints.Exec(c,tx,"UPDATE room_bots SET assigned_role='Mannequin' WHERE room_id=@r AND bot_id=@b",ct,("r",room),("b",botMannequin));
        return true;
    }
}

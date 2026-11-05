using MySqlConnector;

/// <summary>One played match of a role-swap series, as the room screen shows it.</summary>
internal sealed record RoleSwapRound(string Mannequin,bool Finished,bool FullCatch,double? AllCaughtSeconds,int Catches);
/// <summary>Role-swap progress of a room (Docs/CustomGamePlayground.md 12.2). Best is a plain sentence; there is no ranking.</summary>
internal sealed record RoleSwapProgress(int Played,int Total,IReadOnlyList<RoleSwapRound> Rounds,string NextMannequin,string Best);

/// <summary>
/// Custom game "role swap": three matches in a row in one room, each of the three players is the mannequin once.
/// The series is the run of matches with the same three players; a new series starts after three matches or when the
/// players change. Role preferences must still form a match, but do not pick the mannequin while the rule is on; bot rooms
/// and random matches never use it.
/// </summary>
internal static class RoleSwap
{
    internal const int Rounds=3;

    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS room_role_swap_rounds(room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          series INT UNSIGNED NOT NULL,round_no INT UNSIGNED NOT NULL,match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          mannequin_user_id BIGINT UNSIGNED NOT NULL,members_key VARCHAR(96) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
          PRIMARY KEY(room_id,match_id),KEY ix_role_swap_series(room_id,series,round_no),
          FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE);
        """,ct);

    internal static string MembersKey(IEnumerable<ulong> users)=>string.Join(',',users.OrderBy(u=>u));

    /// <summary>Who is the mannequin next: the first player (in room order) who has not been one in this series.</summary>
    internal static ulong NextMannequin(IReadOnlyList<ulong> humans,IReadOnlyCollection<ulong> alreadyMannequin)=>
        humans.FirstOrDefault(u=>!alreadyMannequin.Contains(u),humans[0]);

    private static async Task<(int Series,List<(ulong User,string Match)> Rounds)> CurrentAsync(MySqlConnection c,MySqlTransaction? tx,string room,string key,CancellationToken ct)
    {
        var last=await CommunityEndpoints.Rows(c,tx,"SELECT series,members_key FROM room_role_swap_rounds WHERE room_id=@r ORDER BY series DESC,round_no DESC LIMIT 1",ct,("r",room));
        if(last.Count==0)return(1,[]);
        int series=Convert.ToInt32(last[0]["series"]);
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT mannequin_user_id,match_id FROM room_role_swap_rounds WHERE room_id=@r AND series=@s ORDER BY round_no",ct,("r",room),("s",series));
        var rounds=rows.Select(r=>(Convert.ToUInt64(r["mannequin_user_id"]),Convert.ToString(r["match_id"])!)).ToList();
        if(Convert.ToString(last[0]["members_key"])!=key||rounds.Count>=Rounds)return(series+1,[]);
        return(series,rounds);
    }

    private static async Task<List<ulong>> PlayersAsync(MySqlConnection c,MySqlTransaction? tx,string room,CancellationToken ct)=>
        (await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM room_members WHERE room_id=@r AND is_observer=FALSE ORDER BY joined_at_utc,user_id",ct,("r",room)))
            .Select(r=>Convert.ToUInt64(r["user_id"])).ToList();

    private static async Task<bool> AppliesAsync(MySqlConnection c,MySqlTransaction? tx,string room,int humans,int bots,CancellationToken ct)
    {
        if(humans!=3||bots!=0)return false;
        if((await CommunityEndpoints.Rows(c,tx,"SELECT room_id FROM bot_rooms WHERE room_id=@r UNION ALL SELECT room_id FROM random_match_rooms WHERE room_id=@r",ct,("r",room))).Count>0)return false;
        var rules=await CustomRoomRules.RoomAsync(c,tx,room,ct);
        return rules.Enabled&&rules.RoleSwap&&CustomRoomRules.Valid(rules);
    }

    /// <summary>Assigns the roles for the next match of the series. False when the rule does not apply to this room.</summary>
    internal static async Task<bool> AssignIfNeededAsync(MySqlConnection c,MySqlTransaction tx,string room,IReadOnlyList<ulong> humans,int bots,CancellationToken ct)
    {
        if(!await AppliesAsync(c,tx,room,humans.Count,bots,ct))return false;
        var ordered=(await PlayersAsync(c,tx,room,ct)).Where(humans.Contains).ToList();if(ordered.Count!=3)return false;
        var (_,rounds)=await CurrentAsync(c,tx,room,MembersKey(ordered),ct);
        ulong next=NextMannequin(ordered,rounds.Select(r=>r.User).ToHashSet());
        await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET assigned_role=IF(is_observer,'Observer','Survivor'),returned_to_room=FALSE WHERE room_id=@r",ct,("r",room));
        await CommunityEndpoints.Exec(c,tx,"UPDATE room_members SET assigned_role='Mannequin' WHERE room_id=@r AND user_id=@u",ct,("r",room),("u",next));
        return true;
    }

    /// <summary>At match start: the match becomes the next round of the room's series.</summary>
    internal static async Task CaptureAsync(MySqlConnection c,MySqlTransaction tx,string match,CancellationToken ct)
    {
        var rooms=await CommunityEndpoints.Rows(c,tx,"SELECT id FROM game_rooms WHERE match_id=@m AND status='Started'",ct,("m",match));
        if(rooms.Count!=1)return;string room=Convert.ToString(rooms[0]["id"])!;
        var players=await PlayersAsync(c,tx,room,ct);
        int bots=(await CommunityEndpoints.Rows(c,tx,"SELECT bot_id FROM room_bots WHERE room_id=@r",ct,("r",room))).Count;
        if(!await AppliesAsync(c,tx,room,players.Count,bots,ct))return;
        var mannequin=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM room_members WHERE room_id=@r AND assigned_role='Mannequin'",ct,("r",room));
        if(mannequin.Count!=1)return;
        string key=MembersKey(players);var (series,rounds)=await CurrentAsync(c,tx,room,key,ct);
        if(rounds.Any(r=>r.Match==match))return;
        await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO room_role_swap_rounds(room_id,series,round_no,match_id,mannequin_user_id,members_key) VALUES(@r,@s,@n,@m,@u,@k)",ct,
            ("r",room),("s",series),("n",rounds.Count+1),("m",match),("u",Convert.ToUInt64(mannequin[0]["user_id"])),("k",key));
    }

    internal static string Duration(double seconds){int s=(int)Math.Round(seconds);return s>=60?$"{s/60}분 {s%60}초":$"{s}초";}

    /// <summary>The sentence under the rounds: fastest full catch, or (nobody caught everyone) the most catches.</summary>
    internal static string BestLine(IReadOnlyList<RoleSwapRound> rounds)
    {
        var finished=rounds.Where(r=>r.Finished).ToList();if(finished.Count==0)return "";
        var fastest=finished.Where(r=>r.FullCatch&&r.AllCaughtSeconds is not null).OrderBy(r=>r.AllCaughtSeconds).FirstOrDefault();
        if(fastest is not null)return $"가장 빨리 전원을 잡은 마네킹: {fastest.Mannequin} ({Duration(fastest.AllCaughtSeconds!.Value)})";
        var most=finished.OrderByDescending(r=>r.Catches).First();
        return most.Catches>0?$"아직 아무도 전원을 잡지 못했습니다 · 가장 많이 잡은 마네킹: {most.Mannequin} ({most.Catches}번)":"아직 아무도 전원을 잡지 못했습니다 · 생존자들이 버텼습니다";
    }

    /// <summary>The room's current series for the room screen; null when the rule is off or nothing was played yet.</summary>
    internal static async Task<RoleSwapProgress?> ProgressAsync(MySqlConnection c,MySqlTransaction? tx,string room,CancellationToken ct)
    {
        var rules=await CustomRoomRules.RoomAsync(c,tx,room,ct);if(!rules.Enabled||!rules.RoleSwap)return null;
        var players=await PlayersAsync(c,tx,room,ct);
        var last=await CommunityEndpoints.Rows(c,tx,"SELECT series FROM room_role_swap_rounds WHERE room_id=@r ORDER BY series DESC LIMIT 1",ct,("r",room));
        var played=new List<RoleSwapRound>();var done=new HashSet<ulong>();
        if(last.Count>0)
        {
            var rows=await CommunityEndpoints.Rows(c,tx,"""
                SELECT s.mannequin_user_id,u.display_name,v.success,v.capture_count,v.all_captured_seconds,(v.match_id IS NOT NULL) AS finished
                FROM room_role_swap_rounds s JOIN users u ON u.id=s.mannequin_user_id
                LEFT JOIN verified_match_results v ON v.match_id=s.match_id AND v.user_id=s.mannequin_user_id
                WHERE s.room_id=@r AND s.series=@s ORDER BY s.round_no
                """,ct,("r",room),("s",Convert.ToInt32(last[0]["series"])));
            foreach(var r in rows)
            {
                bool finished=Convert.ToBoolean(r["finished"]);done.Add(Convert.ToUInt64(r["mannequin_user_id"]));
                played.Add(new RoleSwapRound(Convert.ToString(r["display_name"])!,finished,finished&&Convert.ToBoolean(r["success"]),
                    r["all_captured_seconds"] is null or DBNull?null:Convert.ToDouble(r["all_captured_seconds"]),r["capture_count"] is null or DBNull?0:Convert.ToInt32(r["capture_count"])));
            }
        }
        // A finished series (or other players) starts over at the next match: show the new series as empty.
        bool fresh=played.Count>=Rounds||(played.Count>0&&!(await CommunityEndpoints.Rows(c,tx,"SELECT members_key FROM room_role_swap_rounds WHERE room_id=@r ORDER BY series DESC,round_no DESC LIMIT 1",ct,("r",room)))
            .Select(x=>Convert.ToString(x["members_key"])).Contains(MembersKey(players)));
        string next="";
        if(players.Count==3)
        {
            ulong user=NextMannequin(players,fresh?new HashSet<ulong>():done);
            var name=await CommunityEndpoints.Rows(c,tx,"SELECT display_name FROM users WHERE id=@u",ct,("u",user));
            next=name.Count==0?"":Convert.ToString(name[0]["display_name"])!;
        }
        return new RoleSwapProgress(played.Count,Rounds,played,next,BestLine(played));
    }
}

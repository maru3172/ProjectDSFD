using MySqlConnector;

// Role-specific, server-result-only Elo. No client-reported wins or difficulty changes in a match.
internal static class BotSkillRatings
{
    internal static int Level(double rating)=>rating<850?1:rating<1100?2:rating<1350?3:rating<1650?4:5;
    internal static double BotRating(int level)=>level switch{1=>700,2=>975,3=>1225,4=>1500,_=>1800};
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS bot_skill_ratings(
          user_id BIGINT UNSIGNED NOT NULL,role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          rating DOUBLE NOT NULL DEFAULT 1200,games INT UNSIGNED NOT NULL DEFAULT 0,wins INT UNSIGNED NOT NULL DEFAULT 0,
          PRIMARY KEY(user_id,role),FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS bot_rating_receipts(
          match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,user_id BIGINT UNSIGNED NOT NULL,
          role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          own_rating DOUBLE NOT NULL,opponent_rating DOUBLE NOT NULL,applied BOOLEAN NOT NULL DEFAULT FALSE,
          PRIMARY KEY(match_id,user_id));
        """,ct);
    internal static async Task<double> ReadAsync(MySqlConnection c,MySqlTransaction tx,ulong user,string role,CancellationToken ct)
    {
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT rating FROM bot_skill_ratings WHERE user_id=@u AND role=@role",ct,("u",user),("role",role));
        return rows.Count==0?1200:Math.Clamp(Convert.ToDouble(rows[0]["rating"]),400,2400);
    }
    internal static async Task CaptureAsync(MySqlConnection c,MySqlTransaction tx,string match,CancellationToken ct)
    {
        if(await CustomRoomRules.IsCustomAsync(c,tx,match,ct))return;
        var humans=await CommunityEndpoints.Rows(c,tx,"SELECT user_id,role FROM authoritative_match_players WHERE match_id=@m",ct,("m",match));
        var bots=await LobbyEndpoints.LoadMatchBotsAsync(c,tx,match,ct);
        List<(ulong User,string Role,double Rating)> players=[];
        foreach(var row in humans)
        {
            ulong user=Convert.ToUInt64(row["user_id"]);string role=Convert.ToString(row["role"])!;
            await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO bot_skill_ratings(user_id,role) VALUES(@u,@r)",ct,("u",user),("r",role));
            players.Add((user,role,await ReadAsync(c,tx,user,role,ct)));
        }
        foreach(var player in players)
        {
            var opponents=players.Where(p=>p.Role!=player.Role).Select(p=>p.Rating).Concat(bots.Where(b=>b.AssignedRole!=player.Role).Select(b=>BotRating(b.Difficulty))).ToArray();
            if(opponents.Length==0)continue;
            await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO bot_rating_receipts(match_id,user_id,role,own_rating,opponent_rating) VALUES(@m,@u,@r,@own,@opponent)",ct,
                ("m",match),("u",player.User),("r",player.Role),("own",player.Rating),("opponent",opponents.Average()));
        }
    }
    internal static async Task ApplyAsync(MySqlConnection c,MySqlTransaction tx,string match,ulong user,bool won,CancellationToken ct)
    {
        if(await CustomRoomRules.IsCustomAsync(c,tx,match,ct))return;
        var rows=await CommunityEndpoints.Rows(c,tx,"SELECT role,own_rating,opponent_rating FROM bot_rating_receipts WHERE match_id=@m AND user_id=@u AND applied=FALSE FOR UPDATE",ct,("m",match),("u",user));
        if(rows.Count==0)return;
        var row=rows[0];double expected=1/(1+Math.Pow(10,(Convert.ToDouble(row["opponent_rating"])-Convert.ToDouble(row["own_rating"]))/400));
        double delta=24*((won?1:0)-expected);
        await CommunityEndpoints.Exec(c,tx,"UPDATE bot_skill_ratings SET rating=LEAST(2400,GREATEST(400,rating+@delta)),games=games+1,wins=wins+@win WHERE user_id=@u AND role=@r;UPDATE bot_rating_receipts SET applied=TRUE WHERE match_id=@m AND user_id=@u",ct,
            ("delta",delta),("win",won?1:0),("u",user),("r",row["role"]),("m",match));
    }
}

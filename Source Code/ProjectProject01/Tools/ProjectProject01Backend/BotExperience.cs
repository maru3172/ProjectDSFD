using System.Text.RegularExpressions;
using MySqlConnector;

// Bot growth across matches: one pool per role and level (self-made combos and self-tuned plays per
// play style). The game server produces the payload and caps it to the level; here it is only
// shape-checked, bound to a bot that really played that role and level in the match, and applied
// once per match. A level's pool never feeds another level.
internal sealed record BotExperienceUpload(string? Role,int Difficulty,string? Payload);

internal static class BotExperience
{
    private static readonly Regex Shape=new(
        @"^v1(\|[0-7];(\d{1,2}-\d{1,2}-\d{1,2}(,\d{1,2}-\d{1,2}-\d{1,2})*)?;(\d{1,2}-\d{2,3}-\d{2,3}(,\d{1,2}-\d{2,3}-\d{2,3})*)?)*$",
        RegexOptions.CultureInvariant,TimeSpan.FromMilliseconds(50));

    internal static bool IsValid(string? payload)=>payload is { Length: <=2048 }&&Shape.IsMatch(payload);

    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS bot_experience(
          role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          difficulty TINYINT UNSIGNED NOT NULL,
          payload VARCHAR(2048) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          matches INT UNSIGNED NOT NULL DEFAULT 0,
          updated_at_utc DATETIME(6) NOT NULL,
          PRIMARY KEY(role,difficulty));
        CREATE TABLE IF NOT EXISTS bot_experience_receipts(
          match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          difficulty TINYINT UNSIGNED NOT NULL,
          PRIMARY KEY(match_id,role,difficulty));
        """,ct);

    // Stored pools for the roles and levels in this roster, keyed "Role:Level".
    internal static async Task<Dictionary<string,string>> LoadAsync(MySqlConnection c,MySqlTransaction? tx,IEnumerable<MatchBot> bots,CancellationToken ct)
    {
        var pools=new Dictionary<string,string>(StringComparer.Ordinal);
        foreach(var bot in bots)
        {
            string key=$"{bot.AssignedRole}:{bot.Difficulty}";if(pools.ContainsKey(key))continue;
            var rows=await CommunityEndpoints.Rows(c,tx,"SELECT payload FROM bot_experience WHERE role=@r AND difficulty=@d",ct,("r",bot.AssignedRole),("d",bot.Difficulty));
            if(rows.Count==1&&Convert.ToString(rows[0]["payload"]) is { } payload&&IsValid(payload))pools[key]=payload;
        }
        return pools;
    }

    internal static async Task ApplyAsync(MySqlConnection c,MySqlTransaction tx,string match,IReadOnlyList<BotExperienceUpload>? uploads,CancellationToken ct)
    {
        if(uploads is null||uploads.Count==0)return;
        if(await CustomRoomRules.IsCustomAsync(c,tx,match,ct))return;
        var bots=await LobbyEndpoints.LoadMatchBotsAsync(c,tx,match,ct);
        foreach(var upload in uploads.Take(3))
        {
            if(upload.Role is not ("Mannequin" or "Survivor")||upload.Difficulty is <1 or >5||!IsValid(upload.Payload))continue;
            if(!bots.Any(b=>b.AssignedRole==upload.Role&&b.Difficulty==upload.Difficulty))continue;
            // Once per match and pool: outbox retries and duplicate reports change nothing.
            int receipt=await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO bot_experience_receipts(match_id,role,difficulty) VALUES(@m,@r,@d)",ct,
                ("m",match),("r",upload.Role),("d",upload.Difficulty));
            if(receipt!=1)continue;
            await CommunityEndpoints.Exec(c,tx,"""
                INSERT INTO bot_experience(role,difficulty,payload,matches,updated_at_utc) VALUES(@r,@d,@p,1,UTC_TIMESTAMP(6))
                ON DUPLICATE KEY UPDATE payload=VALUES(payload),matches=matches+1,updated_at_utc=UTC_TIMESTAMP(6)
                """,ct,("r",upload.Role),("d",upload.Difficulty),("p",upload.Payload));
        }
    }
}

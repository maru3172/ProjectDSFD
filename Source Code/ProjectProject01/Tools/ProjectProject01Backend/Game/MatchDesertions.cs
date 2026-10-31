using MySqlConnector;

// Desertions: a player who left a match while still in play and never came back (the game server reports it).
// Kept per match and player so the client can show the desertion result once, the next time that player is in.
internal static class MatchDesertions
{
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS match_desertions(
          match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          user_id BIGINT UNSIGNED NOT NULL,
          role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          deserted_at_utc DATETIME(6) NOT NULL,
          seen_at_utc DATETIME(6) NULL,
          PRIMARY KEY(match_id,user_id),
          KEY ix_match_desertions_unseen(user_id,seen_at_utc),
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);

    // Only a player the match really had; the result counts as a loss for the bot-match rating as well.
    internal static async Task RecordAsync(MySqlConnection c,MySqlTransaction tx,string match,ulong user,string role,CancellationToken ct)
    {
        var member=await CommunityEndpoints.Rows(c,tx,"SELECT 1 FROM authoritative_match_players WHERE match_id=@m AND user_id=@u LIMIT 1",ct,("m",match),("u",user));
        if(member.Count==0)return;
        await CommunityEndpoints.Exec(c,tx,"INSERT IGNORE INTO match_desertions(match_id,user_id,role,deserted_at_utc) VALUES(@m,@u,@r,UTC_TIMESTAMP(6))",ct,("m",match),("u",user),("r",role));
        await BotSkillRatings.ApplyAsync(c,tx,match,user,false,ct);
    }

    internal static void Map(WebApplication app)
    {
        // The oldest desertion this player has not seen yet (204 when there is none).
        app.MapGet("/api/matches/desertions/pending",async(HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            try
            {
                await using var c=await db.OpenConnectionAsync(ct);
                var rows=await CommunityEndpoints.Rows(c,null,"SELECT match_id,role,deserted_at_utc FROM match_desertions WHERE user_id=@u AND seen_at_utc IS NULL ORDER BY deserted_at_utc LIMIT 1",ct,("u",user.Id));
                if(rows.Count==0)return Results.NoContent();
                return Results.Ok(new{matchId=Convert.ToString(rows[0]["match_id"]),role=Convert.ToString(rows[0]["role"]),desertedAtUtc=(DateTime)rows[0]["deserted_at_utc"]!});
            }
            catch(MySqlException){return Results.StatusCode(StatusCodes.Status503ServiceUnavailable);}
        }).RequireRateLimiting("lobby");
        app.MapPost("/api/matches/desertions/{matchId}/seen",async(string matchId,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            if(!Guid.TryParse(matchId,out var match))return Results.BadRequest();
            try
            {
                await using var c=await db.OpenConnectionAsync(ct);
                await CommunityEndpoints.Exec(c,null,"UPDATE match_desertions SET seen_at_utc=UTC_TIMESTAMP(6) WHERE match_id=@m AND user_id=@u AND seen_at_utc IS NULL",ct,("m",match.ToString("D")),("u",user.Id));
                return Results.Ok(new{message="기권 결과를 확인했습니다."});
            }
            catch(MySqlException){return Results.StatusCode(StatusCodes.Status503ServiceUnavailable);}
        }).RequireRateLimiting("lobby");
    }
}

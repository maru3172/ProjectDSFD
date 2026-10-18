using MySqlConnector;

internal static class VoiceEndpoints
{
    public static void MapProjectProject01Voice(this WebApplication app)
    {
        app.MapPost("/api/voice/token", async (
            HttpRequest request,
            VoiceTokenRequest body,
            AuthDatabase database,
            EosVoiceTokenService eosVoice,
            VoiceParticipantDirectory directory,
            VoiceAccountRateLimit accountLimits,
            CancellationToken cancellationToken) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request, database, cancellationToken);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            if(!accountLimits.TryAcquire(user.Id,out var retryAfter))
            {
                request.HttpContext.Response.Headers.RetryAfter=retryAfter.ToString(System.Globalization.CultureInfo.InvariantCulture);
                return Results.StatusCode(429);
            }
            if (!Guid.TryParse(body.MatchId, out var matchId) ||
                !EosVoiceTokenService.IsProductUserIdText(body.ProductUserId))
            {
                return Results.BadRequest(new VoiceTokenFailure("매치 ID 또는 EOS Product User ID 형식이 올바르지 않습니다."));
            }
            if (!eosVoice.IsReady)
            {
                return Results.Json(
                    new VoiceTokenFailure("EOS 음성 서버 자격 증명이 아직 구성되지 않았습니다."),
                    statusCode: StatusCodes.Status503ServiceUnavailable);
            }

            try
            {
                await using var connection = await database.OpenConnectionAsync(cancellationToken);
                await using var membershipCommand = new MySqlCommand(
                    """
                    SELECT 1
                    FROM game_rooms r
                    INNER JOIN room_members m ON m.room_id = r.id
                    WHERE r.match_id = @matchId
                      AND r.status = 'Started'
                      AND m.user_id = @userId
                      AND m.assigned_role = 'Survivor'
                    LIMIT 1;
                    """, connection) { CommandTimeout = 5 };
                membershipCommand.Parameters.AddWithValue("@matchId", matchId.ToString("D"));
                membershipCommand.Parameters.AddWithValue("@userId", user.Id);
                if (await membershipCommand.ExecuteScalarAsync(cancellationToken) is null)
                {
                    await SecurityAudit.WriteAsync(database, request, "VoiceToken", "RejectedMembership",
                        user.Id, $"MatchId={matchId:D}", cancellationToken);
                    return Results.Json(new VoiceTokenFailure("현재 경기의 생존자만 음성을 사용할 수 있습니다."),statusCode:403);
                }

                var channelName = $"pp01-survivors-{matchId:N}";
                if(!await eosVoice.VerifyIdentityAsync(body.ProductUserId,body.ProductUserIdToken,cancellationToken))
                    return Results.Json(new VoiceTokenFailure("EOS 사용자 증명을 확인하지 못했습니다. 최신 클라이언트에서 음성을 다시 연결하세요."),statusCode:403);
                var token = await eosVoice.CreateRoomTokenAsync(
                    channelName,
                    body.ProductUserId.Trim(),
                    RequestSecurity.GetClientAddress(request.HttpContext),
                    cancellationToken);
                var currentSurvivors=await ReadSurvivors(connection,matchId,cancellationToken);
                if (!currentSurvivors.ContainsKey(user.Id)) return Results.Json(new VoiceTokenFailure("현재 경기의 생존자만 음성을 사용할 수 있습니다."),statusCode:403);
                if (!directory.TryBind(matchId, user.Id, body.ProductUserId, user.DisplayName))
                    return Results.Conflict(new VoiceTokenFailure("음성 참가자 식별자가 충돌하거나 현재 등록 한도를 초과했습니다."));
                await SecurityAudit.WriteAsync(database, request, "VoiceToken", "Issued",
                    user.Id, $"MatchId={matchId:D}", cancellationToken);
                return Results.Ok(new VoiceTokenResponse(
                    channelName, token.ClientBaseUrl, token.ParticipantToken, body.ProductUserId.Trim(),
                    directory.Read(matchId,user.Id,currentSurvivors)));
            }
            catch (MySqlException)
            {
                return Results.Json(new VoiceTokenFailure("데이터베이스를 사용할 수 없습니다."), statusCode: 503);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return Results.StatusCode(499);
            }
            catch (Exception exception)
            {
                app.Logger.LogError(exception, "EOS voice room token issuance failed for match {MatchId}.", matchId);
                await SecurityAudit.WriteAsync(database, request, "VoiceToken", "Failed",
                    user.Id, $"MatchId={matchId:D}; Error={exception.GetType().Name}", cancellationToken);
                return Results.Json(new VoiceTokenFailure("EOS 음성 방 토큰 발급에 실패했습니다."), statusCode: 503);
            }
        }).RequireRateLimiting("voice-token");

        app.MapGet("/api/voice/participants", async (HttpRequest request, string matchId,
            AuthDatabase database, VoiceParticipantDirectory directory, CancellationToken cancellationToken) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request,database,cancellationToken);
            if (user is null) return Results.Unauthorized();
            if (!Guid.TryParse(matchId,out var match)) return Results.BadRequest();
            try
            {
                await using var connection=await database.OpenConnectionAsync(cancellationToken);
                var survivors=await ReadSurvivors(connection,match,cancellationToken);
                if (!survivors.ContainsKey(user.Id)) return Results.Json(new VoiceTokenFailure("현재 경기의 생존자만 음성을 사용할 수 있습니다."),statusCode:403);
                return Results.Ok(directory.Read(match,user.Id,survivors));
            }
            catch (MySqlException) { return Results.StatusCode(503); }
        }).RequireRateLimiting("voice-roster");
    }

    private static async Task<Dictionary<ulong,string>> ReadSurvivors(MySqlConnection connection,Guid match,
        CancellationToken cancellationToken)
    {
        await using var command=new MySqlCommand("""
            SELECT m.user_id,u.display_name FROM game_rooms r
            INNER JOIN room_members m ON m.room_id=r.id INNER JOIN users u ON u.id=m.user_id
            WHERE r.match_id=@matchId AND r.status='Started' AND m.assigned_role='Survivor';
            """,connection) { CommandTimeout=5 };
        command.Parameters.AddWithValue("@matchId",match.ToString("D"));
        await using var reader=await command.ExecuteReaderAsync(cancellationToken);
        Dictionary<ulong,string> result=[];
        while(await reader.ReadAsync(cancellationToken))result[reader.GetUInt64(0)]=reader.GetString(1);
        return result;
    }
}

internal sealed record VoiceTokenRequest(string MatchId, string ProductUserId, string? ProductUserIdToken);
internal sealed record VoiceTokenResponse(
    string ChannelName,
    string ClientBaseUrl,
    string ParticipantToken,
    string ProductUserId,
    VoiceParticipantRoster Roster);
internal sealed record VoiceTokenFailure(string Message);

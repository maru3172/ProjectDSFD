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
            CancellationToken cancellationToken) =>
        {
            var user = await LobbyEndpoints.AuthenticateAsync(request, database, cancellationToken);
            if (user is null)
            {
                return Results.Unauthorized();
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
                    return Results.Forbid();
                }

                var channelName = $"pp01-survivors-{matchId:N}";
                var token = await eosVoice.CreateRoomTokenAsync(
                    channelName,
                    body.ProductUserId.Trim(),
                    RequestSecurity.GetClientAddress(request.HttpContext),
                    cancellationToken);
                await SecurityAudit.WriteAsync(database, request, "VoiceToken", "Issued",
                    user.Id, $"MatchId={matchId:D}", cancellationToken);
                return Results.Ok(new VoiceTokenResponse(
                    channelName, token.ClientBaseUrl, token.ParticipantToken, body.ProductUserId.Trim()));
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
    }
}

internal sealed record VoiceTokenRequest(string MatchId, string ProductUserId);
internal sealed record VoiceTokenResponse(
    string ChannelName,
    string ClientBaseUrl,
    string ParticipantToken,
    string ProductUserId);
internal sealed record VoiceTokenFailure(string Message);

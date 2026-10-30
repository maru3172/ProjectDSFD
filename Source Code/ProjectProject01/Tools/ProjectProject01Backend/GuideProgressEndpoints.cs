using MySqlConnector;

internal static class GuideProgressEndpoints
{
    internal static void MapGuideProgress(this WebApplication app)
    {
        // 역할 안내 확인 여부는 설치 PC가 아니라 로그인 계정에 귀속한다. 클라이언트는 안내를
        // 실제로 띄운 직후 해당 역할만 기록하며, 다른 역할의 최초 안내 상태는 변경하지 않는다.
        app.MapPost("/api/auth/guide-progress", async (
            HttpRequest request,
            GuideProgressRequest body,
            AuthDatabase database,
            CancellationToken cancellationToken) =>
        {
            var normalizedRole = body.Role?.Trim();
            if (!string.Equals(normalizedRole, "Survivor", StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(normalizedRole, "Mannequin", StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(normalizedRole, "InitialTutorial", StringComparison.OrdinalIgnoreCase))
            {
                return Results.BadRequest(new AuthFailure("역할 안내 종류가 올바르지 않습니다."));
            }
            try
            {
                var user = await LobbyEndpoints.AuthenticateAsync(request, database, cancellationToken);
                if (user is null)
                {
                    return Results.Unauthorized();
                }
                await using var connection = await database.OpenConnectionAsync(cancellationToken);
                var column = string.Equals(normalizedRole, "InitialTutorial", StringComparison.OrdinalIgnoreCase)
                    ? "seen_initial_tutorial"
                    : string.Equals(normalizedRole, "Mannequin", StringComparison.OrdinalIgnoreCase)
                    ? "seen_mannequin_guide"
                    : "seen_survivor_guide";
                await using var update = new MySqlCommand(
                    $"UPDATE users SET {column} = TRUE WHERE id = @userId;", connection)
                {
                    CommandTimeout = 5
                };
                update.Parameters.AddWithValue("@userId", user.Id);
                await update.ExecuteNonQueryAsync(cancellationToken);
                return Results.Ok(new
                {
                    message = "역할 안내 확인 상태를 저장했습니다.",
                    role = normalizedRole
                });
            }
            catch (MySqlException)
            {
                return Results.Json(new AuthFailure("인증 데이터베이스를 사용할 수 없습니다."), statusCode:503);
            }
        }).RequireRateLimiting("lobby");
    }
}

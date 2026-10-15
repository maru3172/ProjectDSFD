using MySqlConnector;

internal sealed record RoomCapacityRequest(int MaximumRooms);
internal static class RoomCapacity
{
    internal const int Limit = 100;
    public static void MapRoomCapacity(this WebApplication app)
    {
        app.MapGet("/api/admin/room-capacity", async (HttpRequest request, AuthDatabase database, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            await using var connection = await database.OpenConnectionAsync(ct);
            await using var command = new MySqlCommand("SELECT max_rooms,(SELECT COUNT(*) FROM game_rooms) FROM lobby_state WHERE id=1;", connection);
            await using var reader = await command.ExecuteReaderAsync(ct);
            await reader.ReadAsync(ct);
            return Results.Ok(new { maximumRooms=reader.GetInt32(0), currentRooms=reader.GetInt32(1), limit=Limit });
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/room-capacity", async (HttpRequest request, RoomCapacityRequest body, AuthDatabase database, AdministratorOptions admin, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            if (body.MaximumRooms is <1 or >Limit) return Results.BadRequest(new { message=$"최대 방 수는 1~{Limit}개로 지정하세요." });
            await using var connection = await database.OpenConnectionAsync(ct);
            // UPDATE takes the same row lock as room creation; lowering the cap preserves existing rooms.
            await using var command = new MySqlCommand("UPDATE lobby_state SET max_rooms=@max WHERE id=1;", connection);
            command.Parameters.AddWithValue("@max", body.MaximumRooms);
            await command.ExecuteNonQueryAsync(ct);
            await SecurityAudit.WriteAsync(database,request,"RoomCapacity","Updated",null,$"MaximumRooms={body.MaximumRooms}",ct);
            return Results.Ok(new {maximumRooms=body.MaximumRooms, message="최대 방 수를 저장했습니다. 기존 방은 유지하며 새 방 생성부터 적용합니다."});
        }).RequireRateLimiting("admin");
    }
}

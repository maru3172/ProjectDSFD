using MySqlConnector;

internal sealed record RestrictAccountRequest(string Reason, int? DurationMinutes);
internal static class AccountRestrictions
{
    internal static async Task<string?> GetMessageAsync(MySqlConnection connection, MySqlTransaction? transaction, ulong userId, CancellationToken ct)
    {
        await using var command = new MySqlCommand("""
            SELECT reason, expires_at_utc FROM account_restrictions
            WHERE user_id=@user AND lifted_at_utc IS NULL
              AND (expires_at_utc IS NULL OR expires_at_utc>UTC_TIMESTAMP(6))
            ORDER BY id DESC LIMIT 1;
            """, connection, transaction);
        command.Parameters.AddWithValue("@user",userId);
        await using var reader=await command.ExecuteReaderAsync(ct);
        if(!await reader.ReadAsync(ct)) return null;
        return reader.IsDBNull(1) ? $"영구 이용 제한된 계정입니다. 사유: {reader.GetString(0)}"
            : $"이용 제한된 계정입니다. 해제 시각(UTC): {reader.GetDateTime(1):yyyy-MM-dd HH:mm:ss}. 사유: {reader.GetString(0)}";
    }

    public static void MapAccountRestrictions(this WebApplication app)
    {
        app.MapGet("/api/admin/account-restrictions",async(HttpRequest request,AuthDatabase database,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            await using var connection=await database.OpenConnectionAsync(ct);
            await using var command=new MySqlCommand("""
                SELECT r.id,u.account_id,r.reason,r.created_at_utc,r.expires_at_utc,r.lifted_at_utc
                FROM account_restrictions r JOIN users u ON u.id=r.user_id
                ORDER BY r.id DESC LIMIT 500;
                """,connection);
            var rows=new List<object>();
            await using var reader=await command.ExecuteReaderAsync(ct);
            while(await reader.ReadAsync(ct)) rows.Add(new {id=reader.GetUInt64(0),accountId=reader.GetString(1),reason=reader.GetString(2),
                createdAtUtc=DateTime.SpecifyKind(reader.GetDateTime(3),DateTimeKind.Utc),
                expiresAtUtc=reader.IsDBNull(4)?(DateTime?)null:DateTime.SpecifyKind(reader.GetDateTime(4),DateTimeKind.Utc),
                liftedAtUtc=reader.IsDBNull(5)?(DateTime?)null:DateTime.SpecifyKind(reader.GetDateTime(5),DateTimeKind.Utc)});
            return Results.Ok(new {restrictions=rows});
        }).RequireRateLimiting("admin");

        app.MapPost("/api/admin/accounts/{accountId}/restriction",async(string accountId,RestrictAccountRequest body,HttpRequest request,AuthDatabase database,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            if(string.IsNullOrWhiteSpace(body.Reason)||body.Reason.Length>512||body.DurationMinutes is <1 or >525600)
                return Results.BadRequest(new {message="사유(1~512자)와 제한 기간(1분~1년 또는 영구)을 확인하세요."});
            await using var connection=await database.OpenConnectionAsync(ct);
            await using var transaction=await connection.BeginTransactionAsync(ct);
            await using var find=new MySqlCommand("SELECT id FROM users WHERE account_id=@account FOR UPDATE;",connection,transaction);
            find.Parameters.AddWithValue("@account",accountId.Trim());
            object? value=await find.ExecuteScalarAsync(ct);
            if(value is null or DBNull) return Results.NotFound(new {message="계정을 찾지 못했습니다."});
            ulong userId=Convert.ToUInt64(value);
            DateTime? expires=body.DurationMinutes is int minutes?DateTime.UtcNow.AddMinutes(minutes):null;
            await using var command=new MySqlCommand("""
                UPDATE account_restrictions SET lifted_at_utc=UTC_TIMESTAMP(6) WHERE user_id=@user AND lifted_at_utc IS NULL;
                INSERT INTO account_restrictions(user_id,reason,expires_at_utc) VALUES(@user,@reason,@expires);
                UPDATE auth_sessions SET revoked_at_utc=UTC_TIMESTAMP(6) WHERE user_id=@user AND revoked_at_utc IS NULL;
                DELETE FROM game_join_tickets WHERE user_id=@user;
                """,connection,transaction);
            command.Parameters.AddWithValue("@user",userId);command.Parameters.AddWithValue("@reason",body.Reason.Trim());
            command.Parameters.AddWithValue("@expires",(object?)expires??DBNull.Value);
            await command.ExecuteNonQueryAsync(ct);
            await LobbyEndpoints.RemoveUserFromCurrentRoomAsync(connection,transaction,userId,ct);
            await OpenParties.LeaveAsync(connection,transaction,userId,ct);
            await transaction.CommitAsync(ct);
            await SecurityAudit.WriteAsync(database,request,"AccountRestriction","Applied",userId,$"DurationMinutes={body.DurationMinutes}",ct);
            return Results.Ok(new {message="계정을 이용 제한하고 기존 로그인 세션을 종료했습니다.",expiresAtUtc=expires});
        }).RequireRateLimiting("admin");

        app.MapDelete("/api/admin/accounts/{accountId}/restriction",async(string accountId,HttpRequest request,AuthDatabase database,AdministratorOptions admin,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            await using var connection=await database.OpenConnectionAsync(ct);
            await using var command=new MySqlCommand("""
                UPDATE account_restrictions r JOIN users u ON u.id=r.user_id SET r.lifted_at_utc=UTC_TIMESTAMP(6)
                WHERE u.account_id=@account AND r.lifted_at_utc IS NULL;
                """,connection);
            command.Parameters.AddWithValue("@account",accountId.Trim());
            int count=await command.ExecuteNonQueryAsync(ct);
            await SecurityAudit.WriteAsync(database,request,"AccountRestriction","Lifted",null,$"AccountId={accountId.Trim()}",ct);
            return Results.Ok(new {message="이용 제한을 해제했습니다. 다시 로그인할 수 있습니다.",count});
        }).RequireRateLimiting("admin");
    }
}

using MySqlConnector;

internal static class LoginFailurePolicy
{
    internal const int MaximumFailedLoginCount=5;
    internal const int AccountLockMinutes=15;

    internal static async Task RecordAsync(MySqlConnection connection,ulong userId,CancellationToken ct)
    {
        await using var command=new MySqlCommand("""
            UPDATE users
            SET locked_until_utc = CASE WHEN failed_login_count + 1 >= @maximum THEN @lockedUntilUtc ELSE locked_until_utc END,
                failed_login_count = LEAST(failed_login_count + 1, 4294967295)
            WHERE id = @userId;
            """,connection){CommandTimeout=5};
        command.Parameters.AddWithValue("@maximum",MaximumFailedLoginCount);
        command.Parameters.AddWithValue("@lockedUntilUtc",DateTime.UtcNow.AddMinutes(AccountLockMinutes));
        command.Parameters.AddWithValue("@userId",userId);
        await command.ExecuteNonQueryAsync(ct);
    }
}

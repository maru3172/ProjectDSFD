using Microsoft.AspNetCore.Identity;
using MySqlConnector;

internal static class RegistrationEndpoint
{
    internal static void MapRegistration(this WebApplication app,
        Func<MySqlConnection,MySqlTransaction,ulong,CancellationToken,Task<TokenPair>> issueSession)
    {
        app.MapPost("/api/auth/register",async (HttpRequest httpRequest,RegisterRequest request,AuthDatabase database,
            IPasswordHasher<AuthUser> passwordHasher,CancellationToken ct)=>
        {
            string displayName=request.DisplayName?.Trim()??string.Empty;
            string? error=AuthValidation.Validate(request.AccountId,request.Password,request.DisplayName)
                ??(displayName.Length is <2 or >32?"표시 이름은 2~32자로 입력하세요.":null);
            if(error is not null)
            {
                await SecurityAudit.WriteAsync(database,httpRequest,"AuthRegister","RejectedInput",null,error,ct);
                return Results.BadRequest(new AuthFailure(error));
            }
            if(!database.IsConfigured)return Results.Json(new AuthFailure("인증 데이터베이스를 사용할 수 없습니다."),statusCode:503);
            string accountId=request.AccountId.Trim();
            var user=new AuthUser(0,accountId,displayName,string.Empty,0,null,false,false);
            string passwordHash=passwordHasher.HashPassword(user,request.Password);
            try
            {
                await using var connection=await database.OpenConnectionAsync(ct);
                await using var transaction=await connection.BeginTransactionAsync(ct);
                await using var insert=new MySqlCommand("INSERT INTO users(account_id,display_name,password_hash) VALUES(@account,@name,@hash);",connection,transaction){CommandTimeout=5};
                insert.Parameters.AddWithValue("@account",accountId);insert.Parameters.AddWithValue("@name",displayName);insert.Parameters.AddWithValue("@hash",passwordHash);
                await insert.ExecuteNonQueryAsync(ct);
                ulong userId=checked((ulong)insert.LastInsertedId);
                var tokens=await issueSession(connection,transaction,userId,ct);
                await transaction.CommitAsync(ct);
                await SecurityAudit.WriteAsync(database,httpRequest,"AuthRegister","Success",userId,null,ct);
                return Results.Created("/api/auth/me",new AuthSuccess("계정이 생성되었습니다.",displayName,tokens.AccessToken,tokens.RefreshToken,
                    tokens.AccessExpiresAtUtc,tokens.RefreshExpiresAtUtc,false,false));
            }
            catch(MySqlException ex)when(ex.Number==1062)
            {
                await SecurityAudit.WriteAsync(database,httpRequest,"AuthRegister","DuplicateAccount",null,null,ct);
                return Results.Conflict(new AuthFailure("이미 사용 중인 계정 ID입니다."));
            }
            catch(MySqlException){return Results.Json(new AuthFailure("인증 데이터베이스를 사용할 수 없습니다."),statusCode:503);}
        }).RequireRateLimiting("auth");
    }
}

// File: Tools/ProjectProject01Backend/LobbyEndpoints.cs
// Target: .NET 9 / MySQL Server 8.0

using System.Security.Cryptography;
using System.Net;
using System.Text;
using Microsoft.AspNetCore.Identity;
using Microsoft.AspNetCore.WebUtilities;
using MySqlConnector;

internal static class LobbyEndpoints
{
    private const int MaximumRooms = 30;
    private const int MaximumPlayers = 3;
    private const int StaleMemberSeconds = 60;
    private const int GameTicketLifetimeSeconds = 60;

    public static void MapProjectProject01Lobby(
        this WebApplication app,
        string configuredTravelUrl,
        ProjectSecurityOptions securityOptions,
        VersionCompatibilityOptions versionCompatibility)
    {
        var travelUrl = string.IsNullOrWhiteSpace(configuredTravelUrl)
            ? "127.0.0.1:7777"
            : configuredTravelUrl.Trim();

        app.MapGet("/api/rooms", async (HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }

            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await CleanupStaleRoomsAsync(connection, null, ct);
                var rooms = new List<RoomSummary>();
                await using var command = new MySqlCommand(
                    """
                    SELECT r.id, r.join_code, r.name, u.display_name, COUNT(m.user_id) AS member_count,
                           r.password_hash IS NOT NULL AS has_password
                    FROM game_rooms r
                    INNER JOIN users u ON u.id = r.host_user_id
                    INNER JOIN room_members m ON m.room_id = r.id
                    WHERE r.status = 'Waiting' AND r.is_public = TRUE
                    GROUP BY r.id, r.name, u.display_name, r.created_at_utc
                    HAVING COUNT(m.user_id) < 3
                    ORDER BY r.created_at_utc ASC
                    LIMIT 30;
                    """, connection) { CommandTimeout = 5 };
                await using var reader = await command.ExecuteReaderAsync(ct);
                while (await reader.ReadAsync(ct))
                {
                    rooms.Add(new RoomSummary(
                        ReadRoomId(reader, 0), reader.GetString(1), reader.GetString(2), reader.GetString(3),
                        reader.GetInt32(4), MaximumPlayers, reader.GetBoolean(5)));
                }
                return Results.Ok(new { message = "열린 방 목록을 불러왔습니다.", rooms });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapGet("/api/rooms/current", async (HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await TouchMemberAsync(connection, null, user.Id, ct);
                await CleanupStaleRoomsAsync(connection, null, ct);
                var room = await LoadCurrentRoomAsync(connection, null, user.Id, ct);
                return room is null
                    ? Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."))
                    : Results.Ok(new { message = "방 상태를 갱신했습니다.", room });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms", async (
            HttpRequest request,
            CreateRoomRequest body,
            AuthDatabase database,
            IPasswordHasher<RoomPasswordRecord> passwordHasher,
            CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            var name = body.Name?.Trim() ?? string.Empty;
            if (name.Length is < 1 or > 48)
            {
                return Results.BadRequest(new LobbyFailure("방 이름은 1~48자여야 합니다."));
            }
            var password = body.Password?.Trim() ?? string.Empty;
            if (password.Length != 0 && password.Length is < 4 or > 64)
            {
                return Results.BadRequest(new LobbyFailure("방 비밀번호는 사용하지 않거나 4~64자로 입력해야 합니다."));
            }

            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await using var transaction = await connection.BeginTransactionAsync(ct);
                await LockLobbyStateAsync(connection, transaction, ct);
                await CleanupStaleRoomsAsync(connection, transaction, ct);
                if (await FindCurrentRoomIdAsync(connection, transaction, user.Id, ct) is not null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("이미 다른 방에 참가 중입니다."));
                }
                if (await CountRoomsAsync(connection, transaction, ct) >= MaximumRooms)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("생성 가능한 방 30개가 모두 사용 중입니다."));
                }
                if (await WaitingRoomNameExistsAsync(connection, transaction, name, ct))
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("이미 사용 중인 방 이름입니다. 다른 이름을 입력하세요."));
                }

                var roomId = Guid.NewGuid().ToString("D");
                var joinCode = await GenerateUniqueJoinCodeAsync(connection, transaction, ct);
                var passwordHash = password.Length == 0
                    ? null
                    : passwordHasher.HashPassword(new RoomPasswordRecord(roomId), password);
                await using (var insertRoom = new MySqlCommand(
                    "INSERT INTO game_rooms (id, join_code, name, host_user_id, password_hash, is_public) VALUES (@roomId, @joinCode, @name, @userId, @passwordHash, @isPublic);",
                    connection, transaction) { CommandTimeout = 5 })
                {
                    insertRoom.Parameters.AddWithValue("@roomId", roomId);
                    insertRoom.Parameters.AddWithValue("@joinCode", joinCode);
                    insertRoom.Parameters.AddWithValue("@name", name);
                    insertRoom.Parameters.AddWithValue("@userId", user.Id);
                    insertRoom.Parameters.AddWithValue("@passwordHash", passwordHash is null ? DBNull.Value : passwordHash);
                    insertRoom.Parameters.AddWithValue("@isPublic", body.IsPublic);
                    await insertRoom.ExecuteNonQueryAsync(ct);
                }
                await using (var insertMember = new MySqlCommand(
                    "INSERT INTO room_members (room_id, user_id, is_ready) VALUES (@roomId, @userId, FALSE);",
                    connection, transaction) { CommandTimeout = 5 })
                {
                    insertMember.Parameters.AddWithValue("@roomId", roomId);
                    insertMember.Parameters.AddWithValue("@userId", user.Id);
                    await insertMember.ExecuteNonQueryAsync(ct);
                }
                await transaction.CommitAsync(ct);
                var room = await LoadCurrentRoomAsync(connection, null, user.Id, ct);
                return Results.Ok(new { message = "방을 생성했습니다.", room });
            }
            catch (MySqlException exception) when (exception.Number == 1062)
            {
                return Results.Conflict(new LobbyFailure("이미 다른 방에 참가 중입니다."));
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/{roomCode}/join", async (
            string roomCode,
            HttpRequest request,
            JoinRoomRequest body,
            AuthDatabase database,
            IPasswordHasher<RoomPasswordRecord> passwordHasher,
            CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            var normalizedRoomCode = roomCode.Trim().ToUpperInvariant();
            if (normalizedRoomCode.Length is < 6 or > 8 ||
                normalizedRoomCode.Any(character => !char.IsAsciiLetterOrDigit(character)))
            {
                return Results.BadRequest(new LobbyFailure("참가 코드는 영문과 숫자로 된 6~8자리여야 합니다."));
            }

            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await using var transaction = await connection.BeginTransactionAsync(ct);
                await CleanupStaleRoomsAsync(connection, transaction, ct);
                if (await FindCurrentRoomIdAsync(connection, transaction, user.Id, ct) is not null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("이미 다른 방에 참가 중입니다."));
                }
                string? resolvedRoomId = null;
                await using (var lockRoom = new MySqlCommand(
                    "SELECT id, status, password_hash FROM game_rooms WHERE join_code = @joinCode FOR UPDATE;", connection, transaction) { CommandTimeout = 5 })
                {
                    lockRoom.Parameters.AddWithValue("@joinCode", normalizedRoomCode);
                    string? status = null;
                    string? passwordHash = null;
                    await using (var reader = await lockRoom.ExecuteReaderAsync(ct))
                    {
                        if (await reader.ReadAsync(ct))
                        {
                            resolvedRoomId = ReadRoomId(reader, 0);
                            status = reader.GetString(1);
                            passwordHash = reader.IsDBNull(2) ? null : reader.GetString(2);
                        }
                    }
                    if (status is null)
                    {
                        await transaction.RollbackAsync(ct);
                        return Results.NotFound(new LobbyFailure("방을 찾지 못했습니다."));
                    }
                    if (!string.Equals(status, "Waiting", StringComparison.Ordinal))
                    {
                        await transaction.RollbackAsync(ct);
                        return Results.Conflict(new LobbyFailure("이미 시작된 방에는 참가할 수 없습니다."));
                    }
                    if (passwordHash is not null)
                    {
                        var suppliedPassword = body.Password?.Trim() ?? string.Empty;
                        var verification = passwordHasher.VerifyHashedPassword(
                            new RoomPasswordRecord(resolvedRoomId!), passwordHash, suppliedPassword);
                        if (verification == PasswordVerificationResult.Failed)
                        {
                            await transaction.RollbackAsync(ct);
                            return Results.Json(new LobbyFailure("방 비밀번호가 올바르지 않습니다."), statusCode: 401);
                        }
                    }
                }
                await using (var countMembers = new MySqlCommand(
                    "SELECT COUNT(*) FROM room_members WHERE room_id = @roomId;", connection, transaction) { CommandTimeout = 5 })
                {
                    countMembers.Parameters.AddWithValue("@roomId", resolvedRoomId!);
                    if (Convert.ToInt32(await countMembers.ExecuteScalarAsync(ct)) >= MaximumPlayers)
                    {
                        await transaction.RollbackAsync(ct);
                        return Results.Conflict(new LobbyFailure("방 인원이 가득 찼습니다."));
                    }
                }
                await using (var insert = new MySqlCommand(
                    "INSERT INTO room_members (room_id, user_id, is_ready) VALUES (@roomId, @userId, FALSE);",
                    connection, transaction) { CommandTimeout = 5 })
                {
                    insert.Parameters.AddWithValue("@roomId", resolvedRoomId!);
                    insert.Parameters.AddWithValue("@userId", user.Id);
                    await insert.ExecuteNonQueryAsync(ct);
                }
                await transaction.CommitAsync(ct);
                var room = await LoadCurrentRoomAsync(connection, null, user.Id, ct);
                return Results.Ok(new { message = "방에 참가했습니다.", room });
            }
            catch (MySqlException exception) when (exception.Number == 1062)
            {
                return Results.Conflict(new LobbyFailure("이미 다른 방에 참가 중입니다."));
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/current/leave", async (HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await using var transaction = await connection.BeginTransactionAsync(ct);
                var membership = await LoadMembershipAsync(connection, transaction, user.Id, true, ct);
                if (membership is null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                }
                var hostTransferred = false;
                var roomDeleted = false;
                if (membership.IsHost)
                {
                    var successorUserId = await FindEarliestOtherMemberAsync(
                        connection, transaction, membership.RoomId, user.Id, ct);
                    if (successorUserId is ulong successor)
                    {
                        await ExecuteAsync(connection, transaction,
                            "UPDATE game_rooms SET host_user_id = @successorUserId WHERE id = @roomId;",
                            ("@successorUserId", successor), ("@roomId", membership.RoomId), ct);
                        await ExecuteAsync(connection, transaction,
                            "UPDATE room_members SET is_ready = FALSE WHERE room_id = @roomId AND user_id = @successorUserId;",
                            ("@roomId", membership.RoomId), ("@successorUserId", successor), ct);
                        await ExecuteAsync(connection, transaction,
                            "DELETE FROM room_members WHERE room_id = @roomId AND user_id = @userId;",
                            ("@roomId", membership.RoomId), ("@userId", user.Id), ct);
                        hostTransferred = true;
                    }
                    else
                    {
                        await ExecuteAsync(connection, transaction,
                            "DELETE FROM game_rooms WHERE id = @roomId;", ("@roomId", membership.RoomId), ct);
                        roomDeleted = true;
                    }
                }
                else
                {
                    await ExecuteAsync(connection, transaction,
                        "DELETE FROM room_members WHERE room_id = @roomId AND user_id = @userId;",
                        ("@roomId", membership.RoomId), ("@userId", user.Id), ct);
                }
                if (!roomDeleted)
                {
                    await ResetStartedRoomIfAllMembersReturnedAsync(connection, transaction, membership.RoomId, ct);
                }
                await transaction.CommitAsync(ct);
                return Results.Ok(new
                {
                    message = hostTransferred
                        ? "방에서 나왔으며 가장 먼저 참가한 플레이어에게 방장 권한을 넘겼습니다."
                        : membership.IsHost
                            ? "마지막 인원이 나가 방이 삭제되었습니다."
                            : "방에서 나왔습니다."
                });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/current/return", async (HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await using var transaction = await connection.BeginTransactionAsync(ct);
                var membership = await LoadMembershipAsync(connection, transaction, user.Id, true, ct);
                if (membership is null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                }
                if (!string.Equals(membership.Status, "Started", StringComparison.Ordinal))
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("현재 진행 중인 경기가 없습니다."));
                }

                await ExecuteAsync(connection, transaction,
                    "UPDATE room_members SET returned_to_room = TRUE, is_ready = FALSE, last_seen_at_utc = UTC_TIMESTAMP(6) WHERE room_id = @roomId AND user_id = @userId;",
                    ("@roomId", membership.RoomId), ("@userId", user.Id), ct);
                var allReturned = await ResetStartedRoomIfAllMembersReturnedAsync(
                    connection, transaction, membership.RoomId, ct);
                await transaction.CommitAsync(ct);

                var room = await LoadCurrentRoomAsync(connection, null, user.Id, ct);
                return room is null
                    ? Results.NotFound(new LobbyFailure("복귀할 방이 더 이상 존재하지 않습니다."))
                    : Results.Ok(new
                    {
                        message = allReturned
                            ? "남아 있는 모든 플레이어가 복귀하여 방을 다시 대기 상태로 전환했습니다."
                            : "게임 서버 접속을 종료하고 방으로 복귀했습니다.",
                        room
                    });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/current/transfer-host", async (
            HttpRequest request, TransferHostRequest body, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            if (!ulong.TryParse(body.TargetUserId, System.Globalization.NumberStyles.None,
                    System.Globalization.CultureInfo.InvariantCulture, out var targetUserId))
            {
                return Results.BadRequest(new LobbyFailure("방장을 넘길 플레이어가 올바르지 않습니다."));
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await using var transaction = await connection.BeginTransactionAsync(ct);
                var membership = await LoadMembershipAsync(connection, transaction, user.Id, true, ct);
                if (membership is null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                }
                if (!membership.IsHost)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Json(new LobbyFailure("방장만 방장 권한을 넘길 수 있습니다."), statusCode: 403);
                }
                if (!string.Equals(membership.Status, "Waiting", StringComparison.Ordinal))
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("대기 중인 방에서만 방장 권한을 넘길 수 있습니다."));
                }
                if (targetUserId == user.Id ||
                    !await IsMemberOfRoomAsync(connection, transaction, membership.RoomId, targetUserId, ct))
                {
                    await transaction.RollbackAsync(ct);
                    return Results.BadRequest(new LobbyFailure("같은 방의 다른 플레이어를 선택해야 합니다."));
                }

                await ExecuteAsync(connection, transaction,
                    "UPDATE game_rooms SET host_user_id = @targetUserId WHERE id = @roomId;",
                    ("@targetUserId", targetUserId), ("@roomId", membership.RoomId), ct);
                await ExecuteAsync(connection, transaction,
                    "UPDATE room_members SET is_ready = FALSE WHERE room_id = @roomId AND user_id IN (@oldHostUserId, @targetUserId);",
                    ("@roomId", membership.RoomId), ("@oldHostUserId", user.Id), ("@targetUserId", targetUserId), ct);
                await transaction.CommitAsync(ct);
                var room = await LoadCurrentRoomAsync(connection, null, user.Id, ct);
                return Results.Ok(new { message = "방장 권한을 넘겼습니다.", room });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/current/delete", async (HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await using var transaction = await connection.BeginTransactionAsync(ct);
                var membership = await LoadMembershipAsync(connection, transaction, user.Id, true, ct);
                if (membership is null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                }
                if (!membership.IsHost)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Json(new LobbyFailure("방장만 방을 삭제할 수 있습니다."), statusCode: 403);
                }

                await ExecuteAsync(connection, transaction,
                    "DELETE FROM game_rooms WHERE id = @roomId;", ("@roomId", membership.RoomId), ct);
                await transaction.CommitAsync(ct);
                return Results.Ok(new { message = "방을 삭제했습니다. 모든 참가자가 로비로 돌아갑니다." });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/current/ready", async (
            HttpRequest request, ReadyRequest body, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                var membership = await LoadMembershipAsync(connection, null, user.Id, false, ct);
                if (membership is null)
                {
                    return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                }
                if (membership.IsHost)
                {
                    return Results.BadRequest(new LobbyFailure("방장은 준비 대신 시작 버튼을 사용합니다."));
                }
                if (!string.Equals(membership.Status, "Waiting", StringComparison.Ordinal))
                {
                    return Results.Conflict(new LobbyFailure("이미 시작된 방의 준비 상태는 바꿀 수 없습니다."));
                }
                await ExecuteAsync(connection, null,
                    "UPDATE room_members SET is_ready = @ready, last_seen_at_utc = UTC_TIMESTAMP(6) WHERE room_id = @roomId AND user_id = @userId;",
                    ("@ready", body.Ready), ("@roomId", membership.RoomId), ("@userId", user.Id), ct);
                var room = await LoadCurrentRoomAsync(connection, null, user.Id, ct);
                return Results.Ok(new { message = body.Ready ? "준비했습니다." : "준비를 취소했습니다.", room });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/current/chat", async (
            HttpRequest request, ChatRequest body, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            var message = body.Message?.Trim() ?? string.Empty;
            if (message.Length is < 1 or > 300)
            {
                return Results.BadRequest(new LobbyFailure("채팅은 1~300자여야 합니다."));
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                var membership = await LoadMembershipAsync(connection, null, user.Id, false, ct);
                if (membership is null)
                {
                    return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                }
                if (!string.Equals(membership.Status, "Waiting", StringComparison.Ordinal))
                {
                    return Results.Conflict(new LobbyFailure("게임이 시작된 뒤에는 로비 채팅을 보낼 수 없습니다."));
                }
                await ExecuteAsync(connection, null,
                    "INSERT INTO room_chat_messages (room_id, user_id, message) VALUES (@roomId, @userId, @message);",
                    ("@roomId", membership.RoomId), ("@userId", user.Id), ("@message", message), ct);
                var room = await LoadCurrentRoomAsync(connection, null, user.Id, ct);
                return Results.Ok(new { message = "채팅을 보냈습니다.", room });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/current/start", async (HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await using var transaction = await connection.BeginTransactionAsync(ct);
                var membership = await LoadMembershipAsync(connection, transaction, user.Id, true, ct);
                if (membership is null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                }
                if (!membership.IsHost)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }
                if (!string.Equals(membership.Status, "Waiting", StringComparison.Ordinal))
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("이미 시작된 방입니다."));
                }

                var members = new List<(ulong UserId, bool Ready)>();
                await using (var selectMembers = new MySqlCommand(
                    "SELECT user_id, is_ready FROM room_members WHERE room_id = @roomId ORDER BY joined_at_utc, user_id FOR UPDATE;",
                    connection, transaction) { CommandTimeout = 5 })
                {
                    selectMembers.Parameters.AddWithValue("@roomId", membership.RoomId);
                    await using var reader = await selectMembers.ExecuteReaderAsync(ct);
                    while (await reader.ReadAsync(ct))
                    {
                        members.Add((reader.GetUInt64(0), reader.GetBoolean(1)));
                    }
                }
                if (members.Count != MaximumPlayers)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("정확히 3명이 모여야 시작할 수 있습니다."));
                }
                if (members.Any(member => member.UserId != user.Id && !member.Ready))
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("방장을 제외한 모든 플레이어가 준비해야 합니다."));
                }

                var mannequinIndex = RandomNumberGenerator.GetInt32(members.Count);
				var requestTravelUrl = ResolveTravelUrlForRequest(request, travelUrl);
                await ExecuteAsync(connection, transaction,
                    "UPDATE room_members SET assigned_role = 'Survivor', returned_to_room = FALSE WHERE room_id = @roomId;",
                    ("@roomId", membership.RoomId), ct);
                await ExecuteAsync(connection, transaction,
                    "UPDATE room_members SET assigned_role = 'Mannequin' WHERE room_id = @roomId AND user_id = @userId;",
                    ("@roomId", membership.RoomId), ("@userId", members[mannequinIndex].UserId), ct);
                var matchId = Guid.NewGuid().ToString("D");
                await ExecuteAsync(connection, transaction,
                    "UPDATE game_rooms SET status = 'Started', travel_url = @travelUrl, match_id = @matchId, started_at_utc = UTC_TIMESTAMP(6) WHERE id = @roomId;",
                    ("@travelUrl", requestTravelUrl), ("@matchId", matchId), ("@roomId", membership.RoomId), ct);
                await transaction.CommitAsync(ct);
                var room = await LoadCurrentRoomAsync(connection, null, user.Id, ct);
                return Results.Ok(new { message = "게임을 시작합니다.", room });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/game-tickets", async (
            HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                await SecurityAudit.WriteAsync(database, request, "GameTicketIssue", "Unauthorized", null, null, ct);
                return Results.Unauthorized();
            }
            if (!securityOptions.IsGameServerSecurityConfigured)
            {
                await SecurityAudit.WriteAsync(database, request, "GameTicketIssue", "SecurityNotConfigured", user.Id, null, ct);
                return Results.Json(new LobbyFailure("게임 서버 보안 비밀키가 설정되지 않았습니다."), statusCode: 503);
            }

            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await using var transaction = await connection.BeginTransactionAsync(ct);
                string roomId;
                string matchId;
                string role;
                string gameServerTravelUrl;
                await using (var select = new MySqlCommand(
                    """
                    SELECT r.id, r.match_id, m.assigned_role, r.travel_url
                    FROM room_members m
                    INNER JOIN game_rooms r ON r.id = m.room_id
                    WHERE m.user_id = @userId AND r.status = 'Started'
                    LIMIT 1
                    FOR UPDATE;
                    """, connection, transaction) { CommandTimeout = 5 })
                {
                    select.Parameters.AddWithValue("@userId", user.Id);
                    await using var reader = await select.ExecuteReaderAsync(ct);
                    if (!await reader.ReadAsync(ct) || reader.IsDBNull(1) || reader.IsDBNull(2) || reader.IsDBNull(3))
                    {
                        await SecurityAudit.WriteAsync(database, request, "GameTicketIssue", "NoStartedMatch", user.Id, null, ct);
                        return Results.Conflict(new LobbyFailure("시작된 경기의 역할 또는 서버 정보가 없습니다."));
                    }
                    roomId = ReadRoomId(reader, 0);
                    matchId = ReadRoomId(reader, 1);
                    role = reader.GetString(2);
                    gameServerTravelUrl = reader.GetString(3);
                }

                var ticket = WebEncoders.Base64UrlEncode(RandomNumberGenerator.GetBytes(48));
                var expiresAtUtc = DateTime.UtcNow.AddSeconds(GameTicketLifetimeSeconds);
                await using (var cleanup = new MySqlCommand(
                    "DELETE FROM game_join_tickets WHERE user_id = @userId;",
                    connection, transaction) { CommandTimeout = 5 })
                {
                    cleanup.Parameters.AddWithValue("@userId", user.Id);
                    await cleanup.ExecuteNonQueryAsync(ct);
                }
                await using (var insert = new MySqlCommand(
                    """
                    INSERT INTO game_join_tickets
                        (ticket_hash, room_id, match_id, user_id, role, expires_at_utc)
                    VALUES
                        (@ticketHash, @roomId, @matchId, @userId, @role, @expiresAtUtc);
                    """, connection, transaction) { CommandTimeout = 5 })
                {
                    insert.Parameters.Add("@ticketHash", MySqlDbType.Binary, 32).Value =
                        SHA256.HashData(Encoding.UTF8.GetBytes(ticket));
                    insert.Parameters.AddWithValue("@roomId", roomId);
                    insert.Parameters.AddWithValue("@matchId", matchId);
                    insert.Parameters.AddWithValue("@userId", user.Id);
                    insert.Parameters.AddWithValue("@role", role);
                    insert.Parameters.AddWithValue("@expiresAtUtc", expiresAtUtc);
                    await insert.ExecuteNonQueryAsync(ct);
                }
                await transaction.CommitAsync(ct);
                // 각 클라이언트 연결마다 서로 다른 256비트 키를 파생한다. 원본 키와 티켓은 DB에 저장하지 않는다.
                var encryptionKey = Convert.ToBase64String(securityOptions.DeriveConnectionEncryptionKey(ticket));
                await SecurityAudit.WriteAsync(database, request, "GameTicketIssue", "Success", user.Id,
                    $"RoomId={roomId}; MatchId={matchId}; Role={role}", ct);
                return Results.Ok(new
                {
                    message = "일회용 게임 접속 티켓을 발급했습니다.",
                    ticket,
                    encryptionKey,
                    roomId,
                    matchId,
                    role,
                    travelUrl = gameServerTravelUrl,
                    expiresAtUtc,
                    clientBuildVersion = versionCompatibility.ClientBuildVersion,
                    dedicatedServerBuildVersion = versionCompatibility.DedicatedServerBuildVersion,
                    apiVersion = versionCompatibility.ApiVersion,
                    gameDataVersion = versionCompatibility.GameDataVersion,
                    networkProtocolVersion = versionCompatibility.NetworkProtocolVersion
                });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("game-ticket");

        app.MapPost("/api/server/game-tickets/consume", async (
            HttpRequest request,
            ConsumeGameTicketRequest body,
            AuthDatabase database,
            CancellationToken ct) =>
        {
            if (!securityOptions.IsAuthorizedGameServer(request.Headers["X-ProjectProject01-Server-Secret"].ToString()))
            {
                await SecurityAudit.WriteAsync(database, request, "GameTicketConsume", "UnauthorizedServer", null, null, ct);
                return Results.Unauthorized();
            }
            var ticket = body.Ticket?.Trim() ?? string.Empty;
            if (ticket.Length is < 48 or > 256)
            {
                await SecurityAudit.WriteAsync(database, request, "GameTicketConsume", "InvalidTicketFormat", null, null, ct);
                return Results.BadRequest(new LobbyFailure("게임 접속 티켓 형식이 올바르지 않습니다."));
            }

            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await using var transaction = await connection.BeginTransactionAsync(ct);
                ulong ticketId;
                ulong userId;
                string displayName;
                string roomId;
                string matchId;
                string role;
                await using (var select = new MySqlCommand(
                    """
                    SELECT t.id, t.user_id, u.display_name, t.room_id, t.match_id, t.role
                    FROM game_join_tickets t
                    INNER JOIN users u ON u.id = t.user_id
                    WHERE t.ticket_hash = @ticketHash
                      AND t.consumed_at_utc IS NULL
                      AND t.expires_at_utc > UTC_TIMESTAMP(6)
                    LIMIT 1
                    FOR UPDATE;
                    """, connection, transaction) { CommandTimeout = 5 })
                {
                    select.Parameters.Add("@ticketHash", MySqlDbType.Binary, 32).Value =
                        SHA256.HashData(Encoding.UTF8.GetBytes(ticket));
                    await using var reader = await select.ExecuteReaderAsync(ct);
                    if (!await reader.ReadAsync(ct))
                    {
                        await transaction.RollbackAsync(ct);
                        await SecurityAudit.WriteAsync(database, request, "GameTicketConsume", "InvalidExpiredOrUsed", null, null, ct);
                        return Results.Unauthorized();
                    }
                    ticketId = reader.GetUInt64(0);
                    userId = reader.GetUInt64(1);
                    displayName = reader.GetString(2);
                    roomId = ReadRoomId(reader, 3);
                    matchId = ReadRoomId(reader, 4);
                    role = reader.GetString(5);
                }
                await ExecuteAsync(connection, transaction,
                    "UPDATE game_join_tickets SET consumed_at_utc = UTC_TIMESTAMP(6) WHERE id = @ticketId;",
                    ("@ticketId", ticketId), ct);
                await transaction.CommitAsync(ct);

                var encryptionKey = Convert.ToBase64String(securityOptions.DeriveConnectionEncryptionKey(ticket));
                await SecurityAudit.WriteAsync(database, request, "GameTicketConsume", "Success", userId,
                    $"RoomId={roomId}; MatchId={matchId}; Role={role}", ct);
                return Results.Ok(new
                {
                    userId = userId.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    displayName,
                    roomId,
                    matchId,
                    role,
                    encryptionKey,
                    clientBuildVersion = versionCompatibility.ClientBuildVersion,
                    dedicatedServerBuildVersion = versionCompatibility.DedicatedServerBuildVersion,
                    apiVersion = versionCompatibility.ApiVersion,
                    gameDataVersion = versionCompatibility.GameDataVersion,
                    networkProtocolVersion = versionCompatibility.NetworkProtocolVersion
                });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("game-server");

        app.MapPost("/api/server/matches/results", async (
            HttpRequest request,
            SubmitVerifiedMatchResultRequest body,
            AuthDatabase database,
            CancellationToken ct) =>
        {
            if (!securityOptions.IsAuthorizedGameServer(request.Headers["X-ProjectProject01-Server-Secret"].ToString()))
            {
                await SecurityAudit.WriteAsync(database, request, "MatchResultVerify", "UnauthorizedServer", null, null, ct);
                return Results.Unauthorized();
            }
            if (!Guid.TryParse(body.MatchId, out var matchId) ||
                !ulong.TryParse(body.UserId, out var userId))
            {
                return Results.BadRequest(new LobbyFailure("경기 또는 사용자 ID 형식이 올바르지 않습니다."));
            }
            var normalizedRole = NormalizeLeaderboardRole(body.Role ?? string.Empty);
            if (normalizedRole is null)
            {
                return Results.BadRequest(new LobbyFailure("역할은 Mannequin 또는 Survivor여야 합니다."));
            }
            var compatibilityBody = new SubmitLeaderboardRecordRequest(
                body.MatchId, normalizedRole, body.Success, body.CaptureCount,
                body.FirstCaptureSeconds, body.AllCapturedSeconds, body.RescueCount, body.EscapeSeconds);
            var validationError = body.Success ? ValidateLeaderboardRecord(normalizedRole, compatibilityBody) : null;
            if (validationError is not null)
            {
                return Results.BadRequest(new LobbyFailure(validationError));
            }

            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                string roomId;
                await using (var membership = new MySqlCommand(
                    """
                    SELECT r.id
                    FROM game_rooms r
                    INNER JOIN room_members m ON m.room_id = r.id
                    WHERE r.match_id = @matchId AND m.user_id = @userId AND m.assigned_role = @role
                    LIMIT 1;
                    """, connection) { CommandTimeout = 5 })
                {
                    membership.Parameters.AddWithValue("@matchId", matchId.ToString("D"));
                    membership.Parameters.AddWithValue("@userId", userId);
                    membership.Parameters.AddWithValue("@role", normalizedRole);
                    var roomValue = await membership.ExecuteScalarAsync(ct);
                    if (roomValue is null)
                    {
                        await SecurityAudit.WriteAsync(database, request, "MatchResultVerify", "MembershipMismatch", userId,
                            $"MatchId={matchId:D}; Role={normalizedRole}", ct);
                        return Results.StatusCode(StatusCodes.Status403Forbidden);
                    }
                    roomId = roomValue switch
                    {
                        Guid guid => guid.ToString("D"),
                        _ => Convert.ToString(roomValue, System.Globalization.CultureInfo.InvariantCulture) ?? string.Empty
                    };
                }

                await using var command = new MySqlCommand(
                    """
                    INSERT INTO verified_match_results
                        (match_id, room_id, user_id, role, success, capture_count, first_capture_seconds,
                         all_captured_seconds, rescue_count, escape_seconds)
                    VALUES
                        (@matchId, @roomId, @userId, @role, @success, @captureCount, @firstCaptureSeconds,
                         @allCapturedSeconds, @rescueCount, @escapeSeconds)
                    ON DUPLICATE KEY UPDATE
                        success = VALUES(success), capture_count = VALUES(capture_count),
                        first_capture_seconds = VALUES(first_capture_seconds),
                        all_captured_seconds = VALUES(all_captured_seconds), rescue_count = VALUES(rescue_count),
                        escape_seconds = VALUES(escape_seconds), verified_at_utc = UTC_TIMESTAMP(6);
                    """, connection) { CommandTimeout = 5 };
                command.Parameters.AddWithValue("@matchId", matchId.ToString("D"));
                command.Parameters.AddWithValue("@roomId", roomId);
                command.Parameters.AddWithValue("@userId", userId);
                command.Parameters.AddWithValue("@role", normalizedRole);
                command.Parameters.AddWithValue("@success", body.Success);
                command.Parameters.AddWithValue("@captureCount", normalizedRole == "Mannequin" && body.Success ? body.CaptureCount : DBNull.Value);
                command.Parameters.AddWithValue("@firstCaptureSeconds", normalizedRole == "Mannequin" && body.Success ? body.FirstCaptureSeconds : DBNull.Value);
                command.Parameters.AddWithValue("@allCapturedSeconds", normalizedRole == "Mannequin" && body.Success ? body.AllCapturedSeconds : DBNull.Value);
                command.Parameters.AddWithValue("@rescueCount", normalizedRole == "Survivor" && body.Success ? body.RescueCount : DBNull.Value);
                command.Parameters.AddWithValue("@escapeSeconds", normalizedRole == "Survivor" && body.Success ? body.EscapeSeconds : DBNull.Value);
                await command.ExecuteNonQueryAsync(ct);
                await SecurityAudit.WriteAsync(database, request, "MatchResultVerify", "Success", userId,
                    $"MatchId={matchId:D}; Role={normalizedRole}; Success={body.Success}", ct);
                return Results.Ok(new { message = "데디케이티드 서버 경기 결과를 검증 기록했습니다." });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("game-server");

		app.MapPost("/api/server/matches/forfeit", async (
			HttpRequest request,
			AuthoritativeForfeitRequest body,
			AuthDatabase database,
			CancellationToken ct) =>
		{
			if (!securityOptions.IsAuthorizedGameServer(request.Headers["X-ProjectProject01-Server-Secret"].ToString()))
			{
				await SecurityAudit.WriteAsync(database, request, "MatchForfeit", "UnauthorizedServer", null, null, ct);
				return Results.Unauthorized();
			}
			if (!Guid.TryParse(body.MatchId, out var matchId) || !ulong.TryParse(body.UserId, out var userId))
			{
				return Results.BadRequest(new LobbyFailure("경기 또는 사용자 ID 형식이 올바르지 않습니다."));
			}

			try
			{
				await using var connection = await database.OpenConnectionAsync(ct);
				await using var transaction = await connection.BeginTransactionAsync(ct);
				string? roomId = null;
				ulong hostUserId = 0;
				await using (var membership = new MySqlCommand(
					"""
					SELECT r.id, r.host_user_id
					FROM game_rooms r
					INNER JOIN room_members m ON m.room_id = r.id
					WHERE r.match_id = @matchId AND m.user_id = @userId
					LIMIT 1 FOR UPDATE;
					""", connection, transaction) { CommandTimeout = 5 })
				{
					membership.Parameters.AddWithValue("@matchId", matchId.ToString("D"));
					membership.Parameters.AddWithValue("@userId", userId);
					await using var reader = await membership.ExecuteReaderAsync(ct);
					if (await reader.ReadAsync(ct))
					{
						roomId = ReadRoomId(reader, 0);
						hostUserId = reader.GetUInt64(1);
					}
				}

				if (roomId is null)
				{
					await transaction.RollbackAsync(ct);
					return Results.Ok(new { message = "이미 정리된 경기 참가자입니다." });
				}

				if (hostUserId == userId)
				{
					var successor = await FindEarliestOtherMemberAsync(connection, transaction, roomId, userId, ct);
					if (successor is ulong successorUserId)
					{
						await ExecuteAsync(connection, transaction,
							"UPDATE game_rooms SET host_user_id = @successorUserId WHERE id = @roomId;",
							("@successorUserId", successorUserId), ("@roomId", roomId), ct);
						await ExecuteAsync(connection, transaction,
							"UPDATE room_members SET is_ready = FALSE WHERE room_id = @roomId AND user_id = @successorUserId;",
							("@roomId", roomId), ("@successorUserId", successorUserId), ct);
					}
				}
				await ExecuteAsync(connection, transaction,
					"DELETE FROM room_members WHERE room_id = @roomId AND user_id = @userId;",
					("@roomId", roomId), ("@userId", userId), ct);

				await using (var count = new MySqlCommand(
					"SELECT COUNT(*) FROM room_members WHERE room_id = @roomId;", connection, transaction) { CommandTimeout = 5 })
				{
					count.Parameters.AddWithValue("@roomId", roomId);
					if (Convert.ToInt32(await count.ExecuteScalarAsync(ct)) == 0)
					{
						await ExecuteAsync(connection, transaction,
							"DELETE FROM game_rooms WHERE id = @roomId;", ("@roomId", roomId), ct);
					}
				}
				await transaction.CommitAsync(ct);
				await SecurityAudit.WriteAsync(database, request, "MatchForfeit", "Success", userId,
					$"MatchId={matchId:D}; Role={body.Role}", ct);
				return Results.Ok(new { message = "재접속 유예시간이 만료된 참가자를 방과 경기에서 정리했습니다." });
			}
			catch (MySqlException)
			{
				return DatabaseUnavailable();
			}
		}).RequireRateLimiting("game-server");

        app.MapGet("/api/leaderboards/{role}", async (
            string role, HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            var normalizedRole = NormalizeLeaderboardRole(role);
            if (normalizedRole is null)
            {
                return Results.BadRequest(new LobbyFailure("리더보드 역할은 Mannequin 또는 Survivor여야 합니다."));
            }
            var normalizedSort = NormalizeLeaderboardSort(normalizedRole, request.Query["sort"].ToString());
            if (normalizedSort is null)
            {
                return Results.BadRequest(new LobbyFailure("선택한 역할에서 사용할 수 없는 정렬 항목입니다."));
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                var entries = await LoadLeaderboardAsync(connection, normalizedRole, normalizedSort, ct);
                return Results.Ok(new
                {
                    message = "리더보드 상위 50개 기록을 불러왔습니다.",
                    role = normalizedRole,
                    sort = normalizedSort,
                    entries
                });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/leaderboards/records", async (
            HttpRequest request, SubmitLeaderboardRecordRequest body, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            if (!body.Success)
            {
                return Results.BadRequest(new LobbyFailure("실패한 경기 기록은 리더보드에 등록할 수 없습니다."));
            }
            if (!Guid.TryParse(body.MatchId, out var matchId))
            {
                return Results.BadRequest(new LobbyFailure("경기 ID 형식이 올바르지 않습니다."));
            }
            var normalizedRole = NormalizeLeaderboardRole(body.Role ?? string.Empty);
            if (normalizedRole is null)
            {
                return Results.BadRequest(new LobbyFailure("리더보드 역할은 Mannequin 또는 Survivor여야 합니다."));
            }

            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                int? verifiedCaptureCount = null;
                double? verifiedFirstCaptureSeconds = null;
                double? verifiedAllCapturedSeconds = null;
                int? verifiedRescueCount = null;
                double? verifiedEscapeSeconds = null;
                var hasVerifiedResult = false;
                await using (var verified = new MySqlCommand(
                    """
                    SELECT capture_count, first_capture_seconds, all_captured_seconds, rescue_count, escape_seconds
                    FROM verified_match_results
                    WHERE match_id = @matchId AND user_id = @userId AND role = @role AND success = TRUE
                    LIMIT 1;
                    """, connection) { CommandTimeout = 5 })
                {
                    verified.Parameters.AddWithValue("@matchId", matchId.ToString("D"));
                    verified.Parameters.AddWithValue("@userId", user.Id);
                    verified.Parameters.AddWithValue("@role", normalizedRole);
                    await using var reader = await verified.ExecuteReaderAsync(ct);
                    if (await reader.ReadAsync(ct))
                    {
                        hasVerifiedResult = true;
                        verifiedCaptureCount = reader.IsDBNull(0) ? null : reader.GetInt32(0);
                        verifiedFirstCaptureSeconds = reader.IsDBNull(1) ? null : Convert.ToDouble(reader.GetValue(1), System.Globalization.CultureInfo.InvariantCulture);
                        verifiedAllCapturedSeconds = reader.IsDBNull(2) ? null : Convert.ToDouble(reader.GetValue(2), System.Globalization.CultureInfo.InvariantCulture);
                        verifiedRescueCount = reader.IsDBNull(3) ? null : reader.GetInt32(3);
                        verifiedEscapeSeconds = reader.IsDBNull(4) ? null : Convert.ToDouble(reader.GetValue(4), System.Globalization.CultureInfo.InvariantCulture);
                    }
                }

                var remoteIpAddress = request.HttpContext.Connection.RemoteIpAddress;
                var isLoopbackDevelopment = remoteIpAddress is not null &&
                    System.Net.IPAddress.IsLoopback(remoteIpAddress);
                if (!hasVerifiedResult && !(securityOptions.AllowUnverifiedLeaderboardSubmissions && isLoopbackDevelopment))
                {
                    await SecurityAudit.WriteAsync(database, request, "LeaderboardPublish", "UnverifiedResultRejected", user.Id,
                        $"MatchId={matchId:D}; Role={normalizedRole}", ct);
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }

                var captureCount = hasVerifiedResult ? verifiedCaptureCount : body.CaptureCount;
                var firstCaptureSeconds = hasVerifiedResult ? verifiedFirstCaptureSeconds : body.FirstCaptureSeconds;
                var allCapturedSeconds = hasVerifiedResult ? verifiedAllCapturedSeconds : body.AllCapturedSeconds;
                var rescueCount = hasVerifiedResult ? verifiedRescueCount : body.RescueCount;
                var escapeSeconds = hasVerifiedResult ? verifiedEscapeSeconds : body.EscapeSeconds;
                var effectiveRecord = new SubmitLeaderboardRecordRequest(
                    matchId.ToString("D"), normalizedRole, true, captureCount,
                    firstCaptureSeconds, allCapturedSeconds, rescueCount, escapeSeconds);
                var validationError = ValidateLeaderboardRecord(normalizedRole, effectiveRecord);
                if (!string.IsNullOrEmpty(validationError))
                {
                    await SecurityAudit.WriteAsync(database, request, "LeaderboardPublish", "InvalidVerifiedResult", user.Id,
                        $"MatchId={matchId:D}; Role={normalizedRole}", ct);
                    return Results.BadRequest(new LobbyFailure(validationError));
                }
                await using var command = new MySqlCommand(
                    """
                    INSERT INTO leaderboard_records
                    (match_id, user_id, role, capture_count, first_capture_seconds, all_captured_seconds, rescue_count, escape_seconds)
                    VALUES
                    (@matchId, @userId, @role, @captureCount, @firstCaptureSeconds, @allCapturedSeconds, @rescueCount, @escapeSeconds);
                    """, connection) { CommandTimeout = 5 };
                command.Parameters.AddWithValue("@matchId", matchId.ToString("D"));
                command.Parameters.AddWithValue("@userId", user.Id);
                command.Parameters.AddWithValue("@role", normalizedRole);
                command.Parameters.AddWithValue("@captureCount",
                    normalizedRole == "Mannequin" ? captureCount!.Value : DBNull.Value);
                command.Parameters.AddWithValue("@firstCaptureSeconds",
                    normalizedRole == "Mannequin" ? firstCaptureSeconds!.Value : DBNull.Value);
                command.Parameters.AddWithValue("@allCapturedSeconds",
                    normalizedRole == "Mannequin" ? allCapturedSeconds!.Value : DBNull.Value);
                command.Parameters.AddWithValue("@rescueCount",
                    normalizedRole == "Survivor" ? rescueCount!.Value : DBNull.Value);
                command.Parameters.AddWithValue("@escapeSeconds",
                    normalizedRole == "Survivor" ? escapeSeconds!.Value : DBNull.Value);
                await command.ExecuteNonQueryAsync(ct);
                if (hasVerifiedResult)
                {
                    await using var markPublished = new MySqlCommand(
                        "UPDATE verified_match_results SET published_at_utc = UTC_TIMESTAMP(6) WHERE match_id = @matchId AND user_id = @userId;",
                        connection) { CommandTimeout = 5 };
                    markPublished.Parameters.AddWithValue("@matchId", matchId.ToString("D"));
                    markPublished.Parameters.AddWithValue("@userId", user.Id);
                    await markPublished.ExecuteNonQueryAsync(ct);
                }
                await SecurityAudit.WriteAsync(database, request, "LeaderboardPublish", "Success", user.Id,
                    $"MatchId={matchId:D}; Role={normalizedRole}; Verified={hasVerifiedResult}", ct);
                return Results.Ok(new { message = "성공 기록을 리더보드에 등록했습니다." });
            }
            catch (MySqlException exception) when (exception.Number == 1062)
            {
                return Results.Conflict(new LobbyFailure("이 경기의 기록은 이미 등록되어 있습니다."));
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");
    }

    private static string ResolveTravelUrlForRequest(HttpRequest request, string configuredTravelUrl)
    {
        var requestHost = request.Host.Host;
        if (IPAddress.TryParse(requestHost, out var parsedAddress) &&
            parsedAddress.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork)
        {
            var bytes = parsedAddress.GetAddressBytes();
            var isTestNetwork = bytes[0] == 25 || bytes[0] == 10 ||
                (bytes[0] == 172 && bytes[1] is >= 16 and <= 31) ||
                (bytes[0] == 192 && bytes[1] == 168) || bytes[0] == 127;
            if (isTestNetwork)
            {
                return $"{requestHost}:7777";
            }
        }
        return configuredTravelUrl;
    }

    private static string? NormalizeLeaderboardRole(string role) => role.Trim().ToLowerInvariant() switch
    {
        "mannequin" => "Mannequin",
        "survivor" => "Survivor",
        _ => null
    };

    private static string? NormalizeLeaderboardSort(string role, string sort)
    {
        var normalized = string.IsNullOrWhiteSpace(sort) ? "overall" : sort.Trim();
        return role switch
        {
            "Mannequin" when normalized is "overall" or "captures" or "firstCapture" or "allCaptured" => normalized,
            "Survivor" when normalized is "overall" or "rescues" or "escape" => normalized,
            _ => null
        };
    }

    private static string? ValidateLeaderboardRecord(string role, SubmitLeaderboardRecordRequest body)
    {
        const double maximumMatchSeconds = 86400.0;
        const int maximumEventCount = 10000;
        if (role == "Mannequin")
        {
            if (body.CaptureCount is null or < 1 or > maximumEventCount ||
                body.FirstCaptureSeconds is null or <= 0.0 or > maximumMatchSeconds ||
                body.AllCapturedSeconds is null or <= 0.0 or > maximumMatchSeconds)
            {
                return "마네킹 성공 기록의 포획 횟수와 포획 시간이 유효하지 않습니다.";
            }
            if (body.AllCapturedSeconds < body.FirstCaptureSeconds)
            {
                return "전원 포획 시간은 첫 포획 시간보다 빠를 수 없습니다.";
            }
            return null;
        }

        if (body.RescueCount is null or < 0 or > maximumEventCount ||
            body.EscapeSeconds is null or <= 0.0 or > maximumMatchSeconds)
        {
            return "생존자 성공 기록의 구출 횟수와 탈출 시간이 유효하지 않습니다.";
        }
        return null;
    }

    private static async Task<IReadOnlyList<LeaderboardEntry>> LoadLeaderboardAsync(
        MySqlConnection connection, string role, string sort, CancellationToken ct)
    {
        var orderBy = role switch
        {
            "Mannequin" when sort == "captures" =>
                "capture_count DESC, all_captured_seconds ASC, first_capture_seconds ASC, record_id ASC",
            "Mannequin" when sort == "firstCapture" =>
                "first_capture_seconds ASC, capture_count DESC, all_captured_seconds ASC, record_id ASC",
            "Mannequin" when sort == "allCaptured" =>
                "all_captured_seconds ASC, capture_count DESC, first_capture_seconds ASC, record_id ASC",
            "Survivor" when sort == "rescues" =>
                "rescue_count DESC, escape_seconds ASC, record_id ASC",
            "Survivor" when sort == "escape" =>
                "escape_seconds ASC, rescue_count DESC, record_id ASC",
            _ => "average_rank ASC, created_at_utc ASC, record_id ASC"
        };
        var rankedColumns = role == "Mannequin"
            ? """
              RANK() OVER (ORDER BY lr.capture_count DESC, lr.all_captured_seconds ASC, lr.first_capture_seconds ASC, lr.id ASC) AS primary_rank,
              RANK() OVER (ORDER BY lr.first_capture_seconds ASC, lr.capture_count DESC, lr.all_captured_seconds ASC, lr.id ASC) AS secondary_rank,
              RANK() OVER (ORDER BY lr.all_captured_seconds ASC, lr.capture_count DESC, lr.first_capture_seconds ASC, lr.id ASC) AS tertiary_rank
              """
            : """
              RANK() OVER (ORDER BY lr.rescue_count DESC, lr.escape_seconds ASC, lr.id ASC) AS primary_rank,
              RANK() OVER (ORDER BY lr.escape_seconds ASC, lr.rescue_count DESC, lr.id ASC) AS secondary_rank,
              0 AS tertiary_rank
              """;
        var averageExpression = role == "Mannequin"
            ? "(primary_rank + secondary_rank + tertiary_rank) / 3.0"
            : "(primary_rank + secondary_rank) / 2.0";
        var sql = $"""
            WITH metric_ranks AS
            (
                SELECT lr.id AS record_id, u.display_name, lr.role, lr.capture_count,
                       lr.first_capture_seconds, lr.all_captured_seconds, lr.rescue_count,
                       lr.escape_seconds, lr.created_at_utc,
                       {rankedColumns},
                       COUNT(*) OVER () AS total_count
                FROM leaderboard_records lr
                INNER JOIN users u ON u.id = lr.user_id
                WHERE lr.role = @role
            ), scored AS
            (
                SELECT *, {averageExpression} AS average_rank
                FROM metric_ranks
            )
            SELECT record_id, display_name, role, capture_count, first_capture_seconds,
                   all_captured_seconds, rescue_count, escape_seconds, created_at_utc,
                   average_rank, total_count
            FROM scored
            ORDER BY {orderBy}
            LIMIT 50;
            """;

        var entries = new List<LeaderboardEntry>();
        await using var command = new MySqlCommand(sql, connection) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@role", role);
        await using var reader = await command.ExecuteReaderAsync(ct);
        var displayRank = 0;
        while (await reader.ReadAsync(ct))
        {
            ++displayRank;
            var averageRank = Convert.ToDouble(reader.GetValue(9), System.Globalization.CultureInfo.InvariantCulture);
            var totalCount = reader.GetInt64(10);
            var overallScore = totalCount <= 1
                ? 100.0
                : Math.Clamp(100.0 * (1.0 - ((averageRank - 1.0) / (totalCount - 1.0))), 0.0, 100.0);
            entries.Add(new LeaderboardEntry(
                displayRank,
                reader.GetString(1),
                reader.GetString(2),
                Math.Round(overallScore, 2),
                reader.IsDBNull(3) ? null : reader.GetInt32(3),
                reader.IsDBNull(4) ? null : Convert.ToDouble(reader.GetValue(4), System.Globalization.CultureInfo.InvariantCulture),
                reader.IsDBNull(5) ? null : Convert.ToDouble(reader.GetValue(5), System.Globalization.CultureInfo.InvariantCulture),
                reader.IsDBNull(6) ? null : reader.GetInt32(6),
                reader.IsDBNull(7) ? null : Convert.ToDouble(reader.GetValue(7), System.Globalization.CultureInfo.InvariantCulture),
                DateTime.SpecifyKind(reader.GetDateTime(8), DateTimeKind.Utc).ToString("O")));
        }
        return entries;
    }

    internal static async Task<LobbyUser?> AuthenticateAsync(HttpRequest request, AuthDatabase database, CancellationToken ct)
    {
        if (!database.IsConfigured)
        {
            return null;
        }
        var authorization = request.Headers.Authorization.ToString();
        const string prefix = "Bearer ";
        if (!authorization.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }
        var token = authorization[prefix.Length..].Trim();
        if (token.Length is < 32 or > 512)
        {
            return null;
        }
        await using var connection = await database.OpenConnectionAsync(ct);
        await using var command = new MySqlCommand(
            """
            SELECT u.id, u.display_name
            FROM auth_sessions s
            INNER JOIN users u ON u.id = s.user_id
            WHERE s.access_token_hash = @accessHash
              AND s.revoked_at_utc IS NULL
              AND s.access_expires_at_utc > UTC_TIMESTAMP(6)
            LIMIT 1;
            """, connection) { CommandTimeout = 5 };
        command.Parameters.Add("@accessHash", MySqlDbType.Binary, 32).Value = SHA256.HashData(Encoding.UTF8.GetBytes(token));
        await using var reader = await command.ExecuteReaderAsync(ct);
        return await reader.ReadAsync(ct) ? new LobbyUser(reader.GetUInt64(0), reader.GetString(1)) : null;
    }

    private static async Task CleanupStaleRoomsAsync(
        MySqlConnection connection, MySqlTransaction? transaction, CancellationToken ct)
    {
        if (transaction is null)
        {
            await using var ownedTransaction = await connection.BeginTransactionAsync(ct);
            await CleanupStaleRoomsCoreAsync(connection, ownedTransaction, ct);
            await ownedTransaction.CommitAsync(ct);
            return;
        }

        await CleanupStaleRoomsCoreAsync(connection, transaction, ct);
    }

    private static async Task CleanupStaleRoomsCoreAsync(
        MySqlConnection connection, MySqlTransaction transaction, CancellationToken ct)
    {
        await ExecuteAsync(connection, transaction,
            $"""
            DELETE m FROM room_members m
            INNER JOIN game_rooms r ON r.id = m.room_id
            WHERE r.status = 'Waiting'
              AND m.user_id <> r.host_user_id
              AND m.last_seen_at_utc < UTC_TIMESTAMP(6) - INTERVAL {StaleMemberSeconds} SECOND;
            """, ct);

        var staleHosts = new List<(string RoomId, ulong HostUserId)>();
        await using (var selectStaleHosts = new MySqlCommand(
            $"""
            SELECT r.id, r.host_user_id
            FROM game_rooms r
            LEFT JOIN room_members host_member
              ON host_member.room_id = r.id AND host_member.user_id = r.host_user_id
            WHERE r.status = 'Waiting'
              AND (host_member.user_id IS NULL OR host_member.last_seen_at_utc < UTC_TIMESTAMP(6) - INTERVAL {StaleMemberSeconds} SECOND)
            FOR UPDATE;
            """, connection, transaction) { CommandTimeout = 5 })
        {
            await using var reader = await selectStaleHosts.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
            {
                staleHosts.Add((ReadRoomId(reader, 0), reader.GetUInt64(1)));
            }
        }

        foreach (var staleHost in staleHosts)
        {
            var successorUserId = await FindEarliestOtherMemberAsync(
                connection, transaction, staleHost.RoomId, staleHost.HostUserId, ct);
            if (successorUserId is ulong successor)
            {
                await ExecuteAsync(connection, transaction,
                    "UPDATE game_rooms SET host_user_id = @successorUserId WHERE id = @roomId;",
                    ("@successorUserId", successor), ("@roomId", staleHost.RoomId), ct);
                await ExecuteAsync(connection, transaction,
                    "UPDATE room_members SET is_ready = FALSE WHERE room_id = @roomId AND user_id = @successorUserId;",
                    ("@roomId", staleHost.RoomId), ("@successorUserId", successor), ct);
                await ExecuteAsync(connection, transaction,
                    "DELETE FROM room_members WHERE room_id = @roomId AND user_id = @hostUserId;",
                    ("@roomId", staleHost.RoomId), ("@hostUserId", staleHost.HostUserId), ct);
            }
            else
            {
                await ExecuteAsync(connection, transaction,
                    "DELETE FROM game_rooms WHERE id = @roomId;", ("@roomId", staleHost.RoomId), ct);
            }
        }
    }

    private static async Task LockLobbyStateAsync(MySqlConnection connection, MySqlTransaction transaction, CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            "SELECT id FROM lobby_state WHERE id = 1 FOR UPDATE;", connection, transaction) { CommandTimeout = 5 };
        await command.ExecuteScalarAsync(ct);
    }

    private static async Task<int> CountRoomsAsync(MySqlConnection connection, MySqlTransaction transaction, CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            "SELECT COUNT(*) FROM game_rooms WHERE status = 'Waiting';", connection, transaction) { CommandTimeout = 5 };
        return Convert.ToInt32(await command.ExecuteScalarAsync(ct));
    }

    private static async Task<bool> WaitingRoomNameExistsAsync(
        MySqlConnection connection, MySqlTransaction transaction, string roomName, CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            "SELECT 1 FROM game_rooms WHERE status = 'Waiting' AND name = @roomName LIMIT 1;",
            connection, transaction) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@roomName", roomName);
        return await command.ExecuteScalarAsync(ct) is not null;
    }

    private static async Task<string> GenerateUniqueJoinCodeAsync(
        MySqlConnection connection, MySqlTransaction transaction, CancellationToken ct)
    {
        const string alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
        var characters = new char[8];
        for (var attempt = 0; attempt < 32; ++attempt)
        {
            for (var index = 0; index < characters.Length; ++index)
            {
                characters[index] = alphabet[RandomNumberGenerator.GetInt32(alphabet.Length)];
            }
            var candidate = new string(characters);
            await using var command = new MySqlCommand(
                "SELECT 1 FROM game_rooms WHERE join_code = @joinCode LIMIT 1;",
                connection, transaction) { CommandTimeout = 5 };
            command.Parameters.AddWithValue("@joinCode", candidate);
            if (await command.ExecuteScalarAsync(ct) is null)
            {
                return candidate;
            }
        }
        throw new InvalidOperationException("고유한 방 참가 코드를 생성하지 못했습니다.");
    }

    private static async Task<string?> FindCurrentRoomIdAsync(
        MySqlConnection connection, MySqlTransaction? transaction, ulong userId, CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            "SELECT room_id FROM room_members WHERE user_id = @userId LIMIT 1;", connection, transaction) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@userId", userId);
        var value = await command.ExecuteScalarAsync(ct);
        return value switch
        {
            null or DBNull => null,
            Guid guid => guid.ToString("D"),
            _ => Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture)
        };
    }

    private static async Task<Membership?> LoadMembershipAsync(
        MySqlConnection connection, MySqlTransaction? transaction, ulong userId, bool forUpdate, CancellationToken ct)
    {
        var sql = """
            SELECT m.room_id, r.host_user_id = m.user_id AS is_host, r.status
            FROM room_members m
            INNER JOIN game_rooms r ON r.id = m.room_id
            WHERE m.user_id = @userId
            LIMIT 1
            """ + (forUpdate ? " FOR UPDATE;" : ";");
        await using var command = new MySqlCommand(sql, connection, transaction) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@userId", userId);
        await using var reader = await command.ExecuteReaderAsync(ct);
        return await reader.ReadAsync(ct)
            ? new Membership(ReadRoomId(reader, 0), reader.GetBoolean(1), reader.GetString(2))
            : null;
    }

    private static async Task TouchMemberAsync(
        MySqlConnection connection, MySqlTransaction? transaction, ulong userId, CancellationToken ct) =>
        await ExecuteAsync(connection, transaction,
            "UPDATE room_members SET last_seen_at_utc = UTC_TIMESTAMP(6) WHERE user_id = @userId;",
            ("@userId", userId), ct);

    private static async Task<ulong?> FindEarliestOtherMemberAsync(
        MySqlConnection connection,
        MySqlTransaction transaction,
        string roomId,
        ulong excludedUserId,
        CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            "SELECT user_id FROM room_members WHERE room_id = @roomId AND user_id <> @excludedUserId ORDER BY joined_at_utc ASC, user_id ASC LIMIT 1 FOR UPDATE;",
            connection, transaction) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@roomId", roomId);
        command.Parameters.AddWithValue("@excludedUserId", excludedUserId);
        var value = await command.ExecuteScalarAsync(ct);
        return value is null or DBNull ? null : Convert.ToUInt64(value);
    }

    private static async Task<bool> ResetStartedRoomIfAllMembersReturnedAsync(
        MySqlConnection connection,
        MySqlTransaction transaction,
        string roomId,
        CancellationToken ct)
    {
        var returnedStates = new List<bool>();
        await using (var command = new MySqlCommand(
            "SELECT returned_to_room FROM room_members WHERE room_id = @roomId FOR UPDATE;",
            connection, transaction) { CommandTimeout = 5 })
        {
            command.Parameters.AddWithValue("@roomId", roomId);
            await using var reader = await command.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
            {
                returnedStates.Add(reader.GetBoolean(0));
            }
        }
        if (returnedStates.Count == 0 || returnedStates.Any(returned => !returned))
        {
            return false;
        }

        await ExecuteAsync(connection, transaction,
            "DELETE FROM game_join_tickets WHERE room_id = @roomId;", ("@roomId", roomId), ct);
        await ExecuteAsync(connection, transaction,
            "UPDATE room_members SET is_ready = FALSE, assigned_role = NULL, returned_to_room = FALSE WHERE room_id = @roomId;",
            ("@roomId", roomId), ct);
        await ExecuteAsync(connection, transaction,
            "UPDATE game_rooms SET status = 'Waiting', travel_url = NULL, match_id = NULL, started_at_utc = NULL WHERE id = @roomId AND status = 'Started';",
            ("@roomId", roomId), ct);
        return true;
    }

    private static async Task<bool> IsMemberOfRoomAsync(
        MySqlConnection connection,
        MySqlTransaction transaction,
        string roomId,
        ulong userId,
        CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            "SELECT 1 FROM room_members WHERE room_id = @roomId AND user_id = @userId LIMIT 1 FOR UPDATE;",
            connection, transaction) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@roomId", roomId);
        command.Parameters.AddWithValue("@userId", userId);
        return await command.ExecuteScalarAsync(ct) is not null;
    }

    private static async Task<RoomState?> LoadCurrentRoomAsync(
        MySqlConnection connection, MySqlTransaction? transaction, ulong userId, CancellationToken ct)
    {
        string roomId;
        string joinCode;
        string name;
        string status;
        string? travelUrl;
        bool isHost;
        bool isReady;
        bool returnedToRoom;
        string? assignedRole;
        await using (var roomCommand = new MySqlCommand(
            """
            SELECT r.id, r.join_code, r.name, r.status, r.travel_url, r.host_user_id = m.user_id AS is_host,
                   m.is_ready, m.assigned_role, m.returned_to_room
            FROM room_members m
            INNER JOIN game_rooms r ON r.id = m.room_id
            WHERE m.user_id = @userId
            LIMIT 1;
            """, connection, transaction) { CommandTimeout = 5 })
        {
            roomCommand.Parameters.AddWithValue("@userId", userId);
            await using var reader = await roomCommand.ExecuteReaderAsync(ct);
            if (!await reader.ReadAsync(ct))
            {
                return null;
            }
            roomId = ReadRoomId(reader, 0);
            joinCode = reader.GetString(1);
            name = reader.GetString(2);
            status = reader.GetString(3);
            travelUrl = reader.IsDBNull(4) ? null : reader.GetString(4);
            isHost = reader.GetBoolean(5);
            isReady = reader.GetBoolean(6);
            assignedRole = reader.IsDBNull(7) ? null : reader.GetString(7);
            returnedToRoom = reader.GetBoolean(8);
        }

        var members = new List<RoomMember>();
        await using (var memberCommand = new MySqlCommand(
            """
            SELECT m.user_id, u.display_name, r.host_user_id = m.user_id AS is_host,
                   m.is_ready, m.assigned_role, m.returned_to_room
            FROM room_members m
            INNER JOIN users u ON u.id = m.user_id
            INNER JOIN game_rooms r ON r.id = m.room_id
            WHERE m.room_id = @roomId
            ORDER BY is_host DESC, m.joined_at_utc ASC, m.user_id ASC;
            """, connection, transaction) { CommandTimeout = 5 })
        {
            memberCommand.Parameters.AddWithValue("@roomId", roomId);
            await using var reader = await memberCommand.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
            {
                members.Add(new RoomMember(
                    reader.GetUInt64(0).ToString(System.Globalization.CultureInfo.InvariantCulture),
                    reader.GetString(1), reader.GetBoolean(2), reader.GetBoolean(3),
                    reader.IsDBNull(4) ? string.Empty : reader.GetString(4), reader.GetBoolean(5)));
            }
        }

        var chatMessages = new List<RoomChatMessage>();
        await using (var chatCommand = new MySqlCommand(
            """
            SELECT recent.id, u.display_name, recent.message, recent.created_at_utc
            FROM
            (
                SELECT id, user_id, message, created_at_utc
                FROM room_chat_messages
                WHERE room_id = @roomId
                ORDER BY id DESC
                LIMIT 100
            ) recent
            INNER JOIN users u ON u.id = recent.user_id
            ORDER BY recent.id ASC;
            """, connection, transaction) { CommandTimeout = 5 })
        {
            chatCommand.Parameters.AddWithValue("@roomId", roomId);
            await using var reader = await chatCommand.ExecuteReaderAsync(ct);
            while (await reader.ReadAsync(ct))
            {
                chatMessages.Add(new RoomChatMessage(
                    reader.GetUInt64(0).ToString(), reader.GetString(1), reader.GetString(2),
                    DateTime.SpecifyKind(reader.GetDateTime(3), DateTimeKind.Utc).ToString("O")));
            }
        }

        var canStart = isHost && members.Count == MaximumPlayers &&
            members.Where(member => !member.IsHost).All(member => member.Ready) &&
            string.Equals(status, "Waiting", StringComparison.Ordinal);
        return new RoomState(
            roomId, joinCode, name, isHost, isReady, canStart, string.Equals(status, "Started", StringComparison.Ordinal),
            returnedToRoom, travelUrl ?? string.Empty, assignedRole ?? string.Empty, members, chatMessages);
    }

    private static async Task ExecuteAsync(
        MySqlConnection connection, MySqlTransaction? transaction, string sql, CancellationToken ct)
    {
        await using var command = new MySqlCommand(sql, connection, transaction) { CommandTimeout = 5 };
        await command.ExecuteNonQueryAsync(ct);
    }

    private static string ReadRoomId(MySqlDataReader reader, int ordinal) =>
        reader.GetValue(ordinal) switch
        {
            Guid guid => guid.ToString("D"),
            string text => text,
            var value => Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture) ?? string.Empty
        };

    private static async Task ExecuteAsync(
        MySqlConnection connection,
        MySqlTransaction? transaction,
        string sql,
        (string Name, object Value) parameter,
        CancellationToken ct) =>
        await ExecuteAsync(connection, transaction, sql, new[] { parameter }, ct);

    private static async Task ExecuteAsync(
        MySqlConnection connection,
        MySqlTransaction? transaction,
        string sql,
        (string Name, object Value) parameter1,
        (string Name, object Value) parameter2,
        CancellationToken ct) =>
        await ExecuteAsync(connection, transaction, sql, new[] { parameter1, parameter2 }, ct);

    private static async Task ExecuteAsync(
        MySqlConnection connection,
        MySqlTransaction? transaction,
        string sql,
        (string Name, object Value) parameter1,
        (string Name, object Value) parameter2,
        (string Name, object Value) parameter3,
        CancellationToken ct) =>
        await ExecuteAsync(connection, transaction, sql, new[] { parameter1, parameter2, parameter3 }, ct);

    private static async Task ExecuteAsync(
        MySqlConnection connection,
        MySqlTransaction? transaction,
        string sql,
        IEnumerable<(string Name, object Value)> parameters,
        CancellationToken ct)
    {
        await using var command = new MySqlCommand(sql, connection, transaction) { CommandTimeout = 5 };
        foreach (var parameter in parameters)
        {
            command.Parameters.AddWithValue(parameter.Name, parameter.Value);
        }
        await command.ExecuteNonQueryAsync(ct);
    }

    private static IResult DatabaseUnavailable() =>
        Results.Json(new LobbyFailure("로비 데이터베이스를 사용할 수 없습니다."), statusCode: 503);
}

internal sealed record CreateRoomRequest(string? Name, bool IsPublic, string? Password);
internal sealed record JoinRoomRequest(string? Password);
internal sealed record ReadyRequest(bool Ready);
internal sealed record ChatRequest(string? Message);
internal sealed record TransferHostRequest(string? TargetUserId);
internal sealed record ConsumeGameTicketRequest(string? Ticket);
internal sealed record SubmitVerifiedMatchResultRequest(
    string? MatchId,
    string? UserId,
    string? Role,
    bool Success,
    int? CaptureCount,
    double? FirstCaptureSeconds,
    double? AllCapturedSeconds,
    int? RescueCount,
    double? EscapeSeconds);
internal sealed record AuthoritativeForfeitRequest(string? MatchId, string? UserId, string? Role);
internal sealed record SubmitLeaderboardRecordRequest(
    string? MatchId,
    string? Role,
    bool Success,
    int? CaptureCount,
    double? FirstCaptureSeconds,
    double? AllCapturedSeconds,
    int? RescueCount,
    double? EscapeSeconds);
internal sealed record LobbyFailure(string Message);
internal sealed record LobbyUser(ulong Id, string DisplayName);
internal sealed record Membership(string RoomId, bool IsHost, string Status);
internal sealed record RoomSummary(
    string RoomId, string JoinCode, string Name, string HostDisplayName, int MemberCount, int MaxPlayers, bool HasPassword);
internal sealed record RoomMember(
    string UserId, string DisplayName, bool IsHost, bool Ready, string AssignedRole, bool ReturnedToRoom);
internal sealed record RoomChatMessage(string MessageId, string DisplayName, string Message, string CreatedAtUtc);
internal sealed record RoomState(
    string RoomId,
    string JoinCode,
    string Name,
    bool IsHost,
    bool IsReady,
    bool CanStart,
    bool Started,
    bool ReturnedToRoom,
    string TravelUrl,
    string AssignedRole,
    IReadOnlyList<RoomMember> Members,
    IReadOnlyList<RoomChatMessage> ChatMessages);
internal sealed record RoomPasswordRecord(string RoomId);
internal sealed record LeaderboardEntry(
    int Rank,
    string DisplayName,
    string Role,
    double OverallScore,
    int? CaptureCount,
    double? FirstCaptureSeconds,
    double? AllCapturedSeconds,
    int? RescueCount,
    double? EscapeSeconds,
    string CreatedAtUtc);

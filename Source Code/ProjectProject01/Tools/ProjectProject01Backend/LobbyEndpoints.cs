// File: Tools/ProjectProject01Backend/LobbyEndpoints.cs
// Target: .NET 9 / MySQL Server 8.0

using System.Security.Cryptography;
using System.Net;
using System.Text;
using Microsoft.AspNetCore.Identity;
using Microsoft.AspNetCore.WebUtilities;
using MySqlConnector;

internal static partial class LobbyEndpoints
{
    private const int MaximumPlayers = 3;
    private const int StaleMemberSeconds = 60;
    private const int GameTicketLifetimeSeconds = 60;
    private static readonly TimeSpan GameServerHeartbeatMaximumAge = TimeSpan.FromSeconds(10);

    public static void MapProjectProject01Lobby(
        this WebApplication app,
        string configuredTravelUrl,
        ProjectSecurityOptions securityOptions,
        VersionCompatibilityOptions versionCompatibility,
        GameServerPresenceState gameServerPresence,
        GameServerDemandManager? demand=null)
    {
        var travelUrl = string.IsNullOrWhiteSpace(configuredTravelUrl)
            ? "127.0.0.1:7777"
            : configuredTravelUrl.Trim();
        bool CanLaunchOrConnect()=>demand?.Configured==true?demand.AcceptingNewMatches:gameServerPresence.IsAlive(GameServerHeartbeatMaximumAge);
        app.MapRandomMatchmaking(travelUrl,CanLaunchOrConnect,demand);
        app.MapBotMatches(CanLaunchOrConnect);
        app.MapCustomRoomRules(CanLaunchOrConnect);

        async Task<IResult> BeginRoomCountdownAsync(MySqlConnection connection, MySqlTransaction transaction,
            string roomId, ulong responseUserId, HttpRequest request, CancellationToken ct)
        {
            if(demand?.AcceptingNewMatches==true)
            {
                await ExecuteAsync(connection,transaction,"UPDATE room_members SET assigned_role=NULL,returned_to_room=FALSE WHERE room_id=@roomId;",("@roomId",roomId),ct);
                // No capacity reservation, process, role or join ticket before the countdown completes.
                string host=new Uri("http://"+ResolveTravelUrlForRequest(request,travelUrl)).Host;
                await ExecuteAsync(connection,transaction,"UPDATE game_rooms SET status='Starting',travel_url=@url,match_id=NULL,started_at_utc=UTC_TIMESTAMP(6) WHERE id=@roomId;",("@url",$"{host}:7777"),("@roomId",roomId),ct);
                await transaction.CommitAsync(ct);
                var countdown=await LoadCurrentRoomAsync(connection,null,responseUserId,CanLaunchOrConnect(),ct);
                return Results.Ok(new {message="게임 시작 카운트다운을 시작합니다.",room=countdown});
            }

            if (demand is {Configured:true,AcceptingNewMatches:false} || !gameServerPresence.IsAlive(GameServerHeartbeatMaximumAge))
            {
                await transaction.RollbackAsync(ct);
                return Results.Json(
                    new LobbyFailure("경기 세션 운영이 중지되었거나 종료 대기 중입니다. 신규 경기를 시작하지 않고 방을 유지합니다."),
                    statusCode: StatusCodes.Status409Conflict);
            }

            int? allocatedPort=await GameServerSlots.ReserveAsync(connection,transaction,roomId,ct);
            if(allocatedPort is null)
            {
                await transaction.RollbackAsync(ct);
                return Results.Conflict(new LobbyFailure("비어 있는 게임 서버가 없습니다. 다른 경기가 끝나거나 서버를 추가할 때까지 기다려주세요."));
            }
				var requestTravelUrl = ResolveTravelUrlForRequest(request, travelUrl);
            var serverUri=new Uri("http://"+requestTravelUrl);
            requestTravelUrl=$"{serverUri.Host}:{allocatedPort.Value}";
            await ExecuteAsync(connection, transaction,
                "UPDATE room_members SET assigned_role = NULL, returned_to_room = FALSE WHERE room_id = @roomId;",
                ("@roomId", roomId), ct);
            await ExecuteAsync(connection, transaction,
                "UPDATE game_rooms SET status = 'Starting', travel_url = @travelUrl, match_id = NULL, started_at_utc = UTC_TIMESTAMP(6) WHERE id = @roomId;",
                ("@travelUrl", requestTravelUrl), ("@roomId", roomId), ct);
            await transaction.CommitAsync(ct);
            var room = await LoadCurrentRoomAsync(
                connection, null, responseUserId,
                CanLaunchOrConnect(), ct);
            return Results.Ok(new { message = "게임 시작 카운트다운을 시작합니다.", room });
        }

        async Task<Membership?> LockStartMembershipAsync(MySqlConnection connection, MySqlTransaction transaction,
            ulong userId, CancellationToken ct)
        {
            var candidate=await LoadMembershipAsync(connection,transaction,userId,false,ct);
            if(candidate is null)return null;
            // Lock the common room before either member row; simultaneous ready requests
            // must not each hold their own member while waiting for the other.
            await using(var roomLock=new MySqlCommand("SELECT id FROM game_rooms WHERE id=@r FOR UPDATE",connection,transaction){CommandTimeout=5})
            {
                roomLock.Parameters.AddWithValue("@r",candidate.RoomId);
                if(await roomLock.ExecuteScalarAsync(ct) is null)return null;
            }
            var current=await LoadMembershipAsync(connection,transaction,userId,true,ct);
            return current?.RoomId==candidate.RoomId?current:null;
        }

        app.MapPost("/api/server/heartbeat", async (HttpRequest request,AuthDatabase database,CancellationToken ct) =>
        {
            if (!securityOptions.IsAuthorizedGameServer(
                    request.Headers["X-ProjectProject01-Server-Secret"].ToString()))
            {
                return Results.Unauthorized();
            }

            ServerHeartbeat body;
            try { body=await request.ReadFromJsonAsync<ServerHeartbeat>(ct)??new(); }
            catch(System.Text.Json.JsonException) { return Results.BadRequest(new {message="잘못된 서버 heartbeat입니다."}); }
            if(body.Port is <1024 or >65535 || string.IsNullOrWhiteSpace(body.BootId) || body.BootId.Length>64 ||
                (body.MatchId is not null && !Guid.TryParse(body.MatchId,out _))) return Results.BadRequest();
            if(body.ManagedId is not null ? demand is null||!demand.OwnsHeartbeat(body.Port,body.ManagedId) : demand?.HasManagedPort(body.Port)==true)
                return Results.Conflict(new {message="종료되었거나 알 수 없는 경기 세션입니다."});
            await using var connection=await database.OpenConnectionAsync(ct);
            await GameServerSlots.HeartbeatAsync(connection,body,ct);
            gameServerPresence.MarkAlive();
            return Results.Ok(new { message = "게임 서버 heartbeat를 확인했습니다." });
        }).RequireRateLimiting("game-server");

        app.MapPost("/api/server/offline", async (HttpRequest request,AuthDatabase database,CancellationToken ct) =>
        {
            if (!securityOptions.IsAuthorizedGameServer(
                    request.Headers["X-ProjectProject01-Server-Secret"].ToString()))
            {
                return Results.Unauthorized();
            }

            gameServerPresence.MarkOffline();
            await using var connection=await database.OpenConnectionAsync(ct);
            await using var offline=new MySqlCommand("UPDATE game_server_slots SET ready=FALSE,last_heartbeat_utc='2000-01-01';",connection);
            await offline.ExecuteNonQueryAsync(ct);
            return Results.Ok(new { message = "게임 서버 비활성 상태를 반영했습니다." });
        }).RequireRateLimiting("game-server");

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
                    SELECT r.id, r.join_code, r.name, u.display_name, SUM(NOT m.is_observer)+(SELECT COUNT(*) FROM room_bots b WHERE b.room_id=r.id) AS member_count,
                           r.password_hash IS NOT NULL AS has_password
                    FROM game_rooms r
                    INNER JOIN users u ON u.id = r.host_user_id
                    INNER JOIN room_members m ON m.room_id = r.id
                    WHERE r.status = 'Waiting' AND r.is_public = TRUE
                    GROUP BY r.id, r.name, u.display_name, r.created_at_utc
                    HAVING member_count < 3 OR (@observers AND SUM(m.is_observer)<2)
                    ORDER BY r.created_at_utc ASC
                    LIMIT 100;
                    """, connection) { CommandTimeout = 5 };
                command.Parameters.AddWithValue("@observers",LocalEditorTesting.ObserversEnabled(request));
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
                var room = await LoadCurrentRoomAsync(
                    connection, null, user.Id,
                    CanLaunchOrConnect(), ct);
                // Removed by the host: said once. In another room by now, the old removal is no longer news.
                if (room is not null)
                {
                    await CommunityEndpoints.Exec(connection, null, "DELETE FROM room_removal_notices WHERE user_id=@u", ct, ("u", user.Id));
                    return Results.Ok(new { message = "방 상태를 갱신했습니다.", room });
                }
                return await TakeRoomRemovalNoticeAsync(connection, user.Id, ct)
                    ? Results.NotFound(new { message = "방에서 강제 퇴장되었습니다.", removedByHost = true })
                    : Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
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
                int maximumRooms = await LockLobbyStateAsync(connection, transaction, ct);
                await CancelUserSearchesAsync(connection,transaction,user.Id,ct);
                await CleanupStaleRoomsAsync(connection, transaction, ct);
                if (await FindCurrentRoomIdAsync(connection, transaction, user.Id, ct) is not null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("이미 다른 방에 참가 중입니다."));
                }
                if (await CountRoomsAsync(connection, transaction, ct) >= maximumRooms)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure($"생성 가능한 방 {maximumRooms}개가 모두 사용 중입니다."));
                }
                if (await WaitingRoomNameExistsAsync(connection, transaction, name, ct))
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("이미 사용 중인 방 이름입니다. 다른 이름을 입력하세요."));
                }

                var roomId = Guid.NewGuid().ToString("D");
                var consent=await OpenParties.RequireAdmissionAsync(connection,transaction,user.Id,request,"Create","오픈매칭 · "+name,System.Text.Json.JsonSerializer.Serialize(new{kind="Create",name,body.IsPublic,password}),null,ct);
                if(consent is not null)return consent;
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
                if(!await OpenParties.AttachToRoomAsync(connection,transaction,user.Id,roomId,"Open",ct))return Results.Conflict(new LobbyFailure("파티장만 같은 모드에서 방을 만들 수 있습니다. 파티원의 방·접속 상태를 확인하세요."));
                await transaction.CommitAsync(ct);
                var room = await LoadCurrentRoomAsync(
                    connection, null, user.Id,
                    CanLaunchOrConnect(), ct);
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
                await LockLobbyStateAsync(connection,transaction,ct);
                await CleanupStaleRoomsAsync(connection, transaction, ct);
                if (await FindCurrentRoomIdAsync(connection, transaction, user.Id, ct) is not null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("이미 다른 방에 참가 중입니다."));
                }
                string? resolvedRoomId = null;
                bool joinAsObserver=false;
                int partySize=await OpenParties.AdmissionSizeAsync(connection,transaction,user.Id,"Open",ct);
                if(partySize<1)return Results.Conflict(new LobbyFailure("오픈매칭 파티장만 파티와 함께 방에 참가할 수 있습니다."));
                // Serialize explicit room entry with random-match formation and retire the old search.
                await CancelUserSearchesAsync(connection,transaction,user.Id,ct);
                if(await FindCurrentRoomIdAsync(connection,transaction,user.Id,ct) is not null)return Results.Conflict(new LobbyFailure("이미 다른 방에 참가 중입니다."));
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
                    if(await IsRandomPartyAsync(connection,transaction,resolvedRoomId!,ct))return Results.Conflict(new LobbyFailure("랜덤 매칭은 개인 신청만 가능합니다. 랜덤 매칭 화면을 이용하세요."));
                    if(await IsBotRoomAsync(connection,transaction,resolvedRoomId!,ct))return Results.Conflict(new LobbyFailure("봇전은 전용 화면에서 시작하는 개인 경기입니다."));
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
                    "SELECT (SELECT COUNT(*) FROM room_members WHERE room_id=@roomId AND is_observer=FALSE)+(SELECT COUNT(*) FROM room_bots WHERE room_id=@roomId);", connection, transaction) { CommandTimeout = 5 })
                {
                    countMembers.Parameters.AddWithValue("@roomId", resolvedRoomId!);
                    int playingCount=Convert.ToInt32(await countMembers.ExecuteScalarAsync(ct));
                    if(partySize>1&&playingCount+partySize>MaximumPlayers)return Results.Conflict(new LobbyFailure("파티 전원이 플레이할 자리가 부족합니다."));
                    if (playingCount >= MaximumPlayers)
                    {
                        if(!LocalEditorTesting.ObserversEnabled(request))return Results.Conflict(new LobbyFailure("방 인원이 가득 찼습니다."));
                        var observers=await CommunityEndpoints.Rows(connection,transaction,"SELECT user_id FROM room_members WHERE room_id=@r AND is_observer=TRUE",ct,("r",resolvedRoomId));
                        if(observers.Count>=2)return Results.Conflict(new LobbyFailure("플레이 3명·관전 2명 정원이 모두 찼습니다."));
                        joinAsObserver=true;
                    }
                }
                var consent=await OpenParties.RequireAdmissionAsync(connection,transaction,user.Id,request,"Join","오픈매칭 · "+normalizedRoomCode,System.Text.Json.JsonSerializer.Serialize(new{kind="Join",room=resolvedRoomId,code=normalizedRoomCode,password=body.Password?.Trim()??""}),resolvedRoomId,ct);
                if(consent is not null)return consent;
                await using (var insert = new MySqlCommand(
                    "INSERT INTO room_members (room_id, user_id, is_ready,is_observer) VALUES (@roomId, @userId, FALSE,@observer);",
                    connection, transaction) { CommandTimeout = 5 })
                {
                    insert.Parameters.AddWithValue("@roomId", resolvedRoomId!);
                    insert.Parameters.AddWithValue("@userId", user.Id);
                    insert.Parameters.AddWithValue("@observer",joinAsObserver);
                    await insert.ExecuteNonQueryAsync(ct);
                }
                if(!await OpenParties.AttachToRoomAsync(connection,transaction,user.Id,resolvedRoomId!,"Open",ct))return Results.Conflict(new LobbyFailure("파티원의 방·접속 상태 또는 역할 정원을 확인하세요. 전원 입장을 취소했습니다."));
                await transaction.CommitAsync(ct);
                var room = await LoadCurrentRoomAsync(
                    connection, null, user.Id,
                    CanLaunchOrConnect(), ct);
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
                var exit = await RemoveUserFromCurrentRoomAsync(connection, transaction, user.Id, ct);
                await transaction.CommitAsync(ct);
                return Results.Ok(new
                {
                    message = exit.HostTransferred
                        ? "방에서 나왔으며 가장 먼저 참가한 플레이어에게 방장 권한을 넘겼습니다."
                        : exit.RoomDeleted
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
                await LockLobbyStateAsync(connection, transaction, ct);
                var membership = await LoadMembershipAsync(connection, transaction, user.Id, true, ct);
                if (membership is null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                }
                if (!string.Equals(membership.Status, "Started", StringComparison.Ordinal))
                {
                    // A lost response may cause a return retry after this solo was already restored.
                    if (membership.Status == "Waiting" && await IsRandomPartyAsync(connection, transaction, membership.RoomId, ct))
                    {
                        var restored = await LoadCurrentRoomAsync(connection, transaction, user.Id, CanLaunchOrConnect(), ct);
                        await transaction.CommitAsync(ct);
                        return Results.Ok(new { message = "랜덤 매칭 대기 화면으로 복귀했습니다.", room = restored });
                    }
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("현재 진행 중인 경기가 없습니다."));
                }

                await ExecuteAsync(connection, transaction,
                    "UPDATE room_members SET returned_to_room = TRUE, is_ready = FALSE, last_seen_at_utc = UTC_TIMESTAMP(6) WHERE room_id = @roomId AND user_id = @userId;",
                    ("@roomId", membership.RoomId), ("@userId", user.Id), ct);
                var allReturned = await ResetStartedRoomIfAllMembersReturnedAsync(
                    connection, transaction, membership.RoomId, ct);
                await transaction.CommitAsync(ct);

                var room = await LoadCurrentRoomAsync(
                    connection, null, user.Id,
                    CanLaunchOrConnect(), ct);
                return room is null
                    ? Results.NotFound(new LobbyFailure("복귀할 방이 더 이상 존재하지 않습니다."))
                    : Results.Ok(new
                    {
                        message = room.RandomParty && !room.RandomMatch
                            ? "랜덤 매칭 대기 화면으로 복귀했습니다. 시작을 누르면 새로 매칭합니다."
                            : allReturned
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
                var room = await LoadCurrentRoomAsync(
                    connection, null, user.Id,
                    CanLaunchOrConnect(), ct);
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
                await using var transaction=await connection.BeginTransactionAsync(System.Data.IsolationLevel.ReadCommitted,ct);
                var membership = await LockStartMembershipAsync(connection, transaction, user.Id, ct);
                if (membership is null)
                {
                    return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                }
                bool duoBot=await IsBotRoomAsync(connection,transaction,membership.RoomId,ct)&&
                    (await CommunityEndpoints.Rows(connection,transaction,"SELECT user_id FROM room_members WHERE room_id=@r AND is_observer=FALSE",ct,("r",membership.RoomId))).Count==2;
                if (membership.IsHost&&!duoBot)
                {
                    return Results.BadRequest(new LobbyFailure("방장은 준비 대신 시작 버튼을 사용합니다."));
                }
                if (!string.Equals(membership.Status, "Waiting", StringComparison.Ordinal))
                {
                    return Results.Conflict(new LobbyFailure("이미 시작된 방의 준비 상태는 바꿀 수 없습니다."));
                }
                await ExecuteAsync(connection, transaction,
                    "UPDATE room_members SET is_ready = @ready, last_seen_at_utc = UTC_TIMESTAMP(6) WHERE room_id = @roomId AND user_id = @userId;",
                    ("@ready", body.Ready), ("@roomId", membership.RoomId), ("@userId", user.Id), ct);
                // Serialize the last ready, cancellation and duplicate requests under the room lock.
                // Only the separate two-human bot mode starts here; open rooms stay manual.
                if (duoBot && body.Ready &&
                    await HasCompletePlayingRosterAsync(connection,transaction,membership.RoomId,2,ct) &&
                    (await CommunityEndpoints.Rows(connection,transaction,
                        "SELECT user_id FROM room_members WHERE room_id=@r AND is_ready=FALSE FOR UPDATE",
                        ct,("r",membership.RoomId))).Count==0)
                {
                    return await BeginRoomCountdownAsync(connection,transaction,membership.RoomId,user.Id,request,ct);
                }
                await transaction.CommitAsync(ct);
                var room = await LoadCurrentRoomAsync(
                    connection, null, user.Id,
                    CanLaunchOrConnect(), ct);
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
                if (await IsRandomPartyAsync(connection, null, membership.RoomId, ct))
                {
                    return Results.Conflict(new LobbyFailure("랜덤 매칭에서는 채팅을 사용할 수 없습니다."));
                }
                if (!string.Equals(membership.Status, "Waiting", StringComparison.Ordinal))
                {
                    return Results.Conflict(new LobbyFailure("게임이 시작된 뒤에는 로비 채팅을 보낼 수 없습니다."));
                }
                await ExecuteAsync(connection, null,
                    "INSERT INTO room_chat_messages (room_id, user_id, message) VALUES (@roomId, @userId, @message);",
                    ("@roomId", membership.RoomId), ("@userId", user.Id), ("@message", message), ct);
                var room = await LoadCurrentRoomAsync(
                    connection, null, user.Id,
                    CanLaunchOrConnect(), ct);
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
                await using var transaction = await connection.BeginTransactionAsync(System.Data.IsolationLevel.ReadCommitted,ct);
                var membership = await LockStartMembershipAsync(connection, transaction, user.Id, ct);
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
                if(await IsRandomPartyAsync(connection,transaction,membership.RoomId,ct))return Results.Conflict(new LobbyFailure("랜덤 매칭 화면의 시작 버튼을 사용하세요."));
                await using (var selectMembers = new MySqlCommand(
                    "SELECT user_id, is_ready FROM room_members WHERE room_id = @roomId AND is_observer=FALSE ORDER BY joined_at_utc, user_id FOR UPDATE;",
                    connection, transaction) { CommandTimeout = 5 })
                {
                    selectMembers.Parameters.AddWithValue("@roomId", membership.RoomId);
                    await using var reader = await selectMembers.ExecuteReaderAsync(ct);
                    while (await reader.ReadAsync(ct))
                    {
                        members.Add((reader.GetUInt64(0), reader.GetBoolean(1)));
                    }
                }
                if (!await HasCompletePlayingRosterAsync(connection,transaction,membership.RoomId,members.Count,ct))
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("정확히 3명이 모여야 시작할 수 있습니다."));
                }
                bool hostRequiresReady=members.Count==2&&await IsBotRoomAsync(connection,transaction,membership.RoomId,ct);
                var notReady=await CommunityEndpoints.Rows(connection,transaction,"SELECT user_id FROM room_members WHERE room_id=@r AND is_ready=FALSE AND (user_id<>@u OR @hostReady=TRUE) FOR UPDATE",ct,("r",membership.RoomId),("u",user.Id),("hostReady",hostRequiresReady));
                if (notReady.Count>0)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("방장을 제외한 모든 플레이어가 준비해야 합니다."));
                }

                return await BeginRoomCountdownAsync(connection,transaction,membership.RoomId,user.Id,request,ct);
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/current/cancel-start", async (
            HttpRequest request, AuthDatabase database, CancellationToken ct) =>
        {
            var user = await AuthenticateAsync(request, database, ct);
            if (user is null)
            {
                return Results.Unauthorized();
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                await using var transaction = await connection.BeginTransactionAsync(System.Data.IsolationLevel.ReadCommitted,ct);
                var membership = await LockStartMembershipAsync(connection, transaction, user.Id, ct);
                if (membership is null)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.NotFound(new LobbyFailure("현재 참가 중인 방이 없습니다."));
                }
                if (!membership.IsHost)
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Json(new LobbyFailure("방장만 게임 시작을 취소할 수 있습니다."), statusCode: 403);
                }
                if (membership.Status is not ("Starting" or "Queued" or "Preparing"))
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("게임 시작을 취소할 수 있는 시간이 지났습니다."));
                }
                var randomStart=await CommunityEndpoints.Rows(connection,transaction,"SELECT room_id FROM random_match_rooms WHERE room_id=@r AND EXISTS(SELECT 1 FROM room_members WHERE room_id=@r AND assigned_role IS NOT NULL)",ct,("r",membership.RoomId));
                if(randomStart.Count>0)return Results.Conflict(new LobbyFailure("랜덤 매칭 3명이 확정되어 매칭 취소는 불가능합니다. 종료하려면 방 나가기를 이용하세요."));
                if(membership.Status=="Starting")
                await using (var cancelWindow = new MySqlCommand(
                    "SELECT started_at_utc IS NOT NULL AND started_at_utc > UTC_TIMESTAMP(6) - INTERVAL 5 SECOND FROM game_rooms WHERE id = @roomId FOR UPDATE;",
                    connection, transaction) { CommandTimeout = 5 })
                {
                    cancelWindow.Parameters.AddWithValue("@roomId", membership.RoomId);
                    if (!Convert.ToBoolean(await cancelWindow.ExecuteScalarAsync(ct)))
                    {
                        await transaction.RollbackAsync(ct);
                        return Results.Conflict(new LobbyFailure("게임 시작을 취소할 수 있는 5초가 지났습니다."));
                    }
                }

                await ResetStartingRoomAsync(connection, transaction, membership.RoomId, ct);
                if (await IsBotRoomAsync(connection,transaction,membership.RoomId,ct) &&
                    (await CommunityEndpoints.Rows(connection,transaction,"SELECT user_id FROM room_members WHERE room_id=@r AND is_observer=FALSE",ct,("r",membership.RoomId))).Count==2)
                {
                    await ExecuteAsync(connection,transaction,"UPDATE room_members SET is_ready=FALSE WHERE room_id=@r",("r",membership.RoomId),ct);
                }
                await transaction.CommitAsync(ct);
                var room = await LoadCurrentRoomAsync(
                    connection, null, user.Id,
                    CanLaunchOrConnect(), ct);
                return Results.Ok(new { message = "게임 시작이 취소되었습니다.", room });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        app.MapPost("/api/rooms/current/confirm-start", async (
            HttpRequest request, AuthDatabase database, CancellationToken ct) =>
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
                    return Results.Json(new LobbyFailure("방장만 게임 시작을 확정할 수 있습니다."), statusCode: 403);
                }
                if(demand?.Configured==true&&membership.Status is "Queued" or "Preparing" or "Started")
                {
                    await transaction.CommitAsync(ct);
                    var current=await LoadCurrentRoomAsync(connection,null,user.Id,CanLaunchOrConnect(),ct);
                    return Results.Ok(new {message="현재 게임 시작 상태를 확인했습니다.",room=current});
                }
                if (!string.Equals(membership.Status, "Starting", StringComparison.Ordinal))
                {
                    await transaction.RollbackAsync(ct);
                    return Results.Conflict(new LobbyFailure("확정할 게임 시작 카운트다운이 없습니다."));
                }
                await using (var confirmWindow = new MySqlCommand(
                    "SELECT started_at_utc IS NOT NULL AND started_at_utc <= UTC_TIMESTAMP(6) - INTERVAL 5 SECOND FROM game_rooms WHERE id = @roomId FOR UPDATE;",
                    connection, transaction) { CommandTimeout = 5 })
                {
                    confirmWindow.Parameters.AddWithValue("@roomId", membership.RoomId);
                    if (!Convert.ToBoolean(await confirmWindow.ExecuteScalarAsync(ct)))
                    {
                        await transaction.RollbackAsync(ct);
                        return Results.Conflict(new LobbyFailure("게임 시작 카운트다운이 아직 끝나지 않았습니다."));
                    }
                }
                if(demand?.Configured==true)
                {
                    if(!demand.AcceptingNewMatches)
                    {
                        await ResetStartingRoomAsync(connection,transaction,membership.RoomId,ct);
                        await transaction.CommitAsync(ct);
                        return Results.Conflict(new LobbyFailure("경기 세션 운영이 중지되어 시작을 취소했습니다. 방을 유지합니다."));
                    }
                    await ExecuteAsync(connection,transaction,"UPDATE game_rooms SET status='Queued',started_at_utc=UTC_TIMESTAMP(6) WHERE id=@roomId AND status='Starting';",("@roomId",membership.RoomId),ct);
                    await transaction.CommitAsync(ct);
                    var searching=await LoadCurrentRoomAsync(connection,null,user.Id,CanLaunchOrConnect(),ct);
                    return Results.Ok(new {message="세션 찾는 중… 빈 세션이 준비되면 자동으로 시작합니다.",room=searching});
                }
                if (!gameServerPresence.IsAlive(GameServerHeartbeatMaximumAge))
                {
                    await ResetStartingRoomAsync(connection, transaction, membership.RoomId, ct);
                    await transaction.CommitAsync(ct);
                    return Results.Json(
                        new LobbyFailure("게임 서버 연결이 끊겼습니다. 게임 시작을 취소하고 준비 상태로 돌아갑니다."),
                        statusCode: StatusCodes.Status409Conflict);
                }

                var memberIds = new List<ulong>();
                await using (var selectMembers = new MySqlCommand(
                    "SELECT user_id FROM room_members WHERE room_id = @roomId AND is_observer=FALSE ORDER BY joined_at_utc, user_id FOR UPDATE;",
                    connection, transaction) { CommandTimeout = 5 })
                {
                    selectMembers.Parameters.AddWithValue("@roomId", membership.RoomId);
                    await using var reader = await selectMembers.ExecuteReaderAsync(ct);
                    while (await reader.ReadAsync(ct))
                    {
                        memberIds.Add(reader.GetUInt64(0));
                    }
                }
                if (!await HasCompletePlayingRosterAsync(connection,transaction,membership.RoomId,memberIds.Count,ct))
                {
                    await ResetStartingRoomAsync(connection, transaction, membership.RoomId, ct);
                    await transaction.CommitAsync(ct);
                    return Results.Conflict(new LobbyFailure("인원이 변경되어 게임 시작을 취소했습니다."));
                }
                var matchId = Guid.NewGuid().ToString("D");
                if(!await GameServerSlots.ConfirmAsync(connection,transaction,membership.RoomId,matchId,ct))
                {
                    await ResetStartingRoomAsync(connection,transaction,membership.RoomId,ct);
                    await transaction.CommitAsync(ct);
                    return Results.Conflict(new LobbyFailure("예약된 게임 서버가 준비되지 않아 시작을 취소했습니다."));
                }
                await AssignPlayingRolesAsync(connection,transaction,membership.RoomId,memberIds,ct);
                await ExecuteAsync(connection, transaction,
                    "UPDATE game_rooms SET status = 'Started', match_id = @matchId, started_at_utc = UTC_TIMESTAMP(6) WHERE id = @roomId AND status = 'Starting';",
                    ("@matchId", matchId), ("@roomId", membership.RoomId), ct);
                await AuthoritativeMatchLedger.CaptureAsync(connection,transaction,matchId,ct);
                await transaction.CommitAsync(ct);
                var room = await LoadCurrentRoomAsync(
                    connection, null, user.Id,
                    gameServerPresence.IsAlive(GameServerHeartbeatMaximumAge), ct);
                return Results.Ok(new { message = "역할 배정을 완료했습니다. 게임 서버에 접속합니다.", room });
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
            if (demand is {Configured:true,Enabled:false} || !gameServerPresence.IsAlive(GameServerHeartbeatMaximumAge))
            {
                await SecurityAudit.WriteAsync(database, request, "GameTicketIssue", "GameServerOffline", user.Id, null, ct);
                return Results.Json(
                    new LobbyFailure("게임 서버 연결이 끊겼습니다. 접속 허가를 발급할 수 없습니다."),
                    statusCode: StatusCodes.Status503ServiceUnavailable);
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

                if(!await GameServerSlots.IsAssignedAsync(connection,transaction,roomId,matchId,null,ct))
                    return Results.Conflict(new LobbyFailure("이 경기에 배정된 게임 서버가 없습니다. 방으로 돌아가 다시 시작하세요."));

                if (!gameServerPresence.IsAlive(GameServerHeartbeatMaximumAge))
                {
                    await ExecuteAsync(connection, transaction,
                        """
                        UPDATE auth_sessions s
                        INNER JOIN room_members m ON m.user_id = s.user_id
                        SET s.revoked_at_utc = UTC_TIMESTAMP(6)
                        WHERE m.room_id = @roomId AND s.revoked_at_utc IS NULL;
                        """, ("@roomId", roomId), ct);
                    await ExecuteAsync(connection, transaction,
                        "DELETE FROM game_rooms WHERE id = @roomId;", ("@roomId", roomId), ct);
                    await transaction.CommitAsync(ct);
                    return Results.Json(
                        new LobbyFailure("게임 서버 연결이 끊겼습니다. 방과 로그인 세션을 안전하게 정리했습니다. 다시 로그인하세요."),
                        statusCode: StatusCodes.Status503ServiceUnavailable);
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
                    await using var bind=new MySqlCommand("INSERT INTO game_ticket_sessions(ticket_id,auth_session_id) SELECT @ticket,id FROM auth_sessions WHERE id=@session AND user_id=@user AND revoked_at_utc IS NULL AND access_expires_at_utc>UTC_TIMESTAMP(6);",connection,transaction);
                    bind.Parameters.AddWithValue("@ticket",insert.LastInsertedId);bind.Parameters.AddWithValue("@session",user.SessionId);bind.Parameters.AddWithValue("@user",user.Id);
                    if(await bind.ExecuteNonQueryAsync(ct)!=1)return Results.Unauthorized();
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
                ulong ticketId=0,userId=0;
                string displayName="",roomId="",matchId="",role="";
                await using (var select = new MySqlCommand(
                    """
                    SELECT t.id, t.user_id, u.display_name, t.room_id, t.match_id, t.role
                    FROM game_join_tickets t
                    INNER JOIN users u ON u.id = t.user_id
                    INNER JOIN game_ticket_sessions binding ON binding.ticket_id=t.id
                    INNER JOIN auth_sessions session ON session.id=binding.auth_session_id AND session.user_id=t.user_id
                    INNER JOIN room_members member ON member.user_id=t.user_id AND member.room_id=t.room_id
                    INNER JOIN game_rooms room ON room.id=t.room_id AND room.match_id=t.match_id
                    WHERE t.ticket_hash = @ticketHash
                      AND session.revoked_at_utc IS NULL AND session.access_expires_at_utc>UTC_TIMESTAMP(6)
                      AND NOT EXISTS(SELECT 1 FROM auth_session_liveness l WHERE l.session_id=session.id AND l.deadline_utc<=UTC_TIMESTAMP(6))
                      AND NOT EXISTS(SELECT 1 FROM account_restrictions restriction WHERE restriction.user_id=t.user_id AND restriction.lifted_at_utc IS NULL AND (restriction.expires_at_utc IS NULL OR restriction.expires_at_utc>UTC_TIMESTAMP(6)))
                      AND room.status='Started' AND member.returned_to_room=FALSE AND member.assigned_role=t.role
                      AND t.consumed_at_utc IS NULL
                      AND t.expires_at_utc > UTC_TIMESTAMP(6)
                    LIMIT 1
                    FOR UPDATE;
                    """, connection, transaction) { CommandTimeout = 5 })
                {
                    select.Parameters.Add("@ticketHash", MySqlDbType.Binary, 32).Value =
                        SHA256.HashData(Encoding.UTF8.GetBytes(ticket));
                    await using var reader = await select.ExecuteReaderAsync(ct);
                    if (await reader.ReadAsync(ct))
                    {
                        ticketId = reader.GetUInt64(0);
                        userId = reader.GetUInt64(1);
                        displayName = reader.GetString(2);
                        roomId = ReadRoomId(reader, 3);
                        matchId = ReadRoomId(reader, 4);
                        role = reader.GetString(5);
                    }
                }
                if(ticketId==0)
                {
                    await transaction.RollbackAsync(ct);
                    await SecurityAudit.WriteAsync(database,request,"GameTicketConsume","InvalidExpiredOrUsed",null,null,ct);
                    return Results.Unauthorized();
                }
                if(!await GameServerSlots.IsAssignedAsync(connection,transaction,roomId,matchId,GameServerSlots.RequestPort(request),ct))
                    return Results.Conflict(new LobbyFailure("이 티켓은 다른 게임 서버에 배정되었습니다."));
                await ExecuteAsync(connection, transaction,
                    "UPDATE game_join_tickets SET consumed_at_utc = UTC_TIMESTAMP(6) WHERE id = @ticketId;",
                    ("@ticketId", ticketId), ct);
                var roster=await LoadMatchBotsAsync(connection,transaction,matchId,ct);
                // Each bot arrives with its role/level pool from earlier matches (optional field).
                var pools=await BotExperience.LoadAsync(connection,transaction,roster,ct);
                var bots=roster.Select(b=>new{b.BotId,b.DisplayName,b.Difficulty,b.AssignedRole,
                    experience=pools.TryGetValue($"{b.AssignedRole}:{b.Difficulty}",out var pool)?pool:null}).ToList();
                var customRules=await CustomRoomRules.MatchAsync(connection,transaction,matchId,ct);
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
                    bots,
                    customRules,
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

        app.MapPost("/api/server/matches/complete", async (HttpRequest request,CompleteAuthoritativeMatchRequest body,AuthDatabase database,CancellationToken ct)=>
        {
            if(!securityOptions.IsAuthorizedGameServer(request.Headers["X-ProjectProject01-Server-Secret"].ToString()))return Results.Unauthorized();
            if(!Guid.TryParse(body.MatchId,out var match))return Results.BadRequest();
            try
            {
                await using var connection=await database.OpenConnectionAsync(ct);
                await using var transaction=await connection.BeginTransactionAsync(ct);
                await LockLobbyStateAsync(connection,transaction,ct);
                if(!await AuthoritativeMatchLedger.EndAsync(connection,transaction,match.ToString("D"),ct))return Results.NotFound();
                await BotExperience.ApplyAsync(connection,transaction,match.ToString("D"),body.BotExperience,ct);
                await transaction.CommitAsync(ct);return Results.Ok(new {message="경기 종료를 확인했습니다."});
            }
            catch(MySqlException){return DatabaseUnavailable();}
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
                await using var transaction=await connection.BeginTransactionAsync(ct);
                await LockLobbyStateAsync(connection,transaction,ct);
                await AuthoritativeMatchLedger.CaptureAsync(connection,transaction,matchId.ToString("D"),ct);
                string roomId;
                await using (var membership = new MySqlCommand(
                    """
                    SELECT a.room_id
                    FROM authoritative_matches a JOIN authoritative_match_players m ON m.match_id=a.match_id
                    WHERE a.match_id = @matchId AND m.user_id = @userId AND m.role = @role
                    LIMIT 1 FOR UPDATE;
                    """, connection,transaction) { CommandTimeout = 5 })
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

                // Database times use millisecond precision. Retries must be idempotent, not overwrite a verified result.
                static decimal? Seconds(double? value)=>value is null?null:Math.Round((decimal)value.Value,3,MidpointRounding.AwayFromZero);
                string digest=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(
                    System.Text.Json.JsonSerializer.Serialize(new {normalizedRole,body.Success,
                        Capture=normalizedRole=="Mannequin"&&body.Success?body.CaptureCount:null,
                        First=normalizedRole=="Mannequin"&&body.Success?Seconds(body.FirstCaptureSeconds):null,
                        All=normalizedRole=="Mannequin"&&body.Success?Seconds(body.AllCapturedSeconds):null,
                        Rescue=normalizedRole=="Survivor"&&body.Success?body.RescueCount:null,
                        Escape=normalizedRole=="Survivor"&&body.Success?Seconds(body.EscapeSeconds):null}))));
                await using(var receipt=new MySqlCommand("SELECT result_digest FROM authoritative_match_players WHERE match_id=@match AND user_id=@user FOR UPDATE;",connection,transaction))
                {
                    receipt.Parameters.AddWithValue("@match",matchId.ToString("D"));receipt.Parameters.AddWithValue("@user",userId);
                    object? previous=await receipt.ExecuteScalarAsync(ct);
                    if(previous is string known && known!=digest)return Results.Conflict(new LobbyFailure("이미 검증한 경기 결과와 다릅니다."));
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
                        match_id = VALUES(match_id);
                    """, connection,transaction) { CommandTimeout = 5 };
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
                await using(var receipt=new MySqlCommand("UPDATE authoritative_match_players SET result_digest=@digest WHERE match_id=@match AND user_id=@user;",connection,transaction))
                {receipt.Parameters.AddWithValue("@match",matchId.ToString("D"));receipt.Parameters.AddWithValue("@user",userId);receipt.Parameters.AddWithValue("@digest",digest);await receipt.ExecuteNonQueryAsync(ct);}
                await BotSkillRatings.ApplyAsync(connection,transaction,matchId.ToString("D"),userId,body.Success,ct);
                await CaseRecords.AwardVerifiedAsync(connection,transaction,matchId.ToString("D"),userId,ct);
                await AuthoritativeMatchLedger.EndAsync(connection,transaction,matchId.ToString("D"),ct);
                await transaction.CommitAsync(ct);
                if (normalizedRole == "Survivor" && body.Success)
                {
                    await RefreshExistingSurvivorTeamRecordAsync(
                        connection, matchId.ToString("D"), ct);
                }
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
				// Kept even when the room seat is already gone: the player still sees the desertion result.
				if (body.Desertion != false)
				{
					await MatchDesertions.RecordAsync(connection, transaction, matchId.ToString("D"), userId,
						NormalizeLeaderboardRole(body.Role ?? string.Empty) ?? "Survivor", ct);
				}
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
					await transaction.CommitAsync(ct);
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
            var normalizedScope = NormalizeLeaderboardScope(normalizedRole, request.Query["scope"].ToString());
            if (normalizedScope is null)
            {
                return Results.BadRequest(new LobbyFailure("생존자 팀 점수는 생존자 리더보드에서만 조회할 수 있습니다."));
            }
            // PvP (people only) and bot matches (any bot in the match: bot match, open match with bots,
            // random match filled with bots) are ranked separately. Default stays PvP for older clients.
            var category = request.Query["category"].ToString().Trim().ToLowerInvariant() switch
            {
                "" or "pvp" => "pvp",
                "bot" => "bot",
                _ => null
            };
            if (category is null)
            {
                return Results.BadRequest(new LobbyFailure("리더보드 구분은 pvp 또는 bot이어야 합니다."));
            }
            try
            {
                await using var connection = await database.OpenConnectionAsync(ct);
                var entries = normalizedScope == "team"
                    ? await LoadSurvivorTeamLeaderboardAsync(connection, normalizedSort, category == "bot", ct)
                    : await LoadLeaderboardAsync(connection, normalizedRole, normalizedSort, category == "bot", ct);
                return Results.Ok(new
                {
                    message = "리더보드 상위 50개 기록을 불러왔습니다.",
                    role = normalizedRole,
                    scope = normalizedScope,
                    sort = normalizedSort,
                    category,
                    entries
                });
            }
            catch (MySqlException)
            {
                return DatabaseUnavailable();
            }
        }).RequireRateLimiting("lobby");

        // Exact, account-scoped receipt lookup; never search a global top-50 list for publication evidence.
        app.MapGet("/api/leaderboards/records/{matchId}",async(string matchId,HttpRequest request,AuthDatabase database,CancellationToken ct)=>
        {
            var user=await AuthenticateAsync(request,database,ct);if(user is null)return Results.Unauthorized();
            if(!Guid.TryParse(matchId,out var match))return Results.BadRequest();
            try
            {
                await using var connection=await database.OpenConnectionAsync(ct);
                await using var command=new MySqlCommand("""
                    SELECT lr.id,lr.room_name,lr.role,tr.survivor_count,tr.average_rescue_count,tr.average_escape_seconds
                    FROM leaderboard_records lr LEFT JOIN leaderboard_survivor_team_records tr ON tr.match_id=lr.match_id
                    WHERE lr.match_id=@match AND lr.user_id=@user LIMIT 1;
                    """,connection);
                command.Parameters.AddWithValue("@match",match.ToString("D"));command.Parameters.AddWithValue("@user",user.Id);
                await using var reader=await command.ExecuteReaderAsync(ct);if(!await reader.ReadAsync(ct))return Results.NotFound();
                return Results.Ok(new {recordId=reader.GetUInt64(0).ToString(),matchId=match.ToString("D"),roomName=reader.GetString(1),role=reader.GetString(2),
                    survivorCount=reader.IsDBNull(3)?0:reader.GetInt32(3),teamRescueCount=reader.IsDBNull(4)?0:reader.GetDouble(4),teamEscapeSeconds=reader.IsDBNull(5)?0:reader.GetDouble(5)});
            }
            catch(MySqlException){return DatabaseUnavailable();}
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
                if(await CustomRoomRules.IsCustomAsync(connection,null,matchId.ToString("D"),ct))return Results.Conflict(new LobbyFailure("사용자 지정 경기는 공식 리더보드에 등록할 수 없습니다."));
                double? verifiedFirstCaptureSeconds = null;
                double? verifiedAllCapturedSeconds = null;
                int? verifiedRescueCount = null;
                double? verifiedEscapeSeconds = null;
                string verifiedRoomName = "알 수 없는 방";
                var hasVerifiedResult = false;
                await using (var verified = new MySqlCommand(
                    """
                    SELECT vr.capture_count, vr.first_capture_seconds, vr.all_captured_seconds,
                           vr.rescue_count, vr.escape_seconds, COALESCE(a.room_name,r.name, '알 수 없는 방')
                    FROM verified_match_results vr
                    LEFT JOIN authoritative_matches a ON a.match_id=vr.match_id
                    LEFT JOIN game_rooms r ON r.id = vr.room_id
                    WHERE vr.match_id = @matchId AND vr.user_id = @userId
                      AND vr.role = @role AND vr.success = TRUE
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
                        verifiedRoomName = reader.GetString(5);
                    }
                }

                var remoteIpAddress = request.HttpContext.Connection.RemoteIpAddress;
                var isLoopbackDevelopment = app.Environment.IsDevelopment() && remoteIpAddress is not null &&
                    System.Net.IPAddress.IsLoopback(remoteIpAddress) &&
                    !request.Headers.ContainsKey("X-Forwarded-For") && !request.Headers.ContainsKey("Forwarded") && !request.Headers.ContainsKey("X-Real-IP");
                if (!hasVerifiedResult && !(securityOptions.AllowUnverifiedLeaderboardSubmissions && isLoopbackDevelopment))
                {
                    await SecurityAudit.WriteAsync(database, request, "LeaderboardPublish", "UnverifiedResultRejected", user.Id,
                        $"MatchId={matchId:D}; Role={normalizedRole}", ct);
                    return Results.Json(
                        new LobbyFailure("서버가 검증한 현재 계정의 성공 경기 기록을 찾지 못했습니다."),
                        statusCode: StatusCodes.Status403Forbidden);
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
                await using var transaction = await connection.BeginTransactionAsync(ct);
                await using var command = new MySqlCommand(
                    """
                    INSERT INTO leaderboard_records
                    (match_id, user_id, room_name, role, capture_count, first_capture_seconds, all_captured_seconds, rescue_count, escape_seconds)
                    VALUES
                    (@matchId, @userId, @roomName, @role, @captureCount, @firstCaptureSeconds, @allCapturedSeconds, @rescueCount, @escapeSeconds);
                    """, connection, transaction) { CommandTimeout = 5 };
                command.Parameters.AddWithValue("@matchId", matchId.ToString("D"));
                command.Parameters.AddWithValue("@userId", user.Id);
                command.Parameters.AddWithValue("@roomName", verifiedRoomName);
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
                if (normalizedRole == "Survivor" && hasVerifiedResult)
                {
                    await PublishOrRefreshSurvivorTeamRecordAsync(
                        connection, transaction, matchId.ToString("D"), user.Id, verifiedRoomName, ct);
                }
                if (hasVerifiedResult)
                {
                    await using var markPublished = new MySqlCommand(
                        "UPDATE verified_match_results SET published_at_utc = UTC_TIMESTAMP(6) WHERE match_id = @matchId AND user_id = @userId;",
                        connection, transaction) { CommandTimeout = 5 };
                    markPublished.Parameters.AddWithValue("@matchId", matchId.ToString("D"));
                    markPublished.Parameters.AddWithValue("@userId", user.Id);
                    await markPublished.ExecuteNonQueryAsync(ct);
                }
                await transaction.CommitAsync(ct);
                await SecurityAudit.WriteAsync(database, request, "LeaderboardPublish", "Success", user.Id,
                    $"MatchId={matchId:D}; Role={normalizedRole}; Verified={hasVerifiedResult}", ct);
                return Results.Ok(new
                {
                    message = normalizedRole == "Survivor"
                        ? "개인 성공 기록과 생존자 팀 기록을 리더보드에 등록했습니다."
                        : "개인 성공 기록을 리더보드에 등록했습니다."
                });
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

    private static string? NormalizeLeaderboardScope(string role, string scope)
    {
        var normalized = string.IsNullOrWhiteSpace(scope) ? "individual" : scope.Trim().ToLowerInvariant();
        return normalized switch
        {
            "individual" => "individual",
            "team" when role == "Survivor" => "team",
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

    private static async Task PublishOrRefreshSurvivorTeamRecordAsync(
        MySqlConnection connection,
        MySqlTransaction transaction,
        string matchId,
        ulong submittedByUserId,
        string fallbackRoomName,
        CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            """
            INSERT INTO leaderboard_survivor_team_records
                (match_id, room_name, survivor_count, average_rescue_count,
                 average_escape_seconds, submitted_by_user_id)
            SELECT @matchId, COALESCE(MAX(a.room_name),MAX(r.name), @fallbackRoomName), COUNT(*),
                   SUM(vr.rescue_count), MAX(vr.escape_seconds), @submittedByUserId
            FROM verified_match_results vr
            LEFT JOIN authoritative_matches a ON a.match_id=vr.match_id
            LEFT JOIN game_rooms r ON r.id = vr.room_id
            WHERE vr.match_id = @matchId AND vr.role = 'Survivor' AND vr.success = TRUE
            HAVING COUNT(*) > 0
            ON DUPLICATE KEY UPDATE
                room_name = VALUES(room_name), survivor_count = VALUES(survivor_count),
                average_rescue_count = VALUES(average_rescue_count),
                average_escape_seconds = VALUES(average_escape_seconds),
                updated_at_utc = UTC_TIMESTAMP(6);
            """, connection, transaction) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@matchId", matchId);
        command.Parameters.AddWithValue("@fallbackRoomName", fallbackRoomName);
        command.Parameters.AddWithValue("@submittedByUserId", submittedByUserId);
        await command.ExecuteNonQueryAsync(ct);
    }

    private static async Task RefreshExistingSurvivorTeamRecordAsync(
        MySqlConnection connection,
        string matchId,
        CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            """
            UPDATE leaderboard_survivor_team_records team
            INNER JOIN
            (
                SELECT vr.match_id, COALESCE(MAX(a.room_name),MAX(r.name), '알 수 없는 방') AS room_name,
                       COUNT(*) AS survivor_count, SUM(vr.rescue_count) AS average_rescue_count,
                       MAX(vr.escape_seconds) AS average_escape_seconds
                FROM verified_match_results vr
                LEFT JOIN authoritative_matches a ON a.match_id=vr.match_id
                LEFT JOIN game_rooms r ON r.id = vr.room_id
                WHERE vr.match_id = @matchId AND vr.role = 'Survivor' AND vr.success = TRUE
                GROUP BY vr.match_id
            ) aggregate ON aggregate.match_id = team.match_id
            SET team.room_name = aggregate.room_name,
                team.survivor_count = aggregate.survivor_count,
                team.average_rescue_count = aggregate.average_rescue_count,
                team.average_escape_seconds = aggregate.average_escape_seconds,
                team.updated_at_utc = UTC_TIMESTAMP(6)
            WHERE team.match_id = @matchId;
            """, connection) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@matchId", matchId);
        await command.ExecuteNonQueryAsync(ct);
    }

    // PvP = people on BOTH sides: a human mannequin and at least one human survivor (a bot may fill the
    // remaining survivor seat). Everything else with bots is a bot match: two human survivors against a bot
    // mannequin, a human mannequin against bot survivors, one human with two bots. A match without any bot
    // roster was people only (also covers records made before the match ledger kept roles).
    private static string PvPMatchCondition(string matchColumn) =>
        $"""
        (NOT EXISTS (SELECT 1 FROM bot_match_rosters b WHERE b.match_id = {matchColumn})
         OR (EXISTS (SELECT 1 FROM authoritative_match_players p WHERE p.match_id = {matchColumn} AND p.role = 'Mannequin')
             AND EXISTS (SELECT 1 FROM authoritative_match_players p WHERE p.match_id = {matchColumn} AND p.role = 'Survivor')))
        """;
    private static string BotMatchFilter(string matchColumn, bool botMatches) =>
        $"({CustomRoomRules.OfficialFilter(matchColumn)}) AND ({(botMatches ? $"NOT {PvPMatchCondition(matchColumn)}" : PvPMatchCondition(matchColumn))})";

    private static async Task<IReadOnlyList<LeaderboardEntry>> LoadLeaderboardAsync(
        MySqlConnection connection, string role, string sort, bool botMatches, CancellationToken ct)
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
                SELECT lr.id AS record_id, u.display_name, lr.room_name, lr.role, lr.capture_count,
                       lr.first_capture_seconds, lr.all_captured_seconds, lr.rescue_count,
                       lr.escape_seconds, lr.created_at_utc,
                       {rankedColumns},
                       COUNT(*) OVER () AS total_count
                FROM leaderboard_records lr
                INNER JOIN users u ON u.id = lr.user_id
                WHERE lr.role = @role AND {BotMatchFilter("lr.match_id", botMatches)}
            ), scored AS
            (
                SELECT *, {averageExpression} AS average_rank
                FROM metric_ranks
            )
            SELECT record_id, display_name, room_name, role, capture_count, first_capture_seconds,
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
            var averageRank = Convert.ToDouble(reader.GetValue(10), System.Globalization.CultureInfo.InvariantCulture);
            var totalCount = reader.GetInt64(11);
            var overallScore = totalCount <= 1
                ? 100.0
                : Math.Clamp(100.0 * (1.0 - ((averageRank - 1.0) / (totalCount - 1.0))), 0.0, 100.0);
            entries.Add(new LeaderboardEntry(
                displayRank,
                reader.GetString(1),
                reader.GetString(2),
                reader.GetString(3),
                false,
                Math.Round(overallScore, 2),
                reader.IsDBNull(4) ? null : reader.GetInt32(4),
                reader.IsDBNull(5) ? null : Convert.ToDouble(reader.GetValue(5), System.Globalization.CultureInfo.InvariantCulture),
                reader.IsDBNull(6) ? null : Convert.ToDouble(reader.GetValue(6), System.Globalization.CultureInfo.InvariantCulture),
                reader.IsDBNull(7) ? null : reader.GetInt32(7),
                reader.IsDBNull(8) ? null : Convert.ToDouble(reader.GetValue(8), System.Globalization.CultureInfo.InvariantCulture),
                null,
                null,
                null,
                DateTime.SpecifyKind(reader.GetDateTime(9), DateTimeKind.Utc).ToString("O")));
        }
        return entries;
    }

    private static async Task<IReadOnlyList<LeaderboardEntry>> LoadSurvivorTeamLeaderboardAsync(
        MySqlConnection connection, string sort, bool botMatches, CancellationToken ct)
    {
        var orderBy = sort switch
        {
            "rescues" => "average_rescue_count DESC, average_escape_seconds ASC, record_id ASC",
            "escape" => "average_escape_seconds ASC, average_rescue_count DESC, record_id ASC",
            _ => "average_rank ASC, created_at_utc ASC, record_id ASC"
        };
        var sql = $"""
            WITH metric_ranks AS
            (
                SELECT tr.id AS record_id, tr.room_name, tr.survivor_count,
                       tr.average_rescue_count, tr.average_escape_seconds, tr.created_at_utc,
                       RANK() OVER (ORDER BY tr.average_rescue_count DESC, tr.average_escape_seconds ASC, tr.id ASC) AS rescue_rank,
                       RANK() OVER (ORDER BY tr.average_escape_seconds ASC, tr.average_rescue_count DESC, tr.id ASC) AS escape_rank,
                       COUNT(*) OVER () AS total_count
                FROM leaderboard_survivor_team_records tr
                WHERE {BotMatchFilter("tr.match_id", botMatches)}
            ), scored AS
            (
                SELECT *, (rescue_rank + escape_rank) / 2.0 AS average_rank
                FROM metric_ranks
            )
            SELECT record_id, room_name, survivor_count, average_rescue_count,
                   average_escape_seconds, created_at_utc, average_rank, total_count
            FROM scored
            ORDER BY {orderBy}
            LIMIT 50;
            """;

        var entries = new List<LeaderboardEntry>();
        await using var command = new MySqlCommand(sql, connection) { CommandTimeout = 5 };
        await using var reader = await command.ExecuteReaderAsync(ct);
        var displayRank = 0;
        while (await reader.ReadAsync(ct))
        {
            ++displayRank;
            var averageRank = Convert.ToDouble(reader.GetValue(6), System.Globalization.CultureInfo.InvariantCulture);
            var totalCount = reader.GetInt64(7);
            var overallScore = totalCount <= 1
                ? 100.0
                : Math.Clamp(100.0 * (1.0 - ((averageRank - 1.0) / (totalCount - 1.0))), 0.0, 100.0);
            entries.Add(new LeaderboardEntry(
                displayRank,
                "생존자 팀",
                reader.GetString(1),
                "Survivor",
                true,
                Math.Round(overallScore, 2),
                null,
                null,
                null,
                null,
                null,
                reader.GetInt32(2),
                Convert.ToDouble(reader.GetValue(3), System.Globalization.CultureInfo.InvariantCulture),
                Convert.ToDouble(reader.GetValue(4), System.Globalization.CultureInfo.InvariantCulture),
                DateTime.SpecifyKind(reader.GetDateTime(5), DateTimeKind.Utc).ToString("O")));
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
            SELECT u.id, u.display_name, s.id
            FROM auth_sessions s
            INNER JOIN users u ON u.id = s.user_id
            WHERE s.access_token_hash = @accessHash
              AND s.revoked_at_utc IS NULL
              AND s.access_expires_at_utc > UTC_TIMESTAMP(6)
              AND NOT EXISTS(SELECT 1 FROM auth_session_liveness l WHERE l.session_id=s.id AND l.deadline_utc<=UTC_TIMESTAMP(6))
            LIMIT 1;
            """, connection) { CommandTimeout = 5 };
        command.Parameters.Add("@accessHash", MySqlDbType.Binary, 32).Value = SHA256.HashData(Encoding.UTF8.GetBytes(token));
        await using var reader = await command.ExecuteReaderAsync(ct);
        return await reader.ReadAsync(ct) ? new LobbyUser(reader.GetUInt64(0), reader.GetString(1),reader.GetUInt64(2)) : null;
    }

    internal static async Task<RoomExitResult> RemoveUserFromCurrentRoomAsync(
        MySqlConnection connection,
        MySqlTransaction transaction,
        ulong userId,
        CancellationToken ct)
    {
        await CancelUserSearchesAsync(connection,transaction,userId,ct);
        await ExecuteAsync(connection,transaction,"DELETE FROM game_join_tickets WHERE user_id=@userId;",("@userId",userId),ct);
        var membership = await LoadMembershipAsync(connection, transaction, userId, true, ct);
        if (membership is null)
        {
            return new RoomExitResult(false, false, false);
        }

        var hostTransferred = false;
        var roomDeleted = false;
        if (membership.IsHost)
        {
            var successorUserId = await FindEarliestOtherMemberAsync(
                connection, transaction, membership.RoomId, userId, ct);
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
                    ("@roomId", membership.RoomId), ("@userId", userId), ct);
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
                ("@roomId", membership.RoomId), ("@userId", userId), ct);
        }

        if (!roomDeleted)
        {
            if (membership.Status is "Starting" or "Queued" or "Preparing")
            {
                await ResetStartingRoomAsync(connection, transaction, membership.RoomId, ct);
            }
            await ResetStartedRoomIfAllMembersReturnedAsync(connection, transaction, membership.RoomId, ct);
        }
        return new RoomExitResult(true, hostTransferred, roomDeleted);
    }

    private static async Task CleanupStaleRoomsAsync(
        MySqlConnection connection, MySqlTransaction? transaction, CancellationToken ct)
    {
        if (transaction is null)
        {
            // Polling paths (GET /api/rooms, /api/rooms/current): most calls find nothing stale. Check the exact
            // predicates of CleanupStaleRoomsCoreAsync with one read-only query first, so the common case takes no
            // row locks and runs no writes. Anything that is stale at this moment is still cleaned in this request.
            if (!await HasStaleRoomStateAsync(connection, ct))
            {
                return;
            }
            await using var ownedTransaction = await connection.BeginTransactionAsync(ct);
            await CleanupStaleRoomsCoreAsync(connection, ownedTransaction, ct);
            await ownedTransaction.CommitAsync(ct);
            return;
        }

        await CleanupStaleRoomsCoreAsync(connection, transaction, ct);
    }

    // Must stay in sync with the WHERE clauses of CleanupStaleRoomsCoreAsync below.
    internal static readonly string StaleRoomStateExistsSql =
        $"""
        SELECT
          EXISTS(SELECT 1 FROM game_rooms WHERE status = 'Starting' AND started_at_utc < UTC_TIMESTAMP(6) - INTERVAL 30 SECOND)
          OR EXISTS(SELECT 1 FROM game_rooms WHERE status = 'Preparing' AND started_at_utc < UTC_TIMESTAMP(6) - INTERVAL 90 SECOND)
          OR EXISTS(SELECT 1 FROM game_rooms WHERE status = 'Queued' AND started_at_utc < UTC_TIMESTAMP(6) - INTERVAL 120 SECOND)
          OR EXISTS(SELECT 1 FROM room_members m INNER JOIN game_rooms r ON r.id = m.room_id
                    WHERE r.status = 'Waiting' AND m.assigned_role IS NOT NULL)
          OR EXISTS(SELECT 1 FROM room_members m INNER JOIN game_rooms r ON r.id = m.room_id
                    WHERE r.status = 'Waiting' AND m.user_id <> r.host_user_id
                      AND m.last_seen_at_utc < UTC_TIMESTAMP(6) - INTERVAL {StaleMemberSeconds} SECOND)
          OR EXISTS(SELECT 1 FROM game_rooms r
                    LEFT JOIN room_members host_member ON host_member.room_id = r.id AND host_member.user_id = r.host_user_id
                    WHERE r.status = 'Waiting'
                      AND (host_member.user_id IS NULL OR host_member.last_seen_at_utc < UTC_TIMESTAMP(6) - INTERVAL {StaleMemberSeconds} SECOND));
        """;

    private static async Task<bool> HasStaleRoomStateAsync(MySqlConnection connection, CancellationToken ct)
    {
        await using var command = new MySqlCommand(StaleRoomStateExistsSql, connection) { CommandTimeout = 5 };
        return Convert.ToInt64(await command.ExecuteScalarAsync(ct)) != 0;
    }

    private static async Task CleanupStaleRoomsCoreAsync(
        MySqlConnection connection, MySqlTransaction transaction, CancellationToken ct)
    {
        await ExecuteAsync(connection, transaction,
            """
            UPDATE game_rooms
            SET status = 'Waiting', travel_url = NULL, match_id = NULL, started_at_utc = NULL
            WHERE status = 'Starting'
              AND started_at_utc < UTC_TIMESTAMP(6) - INTERVAL 30 SECOND;
            UPDATE game_rooms SET status='Waiting',travel_url=NULL,match_id=NULL,started_at_utc=NULL
            WHERE status='Preparing' AND started_at_utc<UTC_TIMESTAMP(6)-INTERVAL 90 SECOND;
            UPDATE game_rooms SET status='Waiting',travel_url=NULL,match_id=NULL,started_at_utc=NULL
            WHERE status='Queued' AND started_at_utc<UTC_TIMESTAMP(6)-INTERVAL 120 SECOND;
            """, ct);
        await ExecuteAsync(connection, transaction,
            """
            UPDATE room_members m
            INNER JOIN game_rooms r ON r.id = m.room_id
            SET m.assigned_role = NULL, m.returned_to_room = FALSE
            WHERE r.status = 'Waiting' AND m.assigned_role IS NOT NULL;
            """, ct);
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

    internal static async Task<int> LockLobbyStateAsync(MySqlConnection connection, MySqlTransaction transaction, CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            "SELECT max_rooms FROM lobby_state WHERE id = 1 FOR UPDATE;", connection, transaction) { CommandTimeout = 5 };
        return Convert.ToInt32(await command.ExecuteScalarAsync(ct));
    }

    private static async Task<int> CountRoomsAsync(MySqlConnection connection, MySqlTransaction transaction, CancellationToken ct)
    {
        await using var command = new MySqlCommand(
            "SELECT (SELECT COUNT(*) FROM game_rooms)+COALESCE((SELECT SUM(reserved) FROM (SELECT GREATEST(COUNT(DISTINCT party_key)-1,0) reserved FROM random_match_origins GROUP BY room_id) origins),0);", connection, transaction) { CommandTimeout = 5 };
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
            SELECT m.room_id, r.host_user_id = m.user_id AS is_host, r.status,m.is_observer
            FROM room_members m
            INNER JOIN game_rooms r ON r.id = m.room_id
            WHERE m.user_id = @userId
            LIMIT 1
            """ + (forUpdate ? " FOR UPDATE;" : ";");
        await using var command = new MySqlCommand(sql, connection, transaction) { CommandTimeout = 5 };
        command.Parameters.AddWithValue("@userId", userId);
        await using var reader = await command.ExecuteReaderAsync(ct);
        return await reader.ReadAsync(ct)
            ? new Membership(ReadRoomId(reader, 0), reader.GetBoolean(1), reader.GetString(2),reader.GetBoolean(3))
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

    internal static async Task<bool> ResetStartedRoomIfAllMembersReturnedAsync(
        MySqlConnection connection,
        MySqlTransaction transaction,
        string roomId,
        CancellationToken ct)
    {
        // Client return flags are not authority to terminate an active match.
        await using(var ended=new MySqlCommand("SELECT 1 FROM game_rooms r JOIN authoritative_matches a ON a.match_id=r.match_id WHERE r.id=@room AND r.status='Started' AND a.ended_at_utc IS NOT NULL FOR UPDATE;",connection,transaction))
        {ended.Parameters.AddWithValue("@room",roomId);if(await ended.ExecuteScalarAsync(ct) is null)return false;}
        if (await RestoreReturnedRandomPlayersAsync(connection, transaction, roomId, ct)) return true;
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
            "UPDATE room_members SET is_ready = FALSE, assigned_role = NULL, returned_to_room = FALSE WHERE room_id = @roomId; UPDATE room_bots SET assigned_role=NULL WHERE room_id=@roomId;",
            ("@roomId", roomId), ct);
        await ExecuteAsync(connection, transaction,
            "UPDATE game_rooms SET status = 'Waiting', travel_url = NULL, match_id = NULL, started_at_utc = NULL WHERE id = @roomId AND status = 'Started';",
            ("@roomId", roomId), ct);
        await RestoreRandomPartiesAsync(connection,transaction,roomId,ct);
        return true;
    }

    private static async Task ResetStartingRoomAsync(
        MySqlConnection connection,
        MySqlTransaction transaction,
        string roomId,
        CancellationToken ct)
    {
        await ExecuteAsync(connection, transaction,
            "UPDATE game_server_slots SET assigned_room_id=NULL,assigned_match_id=NULL,has_started=FALSE WHERE assigned_room_id=@roomId;",
            ("@roomId",roomId),ct);
        await ExecuteAsync(connection, transaction,
            "DELETE FROM game_join_tickets WHERE room_id = @roomId;", ("@roomId", roomId), ct);
        await ExecuteAsync(connection, transaction,
            "UPDATE room_members SET assigned_role = NULL, returned_to_room = FALSE WHERE room_id = @roomId; UPDATE room_bots SET assigned_role=NULL WHERE room_id=@roomId;",
            ("@roomId", roomId), ct);
        await ExecuteAsync(connection, transaction,
            "UPDATE game_rooms SET status = 'Waiting', travel_url = NULL, match_id = NULL, started_at_utc = NULL WHERE id = @roomId AND status IN ('Starting','Queued','Preparing');",
            ("@roomId", roomId), ct);
        await RestoreRandomPartiesAsync(connection,transaction,roomId,ct);
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
        MySqlConnection connection,
        MySqlTransaction? transaction,
        ulong userId,
        bool gameServerOnline,
        CancellationToken ct)
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
        string matchId;
        int sessionWaitElapsedSeconds;
        await using (var roomCommand = new MySqlCommand(
            """
            SELECT r.id, r.join_code, r.name, r.status, r.travel_url, r.host_user_id = m.user_id AS is_host,
                   m.is_ready, m.assigned_role, m.returned_to_room, r.match_id,
                   IF(r.status IN ('Queued','Preparing'),GREATEST(0,COALESCE(TIMESTAMPDIFF(SECOND,r.started_at_utc,UTC_TIMESTAMP(6)),0)),0)
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
            matchId = reader.IsDBNull(9) ? string.Empty : ReadRoomId(reader, 9);
            sessionWaitElapsedSeconds = reader.GetInt32(10);
        }

        var members = new List<RoomMember>();
        await using (var memberCommand = new MySqlCommand(
            """
            SELECT m.user_id, u.display_name, r.host_user_id = m.user_id AS is_host,
                   m.is_ready, m.assigned_role, m.returned_to_room,m.is_observer,COALESCE(banner.item_id,'') banner_id
            FROM room_members m
            INNER JOIN users u ON u.id = m.user_id
            INNER JOIN game_rooms r ON r.id = m.room_id
            LEFT JOIN case_loadout banner ON banner.user_id=m.user_id AND banner.slot='Banner'
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
                    reader.IsDBNull(4) ? string.Empty : reader.GetString(4), reader.GetBoolean(5),reader.GetBoolean(6),reader.GetString(7)));
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
                  AND NOT EXISTS (SELECT 1 FROM random_match_parties p WHERE p.room_id = @roomId)
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

        var bots=await LoadRoomBotsAsync(connection,transaction,roomId,ct);
        var botMatch=await IsBotRoomAsync(connection,transaction,roomId,ct);
        bool hostRequiresReady=botMatch&&members.Count(m=>!m.IsObserver)==2;
        var canStart = isHost && members.Count>0 && members.Count(m=>!m.IsObserver)+bots.Count == MaximumPlayers &&
            members.Where(member => !member.IsHost||hostRequiresReady).All(member => member.Ready) &&
            string.Equals(status, "Waiting", StringComparison.Ordinal) &&
            gameServerOnline;
        var random=await CommunityEndpoints.Rows(connection,transaction,"SELECT room_id FROM random_match_rooms WHERE room_id=@r",ct,("r",roomId));
        if(random.Count>0)members=members.Select(m=>m with{AssignedRole=m.UserId==userId.ToString()?m.AssignedRole:string.Empty}).ToList();
        var party=await CommunityEndpoints.Rows(connection,transaction,"SELECT p.search_id FROM random_match_parties p LEFT JOIN random_match_searches q ON q.search_id=p.search_id AND q.state='Waiting' WHERE p.room_id=@r",ct,("r",roomId));
        return new RoomState(
            roomId, joinCode, name, isHost, isReady, canStart, gameServerOnline,
            string.Equals(status, "Starting", StringComparison.Ordinal),
            string.Equals(status, "Started", StringComparison.Ordinal),
            returnedToRoom, travelUrl ?? string.Empty, assignedRole ?? string.Empty, members, chatMessages,status is "Queued" or "Preparing",
            random.Count>0&&status!="Waiting"&&!string.IsNullOrEmpty(assignedRole),party.Count>0,party.Count>0?Convert.ToString(party[0]["search_id"])??"":"",matchId,
            status is "Queued" or "Preparing" ? status : string.Empty,sessionWaitElapsedSeconds,bots,botMatch,members.Any(m=>m.UserId==userId.ToString()&&m.IsObserver),await CustomRoomRules.RoomAsync(connection,transaction,roomId,ct));
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
// Desertion: the player left while still in play (null from an older server counts as one). False only releases the seat.
internal sealed record AuthoritativeForfeitRequest(string? MatchId, string? UserId, string? Role, bool? Desertion = null);
internal sealed record CompleteAuthoritativeMatchRequest(string? MatchId,List<BotExperienceUpload>? BotExperience=null);
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
internal sealed record LobbyUser(ulong Id, string DisplayName,ulong SessionId);
internal sealed record Membership(string RoomId, bool IsHost, string Status,bool IsObserver=false);
internal sealed record RoomExitResult(bool HadMembership, bool HostTransferred, bool RoomDeleted);
internal sealed record RoomSummary(
    string RoomId, string JoinCode, string Name, string HostDisplayName, int MemberCount, int MaxPlayers, bool HasPassword);
internal sealed record RoomMember(
    string UserId, string DisplayName, bool IsHost, bool Ready, string AssignedRole, bool ReturnedToRoom,bool IsObserver=false,string BannerId="");
internal sealed record RoomChatMessage(string MessageId, string DisplayName, string Message, string CreatedAtUtc);
internal sealed record RoomState(
    string RoomId,
    string JoinCode,
    string Name,
    bool IsHost,
    bool IsReady,
    bool CanStart,
    bool GameServerOnline,
    bool Starting,
    bool Started,
    bool ReturnedToRoom,
    string TravelUrl,
    string AssignedRole,
    IReadOnlyList<RoomMember> Members,
    IReadOnlyList<RoomChatMessage> ChatMessages,
    bool ServerPreparing=false,
    bool RandomMatch=false,
    bool RandomParty=false,
    string MatchSearchId="",
    string MatchId="",
    string SessionWaitPhase="",
    int SessionWaitElapsedSeconds=0,
    IReadOnlyList<MatchBot>? Bots=null,
    bool BotMatch=false,
    bool IsObserver=false,
    CustomRuleSet? CustomRules=null);
internal sealed record RoomPasswordRecord(string RoomId);
internal sealed record LeaderboardEntry(
    int Rank,
    string DisplayName,
    string RoomName,
    string Role,
    bool IsTeam,
    double OverallScore,
    int? CaptureCount,
    double? FirstCaptureSeconds,
    double? AllCapturedSeconds,
    int? RescueCount,
    double? EscapeSeconds,
    int? SurvivorCount,
    double? AverageRescueCount,
    double? AverageEscapeSeconds,
    string CreatedAtUtc);

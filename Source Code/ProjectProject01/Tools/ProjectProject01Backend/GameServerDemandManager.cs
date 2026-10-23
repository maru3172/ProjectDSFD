using System.Diagnostics;
using System.Net.NetworkInformation;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using MySqlConnector;

internal sealed record GameServerDemandConfiguration(bool Enabled,string Executable="",string Arguments="MultiplayTest -log",int FirstPort=7777,int MaximumConcurrent=1,string ProjectRoot="",bool EditorLocalTest=false,string EditorBackendUrl="http://127.0.0.1:5080");
internal sealed record GameServerDemandCapacity(int MaximumConcurrent);
internal sealed record GameServerDrainRequest(bool Cancel=false);
internal interface IManagedGameProcess : IDisposable { bool HasExited {get;} void RequestStop(); void Stop(); }
internal interface IGameServerProcessFactory
{
    bool IsPortAvailable(int port);
    IManagedGameProcess Start(GameServerDemandConfiguration configuration,int port,string instanceId,string secret);
}
internal sealed class GameServerProcessFactory : IGameServerProcessFactory
{
    public bool IsPortAvailable(int port)=>!IPGlobalProperties.GetIPGlobalProperties().GetActiveUdpListeners().Any(p=>p.Port==port);
    public IManagedGameProcess Start(GameServerDemandConfiguration configuration,int port,string instanceId,string secret)
    {
        string logs=Path.Combine(configuration.ProjectRoot,"Saved","GameServerSessions");Directory.CreateDirectory(logs);
        string log=Path.Combine(logs,$"Session-{port}-{instanceId}.log");
        string stopFile=Path.ChangeExtension(log,".stop");
        string arguments=Regex.Replace(configuration.Arguments,@"(?i)(?<!\S)-port=\d+","").Trim();
        string outbox=Path.Combine(configuration.ProjectRoot,"Saved","MatchResultOutbox");
        var start=new ProcessStartInfo(configuration.Executable,$"{arguments} -port={port} -ProjectProject01ManagedId={instanceId} -ProjectProject01ManagedStopFile=\"{stopFile}\" -ProjectProject01ResultOutbox=\"{outbox}\" -abslog=\"{log}\"")
        {WorkingDirectory=Path.GetDirectoryName(configuration.Executable)!,UseShellExecute=false,CreateNoWindow=true};
        start.Environment["PROJECTPROJECT01_GAME_SERVER_SECRET"]=secret;
        return new ManagedProcess(Process.Start(start)??throw new InvalidOperationException("게임 서버 실행 실패"),stopFile);
    }
    private sealed class ManagedProcess(Process process,string stopFile) : IManagedGameProcess
    {
        public bool HasExited=>process.HasExited;
        public void RequestStop()
        {
            if(process.HasExited)return;
            try{File.WriteAllText(stopFile,"stop");}
            catch(IOException){}catch(UnauthorizedAccessException){} // Stop still has a bounded forced-exit fallback.
        }
        public void Stop(){if(!process.HasExited){RequestStop();if(!process.WaitForExit(5000)){process.Kill(true);process.WaitForExit(1000);}}}
        public void Dispose()
        {
            try{if(process.HasExited&&File.Exists(stopFile))File.Delete(stopFile);}
            catch(IOException){}catch(UnauthorizedAccessException){}
            finally{process.Dispose();}
        }
    }
}

// The backend stays running; Unreal processes exist only for requested/active match sessions.
internal sealed class GameServerDemandManager(AuthDatabase database,ProjectSecurityOptions security,
    IGameServerProcessFactory processes,ILogger<GameServerDemandManager> logger,TimeProvider clock) : BackgroundService
{
    private readonly object gate=new();
    private readonly SemaphoreSlim cycle=new(1,1);
    private readonly Dictionary<string,Entry> entries=[];
    private GameServerDemandConfiguration configuration=new(false);
    private bool draining,drainCompleted;
    private sealed class Entry(string room,int port,DateTimeOffset created)
    {
        public string Room=room,Instance=Guid.NewGuid().ToString("N");public int Port=port;
        public DateTimeOffset Created=created;public bool Committed,ObservedMatch;
        public DateTimeOffset LastHealthy=created;
        public IManagedGameProcess? Process;
    }
    internal bool Enabled {get{lock(gate)return configuration.Enabled;}}
    internal bool AcceptingNewMatches {get{lock(gate)return configuration.Enabled&&!draining;}}
    internal bool Configured {get{lock(gate)return !string.IsNullOrEmpty(configuration.Executable);}}
    internal bool EditorLocalTestActive {get{lock(gate)return configuration.Enabled&&configuration.EditorLocalTest;}}
    internal string? ResultOutboxDirectory {get{lock(gate)return string.IsNullOrWhiteSpace(configuration.ProjectRoot)?null:Path.Combine(configuration.ProjectRoot,"Saved","MatchResultOutbox");}}
    internal object Snapshot(){lock(gate)return new {enabled=configuration.Enabled,editorLocalTest=configuration.EditorLocalTest,draining,drainCompleted,acceptingNewMatches=configuration.Enabled&&!draining,maximumConcurrent=configuration.MaximumConcurrent,activeSessions=entries.Count,firstPort=configuration.FirstPort,
        sessions=entries.Values.Select(e=>new {roomId=e.Room,port=e.Port,phase=e.ObservedMatch?"Playing":"Preparing"}).ToArray()};}
    internal bool OwnsHeartbeat(int port,string instance){lock(gate)return entries.Values.Any(e=>e.Port==port&&e.Instance==instance&&e.Committed);}
    internal bool HasManagedPort(int port){lock(gate)return entries.Values.Any(e=>e.Port==port);}
    internal async Task ReconcileStartupAsync(CancellationToken ct)
    {
        await using var c=await database.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
        await LobbyEndpoints.LockLobbyStateAsync(c,tx,ct);
        // No in-memory owner survives a backend restart. Abort interrupted sessions, not waiting rooms.
        await CommunityEndpoints.Exec(c,tx,"""
            UPDATE auth_sessions s JOIN room_members m ON m.user_id=s.user_id JOIN game_rooms r ON r.id=m.room_id
            SET s.revoked_at_utc=UTC_TIMESTAMP(6) WHERE r.status<>'Waiting' AND s.revoked_at_utc IS NULL;
            DELETE FROM game_rooms WHERE status<>'Waiting';
            UPDATE game_server_slots SET ready=FALSE,last_heartbeat_utc='2000-01-01',reported_match_id=NULL,assigned_room_id=NULL,assigned_match_id=NULL,has_started=FALSE;
            """,ct);
        await tx.CommitAsync(ct);
        logger.LogInformation("백엔드 시작 복구: 소유권을 잃은 진행/준비 경기 정리, 대기방 보존");
    }
    internal sealed class Reservation(GameServerDemandManager owner,string room,int port) : IDisposable
    {
        public int Port {get;}=port;private bool committed;
        public void Commit(){lock(owner.gate){if(owner.entries.TryGetValue(room,out var entry)){entry.Committed=true;committed=true;}}}
        public void Dispose(){if(!committed)lock(owner.gate)owner.entries.Remove(room);}
    }
    internal Reservation? Reserve(string room,int roomLimit)
    {
        lock(gate)
        {
            if(!configuration.Enabled||draining||entries.Count>=Math.Min(configuration.MaximumConcurrent,roomLimit)||entries.ContainsKey(room))return null;
            int? port=Enumerable.Range(configuration.FirstPort,configuration.MaximumConcurrent)
                .Where(p=>!entries.Values.Any(e=>e.Port==p)&&processes.IsPortAvailable(p)).Cast<int?>().FirstOrDefault();
            if(port is null)return null;
            entries.Add(room,new(room,port.Value,clock.GetUtcNow()));return new(this,room,port.Value);
        }
    }
    internal async Task ConfigureAsync(GameServerDemandConfiguration value,CancellationToken ct)
    {
        if(value.Enabled)
        {
            if(!security.IsGameServerSecurityConfigured)throw new InvalidOperationException("게임 서버 보안 설정이 없습니다.");
            if(value.MaximumConcurrent is <1 or >100||value.FirstPort<1024||value.FirstPort+value.MaximumConcurrent-1>65535)throw new InvalidOperationException("동시 경기 한도와 포트 범위를 확인하세요.");
            string path=Path.GetFullPath(value.Executable);
            if(value.EditorLocalTest)
            {
                string project=Path.Combine(Path.GetFullPath(value.ProjectRoot),"ProjectDFSD.uproject");
                if(!File.Exists(path)||!Path.GetFileName(path).Equals("UnrealEditor-Cmd.exe",StringComparison.OrdinalIgnoreCase)||!File.Exists(project))
                    throw new InvalidOperationException("로컬 에디터 테스트에는 UnrealEditor-Cmd.exe와 ProjectDFSD.uproject가 필요합니다.");
                // Never accept arbitrary editor switches from a client. The operator's
                // authenticated local setup selects the project; all server flags are fixed.
                if(project.Contains('"')||project.Contains('\n')||project.Contains('\r'))throw new InvalidOperationException("올바르지 않은 프로젝트 경로입니다.");
                if(!Uri.TryCreate(value.EditorBackendUrl,UriKind.Absolute,out var backend)||backend.Scheme!="http"||backend.Host!="127.0.0.1"||backend.AbsolutePath!="/"||backend.Query!=""||backend.Fragment!=""||backend.UserInfo!="")
                    throw new InvalidOperationException("에디터 서버의 백엔드는 HTTP 127.0.0.1 루프백 주소만 사용할 수 있습니다.");
                value=value with {Arguments=$"\"{project}\" /Game/MyProject/Level/MultiplayTest -server -game -unattended -nosound -nullrhi -NoSplash -log -ini:Game:[/Script/ProjectProject01.ProjectProject01GameInstance]:SecurityApiBaseUrl={backend.GetLeftPart(UriPartial.Authority)}"};
            }
            else if(!File.Exists(path)||!(Path.GetFileName(path).Equals("ProjectDFSDServer.exe",StringComparison.OrdinalIgnoreCase)||Path.GetFileName(path).Equals("ProjectProject01Server.exe",StringComparison.OrdinalIgnoreCase)))throw new InvalidOperationException("패키징된 ProjectDFSDServer.exe를 지정하세요.");
            if(value.Arguments.Contains("ProjectProject01Managed",StringComparison.OrdinalIgnoreCase)||value.Arguments.Contains("ProjectProject01ResultOutbox",StringComparison.OrdinalIgnoreCase)||value.Arguments.Contains("-abslog",StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("세션 식별자와 종료/로그/결과 경로는 자동 지정됩니다.");
            value=value with {Executable=path,ProjectRoot=Path.GetFullPath(value.ProjectRoot)};
        }
        await cycle.WaitAsync(ct);
        try
        {
            lock(gate)
            {
                if(value.Enabled&&draining)throw new InvalidOperationException("경기 종료 대기 중입니다. 먼저 종료 대기를 취소하세요.");
                if(!value.Enabled)value=configuration with {Enabled=false};
                if(value.Enabled&&configuration==value)return;
                if(value.Enabled&&entries.Count>0)throw new InvalidOperationException("진행 중인 세션이 있습니다. 운영을 중지한 뒤 설정을 변경하세요.");
                configuration=value;
                draining=false;drainCompleted=false;
                if(!value.Enabled)foreach(var entry in entries.Values)entry.Process?.RequestStop();
            }
        }
        finally{cycle.Release();}
    }
    internal async Task DrainAsync(bool cancel,CancellationToken ct)
    {
        await cycle.WaitAsync(ct);
        try
        {
            lock(gate)
            {
                if(cancel)
                {
                    if(!configuration.Enabled)throw new InvalidOperationException("운영 중지가 이미 완료되었습니다. 게임 서버 운영 시작을 다시 누르세요.");
                    draining=false;drainCompleted=false;
                }
                else if(configuration.Enabled){draining=true;drainCompleted=false;}
                else {draining=false;drainCompleted=true;}
            }
        }
        finally{cycle.Release();}
    }
    internal async Task UpdateCapacityAsync(int maximum,CancellationToken ct)
    {
        await cycle.WaitAsync(ct);
        try
        {
            lock(gate)
            {
                if(maximum is <1 or >100||configuration.FirstPort+maximum-1>65535)
                    throw new InvalidOperationException("동시 경기 한도(1~100)와 포트 범위를 확인하세요.");
                if(maximum==configuration.MaximumConcurrent)return;
                if(entries.Count>0)throw new InvalidOperationException("준비 또는 진행 중인 경기가 있어 한도를 변경하지 않았습니다. 모든 경기 세션이 종료된 뒤 다시 적용하세요.");
                // Limit-only edits must neither enable operation nor replace executable/arguments.
                configuration=configuration with {MaximumConcurrent=maximum};
            }
        }
        finally{cycle.Release();}
    }
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer=new PeriodicTimer(TimeSpan.FromSeconds(1));
        try{while(await timer.WaitForNextTickAsync(stoppingToken))await TickAsync(stoppingToken);}
        catch(OperationCanceledException) when(stoppingToken.IsCancellationRequested){}
    }
    internal async Task TickAsync(CancellationToken ct)
    {
        await cycle.WaitAsync(ct);
        try
        {
            // Queue allocation is serialized with configuration and cleanup. No polling client can
            // jump the queue or create duplicate processes by retrying start/confirm requests.
            try{await AllocateQueuedRoomsAsync(ct);}
            catch(MySqlException ex){logger.LogWarning(ex,"경기 세션 대기열 DB 확인 실패; 다음 주기에 재시도합니다.");}
            Entry[] active;GameServerDemandConfiguration settings;
            lock(gate){active=entries.Values.Where(e=>e.Committed).ToArray();settings=configuration;}
            foreach(var entry in active)
            {
                try
                {
                    if(!settings.Enabled){await RemoveAsync(entry,true,ct);continue;}
                    await using var connection=await database.OpenConnectionAsync(ct);
                    string? status;bool ready=false,healthy=false;string? reported=null;
                    await using(var select=new MySqlCommand("SELECT r.status,s.ready AND s.last_heartbeat_utc>UTC_TIMESTAMP(6)-INTERVAL 10 SECOND,s.reported_match_id,s.last_heartbeat_utc>UTC_TIMESTAMP(6)-INTERVAL 10 SECOND FROM game_rooms r LEFT JOIN game_server_slots s ON s.port=@port WHERE r.id=@room;",connection))
                    {
                        select.Parameters.AddWithValue("@room",entry.Room);select.Parameters.AddWithValue("@port",entry.Port);
                        await using var reader=await select.ExecuteReaderAsync(ct);
                        status=await reader.ReadAsync(ct)?reader.GetString(0):null;
                        if(status is not null){ready=!reader.IsDBNull(1)&&reader.GetBoolean(1);reported=reader.IsDBNull(2)?null:Convert.ToString(reader.GetValue(2));healthy=!reader.IsDBNull(3)&&reader.GetBoolean(3);}
                    }
                    if(status is null or "Waiting"){await RemoveAsync(entry,false,ct);continue;}
                    if(entry.Process is null)
                    {
                        if(status!="Preparing"){await RemoveAsync(entry,true,ct);continue;}
                        // Clear stale evidence for a reused port before starting its new, uniquely identified process.
                        await using(var clear=new MySqlCommand("DELETE FROM game_server_slots WHERE port=@port;",connection)){clear.Parameters.AddWithValue("@port",entry.Port);await clear.ExecuteNonQueryAsync(ct);}
                        ready=false;reported=null;entry.Process=processes.Start(settings,entry.Port,entry.Instance,security.GameServerSharedSecret);
                        logger.LogInformation("경기 세션 실행: 방 {Room}, UDP {Port}",entry.Room,entry.Port);
                    }
                    if(entry.Process.HasExited){logger.LogWarning("경기 세션 프로세스 종료: 방 {Room}",entry.Room);await RemoveAsync(entry,true,ct);continue;}
                    entry.ObservedMatch|=reported is not null;
                    if(healthy)entry.LastHealthy=clock.GetUtcNow();
                    if(entry.ObservedMatch&&clock.GetUtcNow()-entry.LastHealthy>TimeSpan.FromSeconds(30))
                    {logger.LogWarning("진행 경기 heartbeat 유예 만료: 방 {Room}",entry.Room);await RemoveAsync(entry,true,ct);continue;}
                    if(!entry.ObservedMatch&&clock.GetUtcNow()-entry.Created>TimeSpan.FromSeconds(status=="Preparing"?90:150))
                    {logger.LogWarning("경기 세션 준비/입장 시간 초과: 방 {Room}",entry.Room);await RemoveAsync(entry,true,ct);continue;}
                    if(status=="Preparing"&&ready)
                    {
                        await using var tx=await connection.BeginTransactionAsync(ct);
                        await using var roomLock=new MySqlCommand("SELECT status FROM game_rooms WHERE id=@room FOR UPDATE;",connection,tx);roomLock.Parameters.AddWithValue("@room",entry.Room);
                        if((string?)await roomLock.ExecuteScalarAsync(ct)!="Preparing"){await tx.RollbackAsync(ct);continue;}
                        var memberIds=await ReadyMembersAsync(connection,tx,entry.Room,ct);
                        if(!await LobbyEndpoints.HasCompletePlayingRosterAsync(connection,tx,entry.Room,memberIds.Count,ct)){await tx.RollbackAsync(ct);await RemoveAsync(entry,true,ct);continue;}
                        await using var assign=new MySqlCommand("UPDATE game_server_slots SET assigned_room_id=@room,assigned_match_id=NULL,has_started=FALSE,reserved_at_utc=UTC_TIMESTAMP(6) WHERE port=@port AND ready=TRUE AND last_heartbeat_utc>UTC_TIMESTAMP(6)-INTERVAL 10 SECOND AND assigned_room_id IS NULL;",connection,tx);
                        assign.Parameters.AddWithValue("@room",entry.Room);assign.Parameters.AddWithValue("@port",entry.Port);
                        if(await assign.ExecuteNonQueryAsync(ct)!=1){await tx.RollbackAsync(ct);continue;}
                        string match=Guid.NewGuid().ToString("D");
                        if(!await GameServerSlots.ConfirmAsync(connection,tx,entry.Room,match,ct)){await tx.RollbackAsync(ct);continue;}
                        await LobbyEndpoints.AssignPlayingRolesAsync(connection,tx,entry.Room,memberIds,ct);
                        await using var start=new MySqlCommand("UPDATE game_rooms SET status='Started',match_id=@match,started_at_utc=UTC_TIMESTAMP(6) WHERE id=@room AND status='Preparing';",connection,tx);
                        start.Parameters.AddWithValue("@room",entry.Room);start.Parameters.AddWithValue("@match",match);
                        await start.ExecuteNonQueryAsync(ct);
                        await AuthoritativeMatchLedger.CaptureAsync(connection,tx,match,ct);
                        await tx.CommitAsync(ct);logger.LogInformation("경기 세션 준비 완료: 방 {Room}, {Seconds:F1}초 경과, 역할 배정 및 입장 허용 (에디터={Editor})",entry.Room,(clock.GetUtcNow()-entry.Created).TotalSeconds,settings.EditorLocalTest);
                    }
                    else if(status=="Started"&&entry.ObservedMatch&&ready){await RemoveAsync(entry,true,ct);}
                }
                catch(OperationCanceledException) when(ct.IsCancellationRequested){throw;}
                catch(Exception ex)
                {
                    logger.LogWarning(ex,"경기 세션 처리 실패: 방 {Room}",entry.Room);
                    // Transient DB failures must not terminate a healthy, already running match.
                    if(ex is not MySqlException)try{await RemoveAsync(entry,true,ct);}catch(Exception cleanup){logger.LogWarning(cleanup,"세션 정리 재시도 필요");}
                }
            }
            lock(gate)
            {
                if(draining&&entries.Count==0)
                {
                    configuration=configuration with {Enabled=false};draining=false;drainCompleted=true;
                    logger.LogInformation("모든 경기 세션이 정리되어 게임 서버 운영을 안전하게 중지했습니다.");
                }
            }
        }
        finally{cycle.Release();}
    }
    private static async Task<List<ulong>> ReadyMembersAsync(MySqlConnection connection,MySqlTransaction tx,string room,CancellationToken ct)
    {
        var members=new List<ulong>();
        await using var select=new MySqlCommand("SELECT m.user_id,(m.user_id=r.host_user_id OR m.is_ready) AND m.last_seen_at_utc>UTC_TIMESTAMP(6)-INTERVAL 60 SECOND FROM room_members m JOIN game_rooms r ON r.id=m.room_id WHERE m.room_id=@room AND m.is_observer=FALSE ORDER BY m.joined_at_utc,m.user_id FOR UPDATE;",connection,tx);
        select.Parameters.AddWithValue("@room",room);
        await using var reader=await select.ExecuteReaderAsync(ct);
        while(await reader.ReadAsync(ct)){if(!reader.GetBoolean(1))return [];members.Add(reader.GetUInt64(0));}
        return members;
    }
    private static async Task ResetPendingAsync(MySqlConnection connection,MySqlTransaction tx,string room,CancellationToken ct)
    {
        await using var reset=new MySqlCommand("DELETE FROM game_join_tickets WHERE room_id=@room; UPDATE room_members SET assigned_role=NULL,returned_to_room=FALSE,is_ready=FALSE WHERE room_id=@room; UPDATE room_bots SET assigned_role=NULL WHERE room_id=@room; UPDATE game_rooms SET status='Waiting',match_id=NULL,travel_url=NULL,started_at_utc=NULL WHERE id=@room AND status IN ('Starting','Queued','Preparing');",connection,tx);
        reset.Parameters.AddWithValue("@room",room);await reset.ExecuteNonQueryAsync(ct);
        await LobbyEndpoints.RestoreRandomPartiesAsync(connection,tx,room,ct);
    }
    private async Task AllocateQueuedRoomsAsync(CancellationToken ct)
    {
        // An unconfigured manager must not interfere with the legacy manually started server path.
        if(!Configured)return;
        await using var connection=await database.OpenConnectionAsync(ct);
        var pending=new List<(string Room,string Status,bool Expired)>();
        await using(var query=new MySqlCommand("SELECT id,status,started_at_utc IS NULL OR started_at_utc<UTC_TIMESTAMP(6)-INTERVAL (CASE status WHEN 'Queued' THEN 120 WHEN 'Preparing' THEN 90 ELSE 30 END) SECOND FROM game_rooms WHERE status IN ('Starting','Queued','Preparing') ORDER BY started_at_utc,id;",connection))
        {
            await using var reader=await query.ExecuteReaderAsync(ct);
            while(await reader.ReadAsync(ct))pending.Add((Convert.ToString(reader.GetValue(0))!,reader.GetString(1),reader.GetBoolean(2)));
        }
        foreach(var item in pending)
        {
            await using var tx=await connection.BeginTransactionAsync(ct);
            await using var roomLock=new MySqlCommand("SELECT status FROM game_rooms WHERE id=@room FOR UPDATE;",connection,tx);roomLock.Parameters.AddWithValue("@room",item.Room);
            if((string?)await roomLock.ExecuteScalarAsync(ct)!=item.Status){await tx.RollbackAsync(ct);continue;}
            if(!AcceptingNewMatches||item.Expired||!await LobbyEndpoints.HasCompletePlayingRosterAsync(connection,tx,item.Room,(await ReadyMembersAsync(connection,tx,item.Room,ct)).Count,ct))
            {
                await ResetPendingAsync(connection,tx,item.Room,ct);await tx.CommitAsync(ct);
                logger.LogInformation("경기 시작 대기 취소/시간 초과/인원 변경: 방 {Room}",item.Room);continue;
            }
            if(item.Status!="Queued"){await tx.RollbackAsync(ct);continue;}
            await using var limit=new MySqlCommand("SELECT max_rooms FROM lobby_state WHERE id=1",connection,tx);
            using var reservation=Reserve(item.Room,Convert.ToInt32(await limit.ExecuteScalarAsync(ct)));
            if(reservation is null){await tx.RollbackAsync(ct);continue;} // Full capacity is a bounded wait, not an error/logout.
            await using var prepare=new MySqlCommand("UPDATE game_rooms SET status='Preparing',travel_url=CONCAT(SUBSTRING_INDEX(travel_url,':',1),':',@port),started_at_utc=UTC_TIMESTAMP(6) WHERE id=@room",connection,tx);
            prepare.Parameters.AddWithValue("@room",item.Room);prepare.Parameters.AddWithValue("@port",reservation.Port);
            await prepare.ExecuteNonQueryAsync(ct);await tx.CommitAsync(ct);reservation.Commit();
        }
    }
    private async Task RemoveAsync(Entry entry,bool reset,CancellationToken ct)
    {
        entry.Process?.Stop();entry.Process?.Dispose();entry.Process=null;
        await using var connection=await database.OpenConnectionAsync(ct);
        await using var tx=await connection.BeginTransactionAsync(ct);
        if(reset)
        {
            await using var room=new MySqlCommand("SELECT id FROM game_rooms WHERE id=@room FOR UPDATE;",connection,tx);room.Parameters.AddWithValue("@room",entry.Room);await room.ExecuteScalarAsync(ct);
            await using var command=new MySqlCommand("DELETE FROM game_join_tickets WHERE room_id=@room; UPDATE room_members SET assigned_role=NULL,returned_to_room=FALSE,is_ready=FALSE WHERE room_id=@room; UPDATE room_bots SET assigned_role=NULL WHERE room_id=@room; UPDATE game_rooms SET status='Waiting',match_id=NULL,travel_url=NULL,started_at_utc=NULL WHERE id=@room;",connection,tx);command.Parameters.AddWithValue("@room",entry.Room);await command.ExecuteNonQueryAsync(ct);
            await LobbyEndpoints.RestoreRandomPartiesAsync(connection,tx,entry.Room,ct);
        }
        await using(var release=new MySqlCommand("UPDATE game_server_slots SET ready=FALSE,last_heartbeat_utc='2000-01-01',reported_match_id=NULL,assigned_room_id=NULL,assigned_match_id=NULL,has_started=FALSE WHERE port=@port;",connection,tx)){release.Parameters.AddWithValue("@port",entry.Port);await release.ExecuteNonQueryAsync(ct);}
        await tx.CommitAsync(ct);lock(gate)entries.Remove(entry.Room);
        logger.LogInformation("경기 세션 정리: 방 {Room}, UDP {Port}",entry.Room,entry.Port);
    }
    public override async Task StopAsync(CancellationToken cancellationToken)
    {
        await base.StopAsync(cancellationToken);
        lock(gate)configuration=configuration with {Enabled=false};
        Entry[] active;lock(gate)active=entries.Values.ToArray();
        foreach(var entry in active)try{await RemoveAsync(entry,true,cancellationToken);}catch(Exception ex){entry.Process?.Stop();logger.LogWarning(ex,"종료 중 세션 DB 정리 실패");}
    }
}

internal static class GameServerDemandEndpoints
{
    internal static void MapGameServerDemand(this WebApplication app)
    {
        app.MapPost("/api/admin/game-server-demand/drain",async(HttpRequest request,GameServerDrainRequest body,AdministratorOptions admin,GameServerDemandManager manager,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            try{await manager.DrainAsync(body.Cancel,ct);return Results.Ok(manager.Snapshot());}
            catch(InvalidOperationException ex){return Results.Conflict(new {message=ex.Message});}
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/game-server-demand/capacity",async(HttpRequest request,GameServerDemandCapacity body,AdministratorOptions admin,GameServerDemandManager manager,AuthDatabase database,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            if(body.MaximumConcurrent is <1 or >100)return Results.BadRequest(new {message="동시 경기 한도는 1~100개로 지정하세요."});
            await using var connection=await database.OpenConnectionAsync(ct);
            await using var select=new MySqlCommand("SELECT max_rooms FROM lobby_state WHERE id=1",connection);
            if(body.MaximumConcurrent>Convert.ToInt32(await select.ExecuteScalarAsync(ct)))return Results.BadRequest(new {message="동시 경기 수는 최대 방 수 이하여야 합니다. 먼저 방 한도를 늘리세요."});
            try{await manager.UpdateCapacityAsync(body.MaximumConcurrent,ct);return Results.Ok(manager.Snapshot());}
            catch(InvalidOperationException ex){return Results.Conflict(new {message=ex.Message});}
        }).RequireRateLimiting("admin");
        app.MapGet("/api/admin/game-server-demand",(HttpRequest request,AdministratorOptions admin,GameServerDemandManager manager)=>
            admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"])?Results.Ok(manager.Snapshot()):Results.Unauthorized()).RequireRateLimiting("admin");
        app.MapPut("/api/admin/game-server-demand",async(HttpRequest request,GameServerDemandConfiguration body,AdministratorOptions admin,GameServerDemandManager manager,AuthDatabase database,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(request.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            if(body.EditorLocalTest && (request.HttpContext.Connection.RemoteIpAddress is not { } ip || !System.Net.IPAddress.IsLoopback(ip)))
                return Results.BadRequest(new {message="에디터 테스트 서버는 서버 PC의 루프백 요청으로만 설정할 수 있습니다."});
            if(body.Enabled)
            {
                await using var connection=await database.OpenConnectionAsync(ct);await using var select=new MySqlCommand("SELECT max_rooms FROM lobby_state WHERE id=1",connection);
                if(body.MaximumConcurrent>Convert.ToInt32(await select.ExecuteScalarAsync(ct)))return Results.BadRequest(new {message="최대 방 수보다 많은 동시 경기를 지정할 수 없습니다."});
            }
            try{await manager.ConfigureAsync(body,ct);return Results.Ok(manager.Snapshot());}
            catch(Exception ex) when(ex is InvalidOperationException or ArgumentException or IOException){return Results.Conflict(new {message=ex.Message});}
        }).RequireRateLimiting("admin");
    }
}

using Microsoft.AspNetCore.Identity;
using MySqlConnector;

internal static partial class OpenParties
{
    internal const string AdmissionHeader="X-ProjectProject01-Party-Admission";
    internal const int AdmissionSeconds=30;
    private static readonly PasswordHasher<RoomPasswordRecord> AdmissionHasher=new();
    private static Task EnsureAdmissionsAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS case_party_admissions(
          id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
          party_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
          leader_id BIGINT UNSIGNED NOT NULL,kind VARCHAR(16) NOT NULL,label VARCHAR(96) NOT NULL,
          request_hash TEXT NOT NULL,target_room CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
          state VARCHAR(16) NOT NULL DEFAULT 'Pending',message VARCHAR(160) NOT NULL DEFAULT '',
          created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),expires_at_utc DATETIME(6) NOT NULL,
          KEY ix_party_admission(party_id,created_at_utc),
          FOREIGN KEY(party_id) REFERENCES case_parties(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS case_party_admission_members(
          admission_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,user_id BIGINT UNSIGNED NOT NULL,
          accepted BOOLEAN NOT NULL DEFAULT FALSE,PRIMARY KEY(admission_id,user_id),KEY ix_admission_user(user_id),
          FOREIGN KEY(admission_id) REFERENCES case_party_admissions(id) ON DELETE CASCADE,
          FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);
        """,ct);
    private static Task<int> CancelAdmissionAsync(MySqlConnection c,MySqlTransaction tx,string party,string message,CancellationToken ct)=>CommunityEndpoints.Exec(c,tx,
        "UPDATE case_party_admissions SET state='Cancelled',message=@m WHERE party_id=@p AND state IN ('Pending','Ready')",ct,("p",party),("m",message));
    private static async Task<bool> AdmissionMembersValid(MySqlConnection c,MySqlTransaction tx,Dictionary<string,object?> row,CancellationToken ct)
    {
        string id=Convert.ToString(row["id"])!,party=Convert.ToString(row["party_id"])!;
        var leader=await CommunityEndpoints.Rows(c,tx,"SELECT leader_id FROM case_parties WHERE id=@p FOR UPDATE",ct,("p",party));
        if(leader.Count!=1||Convert.ToUInt64(leader[0]["leader_id"])!=Convert.ToUInt64(row["leader_id"]))return false;
        var current=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM case_party_members WHERE party_id=@p ORDER BY user_id",ct,("p",party));
        var expected=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM case_party_admission_members WHERE admission_id=@a ORDER BY user_id",ct,("a",id));
        if(!current.Select(r=>Convert.ToUInt64(r["user_id"])).SequenceEqual(expected.Select(r=>Convert.ToUInt64(r["user_id"]))))return false;
        foreach(var member in expected)
        {
            ulong user=Convert.ToUInt64(member["user_id"]);
            if(!await CanEnter(c,tx,user,ct)||await Busy(c,tx,user,ct)||(await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM room_members WHERE user_id=@u",ct,("u",user))).Count>0)return false;
        }
        if(row["target_room"] is not null)
        {
            var rooms=await CommunityEndpoints.Rows(c,tx,"SELECT status,(SELECT COUNT(*) FROM room_members m WHERE m.room_id=g.id AND m.is_observer=FALSE)+(SELECT COUNT(*) FROM room_bots b WHERE b.room_id=g.id) playing FROM game_rooms g WHERE id=@r",ct,("r",row["target_room"]));
            if(rooms.Count!=1||Convert.ToString(rooms[0]["status"])!="Waiting"||Convert.ToInt32(rooms[0]["playing"])+expected.Count>3)return false;
        }
        return true;
    }
    private static async Task InspectAdmission(MySqlConnection c,MySqlTransaction tx,Dictionary<string,object?> row,CancellationToken ct)
    {
        if(Convert.ToString(row["state"]) is not ("Pending" or "Ready"))return;
        string? reason=null;
        if(Convert.ToDateTime(row["expires_at_utc"])<=DateTime.UtcNow)reason="응답 시간이 지나 입장을 취소했습니다.";
        else if(!await AdmissionMembersValid(c,tx,row,ct))reason="파티원 또는 방 상태가 변경되어 입장을 취소했습니다.";
        if(reason is not null)
        {await CancelAdmissionAsync(c,tx,Convert.ToString(row["party_id"])!,reason,ct);row["state"]="Cancelled";row["message"]=reason;}
    }
    private static async Task<Dictionary<string,object?>?> ReadAdmissionLocked(MySqlConnection c,MySqlTransaction tx,string id,CancellationToken ct)
    {
        var refs=await CommunityEndpoints.Rows(c,tx,"SELECT party_id FROM case_party_admissions WHERE id=@a",ct,("a",id));if(refs.Count==0)return null;
        // Same party -> admission lock order as logout/leave; no inverse lock with liveness cleanup.
        await CommunityEndpoints.Rows(c,tx,"SELECT id FROM case_parties WHERE id=@p FOR UPDATE",ct,("p",refs[0]["party_id"]));
        return (await CommunityEndpoints.Rows(c,tx,"SELECT * FROM case_party_admissions WHERE id=@a FOR UPDATE",ct,("a",id))).FirstOrDefault();
    }
    private static async Task<object> AdmissionView(MySqlConnection c,MySqlTransaction? tx,Dictionary<string,object?> row,ulong user,CancellationToken ct)
    {
        var members=await CommunityEndpoints.Rows(c,tx,"SELECT user_id,accepted FROM case_party_admission_members WHERE admission_id=@a",ct,("a",row["id"]));
        ulong leader=Convert.ToUInt64(row["leader_id"]);return new{
            id=Convert.ToString(row["id"]),state=Convert.ToString(row["state"]),label=row["label"],kind=row["kind"],
            isLeader=user==leader,hasAccepted=members.Any(m=>Convert.ToUInt64(m["user_id"])==user&&Convert.ToBoolean(m["accepted"])),
            accepted=members.Count(m=>Convert.ToUInt64(m["user_id"])!=leader&&Convert.ToBoolean(m["accepted"])),required=Math.Max(0,members.Count-1),
            remainingSeconds=Math.Max(0,(int)Math.Ceiling((Convert.ToDateTime(row["expires_at_utc"])-DateTime.UtcNow).TotalSeconds)),message=row["message"]};
    }
    // Called before any room mutation under the shared lobby transaction. Only the original
    // leader/request can consume a ready proposal; completion rolls back with a failed room entry.
    internal static async Task<IResult?> RequireAdmissionAsync(MySqlConnection c,MySqlTransaction tx,ulong user,HttpRequest request,string kind,string label,string canonical,string? targetRoom,CancellationToken ct)
    {
        string proof=request.Headers[AdmissionHeader].ToString();string? party=await PartyOf(c,tx,user,ct);
        if(party is null)return string.IsNullOrEmpty(proof)?null:Results.Conflict(new{message="파티 입장 요청이 만료되었습니다."});
        var leaders=await CommunityEndpoints.Rows(c,tx,"SELECT leader_id FROM case_parties WHERE id=@p FOR UPDATE",ct,("p",party));
        if(leaders.Count!=1||Convert.ToUInt64(leaders[0]["leader_id"])!=user)return Results.StatusCode(403);
        var members=await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM case_party_members WHERE party_id=@p ORDER BY user_id",ct,("p",party));
        if(members.Count<=1)return string.IsNullOrEmpty(proof)?null:Results.Conflict(new{message="파티 구성이 변경되었습니다."});
        if(kind=="Bot"&&members.Count>2)return Results.Conflict(new{message="봇전 파티는 최대 2명입니다."});
        await CommunityEndpoints.Exec(c,tx,"DELETE FROM case_party_admissions WHERE expires_at_utc<UTC_TIMESTAMP(6)-INTERVAL 2 MINUTE",ct);
        if(!string.IsNullOrEmpty(proof))
        {
            if(!Guid.TryParse(proof,out var parsed))return Results.BadRequest();
            var row=await ReadAdmissionLocked(c,tx,parsed.ToString(),ct);
            if(row is null||Convert.ToString(row["party_id"])!=party||Convert.ToUInt64(row["leader_id"])!=user)return Results.StatusCode(403);
            await InspectAdmission(c,tx,row,ct);
            if(Convert.ToString(row["state"])!="Ready"||Convert.ToString(row["kind"])!=kind||AdmissionHasher.VerifyHashedPassword(new RoomPasswordRecord(proof),Convert.ToString(row["request_hash"])!,canonical)==PasswordVerificationResult.Failed)
            {await tx.CommitAsync(ct);return Results.Conflict(new{message="전원 수락한 유효한 입장 요청이 필요합니다."});}
            await CommunityEndpoints.Exec(c,tx,"UPDATE case_party_admissions SET state='Completed',message='' WHERE id=@a",ct,("a",row["id"]));return null;
        }
        var active=await CommunityEndpoints.Rows(c,tx,"SELECT * FROM case_party_admissions WHERE party_id=@p AND state IN ('Pending','Ready') ORDER BY created_at_utc DESC LIMIT 1",ct,("p",party));
        Dictionary<string,object?>? pending=active.FirstOrDefault();if(pending is not null){await InspectAdmission(c,tx,pending,ct);if(Convert.ToString(pending["state"])=="Cancelled")pending=null;}
        if(pending is not null&&AdmissionHasher.VerifyHashedPassword(new RoomPasswordRecord(Convert.ToString(pending["id"])!),Convert.ToString(pending["request_hash"])!,canonical)==PasswordVerificationResult.Failed)
            return Results.Conflict(new{message="진행 중인 파티 입장을 먼저 취소하세요."});
        if(pending is null)
        {
            foreach(var m in members)if(!await CanEnter(c,tx,Convert.ToUInt64(m["user_id"]),ct)||(await CommunityEndpoints.Rows(c,tx,"SELECT user_id FROM room_members WHERE user_id=@u",ct,("u",m["user_id"]))).Count>0)return Results.Conflict(new{message="파티원이 로비에 접속한 상태에서 입장하세요."});
            string id=Guid.NewGuid().ToString();string hash=AdmissionHasher.HashPassword(new RoomPasswordRecord(id),canonical);
            await CommunityEndpoints.Exec(c,tx,$"INSERT INTO case_party_admissions(id,party_id,leader_id,kind,label,request_hash,target_room,expires_at_utc) VALUES(@a,@p,@u,@k,@l,@h,@r,UTC_TIMESTAMP(6)+INTERVAL {AdmissionSeconds} SECOND)",ct,("a",id),("p",party),("u",user),("k",kind),("l",label),("h",hash),("r",targetRoom));
            foreach(var m in members)await CommunityEndpoints.Exec(c,tx,"INSERT INTO case_party_admission_members(admission_id,user_id,accepted) VALUES(@a,@u,@yes)",ct,("a",id),("u",m["user_id"]),("yes",Convert.ToUInt64(m["user_id"])==user));
            pending=(await CommunityEndpoints.Rows(c,tx,"SELECT * FROM case_party_admissions WHERE id=@a",ct,("a",id)))[0];
        }
        var view=await AdmissionView(c,tx,pending,user,ct);await tx.CommitAsync(ct);
        return Results.Json(new{message="파티원의 수락을 기다리고 있습니다.",partyAdmission=view},statusCode:202);
    }
    private sealed record AdmissionAnswer(string Id,string Action);
    private static void MapPartyAdmissions(this WebApplication app)
    {
        app.MapGet("/api/parties/admission",async(HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);
            var refs=await CommunityEndpoints.Rows(c,null,"SELECT a.* FROM case_party_admissions a JOIN case_party_admission_members m ON m.admission_id=a.id WHERE m.user_id=@u AND a.expires_at_utc>UTC_TIMESTAMP(6)-INTERVAL 2 MINUTE ORDER BY a.created_at_utc DESC LIMIT 1",ct,("u",user.Id));
            var row=refs.FirstOrDefault();object? view=null;
            // Idle parties never take the global room lock. Only an active admission can mutate state.
            if(row is not null&&Convert.ToString(row["state"]) is "Pending" or "Ready")
            {
                await using var tx=await c.BeginTransactionAsync(ct);await LobbyEndpoints.LockLobbyStateAsync(c,tx,ct);
                row=await ReadAdmissionLocked(c,tx,Convert.ToString(row["id"])!,ct);if(row is not null){await InspectAdmission(c,tx,row,ct);view=await AdmissionView(c,tx,row,user.Id,ct);}await tx.CommitAsync(ct);
            }
            else if(row is not null)view=await AdmissionView(c,null,row,user.Id,ct);
            var party=await CommunityEndpoints.Rows(c,null,"SELECT CAST(p.leader_id AS CHAR) leader,(SELECT COUNT(*) FROM case_party_members peers WHERE peers.party_id=p.id) members FROM case_parties p JOIN case_party_members m ON m.party_id=p.id WHERE m.user_id=@u",ct,("u",user.Id));
            string partyLeaderId=party.Count==0?"":Convert.ToString(party[0]["leader"])!;int partyMemberCount=party.Count==0?0:Convert.ToInt32(party[0]["members"]);
            return Results.Ok(new{partyAdmission=view,partyLeaderId,partyMemberCount});
        }).RequireRateLimiting("social");
        app.MapPost("/api/parties/admission",async(AdmissionAnswer body,HttpRequest request,AuthDatabase db,CancellationToken ct)=>
        {
            var user=await LobbyEndpoints.AuthenticateAsync(request,db,ct);if(user is null)return Results.Unauthorized();
            if(!Guid.TryParse(body.Id,out var id)||body.Action is not ("Accept" or "Decline" or "Cancel"))return Results.BadRequest();
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);await LobbyEndpoints.LockLobbyStateAsync(c,tx,ct);
            var row=await ReadAdmissionLocked(c,tx,id.ToString(),ct);if(row is null)return Results.NotFound();
            var member=await CommunityEndpoints.Rows(c,tx,"SELECT accepted FROM case_party_admission_members WHERE admission_id=@a AND user_id=@u",ct,("a",id.ToString()),("u",user.Id));
            if(member.Count==0)return Results.StatusCode(403);bool leader=Convert.ToUInt64(row["leader_id"])==user.Id;
            if((body.Action=="Cancel"&&!leader)||(body.Action=="Accept"&&leader))return Results.StatusCode(403);
            await InspectAdmission(c,tx,row,ct);
            if(Convert.ToString(row["state"]) is "Pending" or "Ready")
            {
                if(body.Action!="Accept")await CancelAdmissionAsync(c,tx,Convert.ToString(row["party_id"])!,"파티 입장이 취소되었습니다.",ct);
                else
                {
                    await CommunityEndpoints.Exec(c,tx,"UPDATE case_party_admission_members SET accepted=TRUE WHERE admission_id=@a AND user_id=@u;UPDATE case_party_admissions a SET state='Ready' WHERE a.id=@a AND NOT EXISTS(SELECT 1 FROM case_party_admission_members m WHERE m.admission_id=a.id AND accepted=FALSE)",ct,("a",id.ToString()),("u",user.Id));
                }
            }
            row=(await CommunityEndpoints.Rows(c,tx,"SELECT * FROM case_party_admissions WHERE id=@a",ct,("a",id.ToString())))[0];
            var view=await AdmissionView(c,tx,row,user.Id,ct);await tx.CommitAsync(ct);return Results.Ok(new{partyAdmission=view});
        }).RequireRateLimiting("social");
    }
}

using MySqlConnector;

internal static partial class CommunityEndpoints
{
    internal sealed record PlayerReportRequest(string? TargetUserId,string Reason,string Details,string? TargetAccount=null);
    internal sealed record PlayerReportReview(string Status,string Note);

    private static void MapPlayerReports(this WebApplication app)
    {
        app.MapPost("/api/social/reports",async(PlayerReportRequest input,HttpRequest req,AuthDatabase db,CancellationToken ct)=>
        {
            var me=await LobbyEndpoints.AuthenticateAsync(req,db,ct);if(me is null)return Results.Unauthorized();
            bool byAccount=!string.IsNullOrWhiteSpace(input.TargetAccount);
            if(byAccount&&(input.TargetAccount!.Trim().Length is <3 or >32||!string.IsNullOrEmpty(input.TargetUserId)))return Error("상대의 계정 아이디를 확인하세요. 신고 대상은 한 가지 방식으로만 지정하세요.");
            ulong target=0;
            if(!byAccount&&(!ulong.TryParse(input.TargetUserId,out target)||target==0))return Error("신고할 참가자 정보를 확인하세요.");
            if(input.Reason is not ("Abuse" or "Cheating" or "Griefing" or "Other")||!ValidText(input.Details,1000))return Error("신고 사유를 선택하고 상세 내용을 1~1000자로 입력하세요.");
            await using var c=await db.OpenConnectionAsync(ct);await using var tx=await c.BeginTransactionAsync(ct);
            // Serialize submissions by reporter, including simultaneous requests.
            await Rows(c,tx,"SELECT id FROM users WHERE id=@me FOR UPDATE",ct,("me",me.Id));
            if(byAccount)
            {
                var users=await Rows(c,tx,"SELECT id FROM users WHERE account_id=@account",ct,("account",input.TargetAccount!.Trim()));
                if(users.Count==0)return Results.NotFound(new{message="해당 계정 아이디를 찾을 수 없습니다. 닉네임이 아닌 로그인 아이디를 입력하세요."});
                target=Convert.ToUInt64(users[0]["id"]);
            }
            if(target==me.Id)return Error("본인은 신고할 수 없습니다.");
            var context=await Rows(c,tx,"""
                SELECT CAST(r.id AS CHAR) roomId,r.name roomName,u.account_id reporterAccount,u.display_name reporterName,
                  t.account_id targetAccount,t.display_name targetName
                FROM room_members m JOIN room_members other ON other.room_id=m.room_id
                JOIN game_rooms r ON r.id=m.room_id JOIN users u ON u.id=m.user_id JOIN users t ON t.id=other.user_id
                WHERE m.user_id=@me AND other.user_id=@target
                """,ct,("me",me.Id),("target",target));
            if(context.Count==0)
            {
                if(!byAccount)return Results.NotFound(new{message="같은 방에 있는 참가자만 신고할 수 있습니다. 참가자 목록을 다시 확인하세요."});
                // An ID-only report does not claim the players shared a room. Resolve both identities on the server.
                context=await Rows(c,tx,"""
                    SELECT '' roomId,'아이디 직접 신고 (같은 방 기록 없음)' roomName,
                      u.account_id reporterAccount,u.display_name reporterName,t.account_id targetAccount,t.display_name targetName
                    FROM users u JOIN users t ON t.id=@target WHERE u.id=@me
                    """,ct,("me",me.Id),("target",target));
                if(context.Count==0)return Results.NotFound();
            }
            if(await Scalar(c,tx,"""
                SELECT COUNT(*) FROM player_reports WHERE reporter_id=@me AND
                  (created_at_utc>UTC_TIMESTAMP(6)-INTERVAL 30 SECOND OR
                   (target_id=@target AND created_at_utc>UTC_TIMESTAMP(6)-INTERVAL 10 MINUTE))
                """,ct,("me",me.Id),("target",target))>0||
                await Scalar(c,tx,"SELECT COUNT(*) FROM player_reports WHERE reporter_id=@me AND created_at_utc>UTC_TIMESTAMP(6)-INTERVAL 1 DAY",ct,("me",me.Id))>=20)
                return Results.Json(new{message="반복 신고는 제한됩니다. 동일 대상은 10분 후, 다른 대상은 30초 후 다시 신고할 수 있습니다. 하루 최대 20건입니다."},statusCode:429);
            var row=context[0];
            await Exec(c,tx,"""
                INSERT INTO player_reports(reporter_id,target_id,reporter_account,reporter_name,target_account,target_name,room_id,room_name,reason,details)
                VALUES(@me,@target,@ra,@rn,@ta,@tn,@room,@name,@reason,@details)
                """,ct,("me",me.Id),("target",target),("ra",row["reporterAccount"]),("rn",row["reporterName"]),
                ("ta",row["targetAccount"]),("tn",row["targetName"]),("room",row["roomId"]),("name",row["roomName"]),("reason",input.Reason),("details",input.Details.Trim()));
            string id=Convert.ToString(await Scalar(c,tx,"SELECT LAST_INSERT_ID()",ct))!;
            await tx.CommitAsync(ct);
            return Results.Ok(new{id,message="신고가 접수되었습니다. 운영자가 확인합니다. 신고만으로 자동 제재하지 않습니다."});
        }).RequireRateLimiting("social");

        app.MapGet("/api/admin/community/reports",async(string? status,ulong? before,HttpRequest req,AdministratorOptions admin,AuthDatabase db,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            if(status is not (null or "All" or "Open" or "Reviewed" or "Dismissed"))return Error("처리 상태를 확인하세요.");
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await Rows(c,null,"""
                SELECT CAST(id AS CHAR) id,CAST(reporter_id AS CHAR) reporterId,CAST(target_id AS CHAR) targetId,
                  reporter_account reporterAccount,reporter_name reporterName,target_account targetAccount,target_name targetName,
                  CAST(room_id AS CHAR) roomId,room_name roomName,reason,details,status,COALESCE(review_note,'') reviewNote,
                  created_at_utc createdAtUtc,reviewed_at_utc reviewedAtUtc FROM player_reports
                WHERE (@status='All' OR status=@status) AND id<@before ORDER BY id DESC LIMIT 101
                """,ct,("status",status??"All"),("before",before??ulong.MaxValue));
            bool more=rows.Count>100;if(more)rows.RemoveAt(100);
            return Results.Ok(new{reports=rows,hasMore=more,nextBefore=more?Convert.ToString(rows[^1]["id"]):null});
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/community/reports/{id:long}",async(long id,PlayerReportReview input,HttpRequest req,AdministratorOptions admin,AuthDatabase db,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            if(input.Status is not ("Open" or "Reviewed" or "Dismissed")||input.Note is null||input.Note.Length>2000||
                (input.Note.Length>0&&!ValidText(input.Note,2000)))return Error("상태와 검토 메모(최대 2000자)를 확인하세요.");
            await using var c=await db.OpenConnectionAsync(ct);
            int changed=await Exec(c,null,"UPDATE player_reports SET status=@status,review_note=@note,reviewed_at_utc=IF(@status='Open',NULL,UTC_TIMESTAMP(6)) WHERE id=@id",ct,("id",id),("status",input.Status),("note",input.Note.Trim()));
            return changed==0?Results.NotFound():Results.Ok(new{message="신고 검토 상태를 저장했습니다. 자동 제재는 실행하지 않습니다."});
        }).RequireRateLimiting("admin");
    }
}

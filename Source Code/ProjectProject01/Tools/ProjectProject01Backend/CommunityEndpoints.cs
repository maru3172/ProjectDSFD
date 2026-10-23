using MySqlConnector;

// Account-scoped social data. No presence, messages or block lists are exposed anonymously.
internal static partial class CommunityEndpoints
{
    internal sealed record ActionRequest(string Operation, string? Target = null, string? Text = null,
        string? Presence = null, string? Context = null);
    internal sealed record PostRequest(string Kind, string Title, string Body, bool Published = true, string? PreviewImageId = null);
    internal sealed record InquiryRequest(string Subject, string Body, string? ReplyCode = null);
    internal sealed record ReplyRequest(string Reply, string Status = "Answered");

    internal static bool ValidPresence(string? value) => value is "Online" or "Offline" or "Away" or "DoNotDisturb";
    internal static bool ValidText(string? value, int max) => !string.IsNullOrWhiteSpace(value) && value.Length <= max && !value.Any(c => char.IsControl(c) && c is not '\n' and not '\r' and not '\t');
    internal static bool ValidPostBody(string? value)=>ValidText(value,16000)&&System.Text.RegularExpressions.Regex.Matches(value!,@"!\[[^\]\r\n]*\]\(image:").Count<=8;

    public static void MapCommunity(this WebApplication app)
    {
        app.MapCommunityImages();
        app.MapSocialProfiles();
        app.MapPlayerReports();
        app.MapConversations();
        app.MapGet("/api/social", Snapshot).RequireRateLimiting("social");
        app.MapPost("/api/social/action", ActSafe).RequireRateLimiting("social");
        app.MapGet("/api/launcher/posts", async (AuthDatabase db, CancellationToken ct) =>
        {
            await using var c = await db.OpenConnectionAsync(ct);
            return Results.Ok(new { posts = await Rows(c, null, "SELECT CAST(id AS CHAR) id,kind,title,body,preview_image_id previewImageId,updated_at_utc updatedAtUtc FROM launcher_posts WHERE published=1 ORDER BY updated_at_utc DESC,id DESC LIMIT 100", ct) });
        }).RequireRateLimiting("survey-read");
        // The launcher works before game login. A random receipt permits only the author to read replies.
        app.MapPost("/api/launcher/inquiries", async (InquiryRequest input, AuthDatabase db, CancellationToken ct) =>
        {
            if (!ValidText(input.Subject, 120) || !ValidText(input.Body, 4000)) return Error("제목 120자, 문의 내용 4000자 이내로 입력하세요.");
            string receipt = Convert.ToHexString(System.Security.Cryptography.RandomNumberGenerator.GetBytes(24));
            await using var c = await db.OpenConnectionAsync(ct);
            await Exec(c, null, "INSERT INTO support_inquiries(receipt_hash,subject,body) VALUES(SHA2(@receipt,256),@subject,@body)", ct,
                ("receipt", receipt), ("subject", input.Subject.Trim()), ("body", input.Body.Trim()));
            return Results.Ok(new { receipt, message = "문의가 접수되었습니다. 답변 확인 코드를 안전하게 보관하세요." });
        }).RequireRateLimiting("survey");
        app.MapPost("/api/launcher/inquiries/reply", async (InquiryRequest input, AuthDatabase db, CancellationToken ct) =>
        {
            if (input.ReplyCode?.Length != 48) return Error("답변 확인 코드를 확인하세요.");
            await using var c = await db.OpenConnectionAsync(ct);
            var rows = await Rows(c, null, "SELECT subject,body,reply,status,created_at_utc createdAtUtc FROM support_inquiries WHERE receipt_hash=SHA2(@code,256)", ct, ("code", input.ReplyCode));
            return rows.Count == 0 ? Results.NotFound() : Results.Ok(rows[0]);
        }).RequireRateLimiting("survey");
        app.MapGet("/api/admin/community", async (HttpRequest req, AdministratorOptions admin, AuthDatabase db, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct);
            return Results.Ok(new {
                posts = await Rows(c, null, "SELECT CAST(id AS CHAR) id,kind,title,body,published,preview_image_id previewImageId FROM launcher_posts ORDER BY id DESC LIMIT 200", ct),
                inquiries = await Rows(c, null, "SELECT CAST(id AS CHAR) id,subject,body,reply,status,created_at_utc createdAtUtc FROM support_inquiries ORDER BY id DESC LIMIT 300", ct)
            });
        }).RequireRateLimiting("admin");
        app.MapPost("/api/admin/community/posts", async (HttpRequest req, PostRequest input, AdministratorOptions admin, AuthDatabase db, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            if (input.Kind is not ("News" or "Patch") || !ValidText(input.Title,120) || !ValidPostBody(input.Body)) return Error("종류·제목·내용을 확인하세요. 사진은 글당 최대 8장입니다.");
            await using var c = await db.OpenConnectionAsync(ct);
            if(!await ValidatePreview(c,input,ct))return Error("미리보기 사진은 본문에 첨부한 사진 중 한 장만 선택하세요.");
            await Exec(c,null,"INSERT INTO launcher_posts(kind,title,body,published,preview_image_id) VALUES(@kind,@title,@body,@published,@preview)",ct,("kind",input.Kind),("title",input.Title.Trim()),("body",input.Body.Trim()),("published",input.Published),("preview",NormalizePreview(input.PreviewImageId)));
            return Results.Ok(new { message = "게시되었습니다." });
        }).RequireRateLimiting("admin");
        app.MapDelete("/api/admin/community/posts/{id:long}", async (long id, HttpRequest req, AdministratorOptions admin, AuthDatabase db, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            await using var c = await db.OpenConnectionAsync(ct);
            await Exec(c,null,"DELETE FROM launcher_posts WHERE id=@id",ct,("id",id));
            return Results.Ok(new { message = "삭제되었습니다." });
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/community/posts/{id:long}", async (long id, HttpRequest req, PostRequest input, AdministratorOptions admin, AuthDatabase db, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            if(input.Kind is not ("News" or "Patch") || !ValidText(input.Title,120) || !ValidPostBody(input.Body))return Error("종류·제목·내용을 확인하세요. 사진은 글당 최대 8장입니다.");
            await using var c=await db.OpenConnectionAsync(ct);
            if(!await ValidatePreview(c,input,ct))return Error("미리보기 사진은 본문에 첨부한 사진 중 한 장만 선택하세요.");
            await Exec(c,null,"UPDATE launcher_posts SET kind=@kind,title=@title,body=@body,published=@published,preview_image_id=@preview,updated_at_utc=UTC_TIMESTAMP(6) WHERE id=@id",ct,("kind",input.Kind),("title",input.Title.Trim()),("body",input.Body.Trim()),("published",input.Published),("id",id),("preview",NormalizePreview(input.PreviewImageId)));
            return Results.Ok(new {message="변경되었습니다."});
        }).RequireRateLimiting("admin");
        app.MapPut("/api/admin/community/inquiries/{id:long}", async (long id, HttpRequest req, ReplyRequest input, AdministratorOptions admin, AuthDatabase db, CancellationToken ct) =>
        {
            if (!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"])) return Results.Unauthorized();
            if (!ValidText(input.Reply,4000) || input.Status is not ("Answered" or "Closed")) return Error("답변을 확인하세요.");
            await using var c=await db.OpenConnectionAsync(ct);
            await Exec(c,null,"UPDATE support_inquiries SET reply=@reply,status=@status WHERE id=@id",ct,("reply",input.Reply.Trim()),("status",input.Status),("id",id));
            return Results.Ok(new { message = "답변이 저장되었습니다." });
        }).RequireRateLimiting("admin");
    }

    private static IResult Error(string message) => Results.BadRequest(new { message });
    private static string? NormalizePreview(string? id)=>Guid.TryParse(id,out var value)?value.ToString("D"):null;
    private static async Task<bool> ValidatePreview(MySqlConnection c,PostRequest input,CancellationToken ct)
    {
        if(string.IsNullOrWhiteSpace(input.PreviewImageId))return true;
        string? id=NormalizePreview(input.PreviewImageId);
        return id is not null&&input.Body.Contains("(image:"+id+")",StringComparison.OrdinalIgnoreCase)&&
            (await Rows(c,null,"SELECT id FROM launcher_images WHERE id=@id",ct,("id",id))).Count==1;
    }
    private static async Task<IResult> Snapshot(HttpRequest req, AuthDatabase db, CancellationToken ct)
    {
        var user=await LobbyEndpoints.AuthenticateAsync(req,db,ct);
        if(user is null)return Results.Unauthorized();
        await using var c=await db.OpenConnectionAsync(ct);
        var friends=await Rows(c,null,"""
            SELECT u.account_id accountId,u.display_name displayName,COALESCE(pr.status_message,'') statusMessage,COALESCE(pr.accent,'Slate') accent,COALESCE(pr.title,'Rookie') title,
              CASE WHEN p.last_seen_utc>UTC_TIMESTAMP(6)-INTERVAL 20 SECOND AND EXISTS(SELECT 1 FROM auth_sessions s WHERE s.user_id=u.id AND s.revoked_at_utc IS NULL AND s.access_expires_at_utc>UTC_TIMESTAMP(6)) THEN COALESCE(p.presence,'Offline') ELSE 'Offline' END presence,
              CASE WHEN p.presence='Online' AND p.last_seen_utc>UTC_TIMESTAMP(6)-INTERVAL 20 SECOND
                AND EXISTS(SELECT 1 FROM auth_sessions s WHERE s.user_id=u.id AND s.revoked_at_utc IS NULL AND s.access_expires_at_utc>UTC_TIMESTAMP(6))
                THEN CASE WHEN r.status='Started' AND m.returned_to_room=0 THEN CASE WHEN m.assigned_role='Mannequin' THEN '경기 중 / 마네킹' ELSE '경기 중 / 생존자' END WHEN r.id IS NOT NULL THEN '방' ELSE '로비' END ELSE '' END activity
            FROM social_friends f JOIN users u ON u.id=IF(f.user_low=@me,f.user_high,f.user_low)
            LEFT JOIN social_presence p ON p.user_id=u.id LEFT JOIN room_members m ON m.user_id=u.id LEFT JOIN game_rooms r ON r.id=m.room_id
            LEFT JOIN social_profiles pr ON pr.user_id=u.id
            WHERE (f.user_low=@me OR f.user_high=@me) ORDER BY u.display_name LIMIT 200
            """,ct,("me",user.Id));
        var requests=await Rows(c,null,"SELECT u.account_id accountId,u.display_name displayName,f.created_at_utc createdAtUtc FROM social_requests f JOIN users u ON u.id=f.sender_id WHERE f.recipient_id=@me ORDER BY f.created_at_utc DESC LIMIT 100",ct,("me",user.Id));
        var blocks=await Rows(c,null,"SELECT u.account_id accountId,u.display_name displayName FROM social_blocks b JOIN users u ON u.id=b.target_id WHERE b.owner_id=@me ORDER BY u.display_name LIMIT 500",ct,("me",user.Id));
        var notifications=await Rows(c,null,"SELECT CAST(id AS CHAR) id,kind,body,unread FROM social_notifications WHERE user_id=@me ORDER BY id DESC LIMIT 100",ct,("me",user.Id));
        // No whisper contents are returned while the user is in a match, even for an old pending message.
        var messages=await Rows(c,null,"""
            SELECT CAST(w.id AS CHAR) id,u.account_id accountId,u.display_name displayName,w.body,w.created_at_utc createdAtUtc,IF(w.recipient_id=@me,'Incoming','Outgoing') direction
            FROM social_whispers w JOIN users u ON u.id=IF(w.sender_id=@me,w.recipient_id,w.sender_id)
            WHERE (w.recipient_id=@me OR w.sender_id=@me) AND w.created_at_utc>UTC_TIMESTAMP(6)-INTERVAL 7 DAY
              AND w.id>COALESCE((SELECT deleted_through_id FROM social_conversation_state WHERE owner_id=@me AND target_id=u.id),0)
              AND EXISTS(SELECT 1 FROM social_presence p WHERE p.user_id=@me AND p.context='Lobby' AND p.last_seen_utc>UTC_TIMESTAMP(6)-INTERVAL 20 SECOND)
              AND NOT EXISTS(SELECT 1 FROM room_members m JOIN game_rooms r ON r.id=m.room_id WHERE m.user_id=@me AND r.status='Started' AND m.returned_to_room=0)
              AND NOT EXISTS(SELECT 1 FROM social_blocks b WHERE (b.owner_id=@me AND b.target_id=u.id) OR (b.owner_id=u.id AND b.target_id=@me))
            ORDER BY w.id DESC LIMIT 100
            """,ct,("me",user.Id));
        var me=await ProfileRows(c,user.Id,ct);
        var statusMessages=await Rows(c,null,"""
            SELECT u.account_id accountId,u.display_name displayName,p.status_message statusMessage,p.status_updated_at_utc updatedAtUtc
            FROM social_profiles p JOIN users u ON u.id=p.user_id WHERE p.status_message<>''
            AND NOT EXISTS(SELECT 1 FROM social_blocks b WHERE (b.owner_id=@me AND b.target_id=u.id) OR (b.owner_id=u.id AND b.target_id=@me))
            ORDER BY p.status_updated_at_utc DESC,u.id DESC LIMIT 50
            """,ct,("me",user.Id));
        var invites=await Rows(c,null,"""
            SELECT i.id inviteId,u.account_id accountId,u.display_name displayName,r.name roomName,i.expires_at_utc expiresAtUtc
            FROM social_room_invites i JOIN users u ON u.id=i.sender_id JOIN game_rooms r ON r.id=i.room_id
            WHERE i.recipient_id=@me AND i.expires_at_utc>UTC_TIMESTAMP(6) AND r.status='Waiting'
            AND EXISTS(SELECT 1 FROM room_members m WHERE m.user_id=i.sender_id AND m.room_id=i.room_id)
            AND NOT EXISTS(SELECT 1 FROM social_blocks b WHERE (b.owner_id=@me AND b.target_id=i.sender_id) OR (b.owner_id=i.sender_id AND b.target_id=@me))
            ORDER BY i.created_at_utc DESC LIMIT 30
            """,ct,("me",user.Id));
        var conversations=await ConversationRows(c,user.Id,ct);
        return Results.Ok(new {friends,requests,blocks,messages,notifications,me=me[0],statusMessages,invites,conversations});
    }

    private static async Task<IResult> ActSafe(ActionRequest input,HttpRequest req,AuthDatabase db,CancellationToken ct)
    {
        try{return await Act(input,req,db,ct);}
        catch(MySqlException ex)when(ex.Number is 1062 or 1213 or 1205){return Results.Conflict(new{message="방이나 친구 정보가 변경 중입니다. 상태를 확인하고 다시 시도하세요."});}
    }

    private static async Task<IResult> Act(ActionRequest input,HttpRequest req,AuthDatabase db,CancellationToken ct)
    {
        var user=await LobbyEndpoints.AuthenticateAsync(req,db,ct);
        if(user is null)return Results.Unauthorized();
        await using var c=await db.OpenConnectionAsync(ct);
        bool allowObservers=req.HttpContext.RequestServices.GetRequiredService<IConfiguration>().GetValue<bool>("Bots:EnableOpenObservers");
        if(input.Operation is "AcceptInvite" or "DeclineInvite")return await RespondToRoomInvite(c,user.Id,user.DisplayName,input,allowObservers,ct);
        if(input.Operation=="Presence")
        {
            if(!ValidPresence(input.Presence)|| input.Context is not ("Lobby" or "Game" or "Other"))return Error("접속 상태가 올바르지 않습니다.");
            await Exec(c,null,"INSERT INTO social_presence(user_id,presence,context,last_seen_utc) VALUES(@me,@p,@context,UTC_TIMESTAMP(6)) ON DUPLICATE KEY UPDATE presence=@p,context=@context,last_seen_utc=UTC_TIMESTAMP(6)",ct,("me",user.Id),("p",input.Presence),("context",input.Context));
            return Results.Ok(new {message="상태가 갱신되었습니다."});
        }
        if(input.Operation=="Read")
        {
            await Exec(c,null,"UPDATE social_notifications SET unread=0 WHERE user_id=@me",ct,("me",user.Id));
            return Results.Ok(new {message="알림을 확인했습니다."});
        }
        if(string.IsNullOrWhiteSpace(input.Target)|| input.Target.Length>32)return Error("상대 계정 ID를 입력하세요.");
        await using var tx=await c.BeginTransactionAsync(ct);
        // Serialize per-account social mutations; pair locks always acquired in ascending user-id order.
        var people=await Rows(c,tx,"SELECT id,account_id FROM users WHERE id=@me OR account_id=@target ORDER BY id FOR UPDATE",ct,("me",user.Id),("target",input.Target.Trim()));
        var other=people.FirstOrDefault(p=>Convert.ToUInt64(p["id"])!=user.Id);
        if(other is null)return Error("상대 계정을 확인하세요.");
        ulong target=Convert.ToUInt64(other["id"]);
        var pair=new (string,object?)[]{("me",user.Id),("other",target),("low",Math.Min(user.Id,target)),("high",Math.Max(user.Id,target))};
        bool blocked=await Scalar(c,tx,"SELECT COUNT(*) FROM social_blocks WHERE (owner_id=@me AND target_id=@other) OR (owner_id=@other AND target_id=@me)",ct,pair)>0;
        switch(input.Operation)
        {
            case "DeleteConversation":
            case "ReadConversation":
                if(input.Operation=="ReadConversation"&&(!ulong.TryParse(input.Text,out _)||input.Text.Length>20))return Error("읽은 대화 정보를 확인하세요.");
                await Exec(c,tx,"""
                    INSERT INTO social_conversation_state(owner_id,target_id,deleted_through_id,read_through_id)
                    SELECT @me,@other,IF(@delete,COALESCE(MAX(id),0),0),IF(@delete,COALESCE(MAX(id),0),LEAST(@read,COALESCE(MAX(id),0))) FROM social_whispers
                    WHERE (sender_id=@me AND recipient_id=@other) OR (sender_id=@other AND recipient_id=@me)
                    ON DUPLICATE KEY UPDATE deleted_through_id=IF(@delete,GREATEST(deleted_through_id,VALUES(deleted_through_id)),deleted_through_id),
                      read_through_id=GREATEST(read_through_id,VALUES(read_through_id))
                    """,ct,[..pair,("delete",input.Operation=="DeleteConversation"),("read",ulong.TryParse(input.Text,out ulong readId)?readId:0UL)]);
                break;
            case "Block":
                if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_blocks WHERE owner_id=@me",ct,pair)>=500)return Error("차단 목록은 최대 500명입니다.");
                await Exec(c,tx,"INSERT IGNORE INTO social_blocks(owner_id,target_id) VALUES(@me,@other); DELETE FROM social_requests WHERE (sender_id=@me AND recipient_id=@other) OR (sender_id=@other AND recipient_id=@me); DELETE FROM social_whispers WHERE (sender_id=@me AND recipient_id=@other) OR (sender_id=@other AND recipient_id=@me)",ct,pair);
                await Exec(c,tx,"DELETE FROM social_room_invites WHERE (sender_id=@me AND recipient_id=@other) OR (sender_id=@other AND recipient_id=@me)",ct,pair);
                break;
            case "Unblock":await Exec(c,tx,"DELETE FROM social_blocks WHERE owner_id=@me AND target_id=@other",ct,pair);break;
            case "Remove":await Exec(c,tx,"DELETE FROM social_friends WHERE user_low=@low AND user_high=@high;DELETE FROM social_room_invites WHERE (sender_id=@me AND recipient_id=@other) OR (sender_id=@other AND recipient_id=@me)",ct,pair);break;
            case "Invite":
                var inviteError=await InviteToRoom(c,tx,user.Id,target,user.DisplayName,blocked,allowObservers,ct);
                if(inviteError is not null)return inviteError;
                break;
            case "Decline":await Exec(c,tx,"DELETE FROM social_requests WHERE sender_id=@other AND recipient_id=@me",ct,pair);break;
            case "Request":
                if(blocked)return Error("요청을 전달할 수 없습니다.");
                if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_friends WHERE user_low=@me OR user_high=@me",ct,pair)>=200)return Error("친구는 최대 200명입니다.");
                if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_friends WHERE user_low=@low AND user_high=@high",ct,pair)>0)return Error("이미 친구입니다.");
                if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_requests WHERE recipient_id=@other",ct,pair)>=100)return Error("요청을 전달할 수 없습니다.");
                if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_requests WHERE sender_id=@me",ct,pair)>=100)return Error("보낸 친구 요청은 최대 100개입니다.");
                int inserted=await Exec(c,tx,"INSERT IGNORE INTO social_requests(sender_id,recipient_id) VALUES(@me,@other)",ct,pair);
                if(inserted>0)await Notify(c,tx,target,"FriendRequest",user.DisplayName+"님이 친구를 요청했습니다.",ct);
                break;
            case "Accept":
                if(blocked)return Error("요청을 수락할 수 없습니다.");
                if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_friends WHERE user_low=@me OR user_high=@me",ct,pair)>=200 || await Scalar(c,tx,"SELECT COUNT(*) FROM social_friends WHERE user_low=@other OR user_high=@other",ct,pair)>=200)return Error("친구 목록이 가득 찼습니다.");
                int removed=await Exec(c,tx,"DELETE FROM social_requests WHERE sender_id=@other AND recipient_id=@me",ct,pair);
                if(removed==0)return Error("만료된 친구 요청입니다.");
                await Exec(c,tx,"INSERT IGNORE INTO social_friends(user_low,user_high) VALUES(@low,@high); DELETE FROM social_requests WHERE sender_id=@me AND recipient_id=@other",ct,pair);
                await Notify(c,tx,target,"FriendAccepted",user.DisplayName+"님이 친구 요청을 수락했습니다.",ct);
                break;
            case "Whisper":
                if(blocked|| !ValidText(input.Text,500))return Error("메시지를 전달할 수 없습니다. (최대 500자)");
                if(await Scalar(c,tx,"""
                    SELECT COUNT(*) FROM social_presence p WHERE p.user_id IN(@me,@other) AND p.context='Lobby'
                      AND p.last_seen_utc>UTC_TIMESTAMP(6)-INTERVAL 20 SECOND AND p.presence<>'Offline'
                      AND EXISTS(SELECT 1 FROM auth_sessions s WHERE s.user_id=p.user_id AND s.revoked_at_utc IS NULL AND s.access_expires_at_utc>UTC_TIMESTAMP(6))
                      AND NOT EXISTS(SELECT 1 FROM room_members m JOIN game_rooms r ON r.id=m.room_id WHERE m.user_id=p.user_id AND r.status='Started' AND m.returned_to_room=0)
                    """,ct,pair)!=2)return Error("귓속말은 두 플레이어 모두 로비 또는 방에 있을 때만 가능합니다.");
                if(await Scalar(c,tx,"SELECT COUNT(*) FROM social_whispers WHERE sender_id=@me AND created_at_utc>UTC_TIMESTAMP(6)-INTERVAL 10 SECOND",ct,pair)>=5)return Error("잠시 후 다시 보내세요.");
                await Exec(c,tx,"INSERT INTO social_whispers(sender_id,recipient_id,body) VALUES(@me,@other,@body)",ct,[..pair,("body",input.Text!.Trim())]);
                await Notify(c,tx,target,"Whisper",user.DisplayName+"님의 귓속말이 도착했습니다.",ct);
                await Exec(c,tx,"DELETE FROM social_whispers WHERE created_at_utc<UTC_TIMESTAMP(6)-INTERVAL 7 DAY LIMIT 500",ct);
                break;
            default:return Error("지원하지 않는 요청입니다.");
        }
        await tx.CommitAsync(ct);
        return Results.Ok(new {message="처리되었습니다."});
    }

    private static async Task Notify(MySqlConnection c,MySqlTransaction tx,ulong user,string kind,string body,CancellationToken ct)
    {
        await Exec(c,tx,"INSERT INTO social_notifications(user_id,kind,body) VALUES(@u,@kind,@body)",ct,("u",user),("kind",kind),("body",body));
        await Exec(c,tx,"DELETE FROM social_notifications WHERE user_id=@u AND id NOT IN(SELECT id FROM (SELECT id FROM social_notifications WHERE user_id=@u ORDER BY id DESC LIMIT 100) retained)",ct,("u",user));
    }
    internal static async Task<int> Exec(MySqlConnection c,MySqlTransaction? tx,string sql,CancellationToken ct,params (string,object?)[] args)
    {
        await using var cmd=Command(c,tx,sql,args);return await cmd.ExecuteNonQueryAsync(ct);
    }
    private static async Task<long> Scalar(MySqlConnection c,MySqlTransaction tx,string sql,CancellationToken ct,params (string,object?)[] args)
    {
        await using var cmd=Command(c,tx,sql,args);return Convert.ToInt64(await cmd.ExecuteScalarAsync(ct));
    }
    internal static async Task<List<Dictionary<string,object?>>> Rows(MySqlConnection c,MySqlTransaction? tx,string sql,CancellationToken ct,params (string,object?)[] args)
    {
        await using var cmd=Command(c,tx,sql,args);await using var r=await cmd.ExecuteReaderAsync(ct);
        var rows=new List<Dictionary<string,object?>>();
        while(await r.ReadAsync(ct)){var row=new Dictionary<string,object?>();for(int i=0;i<r.FieldCount;i++)row[r.GetName(i)]=r.IsDBNull(i)?null:r.GetValue(i);rows.Add(row);}return rows;
    }
    private static MySqlCommand Command(MySqlConnection c,MySqlTransaction? tx,string sql,(string,object?)[] args)
    {
        var cmd=new MySqlCommand(sql,c,tx){CommandTimeout=5};foreach(var (key,value) in args)cmd.Parameters.AddWithValue("@"+key,value??DBNull.Value);return cmd;
    }
}

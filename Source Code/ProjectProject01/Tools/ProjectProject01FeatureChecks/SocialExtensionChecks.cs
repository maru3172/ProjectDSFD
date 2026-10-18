using System.Net;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

internal static class SocialExtensionChecks
{
    public static async Task RunAsync(AuthDatabase db,HttpClient admin,(HttpClient Client,string Account,ulong Id) a,(HttpClient Client,string Account,ulong Id) b)
    {
        int passed=0;
        void Check(bool ok,string name){if(!ok)throw new Exception(name);passed++;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(HttpResponseMessage response)
        {using(response){string body=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception($"Social extension {(int)response.StatusCode}: {body}");using var j=JsonDocument.Parse(body);return j.RootElement.Clone();}}
        async Task Expect(HttpResponseMessage response,HttpStatusCode status,string name){using(response)Check(response.StatusCode==status,name+" "+response.StatusCode);}
        async Task Act(HttpClient client,string op,string target,string? text=null)=>await Read(await client.PostAsJsonAsync("/api/social/action",new{operation=op,target,text}));
        async Task Sql(string sql,params (string,object?)[] args){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,args);}
        async Task<JsonElement> Snap(HttpClient client)=>await Read(await client.GetAsync("/api/social"));
        using var anon=new HttpClient{BaseAddress=admin.BaseAddress};
        string? roomId=null;
        try
        {
            await Expect(await anon.GetAsync("/api/social/search?q=ab"),HttpStatusCode.Unauthorized,"미로그인 친구 검색 차단");
            await Expect(await anon.GetAsync("/api/social/profiles/"+a.Account),HttpStatusCode.Unauthorized,"미로그인 프로필 조회 차단");
            await Read(await a.Client.PutAsJsonAsync("/api/social/profile",new{statusMessage="모두에게 공개할 한글 상태",bio="내 소개글",accent="Ocean",title="Explorer"}));
            var profile=await Read(await b.Client.GetAsync("/api/social/profiles/"+a.Account));
            Check(profile.GetProperty("profile").GetProperty("bio").GetString()=="내 소개글"&&profile.GetProperty("profile").GetProperty("accent").GetString()=="Ocean","프로필 꾸미기 한글·색상·칭호 저장");
            Check(profile.GetProperty("statistics").GetArrayLength()==0&&!profile.GetProperty("isSelf").GetBoolean(),"없는 전적을 만들어 표시하지 않음");
            await Sql("INSERT INTO verified_match_results(match_id,room_id,user_id,role,success,rescue_count,escape_seconds) VALUES(@m,@r,@u,'Survivor',TRUE,3,42.5)",("m",Guid.NewGuid().ToString("D")),("r",Guid.NewGuid().ToString("D")),("u",a.Id));
            profile=await Read(await b.Client.GetAsync("/api/social/profiles/"+a.Account));var stat=profile.GetProperty("statistics")[0];
            Check(stat.GetProperty("wins").GetInt32()==1&&stat.GetProperty("rescues").GetInt32()==3&&stat.GetProperty("bestEscapeSeconds").GetDouble()==42.5,"프로필에 서버 검증 전적 합계·탈출 시간 연결");
            await Sql("DELETE FROM verified_match_results WHERE user_id=@u",("u",a.Id));
            Check((await Snap(b.Client)).GetProperty("statusMessages").EnumerateArray().Any(s=>s.GetProperty("accountId").GetString()==a.Account),"전체 상태메시지 공개 피드");
            await Expect(await a.Client.PutAsJsonAsync("/api/social/profile",new{statusMessage=new string('x',161),bio="",accent="Ocean",title="Explorer"}),HttpStatusCode.BadRequest,"프로필 길이 제한");
            await Expect(await a.Client.PutAsJsonAsync("/api/social/profile",new{statusMessage="",bio="",accent="script",title="Admin"}),HttpStatusCode.BadRequest,"제공하지 않은 프로필 색상·칭호 차단");
            await Expect(await a.Client.PutAsJsonAsync("/api/social/profile",new{statusMessage="너무 빠른 변경",bio="",accent="Ocean",title="Explorer"}),HttpStatusCode.TooManyRequests,"프로필 변경 빈도 제한");
            var found=await Read(await b.Client.GetAsync("/api/social/search?q="+a.Account));Check(found.GetProperty("players").GetArrayLength()==1,"계정 ID로 친구 검색");
            found=await Read(await b.Client.GetAsync("/api/social/search?q=%25%25"));Check(found.GetProperty("players").GetArrayLength()==0,"검색 SQL 와일드카드 이스케이프");

            await Act(a.Client,"Whisper",b.Account,"삭제 이전 대화");
            var conversation=await Read(await b.Client.GetAsync("/api/social/conversations/"+a.Account));
            string lastId=conversation.GetProperty("messages")[0].GetProperty("id").GetString()!;
            Check(conversation.GetProperty("messages").GetArrayLength()==1,"상대별 채팅창 기록 조회");
            Check((await Snap(b.Client)).GetProperty("conversations")[0].GetProperty("unread").GetBoolean(),"대화 목록 읽지 않은 알림");
            await Act(b.Client,"ReadConversation",a.Account,lastId);
            Check(!(await Snap(b.Client)).GetProperty("conversations")[0].GetProperty("unread").GetBoolean(),"본 대화만 읽음 처리");
            await Act(b.Client,"DeleteConversation",a.Account);
            Check((await Snap(b.Client)).GetProperty("conversations").GetArrayLength()==0,"내 대화 목록 삭제");
            Check((await Read(await a.Client.GetAsync("/api/social/conversations/"+b.Account))).GetProperty("messages").GetArrayLength()==1,"삭제해도 상대 기록 유지");
            await Act(a.Client,"Whisper",b.Account,"삭제 이후 새 대화");
            conversation=await Read(await b.Client.GetAsync("/api/social/conversations/"+a.Account));
            Check(conversation.GetProperty("messages").GetArrayLength()==1&&conversation.GetProperty("messages")[0].GetProperty("body").GetString()=="삭제 이후 새 대화","새 메시지로 대화 재생성·과거 기록은 삭제 유지");
            await Expect(await anon.GetAsync("/api/social/conversations/"+a.Account),HttpStatusCode.Unauthorized,"비로그인 대화 조회 차단");
            // Clear fixtures so the original community checks still start from their expected empty history.
            await Sql("DELETE FROM social_whispers WHERE sender_id IN(@a,@b);DELETE FROM social_conversation_state WHERE owner_id IN(@a,@b);DELETE FROM social_notifications WHERE user_id IN(@a,@b) AND kind='Whisper'",("a",a.Id),("b",b.Id));

            var room=await Read(await a.Client.PostAsJsonAsync("/api/rooms",new{name="친구 초대 전용 비공개 방",isPublic=false,password="fixture-pass"}));
            roomId=room.GetProperty("room").GetProperty("roomId").GetString()!;
            await Act(a.Client,"Invite",b.Account);
            string invite=(await Snap(b.Client)).GetProperty("invites")[0].GetProperty("inviteId").GetString()!;
            await using(var c=await db.OpenConnectionAsync(default))Check((await CommunityEndpoints.Rows(c,null,"SELECT user_id FROM room_members WHERE user_id=@b",default,("b",b.Id))).Count==0,"초대 수신만으로 입장하지 않음");
            await Expect(await a.Client.PostAsJsonAsync("/api/social/action",new{operation="AcceptInvite",target=invite}),HttpStatusCode.BadRequest,"타인의 방 초대 수락 차단");
            await Act(b.Client,"DeclineInvite",invite);
            Check((await Snap(b.Client)).GetProperty("invites").GetArrayLength()==0,"거절은 입장 없이 초대만 정리");
            await Act(a.Client,"Invite",b.Account);invite=(await Snap(b.Client)).GetProperty("invites")[0].GetProperty("inviteId").GetString()!;
            await Sql("UPDATE social_room_invites SET expires_at_utc=UTC_TIMESTAMP()-INTERVAL 1 SECOND WHERE id=@id",("id",invite));
            await Expect(await b.Client.PostAsJsonAsync("/api/social/action",new{operation="AcceptInvite",target=invite}),HttpStatusCode.BadRequest,"만료된 초대 입장 차단");
            await Act(a.Client,"Invite",b.Account);invite=(await Snap(b.Client)).GetProperty("invites")[0].GetProperty("inviteId").GetString()!;
            await Sql("UPDATE game_rooms SET status='Starting' WHERE id=@room",("room",roomId));
            await Expect(await b.Client.PostAsJsonAsync("/api/social/action",new{operation="AcceptInvite",target=invite}),HttpStatusCode.BadRequest,"경기 시작 중 초대 입장 차단");
            await Sql("UPDATE game_rooms SET status='Waiting' WHERE id=@room",("room",roomId));
            await Act(b.Client,"AcceptInvite",invite);
            var current=await Read(await b.Client.GetAsync("/api/rooms/current"));Check(current.GetProperty("room").GetProperty("roomId").GetString()==roomId,"수락한 초대만 비공개 방 참가 허용");
            profile=await Read(await a.Client.GetAsync("/api/social/room-profiles/"+b.Id));
            Check(profile.GetProperty("profile").GetProperty("accountId").GetString()==b.Account,"동명이인도 참가자 ID로 정확한 방 프로필 조회");
            Check(profile.GetProperty("isFriend").GetBoolean()&&!profile.GetProperty("isBlocked").GetBoolean(),"방 우클릭 메뉴에 기존 친구·차단 여부 전달");
            await Act(a.Client,"Remove",b.Account);
            Check(!(await Read(await a.Client.GetAsync("/api/social/room-profiles/"+b.Id))).GetProperty("isFriend").GetBoolean(),"친구 삭제 후 메뉴가 친구 추가로 전환");
            await Act(a.Client,"Request",b.Account);
            Check(!(await Read(await a.Client.GetAsync("/api/social/room-profiles/"+b.Id))).GetProperty("isFriend").GetBoolean(),"방에서 보낸 친구 요청도 상대 수락 전에는 친구가 아님");
            await Act(b.Client,"Accept",a.Account);
            await Act(a.Client,"Block",b.Account);
            Check((await Read(await a.Client.GetAsync("/api/social/room-profiles/"+b.Id))).GetProperty("isBlocked").GetBoolean(),"본인이 차단한 참가자는 차단 해제 메뉴 제공");
            await Act(a.Client,"Unblock",b.Account);
            await Expect(await anon.PostAsJsonAsync("/api/social/reports",new{targetUserId=b.Id.ToString(),reason="Abuse",details="검증용 신고"}),HttpStatusCode.Unauthorized,"비로그인 신고 차단");
            await Expect(await a.Client.PostAsJsonAsync("/api/social/reports",new{targetUserId=a.Id.ToString(),reason="Abuse",details="본인 신고"}),HttpStatusCode.BadRequest,"본인 신고 차단");
            await Expect(await a.Client.PostAsJsonAsync("/api/social/reports",new{targetUserId=b.Id.ToString(),reason="Unknown",details="잘못된 사유"}),HttpStatusCode.BadRequest,"제공하지 않은 신고 사유 차단");
            await Expect(await a.Client.PostAsJsonAsync("/api/social/reports",new{targetUserId=b.Id.ToString(),reason="Other",details=" "}),HttpStatusCode.BadRequest,"상세 신고 사유 필수");
            await Expect(await a.Client.PostAsJsonAsync("/api/social/reports",new{targetUserId=b.Id.ToString(),reason="Other",details=new string('x',1001)}),HttpStatusCode.BadRequest,"과도한 신고 내용 길이 차단");
            var receipt=await Read(await a.Client.PostAsJsonAsync("/api/social/reports",new{targetUserId=b.Id.ToString(),reason="Griefing",details="한글 신고 사유: 게임 진행을 반복적으로 방해했습니다."}));
            string reportId=receipt.GetProperty("id").GetString()!;
            await Expect(await a.Client.PostAsJsonAsync("/api/social/reports",new{targetUserId=b.Id.ToString(),reason="Griefing",details="반복 신고"}),HttpStatusCode.TooManyRequests,"동일 신고 반복 제출 차단");
            await Expect(await a.Client.GetAsync("/api/admin/community/reports"),HttpStatusCode.Unauthorized,"일반 로그인 사용자 신고 목록 조회 차단");
            await Expect(await anon.GetAsync("/api/admin/community/reports"),HttpStatusCode.Unauthorized,"비로그인 신고 목록 조회 차단");
            var reportPage=await Read(await admin.GetAsync("/api/admin/community/reports?status=Open"));
            var report=reportPage.GetProperty("reports").EnumerateArray().Single(r=>r.GetProperty("id").GetString()==reportId);
            Check(report.GetProperty("reporterAccount").GetString()==a.Account&&report.GetProperty("targetAccount").GetString()==b.Account&&report.GetProperty("roomId").GetString()==roomId&&report.GetProperty("details").GetString()!.StartsWith("한글 신고 사유"),"운영자에게 정확한 신고자·대상·방·한글 사유 전달");
            await Expect(await a.Client.PutAsJsonAsync("/api/admin/community/reports/"+reportId,new{status="Reviewed",note="변조"}),HttpStatusCode.Unauthorized,"일반 클라이언트 신고 검토 변경 차단");
            await Expect(await admin.PutAsJsonAsync("/api/admin/community/reports/"+reportId,new{status="Banned",note="자동 제재 안됨"}),HttpStatusCode.BadRequest,"신고 API에서 자동 제재 상태 차단");
            await Read(await admin.PutAsJsonAsync("/api/admin/community/reports/"+reportId,new{status="Reviewed",note="운영자 확인 메모"}));
            reportPage=await Read(await admin.GetAsync("/api/admin/community/reports?status=Reviewed"));
            Check(reportPage.GetProperty("reports").EnumerateArray().Any(r=>r.GetProperty("id").GetString()==reportId&&r.GetProperty("reviewNote").GetString()=="운영자 확인 메모"),"운영자 검토 상태·메모 저장 확인");
            Check((await Read(await a.Client.GetAsync("/api/social/room-profiles/"+a.Id))).GetProperty("isSelf").GetBoolean(),"방 참가자 목록에서 본인 프로필 조회 허용");
            await Expect(await anon.GetAsync("/api/social/room-profiles/"+b.Id),HttpStatusCode.Unauthorized,"로그인 전 방 참가자 프로필 조회 차단");
            await Expect(await a.Client.GetAsync("/api/social/room-profiles/not-an-id"),HttpStatusCode.BadRequest,"잘못된 방 참가자 ID 차단");
            Check((await Snap(b.Client)).GetProperty("invites").GetArrayLength()==0,"수락 초대 일회성 소비");
            await Expect(await b.Client.PostAsJsonAsync("/api/social/action",new{operation="AcceptInvite",target=invite}),HttpStatusCode.BadRequest,"동일 초대 중복 수락 차단");
            await Read(await b.Client.PostAsync("/api/rooms/current/leave",null));
            await Expect(await a.Client.GetAsync("/api/social/room-profiles/"+b.Id),HttpStatusCode.NotFound,"방에서 나간 참가자의 방 전용 프로필 조회 차단");
            await Expect(await a.Client.PostAsJsonAsync("/api/social/reports",new{targetUserId=b.Id.ToString(),reason="Other",details="방을 나간 대상"}),HttpStatusCode.NotFound,"같은 방이 아닌 대상 신고 차단");
            Check((await Read(await admin.GetAsync("/api/admin/community/reports?status=Reviewed"))).GetProperty("reports").EnumerateArray().Any(r=>r.GetProperty("id").GetString()==reportId),"퇴장 후에도 이미 접수된 신고 보존");
            await Expect(await anon.PostAsJsonAsync("/api/social/reports",new{targetAccount=a.Account,reason="Other",details="로그인 전 직접 신고"}),HttpStatusCode.Unauthorized,"미로그인 아이디 직접 신고 차단");
            await Expect(await b.Client.PostAsJsonAsync("/api/social/reports",new{targetAccount=b.Account,reason="Other",details="직접 본인 신고"}),HttpStatusCode.BadRequest,"아이디 직접 입력해도 본인 신고 차단");
            await Expect(await b.Client.PostAsJsonAsync("/api/social/reports",new{targetAccount="no_user_"+Guid.NewGuid().ToString("N")[..12],reason="Other",details="없는 계정"}),HttpStatusCode.NotFound,"존재하지 않는 신고 대상 아이디 차단");
            await Expect(await b.Client.PostAsJsonAsync("/api/social/reports",new{targetAccount=a.Account,targetUserId=a.Id.ToString(),reason="Other",details="두 대상 지정"}),HttpStatusCode.BadRequest,"아이디와 참가자 대상 동시 지정 차단");
            receipt=await Read(await b.Client.PostAsJsonAsync("/api/social/reports",new{targetAccount=a.Account,reason="Abuse",details="같은 방이 아니어도 아이디를 알고 신고합니다."}));
            string directReportId=receipt.GetProperty("id").GetString()!;
            report=(await Read(await admin.GetAsync("/api/admin/community/reports?status=Open"))).GetProperty("reports").EnumerateArray().Single(r=>r.GetProperty("id").GetString()==directReportId);
            Check(report.GetProperty("targetAccount").GetString()==a.Account&&report.GetProperty("reporterAccount").GetString()==b.Account&&report.GetProperty("roomId").GetString()==""&&report.GetProperty("roomName").GetString()!.StartsWith("아이디 직접 신고"),"별도 신고 버튼: 아이디로 신고 접수·동일 방이라고 허위 기록하지 않음");
            await Expect(await b.Client.PostAsJsonAsync("/api/social/reports",new{targetAccount=a.Account,reason="Abuse",details="직접 신고 반복"}),HttpStatusCode.TooManyRequests,"아이디 신고도 동일한 반복 제출 제한");
            var previous=await Read(await admin.GetAsync("/api/admin/community/reports?before="+directReportId));
            Check(previous.GetProperty("reports").EnumerateArray().Any(r=>r.GetProperty("id").GetString()==reportId)&&!previous.GetProperty("reports").EnumerateArray().Any(r=>r.GetProperty("id").GetString()==directReportId),"신고 목록 이전 접수 조회 경계 정확성");
            await Act(a.Client,"Invite",b.Account);await Act(b.Client,"Block",a.Account);
            Check((await Snap(b.Client)).GetProperty("invites").GetArrayLength()==0,"차단하면 보류 초대도 취소");await Act(b.Client,"Unblock",a.Account);

            // Arm a fixture session, renew it, then simulate force-kill by aging only its deadline.
            await Sql("INSERT INTO auth_session_liveness(session_id,deadline_utc) SELECT id,UTC_TIMESTAMP()+INTERVAL 90 SECOND FROM auth_sessions WHERE user_id=@a AND revoked_at_utc IS NULL",("a",a.Id));
            await Expect(await a.Client.PostAsync("/api/auth/heartbeat",null),HttpStatusCode.OK,"로그인 클라이언트 접속 확인 갱신");
            byte[] oldHash=SHA256.HashData(Encoding.UTF8.GetBytes(Guid.NewGuid().ToString("N")));
            await Sql("INSERT INTO auth_sessions(user_id,access_token_hash,refresh_token_hash,access_expires_at_utc,refresh_expires_at_utc) VALUES(@u,@h,@h,UTC_TIMESTAMP()+INTERVAL 1 HOUR,UTC_TIMESTAMP()+INTERVAL 1 DAY);INSERT INTO auth_session_liveness(session_id,deadline_utc) VALUES(LAST_INSERT_ID(),UTC_TIMESTAMP()-INTERVAL 1 SECOND)",("u",a.Id),("h",oldHash));
            Check(await ClientSessionLiveness.SweepAsync(db,default)==1,"이전 프로세스의 만료 세션만 정리");
            Check((await Read(await a.Client.GetAsync("/api/rooms/current"))).GetProperty("room").GetProperty("roomId").GetString()==roomId,"새 로그인 세션과 방은 이전 세션 정리에 영향 없음");
            await Sql("DELETE FROM auth_sessions WHERE access_token_hash=@h",("h",oldHash));
            await Sql("UPDATE auth_session_liveness l JOIN auth_sessions s ON s.id=l.session_id SET l.deadline_utc=UTC_TIMESTAMP()-INTERVAL 1 SECOND WHERE s.user_id=@a",("a",a.Id));
            await Expect(await a.Client.PostAsync("/api/auth/heartbeat",null),HttpStatusCode.Unauthorized,"유예 만료 후 늦은 접속 확인으로 세션 부활 차단");
            Check(await ClientSessionLiveness.SweepAsync(db,default)==1,"강제 종료 세션 서버 측 정리");
            await using(var c=await db.OpenConnectionAsync(default))Check((await CommunityEndpoints.Rows(c,null,"SELECT id FROM game_rooms WHERE id=@room",default,("room",roomId))).Count==0,"마지막 인원 종료 시 방 안전 정리");
            Check((await Read(await admin.GetAsync("/api/admin/community/reports?status=Reviewed"))).GetProperty("reports").EnumerateArray().Any(r=>r.GetProperty("id").GetString()==reportId),"방 삭제 뒤에도 접수 당시 신고 맥락 보존");
            // Restore this test's token only (isolated DB) for the original checks that follow.
            await Sql("DELETE l FROM auth_session_liveness l JOIN auth_sessions s ON s.id=l.session_id WHERE s.user_id=@a;UPDATE auth_sessions SET revoked_at_utc=NULL WHERE user_id=@a",("a",a.Id));
            await Read(await a.Client.PostAsJsonAsync("/api/social/action",new{operation="Presence",presence="Online",context="Lobby"}));
            Console.WriteLine($"PASS: {passed} extended social checks");
        }
        finally{await Sql("DELETE FROM player_reports WHERE reporter_id IN(@a,@b)",("a",a.Id),("b",b.Id));if(roomId is not null)await Sql("DELETE FROM game_rooms WHERE id=@room",("room",roomId));}
    }
}

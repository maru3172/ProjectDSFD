using System.Net;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MySqlConnector;

internal static class CommunityChecks
{
    public static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int passed=0;
        void Check(bool ok,string message){if(!ok)throw new Exception(message);passed++;Console.WriteLine("PASS "+message);}
        async Task<JsonElement> Read(HttpResponseMessage response)
        {
            using(response){var text=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception($"Community HTTP {(int)response.StatusCode}: {text}");using var json=JsonDocument.Parse(text);return json.RootElement.Clone();}
        }
        async Task Expect(HttpResponseMessage response,HttpStatusCode code,string name)
        {using(response){Check(response.StatusCode==code,name+": "+response.StatusCode);}}
        async Task<(HttpClient Client,string Account,ulong Id)> Player()
        {
            string account="social_"+Guid.NewGuid().ToString("N")[..16],token=Guid.NewGuid().ToString("N");
            await using var c=await db.OpenConnectionAsync(default);
            await CommunityEndpoints.Exec(c,null,"INSERT INTO users(account_id,display_name,password_hash) VALUES(@a,'친구 검증 계정','unused')",default,("a",account));
            var users=await CommunityEndpoints.Rows(c,null,"SELECT id FROM users WHERE account_id=@a",default,("a",account));ulong id=Convert.ToUInt64(users[0]["id"]);
            await CommunityEndpoints.Exec(c,null,"INSERT INTO auth_sessions(user_id,access_token_hash,refresh_token_hash,access_expires_at_utc,refresh_expires_at_utc) VALUES(@u,@hash,@hash,UTC_TIMESTAMP()+INTERVAL 1 HOUR,UTC_TIMESTAMP()+INTERVAL 1 DAY)",default,("u",id),("hash",SHA256.HashData(Encoding.UTF8.GetBytes(token))));
            var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",token);return(client,account,id);
        }
        var a=await Player();var b=await Player();
        using var anon=new HttpClient{BaseAddress=admin.BaseAddress};
        string marker="소셜검증_"+Guid.NewGuid().ToString("N");
        string? imageId=null;
        try
        {
            async Task<JsonElement> Act(HttpClient client,string op,string? target=null,string? text=null,string? presence=null,string? context=null)=>await Read(await client.PostAsJsonAsync("/api/social/action",new{operation=op,target,text,presence,context}));
            await Expect(await anon.GetAsync("/api/social"),HttpStatusCode.Unauthorized,"로그인 전 차단 목록·친구·메시지 조회 차단");
            await Expect(await anon.PostAsJsonAsync("/api/social/action",new{operation="Block",target=b.Account}),HttpStatusCode.Unauthorized,"로그인 전 차단 변경 차단");
            await Act(a.Client,"Presence",presence:"Online",context:"Lobby");await Act(b.Client,"Presence",presence:"Online",context:"Lobby");
            await Act(a.Client,"Request",b.Account);
            var snap=await Read(await b.Client.GetAsync("/api/social"));Check(snap.GetProperty("requests").GetArrayLength()==1,"친구 요청 수신");
            Check(snap.GetProperty("friends").GetArrayLength()==0,"친구 요청 수신만으로 자동 친구 등록 금지");
            string firstRequestStamp=snap.GetProperty("requests")[0].GetProperty("createdAtUtc").GetString()!;
            Check(snap.GetProperty("notifications").EnumerateArray().Any(n=>n.GetProperty("unread").GetBoolean()),"친구 요청 알림 레드닷 상태");
            await Act(b.Client,"Decline",a.Account);snap=await Read(await b.Client.GetAsync("/api/social"));
            Check(snap.GetProperty("requests").GetArrayLength()==0&&snap.GetProperty("friends").GetArrayLength()==0,"친구 요청 거절 시 친구로 등록하지 않음");
            await Act(a.Client,"Request",b.Account);snap=await Read(await b.Client.GetAsync("/api/social"));
            Check(snap.GetProperty("requests")[0].GetProperty("createdAtUtc").GetString()!=firstRequestStamp,"같은 계정의 새 요청도 별도 확인 가능");
            await Act(b.Client,"Accept",a.Account);
            snap=await Read(await a.Client.GetAsync("/api/social"));Check(snap.GetProperty("friends").GetArrayLength()==1,"친구 수락 양방향 반영");
            Check(snap.GetProperty("friends")[0].GetProperty("activity").GetString()=="로비","온라인 활동 표시");
            await SocialExtensionChecks.RunAsync(db,admin,a,b);
            await Act(b.Client,"Presence",presence:"Away",context:"Lobby");
            snap=await Read(await a.Client.GetAsync("/api/social"));Check(snap.GetProperty("friends")[0].GetProperty("activity").GetString()=="","자리비움 활동 비공개");
            await Act(a.Client,"Whisper",b.Account,"한국어 귓속말 검증");
            snap=await Read(await b.Client.GetAsync("/api/social"));Check(snap.GetProperty("messages")[0].GetProperty("body").GetString()=="한국어 귓속말 검증","한국어 귓속말 전달");
            await Act(b.Client,"Presence",presence:"Online",context:"Game");
            await Expect(await a.Client.PostAsJsonAsync("/api/social/action",new{operation="Whisper",target=b.Account,text="거부"}),HttpStatusCode.BadRequest,"경기 중 귓속말 수신 차단");
            snap=await Read(await b.Client.GetAsync("/api/social"));Check(snap.GetProperty("messages").GetArrayLength()==0,"경기 화면에서는 과거 귓속말 내용도 반환하지 않음");
            await Act(b.Client,"Presence",presence:"Online",context:"Lobby");await Act(b.Client,"Read");
            snap=await Read(await b.Client.GetAsync("/api/social"));Check(!snap.GetProperty("notifications").EnumerateArray().Any(n=>n.GetProperty("unread").GetBoolean()),"알림 읽음 처리");
            int alerts=snap.GetProperty("notifications").GetArrayLength();
            await Act(a.Client,"Block",b.Account);
            snap=await Read(await a.Client.GetAsync("/api/social"));Check(snap.GetProperty("blocks").GetArrayLength()==1,"내 계정 차단 목록 저장");
            snap=await Read(await b.Client.GetAsync("/api/social"));Check(snap.GetProperty("blocks").GetArrayLength()==0&&snap.GetProperty("notifications").GetArrayLength()==alerts,"다른 계정 차단 목록과 차단 알림 비공개");
            await Expect(await b.Client.PostAsJsonAsync("/api/social/action",new{operation="Whisper",target=a.Account,text="차단 검증"}),HttpStatusCode.BadRequest,"차단 상대 메시지 서버 차단");
            await Act(a.Client,"Unblock",b.Account);await Act(b.Client,"Whisper",a.Account,"해제 후 정상");
            await Act(a.Client,"Remove",b.Account);
            snap=await Read(await b.Client.GetAsync("/api/social"));Check(snap.GetProperty("friends").GetArrayLength()==0,"친구 삭제 양방향 반영");
            await Expect(await anon.GetAsync("/api/admin/community"),HttpStatusCode.Unauthorized,"일반 클라이언트 운영 문의 열람 차단");
            await Read(await admin.PostAsJsonAsync("/api/admin/community/posts",new{kind="News",title=marker,body="한글 공지 본문",published=true}));
            await Read(await admin.PostAsJsonAsync("/api/admin/community/posts",new{kind="Patch",title=marker+"비공개",body="비공개 초안",published=false}));
            var posts=await Read(await anon.GetAsync("/api/launcher/posts"));
            Check(posts.GetProperty("posts").EnumerateArray().Any(p=>p.GetProperty("title").GetString()==marker&&p.GetProperty("body").GetString()=="한글 공지 본문"),"런처 공개 뉴스·한국어 본문");
            Check(!posts.GetProperty("posts").EnumerateArray().Any(p=>p.GetProperty("title").GetString()==marker+"비공개"),"미공개 패치노트 제외");
            byte[] photo=Convert.FromBase64String("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j2ioAAAAASUVORK5CYII=");
            Check(CommunityEndpoints.IsSafeImage(photo,out string imageType)&&imageType=="image/png","사진 PNG 헤더·크기 검사");
            Check(!CommunityEndpoints.IsSafeImage("<svg onload='bad'/>"u8,out _),"실행 가능한 SVG·HTML 첨부 차단");
            await Expect(await anon.PostAsync("/api/admin/community/images",new ByteArrayContent(photo)),HttpStatusCode.Unauthorized,"클라이언트 사진 업로드 차단");
            var uploaded=await Read(await admin.PostAsync("/api/admin/community/images",new ByteArrayContent(photo)));imageId=uploaded.GetProperty("id").GetString()!;
            await Expect(await anon.GetAsync("/api/launcher/images/"+imageId),HttpStatusCode.NotFound,"미게시 사진 공개 차단");
            await Expect(await anon.GetAsync("/api/admin/community/images/"+imageId),HttpStatusCode.Unauthorized,"관리자 미리보기 사진 일반 클라이언트 차단");
            await Expect(await admin.GetAsync("/api/admin/community/images/"+imageId),HttpStatusCode.OK,"운영자는 게시 전 사진 미리보기 가능");
            var allPosts=await Read(await admin.GetAsync("/api/admin/community"));string postId=allPosts.GetProperty("posts").EnumerateArray().Single(p=>p.GetProperty("title").GetString()==marker).GetProperty("id").GetString()!;
            await Read(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="News",title=marker,body="사진과 표\n"+uploaded.GetProperty("markup").GetString()+"\n| 항목 | 내용 |\n| --- | --- |\n| 버전 | 1.0 |",published=true}));
            string richBody="사진과 표\n"+uploaded.GetProperty("markup").GetString()+"\n| 항목 | 내용 |\n| --- | --- |\n| 버전 | 1.0 |";
            async Task<JsonElement> PublishedPost()=> (await Read(await anon.GetAsync("/api/launcher/posts"))).GetProperty("posts").EnumerateArray().Single(p=>p.GetProperty("id").GetString()==postId);
            Check(!(await PublishedPost()).GetProperty("featured").GetBoolean(),"기존 게시글은 대표 칸에 자동 선택되지 않음");
            await Read(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="Event",title=marker,body=richBody,published=true,featured=true}));
            var featuredPost=await PublishedPost();
            Check(featuredPost.GetProperty("kind").GetString()=="Event"&&featuredPost.GetProperty("featured").GetBoolean(),"운영자가 선택한 이벤트 대표 칸 설정 공개 API 전달");
            var featuredAdmin=await Read(await admin.GetAsync("/api/admin/community"));
            Check(featuredAdmin.GetProperty("posts").EnumerateArray().Single(p=>p.GetProperty("id").GetString()==postId).GetProperty("featured").GetBoolean(),"대표 칸 선택 상태를 운영자 재조회 시 유지");
            await Read(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="Event",title=marker,body=richBody,published=false,featured=true}));
            Check(!(await Read(await anon.GetAsync("/api/launcher/posts"))).GetProperty("posts").EnumerateArray().Any(p=>p.GetProperty("id").GetString()==postId),"대표 칸에 선택해도 비공개 글은 노출하지 않음");
            await Read(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="News",title=marker,body=richBody,published=true,featured=false}));
            Check(!(await PublishedPost()).GetProperty("featured").GetBoolean(),"대표 칸 선택을 해제해도 일반 공지 목록에는 유지");
            string customSummary="직접 작성한 런처 요약\n둘째 줄";
            await Read(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="News",title=marker,body=richBody,published=true,summary=customSummary}));
            var summarized=await PublishedPost();
            Check(summarized.GetProperty("summary").GetString()==customSummary&&summarized.GetProperty("body").GetString()==richBody,"런처 요약은 본문과 별도로 저장하고 줄바꿈 유지");
            var summaryAdmin=await Read(await admin.GetAsync("/api/admin/community"));
            Check(summaryAdmin.GetProperty("posts").EnumerateArray().Single(p=>p.GetProperty("id").GetString()==postId).GetProperty("summary").GetString()==customSummary,"운영자가 기존 요약을 다시 불러와 수정 가능");
            await Expect(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="News",title=marker,body=richBody,published=true,summary=new string('x',501)}),HttpStatusCode.BadRequest,"500자를 넘는 요약은 저장 거부");
            await Read(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="News",title=marker,body=richBody,published=true}));
            Check((await PublishedPost()).GetProperty("summary").GetString()==customSummary,"구형 요청에 요약 항목이 없어도 기존 요약 보존");
            await Read(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="News",title=marker,body=richBody,published=true,summary=""}));
            Check((await PublishedPost()).GetProperty("summary").GetString()=="","요약을 지우면 본문 자동 요약으로 복귀 가능");
            Check((await PublishedPost()).GetProperty("previewImageId").ValueKind==JsonValueKind.Null,"본문에 사진이 있어도 대표 미리보기는 기본 선택 없음");
            await Read(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="News",title=marker,body=richBody,published=true,previewImageId=imageId}));
            Check((await PublishedPost()).GetProperty("previewImageId").GetString()==imageId,"첨부 사진 한 장을 대표 미리보기로 저장·전달");
            await Expect(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="News",title=marker,body=richBody,published=true,previewImageId=Guid.NewGuid().ToString("D")}),HttpStatusCode.BadRequest,"첨부하지 않은 사진의 대표 미리보기 지정 차단");
            await Read(await admin.PutAsJsonAsync("/api/admin/community/posts/"+postId,new{kind="News",title=marker,body=richBody,published=true,previewImageId=(string?)null}));
            Check((await PublishedPost()).GetProperty("previewImageId").ValueKind==JsonValueKind.Null,"대표 사진 선택 해제하면 사진 미리보기 없음으로 저장");
            using(var downloaded=await anon.GetAsync("/api/launcher/images/"+imageId))Check(downloaded.IsSuccessStatusCode&&(await downloaded.Content.ReadAsByteArrayAsync()).SequenceEqual(photo),"서버 사진 첨부→게시→클라이언트 읽기");
            var receipt=await Read(await anon.PostAsJsonAsync("/api/launcher/inquiries",new{subject=marker,body="문의 본문"}));string code=receipt.GetProperty("receipt").GetString()!;
            var view=await Read(await admin.GetAsync("/api/admin/community"));var inquiry=view.GetProperty("inquiries").EnumerateArray().Single(q=>q.GetProperty("subject").GetString()==marker);
            await Read(await admin.PutAsJsonAsync("/api/admin/community/inquiries/"+inquiry.GetProperty("id").GetString(),new{reply="한글 문의 답변",status="Answered"}));
            var answer=await Read(await anon.PostAsJsonAsync("/api/launcher/inquiries/reply",new{subject="",body="",replyCode=code}));Check(answer.GetProperty("reply").GetString()=="한글 문의 답변","문의 접수→운영 답변→작성자 확인");
            await Expect(await anon.PostAsJsonAsync("/api/launcher/inquiries/reply",new{subject="",body="",replyCode=new string('0',48)}),HttpStatusCode.NotFound,"다른 문의 답변 추측 조회 차단");
            await using var c=await db.OpenConnectionAsync(default);
            await CommunityEndpoints.Exec(c,null,"UPDATE auth_sessions SET revoked_at_utc=UTC_TIMESTAMP() WHERE user_id=@u",default,("u",a.Id));
            await Expect(await a.Client.GetAsync("/api/social"),HttpStatusCode.Unauthorized,"로그아웃 후 차단 목록 재조회 차단");
            Check(!CommunityEndpoints.ValidPresence("Unknown")&&!CommunityEndpoints.ValidText(new string('x',501),500),"접속 상태·메시지 길이 검증");
            Console.WriteLine($"PASS: {passed} community checks");
        }
        finally
        {
            a.Client.Dispose();b.Client.Dispose();
            await using var c=await db.OpenConnectionAsync(default);
            await CommunityEndpoints.Exec(c,null,"DELETE FROM users WHERE id IN(@a,@b);DELETE FROM launcher_posts WHERE title IN(@m,@draft);DELETE FROM support_inquiries WHERE subject=@m",default,("a",a.Id),("b",b.Id),("m",marker),("draft",marker+"비공개"));
            if(imageId is not null)await CommunityEndpoints.Exec(c,null,"DELETE FROM launcher_images WHERE id=@id",default,("id",imageId));
        }
    }
}

using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

// Banned words (Docs/ChatFilter.md): matching through separators, allowed words, masking chat, refusing names and
// profile text, the admin lists with their test, and a list change taking effect at once.
internal static class ChatFilterChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int count=0;void Check(bool ok,string message){if(!ok)throw new Exception(message);count++;Console.WriteLine("PASS "+message);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string text=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Chat filter {(int)r.StatusCode}: {text}");return JsonDocument.Parse(text).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode status,string message){using var r=await pending;Check(r.StatusCode==status,message+" "+r.StatusCode);}

        // ---- Matching ----
        var f=ChatFilter.Create(ChatFilter.DefaultWords,ChatFilter.DefaultAllowed);
        Check(f.Mask("야 씨발 뭐해")=="야 ** 뭐해"&&f.Mask("시 . 발")=="* * *"&&f.Mask("씨-발")=="***","띄어쓰기·기호를 끼워 넣어도 찾아서 가림");
        Check(f.Mask("FuCk this")=="**** this"&&f.Contains("ㅅㅂ"),"영문 대소문자·초성 줄임말도 찾음");
        Check(!f.Contains("시발점에서 출발")&&!f.Contains("보지 마")&&!f.Contains("새끼 고양이")&&!f.Contains("자지 말고 일어나"),"허용 단어와 일상 표현(보지 마·새끼 고양이)은 막지 않음");
        Check(f.Mask("시발점이 시발이네")=="시발점이 **이네","허용 단어 밖의 같은 말은 가림");
        Check(f.Mask("안녕하세요")=="안녕하세요"&&!f.Contains(""),"걸리는 말이 없으면 그대로");

        // ---- Admin lists ----
        var lists=await Read(admin.GetAsync("/api/admin/community/chat-filter"));long revision=lists.GetProperty("revision").GetInt64();
        var words=lists.GetProperty("words").EnumerateArray().Select(w=>w.GetString()!).ToList();var allowed=lists.GetProperty("allowed").EnumerateArray().Select(w=>w.GetString()!).ToList();
        Check(words.Contains("씨발")&&allowed.Contains("시발점"),"처음 설치하면 기본 금칙어·허용 단어가 들어감");
        var tried=await Read(admin.PostAsJsonAsync("/api/admin/community/chat-filter/test",new{text="이 마네킹놈아",words=words.Append("마네킹놈").ToArray(),allowed}));
        Check(tried.GetProperty("masked").GetString()=="이 ****아"&&tried.GetProperty("blocked").GetBoolean(),"저장 전 목록으로 문장 시험");
        await Expect(admin.PutAsJsonAsync("/api/admin/community/chat-filter",new{revision,words=new[]{new string('가',41)},allowed}),HttpStatusCode.BadRequest,"너무 긴 단어는 저장 거부");
        await Expect(admin.PutAsJsonAsync("/api/admin/community/chat-filter",new{revision=revision+5,words,allowed}),HttpStatusCode.Conflict,"다른 곳에서 먼저 저장했으면 거부");
        await Read(admin.PutAsJsonAsync("/api/admin/community/chat-filter",new{revision,words=words.Append("마네킹놈").ToArray(),allowed}));revision++;

        // ---- Where it applies ----
        var p=await OperationsIntegration.Player(db);using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",p.Token);
        try
        {
            await Expect(client.PostAsJsonAsync("/api/rooms",new{name="씨발 방",isPublic=true}),HttpStatusCode.BadRequest,"금칙어가 든 방 이름은 거부");
            await Read(client.PostAsJsonAsync("/api/rooms",new{name="금칙어 검증 방",isPublic=false}));
            var room=(await Read(client.PostAsJsonAsync("/api/rooms/current/chat",new{message="야 씨-발 마네킹놈아 반가워"}))).GetProperty("room");
            var sent=room.GetProperty("chatMessages").EnumerateArray().Last().GetProperty("message").GetString();
            Check(sent=="야 *** ****아 반가워","방 채팅은 금칙어만 가려서 보내고, 새로 저장한 단어도 바로 적용");
            using(var r=await client.PutAsJsonAsync("/api/social/profile",new{statusMessage="ㅅㅂ 졸려",bio="",accent="Slate",title=""}))
                Check(!r.IsSuccessStatusCode,"금칙어가 든 상태메시지는 저장 거부 "+r.StatusCode);
            using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};
            await Expect(anonymous.PostAsJsonAsync("/api/auth/register",new{accountId="Filter_Name01",password="ReviewPassword123!",displayName="병신왕"}),HttpStatusCode.BadRequest,"금칙어가 든 표시 이름으로는 가입 거부");
            await Read(client.PostAsJsonAsync("/api/rooms/current/leave",new{}));
        }
        finally
        {
            await Read(admin.PutAsJsonAsync("/api/admin/community/chat-filter",new{revision,words,allowed}));
        }
        Console.WriteLine($"PASS: {count} chat filter checks");
    }
}

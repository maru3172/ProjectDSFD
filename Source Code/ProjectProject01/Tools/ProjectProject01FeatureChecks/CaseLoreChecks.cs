using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

// 기본 기록: operator-written lore opened per account by a condition, and nothing locked ever sent to the client.
internal static class CaseLoreChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int checks=0;void Check(bool ok,string name){if(!ok)throw new Exception(name);checks++;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string body=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Lore API {(int)r.StatusCode}: {body}");return JsonDocument.Parse(body).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode code,string name){using var r=await pending;Check(r.StatusCode==code,name+" "+r.StatusCode);}
        async Task Sql(string sql,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,sql,default,p);}
        var player=await OperationsIntegration.Player(db);using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);
        string old;await using(var c=await db.OpenConnectionAsync(default)){await CaseLore.EnsureAsync(c,default);old=(string)(await CommunityEndpoints.Rows(c,null,"SELECT entries_json FROM case_lore WHERE singleton_id=1",default))[0]["entries_json"]!;}
        CaseLore.Entry E(string id,string category,string unlock,string value="")=>new(id,category,$"title-{id}",$"body-{id}\r\n\r\n두 번째 문단.  공백 유지.",unlock,value);
        var entries=new[]{E("me_01","플레이어","always"),E("me_02","플레이어","level","3"),E("store_01","그 밖의 기록","achievement","bot_first"),E("store_02","그 밖의 기록","part","1")};
        Task<JsonElement> Lore()=>Read(client.GetAsync("/api/case-records")).ContinueWith(t=>t.Result.GetProperty("lore").GetProperty("records"));
        JsonElement Find(JsonElement list,string id)=>list.EnumerateArray().Single(e=>e.GetProperty("id").GetString()==id);
        try
        {
            await Expect(client.GetAsync("/api/admin/case-records/lore"),HttpStatusCode.Unauthorized,"일반 계정 기본 기록 원문 조회 차단");
            await Expect(client.PutAsJsonAsync("/api/admin/case-records/lore",entries),HttpStatusCode.Unauthorized,"일반 계정 기본 기록 수정 차단");
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/lore",new[]{entries[0],entries[0]}),HttpStatusCode.BadRequest,"같은 기록 ID 두 번 거부");
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/lore",new[]{E("bad","플레이어","level","101")}),HttpStatusCode.BadRequest,"레벨 조건 범위 밖 거부");
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/lore",new[]{E("bad","플레이어","part","14")}),HttpStatusCode.BadRequest,"파트 조건 범위 밖 거부");
            await Expect(admin.PutAsJsonAsync("/api/admin/case-records/lore",new[]{E("bad","플레이어","season")}),HttpStatusCode.BadRequest,"알 수 없는 열림 조건 거부");
            await Read(admin.PutAsJsonAsync("/api/admin/case-records/lore",entries));
            var authored=(await Read(admin.GetAsync("/api/admin/case-records/lore"))).GetProperty("entries");
            Check(authored.GetArrayLength()==4&&authored[0].GetProperty("body").GetString()==entries[0].Body,"운영자 원문은 줄바꿈·공백 그대로 저장, 순서 유지");
            var lore=await Lore();
            Check(lore.GetArrayLength()==4&&Find(lore,"me_01").GetProperty("unlocked").GetBoolean()&&Find(lore,"me_01").GetProperty("body").GetString()==entries[0].Body,"처음부터 열린 기록은 바로 열람");
            Check(!lore.ToString().Contains("body-me_02")&&!lore.ToString().Contains("title-me_02")&&!lore.ToString().Contains("body-store_0"),"잠긴 기록의 제목·본문은 클라이언트에 보내지 않음");
            Check(Find(lore,"me_02").GetProperty("unlock").GetString()=="level"&&Find(lore,"me_02").GetProperty("value").GetString()=="3"&&Find(lore,"store_01").GetProperty("category").GetString()=="그 밖의 기록","잠긴 기록도 분류와 열림 조건은 보여 줌");
            await Sql("UPDATE case_accounts SET xp=400 WHERE user_id=@u",("u",player.Id));
            Check(Find(await Lore(),"me_02").GetProperty("body").GetString()==entries[1].Body,"계정 레벨에 닿으면 열림");
            await Sql("INSERT INTO case_achievements(user_id,code,source) VALUES(@u,'bot_first','Server')",("u",player.Id));
            Check(Find(await Lore(),"store_01").GetProperty("unlocked").GetBoolean(),"업적을 달성하면 열림");
            Check(!Find(await Lore(),"store_02").GetProperty("unlocked").GetBoolean(),"파트 보상 전에는 파트 기록 잠김");
            await Sql("INSERT INTO case_claims(user_id,reward_key) VALUES(@u,@k)",("u",player.Id),("k",CaseRecords.Season+":Investigation:1"));
            Check(Find(await Lore(),"store_02").GetProperty("title").GetString()=="title-store_02","조사 파트 보상을 받으면 파트 기록 열림");
            Console.WriteLine($"PASS: {checks} lore HTTP/DB checks");
        }
        finally
        {
            await Sql("UPDATE case_lore SET entries_json=@j WHERE singleton_id=1;DELETE FROM users WHERE id=@u",("j",old),("u",player.Id));
        }
    }
}

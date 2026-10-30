using System.Net;
using System.Net.Http.Json;

internal static class GuideProgressChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        var first=await OperationsIntegration.Player(db);var second=await OperationsIntegration.Player(db);
        using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",first.Token);
        using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};
        int count=0;
        void Check(bool ok,string text){if(!ok)throw new Exception(text);count++;Console.WriteLine("PASS "+text);}
        async Task<Dictionary<string,object?>> Flags(ulong user)
        {
            await using var c=await db.OpenConnectionAsync(default);
            return (await CommunityEndpoints.Rows(c,null,"SELECT seen_survivor_guide,seen_mannequin_guide,seen_initial_tutorial FROM users WHERE id=@u",default,("u",user)))[0];
        }
        try
        {
            Check(!Convert.ToBoolean((await Flags(first.Id))["seen_initial_tutorial"]),"새 계정은 최초 생존자 튜토리얼 선택 대상");
            using(var response=await anonymous.PostAsJsonAsync("/api/auth/guide-progress",new{role="InitialTutorial"}))Check(response.StatusCode==HttpStatusCode.Unauthorized,"미로그인 최초 안내 기록 차단");
            foreach(string role in new[]{"InitialTutorial","InitialTutorial"})
            {using var response=await client.PostAsJsonAsync("/api/auth/guide-progress",new{role});Check(response.IsSuccessStatusCode,"최초 안내 확인·건너뛰기 기록은 재요청해도 정상");}
            var flags=await Flags(first.Id);
            Check(Convert.ToBoolean(flags["seen_initial_tutorial"])&&!Convert.ToBoolean(flags["seen_survivor_guide"])&&!Convert.ToBoolean(flags["seen_mannequin_guide"]),"최초 튜토리얼 선택 기록은 인게임 역할 팜플렛 기록과 분리");
            Check(!Convert.ToBoolean((await Flags(second.Id))["seen_initial_tutorial"]),"다른 계정의 첫 안내 상태는 유지");
            using(var response=await client.PostAsJsonAsync("/api/auth/guide-progress",new{role="Survivor"}))Check(response.IsSuccessStatusCode,"생존자 인게임 팜플렛 확인 저장");
            flags=await Flags(first.Id);Check(Convert.ToBoolean(flags["seen_initial_tutorial"])&&Convert.ToBoolean(flags["seen_survivor_guide"])&&!Convert.ToBoolean(flags["seen_mannequin_guide"]),"계정별 최초 안내·역할별 팜플렛은 각각 보존");
            using(var response=await client.PostAsJsonAsync("/api/auth/guide-progress",new{role="seen_initial_tutorial=FALSE"}))Check(response.StatusCode==HttpStatusCode.BadRequest,"허용되지 않은 안내 종류·열 지정 차단");
            Check(Convert.ToBoolean((await Flags(first.Id))["seen_initial_tutorial"]),"다시 접속할 때 읽는 계정 기록에도 최초 안내 완료 유지");
            Console.WriteLine($"PASS: {count} guide progress checks");
        }
        finally
        {
            await using var c=await db.OpenConnectionAsync(default);
            await CommunityEndpoints.Exec(c,null,"DELETE FROM users WHERE id IN (@a,@b)",default,("a",first.Id),("b",second.Id));
        }
    }
}

using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class AnnouncementChecks
{
    internal static async Task RunAsync(HttpClient admin)
    {
        async Task<JsonElement> Read(Task<HttpResponseMessage> task){using var r=await task;r.EnsureSuccessStatusCode();return JsonDocument.Parse(await r.Content.ReadAsStringAsync()).RootElement.Clone();}
        var old=await Read(admin.GetAsync("/api/admin/service"));
        using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};int checks=0;
        void Check(bool ok,string message){if(!ok)throw new Exception(message);++checks;Console.WriteLine("PASS "+message);}
        try
        {
            using(var denied=await anonymous.PutAsJsonAsync("/api/admin/service/announcement",new{announcement="위조"}))Check(denied.StatusCode==HttpStatusCode.Unauthorized,"관리자만 안내 공지 발송 가능");
            await Read(admin.PutAsJsonAsync("/api/admin/service",new{maintenanceEnabled=true,announcement="이전 공지"}));
            const string text="첫 줄\r\n둘째 줄\n띄어쓰기  유지";
            var published=await Read(admin.PutAsJsonAsync("/api/admin/service/announcement",new{announcement=text}));
            Check(published.GetProperty("manualMaintenanceEnabled").GetBoolean(),"안내 공지 발송으로 점검 설정을 변경하지 않음");
            var publicStatus=await Read(anonymous.GetAsync("/api/service/status"));
            Check(publicStatus.GetProperty("baseAnnouncement").GetString()==text,"런처용 공지 원문 줄바꿈과 공백 그대로 전달");
            long first=publicStatus.GetProperty("revision").GetInt64();
            var repeated=await Read(admin.PutAsJsonAsync("/api/admin/service/announcement",new{announcement=text}));
            Check(repeated.GetProperty("revision").GetInt64()>first,"동일한 공지 재발송도 새 발송으로 식별");
            using(var longText=await admin.PutAsJsonAsync("/api/admin/service/announcement",new{announcement=new string('a',513)}))Check(longText.StatusCode==HttpStatusCode.BadRequest,"공지 길이 상한 유지");
            var cleared=await Read(admin.PutAsJsonAsync("/api/admin/service/announcement",new{announcement=""}));
            Check(cleared.GetProperty("baseAnnouncement").GetString()==""&&cleared.GetProperty("manualMaintenanceEnabled").GetBoolean(),"공지 삭제는 점검 상태를 유지하고 빈 공지 전달");
            Console.WriteLine($"PASS: {checks} announcement HTTP checks");
        }
        finally{await Read(admin.PutAsJsonAsync("/api/admin/service",new{maintenanceEnabled=old.GetProperty("manualMaintenanceEnabled").GetBoolean(),announcement=old.GetProperty("baseAnnouncement").GetString()}));}
    }
}

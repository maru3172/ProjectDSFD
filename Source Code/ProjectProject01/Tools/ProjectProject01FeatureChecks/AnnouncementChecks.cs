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
            var published=await Read(admin.PutAsJsonAsync("/api/admin/service/announcement",new{announcement=text,tag="-긴급 공지-",repeatCount=3}));
            Check(published.GetProperty("manualMaintenanceEnabled").GetBoolean(),"안내 공지 발송으로 점검 설정을 변경하지 않음");
            var publicStatus=await Read(anonymous.GetAsync("/api/service/status"));
            Check(publicStatus.GetProperty("baseAnnouncement").GetString()==text,"런처용 공지 원문 줄바꿈과 공백 그대로 전달");
            Check(publicStatus.GetProperty("announcementTag").GetString()=="-긴급 공지-"&&publicStatus.GetProperty("announcementRepeatCount").GetInt32()==3,"공지 태그와 선택 반복 횟수 전달");
            string firstBroadcast=publicStatus.GetProperty("announcementBroadcastId").GetString()!;
            await Read(admin.PutAsJsonAsync("/api/admin/service",new{maintenanceEnabled=true,announcement=text}));
            Check((await Read(anonymous.GetAsync("/api/service/status"))).GetProperty("announcementBroadcastId").GetString()==firstBroadcast,"점검 상태 변경은 새 공지 전송으로 취급하지 않음");
            long first=publicStatus.GetProperty("revision").GetInt64();
            var repeated=await Read(admin.PutAsJsonAsync("/api/admin/service/announcement",new{announcement=text}));
            Check(repeated.GetProperty("revision").GetInt64()>first,"동일한 공지 재발송도 새 발송으로 식별");
            Check(repeated.GetProperty("announcementBroadcastId").GetString()!=firstBroadcast,"명시적 재전송만 새 전송 번호 생성");
            Check(repeated.GetProperty("announcementTag").GetString()=="","태그 미입력 시 기본 공지 태그를 강제하지 않음");
            using(var invalid=await admin.PutAsJsonAsync("/api/admin/service/announcement",new{announcement=text,repeatCount=0}))Check(invalid.StatusCode==HttpStatusCode.BadRequest,"공지 반복 횟수 하한 검사");
            using(var longText=await admin.PutAsJsonAsync("/api/admin/service/announcement",new{announcement=new string('a',513)}))Check(longText.StatusCode==HttpStatusCode.BadRequest,"공지 길이 상한 유지");
            var start=DateTimeOffset.UtcNow.AddSeconds(3);var end=start.AddSeconds(4);
            var scheduled=await Read(admin.PutAsJsonAsync("/api/admin/service/announcement",new{announcement="시간 지정 공지",startsAtUtc=start,endsAtUtc=end,repeatCount=100}));
            string scheduledId=scheduled.GetProperty("announcementBroadcastId").GetString()!;
            var beforeStart=await Read(anonymous.GetAsync("/api/service/status"));Check(!beforeStart.GetProperty("announcementActive").GetBoolean()&&beforeStart.GetProperty("baseAnnouncement").GetString()=="","시작 시각 전에는 게임·런처용 공지 본문을 보내지 않음");
            async Task<JsonElement> WaitFor(bool active)
            {
                var clock=System.Diagnostics.Stopwatch.StartNew();
                do{var status=await Read(anonymous.GetAsync("/api/service/status"));if(status.GetProperty("announcementActive").GetBoolean()==active)return status;await Task.Delay(100);}while(clock.Elapsed<TimeSpan.FromSeconds(12));
                throw new Exception("공지 예약 상태 전환 시간 초과");
            }
            var started=await WaitFor(true);Check(started.GetProperty("baseAnnouncement").GetString()=="시간 지정 공지"&&started.GetProperty("announcementBroadcastId").GetString()==scheduledId,"재적용 없이 예약 시각에 같은 전송 번호로 공지 활성화");
            var expired=await WaitFor(false);Check(expired.GetProperty("baseAnnouncement").GetString()==""&&expired.GetProperty("announcementRemainingSeconds").GetDouble()==0,"횟수가 남아도 종료 시각이면 게임·런처 공지 해제");
            var cleared=await Read(admin.PutAsJsonAsync("/api/admin/service/announcement",new{announcement=""}));
            Check(cleared.GetProperty("baseAnnouncement").GetString()==""&&cleared.GetProperty("manualMaintenanceEnabled").GetBoolean(),"공지 삭제는 점검 상태를 유지하고 빈 공지 전달");
            Check(!cleared.GetProperty("announcementActive").GetBoolean()&&cleared.GetProperty("announcementStartsAtUtc").ValueKind==JsonValueKind.Null&&cleared.GetProperty("announcementEndsAtUtc").ValueKind==JsonValueKind.Null,"제거하면 공지 내용과 표시 시간 예약도 함께 취소");
            Console.WriteLine($"PASS: {checks} announcement HTTP checks");
        }
        finally{await Read(admin.PutAsJsonAsync("/api/admin/service",new{maintenanceEnabled=old.GetProperty("manualMaintenanceEnabled").GetBoolean(),announcement=old.GetProperty("baseAnnouncement").GetString()}));}
    }
}

using System.IO.Compression;
using System.Net;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using MySqlConnector;

internal static class IntegrationChecks
{
    private static int passed;
    public static async Task RunAsync(string projectRoot,int port)
    {
        if(port!=23307)throw new InvalidOperationException("Only the isolated test MySQL port is allowed.");
        string output=Path.Combine(projectRoot,"Saved","FeatureChecks");
        Directory.CreateDirectory(output);
        var database=new AuthDatabase($"Server=127.0.0.1;Port={port};User ID=root;Database=projectproject01;SslMode=None");
        await using(var safety=new MySqlConnection($"Server=127.0.0.1;Port={port};User ID=root;SslMode=None"))
        {
            await safety.OpenAsync();
            await using var command=new MySqlCommand("SELECT @@datadir;",safety);
            string actual=Path.GetFullPath((string)(await command.ExecuteScalarAsync())!).TrimEnd(Path.DirectorySeparatorChar);
            string expected=Path.GetFullPath(Path.Combine(output,"IsolatedMySql"));
            if(!string.Equals(actual,expected,StringComparison.OrdinalIgnoreCase))throw new Exception("Refusing to test against a non-isolated MySQL data directory.");
        }
        await database.InitializeSchemaAsync(Path.Combine(projectRoot,"Tools","ProjectProject01Backend"),CancellationToken.None);
        // Also exercise startup migration of a pre-existing survey table.
        await using(var connection=await database.OpenConnectionAsync(CancellationToken.None))
        await using(var command=new MySqlCommand("ALTER TABLE player_survey_responses DROP COLUMN review_status, DROP COLUMN review_reason;",connection))
            await command.ExecuteNonQueryAsync();
        await database.InitializeSchemaAsync(Path.Combine(projectRoot,"Tools","ProjectProject01Backend"),CancellationToken.None);
        var builder=WebApplication.CreateBuilder(new WebApplicationOptions{ContentRootPath=output});
        builder.Logging.ClearProviders();
        builder.Services.AddSingleton(database);
        builder.Services.AddSingleton(new CrashArtifactStore(Path.Combine(output,"CrashArtifactsFixture",Guid.NewGuid().ToString("N")),TimeProvider.System));
        builder.Services.AddSingleton<ServiceControlState>();
        builder.Services.AddSingleton<Microsoft.AspNetCore.Identity.IPasswordHasher<RoomPasswordRecord>,Microsoft.AspNetCore.Identity.PasswordHasher<RoomPasswordRecord>>();
        builder.Services.AddSingleton(AdministratorOptions.FromConfiguration(new ConfigurationBuilder().AddInMemoryCollection(
            new Dictionary<string,string?>{{"Administrator:ApiKey",new string('a',48)}}).Build()));
        builder.Services.AddRateLimiter(options=>{
            foreach(string policy in new[]{"admin","survey","survey-read","crash-report","lobby","game-server","game-ticket"})
                options.AddFixedWindowLimiter(policy,limit=>{limit.PermitLimit=1000;limit.Window=TimeSpan.FromMinutes(1);});
        });
        var testConfig=new ConfigurationBuilder().AddInMemoryCollection(new Dictionary<string,string?> {
            ["GameServer:SharedSecret"]=new string('s',48),["GameServer:TicketKey"]=Convert.ToBase64String(new byte[32])}).Build();
        var demandFactory=new DemandSessionChecks.FakeFactory();
        var demandClock=new DemandSessionChecks.FakeClock();
        var demand=new GameServerDemandManager(database,ProjectSecurityOptions.FromConfiguration(testConfig),demandFactory,
            Microsoft.Extensions.Logging.Abstractions.NullLogger<GameServerDemandManager>.Instance,demandClock);
        builder.Services.AddSingleton(demand);
        await using var app=builder.Build();
        app.Urls.Add("http://127.0.0.1:0");app.UseRateLimiter();
        app.MapProjectProject01Surveys();app.MapProjectProject01CrashArtifacts();
        app.MapProjectProject01ServiceAdministration();
        app.MapAccountRestrictions();app.MapRoomCapacity();
        app.MapGameServerDemand();
        app.MapProjectProject01Lobby("127.0.0.1:24477",ProjectSecurityOptions.FromConfiguration(testConfig),VersionCompatibilityOptions.FromConfiguration(testConfig),new GameServerPresenceState(),demand);
        await app.StartAsync();
        try
        {
            using var admin=new HttpClient{BaseAddress=new Uri(app.Urls.Single())};
            admin.DefaultRequestHeaders.Add("X-ProjectProject01-Admin-Key",new string('a',48));
            using var player=new HttpClient{BaseAddress=admin.BaseAddress};
            await Expect(await player.GetAsync("/api/surveys/active"),HttpStatusCode.Unauthorized,"미로그인 설문 조회 차단");
            string questionId=Guid.NewGuid().ToString("D");
            var question=new {id=questionId,type="Choice",prompt="두 개 선택",options=new[]{"A","B","C"},required=true,allowMultiple=true,maxSelections=2,isTrap=true,expectedAnswers=new[]{"A","B"}};
            var definition=new {title="통합 검증 설문",description="설명",questions=new[]{question}};
            var created=await Read(await admin.PostAsJsonAsync("/api/admin/surveys",definition));
            string id=created.GetProperty("surveyId").GetString()!;
            var list=await Read(await admin.GetAsync("/api/admin/surveys"));
            Check(list.GetProperty("surveys").EnumerateArray().Any(s=>s.GetProperty("surveyId").GetString()==id),"저장 후 목록에 설문 표시");
            await Expect(await admin.PutAsJsonAsync($"/api/admin/surveys/{id}",definition),HttpStatusCode.OK,"저장된 설문 수정");
            await Expect(await admin.PostAsync($"/api/admin/surveys/{id}/activate",null),HttpStatusCode.OK,"설문 배포");
            await Expect(await admin.PutAsJsonAsync($"/api/admin/surveys/{id}",definition),HttpStatusCode.Conflict,"배포된 설문 응답 구조 보호");
            string token=await CreatePlayer(database);
            player.DefaultRequestHeaders.Authorization=new("Bearer",token);
            var active=await Read(await player.GetAsync("/api/surveys/active"));
            string activeJson=active.GetRawText();
            Check(!activeJson.Contains("ExpectedAnswers")&&!activeJson.Contains("expectedAnswers"),"실제 API 함정 정답 미노출");
            Check(active.GetProperty("questions")[0].GetProperty("allowMultiple").GetBoolean(),"수정 이후 복수 응답 JSON 전달");
            await Expect(await player.PostAsJsonAsync($"/api/surveys/{id}/responses",Response(questionId,["A","B","C"])),HttpStatusCode.BadRequest,"실제 API 최대 응답 수 검증");
            await Expect(await player.PostAsJsonAsync($"/api/surveys/{id}/responses",Response(questionId,["A","B"])),HttpStatusCode.Created,"정답 응답 저장");
            await Expect(await player.PostAsJsonAsync($"/api/surveys/{id}/responses",Response(questionId,["A","B"])),HttpStatusCode.Conflict,"계정당 중복 응답 차단");
            player.DefaultRequestHeaders.Authorization=new("Bearer",await CreatePlayer(database));
            var pending=await Read(await player.PostAsJsonAsync($"/api/surveys/{id}/responses",Response(questionId,["B","C"])));
            string responseId=pending.GetProperty("responseId").GetString()!;
            var result=await Read(await admin.GetAsync($"/api/admin/surveys/{id}/results"));
            Check(result.GetProperty("acceptedCount").GetInt32()==1 && result.GetProperty("pendingCount").GetInt32()==1,"오답 보류 및 통계 분리");
            Check(result.GetProperty("choiceStatistics")[0].GetProperty("options")[2].GetProperty("count").GetInt32()==0,"보류 선택지는 그래프 집계 제외");
            await File.WriteAllTextAsync(Path.Combine(output,"survey-results.json"),result.GetRawText());
            var overview=await Read(await admin.GetAsync("/api/admin/surveys/statistics"));
            Check(overview.GetProperty("pendingCount").GetInt32()>=1 && overview.GetProperty("participantCount").GetInt32()>=2,"전체 설문 참여 및 보류 통계");
            var daily=overview.GetProperty("responseTrend").EnumerateArray().ToArray();
            Check(daily.Sum(p=>p.GetProperty("responseCount").GetInt32())==overview.GetProperty("responseCount").GetInt32() &&
                daily.Sum(p=>p.GetProperty("acceptedCount").GetInt32())==overview.GetProperty("acceptedCount").GetInt32() &&
                daily.Sum(p=>p.GetProperty("pendingCount").GetInt32())==overview.GetProperty("pendingCount").GetInt32(),"전체 통계 꺾은선과 합계 일치");
            Check(overview.GetProperty("trendTimeZone").GetString()=="Asia/Seoul","전체 통계 일자 한국 시간 기준");
            await File.WriteAllTextAsync(Path.Combine(output,"survey-overview.json"),overview.GetRawText());
            await Expect(await admin.PostAsync($"/api/admin/surveys/{id}/responses/{responseId}/accept",null),HttpStatusCode.OK,"보류 응답 수동 반영");
            result=await Read(await admin.GetAsync($"/api/admin/surveys/{id}/results"));
            Check(result.GetProperty("acceptedCount").GetInt32()==2 && result.GetProperty("pendingCount").GetInt32()==0,"수동 반영 후 통계 갱신");
            player.DefaultRequestHeaders.Authorization=new("Bearer",await CreatePlayer(database));
            pending=await Read(await player.PostAsJsonAsync($"/api/surveys/{id}/responses",Response(questionId,["C"])));
            await Expect(await admin.DeleteAsync($"/api/admin/surveys/{id}/responses/{pending.GetProperty("responseId").GetString()}"),HttpStatusCode.OK,"보류 응답 삭제");
            await Expect(await admin.DeleteAsync($"/api/admin/surveys/{id}"),HttpStatusCode.Conflict,"진행 중 설문 삭제 보호");
            await Expect(await admin.PostAsync($"/api/admin/surveys/{id}/close",null),HttpStatusCode.OK,"설문 종료");
            await Expect(await admin.DeleteAsync($"/api/admin/surveys/{id}"),HttpStatusCode.OK,"종료 후 설문 삭제");

            string report=Guid.NewGuid().ToString("D");
            string match=Guid.NewGuid().ToString("D");
            string pieReport=Guid.NewGuid().ToString("D");
            await using(var connection=await database.OpenConnectionAsync(CancellationToken.None))
            await using(var command=new MySqlCommand("""
                INSERT INTO crash_reports(report_id,detected_at_utc,started_at_utc,last_updated_at_utc,build_version,map_name,game_mode,process_role,last_log_path,diagnostic_run_id,match_id)
                VALUES(@id,UTC_TIMESTAMP(),UTC_TIMESTAMP(),UTC_TIMESTAMP(),'test','TestMap','TestMode','Client','Saved/Logs/Game.log',@id,@match),
                  (@pie,UTC_TIMESTAMP(),UTC_TIMESTAMP(),UTC_TIMESTAMP(),'test','UEDPIE_0_TestMap','TestMode','Client','Saved/Logs/Game.log',@pie,NULL);
                """,connection))
            {command.Parameters.AddWithValue("@id",report);command.Parameters.AddWithValue("@match",match);
                command.Parameters.AddWithValue("@pie",pieReport);await command.ExecuteNonQueryAsync();}
            await Expect(await player.GetAsync("/api/admin/crash-reports"),HttpStatusCode.Unauthorized,"일반 계정 크래시 목록 조회 차단");
            var reports=await Read(await admin.GetAsync("/api/admin/crash-reports?limit=100"));
            var found=reports.GetProperty("crashReports").EnumerateArray().Single(r=>r.GetProperty("reportId").GetString()==report);
            Check(found.GetProperty("matchId").GetString()==match,"크래시 목록 Guid 보고서·매치 ID 문자열 변환");
            Check(reports.GetProperty("crashReports").EnumerateArray().All(r=>r.GetProperty("reportId").GetString()!=pieReport),"크래시 목록 PIE 오탐 제외");
            reports=await Read(await admin.GetAsync("/api/admin/crash-reports?limit=1"));
            Check(reports.GetProperty("count").GetInt32()==1,"크래시 목록 조회 개수 제한");
            await Expect(await player.PostAsync($"/api/crash-reports/{report}/artifacts/Crash.dmp",new ByteArrayContent(Encoding.ASCII.GetBytes("MDMP-test-fixture"))),HttpStatusCode.NoContent,"기본 덤프 업로드 비활성");
            await Expect(await admin.PutAsJsonAsync("/api/admin/crash-artifact-policy",new CrashArtifactPolicy(AllowMinidumps:true)),HttpStatusCode.OK,"관리자가 원본 덤프 수집 허용");
            await Expect(await player.PostAsync($"/api/crash-reports/{report}/artifacts/Crash.dmp",new ByteArrayContent(Encoding.ASCII.GetBytes("MDMP-test-fixture"))),HttpStatusCode.OK,"허용 후 덤프 업로드");
            await Expect(await player.PostAsync($"/api/crash-reports/{report}/artifacts/Game.log",new StringContent("log password=secret-fixture Bearer token-fixture")),HttpStatusCode.OK,"전체 로그 업로드");
            await Expect(await player.PostAsync($"/api/crash-reports/{report}/artifacts/secret.json",new StringContent("x")),HttpStatusCode.BadRequest,"허용되지 않은 첨부파일 차단");
            await Expect(await player.GetAsync($"/api/admin/crash-reports/{report}/artifacts"),HttpStatusCode.Unauthorized,"일반 클라이언트 덤프 다운로드 차단");
            using var downloaded=await admin.GetAsync($"/api/admin/crash-reports/{report}/artifacts");
            byte[] zip=await downloaded.Content.ReadAsByteArrayAsync();
            using var archive=new ZipArchive(new MemoryStream(zip));
            Check(archive.GetEntry("Crash.dmp") is not null && archive.GetEntry("Game.log") is not null,"관리자 ZIP 다운로드");
            using var reader=new StreamReader(archive.GetEntry("Game.log")!.Open());
            string log=await reader.ReadToEndAsync();
            Check(!log.Contains("secret-fixture")&&!log.Contains("token-fixture"),"서버 저장 로그 비밀값 제거");
            string zipPath=Path.Combine(output,"crash-fixture.zip");
            await ProjectProject01BuildServerHelper.CrashReportDownloadForm.SaveZipAsync(zipPath,zip);
            Check(File.Exists(zipPath)&&new FileInfo(zipPath).Length==zip.Length,"도우미 ZIP 실제 파일 저장");
            try{await ProjectProject01BuildServerHelper.CrashReportDownloadForm.SaveZipAsync(zipPath,[1,2,3]);throw new Exception("Invalid ZIP accepted");}
            catch(InvalidDataException){Check((await File.ReadAllBytesAsync(zipPath)).SequenceEqual(zip),"손상 ZIP은 기존 저장본 보존");}
            await OperationsIntegration.RunAsync(database,admin);
            await DemandSessionChecks.RunAsync(database,admin,demand,demandFactory,demandClock,output);
            await DrainSessionChecks.RunAsync(database,admin,demand,demandFactory,output);
            Console.WriteLine($"PASS: {passed} isolated API/database checks");
        }
        finally{await app.StopAsync();}
    }
    private static object Response(string id,string[] values)=>new {responseId=Guid.NewGuid().ToString("D"),submittedAtUtc=DateTime.UtcNow,
        answers=new[]{new {questionId=id,value=string.Join(", ",values),values}},buildVersion="test",mapName="Test",processRole="Client",playerRole="Survivor"};
    private static async Task<string> CreatePlayer(AuthDatabase database)
    {
        string suffix=Guid.NewGuid().ToString("N")[..16],token=Guid.NewGuid().ToString("N");
        await using var connection=await database.OpenConnectionAsync(CancellationToken.None);
        await using var command=new MySqlCommand("""
            INSERT INTO users(account_id,display_name,password_hash) VALUES(@account,'검증 계정','unused');
            INSERT INTO auth_sessions(user_id,access_token_hash,refresh_token_hash,access_expires_at_utc,refresh_expires_at_utc)
            VALUES(LAST_INSERT_ID(),@hash,@hash,DATE_ADD(UTC_TIMESTAMP(),INTERVAL 1 HOUR),DATE_ADD(UTC_TIMESTAMP(),INTERVAL 1 DAY));
            """,connection);
        command.Parameters.AddWithValue("@account",suffix);command.Parameters.AddWithValue("@hash",SHA256.HashData(Encoding.UTF8.GetBytes(token)));
        await command.ExecuteNonQueryAsync();return token;
    }
    private static async Task<JsonElement> Read(HttpResponseMessage response)
    {
        using(response){string body=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new Exception($"HTTP {(int)response.StatusCode}: {body}");using var doc=JsonDocument.Parse(body);return doc.RootElement.Clone();}
    }
    private static async Task Expect(HttpResponseMessage response,HttpStatusCode expected,string name)
    {using(response){string body=await response.Content.ReadAsStringAsync();Check(response.StatusCode==expected,$"{name}: {response.StatusCode} {(!response.IsSuccessStatusCode&&response.StatusCode!=expected?body:"")}");}}
    private static void Check(bool success,string name){if(!success)throw new Exception(name);passed++;Console.WriteLine($"PASS {name}");}
}

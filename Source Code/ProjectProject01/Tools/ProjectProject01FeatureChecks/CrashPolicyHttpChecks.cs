using System.Net;
using System.Net.Http.Json;
using System.IO.Compression;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

internal static class CrashPolicyHttpChecks
{
    internal static async Task RunAsync(Action<bool,string> check)
    {
        string fixture=Path.GetFullPath(Path.Combine("Saved","FeatureChecks","CrashPolicyHttp",Guid.NewGuid().ToString("N")));
        Directory.CreateDirectory(fixture);
        var builder=WebApplication.CreateBuilder(new WebApplicationOptions {ContentRootPath=fixture});builder.Logging.ClearProviders();
        var store=new CrashArtifactStore(fixture,TimeProvider.System);
        builder.Services.AddSingleton(store);builder.Services.AddSingleton(new AuthDatabase(null));
        builder.Services.AddSingleton(AdministratorOptions.FromConfiguration(new ConfigurationBuilder().AddInMemoryCollection(
            new Dictionary<string,string?>{{"Administrator:ApiKey",new string('a',48)}}).Build()));
        builder.Services.AddRateLimiter(options=>
        {
            foreach(string policy in new[]{"admin","crash-report"})options.AddFixedWindowLimiter(policy,limit=>{limit.PermitLimit=1000;limit.Window=TimeSpan.FromMinutes(1);});
        });
        await using var app=builder.Build();app.Urls.Add("http://127.0.0.1:0");app.UseRateLimiter();app.MapProjectProject01CrashArtifacts();
        await app.StartAsync();
        try
        {
            using var client=new HttpClient {BaseAddress=new Uri(app.Urls.Single()),Timeout=TimeSpan.FromSeconds(5)};
            using(var response=await client.GetAsync("/api/admin/crash-artifact-policy"))check(response.StatusCode==HttpStatusCode.Unauthorized,"일반 클라이언트 보관 정책 조회 차단");
            using(var response=await client.PutAsJsonAsync("/api/admin/crash-artifact-policy",new CrashArtifactPolicy()))check(response.StatusCode==HttpStatusCode.Unauthorized,"일반 클라이언트 보관 정책 변경 차단");
            Guid id=Guid.NewGuid();
            using(var response=await client.PostAsync($"/api/crash-reports/{id:D}/artifacts/Crash.dmp",new ByteArrayContent("MDMPfixture"u8.ToArray())))
                check(response.StatusCode==HttpStatusCode.NoContent && !Directory.Exists(store.ReportDirectory(id)),"수집 해제된 덤프는 디스크에 저장하지 않음");
            client.DefaultRequestHeaders.Add("X-ProjectProject01-Admin-Key",new string('a',48));
            using(var response=await client.GetAsync("/api/admin/crash-artifact-policy"))
            {
                var status=await response.Content.ReadFromJsonAsync<CrashArtifactStorageStatus>();
                check(response.IsSuccessStatusCode && status is {Policy.AllowMinidumps:false,Policy.AutoCleanupEnabled:false},"관리자 정책 기본값 조회");
            }
            using(var response=await client.PutAsJsonAsync("/api/admin/crash-artifact-policy",new CrashArtifactPolicy(false,0,1,false)))check(response.StatusCode==HttpStatusCode.BadRequest,"API에서도 잘못된 보관 범위 차단");
            using(var response=await client.PutAsJsonAsync("/api/admin/crash-artifact-policy",new CrashArtifactPolicy(false,14,512,true)))
                check(response.IsSuccessStatusCode && store.Policy is {RetentionDays:14,MaximumTotalMiB:512,AllowMinidumps:true},"관리자 정책 적용 및 즉시 반영");
            string directory=store.ReportDirectory(id);Directory.CreateDirectory(directory);await File.WriteAllTextAsync(Path.Combine(directory,"Game.log"),"isolated fixture");
            using(var response=await client.GetAsync($"/api/admin/crash-reports/{id:D}/artifacts"))
            {
                using var stream=new MemoryStream(await response.Content.ReadAsByteArrayAsync());using var zip=new ZipArchive(stream);
                check(response.IsSuccessStatusCode && zip.GetEntry("Game.log") is not null,"디스크 스풀 ZIP이 실제 HTTP 다운로드에서 열림");
            }
        }
        finally {await app.StopAsync();}
    }
}

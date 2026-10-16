using System.Reflection;
using System.Security.Cryptography;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Logging;
using ProjectProject01ClientLauncher;

internal static class UpdateInstallChecks
{
    internal static async Task RunAsync(string root)
    {
        string fixture=Path.Combine(root,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(Path.Combine(fixture,"Content"));
        var files=new Dictionary<string,byte[]> { ["Content/first.bin"]="new first"u8.ToArray(),["Content/second.bin"]="new second"u8.ToArray() };
        await File.WriteAllTextAsync(Path.Combine(fixture,"Content/first.bin"),"old first");
        await File.WriteAllTextAsync(Path.Combine(fixture,"Content/second.bin"),"old second");
        await File.WriteAllTextAsync(Path.Combine(fixture,"stale.txt"),"stale fixture");
        var builder=WebApplication.CreateBuilder();builder.Logging.ClearProviders();
        await using var app=builder.Build();app.Urls.Add("http://127.0.0.1:0");int requests=0;
        app.MapGet("/api/client-updates/files/{**path}",(string path)=>
        {Interlocked.Increment(ref requests);return Results.Bytes(files[path],enableRangeProcessing:true);});
        await app.StartAsync();
        try
        {
            string url=app.Urls.Single();
            await OnStaAsync(async()=>
            {
                using var launcher=new LauncherForm(fixture);
                var remote=files.Select(item=>new ClientUpdateFile(item.Key,item.Value.Length,Convert.ToHexString(SHA256.HashData(item.Value)))).ToList();
                var manifest=new ClientUpdateManifest("fixture-release","1",DateTime.UtcNow,"FixtureGame.exe",remote);
                var plans=remote.Select(file=>new ClientFileUpdatePlan(file,null,file.Size)).ToList();
                var method=typeof(LauncherForm).GetMethod("DownloadAndCommitAsync",BindingFlags.Instance|BindingFlags.NonPublic)!;
                Task Install()=>(Task)method.Invoke(launcher,[url,manifest,plans,new List<string>{"stale.txt"}])!;
                using(var locked=new FileStream(Path.Combine(fixture,"Content/second.bin"),FileMode.Open,FileAccess.Read,FileShare.Read))
                {
                    try{await Install();throw new Exception("Locked file should prevent replacement");}
                    catch(IOException){}
                    if(await File.ReadAllTextAsync(Path.Combine(fixture,"Content/first.bin"))!="old first"||!File.Exists(Path.Combine(fixture,"stale.txt")))throw new Exception("Rollback did not restore old client");
                    Console.WriteLine("PASS 파일 잠금 적용 실패 시 앞서 교체한 파일까지 기존 설치본으로 롤백");
                }
                int before=requests;await Install();
                if(requests!=before)throw new Exception("Verified downloads were fetched again after rollback");
                Console.WriteLine("PASS 잠금 해제 뒤 재시도는 검증 완료 캐시로 적용·재다운로드 없음");
                foreach(var item in files)if(!(await File.ReadAllBytesAsync(Path.Combine(fixture,item.Key))).SequenceEqual(item.Value))throw new Exception("Installed file mismatch");
                if(File.Exists(Path.Combine(fixture,"stale.txt")))throw new Exception("Stale fixture not removed");
                Console.WriteLine("PASS 성공한 업데이트만 새 파일 적용·이전 목록 파일 정리");
            });
        }
        finally{await app.StopAsync();}
    }
    private static Task OnStaAsync(Func<Task> action)
    {
        var completion=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var thread=new Thread(()=>
        {
            try
            {
                Task task=action();var timer=System.Diagnostics.Stopwatch.StartNew();
                while(!task.IsCompleted)
                {
                    if(timer.Elapsed>TimeSpan.FromSeconds(30))throw new TimeoutException("Installer fixture exceeded 30 seconds");
                    Application.DoEvents();Thread.Sleep(2);
                }
                task.GetAwaiter().GetResult();completion.SetResult();
            }
            catch(Exception ex){completion.SetException(ex);}
        }){IsBackground=true};
        thread.SetApartmentState(ApartmentState.STA);thread.Start();return completion.Task;
    }
}

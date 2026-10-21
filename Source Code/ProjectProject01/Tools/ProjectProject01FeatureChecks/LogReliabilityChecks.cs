using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using ProjectProject01BuildServerHelper;

internal static class LogReliabilityChecks
{
    internal const string StandardText = "백엔드 시작 복구: 진행/준비 경기 정리, 대기방 보존";
    internal const string ErrorText = "경기 결과 복구 거부: 한글 로그 검사";

    internal static async Task RunAsync(Action<bool,string> check)
    {
        var info = new ProcessStartInfo("dotnet", $"\"{typeof(LogReliabilityChecks).Assembly.Location}\" --encoding-fixture")
        { UseShellExecute=false, RedirectStandardOutput=true, RedirectStandardError=true, CreateNoWindow=true };
        ProcessLogEncoding.Configure(info);
        check(info.StandardOutputEncoding?.CodePage==65001 && info.StandardErrorEncoding?.CodePage==65001,
            "도우미 dotnet stdout/stderr UTF-8 고정");
        check(info.Environment["DOTNET_CLI_FORCE_UTF8_ENCODING"]=="true", "dotnet 하위 빌드 출력도 UTF-8 사용");
        using (var process = Process.Start(info)!)
        {
            var output=process.StandardOutput.ReadToEndAsync();var error=process.StandardError.ReadToEndAsync();
            using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(10));
            await process.WaitForExitAsync(timeout.Token);
            check(process.ExitCode==0 && (await output).Trim()==StandardText && (await error).Trim()==ErrorText,
                "실제 하위 프로세스에서 한국어 stdout/stderr 손실 없이 왕복");
        }
        var legacy=new ProcessStartInfo("powershell.exe","-NoProfile -File fixture.ps1");
        ProcessLogEncoding.Configure(legacy);
        check(legacy.StandardOutputEncoding is null, "레거시 PowerShell 출력 인코딩 임의 변경하지 않음");
        var uat=new ProcessStartInfo("cmd.exe","/c RunUAT.bat BuildCookRun -utf8output");
        ProcessLogEncoding.Configure(uat);
        check(uat.StandardOutputEncoding?.CodePage==65001, "명시적 UAT UTF-8 출력도 같은 방식으로 읽기");

        await CheckRecoveryAsync(check);
        CheckTickPump(check);
    }

    private sealed class Transport : HttpMessageHandler
    {
        internal int Requests;
        internal HttpStatusCode Status=HttpStatusCode.OK;
        internal string Body="";
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken ct)
        { Requests++;Body=await request.Content!.ReadAsStringAsync(ct);return new HttpResponseMessage(Status); }
    }

    private static async Task CheckRecoveryAsync(Action<bool,string> check)
    {
        string root=Path.GetFullPath(Path.Combine("Saved","ReviewRemediation","LogReliability",Guid.NewGuid().ToString("N")));
        Directory.CreateDirectory(root);
        const string origin="http://127.0.0.1:59998";
        string Journal(string match,string route="/api/server/matches/complete",string backend=origin)
        {
            var headers=new Dictionary<string,string>
            {
                [VersionCompatibilityOptions.ClientBuildHeader]="1.0.0",[VersionCompatibilityOptions.DedicatedServerBuildHeader]="1.0.0",
                [VersionCompatibilityOptions.ApiVersionHeader]="1",[VersionCompatibilityOptions.GameDataVersionHeader]="1",
                [VersionCompatibilityOptions.NetworkProtocolHeader]="3"
            };
            string payload=JsonSerializer.Serialize(new {schema=1,backend,route,headers,body=JsonSerializer.Serialize(new {matchId=match,userId="1",role="Survivor",success=true})});
            string sha1=Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(payload)));
            string file=Path.Combine(root,sha1+".json");File.WriteAllText(file,JsonSerializer.Serialize(new {payload,sha1}),new UTF8Encoding(false));return file;
        }
        using var transport=new Transport();using var client=new HttpClient(transport);
        Task Drain(string file)=>MatchResultRecoveryWorker.DrainFileAsync(file,[origin],new string('s',48),client,NullLogger.Instance,default);
        foreach(string route in new[]{"/api/server/matches/results","/api/server/matches/complete"})
        {
            string file=Journal("LocalPIE-UEDPIE_0_MultiplayTest",route);byte[] before=File.ReadAllBytes(file);
            await Drain(file);
            check(transport.Requests==0 && !File.Exists(file) && File.ReadAllBytes(file+".local-test").SequenceEqual(before),
                "과거 PIE 결과/완료 기록은 HTTP 없이 원본 바이트 보존: "+route);
            // A previously preserved test journal must not be overwritten by a duplicate legacy file.
            file=Journal("LocalPIE-UEDPIE_0_MultiplayTest",route);await Drain(file);
            check(Directory.GetFiles(root,Path.GetFileName(file)+"*.local-test").Length==2,
                "중복 테스트 기록 격리 시 기존 보관본 덮어쓰지 않음");
        }
        string invalid=Journal("invalid-fixture-id");await Drain(invalid);
        check(transport.Requests==0 && File.Exists(invalid+".invalid"),"잘못된 경기 ID는 전송 전에 보존·분리");
        string match=Guid.NewGuid().ToString("D");
        string real=Journal(match);await Drain(real);
        check(transport.Requests==1 && !File.Exists(real) && transport.Body.Contains(match),"정식 경기 완료는 기존 내용으로 전송·ACK 후 처리");
        real=Journal(match,"/api/server/matches/results");transport.Status=HttpStatusCode.ServiceUnavailable;await Drain(real);
        check(transport.Requests==2 && File.Exists(real),"정식 경기 503 오류는 저널 유지");
        transport.Status=HttpStatusCode.OK;await Drain(real);
        check(transport.Requests==3 && !File.Exists(real),"다음 복구 실행에서 정식 경기 재전송 성공");
        real=Journal(Guid.NewGuid().ToString("D"));transport.Status=HttpStatusCode.BadRequest;await Drain(real);
        check(File.Exists(real+".rejected"),"정식 경기 영구 거부는 기존처럼 보관");
        int sent=transport.Requests;string foreign=Journal(Guid.NewGuid().ToString("D"),backend:"http://203.0.113.1:59998");await Drain(foreign);
        check(transport.Requests==sent && File.Exists(foreign),"다른 서버로 호스트 비밀키 전송하지 않음");
    }

    private sealed class Clock : TimeProvider
    {
        internal long Ticks;
        public override long TimestampFrequency=>1_000_000;
        public override long GetTimestamp()=>Ticks;
        internal void Advance(double milliseconds)=>Ticks+=(long)(milliseconds*1000);
    }
    private static void CheckTickPump(Action<bool,string> check)
    {
        var queue=new ConcurrentQueue<Action>();var clock=new Clock();int ticks=0,done=0,failures=0;
        var order=new List<string>();int owner=Environment.CurrentManagedThreadId;bool sameThread=true;
        void Tick(){ticks++;order.Add("tick");sameThread&=Environment.CurrentManagedThreadId==owner;clock.Advance(1);}
        void Failure(Exception _){failures++;}
        check(EosTickPump.RunIteration(queue,Tick,Failure,default,clock)==9,"EOS 틱 실행시간을 대기 간격에 포함");
        for(int i=0;i<100;i++)queue.Enqueue(()=>{done++;sameThread&=Environment.CurrentManagedThreadId==owner;order.Add("command");});
        int before=ticks;
        int wait=EosTickPump.RunIteration(queue,Tick,Failure,default,clock);
        check(wait==0 && done==EosTickPump.MaximumCommandsPerIteration && ticks==before+1,
            "요청 적체 시 무제한 큐 배출 대신 틱 우선·배치 상한 유지");
        while(!queue.IsEmpty)EosTickPump.RunIteration(queue,Tick,Failure,default,clock);
        check(done==100 && ticks>=14 && sameThread,"적체 요청 전부 처리하면서 동일 SDK 소유 스레드에서 틱 지속");
        queue.Enqueue(()=>clock.Advance(35));
        check(EosTickPump.RunIteration(queue,Tick,Failure,default,clock)==0,"느린 요청 뒤 고정 10ms 지연을 추가하지 않음");
        queue.Enqueue(()=>throw new InvalidOperationException("fixture"));queue.Enqueue(()=>done++);
        EosTickPump.RunIteration(queue,Tick,Failure,default,clock);
        check(failures==1 && done==101,"하나의 명령 예외가 이후 명령과 틱을 중단하지 않음");
        before=ticks;using var cancelled=new CancellationTokenSource();cancelled.Cancel();
        check(EosTickPump.RunIteration(queue,Tick,Failure,cancelled.Token,clock)==0 && ticks==before,"종료 취소 후 추가 SDK 틱 없음");
    }
}

using System.Diagnostics;
using System.Diagnostics.Metrics;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using ProjectProject01BuildServerHelper;

internal static class DiagnosticsChecks
{
    internal static async Task RunAsync(Action<bool,string> check)
    {
        string output=Path.GetFullPath("Saved/ReviewRemediation/LightweightDiagnostics-20261022");Directory.CreateDirectory(output);
        using var metrics=new BackendDiagnostics();check(!metrics.CaptureEnabled&&metrics.Sample() is null,"진단 기본 꺼짐 / 원시 기록 생성 없음");
        var cache=new DiagnosticCacheCounters();Dictionary<string,double?> cacheValues=[];cache.Read(cacheValues);
        check(cacheValues["Cache.HitPercent"] is null&&cacheValues["Cache.LookupMeanMs"] is null,"조회 없는 캐시는 가짜 0%/0ms 아님");
        cache.Lookup(true,Stopwatch.Frequency/1000);cache.Lookup(true,Stopwatch.Frequency/1000);cache.Lookup(false,Stopwatch.Frequency/1000);
        cache.Expired();cache.Size(3,120);cache.Read(cacheValues);
        check(Math.Abs(cacheValues["Cache.HitPercent"]!.Value-200d/3)<.001&&cacheValues["Cache.Misses"]==1,"캐시 히트·미스 비율 계산");
        check(cacheValues["Cache.Evictions"]==0&&cacheValues["Cache.Expirations"]==1&&cacheValues["Cache.Entries"]==3,"만료와 제거 구분 / 크기 범위 분리");
        check(cacheValues["Cache.LookupMeanMs"] is >.9 and <1.1,"평균 조회 시간 stopwatch 단위 변환");
        using var meter=new Meter("MySqlConnector");
        var usage=meter.CreateUpDownCounter<long>("db.client.connections.usage");var maximum=meter.CreateUpDownCounter<long>("db.client.connections.max");
        var wait=meter.CreateHistogram<double>("db.client.connections.wait_time","s");
        // Sensitive label must never be copied into records, even though the source library emits it.
        usage.Add(2,new KeyValuePair<string,object?>("state","used"),new("pool.name","DO_NOT_EXPORT_connection_string"));
        usage.Add(8,new KeyValuePair<string,object?>("state","idle"));maximum.Add(10);
        using var observationEntered=new ManualResetEventSlim();using var releaseObservation=new ManualResetEventSlim();bool blockObservation=false;
        meter.CreateObservableGauge("db.client.connections.pending_requests",()=>{if(blockObservation){observationEntered.Set();releaseObservation.Wait(TimeSpan.FromSeconds(5));}return 7L;});
        metrics.SetCapture(true);metrics.BeginRequest();metrics.EndRequest(200,false,false,Stopwatch.Frequency/1000);
        metrics.BeginRequest();metrics.EndRequest(503,false,false,Stopwatch.Frequency/1000);
        metrics.BeginRequest();metrics.EndRequest(401,false,false,Stopwatch.Frequency/1000);
        metrics.BeginRequest();metrics.EndRequest(200,true,false,Stopwatch.Frequency/1000);
        wait.Record(.005);wait.Record(.015);metrics.Failure("DB","timeout");metrics.Failure("Result recovery","retry",503,1,3,30);
        var first=metrics.Sample()!;
        check(first.Values["HTTP.Completed"]==4&&first.Values["HTTP.ServerErrorPercent"]==25&&first.Values["HTTP.ClientErrorPercent"]==25&&first.Values["HTTP.Canceled"]==1,"TPS 집계 / 5xx·4xx·취소 분리");
        check(first.Values["DB.PoolUsePercent"]==20&&Math.Abs(first.Values["db.client.connections.wait_time.mean_ms"]!.Value-10)<.001,"실제 Meter 연결 풀 사용률과 대기 시간");
        check(first.Values["db.client.connections.pending_requests"]==7&&metrics.Sample()!.Values["db.client.connections.pending_requests"]==7,"Observable 게이지는 매 샘플 누적되지 않음");
        check(first.Values["Managed.AllocationBytesPerSecond"]>=0&&first.Values["ThreadPool.PendingWorkItems"]>=0&&first.Values["Process.Threads"]>0,"런타임 할당률·큐·스레드 실제 조회");
        check(first.Values["Process.FileDescriptors"] is null&&first.Values["Circuit.ResultRecovery.State"]==0,"미지원 파일 FD 구분 / 실제 서킷 상태 측정");
        string json=JsonSerializer.Serialize(first);
        check(!json.Contains("DO_NOT_EXPORT")&&!json.Contains("pool.name"),"연결 풀 이름/연결 문자열 태그 내보내기 제외");
        check(first.Failures.Any(f=>f.Source=="DB"&&f.Kind=="timeout")&&first.Failures.Any(f=>f.NextDelaySeconds==30),"타임아웃·재시도 세부사항");
        for(int i=0;i<500;++i)metrics.Failure("HTTP server","http-status",100+i);
        check(metrics.Sample()!.Failures.Length<=65,"오류 세부사항 고정 최대 개수");
        blockObservation=true;var sampling=Task.Run(()=>metrics.Sample());
        try
        {
            check(observationEntered.Wait(TimeSpan.FromSeconds(2)),"격리된 느린 계측 공급자 실행");
            var counter=Task.Run(()=>usage.Add(0,new KeyValuePair<string,object?>("state","used")));
            check(counter.Wait(TimeSpan.FromSeconds(1)),"느린 표본 조회가 일반 풀 카운터를 잠금으로 막지 않음");
        }
        finally{blockObservation=false;releaseObservation.Set();}
        await sampling;
        var writerRoot=Path.Combine(output,"BoundedWriterFixture");Directory.CreateDirectory(writerRoot);File.WriteAllText(Path.Combine(writerRoot,"keep.txt"),"not diagnostic output");
        var writer=new BoundedDiagnosticWriter(writerRoot);
        for(int i=0;i<965;++i)await writer.WriteAsync(first with{UtcTime=first.UtcTime.AddSeconds(i)},CancellationToken.None);
        var slots=Directory.GetFiles(writerRoot,"backend-metrics-*.jsonl");
        check(slots.Length==8&&slots.All(file=>new FileInfo(file).Length<=BoundedDiagnosticWriter.MaximumFileBytes&&File.ReadLines(file).Count()<=120),"실제 파일 기록 965회에도 8슬롯·120행·파일 크기 상한 유지");
        check(File.Exists(Path.Combine(writerRoot,"keep.txt")),"순환 기록은 다른 사용자 파일을 지우지 않음");
        metrics.SetCapture(false);metrics.SetCapture(true);
        var concurrent=Task.Run(()=>Parallel.For(0,20000,i=>{metrics.BeginRequest();metrics.EndRequest(503,false,false,1);}));
        bool coherent=true;
        for(int i=0;i<10&&!concurrent.IsCompleted;++i)
        {
            var snapshot=metrics.Sample()!;
            if(snapshot.Values["HTTP.Completed"]>0)coherent&=snapshot.Values["HTTP.ServerErrorPercent"]==100;
            await Task.Delay(1);
        }
        await concurrent;var finalWindow=metrics.Sample()!;
        if(finalWindow.Values["HTTP.Completed"]>0)coherent&=finalWindow.Values["HTTP.ServerErrorPercent"]==100;
        check(coherent&&finalWindow.Values["HTTP.ActiveRequests"]==0,"동시 요청 중에도 완료·오류 분모/분자와 동시 수 일관성 유지");
        metrics.SetCapture(false);check(metrics.Sample() is null,"수집 중지 후 스냅샷 생성 중단");
        await CircuitBreakerChecks.RunAsync(check,output);
        if(OperatingSystem.IsWindows())
        {
            var before=ProcessSocketCounters.Capture();var tcp=new TcpListener(IPAddress.Loopback,0);tcp.Start();
            using var udp=new UdpClient(new IPEndPoint(IPAddress.Loopback,0));
            try{var after=ProcessSocketCounters.Capture();check(before.Tcp.HasValue&&after.Tcp>=before.Tcp+1&&before.Udp.HasValue&&after.Udp>=before.Udp+1,"실제 PID 소유 TCP·UDP 엔드포인트 증감");}
            finally{tcp.Stop();}
        }
        await CheckEndpoints(metrics,check);
        File.WriteAllText(Path.Combine(output,"BackendFixture.jsonl"),json+"\n");
        var stopwatch=Stopwatch.StartNew();long bytesBefore=GC.GetTotalAllocatedBytes(false);const int iterations=100000;
        for(int i=0;i<iterations;++i){metrics.BeginRequest();metrics.EndRequest(200,false,false,1);}
        stopwatch.Stop();
        File.WriteAllText(Path.Combine(output,"CounterMicrobenchmark.json"),JsonSerializer.Serialize(new{iterations,elapsedMs=stopwatch.Elapsed.TotalMilliseconds,allocatedBytes=Math.Max(0,GC.GetTotalAllocatedBytes(false)-bytesBefore),scope="Synthetic counter-only microbenchmark; not game/backend load benchmark; capture OFF"},new JsonSerializerOptions{WriteIndented=true}));
        using var helper=new MainForm();var tabs=Children(helper).OfType<TabControl>().Single(c=>c.Name=="HelperActionTabs");
        check(tabs.TabPages.Count==5,"도우미 작업 버튼 5개 범주 분리");
        var buttonNames=Children(tabs).OfType<Button>().Select(b=>b.Text).ToArray();
        string[] expected=["환경 검사","클라이언트 패키징","클라이언트 업데이트 게시","데디케이티드 서버 패키징","MySQL 초기화/시작","MySQL 중지","DB 백업·복원","백엔드 시작","백엔드 중지","EOS 보이스 자격 증명","IP 직접 연결 방화벽 설정","IP 직접 연결 검사","서버 운영","게임 서버 운영 시작","경기 종료 후 운영 중지","종료 대기 취소","게임 서버 운영 즉시 중지","전체 흐름 스모크 테스트","로그 폴더","진단 결과 폴더","빌드 폴더","진단 대시보드 열기"];
        check(expected.All(name=>buttonNames.Count(b=>b==name)==1),"기존 21개 작업 유지 / 대시보드 진입 추가 / 중복 없음");
        helper.ShowInTaskbar=false;helper.Opacity=0;helper.Location=new Point(-32000,-32000);helper.Show();helper.PerformLayout();Application.DoEvents();
        foreach(TabPage page in tabs.TabPages)
        {
            tabs.SelectedTab=page;helper.PerformLayout();Application.DoEvents();
            using var image=new Bitmap(helper.Width,helper.Height);helper.DrawToBitmap(image,new Rectangle(Point.Empty,helper.Size));image.Save(Path.Combine(output,$"Helper-{tabs.SelectedIndex}.png"));
            check(Children(page).OfType<Button>().All(b=>b.Width>20&&b.Height>20),"도우미 버튼 배치: "+page.Text);
        }
        helper.Hide();
    }
    private static async Task CheckEndpoints(BackendDiagnostics metrics,Action<bool,string> check)
    {
        var builder=WebApplication.CreateBuilder();builder.Logging.ClearProviders();builder.WebHost.UseUrls("http://127.0.0.1:0");
        var admin=AdministratorOptions.FromConfiguration(new ConfigurationBuilder().AddInMemoryCollection(new Dictionary<string,string?>{{"Administrator:ApiKey",new string('x',32)}}).Build());
        builder.Services.AddSingleton(metrics);builder.Services.AddSingleton(admin);
        await using var app=builder.Build();app.UseBackendDiagnostics();app.MapBackendDiagnostics();
        app.MapGet("/fixture/ok",()=>Results.Ok());app.MapGet("/fixture/error",()=>Results.StatusCode(503));
        await app.StartAsync();
        try
        {
            string address=app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single();
            using var client=new HttpClient{BaseAddress=new Uri(address),Timeout=TimeSpan.FromSeconds(3)};
            using(var denied=await client.PostAsync("/api/admin/diagnostics?enabled=true",null))check(denied.StatusCode==HttpStatusCode.Unauthorized&&!metrics.CaptureEnabled,"운영자 인증 없는 수집 시작 차단");
            client.DefaultRequestHeaders.Add("X-ProjectProject01-Admin-Key",new string('x',32));
            using(var started=await client.PostAsync("/api/admin/diagnostics?enabled=true",null))check(started.IsSuccessStatusCode&&metrics.CaptureEnabled,"운영자 수집 시작 API");
            using(var ok=await client.GetAsync("/fixture/ok")){}
            using(var error=await client.GetAsync("/fixture/error")){}
            using(var read=await client.GetAsync("/api/admin/diagnostics"))check(read.IsSuccessStatusCode,"운영자 스냅샷 API");
            var sample=metrics.Sample()!;check(sample.Values["HTTP.Completed"]==2&&sample.Values["HTTP.ServerErrorPercent"]==50,"실제 요청 미들웨어 + 진단 API 자체 트래픽 제외");
            using(var stopped=await client.PostAsync("/api/admin/diagnostics?enabled=false",null))check(stopped.IsSuccessStatusCode&&!metrics.CaptureEnabled,"운영자 수집 중지 API");
        }
        finally{await app.StopAsync();}
    }
    private static IEnumerable<Control> Children(Control root){foreach(Control child in root.Controls){yield return child;foreach(var nested in Children(child))yield return nested;}}
}

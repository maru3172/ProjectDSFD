using System.Text.Json;

namespace ProjectProject01DiagnosticsDashboard;

internal static class ExtendedDiagnosticsTests
{
    internal static void Run(PerformanceSample sample)
    {
        void Check(bool value,string name){if(!value)throw new InvalidOperationException("Extended diagnostics: "+name);}
        Check(RuntimeMetricAnalysis.Read(new Dictionary<string,string>()).Count==0,"legacy counters absent, not zeros");
        var counters=RuntimeMetricAnalysis.Read(new Dictionary<string,string>{{"ProcessHandleCount","-1"},{"ProcessThreadCount","NaN"},{"TcpEndpointCount","2"}});
        Check(counters.Count==1&&counters["TcpEndpointCount"]==2,"unsupported/nonfinite rejected");
        var samples=new[]{sample with{GameSeconds=0,RuntimeCounters=new(){{"SampleWallSeconds",5},{"HttpCompletedCount",10},{"HttpServerFailureCount",1}}},
            sample with{GameSeconds=60,RuntimeCounters=new(){{"SampleWallSeconds",5},{"HttpCompletedCount",20},{"HttpServerFailureCount",1}}}};
        var gc=new[]{new DiagnosticEvent("",sample.Role,sample.NetMode,1,"Normal","GarbageCollectionMemoryDelta","ElapsedMilliseconds=2;PhysicalBeforeBytes=104857600;PhysicalAfterBytes=83886080;AllocatorBeforeAvailable=1;AllocatorAfterAvailable=1;AllocatorUsedBeforeBytes=1048576;AllocatorUsedAfterBytes=0",""),
            new DiagnosticEvent("",sample.Role,sample.NetMode,61,"Normal","GarbageCollectionMemoryDelta","ElapsedMilliseconds=4;PhysicalBeforeBytes=83886080;PhysicalAfterBytes=100663296","")};
        var rows=RuntimeMetricAnalysis.Analyze(samples,gc);
        Check(rows.Single(r=>r.Metric=="인증·로비 HTTP 처리량").Value=="3","rate uses observed interval, not fixed timer");
        Check(rows.Single(r=>r.Metric=="GC 빈도").Value=="2"&&rows.Single(r=>r.Metric=="GC 평균 경과 시간").Value=="3","GC count/minute and mean elapsed");
        Check(rows.Single(r=>r.Metric=="GC당 평균 프로세스 RAM 순감소").Value=="2","negative freed delta retained as growth");
        Check(rows.Single(r=>r.Metric=="GC당 평균 사용 중 할당 순감소").Value=="1","unavailable allocator events excluded");
        Check(rows.Single(r=>r.Metric=="Windows 파일 디스크립터").Value.StartsWith("측정 불가"),"FD not conflated with handles");
        var reduced=TrendChart.ReducePoints(Enumerable.Range(0,100000).Select(i=>new ChartPoint("test",i,i==12345?9999:i==55555?-9999:1)));
        Check(reduced.Count<=800&&reduced.Any(p=>p.Y==9999)&&reduced.Any(p=>p.Y==-9999)&&reduced[0].X==0&&reduced[^1].X==99999,"bounded chart retains extrema and endpoints");
        string id=Guid.NewGuid().ToString("N");var utc=DateTimeOffset.UtcNow;
        var backend=new BackendMetricSnapshot(1,id,utc,42,5,new(){{"HTTP.CompletedPerSecond",4},{"Process.FileDescriptors",null}},new(){{"CircuitBreaker","NotImplemented"}},[]);
        string camel=JsonSerializer.Serialize(backend,new JsonSerializerOptions{PropertyNamingPolicy=JsonNamingPolicy.CamelCase});
        Check(BackendMetricFiles.Parse(camel)?.Values["HTTP.CompletedPerSecond"]==4,"live API camelCase contract");
        Check(BackendMetricFiles.Parse(JsonSerializer.Serialize(backend))?.Values["Process.FileDescriptors"] is null,"record PascalCase and unavailable contract");
        Check(BackendMetricFiles.Parse(new string('x',65537)) is null&&BackendMetricFiles.Parse("{broken") is null,"oversized/truncated record ignored");
        Check(BackendMetricFiles.Parse(JsonSerializer.Serialize(backend with{CaptureId="=cmd"})) is null,"untrusted capture identity cannot inject CSV formula");
        string root=Path.Combine(Path.GetTempPath(),"ProjectDFSD-Diagnostics-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        string newer=Path.Combine(root,"slot-0.jsonl"),older=Path.Combine(root,"slot-7.jsonl");
        // Ring slot order is not chronological. Keep the newest 720, not the last file's 720.
        File.WriteAllLines(newer,Enumerable.Range(500,500).Select(i=>JsonSerializer.Serialize(backend with{UtcTime=utc.AddSeconds(i)})));
        File.WriteAllLines(older,Enumerable.Range(0,500).Select(i=>JsonSerializer.Serialize(backend with{UtcTime=utc.AddSeconds(i)})));
        var loaded=BackendMetricFiles.Load([newer,older]);
        Check(loaded.Count==720&&loaded[0].UtcTime==utc.AddSeconds(280)&&loaded[^1].UtcTime==utc.AddSeconds(999),"bounded latest records across unordered ring files");
        using(var host=new Form{Width=1200,Height=720,ShowInTaskbar=false,Opacity=0,StartPosition=FormStartPosition.Manual,Location=new Point(-32000,-32000)})
        {
            var panel=new BackendDiagnosticsPanel();host.Controls.Add(panel);
            string actualFile=Path.GetFullPath("Saved/ReviewRemediation/LightweightDiagnostics-20261022/BackendFixture.jsonl");
            var actual=File.Exists(actualFile)?BackendMetricFiles.Load([actualFile]):[];
            Check(actual.Count==0||actual[0].Values.ContainsKey("Managed.AllocationBytesPerSecond"),"backend writer DTO and dashboard parser integration");
            panel.LoadForTest(actual.Count>0?actual:[backend]);host.Show();host.PerformLayout();Application.DoEvents();
            string output=Path.GetFullPath("Saved/ReviewRemediation/LightweightDiagnostics-20261022");Directory.CreateDirectory(output);
            using var image=new Bitmap(host.Width,host.Height);host.DrawToBitmap(image,new Rectangle(Point.Empty,host.Size));image.Save(Path.Combine(output,"DashboardBackend.png"));host.Hide();
        }
        Console.WriteLine("Extended diagnostics self-tests passed (GC/rates, legacy, API/file schema, bounded imports/charts, UI render).");
    }
}

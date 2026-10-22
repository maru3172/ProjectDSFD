using System.Globalization;
using System.Net;
using System.Text;
using System.Text.Json;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed record BackendMetricFailure(string Source,string Kind,int Status,long Count,int? Attempt,int? Maximum,double? NextDelaySeconds);
internal sealed record BackendMetricSnapshot(int Schema,string CaptureId,DateTimeOffset UtcTime,int ProcessId,double IntervalSeconds,
    Dictionary<string,double?> Values,Dictionary<string,string> States,BackendMetricFailure[] Failures);
internal static class BackendMetricFiles
{
    internal static readonly JsonSerializerOptions Options=new(){PropertyNameCaseInsensitive=true};
    internal static BackendMetricSnapshot? Parse(string line)
    {
        if(line.Length>65536)return null;
        try
        {
            var record=JsonSerializer.Deserialize<BackendMetricSnapshot>(line,Options);
            if(record is null||record.Schema!=1||record.CaptureId is null||!Guid.TryParseExact(record.CaptureId,"N",out _)||record.Values is null||record.Values.Count>128||record.States is null||record.States.Count>16||record.Failures is null||record.Failures.Length>65)return null;
            if(!double.IsFinite(record.IntervalSeconds)||record.IntervalSeconds<=0)return null;
            if(record.States.Any(p=>p.Key.Length>100||p.Value is null||p.Value.Length>1024)||record.Failures.Any(f=>f is null||f.Source is null||f.Source.Length>64||f.Kind is null||f.Kind.Length>64||f.Count<0||f.Status<0||f.Status>599))return null;
            record.Values.Keys.Where(k=>k.Length>100||k.Any(c=>!char.IsAsciiLetterOrDigit(c)&&c is not('.' or '_'))).ToList().ForEach(k=>record.Values.Remove(k));
            foreach(var key in record.Values.Keys.ToArray())if(record.Values[key] is{} value&&!double.IsFinite(value))record.Values[key]=null;
            return record;
        }
        catch(Exception e)when(e is JsonException or NotSupportedException){return null;}
    }
    internal static List<BackendMetricSnapshot> Load(IEnumerable<string> files)
    {
        var bounded=new PriorityQueue<BackendMetricSnapshot,long>();
        foreach(string file in files.Take(8))
        {
            if(new FileInfo(file).Length>4*1024*1024)throw new InvalidDataException("진단 기록 파일은 각각 4 MiB 이하여야 합니다.");
            using var stream=new FileStream(file,FileMode.Open,FileAccess.Read,FileShare.ReadWrite);
            using var reader=new StreamReader(stream);
            while(reader.ReadLine() is{} line)
                if(Parse(line) is{} sample){bounded.Enqueue(sample,sample.UtcTime.UtcTicks);if(bounded.Count>720)bounded.Dequeue();}
        }
        return bounded.UnorderedItems.Select(x=>x.Element).OrderBy(s=>s.UtcTime).ToList();
    }
}

internal sealed class BackendDiagnosticsPanel:UserControl
{
    private readonly TextBox address=new(){Width=210,Text="http://127.0.0.1:5080"};
    private readonly TextBox key=new(){Width=210,MaxLength=512,UseSystemPasswordChar=true,PlaceholderText="운영자 API 키 (저장하지 않음)"};
    private readonly Label status=new(){AutoSize=true,Text="수집 꺼짐 · 운영자 인증 필요 · 5초 간격 / 최대 720표본 표시"};
    private readonly TextBox scope=new(){Dock=DockStyle.Bottom,Height=76,ReadOnly=true,Multiline=true,ScrollBars=ScrollBars.Vertical};
    private readonly DataGridView values=Grid(),failures=Grid();
    private readonly ComboBox metric=new(){Dock=DockStyle.Top,DropDownStyle=ComboBoxStyle.DropDownList};
    private readonly TrendChart chart=new(){Dock=DockStyle.Fill};
    private readonly System.Windows.Forms.Timer timer=new(){Interval=5000};
    private readonly HttpClient client=new(new HttpClientHandler{AllowAutoRedirect=false}){Timeout=TimeSpan.FromSeconds(4)};
    private readonly List<BackendMetricSnapshot> history=[];
    private readonly CancellationTokenSource lifetime=new();
    private bool busy;
    private bool resourcesDisposed;
    public BackendDiagnosticsPanel()
    {
        Dock=DockStyle.Fill;
        var start=new Button{Text="수집 시작",AutoSize=true};var stop=new Button{Text="수집 중지",AutoSize=true};
        var refresh=new Button{Text="새로고침",AutoSize=true};var open=new Button{Text="기록 열기",AutoSize=true};var export=new Button{Text="CSV 내보내기",AutoSize=true};
        var top=new FlowLayoutPanel{Dock=DockStyle.Top,Height=82,AutoScroll=true,WrapContents=true};
        top.Controls.AddRange([address,key,start,stop,refresh,open,export,status]);
        var tabs=new TabControl{Dock=DockStyle.Fill};
        tabs.TabPages.Add(new TabPage("현재 지표"){Controls={values}});
        var trend=new TabPage("시간별 그래프");trend.Controls.Add(chart);trend.Controls.Add(metric);tabs.TabPages.Add(trend);
        tabs.TabPages.Add(new TabPage("오류 세부사항"){Controls={failures}});
        Controls.Add(tabs);Controls.Add(scope);Controls.Add(top);
        start.Click+=async(_,_)=>await Fetch(true);stop.Click+=async(_,_)=>await Fetch(false);refresh.Click+=async(_,_)=>await Fetch(null);
        timer.Tick+=async(_,_)=>await Fetch(null);
        open.Click+=async(_,_)=>
        {
            using var dialog=new OpenFileDialog{Filter="백엔드 진단 (*.jsonl)|*.jsonl",Multiselect=true};
            if(dialog.ShowDialog(this)!=DialogResult.OK)return;
            timer.Stop();
            try{var loaded=await Task.Run(()=>BackendMetricFiles.Load(dialog.FileNames));if(IsDisposed)return;history.Clear();history.AddRange(loaded);ShowLatest();status.Text=$"저장 기록 {history.Count}개 (최대 720개)";}
            catch(Exception ex)when(ex is IOException or UnauthorizedAccessException or InvalidDataException){status.Text="기록 읽기 실패: "+ex.GetType().Name;}
        };
        export.Click+=(_,_)=>Export();metric.SelectedIndexChanged+=(_,_)=>Draw();
    }
    private Uri Origin()
    {
        if(!Uri.TryCreate(address.Text.Trim(),UriKind.Absolute,out var uri)||uri.Scheme is not("http" or "https")||uri.AbsolutePath!="/"||uri.Query.Length>0||uri.Fragment.Length>0||uri.UserInfo.Length>0)throw new InvalidDataException();
        if(uri.Scheme=="http")
        {
            if(!IPAddress.TryParse(uri.Host,out var ip))throw new InvalidDataException();
            var b=ip.GetAddressBytes();bool local=IPAddress.IsLoopback(ip)||b.Length==4&&(b[0]==10||b[0]==25||b[0]==192&&b[1]==168||b[0]==172&&b[1]>=16&&b[1]<=31);
            if(!local)throw new InvalidDataException();
        }
        return uri;
    }
    private async Task Fetch(bool? capture)
    {
        if(IsDisposed||resourcesDisposed||busy)return;busy=true;
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);timeout.CancelAfter(TimeSpan.FromSeconds(5));
        var token=timeout.Token;
        try
        {
            var origin=Origin();if(key.Text.Length<32)throw new InvalidDataException();
            if(capture.HasValue)
            {
                using var command=new HttpRequestMessage(HttpMethod.Post,new Uri(origin,"/api/admin/diagnostics?enabled="+capture.Value.ToString().ToLowerInvariant()));
                command.Headers.Add("X-ProjectProject01-Admin-Key",key.Text);
                using var result=await client.SendAsync(command,HttpCompletionOption.ResponseHeadersRead,token);result.EnsureSuccessStatusCode();
            }
            using var request=new HttpRequestMessage(HttpMethod.Get,new Uri(origin,"/api/admin/diagnostics"));
            request.Headers.Add("X-ProjectProject01-Admin-Key",key.Text);
            using var response=await client.SendAsync(request,HttpCompletionOption.ResponseHeadersRead,token);response.EnsureSuccessStatusCode();
            if(response.Content.Headers.ContentLength>131072)throw new InvalidDataException();
            await using var stream=await response.Content.ReadAsStreamAsync(token);
            using var buffer=new MemoryStream();byte[] bytes=new byte[4096];int read;
            while((read=await stream.ReadAsync(bytes,token))>0){if(buffer.Length+read>131072)throw new InvalidDataException();buffer.Write(bytes,0,read);}
            if(IsDisposed)return;
            using var json=JsonDocument.Parse(buffer.ToArray());
            bool enabled=json.RootElement.GetProperty("enabled").GetBoolean();
            if(json.RootElement.TryGetProperty("snapshot",out var item)&&item.ValueKind==JsonValueKind.Object&&BackendMetricFiles.Parse(item.GetRawText()) is{} sample)
            {
                if(history.Count==0||history[^1].CaptureId!=sample.CaptureId||history[^1].UtcTime!=sample.UtcTime)
                {history.Add(sample);if(history.Count>720)history.RemoveAt(0);ShowLatest();}
            }
            if(enabled)timer.Start();else timer.Stop();status.Text=enabled?"수집 중 · 5초 간격 · 최근 최대 720표본":"수집 중지 · 마지막 기록 유지";
        }
        catch(Exception ex)when(ex is HttpRequestException or OperationCanceledException or IOException or JsonException or InvalidOperationException or InvalidDataException or ArgumentException or FormatException)
        {if(!IsDisposed){timer.Stop();status.Text="진단 연결 실패 (주소·운영자 키·백엔드 상태 확인): "+ex.GetType().Name;}}
        finally{busy=false;}
    }
    private void ShowLatest()
    {
        if(history.Count==0){status.Text="읽을 수 있는 표본이 없습니다.";return;}
        var last=history[^1];
        values.DataSource=last.Values.OrderBy(x=>x.Key).Select(x=>new{항목=MetricLabels.Label(x.Key),값=MetricLabels.Value(x.Key,x.Value),지표=x.Key}).ToList();
        failures.DataSource=history.TakeLast(12).SelectMany(s=>s.Failures.Select(f=>new{시각=s.UtcTime.ToLocalTime().ToString("HH:mm:ss"),범위=f.Source,원인=f.Kind,HTTP=f.Status,횟수=f.Count,시도=f.Attempt,최대=f.Maximum,다음대기초=f.NextDelaySeconds})).TakeLast(200).ToList();
        scope.Text=$"PID {last.ProcessId} · 표본 구간 {last.IntervalSeconds:F2}초 · 공유 프로세스끼리 합산 금지 · 오류는 최근 12구간/최대 200행\r\n"+string.Join("\r\n",last.States.Select(s=>MetricLabels.State(s.Key,s.Value)));
        string? selected=metric.SelectedItem as string;var keys=history.SelectMany(s=>s.Values.Keys).Distinct().Order().ToArray();
        if(!metric.Items.Cast<string>().SequenceEqual(keys)){metric.Items.Clear();metric.Items.AddRange(keys);if(keys.Length>0)metric.SelectedItem=selected is not null&&keys.Contains(selected)?selected:keys[0];}
        Draw();
    }
    private void Draw()
    {
        if(metric.SelectedItem is not string selected||history.Count==0)return;
        var origin=history[0].UtcTime;
        chart.SetData(selected,history.Select(s=>new ChartPoint($"PID {s.ProcessId} / {s.CaptureId[..Math.Min(8,s.CaptureId.Length)]}",(s.UtcTime-origin).TotalSeconds,s.Values.GetValueOrDefault(selected)??double.NaN)));
    }
    private void Export()
    {
        if(history.Count==0)return;
        using var dialog=new SaveFileDialog{Filter="CSV|*.csv",FileName="BackendMetrics.csv"};if(dialog.ShowDialog(this)!=DialogResult.OK)return;
        try
        {
            var keys=history.SelectMany(s=>s.Values.Keys).Distinct().Order().ToArray();
            using var writer=new StreamWriter(dialog.FileName,false,new UTF8Encoding(true));
            writer.WriteLine("UtcTime,CaptureId,ProcessId,IntervalSeconds,"+string.Join(',',keys));
            foreach(var s in history)writer.WriteLine(string.Join(',',new[]{s.UtcTime.ToString("O"),s.CaptureId,s.ProcessId.ToString(CultureInfo.InvariantCulture),s.IntervalSeconds.ToString(CultureInfo.InvariantCulture)}.Concat(keys.Select(k=>s.Values.GetValueOrDefault(k)?.ToString("R",CultureInfo.InvariantCulture)??""))));
            status.Text="CSV 저장 완료 (측정 불가 값은 빈 칸)";
        }
        catch(Exception ex)when(ex is IOException or UnauthorizedAccessException){status.Text="CSV 저장 실패: "+ex.GetType().Name;}
    }
    private static DataGridView Grid()=>new(){Dock=DockStyle.Fill,ReadOnly=true,AllowUserToAddRows=false,AllowUserToDeleteRows=false,AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.DisplayedCells};
    internal void LoadForTest(IEnumerable<BackendMetricSnapshot> samples){history.Clear();history.AddRange(samples.TakeLast(720));ShowLatest();}
    protected override void Dispose(bool disposing){if(disposing&&!resourcesDisposed){resourcesDisposed=true;lifetime.Cancel();timer.Stop();timer.Dispose();client.Dispose();lifetime.Dispose();}base.Dispose(disposing);}
}

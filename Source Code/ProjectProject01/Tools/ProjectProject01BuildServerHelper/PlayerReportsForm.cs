using System.Text.Json;

namespace ProjectProject01BuildServerHelper;

// Admin credentials are supplied by the existing operation window, never by a game client.
internal sealed class PlayerReportsForm : Form
{
    private readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(8)};
    private readonly CancellationTokenSource closing=new();
    private readonly CancellationToken token;
    private readonly Func<HttpMethod,string,object?,HttpRequestMessage> create;
    private readonly ListBox reports=new(){Dock=DockStyle.Left,Width=390,DisplayMember=nameof(Report.Label)};
    private readonly TextBox detail=new(){Dock=DockStyle.Fill,Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical};
    private readonly TextBox note=new(){Dock=DockStyle.Bottom,Height=100,Multiline=true,MaxLength=2000,ScrollBars=ScrollBars.Vertical,PlaceholderText="운영자 검토 메모 (클라이언트에는 공개하지 않음)"};
    private readonly ComboBox filter=new(){DropDownStyle=ComboBoxStyle.DropDownList,Width=110};
    private readonly Label status=new(){Dock=DockStyle.Bottom,Height=40,Text="신고 접수 목록 — 신고만으로 자동 제재하지 않습니다."};
    private readonly Button next=new(){Text="이전 접수 더 보기",AutoSize=true,Enabled=false};
    private bool pending;
    private string? before,nextBefore;
    private sealed record Report(string Id,string Label,string Detail,string Note);
    internal static string Reason(string value)=>value switch{"Abuse"=>"욕설·괴롭힘","Cheating"=>"부정행위 의심","Griefing"=>"게임 진행 방해","Other"=>"기타",_=>value};
    internal static string State(string value)=>value switch{"Open"=>"미확인","Reviewed"=>"검토 완료","Dismissed"=>"보류 종료",_=>value};

    public PlayerReportsForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        this.create=create;token=closing.Token;Text="플레이어 신고 확인";Width=1160;Height=720;MinimumSize=new Size(900,540);StartPosition=FormStartPosition.CenterParent;
        var toolbar=new FlowLayoutPanel{Dock=DockStyle.Top,AutoSize=true};
        filter.Items.AddRange(["전체","미확인","검토 완료","보류 종료"]);filter.SelectedIndex=0;
        toolbar.Controls.Add(new Label{Text="처리 상태",AutoSize=true});toolbar.Controls.Add(filter);
        Add(toolbar,"최신 목록 / 새로고침",async()=>{before=null;await LoadReports();});
        toolbar.Controls.Add(next);next.Click+=async(_,_)=>await Run(async()=>{before=nextBefore;await LoadReports();});
        var actions=new FlowLayoutPanel{Dock=DockStyle.Bottom,AutoSize=true};
        Add(actions,"검토 완료",()=>Review("Reviewed"));Add(actions,"보류 종료",()=>Review("Dismissed"));Add(actions,"미확인으로 변경",()=>Review("Open"));
        var right=new Panel{Dock=DockStyle.Fill,Padding=new Padding(8)};
        right.Controls.Add(detail);right.Controls.Add(note);right.Controls.Add(actions);
        Controls.Add(right);Controls.Add(reports);Controls.Add(toolbar);Controls.Add(status);
        reports.SelectedIndexChanged+=(_,_)=>{if(reports.SelectedItem is Report r){detail.Text=r.Detail;note.Text=r.Note;}else{detail.Clear();note.Clear();}};
        filter.SelectedIndexChanged+=async(_,_)=>await Run(async()=>{before=null;await LoadReports();});
        Shown+=async(_,_)=>await Run(LoadReports);
        FormClosed+=(_,_)=>{closing.Cancel();http.Dispose();closing.Dispose();};
    }
    private void Add(FlowLayoutPanel panel,string label,Func<Task> action)
    {var b=new Button{Text=label,AutoSize=true};panel.Controls.Add(b);b.Click+=async(_,_)=>await Run(action);}
    private async Task Run(Func<Task> action)
    {
        if(pending||IsDisposed)return;pending=true;filter.Enabled=false;
        try{await action();}
        catch(OperationCanceledException)when(token.IsCancellationRequested){}
        catch(Exception ex){if(!IsDisposed)status.Text="신고 조회/처리 실패: "+ex.Message;}
        finally{pending=false;if(!IsDisposed)filter.Enabled=true;}
    }
    private async Task LoadReports()
    {
        string state=filter.SelectedIndex switch{1=>"Open",2=>"Reviewed",3=>"Dismissed",_=>"All"};
        using var req=create(HttpMethod.Get,"/api/admin/community/reports?status="+state+(before is null?"":"&before="+Uri.EscapeDataString(before)),null);
        using var response=await http.SendAsync(req,token);string json=await response.Content.ReadAsStringAsync(token);
        if(!response.IsSuccessStatusCode)throw new InvalidOperationException($"{(int)response.StatusCode}: {json}");
        using var doc=JsonDocument.Parse(json);if(IsDisposed||token.IsCancellationRequested)return;
        string? selected=(reports.SelectedItem as Report)?.Id;reports.Items.Clear();
        foreach(var r in doc.RootElement.GetProperty("reports").EnumerateArray())
        {
            string V(string key)=>r.TryGetProperty(key,out var value)&&value.ValueKind!=JsonValueKind.Null?value.ToString():"";
            string label=$"#{V("id")} [{State(V("status"))}] {V("targetName")} — {Reason(V("reason"))}";
            string text=$"접수 번호: {V("id")}\r\n상태: {State(V("status"))}\r\n접수 시각 (UTC): {V("createdAtUtc")}\r\n검토 시각 (UTC): {V("reviewedAtUtc")}\r\n\r\n신고자: {V("reporterName")} [{V("reporterAccount")}] (ID {V("reporterId")})\r\n신고 대상: {V("targetName")} [{V("targetAccount")}] (ID {V("targetId")})\r\n방: {V("roomName")} ({V("roomId")})\r\n\r\n신고 사유: {Reason(V("reason"))}\r\n\r\n{V("details")}";
            var item=new Report(V("id"),label,text,V("reviewNote"));reports.Items.Add(item);if(item.Id==selected)reports.SelectedItem=item;
        }
        if(reports.SelectedIndex<0&&reports.Items.Count>0)reports.SelectedIndex=0;
        nextBefore=doc.RootElement.GetProperty("nextBefore").GetString();next.Enabled=doc.RootElement.GetProperty("hasMore").GetBoolean();
        status.Text=$"이 페이지 {reports.Items.Count}건 — 최대 100건씩 조회합니다. 신고만으로 자동 제재하지 않습니다.";
    }
    private async Task Review(string state)
    {
        if(reports.SelectedItem is not Report r)throw new InvalidOperationException("검토할 신고를 선택하세요.");
        using var req=create(HttpMethod.Put,"/api/admin/community/reports/"+r.Id,new{status=state,note=note.Text});
        using var response=await http.SendAsync(req,token);
        if(!response.IsSuccessStatusCode)throw new InvalidOperationException($"{(int)response.StatusCode}: "+await response.Content.ReadAsStringAsync(token));
        if(!IsDisposed)await LoadReports();
    }
}

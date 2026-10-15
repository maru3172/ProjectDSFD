using System.Text.Json;

namespace ProjectProject01BuildServerHelper;
internal sealed class AccountRestrictionForm : Form
{
    private readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(8)};
    private readonly TextBox account=new(){Width=180,PlaceholderText="계정 ID"};
    private readonly TextBox reason=new(){Width=330,MaxLength=512,PlaceholderText="이용 제한 사유"};
    private readonly NumericUpDown minutes=new(){Minimum=1,Maximum=525600,Value=60,Width=100};
    private readonly CheckBox permanent=new(){Text="영구 제한",AutoSize=true};
    private readonly DataGridView history=new(){Dock=DockStyle.Fill,ReadOnly=true,AllowUserToAddRows=false,AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill};
    private readonly Label status=new(){Dock=DockStyle.Bottom,Height=28};
    public AccountRestrictionForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        Text="계정 이용 제한 · 기간제/영구 · 해제 이력";Width=1100;Height=580;
        var inputs=new FlowLayoutPanel{Dock=DockStyle.Top,AutoSize=true};
        inputs.Controls.AddRange([account,reason,new Label{Text="기간(분)",AutoSize=true},minutes,permanent]);
        permanent.CheckedChanged+=(_,_)=>minutes.Enabled=!permanent.Checked;
        foreach(string name in new[]{"계정","사유","시작","만료","해제","상태"})history.Columns.Add(name,name);
        history.CellClick+=(_,e)=>{if(e.RowIndex>=0)account.Text=history.Rows[e.RowIndex].Cells[0].Value?.ToString()??"";};
        var buttons=new FlowLayoutPanel{Dock=DockStyle.Top,AutoSize=true};
        async Task LoadAsync()
        {
            using var request=create(HttpMethod.Get,"/api/admin/account-restrictions",null);
            using var response=await http.SendAsync(request);response.EnsureSuccessStatusCode();
            using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync());history.Rows.Clear();
            foreach(var row in doc.RootElement.GetProperty("restrictions").EnumerateArray())
            {
                DateTime? expires=row.GetProperty("expiresAtUtc").ValueKind==JsonValueKind.Null?null:row.GetProperty("expiresAtUtc").GetDateTime();
                DateTime? lifted=row.GetProperty("liftedAtUtc").ValueKind==JsonValueKind.Null?null:row.GetProperty("liftedAtUtc").GetDateTime();
                history.Rows.Add(row.GetProperty("accountId").GetString(),row.GetProperty("reason").GetString(),
                    row.GetProperty("createdAtUtc").GetDateTime().ToLocalTime().ToString("g"),expires?.ToLocalTime().ToString("g")??"영구",lifted?.ToLocalTime().ToString("g")??"",
                    lifted is not null?"해제":expires<=DateTime.UtcNow?"기간 만료":"제한 중");
            }
            status.Text="최근 500건 · 시각은 이 PC의 로컬 시각 · 해제해도 기존 토큰은 복구하지 않습니다.";
        }
        void Add(string text,Func<Task> action)
        {
            var button=new Button{Text=text,AutoSize=true};buttons.Controls.Add(button);
            button.Click+=async(_,_)=>{button.Enabled=false;try{await action();}catch(Exception ex){status.Text=ex.Message;}finally{button.Enabled=true;}};
        }
        Add("새로고침",LoadAsync);
        async Task ModifyAsync(bool lift)
        {
            if(string.IsNullOrWhiteSpace(account.Text)||(!lift&&string.IsNullOrWhiteSpace(reason.Text)))throw new InvalidOperationException("계정과 사유를 입력하세요.");
            if(MessageBox.Show(lift?$"{account.Text} 계정의 이용 제한을 해제할까요?":$"{account.Text} 계정을 {(permanent.Checked?"영구":minutes.Value+"분")} 제한하고 로그인 세션을 종료할까요?", "계정 제한 확인",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes)return;
            using var request=create(lift?HttpMethod.Delete:HttpMethod.Post,$"/api/admin/accounts/{Uri.EscapeDataString(account.Text.Trim())}/restriction",
                lift?null:new {Reason=reason.Text.Trim(),DurationMinutes=permanent.Checked?(int?)null:(int)minutes.Value});
            using var response=await http.SendAsync(request);
            string body=await response.Content.ReadAsStringAsync();
            if(!response.IsSuccessStatusCode)throw new InvalidOperationException(body);
            await LoadAsync();
        }
        Add("이용 제한 적용",()=>ModifyAsync(false));Add("선택 계정 제한 해제",()=>ModifyAsync(true));
        Shown+=async(_,_)=>{try{await LoadAsync();}catch(Exception ex){status.Text="조회 실패: "+ex.Message;}};
        Controls.Add(history);Controls.Add(buttons);Controls.Add(inputs);Controls.Add(status);
        FormClosed+=(_,_)=>http.Dispose();
    }
}

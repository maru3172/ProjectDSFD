using System.Text.Json;
namespace ProjectProject01BuildServerHelper;
internal sealed class MissionBudgetForm:Form
{
    public MissionBudgetForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        Text="주간 미션 재화 상한";Width=420;Height=230;StartPosition=FormStartPosition.CenterParent;
        var root=new FlowLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(16),FlowDirection=FlowDirection.TopDown};
        var amount=new NumericUpDown{Minimum=0,Maximum=1000000,Value=2000,Width=180};var status=new Label{AutoSize=true,MaximumSize=new Size(360,0)};var save=new Button{Text="적용",AutoSize=true,Enabled=false};
        root.Controls.AddRange([new Label{Text="PvP·봇전 합산 주간 상한",AutoSize=true},amount,save,status]);Controls.Add(root);
        var http=new HttpClient{Timeout=TimeSpan.FromSeconds(8)};
        async Task Request(bool update)
        {
            save.Enabled=false;
            try{using var req=create(update?HttpMethod.Put:HttpMethod.Get,"/api/admin/case-records/mission-budget",update?new{weeklyLimit=(int)amount.Value}:null);using var response=await http.SendAsync(req);response.EnsureSuccessStatusCode();using var json=JsonDocument.Parse(await response.Content.ReadAsStringAsync());if(IsDisposed)return;amount.Value=json.RootElement.GetProperty("weeklyLimit").GetInt32();save.Enabled=true;status.Text=update?"적용했습니다.":"";}
            catch(Exception ex){if(!IsDisposed)status.Text="요청 실패: "+ex.Message;}
        }
        Shown+=async(_,_)=>await Request(false);save.Click+=async(_,_)=>await Request(true);FormClosed+=(_,_)=>http.Dispose();
    }
}

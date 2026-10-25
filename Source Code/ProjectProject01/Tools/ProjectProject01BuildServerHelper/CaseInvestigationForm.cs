using System.Text.Json;
namespace ProjectProject01BuildServerHelper;

internal sealed class CaseInvestigationForm:Form
{
    private readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(8)};
    public CaseInvestigationForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        Text="사건 기록 조사 · 수집 수량 / 시즌 종료";Width=740;Height=420;StartPosition=FormStartPosition.CenterParent;
        var panel=new FlowLayoutPanel{Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown,WrapContents=false,Padding=new Padding(16)};
        panel.Controls.Add(new Label{Text="13파트 × 9조각 = 117조각 · 파트 완성 후 다음 파트 해금",AutoSize=true});
        panel.Controls.Add(new Label{Text="하루 수집 제한 없음 · 지난주 미사용 수량 이월 없음\n시즌 말 수량 증가는 운영자가 직접 적용합니다. 수집 기록은 초기화하지 않습니다.",AutoSize=true,Margin=new Padding(0,10,0,14)});
        var amount=new NumericUpDown{Minimum=15,Maximum=117,Value=15,Width=150};panel.Controls.Add(amount);
        var useEnd=new CheckBox{Text="시즌 종료 시각 지정 (한국 시간)",AutoSize=true,Margin=new Padding(0,12,0,4)};panel.Controls.Add(useEnd);
        var end=new DateTimePicker{Format=DateTimePickerFormat.Custom,CustomFormat="yyyy-MM-dd HH:mm:ss",Width=230,Enabled=false,Value=DateTime.Now.AddMonths(3)};panel.Controls.Add(end);
        useEnd.CheckedChanged+=(_,_)=>end.Enabled=useEnd.Checked;
        var buttons=new FlowLayoutPanel{AutoSize=true};var refresh=new Button{Text="설정 조회",AutoSize=true};var apply=new Button{Text="설정 적용",AutoSize=true};buttons.Controls.AddRange([refresh,apply]);panel.Controls.Add(buttons);
        var catalogue=new Button{Text="117조각 문구·위치 연결 구성",AutoSize=true};buttons.Controls.Add(catalogue);catalogue.Click+=(_,_)=>{using var form=new CaseInvestigationCatalogueForm(create);form.ShowDialog(this);};
        var stories=new Button{Text="파트별 스토리 작성",AutoSize=true};buttons.Controls.Add(stories);stories.Click+=(_,_)=>{using var form=new CasePartStoriesForm(create);form.ShowDialog(this);};
        var status=new Label{AutoSize=true,MaximumSize=new Size(620,0)};panel.Controls.Add(status);
        async Task Request(bool update)
        {
            refresh.Enabled=apply.Enabled=false;
            try
            {
                DateTimeOffset? endsAt=useEnd.Checked?new DateTimeOffset(DateTime.SpecifyKind(end.Value,DateTimeKind.Unspecified),TimeSpan.FromHours(9)).ToUniversalTime():null;
                if(update&&endsAt<=DateTimeOffset.UtcNow&&MessageBox.Show("지정한 시즌 종료 시각이 지났습니다. 이야기를 모두에게 공개하여 영구 보관할까요? 공개 후 다시 비공개로 되돌릴 수 없습니다.","시즌 종료 확인",MessageBoxButtons.YesNo)!=DialogResult.Yes)return;
                using var request=create(update?HttpMethod.Put:HttpMethod.Get,"/api/admin/case-records/settings",update?new{weeklyLimit=(int)amount.Value,endsAtUtc=endsAt,updateSeasonEnd=true}:null);
                using var response=await http.SendAsync(request);string text=await response.Content.ReadAsStringAsync();
                if(!response.IsSuccessStatusCode)throw new InvalidOperationException(text);
                using var json=JsonDocument.Parse(text);amount.Value=json.RootElement.GetProperty("weeklyLimit").GetInt32();
                useEnd.Checked=json.RootElement.TryGetProperty("endsAtUtc",out var endValue)&&endValue.ValueKind==JsonValueKind.String;
                if(useEnd.Checked)end.Value=endValue.GetDateTimeOffset().ToOffset(TimeSpan.FromHours(9)).DateTime;
                status.Text=update?"설정을 적용했습니다. 기존 조각·보상·수집량은 유지됩니다.":"시즌 중에는 파트 보상을 수령한 이야기만 개인 열람, 종료 후에는 전체 이야기를 모두에게 공개·보관합니다.";
            }
            catch(Exception ex){status.Text="요청 실패: "+ex.Message;}
            finally{if(!IsDisposed)refresh.Enabled=apply.Enabled=true;}
        }
        refresh.Click+=async(_,_)=>await Request(false);apply.Click+=async(_,_)=>await Request(true);Shown+=async(_,_)=>await Request(false);
        Controls.Add(panel);FormClosed+=(_,_)=>http.Dispose();
    }
}

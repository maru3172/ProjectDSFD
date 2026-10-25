using System.Net.Http.Json;
using System.Text.Json;
namespace ProjectProject01BuildServerHelper;

internal sealed class CaseInvestigationCatalogueForm:Form
{
    private readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(8)};
    private sealed record Entry(string Id,string Title,string Description,string Map,string Region,int Stage,int Part,int Points);
    public CaseInvestigationCatalogueForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        Text="사건 기록 117조각 구성 · 위치는 맵의 조사 구역과 연결";Width=1250;Height=750;StartPosition=FormStartPosition.CenterParent;
        var grid=new DataGridView{Dock=DockStyle.Fill,AllowUserToAddRows=false,AllowUserToDeleteRows=false,AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill,RowHeadersVisible=false};
        foreach(string name in new[]{"ID","파트","조각","제목","단어 단서","맵 패키지","구역 ID","포인트"})grid.Columns.Add(name,name);
        grid.Columns[1].FillWeight=30;grid.Columns[2].FillWeight=30;grid.Columns[7].FillWeight=40;grid.Columns[4].FillWeight=180;grid.Columns[5].FillWeight=200;
        var status=new Label{Dock=DockStyle.Bottom,Height=48,Text="단어 단서는 해당 조각을 발견하면 공개됩니다. 전체 이야기는 ‘파트별 스토리 작성’에서 별도로 작성합니다."};
        var bar=new FlowLayoutPanel{Dock=DockStyle.Top,AutoSize=true};
        void Fill(Entry[] rows){grid.Rows.Clear();foreach(var r in rows)grid.Rows.Add(r.Id,r.Stage,r.Part,r.Title,r.Description,r.Map,r.Region,r.Points);}
        async Task Load()
        {
            using var req=create(HttpMethod.Get,"/api/admin/case-records/catalogue",null);using var response=await http.SendAsync(req);response.EnsureSuccessStatusCode();
            using var json=JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            var records=json.RootElement.GetProperty("records").Deserialize<Entry[]>(new JsonSerializerOptions{PropertyNameCaseInsensitive=true})??[];
            Fill(records);status.Text=records.Length==0?"미게시 상태입니다. 117칸 생성 후 문구·맵·구역 ID를 편집하고 저장하세요.":"서버에 저장된 구성입니다. 이미 발견된 조각은 ID·파트·위치·포인트를 변경할 수 없습니다.";
        }
        void Add(string text,Func<Task> action)
        {
            var button=new Button{Text=text,AutoSize=true};bar.Controls.Add(button);
            button.Click+=async(_,_)=>{button.Enabled=false;try{await action();}catch(Exception ex){if(!IsDisposed)status.Text="실패: "+ex.Message;}finally{if(!IsDisposed)button.Enabled=true;}};
        }
        Add("서버 구성 조회",Load);
        Add("파트별 스토리 작성",()=>{using var form=new CasePartStoriesForm(create);form.ShowDialog(this);return Task.CompletedTask;});
        Add("빈 117칸 생성",()=>
        {
            if(grid.Rows.Count>0&&MessageBox.Show("편집 중인 내용을 기본 양식으로 바꿀까요? 서버에는 저장하지 않습니다.","편집 내용 확인",MessageBoxButtons.YesNo)!=DialogResult.Yes)return Task.CompletedTask;
            Fill(Enumerable.Range(0,117).Select(i=>new Entry($"r0_p{i/9+1:D2}_f{i%9+1:D2}",$"{i/9+1}파트 {i%9+1}조각","", "/Game/MyProject/Level/MultiplayTest",$"p{i/9+1:D2}_f{i%9+1:D2}",i/9+1,i%9+1,30)).ToArray());return Task.CompletedTask;
        });
        Add("구성 저장·게시",async()=>
        {
            grid.EndEdit();var rows=grid.Rows.Cast<DataGridViewRow>().Select(r=>new Entry(Convert.ToString(r.Cells[0].Value)??"",Convert.ToString(r.Cells[3].Value)??"",Convert.ToString(r.Cells[4].Value)??"",Convert.ToString(r.Cells[5].Value)??"",Convert.ToString(r.Cells[6].Value)??"",Convert.ToInt32(r.Cells[1].Value),Convert.ToInt32(r.Cells[2].Value),Convert.ToInt32(r.Cells[7].Value))).ToArray();
            using var req=create(HttpMethod.Put,"/api/admin/case-records/catalogue",rows);using var response=await http.SendAsync(req);string body=await response.Content.ReadAsStringAsync();
            if(!response.IsSuccessStatusCode)throw new InvalidOperationException(body);status.Text="117조각 구성 저장 완료. 기존 발견·수령 기록은 유지됩니다.";
        });
        Controls.Add(grid);Controls.Add(bar);Controls.Add(status);Shown+=async(_,_)=>{try{await Load();}catch(Exception ex){if(!IsDisposed)status.Text="조회 실패: "+ex.Message;}};
        FormClosed+=(_,_)=>http.Dispose();
    }
}

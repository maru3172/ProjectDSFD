using System.Text.Json;
namespace ProjectProject01BuildServerHelper;

internal sealed class MainQuestAdministrationForm:Form
{
    private sealed record Goal(string Id,string Metric,string Label,int Target=1);
    private sealed record Step(string Id,string Title,Goal[] Goals,string Story="");
    private sealed record Plan(string Name,string BaseStory,Step[] Steps);
    private readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(8)};
    public MainQuestAdministrationForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        Text="메인 퀘스트 · 탐사";Width=1050;Height=780;MinimumSize=new Size(800,600);StartPosition=FormStartPosition.CenterParent;
        var json=new JsonSerializerOptions(JsonSerializerDefaults.Web);Plan? plan=null;long revision=0;int current=-1;
        var root=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Padding=new Padding(12)};root.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,210));root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));Controls.Add(root);
        var list=new ListBox{Dock=DockStyle.Fill};root.Controls.Add(list,0,0);
        var editor=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=6,Padding=new Padding(12,0,0,0)};editor.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));root.Controls.Add(editor,1,0);
        foreach(var height in new[]{24f,34f,200f})editor.RowStyles.Add(new RowStyle(SizeType.Absolute,height));editor.RowStyles.Add(new RowStyle(SizeType.Percent,100));editor.RowStyles.Add(new RowStyle(SizeType.AutoSize));editor.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        editor.Controls.Add(new Label{Text="제목 / 단계 완료 후 공개할 이야기",AutoSize=true},0,0);
        var title=new TextBox{Dock=DockStyle.Fill,MaxLength=180};editor.Controls.Add(title,0,1);
        var goals=new DataGridView{Dock=DockStyle.Fill,AutoGenerateColumns=false,AllowUserToAddRows=true,AllowUserToDeleteRows=true,AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill,RowHeadersVisible=false};editor.Controls.Add(goals,0,2);
        goals.Columns.Add(new DataGridViewTextBoxColumn{Name="id",Visible=false});
        var metrics=new[]{new{Id="SurvivorTutorial",Name="생존자 튜토리얼"},new{Id="MannequinTutorial",Name="마네킹 튜토리얼"},new{Id="Match",Name="경기 플레이"},new{Id="ThirdFloor",Name="3층 탈출"},new{Id="SecondFloor",Name="2층 탈출"},new{Id="Escape",Name="최종 탈출"},new{Id="Capture",Name="마네킹 포획"},new{Id="MannequinWin",Name="마네킹 승리"}};
        goals.Columns.Add(new DataGridViewComboBoxColumn{Name="metric",HeaderText="조건",DataSource=metrics,DisplayMember="Name",ValueMember="Id"});
        goals.Columns.Add(new DataGridViewTextBoxColumn{Name="label",HeaderText="표시할 조건명",FillWeight=150});goals.Columns.Add(new DataGridViewTextBoxColumn{Name="target",HeaderText="필요 횟수",FillWeight=55});
        var story=new TextBox{Dock=DockStyle.Fill,Multiline=true,AcceptsReturn=true,ScrollBars=ScrollBars.Vertical,MaxLength=16000};editor.Controls.Add(story,0,3);
        var status=new Label{AutoSize=true,MaximumSize=new Size(760,0)};editor.Controls.Add(status,0,4);
        var actions=new FlowLayoutPanel{Dock=DockStyle.Fill,AutoSize=true};var save=new Button{Text="저장",AutoSize=true,Enabled=false};var reload=new Button{Text="다시 불러오기",AutoSize=true};var import=new Button{Text="스토리 CSV 불러오기",AutoSize=true,Enabled=false};var export=new Button{Text="스토리 CSV 내보내기",AutoSize=true,Enabled=false};var close=new Button{Text="닫기",AutoSize=true};actions.Controls.AddRange([save,reload,import,export,close]);editor.Controls.Add(actions,0,5);
        void Capture()
        {
            if(plan is null||current<0)return;goals.EndEdit();
            if(current==0){plan=plan with{Name=title.Text,BaseStory=story.Text};return;}
            var values=goals.Rows.Cast<DataGridViewRow>().Where(r=>!r.IsNewRow).Select(r=>new Goal(Convert.ToString(r.Cells["id"].Value) is {Length:>0} id?id:"g"+Guid.NewGuid().ToString("N")[..8],Convert.ToString(r.Cells["metric"].Value)??"Match",Convert.ToString(r.Cells["label"].Value)??"",int.TryParse(Convert.ToString(r.Cells["target"].Value),out var n)?n:1)).ToArray();
            plan.Steps[current-1]=plan.Steps[current-1] with{Title=title.Text,Story=story.Text,Goals=values};
        }
        void Display(int index)
        {
            if(plan is null)return;current=index;goals.Rows.Clear();goals.Enabled=index>0;
            title.Text=index==0?plan.Name:plan.Steps[index-1].Title;story.Text=index==0?plan.BaseStory:plan.Steps[index-1].Story;
            if(index>0)foreach(var g in plan.Steps[index-1].Goals)goals.Rows.Add(g.Id,g.Metric,g.Label,g.Target);
        }
        async Task Load()
        {
            save.Enabled=reload.Enabled=list.Enabled=import.Enabled=export.Enabled=false;
            try{using var req=create(HttpMethod.Get,"/api/admin/main-quest",null);using var response=await http.SendAsync(req);response.EnsureSuccessStatusCode();using var value=JsonDocument.Parse(await response.Content.ReadAsStringAsync());var loaded=value.RootElement.GetProperty("plan").Deserialize<Plan>(json);if(loaded is null||loaded.Steps.Length!=8)throw new InvalidOperationException("단계 구성을 확인하세요.");if(IsDisposed)return;current=-1;plan=loaded;revision=value.RootElement.GetProperty("revision").GetInt64();list.Items.Clear();list.Items.Add("기본 이야기");for(int i=0;i<8;i++)list.Items.Add($"{i+1}. {plan.Steps[i].Title}");list.SelectedIndex=0;status.Text="";save.Enabled=true;}
            catch(Exception ex){if(!IsDisposed)status.Text="조회 실패: "+ex.Message;}finally{if(!IsDisposed){reload.Enabled=list.Enabled=true;import.Enabled=export.Enabled=plan is not null;}}
        }
        list.SelectedIndexChanged+=(_,_)=>{Capture();if(list.SelectedIndex>=0)Display(list.SelectedIndex);};
        save.Click+=async(_,_)=>{Capture();if(plan is null)return;save.Enabled=false;try{using var req=create(HttpMethod.Put,"/api/admin/main-quest",new{revision,plan});using var response=await http.SendAsync(req);var body=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new InvalidOperationException(body);revision++;if(!IsDisposed)status.Text="저장했습니다.";}catch(Exception ex){if(!IsDisposed)status.Text="저장 실패: "+ex.Message;}finally{if(!IsDisposed)save.Enabled=true;}};
        import.Click+=(_,_)=>
        {
            if(plan is null)return;using var dialog=new OpenFileDialog{Filter="스토리 CSV (*.csv)|*.csv",Title="메인 퀘스트 스토리 불러오기"};if(dialog.ShowDialog(this)!=DialogResult.OK)return;
            try{var stories=MainQuestStoryCsv.ReadFile(dialog.FileName);Capture();plan=plan with{BaseStory=stories["base"],Steps=plan.Steps.Select(s=>s with{Story=stories[s.Id]}).ToArray()};Display(Math.Max(0,current));status.Text="CSV를 불러왔습니다. 저장하면 적용됩니다.";}
            catch(Exception ex){status.Text="CSV 불러오기 실패: "+ex.Message;}
        };
        export.Click+=(_,_)=>
        {
            if(plan is null)return;using var dialog=new SaveFileDialog{Filter="스토리 CSV (*.csv)|*.csv",FileName="MainQuestStories.csv",Title="메인 퀘스트 스토리 내보내기"};if(dialog.ShowDialog(this)!=DialogResult.OK)return;
            try{Capture();var stories=plan.Steps.ToDictionary(s=>s.Id,s=>s.Story);stories.Add("base",plan.BaseStory);using var writer=new StreamWriter(dialog.FileName,false,new System.Text.UTF8Encoding(true));MainQuestStoryCsv.Write(writer,stories);status.Text="CSV를 내보냈습니다.";}
            catch(Exception ex){status.Text="CSV 내보내기 실패: "+ex.Message;}
        };
        reload.Click+=async(_,_)=>await Load();close.Click+=(_,_)=>Close();Shown+=async(_,_)=>await Load();FormClosed+=(_,_)=>http.Dispose();
    }
}

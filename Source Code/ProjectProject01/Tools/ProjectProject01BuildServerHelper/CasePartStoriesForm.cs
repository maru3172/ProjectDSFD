using System.Net.Http.Json;
using System.Text.Json;
namespace ProjectProject01BuildServerHelper;

internal sealed class CasePartStoriesForm:Form
{
    private sealed record Entry(int Stage,string Title,string Story);
    private readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(8)};
    public CasePartStoriesForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        Text="파트별 스토리 작성";Width=1050;Height=740;MinimumSize=new Size(740,500);StartPosition=FormStartPosition.CenterParent;
        var entries=Enumerable.Range(1,13).Select(s=>new Entry(s,$"사건 기록 {s}파트","")).ToArray();int current=-1;
        var root=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Padding=new Padding(12)};root.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,140));root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        var parts=new ListBox{Name="StoryParts",Dock=DockStyle.Fill};foreach(var e in entries)parts.Items.Add($"{e.Stage}파트");root.Controls.Add(parts,0,0);
        var editor=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=5,Padding=new Padding(12,0,0,0)};editor.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        editor.RowStyles.Add(new RowStyle(SizeType.AutoSize));editor.RowStyles.Add(new RowStyle(SizeType.AutoSize));editor.RowStyles.Add(new RowStyle(SizeType.Percent,100));editor.RowStyles.Add(new RowStyle(SizeType.AutoSize));editor.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var title=new TextBox{Name="PartStoryTitle",Dock=DockStyle.Fill,MaxLength=100};var story=new TextBox{Name="PartStoryText",Dock=DockStyle.Fill,Multiline=true,AcceptsReturn=true,ScrollBars=ScrollBars.Vertical,MaxLength=20000};
        editor.Controls.Add(new Label{Text="파트 제목",AutoSize=true},0,0);editor.Controls.Add(title,0,1);editor.Controls.Add(story,0,2);
        var status=new Label{AutoSize=true,MaximumSize=new Size(800,0)};editor.Controls.Add(status,0,3);var buttons=new FlowLayoutPanel{AutoSize=true,Dock=DockStyle.Fill};editor.Controls.Add(buttons,0,4);
        var save=new Button{Text="스토리 저장·게시",AutoSize=true,Enabled=false};var close=new Button{Text="닫기",AutoSize=true};buttons.Controls.AddRange([save,close]);root.Controls.Add(editor,1,0);Controls.Add(root);
        void Capture(){if(current>=0)entries[current]=new Entry(current+1,title.Text,story.Text);}
        void Display(int index){current=index;title.Text=entries[index].Title;story.Text=entries[index].Story;}
        parts.SelectedIndexChanged+=(_,_)=>{Capture();if(parts.SelectedIndex>=0)Display(parts.SelectedIndex);};parts.SelectedIndex=0;close.Click+=(_,_)=>Close();
        save.Click+=async(_,_)=>
        {
            Capture();save.Enabled=false;
            try{using var req=create(HttpMethod.Put,"/api/admin/case-records/stories",entries);using var response=await http.SendAsync(req);var text=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new InvalidOperationException(text);status.Text="파트별 스토리 저장 완료";}
            catch(Exception ex){if(!IsDisposed)status.Text="저장 실패: "+ex.Message;}finally{if(!IsDisposed)save.Enabled=true;}
        };
        Shown+=async(_,_)=>
        {
            save.Enabled=parts.Enabled=false;title.ReadOnly=story.ReadOnly=true;bool ready=false;
            try{using var req=create(HttpMethod.Get,"/api/admin/case-records/stories",null);using var response=await http.SendAsync(req);response.EnsureSuccessStatusCode();using var json=JsonDocument.Parse(await response.Content.ReadAsStringAsync());var loaded=json.RootElement.GetProperty("stories").Deserialize<Entry[]>(new JsonSerializerOptions{PropertyNameCaseInsensitive=true});if(loaded is null||loaded.Length!=13||loaded.Select(e=>e.Stage).Distinct().Count()!=13||loaded.Any(e=>e.Stage is <1 or >13))throw new InvalidOperationException("스토리 구성을 확인하세요.");if(IsDisposed)return;entries=loaded.OrderBy(e=>e.Stage).ToArray();Display(parts.SelectedIndex);ready=true;}
            catch(Exception ex){if(!IsDisposed)status.Text="조회 실패: "+ex.Message;}finally{if(!IsDisposed){save.Enabled=parts.Enabled=ready;title.ReadOnly=story.ReadOnly=!ready;}}
        };
        FormClosed+=(_,_)=>http.Dispose();
    }
}

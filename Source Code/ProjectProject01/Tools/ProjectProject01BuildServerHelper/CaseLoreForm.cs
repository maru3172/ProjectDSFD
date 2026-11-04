using System.Text.Json;
namespace ProjectProject01BuildServerHelper;

// 기본 기록 (lore) writer: entries about the player and the world around them, each opened in game by a condition.
internal sealed class CaseLoreForm:Form
{
    private sealed record Entry(string Id,string Category,string Title,string Body,string Unlock,string Value);
    private static readonly (string Id,string Name,string Hint)[] Unlocks=[("always","처음부터",""),("achievement","업적 달성","업적 코드 (예: pvp_first)"),("level","계정 레벨","레벨 1~100"),("part","조사 파트 완성","파트 1~13 (파트 보상 수령 또는 시즌 종료 시)")];
    private readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(8)};
    public CaseLoreForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        Text="기본 기록 작성 · 플레이어와 그 밖의 기록";Width=1100;Height=760;MinimumSize=new Size(820,540);StartPosition=FormStartPosition.CenterParent;
        var entries=new List<Entry>();int current=-1;bool loading=false;
        var root=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Padding=new Padding(12)};root.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,300));root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        var left=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=2};left.RowStyles.Add(new RowStyle(SizeType.Percent,100));left.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var list=new ListBox{Name="LoreEntries",Dock=DockStyle.Fill,IntegralHeight=false};left.Controls.Add(list,0,0);
        var listButtons=new FlowLayoutPanel{AutoSize=true,Dock=DockStyle.Fill};var add=new Button{Text="추가",AutoSize=true};var remove=new Button{Text="빼기",AutoSize=true};var up=new Button{Text="위로",AutoSize=true};var down=new Button{Text="아래로",AutoSize=true};
        listButtons.Controls.AddRange([add,remove,up,down]);left.Controls.Add(listButtons,0,1);root.Controls.Add(left,0,0);
        var editor=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=7,Padding=new Padding(12,0,0,0)};editor.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,90));editor.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        for(int i=0;i<7;i++)editor.RowStyles.Add(i==4?new RowStyle(SizeType.Percent,100):new RowStyle(SizeType.AutoSize));
        var id=new TextBox{Name="LoreId",Dock=DockStyle.Fill,MaxLength=48};
        var category=new ComboBox{Name="LoreCategory",Dock=DockStyle.Fill,DropDownStyle=ComboBoxStyle.DropDown,MaxLength=20};category.Items.AddRange(["플레이어","그 밖의 기록"]);
        var title=new TextBox{Name="LoreTitle",Dock=DockStyle.Fill,MaxLength=100};
        var conditionRow=new FlowLayoutPanel{AutoSize=true,Dock=DockStyle.Fill,WrapContents=false};
        var unlock=new ComboBox{Name="LoreUnlock",DropDownStyle=ComboBoxStyle.DropDownList,Width=150};foreach(var u in Unlocks)unlock.Items.Add(u.Name);
        var value=new TextBox{Name="LoreUnlockValue",Width=180,MaxLength=48};var valueHint=new Label{AutoSize=true,Margin=new Padding(8,6,0,0)};conditionRow.Controls.AddRange([unlock,value,valueHint]);
        var body=new TextBox{Name="LoreBody",Dock=DockStyle.Fill,Multiline=true,AcceptsReturn=true,ScrollBars=ScrollBars.Vertical,MaxLength=20000};
        void Row(int row,string label,Control control){editor.Controls.Add(new Label{Text=label,AutoSize=true,Margin=new Padding(0,6,0,0)},0,row);editor.Controls.Add(control,1,row);}
        Row(0,"기록 ID",id);Row(1,"분류",category);Row(2,"제목",title);Row(3,"열림 조건",conditionRow);Row(4,"본문",body);
        var status=new Label{AutoSize=true,MaximumSize=new Size(760,0)};editor.Controls.Add(status,1,5);
        var buttons=new FlowLayoutPanel{AutoSize=true,Dock=DockStyle.Fill};var save=new Button{Text="기본 기록 저장·게시",AutoSize=true,Enabled=false};var close=new Button{Text="닫기",AutoSize=true};buttons.Controls.AddRange([save,close]);editor.Controls.Add(buttons,1,6);
        root.Controls.Add(editor,1,0);Controls.Add(root);
        string Caption(Entry e)=>$"[{e.Category}] {e.Title}";
        void ShowHint(){int u=Math.Max(0,unlock.SelectedIndex);value.Enabled=u>0;valueHint.Text=Unlocks[u].Hint;}
        void Capture()
        {
            if(current<0||current>=entries.Count||loading)return;
            entries[current]=new Entry(id.Text.Trim(),category.Text.Trim(),title.Text,body.Text,Unlocks[Math.Max(0,unlock.SelectedIndex)].Id,value.Enabled?value.Text.Trim():"");
            // Renaming the row re-fires the list's selection event; keep it from capturing again.
            string caption=Caption(entries[current]);if(!Equals(list.Items[current],caption)){loading=true;try{list.Items[current]=caption;}finally{loading=false;}}
        }
        void Display(int index)
        {
            current=index;bool has=index>=0&&index<entries.Count;loading=true;
            foreach(Control c in new Control[]{id,category,title,unlock,value,body})c.Enabled=has;
            var e=has?entries[index]:new Entry("","","","","always","");
            id.Text=e.Id;category.Text=e.Category;title.Text=e.Title;body.Text=e.Body;value.Text=e.Value;unlock.SelectedIndex=Math.Max(0,Array.FindIndex(Unlocks,u=>u.Id==e.Unlock));
            loading=false;ShowHint();if(!has)value.Enabled=false;
        }
        void Reload(int select){list.Items.Clear();foreach(var e in entries)list.Items.Add(Caption(e));list.SelectedIndex=Math.Min(select,entries.Count-1);if(entries.Count==0)Display(-1);}
        list.SelectedIndexChanged+=(_,_)=>{if(loading)return;Capture();if(list.SelectedIndex>=0)Display(list.SelectedIndex);};
        foreach(Control c in new Control[]{id,category,title,value,body})c.TextChanged+=(_,_)=>Capture();
        unlock.SelectedIndexChanged+=(_,_)=>{ShowHint();Capture();};
        add.Click+=(_,_)=>
        {
            Capture();int n=entries.Count+1;string next;do next=$"lore{n++:D3}";while(entries.Any(e=>e.Id==next));
            entries.Add(new Entry(next,list.SelectedIndex>=0?entries[list.SelectedIndex].Category:"플레이어","새 기록","","always",""));Reload(entries.Count-1);title.Focus();title.SelectAll();
        };
        remove.Click+=(_,_)=>{int i=list.SelectedIndex;if(i<0)return;current=-1;entries.RemoveAt(i);Reload(Math.Max(0,i-1));};
        void Move(int by){Capture();int i=list.SelectedIndex,j=i+by;if(i<0||j<0||j>=entries.Count)return;(entries[i],entries[j])=(entries[j],entries[i]);current=-1;Reload(j);}
        up.Click+=(_,_)=>Move(-1);down.Click+=(_,_)=>Move(1);close.Click+=(_,_)=>Close();
        save.Click+=async(_,_)=>
        {
            Capture();save.Enabled=false;
            try{using var req=create(HttpMethod.Put,"/api/admin/case-records/lore",entries);using var response=await http.SendAsync(req);var text=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new InvalidOperationException(text);status.Text=$"기본 기록 {entries.Count}개 저장 완료";}
            catch(Exception ex){if(!IsDisposed)status.Text="저장 실패: "+ex.Message;}finally{if(!IsDisposed)save.Enabled=true;}
        };
        Display(-1);
        Shown+=async(_,_)=>
        {
            // Saving stays off until the current entries load, so a failed read can never publish an empty list over them.
            save.Enabled=add.Enabled=false;bool ready=false;
            try
            {
                using var req=create(HttpMethod.Get,"/api/admin/case-records/lore",null);using var response=await http.SendAsync(req);response.EnsureSuccessStatusCode();
                using var json=JsonDocument.Parse(await response.Content.ReadAsStringAsync());
                var loaded=json.RootElement.GetProperty("entries").Deserialize<Entry[]>(new JsonSerializerOptions{PropertyNameCaseInsensitive=true})??[];
                if(IsDisposed)return;entries.Clear();entries.AddRange(loaded.Select(e=>e with{Value=e.Value??""}));Reload(0);ready=true;
            }
            catch(Exception ex){if(!IsDisposed)status.Text="조회 실패: "+ex.Message;}
            finally{if(!IsDisposed)save.Enabled=add.Enabled=ready;}
        };
        FormClosed+=(_,_)=>http.Dispose();
    }
}

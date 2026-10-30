using System.Text.Json;

namespace ProjectProject01BuildServerHelper;

internal sealed class CaseDrawAdministrationForm:Form
{
    private readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(8)};
    private static void SortPrizes(DataGridView grid)=>grid.Sort(grid.Columns["grade"],System.ComponentModel.ListSortDirection.Ascending);
    internal static int AddMissingPrizes(DataGridView grid,IEnumerable<JsonElement> defaults)
    {
        int added=0;
        foreach(var prize in defaults)
        {
            string id=prize.GetProperty("id").GetString()!,item=prize.GetProperty("itemId").GetString()??"";
            if(grid.Rows.Cast<DataGridViewRow>().Any(r=>!r.IsNewRow&&(Convert.ToString(r.Cells["id"].Value)==id||item.Length>0&&Convert.ToString(r.Cells["itemId"].Value)==item)))continue;
            if(grid.Rows.Cast<DataGridViewRow>().Count(r=>!r.IsNewRow)>=32)throw new InvalidOperationException("상품은 최대 32종입니다.");
            grid.Rows.Add(id,prize.GetProperty("name").GetString(),prize.GetProperty("grade").GetInt32(),prize.GetProperty("weight").GetInt32(),item,prize.GetProperty("credits").GetInt32());added++;
        }
        SortPrizes(grid);return added;
    }
    public CaseDrawAdministrationForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        Text="시즌 뽑기 · 상품 / 확률";AutoScaleMode=AutoScaleMode.Dpi;ClientSize=new Size(1050,650);MinimumSize=new Size(820,550);StartPosition=FormStartPosition.CenterParent;
        long revision=-1;var root=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=4,Padding=new Padding(14)};
        root.RowStyles.Add(new(SizeType.AutoSize));root.RowStyles.Add(new(SizeType.Percent,100));root.RowStyles.Add(new(SizeType.AutoSize));root.RowStyles.Add(new(SizeType.AutoSize));
        var header=new FlowLayoutPanel{AutoSize=true,Dock=DockStyle.Fill};
        var id=new TextBox{Width=120,PlaceholderText="시즌 ID"};var title=new TextBox{Width=180,PlaceholderText="제목"};
        var start=new DateTimePicker{Format=DateTimePickerFormat.Custom,CustomFormat="yyyy-MM-dd HH:mm",Width=165};var end=new DateTimePicker{Format=DateTimePickerFormat.Custom,CustomFormat="yyyy-MM-dd HH:mm",Width=165};
        var published=new CheckBox{Text="공개",AutoSize=true};header.Controls.AddRange([id,title,start,end,published]);root.Controls.Add(header,0,0);
        var grid=new DataGridView{Dock=DockStyle.Fill,AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill,AllowUserToAddRows=true,AllowUserToDeleteRows=true,RowHeadersVisible=false,BackgroundColor=SystemColors.Window};
        foreach(var pair in new[]{("id","상품 ID"),("name","상품명"),("grade","등급 (1~5)"),("weight","확률 가중치"),("itemId","꾸미기 ID"),("credits","포인트")})grid.Columns.Add(pair.Item1,pair.Item2);
        root.Controls.Add(grid,0,1);
        var slots=new Label{AutoSize=true,MaximumSize=new Size(990,0),Text="기본 상품 29종 · 재화 1개 = 500포인트 · 50회 1·2등급 각 50% · 100회 1등급 보장"};root.Controls.Add(slots,0,2);
        var footer=new FlowLayoutPanel{AutoSize=true,Dock=DockStyle.Fill};var refresh=new Button{Text="조회",AutoSize=true};var save=new Button{Text="적용",AutoSize=true,Enabled=false};var fill=new Button{Text="기본 29종 채우기",AutoSize=true,Enabled=false};var status=new Label{AutoSize=true,MaximumSize=new Size(560,0)};footer.Controls.AddRange([refresh,fill,save,status]);root.Controls.Add(footer,0,3);JsonElement[] defaults=[];
        fill.Click+=(_,_)=>{try{grid.EndEdit();int added=AddMissingPrizes(grid,defaults);status.Text=$"{added}종 추가 · 적용 전";}catch(Exception ex){status.Text=ex.Message;}};
        var debugPoints=new CheckBox{Text="에디터 디버그 허용",AutoSize=true,Enabled=false};footer.Controls.Add(debugPoints);bool updatingDebug=false;
        async Task DebugSettings(bool update)
        {
            debugPoints.Enabled=false;updatingDebug=true;
            try
            {
                using var request=create(update?HttpMethod.Put:HttpMethod.Get,"/api/admin/case-records/debug-points",update?new{enabled=debugPoints.Checked}:null);
                using var response=await http.SendAsync(request);string text=await response.Content.ReadAsStringAsync();if(!response.IsSuccessStatusCode)throw new InvalidOperationException(text);
                using var json=JsonDocument.Parse(text);debugPoints.Checked=json.RootElement.GetProperty("enabled").GetBoolean();
            }
            catch(Exception ex){status.Text="디버그 설정 실패: "+ex.Message;}
            finally{updatingDebug=false;if(!IsDisposed)debugPoints.Enabled=true;}
        }
        debugPoints.CheckedChanged+=async(_,_)=>{if(!updatingDebug)await DebugSettings(true);};Shown+=async(_,_)=>await DebugSettings(false);
        static string Cell(DataGridViewRow row,string key)=>Convert.ToString(row.Cells[key].Value)?.Trim()??"";
        async Task Load(bool update)
        {
            refresh.Enabled=save.Enabled=fill.Enabled=false;
            try
            {
                object? body=null;
                if(update)
                {
                    grid.EndEdit();var prizes=grid.Rows.Cast<DataGridViewRow>().Where(r=>!r.IsNewRow).Select(r=>new{id=Cell(r,"id"),name=Cell(r,"name"),grade=int.Parse(Cell(r,"grade")),weight=int.Parse(Cell(r,"weight")),itemId=string.IsNullOrWhiteSpace(Cell(r,"itemId"))?null:Cell(r,"itemId"),credits=int.Parse(Cell(r,"credits"))}).ToArray();
                    DateTimeOffset Utc(DateTime value)=>new DateTimeOffset(DateTime.SpecifyKind(value,DateTimeKind.Unspecified),TimeSpan.FromHours(9)).ToUniversalTime();
                    body=new{revision,pool=new{id=id.Text.Trim(),title=title.Text.Trim(),startsAtUtc=Utc(start.Value),endsAtUtc=Utc(end.Value),published=published.Checked,prizes}};
                }
                using var request=create(update?HttpMethod.Put:HttpMethod.Get,"/api/admin/case-records/draw",body);using var response=await http.SendAsync(request);string text=await response.Content.ReadAsStringAsync();
                if(!response.IsSuccessStatusCode)throw new InvalidOperationException(text);
                using var json=JsonDocument.Parse(text);var value=json.RootElement;revision=value.GetProperty("revision").GetInt64();var pool=value.GetProperty("pool");
                if(value.TryGetProperty("defaultPrizes",out var template))defaults=template.EnumerateArray().Select(p=>p.Clone()).ToArray();
                id.Text=pool.GetProperty("id").GetString();title.Text=pool.GetProperty("title").GetString();start.Value=pool.GetProperty("startsAtUtc").GetDateTimeOffset().ToOffset(TimeSpan.FromHours(9)).DateTime;end.Value=pool.GetProperty("endsAtUtc").GetDateTimeOffset().ToOffset(TimeSpan.FromHours(9)).DateTime;published.Checked=pool.GetProperty("published").GetBoolean();
                grid.Rows.Clear();foreach(var p in pool.GetProperty("prizes").EnumerateArray())grid.Rows.Add(p.GetProperty("id").GetString(),p.GetProperty("name").GetString(),p.GetProperty("grade").GetInt32(),p.GetProperty("weight").GetInt32(),p.GetProperty("itemId").ValueKind==JsonValueKind.String?p.GetProperty("itemId").GetString():"",p.GetProperty("credits").GetInt32());
                SortPrizes(grid);
                status.Text=update?"적용했습니다.":"조회했습니다.";
            }
            catch(Exception ex){status.Text="요청 실패: "+ex.Message;}
            finally{if(!IsDisposed){refresh.Enabled=true;save.Enabled=revision>=0;fill.Enabled=revision>=0&&defaults.Length>0;}}
        }
        refresh.Click+=async(_,_)=>await Load(false);save.Click+=async(_,_)=>await Load(true);Shown+=async(_,_)=>await Load(false);Controls.Add(root);FormClosed+=(_,_)=>http.Dispose();
    }
}

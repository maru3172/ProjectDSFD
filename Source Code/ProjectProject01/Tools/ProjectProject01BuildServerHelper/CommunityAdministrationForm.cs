using System.Text.Json;

namespace ProjectProject01BuildServerHelper;

internal sealed class CommunityAdministrationForm : Form
{
    private readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(8)};
    private readonly CancellationTokenSource closing=new();
    private readonly Label status=new(){Dock=DockStyle.Bottom,Height=36,Text="준비"};
    private readonly ListBox posts=new(){Dock=DockStyle.Left,Width=270,DisplayMember="Title"};
    private readonly ListBox inquiries=new(){Dock=DockStyle.Left,Width=270,DisplayMember="Title"};
    private readonly TextBox title=new(){Dock=DockStyle.Top,MaxLength=120,PlaceholderText="제목"};
    private readonly TextBox body=new(){Dock=DockStyle.Fill,Multiline=true,ScrollBars=ScrollBars.Vertical,MaxLength=16000};
    private readonly ComboBox kind=new(){DropDownStyle=ComboBoxStyle.DropDownList,Width=150};
    private readonly CheckBox published=new(){AutoSize=true,Text="런처에 공개",Checked=true};
    private readonly ComboBox previewImage=new(){DropDownStyle=ComboBoxStyle.DropDownList,Width=220,DisplayMember="Label"};
    private readonly PictureBox previewPhoto=new(){Width=96,Height=54,SizeMode=PictureBoxSizeMode.Zoom,Visible=false};
    private CancellationTokenSource? previewLoading;
    private readonly TextBox details=new(){Dock=DockStyle.Fill,Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical};
    private readonly TextBox reply=new(){Dock=DockStyle.Bottom,Height=130,Multiline=true,MaxLength=4000,ScrollBars=ScrollBars.Vertical};
    private readonly Func<HttpMethod,string,object?,HttpRequestMessage> create;
    private string? editingId;
    private bool pending;
    private sealed record Post(string Id,string Kind,string Title,string Body,bool Published,string? PreviewImageId);
    private sealed record PreviewOption(string? Id,string Label);
    private sealed record Inquiry(string Id,string Title,string Body,string Reply,string Status);

    public CommunityAdministrationForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        this.create=create;Text="런처 뉴스·패치노트 / 고객센터 문의";Width=1100;Height=720;MinimumSize=new Size(820,500);StartPosition=FormStartPosition.CenterParent;
        var tabs=new TabControl{Dock=DockStyle.Fill};var news=new TabPage("공지·패치노트");var support=new TabPage("접수 문의·답변");tabs.TabPages.AddRange([news,support]);
        var editor=new Panel{Dock=DockStyle.Fill,Padding=new Padding(8)};var toolbar=new FlowLayoutPanel{Dock=DockStyle.Top,AutoSize=true};
        kind.Items.AddRange(["뉴스 / 공지","패치노트"]);kind.SelectedIndex=0;toolbar.Controls.AddRange([kind,published]);
        Add(toolbar,"새 글",()=>{editingId=null;posts.ClearSelected();title.Clear();body.Clear();published.Checked=true;return Task.CompletedTask;});
        Add(toolbar,"사진 첨부",async()=>
        {
            using var picker=new OpenFileDialog {Filter="PNG / JPG 사진|*.png;*.jpg;*.jpeg",Title="공지에 첨부할 사진 선택"};
            if(picker.ShowDialog(this)!=DialogResult.OK)return;
            if(new FileInfo(picker.FileName).Length>5*1024*1024)throw new InvalidOperationException("사진은 최대 5MB입니다.");
            using var request=create(HttpMethod.Post,"/api/admin/community/images",null);
            request.Content=new ByteArrayContent(await File.ReadAllBytesAsync(picker.FileName,closing.Token));
            using var response=await http.SendAsync(request,closing.Token);
            string json=await response.Content.ReadAsStringAsync(closing.Token);
            if(!response.IsSuccessStatusCode)throw new InvalidOperationException(json);
            using var doc=JsonDocument.Parse(json);body.SelectedText=Environment.NewLine+doc.RootElement.GetProperty("markup").GetString()+Environment.NewLine;
        });
        Add(toolbar,"표 삽입",()=>{InsertTable();return Task.CompletedTask;});
        Add(toolbar,"본문 미리보기",()=>
        {
            using var preview=new Form {Text="게시글 본문 미리보기 — 클라이언트 표시",Width=800,Height=700,StartPosition=FormStartPosition.CenterParent};
            var viewer=new LauncherPostView(new AdminImageHandler(create)){Dock=DockStyle.Fill};preview.Controls.Add(viewer);
            var label=new Label {Dock=DockStyle.Bottom,Height=30};preview.Controls.Add(label);
            preview.Shown+=async(_,_)=>{try{await viewer.ShowPostAsync(body.Text,"http://127.0.0.1");}catch(OperationCanceledException){}catch(Exception ex){if(!preview.IsDisposed)label.Text=ex.Message;}};
            preview.ShowDialog(this);return Task.CompletedTask;
        });
        Add(toolbar,"저장",async()=>{await Send(editingId is null?HttpMethod.Post:HttpMethod.Put,"/api/admin/community/posts"+(editingId is null?"":"/"+editingId),new {kind=kind.SelectedIndex==1?"Patch":"News",title=title.Text,body=body.Text,published=published.Checked,previewImageId=(previewImage.SelectedItem as PreviewOption)?.Id});await LoadData();});
        Add(toolbar,"선택 글 삭제",async()=>
        {
            if(posts.SelectedItem is not Post selected)throw new InvalidOperationException("삭제할 글을 선택하세요.");
            if(MessageBox.Show(this,"선택한 글을 삭제할까요?","삭제 확인",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes)return;
            await Send(HttpMethod.Delete,"/api/admin/community/posts/"+selected.Id,null);editingId=null;title.Clear();body.Clear();await LoadData();
        });
        var instructions=new Label {Dock=DockStyle.Top,Height=36,Text="제목과 본문을 작성하세요. 사진 첨부·표 삽입은 본문에 표시 기호를 넣습니다.\n사진·표는 게시 후 런처에서 읽기 전용으로 보입니다. HTML·스크립트는 실행하지 않습니다."};
        var previewBar=new FlowLayoutPanel{Dock=DockStyle.Top,AutoSize=true};previewBar.Controls.AddRange([new Label {Text="목록 미리보기 사진",AutoSize=true},previewImage,previewPhoto]);
        editor.Controls.Add(body);editor.Controls.Add(instructions);editor.Controls.Add(previewBar);editor.Controls.Add(title);editor.Controls.Add(toolbar);news.Controls.Add(editor);news.Controls.Add(posts);
        body.TextChanged+=(_,_)=>RefreshPreviewOptions();previewImage.SelectedIndexChanged+=async(_,_)=>await RefreshPreviewPhoto();RefreshPreviewOptions();
        posts.SelectedIndexChanged+=(_,_)=>{if(posts.SelectedItem is Post p){editingId=p.Id;title.Text=p.Title;body.Text=p.Body;kind.SelectedIndex=p.Kind=="Patch"?1:0;published.Checked=p.Published;SelectPreview(p.PreviewImageId);}};
        var supportEditor=new Panel{Dock=DockStyle.Fill,Padding=new Padding(8)};
        var replyActions=new FlowLayoutPanel{Dock=DockStyle.Bottom,AutoSize=true};
        async Task Answer(string state)
        {
            if(inquiries.SelectedItem is not Inquiry q)throw new InvalidOperationException("문의 목록에서 항목을 선택하세요.");
            await Send(HttpMethod.Put,"/api/admin/community/inquiries/"+q.Id,new {reply=reply.Text,status=state});await LoadData();
        }
        Add(replyActions,"답변 저장",()=>Answer("Answered"));Add(replyActions,"답변 저장·처리 완료",()=>Answer("Closed"));
        supportEditor.Controls.Add(details);supportEditor.Controls.Add(reply);supportEditor.Controls.Add(replyActions);support.Controls.Add(supportEditor);support.Controls.Add(inquiries);
        inquiries.SelectedIndexChanged+=(_,_)=>{if(inquiries.SelectedItem is Inquiry q){details.Text="["+q.Status+"] "+q.Title+Environment.NewLine+q.Body;reply.Text=q.Reply;}};
        var refresh=new FlowLayoutPanel{Dock=DockStyle.Top,AutoSize=true};Add(refresh,"새로고침",LoadData);
        Controls.Add(tabs);Controls.Add(refresh);Controls.Add(status);
        Shown+=async(_,_)=>await Run(LoadData);
        FormClosed+=(_,_)=>{closing.Cancel();previewLoading?.Cancel();previewLoading?.Dispose();previewLoading=null;previewPhoto.Image?.Dispose();previewPhoto.Image=null;http.Dispose();closing.Dispose();};
    }
    private void Add(FlowLayoutPanel panel,string text,Func<Task> action)
    {
        var b=new Button{Text=text,AutoSize=true};panel.Controls.Add(b);b.Click+=async(_,_)=>await Run(action);
    }
    private void InsertTable()
    {
        using var editor=new Form {Text="본문에 표 삽입",Width=760,Height=460,StartPosition=FormStartPosition.CenterParent};
        var columns=new NumericUpDown {Minimum=1,Maximum=12,Value=2,Width=60};
        var rows=new NumericUpDown {Minimum=1,Maximum=100,Value=2,Width=60};
        var grid=new DataGridView {Dock=DockStyle.Fill,AllowUserToAddRows=false,AllowUserToDeleteRows=false,AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill};
        var bar=new FlowLayoutPanel {Dock=DockStyle.Top,AutoSize=true};
        bar.Controls.AddRange([new Label {Text="열 수",AutoSize=true},columns,new Label {Text="내용 행 수",AutoSize=true},rows]);
        var resize=new Button {Text="표 크기 적용",AutoSize=true};bar.Controls.Add(resize);
        var insert=new Button {Text="본문에 삽입",Dock=DockStyle.Bottom,Height=34};
        void ResizeTable()
        {
            grid.Columns.Clear();for(int n=0;n<(int)columns.Value;n++)grid.Columns.Add("col"+n,(n+1)+"열");
            grid.Rows.Add((int)rows.Value+1);for(int n=0;n<grid.Columns.Count;n++)grid.Rows[0].Cells[n].Value="항목 "+(n+1);
        }
        resize.Click+=(_,_)=>ResizeTable();ResizeTable();
        var help=new Label {Text="첫 번째 행은 제목입니다. 칸을 눌러 내용을 입력한 다음 삽입하세요. 크기를 다시 적용하면 내용을 초기화합니다.",AutoSize=true,Dock=DockStyle.Top};
        insert.Click+=(_,_)=>
        {
            grid.EndEdit();
            string Line(DataGridViewRow row)=>"| "+string.Join(" | ",row.Cells.Cast<DataGridViewCell>().Select(c=>(c.Value?.ToString()??"").Replace('|','／').Replace('\r',' ').Replace('\n',' ')))+" |\r\n";
            var result=new System.Text.StringBuilder("\r\n");result.Append(Line(grid.Rows[0]));result.Append("| "+string.Join(" | ",Enumerable.Repeat("---",grid.Columns.Count))+" |\r\n");
            foreach(DataGridViewRow row in grid.Rows.Cast<DataGridViewRow>().Skip(1))result.Append(Line(row));
            body.SelectedText=result.ToString();editor.Close();
        };
        editor.Controls.Add(grid);editor.Controls.Add(help);editor.Controls.Add(bar);editor.Controls.Add(insert);editor.ShowDialog(this);
    }
    private void RefreshPreviewOptions()
    {
        string? selected=(previewImage.SelectedItem as PreviewOption)?.Id;
        var options=new List<PreviewOption>{new(null,"미리보기 없음")};
        var ids=new HashSet<string>();
        foreach(System.Text.RegularExpressions.Match match in System.Text.RegularExpressions.Regex.Matches(body.Text,@"!\[([^\]\r\n]*)\]\(image:([0-9a-fA-F-]{36})\)"))
            if(Guid.TryParse(match.Groups[2].Value,out var id)&&ids.Add(id.ToString("D")))options.Add(new PreviewOption(id.ToString("D"),$"{ids.Count}번째 사진: {match.Groups[1].Value}"));
        if(previewImage.Items.Cast<PreviewOption>().SequenceEqual(options))return;
        previewImage.Items.Clear();previewImage.Items.AddRange(options.Cast<object>().ToArray());
        SelectPreview(selected);
    }
    private void SelectPreview(string? id)
    {
        previewImage.SelectedIndex=0;
        if(id is not null)foreach(PreviewOption option in previewImage.Items)if(option.Id==id){previewImage.SelectedItem=option;break;}
    }
    private async Task RefreshPreviewPhoto()
    {
        if(IsDisposed||Disposing||closing.IsCancellationRequested)return;
        previewLoading?.Cancel();previewLoading?.Dispose();previewLoading=null;
        previewPhoto.Image?.Dispose();previewPhoto.Image=null;previewPhoto.Visible=false;
        if((previewImage.SelectedItem as PreviewOption)?.Id is not string id)return;
        previewLoading=CancellationTokenSource.CreateLinkedTokenSource(closing.Token);var ct=previewLoading.Token;
        try
        {
            using var transport=new HttpClient(new AdminImageHandler(create)){Timeout=TimeSpan.FromSeconds(5)};
            Image image=await LauncherPostView.DownloadImageAsync(transport,"http://127.0.0.1/api/launcher/images/"+id,ct,192);
            if(ct.IsCancellationRequested||IsDisposed){image.Dispose();return;}
            previewPhoto.Image=image;previewPhoto.Visible=true;
        }
        catch(OperationCanceledException){}
        catch(Exception ex){if(!ct.IsCancellationRequested&&!IsDisposed)status.Text="미리보기 사진: "+ex.Message;}
    }
    private sealed class AdminImageHandler(Func<HttpMethod,string,object?,HttpRequestMessage> create):HttpMessageHandler
    {
        private readonly HttpClient transport=new(){Timeout=TimeSpan.FromSeconds(8)};
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage req,CancellationToken ct)
        {
            string id=req.RequestUri!.Segments.Last();if(!Guid.TryParse(id,out var parsed))throw new InvalidDataException("사진 주소가 올바르지 않습니다.");
            using var request=create(HttpMethod.Get,"/api/admin/community/images/"+parsed.ToString("D"),null);
            return await transport.SendAsync(request,HttpCompletionOption.ResponseHeadersRead,ct);
        }
        protected override void Dispose(bool disposing){if(disposing)transport.Dispose();base.Dispose(disposing);}
    }
    private async Task Run(Func<Task> action)
    {
        if(pending)return;pending=true;
        try{await action();if(!IsDisposed)status.Text="완료";}
        catch(OperationCanceledException)when(IsDisposed){}
        catch(Exception ex){if(!IsDisposed)status.Text="요청 실패: "+ex.Message;}
        finally{pending=false;}
    }
    private async Task Send(HttpMethod method,string path,object? input)
    {
        using var req=create(method,path,input);using var response=await http.SendAsync(req,closing.Token);
        if(!response.IsSuccessStatusCode)throw new InvalidOperationException($"{(int)response.StatusCode}: "+await response.Content.ReadAsStringAsync(closing.Token));
    }
    private async Task LoadData()
    {
        using var req=create(HttpMethod.Get,"/api/admin/community",null);using var response=await http.SendAsync(req,closing.Token);response.EnsureSuccessStatusCode();
        using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync(closing.Token));
        string? selectedInquiry=(inquiries.SelectedItem as Inquiry)?.Id;
        posts.Items.Clear();inquiries.Items.Clear();
        foreach(var p in doc.RootElement.GetProperty("posts").EnumerateArray())posts.Items.Add(new Post(p.GetProperty("id").GetString()!,p.GetProperty("kind").GetString()!,p.GetProperty("title").GetString()!,p.GetProperty("body").GetString()!,p.GetProperty("published").GetBoolean(),p.TryGetProperty("previewImageId",out var preview)?preview.GetString():null));
        foreach(var q in doc.RootElement.GetProperty("inquiries").EnumerateArray())
        {
            var row=new Inquiry(q.GetProperty("id").GetString()!,q.GetProperty("subject").GetString()!,q.GetProperty("body").GetString()!,q.GetProperty("reply").GetString()??"",q.GetProperty("status").GetString()!);
            int index=inquiries.Items.Add(row);if(row.Id==selectedInquiry)inquiries.SelectedIndex=index;
        }
        if(editingId is not null)foreach(Post post in posts.Items)if(post.Id==editingId){posts.SelectedItem=post;break;}
    }
}

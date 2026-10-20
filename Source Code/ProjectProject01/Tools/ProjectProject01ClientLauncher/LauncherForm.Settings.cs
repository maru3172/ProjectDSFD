using System.Diagnostics;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectProject01ClientLauncher;

internal sealed partial class LauncherForm
{
    private string inquiryReceipt=string.Empty;

    private sealed class InstallLock : IDisposable
    {
        private readonly Mutex mutex;
        public InstallLock(string root)
        {
            string hash=Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Path.GetFullPath(root).ToUpperInvariant())))[..24];
            mutex=new Mutex(false,"Local\\ProjectProject01Install_"+hash);
            bool acquired;try{acquired=mutex.WaitOne(0);}catch(AbandonedMutexException){acquired=true;}
            if(!acquired){mutex.Dispose();throw new InvalidOperationException("이 게임 설치 폴더를 다른 런처가 사용 중입니다.");}
        }
        public void Dispose(){mutex.ReleaseMutex();mutex.Dispose();}
    }
    // Named mutexes are thread-affine; async continuations return to the WinForms UI context.
    private IDisposable AcquireInstallLock()=>new InstallLock(ValidateInstallRoot(installRoot));

    internal static string ValidateInstallRoot(string path)
    {
        string root=Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar);
        if(root.Length<4 || string.Equals(root,Path.GetPathRoot(root)?.TrimEnd(Path.DirectorySeparatorChar),StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("드라이브 전체가 아닌 게임 전용 폴더를 선택하세요.");
        foreach(string special in new[]{Environment.GetFolderPath(Environment.SpecialFolder.Windows),Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory)})
            if(string.Equals(root,special.TrimEnd(Path.DirectorySeparatorChar),StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("시스템·사용자·바탕화면 전체를 설치 폴더로 사용할 수 없습니다.");
        for(var dir=new DirectoryInfo(root);dir is not null;dir=dir.Parent)
            if(dir.Exists&&(dir.Attributes&FileAttributes.ReparsePoint)!=0)throw new InvalidOperationException("연결 폴더가 아닌 실제 폴더를 선택하세요.");
        return root;
    }

    private void OpenLauncherSettings()
    {
        using var dialog=new Form {Text="런처 환경설정",Width=820,Height=650,MinimumSize=new Size(680,500),StartPosition=FormStartPosition.CenterParent};
        var tabs=new TabControl {Dock=DockStyle.Fill};dialog.Controls.Add(tabs);
        var statusLine=new Label {Dock=DockStyle.Bottom,AutoSize=true,MaximumSize=new Size(770,0)};dialog.Controls.Add(statusLine);
        TabPage Page(string title){var page=new TabPage(title){AutoScroll=true,Padding=new Padding(12)};tabs.TabPages.Add(page);return page;}
        var install=Page("서버·설치 관리");
        var layout=new FlowLayoutPanel {Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown,WrapContents=false,AutoScroll=true};install.Controls.Add(layout);
        layout.Controls.Add(new Label {Text="서버 PC IPv4",AutoSize=true});serverAddress.Width=580;serverAddress.Dock=DockStyle.None;layout.Controls.Add(serverAddress);
        var location=new TextBox {ReadOnly=true,Width=700,Text=installRoot};layout.Controls.Add(new Label {Text="게임 설치 위치",AutoSize=true});layout.Controls.Add(location);
        bool operation=false;
        async Task Run(Func<Task> action)
        {
            if(operation)return;operation=true;tabs.Enabled=false;
            try{await action();if(!dialog.IsDisposed)statusLine.Text=status.Text;}
            catch(Exception ex){if(!dialog.IsDisposed)statusLine.Text="실패: "+ex.Message;}
            finally{operation=false;if(!dialog.IsDisposed)tabs.Enabled=true;}
        }
        void Add(FlowLayoutPanel panel,string label,Func<Task> action)
        {
            var button=new Button {Text=label,AutoSize=true};panel.Controls.Add(button);button.Click+=async(_,_)=>await Run(action);
        }
        Add(layout,"서버 주소 저장",()=>{GetBackendBaseUrl();SaveSettings();SetStatus("서버 주소를 저장했습니다.");return Task.CompletedTask;});
        Task Choose(bool existing)
        {
            using var picker=new FolderBrowserDialog {Description=existing?"기존 Windows 게임 폴더를 선택하세요.":"새 게임을 설치할 전용 폴더를 선택하세요.",UseDescriptionForTitle=true,InitialDirectory=installRoot};
            if(picker.ShowDialog(dialog)!=DialogResult.OK)return Task.CompletedTask;
            string selected=ValidateInstallRoot(picker.SelectedPath);
            if(existing&&!File.Exists(Path.Combine(selected,"ProjectDFSD.exe"))&&!File.Exists(Path.Combine(selected,"ProjectProject01.exe"))&&!File.Exists(Path.Combine(selected,LocalManifestFileName)))throw new InvalidOperationException("게임 실행 파일 또는 설치 목록이 없는 폴더입니다.");
            if(!existing&&Directory.Exists(selected)&&Directory.EnumerateFileSystemEntries(selected).Any())throw new InvalidOperationException("새 설치에는 비어 있는 전용 폴더를 선택하세요. 기존 게임은 '기존 설치 지정'을 사용하세요.");
            EnsureGameIsNotRunning("ProjectDFSD.exe");installRoot=selected;Directory.CreateDirectory(installRoot);location.Text=installRoot;SaveSettings();SetStatus("설치 위치를 저장했습니다.");return Task.CompletedTask;
        }
        Add(layout,"새 설치 위치 선택",()=>Choose(false));Add(layout,"기존 게임 설치 위치 지정",()=>Choose(true));
        Add(layout,"선택한 위치에 설치 / 복구",()=>RunBusyAsync(()=>UpdateAsync(false)));
        Add(layout,"무결성 검사",()=>RunBusyAsync(async()=>
        {
            var manifest=await FetchManifestAsync(GetBackendBaseUrl());var plans=await BuildUpdatePlansAsync(manifest);
            SetStatus(plans.Count==0?"무결성 검사 완료: 모든 배포 파일이 정상입니다.":$"누락·변경된 파일 {plans.Count}개입니다. 설치 / 복구를 누르면 필요한 부분을 받습니다.");
        }));
        Add(layout,"캐시 삭제",()=>
        {
            EnsureGameIsNotRunning("ProjectDFSD.exe");
            if(MessageBox.Show(dialog,"이어받기 다운로드 캐시와 게임 셰이더 캐시만 삭제합니다. 저장소·환경설정·설치된 게임 파일은 유지됩니다. 계속할까요?","캐시 삭제",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes)return Task.CompletedTask;
            using var guard=AcquireInstallLock();
            foreach(string relative in new[]{".update-cache","ProjectProject01/Saved/DerivedDataCache","ProjectProject01/Saved/Shaders"})
            {
                string target=ResolveInsideRoot(ValidateInstallRoot(installRoot),relative);
                if(!Directory.Exists(target))continue;
                var directories=new Stack<string>();directories.Push(target);
                while(directories.TryPop(out string? directory))foreach(string entry in Directory.EnumerateFileSystemEntries(directory))
                {
                    FileAttributes attributes=File.GetAttributes(entry);
                    if((attributes&FileAttributes.ReparsePoint)!=0)throw new InvalidOperationException("연결 파일이 있는 캐시는 안전을 위해 삭제하지 않습니다.");
                    if((attributes&FileAttributes.Directory)!=0)directories.Push(entry);
                }
                Directory.Delete(target,true);
            }
            SetStatus("캐시를 삭제했습니다. 캐시는 필요할 때 다시 생성됩니다. 게임 파일 복구는 설치 / 복구를 사용하세요.");return Task.CompletedTask;
        });
        Add(layout,"게임 설치 / 다운로드 위치 열기",()=>{Process.Start(new ProcessStartInfo(installRoot){UseShellExecute=true});return Task.CompletedTask;});
        Add(layout,"이어받기 캐시 위치 열기",()=>{string cache=Path.Combine(installRoot,".update-cache");Directory.CreateDirectory(cache);Process.Start(new ProcessStartInfo(cache){UseShellExecute=true});return Task.CompletedTask;});

        var news=Page("공지사항·패치노트");
        var articles=new ListBox {Dock=DockStyle.Left,Width=250,DisplayMember="Title",DrawMode=DrawMode.OwnerDrawVariable,IntegralHeight=false};
        var thumbnails=new Dictionary<string,Image>();var attemptedThumbnails=new HashSet<string>();
        var thumbnailHttp=new HttpClient {Timeout=TimeSpan.FromSeconds(5)};
        var thumbnailLifetime=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        void ClearThumbnails(){foreach(var image in thumbnails.Values)image.Dispose();thumbnails.Clear();attemptedThumbnails.Clear();}
        async Task LoadThumbnail(LauncherPost post,CancellationToken ct)
        {
            string id=post.PreviewImageId!;
            try
            {
                Image image=await LauncherPostView.DownloadImageAsync(thumbnailHttp,GetBackendBaseUrl()+"/api/launcher/images/"+id,ct,192);
                if(ct.IsCancellationRequested||dialog.IsDisposed){image.Dispose();return;}
                thumbnails[id]=image;articles.Invalidate();
            }
            catch(OperationCanceledException){}
            catch(Exception){/* A failed optional thumbnail never prevents reading the post. */}
        }
        articles.MeasureItem+=(_,e)=>e.ItemHeight=e.Index>=0&&e.Index<articles.Items.Count&&(articles.Items[e.Index] as LauncherPost)?.PreviewImageId is not null?78:42;
        articles.DrawItem+=(_,e)=>
        {
            if(dialog.IsDisposed||thumbnailLifetime.IsCancellationRequested)return;
            if(e.Index<0||e.Index>=articles.Items.Count||articles.Items[e.Index] is not LauncherPost post)return;
            e.DrawBackground();int left=e.Bounds.Left+5;
            if(post.PreviewImageId is string id)
            {
                if(thumbnails.TryGetValue(id,out var image))
                {
                    double fit=Math.Min(96d/image.Width,54d/image.Height);int w=Math.Max(1,(int)(image.Width*fit)),h=Math.Max(1,(int)(image.Height*fit));
                    e.Graphics.DrawImage(image,new Rectangle(left+(96-w)/2,e.Bounds.Top+6+(54-h)/2,w,h));
                }
                else if(attemptedThumbnails.Add(id))_ = LoadThumbnail(post,thumbnailLifetime.Token);
                left+=102;
            }
            TextRenderer.DrawText(e.Graphics,post.Title,e.Font,new Rectangle(left,e.Bounds.Top+4,Math.Max(10,e.Bounds.Right-left-4),e.Bounds.Height-8),e.ForeColor,TextFormatFlags.WordBreak|TextFormatFlags.EndEllipsis);
            e.DrawFocusRectangle();
        };
        dialog.FormClosed+=(_,_)=>{thumbnailLifetime.Cancel();thumbnailLifetime.Dispose();thumbnailHttp.Dispose();ClearThumbnails();};
        var articleBody=new LauncherPostView {Dock=DockStyle.Fill};
        var reload=new Button {Text="새로고침",Dock=DockStyle.Top};news.Controls.Add(articleBody);news.Controls.Add(articles);news.Controls.Add(reload);
        articles.SelectedIndexChanged+=async(_,_)=>
        {
            if(articles.SelectedItem is not LauncherPost post)return;
            try{await articleBody.ShowPostAsync(post.Body,GetBackendBaseUrl());}
            catch(OperationCanceledException){}catch(Exception ex){if(!dialog.IsDisposed)statusLine.Text="표시 실패: "+ex.Message;}
        };
        reload.Click+=async(_,_)=>await Run(async()=>
        {
            using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(5)};
            using var response=await http.GetAsync(GetBackendBaseUrl()+"/api/launcher/posts",lifetime.Token);response.EnsureSuccessStatusCode();
            using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync(lifetime.Token));
            thumbnailLifetime.Cancel();thumbnailLifetime.Dispose();thumbnailLifetime=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);ClearThumbnails();articles.Items.Clear();
            foreach(var post in doc.RootElement.GetProperty("posts").EnumerateArray())
            {
                string? imageId=post.TryGetProperty("previewImageId",out var preview)?preview.GetString():null;
                articles.Items.Add(new LauncherPost((post.GetProperty("kind").GetString()=="Patch"?"[패치] ":"[뉴스] ")+post.GetProperty("title").GetString(),post.GetProperty("body").GetString()??"",Guid.TryParse(imageId,out var imageGuid)?imageGuid.ToString("D"):null));
            }
            if(articles.Items.Count>0)articles.SelectedIndex=0;
        });
        var support=Page("고객센터·문의");
        var inquiryLayout=new FlowLayoutPanel {Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown,WrapContents=false,AutoScroll=true};support.Controls.Add(inquiryLayout);
        var subject=new TextBox {Width=650,MaxLength=120,PlaceholderText="문의 제목"};
        var body=new TextBox {Width=650,Height=150,Multiline=true,ScrollBars=ScrollBars.Vertical,MaxLength=4000,PlaceholderText="문의 내용 — 비밀번호·인증 토큰은 적지 마세요."};
        var receipt=new TextBox {Width=650,Text=inquiryReceipt,MaxLength=48,PlaceholderText="답변 확인 코드 (분실하면 답변을 조회할 수 없습니다.)"};
        var answer=new TextBox {Width=650,Height=140,Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical};
        inquiryLayout.Controls.Add(new Label {AutoSize=true,Text="문의 제목"});inquiryLayout.Controls.Add(subject);
        inquiryLayout.Controls.Add(new Label {AutoSize=true,Text="문의 내용 (비밀번호·인증 토큰은 적지 마세요.)"});inquiryLayout.Controls.Add(body);
        Add(inquiryLayout,"문의 접수",async()=>
        {
            if(string.IsNullOrWhiteSpace(subject.Text)||string.IsNullOrWhiteSpace(body.Text))throw new InvalidOperationException("제목과 문의 내용을 입력하세요.");
            using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(5)};
            using var response=await http.PostAsJsonAsync(GetBackendBaseUrl()+"/api/launcher/inquiries",new{subject=subject.Text,body=body.Text},lifetime.Token);response.EnsureSuccessStatusCode();
            using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync(lifetime.Token));
            receipt.Text=inquiryReceipt=doc.RootElement.GetProperty("receipt").GetString()!;SaveSettings();answer.Text="문의가 접수되었습니다. 아래 확인 코드를 보관하세요.";
        });
        inquiryLayout.Controls.Add(new Label {AutoSize=true,Text="답변 확인 코드 — 최근 접수 코드는 이 PC에 보관됩니다. 이전 문의 코드는 따로 보관하세요."});inquiryLayout.Controls.Add(receipt);
        Add(inquiryLayout,"답변 확인",async()=>
        {
            using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(5)};
            using var response=await http.PostAsJsonAsync(GetBackendBaseUrl()+"/api/launcher/inquiries/reply",new{subject="",body="",replyCode=receipt.Text.Trim()},lifetime.Token);response.EnsureSuccessStatusCode();
            using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync(lifetime.Token));var root=doc.RootElement;
            answer.Text=root.GetProperty("subject").GetString()+Environment.NewLine+root.GetProperty("body").GetString()+Environment.NewLine+"답변: "+(root.GetProperty("reply").GetString()??"아직 답변이 없습니다.");
        });
        inquiryLayout.Controls.Add(answer);
        dialog.FormClosing+=(_,e)=>{if(operation){e.Cancel=true;statusLine.Text="작업을 마친 뒤 닫으세요.";}};
        dialog.ShowDialog(this);layout.Controls.Remove(serverAddress);SaveSettings();
    }
    private sealed record LauncherPost(string Title,string Body,string? PreviewImageId=null);
}

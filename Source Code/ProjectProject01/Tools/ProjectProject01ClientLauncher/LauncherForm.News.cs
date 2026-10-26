using System.Text.Json;

namespace ProjectProject01ClientLauncher;

internal sealed partial class LauncherForm
{
    private void OpenLauncherNews(string? selectedId=null)
    {
        using var dialog=new Form {Text="공지사항·패치노트",Width=900,Height=620,MinimumSize=new Size(680,460),StartPosition=FormStartPosition.CenterParent};
        var statusLine=new Label {Dock=DockStyle.Bottom,AutoSize=true,MaximumSize=new Size(850,0)};
        dialog.Controls.Add(statusLine);
        bool operation=false;
        async Task Run(Func<Task> action)
        {
            if(operation)return;operation=true;
            try{await action();if(!dialog.IsDisposed)statusLine.Text=string.Empty;}
            catch(OperationCanceledException){}
            catch(Exception ex){if(!dialog.IsDisposed)statusLine.Text="불러오기 실패: "+ex.Message;}
            finally{operation=false;}
        }
        var news=new Panel {Dock=DockStyle.Fill,Padding=new Padding(12)};dialog.Controls.Add(news);
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
        Task RefreshPosts()=>Run(async()=>
        {
            using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(5)};
            using var response=await http.GetAsync(GetBackendBaseUrl()+"/api/launcher/posts",lifetime.Token);response.EnsureSuccessStatusCode();
            using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync(lifetime.Token));
            thumbnailLifetime.Cancel();thumbnailLifetime.Dispose();thumbnailLifetime=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);ClearThumbnails();articles.Items.Clear();
            foreach(var post in doc.RootElement.GetProperty("posts").EnumerateArray())
            {
                string? imageId=post.TryGetProperty("previewImageId",out var preview)?preview.GetString():null;
                string prefix=post.GetProperty("kind").GetString() switch {"Patch"=>"[패치] ","Event"=>"[이벤트] ",_=>"[공지] "};
                articles.Items.Add(new LauncherPost(prefix+post.GetProperty("title").GetString(),post.GetProperty("body").GetString()??"",Guid.TryParse(imageId,out var imageGuid)?imageGuid.ToString("D"):null,post.GetProperty("id").GetString()));
            }
            if(articles.Items.Count>0)articles.SelectedIndex=0;
            if(selectedId is not null)foreach(LauncherPost post in articles.Items)if(post.Id==selectedId){articles.SelectedItem=post;break;}
        });
        reload.Click+=async(_,_)=>await RefreshPosts();
        dialog.Shown+=async(_,_)=>await RefreshPosts();
        dialog.ShowDialog(this);
    }
}

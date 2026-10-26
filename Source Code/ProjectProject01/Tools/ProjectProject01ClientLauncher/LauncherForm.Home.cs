using System.Text.Json;

namespace ProjectProject01ClientLauncher;

internal sealed partial class LauncherForm
{
    private readonly LauncherFeaturedCarousel featuredPosts=new(){Dock=DockStyle.Fill,Margin=new Padding(3,8,3,3)};
    private readonly TableLayoutPanel launcherSidebar=new(){Dock=DockStyle.Fill,ColumnCount=1,RowCount=2,Margin=Padding.Empty};
    private readonly Panel patchPanel=new(){Dock=DockStyle.Fill,Margin=new Padding(12,10,0,3),Padding=new Padding(10),BackColor=Color.FromArgb(28,31,35),Cursor=Cursors.Hand};
    private readonly Label patchHeading=new(){Dock=DockStyle.Top,Height=24,Text="패치노트",ForeColor=Color.White};
    private readonly Label patchSummary=new(){Dock=DockStyle.Fill,Text="등록된 패치노트가 없습니다.",ForeColor=Color.Gainsboro,AutoEllipsis=true,UseMnemonic=false};
    private readonly System.Windows.Forms.Timer homeTimer=new(){Interval=60000};
    private readonly HttpClient homeHttp=new(){Timeout=TimeSpan.FromSeconds(5)};
    private CancellationTokenSource? homeLoading;
    private string? patchId;
    private LauncherHomePost? currentPatch;

    private void InitializeHome()
    {
        patchPanel.Controls.Add(patchSummary);patchPanel.Controls.Add(patchHeading);
        featuredPosts.PostClicked+=id=>OpenLauncherNews(id);
        void ShowPatch(object? sender,EventArgs e){if(patchId is string id)OpenLauncherNews(id);}
        patchPanel.Click+=ShowPatch;patchHeading.Click+=ShowPatch;patchSummary.Click+=ShowPatch;
        homeTimer.Tick+=async(_,_)=>await RefreshHomeAsync();
        serverAddress.TextChanged+=(_,_)=>
        {
            homeLoading?.Cancel();ApplyHomePosts([],"");
            _ = RefreshHomeAsync();
        };
        FormClosed+=(_,_)=>{homeTimer.Dispose();homeLoading?.Cancel();homeLoading?.Dispose();homeHttp.Dispose();};
    }

    private async Task RefreshHomeAsync()
    {
        if(IsDisposed||lifetime.IsCancellationRequested)return;
        string url;
        try{url=GetBackendBaseUrl();}catch(InvalidOperationException){return;}
        homeLoading?.Cancel();homeLoading?.Dispose();homeLoading=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        var ct=homeLoading.Token;
        try
        {
            using var response=await homeHttp.GetAsync(url+"/api/launcher/posts",ct);response.EnsureSuccessStatusCode();
            using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync(ct));
            if(!ct.IsCancellationRequested&&!IsDisposed&&GetBackendBaseUrl()==url)ApplyHomePosts(ParseHomePosts(doc.RootElement),url);
        }
        catch(Exception ex)when(ex is HttpRequestException or OperationCanceledException or JsonException or InvalidOperationException or ObjectDisposedException){ }
    }

    internal static LauncherHomePost[] ParseHomePosts(JsonElement root)=>root.GetProperty("posts").EnumerateArray().Select(p=>new LauncherHomePost(
        p.GetProperty("id").GetString()!,p.GetProperty("kind").GetString()!,p.GetProperty("title").GetString()!,p.GetProperty("body").GetString()??"",
        p.TryGetProperty("previewImageId",out var preview)&&Guid.TryParse(preview.GetString(),out var id)?id.ToString("D"):null,
        p.TryGetProperty("featured",out var featured)&&featured.ValueKind==JsonValueKind.True,
        p.TryGetProperty("summary",out var summary)?summary.GetString():null)).ToArray();

    private void ApplyHomePosts(LauncherHomePost[] posts,string url)
    {
        featuredPosts.SetPosts(posts,url);
        currentPatch=posts.FirstOrDefault(p=>p.Kind=="Patch");
        UpdatePatchSummary();
    }
    private void UpdatePatchSummary()
    {
        var patch=currentPatch;
        patchId=patch?.Id;
        string source=patch is null?"":string.IsNullOrWhiteSpace(patch.CustomSummary)?patch.Body:patch.CustomSummary;
        int limit=announcementPanel.Visible?240:500;
        string excerpt=string.Join(Environment.NewLine,source.Replace("\r","").Split('\n').Where(l=>!string.IsNullOrWhiteSpace(l)&&!l.TrimStart().StartsWith("![")).Take(announcementPanel.Visible?5:12).Select(l=>l.Trim()));
        if(excerpt.Length>limit)excerpt=excerpt[..limit]+"…";
        patchSummary.Text=patch is null?"등록된 패치노트가 없습니다.":patch.Title+Environment.NewLine+Environment.NewLine+excerpt;
    }
    internal void FeatureSetHomePosts(LauncherHomePost[] posts)=>ApplyHomePosts(posts,"http://127.0.0.1:1");
}

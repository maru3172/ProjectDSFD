using System.Text.Json;

namespace ProjectProject01ClientLauncher;

internal sealed partial class LauncherForm
{
    private readonly TableLayoutPanel launcherContent=new(){Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Margin=Padding.Empty};
    private readonly Panel announcementPanel=new(){Dock=DockStyle.Top,Height=150,Visible=false,Margin=new Padding(12,3,0,0),Padding=new Padding(10,8,10,8),BackColor=Color.FromArgb(28,31,35)};
    private readonly Label announcementHeading=new(){Dock=DockStyle.Top,Height=20,Text="-공지-",ForeColor=Color.Gainsboro,TextAlign=ContentAlignment.MiddleLeft};
    private readonly RichTextBox announcementBody=new(){Dock=DockStyle.Fill,ReadOnly=true,BorderStyle=BorderStyle.None,WordWrap=true,ScrollBars=RichTextBoxScrollBars.Vertical,BackColor=Color.FromArgb(28,31,35),ForeColor=Color.Gainsboro};
    private readonly System.Windows.Forms.Timer announcementTimer=new(){Interval=5000};
    private readonly HttpClient announcementHttp=new(){Timeout=TimeSpan.FromSeconds(5)};
    private bool announcementPending;

    private void InitializeAnnouncement()
    {
        announcementPanel.Controls.Add(announcementBody);announcementPanel.Controls.Add(announcementHeading);
        serverAddress.TextChanged+=(_,_)=>ApplyAnnouncement(string.Empty);
        announcementTimer.Tick+=async(_,_)=>await RefreshAnnouncementAsync();
        FormClosed+=(_,_)=>{announcementTimer.Stop();announcementTimer.Dispose();announcementHttp.Dispose();};
    }
    private void ApplyAnnouncement(string? text,string? tag=null)
    {
        string value=text??string.Empty;
        announcementBody.Text=value;
        announcementHeading.Text=tag??string.Empty;announcementHeading.Visible=!string.IsNullOrWhiteSpace(tag);
        announcementPanel.Visible=!string.IsNullOrWhiteSpace(value);
        UpdatePatchSummary();
    }
    private async Task RefreshAnnouncementAsync()
    {
        if(announcementPending||IsDisposed||lifetime.IsCancellationRequested)return;
        string baseUrl;
        try{baseUrl=GetBackendBaseUrl();}catch(InvalidOperationException){ApplyAnnouncement(string.Empty);return;}
        announcementPending=true;
        try
        {
            using var response=await announcementHttp.GetAsync(baseUrl+"/api/service/status",lifetime.Token);
            response.EnsureSuccessStatusCode();
            using var json=JsonDocument.Parse(await response.Content.ReadAsStringAsync(lifetime.Token));
            string notice=json.RootElement.TryGetProperty("baseAnnouncement",out var value)?value.GetString()??string.Empty:
                json.RootElement.TryGetProperty("announcement",out value)?value.GetString()??string.Empty:string.Empty;
            string tag=json.RootElement.TryGetProperty("announcementTag",out var tagValue)?tagValue.GetString()??"":"";
            if(json.RootElement.TryGetProperty("announcementActive",out var active)&&active.ValueKind==JsonValueKind.False)notice=string.Empty;
            if(!IsDisposed&&!lifetime.IsCancellationRequested&&GetBackendBaseUrl()==baseUrl)ApplyAnnouncement(notice,tag);
        }
        catch(Exception ex)when(ex is HttpRequestException or TaskCanceledException or JsonException or InvalidOperationException or ObjectDisposedException)
        {if(!IsDisposed&&!lifetime.IsCancellationRequested)ApplyAnnouncement(string.Empty);}
        finally{announcementPending=false;}
    }

    internal void FeatureSetAnnouncement(string? value,string? tag=null)=>ApplyAnnouncement(value,tag);
    internal string FeatureAnnouncementTag=>announcementHeading.Visible?announcementHeading.Text:"";
    internal string FeatureAnnouncementText=>announcementBody.Text;
    internal bool FeatureAnnouncementVisible=>announcementPanel.Visible;
}

using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Text.RegularExpressions;

namespace ProjectProject01ClientLauncher;

internal sealed record LauncherHomePost(string Id,string Kind,string Title,string Body,string? PreviewImageId,bool Featured,string? CustomSummary=null)
{
    internal string Category=>Kind switch {"Patch"=>"패치노트","Event"=>"이벤트",_=>"공지사항"};
    internal string Summary=>Summarize(string.IsNullOrWhiteSpace(CustomSummary)?Body:CustomSummary);
    internal static string Summarize(string body)
    {
        var lines=body.Replace("\r","").Split('\n').Where(l=>!l.TrimStart().StartsWith("![")&&!Regex.IsMatch(l.Trim(),@"^\|?[\s:|\-]+\|$"));
        string text=Regex.Replace(string.Join(" ",lines).Replace('|',' '),@"\s+"," ").Trim();
        return text.Length>240?text[..240]+"…":text;
    }
}

/// <summary>Two clipped cards slide to the left; keep at most the current/next image decoded.</summary>
internal sealed class LauncherFeaturedCarousel : Control
{
    private readonly System.Windows.Forms.Timer timer=new(){Interval=16};
    private readonly Stopwatch clock=Stopwatch.StartNew();
    private readonly HttpClient http;
    private readonly Dictionary<string,Image> images=[];
    private readonly HashSet<string> attempted=[];
    private CancellationTokenSource loading=new();
    private LauncherHomePost[] posts=[];
    private string baseUrl="";
    private long changedAt;
    private int index;
    private float transition;
    internal event Action<string>? PostClicked;
    internal int PostCount=>posts.Length;
    internal string? CurrentId=>posts.Length==0?null:posts[index].Id;

    internal LauncherFeaturedCarousel(HttpMessageHandler? handler=null)
    {
        http=handler is null?new HttpClient():new HttpClient(handler);http.Timeout=TimeSpan.FromSeconds(5);
        SetStyle(ControlStyles.AllPaintingInWmPaint|ControlStyles.UserPaint|ControlStyles.OptimizedDoubleBuffer,true);
        BackColor=Color.FromArgb(24,28,34);ForeColor=Color.Gainsboro;
        AccessibleName="공지사항·이벤트";
        timer.Tick+=(_,_)=>
        {
            if(posts.Length<2||!Visible||FindForm()?.WindowState==FormWindowState.Minimized)return;
            Advance(clock.ElapsedMilliseconds-changedAt);
        };
        Click+=(_,_)=>{if(CurrentId is string id)PostClicked?.Invoke(id);};
    }

    private void Advance(long elapsed)
    {
        if(posts.Length<2||elapsed<10000)return;
        transition=Math.Clamp((elapsed-10000)/550f,0,1);
        if(transition>=1){index=(index+1)%posts.Length;transition=0;changedAt=clock.ElapsedMilliseconds;_ = PrepareImagesAsync(loading.Token);}
        Invalidate();
    }

    internal void SetPosts(IEnumerable<LauncherHomePost> value,string url)
    {
        var next=value.Where(p=>p.Featured).ToArray();
        if(baseUrl==url&&posts.SequenceEqual(next))return;
        loading.Cancel();loading.Dispose();loading=new CancellationTokenSource();
        foreach(var image in images.Values)image.Dispose();images.Clear();attempted.Clear();
        posts=next;baseUrl=url;index=0;transition=0;changedAt=clock.ElapsedMilliseconds;
        Cursor=posts.Length>0?Cursors.Hand:Cursors.Default;
        if(posts.Length>1)timer.Start();else timer.Stop();
        _ = PrepareImagesAsync(loading.Token);Invalidate();
    }

    private async Task PrepareImagesAsync(CancellationToken ct)
    {
        var keep=new HashSet<string>();
        if(posts.Length>0)
            for(int n=0;n<Math.Min(2,posts.Length);n++)
                if(posts[(index+n)%posts.Length].PreviewImageId is string id)keep.Add(id);
        foreach(string id in images.Keys.Where(id=>!keep.Contains(id)).ToArray()){images[id].Dispose();images.Remove(id);attempted.Remove(id);}
        foreach(string id in keep)
        {
            if(!attempted.Add(id))continue;
            try
            {
                Image image=await LauncherPostView.DownloadImageAsync(http,baseUrl+"/api/launcher/images/"+id,ct,1024);
                if(ct.IsCancellationRequested||IsDisposed){image.Dispose();return;}
                // A slide may have advanced during the request; discard out-of-window images.
                bool needed=posts.Length>0&&Enumerable.Range(0,Math.Min(2,posts.Length)).Any(n=>posts[(index+n)%posts.Length].PreviewImageId==id);
                if(!needed){image.Dispose();attempted.Remove(id);continue;}
                images[id]=image;Invalidate();
            }
            catch(OperationCanceledException){}
            catch(Exception ex)when(ex is HttpRequestException or InvalidDataException or ArgumentException or ObjectDisposedException){ }
        }
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        if(posts.Length==0)
        {
            TextRenderer.DrawText(e.Graphics,"등록된 공지·이벤트가 없습니다.",Font,ClientRectangle,ForeColor,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter);
            return;
        }
        var offsets=SlideOffsets;
        DrawCard(e.Graphics,posts[index],offsets.Current,index+1);
        if(transition>0)DrawCard(e.Graphics,posts[(index+1)%posts.Length],offsets.Next,(index+1)%posts.Length+1);
    }

    internal (int Current,int Next) SlideOffsets
    {
        get{float eased=transition*transition*(3-2*transition);int current=-(int)(ClientSize.Width*eased);return(current,current+ClientSize.Width);}
    }

    private void DrawCard(Graphics canvas,LauncherHomePost post,int offset,int number)
    {
        var saved=canvas.Save();canvas.TranslateTransform(offset,0);canvas.SetClip(new Rectangle(0,0,Width,Height));
        using(var background=new LinearGradientBrush(new Rectangle(0,0,Math.Max(1,Width),Math.Max(1,Height)),Color.FromArgb(37,43,52),BackColor,90))canvas.FillRectangle(background,0,0,Width,Height);
        if(post.PreviewImageId is string id&&images.TryGetValue(id,out var image))
        {
            double fit=Math.Min((double)Width/image.Width,(double)Height/image.Height);
            int w=(int)(image.Width*fit),h=(int)(image.Height*fit);
            canvas.DrawImage(image,(Width-w)/2,(Height-h)/2,w,h);
        }
        int bottom=Math.Min(100,Height);
        using(var shade=new SolidBrush(Color.FromArgb(215,18,21,26)))canvas.FillRectangle(shade,0,Height-bottom,Width,bottom);
        using var titleFont=new Font(Font.FontFamily,14,FontStyle.Bold);
        using(var tagShade=new SolidBrush(Color.FromArgb(210,35,40,48)))canvas.FillRectangle(tagShade,10,10,90,24);
        DrawText(canvas,post.Category,Font,new Rectangle(14,12,Width-28,22),Color.White);
        DrawText(canvas,post.Title,titleFont,new Rectangle(14,Height-bottom+10,Width-28,28),Color.White);
        DrawText(canvas,post.Summary,Font,new Rectangle(14,Height-bottom+42,Width-28,36),Color.Gainsboro,true);
        DrawText(canvas,$"{number} / {posts.Length}",Font,new Rectangle(14,Height-20,Width-28,18),Color.Silver,false,true);
        canvas.Restore(saved);
    }

    private static void DrawText(Graphics canvas,string text,Font font,Rectangle bounds,Color color,bool wrap=false,bool right=false)
    {
        using var brush=new SolidBrush(color);
        using var format=new StringFormat {Trimming=StringTrimming.EllipsisCharacter,FormatFlags=wrap?0:StringFormatFlags.NoWrap,Alignment=right?StringAlignment.Far:StringAlignment.Near};
        canvas.DrawString(text,font,brush,bounds,format);
    }

    internal void FeatureSetTransition(float amount){timer.Stop();transition=Math.Clamp(amount,0,1);Invalidate();}
    internal void FeatureAdvance(long elapsed)=>Advance(elapsed);
    internal int LoadedImageCount=>images.Count;
    protected override void Dispose(bool disposing)
    {
        if(disposing){timer.Dispose();loading.Cancel();loading.Dispose();http.Dispose();foreach(var image in images.Values)image.Dispose();images.Clear();}
        base.Dispose(disposing);
    }
}

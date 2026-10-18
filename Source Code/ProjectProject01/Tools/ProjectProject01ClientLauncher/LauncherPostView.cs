using System.Text.RegularExpressions;

#if PROJECT_SERVER_HELPER
namespace ProjectProject01BuildServerHelper;
#else
namespace ProjectProject01ClientLauncher;
#endif

/// <summary>Read-only post blocks; no browser, HTML, scripts or arbitrary external URLs.</summary>
internal sealed class LauncherPostView : UserControl
{
    private readonly FlowLayoutPanel blocks=new(){Dock=DockStyle.Fill,AutoScroll=true,FlowDirection=FlowDirection.TopDown,WrapContents=false};
    private readonly HttpClient http;
    private CancellationTokenSource? loading;
    private readonly List<Image> images=[];
    public LauncherPostView(HttpMessageHandler? handler=null){http=handler is null?new HttpClient():new HttpClient(handler);http.Timeout=TimeSpan.FromSeconds(5);Controls.Add(blocks);Resize+=(_,_)=>ResizeBlocks();}
    internal async Task ShowPostAsync(string text,string baseUrl)
    {
        if(IsDisposed)return;
        loading?.Cancel();loading?.Dispose();loading=new CancellationTokenSource();CancellationToken ct=loading.Token;
        foreach(Control old in blocks.Controls.Cast<Control>().ToArray())old.Dispose();blocks.Controls.Clear();foreach(var old in images)old.Dispose();images.Clear();
        var lines=text.Replace("\r","").Split('\n');int photoCount=0;
        for(int i=0;i<lines.Length;i++)
        {
            ct.ThrowIfCancellationRequested();string line=lines[i].Trim();
            if(line.StartsWith('|')&&line.EndsWith('|')&&i+1<lines.Length&&IsSeparator(lines[i+1]))
            {
                string[] header=Cells(line).Take(12).ToArray();
                var table=new DataGridView {ReadOnly=true,AllowUserToAddRows=false,AllowUserToDeleteRows=false,AllowUserToOrderColumns=false,RowHeadersVisible=false,AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill,BackgroundColor=SystemColors.Window,MultiSelect=false,Height=80,Tag="table"};
                for(int n=0;n<header.Length;n++)table.Columns.Add("col"+n,header[n]);i++;
                while(i+1<lines.Length&&lines[i+1].Trim().StartsWith('|')&&lines[i+1].Trim().EndsWith('|'))
                {
                    i++;if(table.Rows.Count>=100)continue;
                    var cells=Cells(lines[i]);table.Rows.Add(Enumerable.Range(0,header.Length).Select(n=>(object)(n<cells.Length?cells[n]:"")).ToArray());
                }
                table.Height=Math.Min(360,table.ColumnHeadersHeight+(table.Rows.Count+1)*table.RowTemplate.Height+4);blocks.Controls.Add(table);ResizeBlocks();continue;
            }
            var match=Regex.Match(line,@"^!\[([^\]\r\n]{0,120})\]\(image:([0-9a-fA-F-]{36})\)$");
            if(match.Success&&Guid.TryParse(match.Groups[2].Value,out var id)&&photoCount++<8)
            {
                var caption=new Label {AutoSize=true,Text=match.Groups[1].Value+" (불러오는 중…)"};blocks.Controls.Add(caption);ResizeBlocks();
                try
                {
                    Image image=await DownloadImageAsync(http,baseUrl+"/api/launcher/images/"+id.ToString("D"),ct,1600);images.Add(image);blocks.Controls.Add(new PictureBox {Image=image,SizeMode=PictureBoxSizeMode.Zoom,Tag="photo",AccessibleName=match.Groups[1].Value});caption.Text=match.Groups[1].Value;ResizeBlocks();
                }
                catch(OperationCanceledException)when(ct.IsCancellationRequested){return;}
                catch(Exception ex)when(ex is HttpRequestException or InvalidDataException or ArgumentException or OperationCanceledException)
                {if(!ct.IsCancellationRequested&&!IsDisposed)caption.Text="사진을 표시할 수 없습니다. 서버 연결을 확인하세요.";}
                continue;
            }
            blocks.Controls.Add(new Label {AutoSize=true,Text=line.Length==0?" ":line,ForeColor=SystemColors.ControlText});ResizeBlocks();
        }
    }
    internal static async Task<Image> DownloadImageAsync(HttpClient http,string url,CancellationToken ct,int maxDimension)
    {
        using var response=await http.GetAsync(url,HttpCompletionOption.ResponseHeadersRead,ct);response.EnsureSuccessStatusCode();
        if(response.Content.Headers.ContentLength>5*1024*1024)throw new InvalidDataException("사진이 너무 큽니다.");
        await using var stream=await response.Content.ReadAsStreamAsync(ct);using var memory=new MemoryStream();byte[] buffer=new byte[16384];int count;
        while((count=await stream.ReadAsync(buffer,ct))>0){if(memory.Length+count>5*1024*1024)throw new InvalidDataException("사진이 너무 큽니다.");memory.Write(buffer,0,count);}
        ct.ThrowIfCancellationRequested();memory.Position=0;using var decoded=Image.FromStream(memory,true,true);
        if(decoded.Width>8192||decoded.Height>8192||(long)decoded.Width*decoded.Height>16_000_000)throw new InvalidDataException("사진 크기 제한을 초과했습니다.");
        double scale=Math.Min(1,(double)maxDimension/Math.Max(decoded.Width,decoded.Height));
        return new Bitmap(decoded,Math.Max(1,(int)(decoded.Width*scale)),Math.Max(1,(int)(decoded.Height*scale)));
    }
    private static string[] Cells(string line)=>line.Trim().Trim('|').Split('|').Select(s=>s.Trim()).ToArray();
    private static bool IsSeparator(string line)=>line.Trim().StartsWith('|')&&Cells(line).All(s=>Regex.IsMatch(s,@"^:?-{3,}:?$"));
    private void ResizeBlocks()
    {
        int width=Math.Max(120,blocks.ClientSize.Width-26);
        foreach(Control c in blocks.Controls)
        {
            if(c is Label label)label.MaximumSize=new Size(width,0);
            else{c.Width=width;if(c is PictureBox picture&&picture.Image is not null)c.Height=Math.Clamp((int)((double)width*picture.Image.Height/picture.Image.Width),40,600);}
        }
    }
    protected override void Dispose(bool disposing)
    {
        if(disposing){var cancellation=loading;loading=null;cancellation?.Cancel();cancellation?.Dispose();http.Dispose();foreach(var image in images)image.Dispose();images.Clear();}base.Dispose(disposing);
    }
}

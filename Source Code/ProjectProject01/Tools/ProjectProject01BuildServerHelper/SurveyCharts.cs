using System.Text.Json;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace ProjectProject01BuildServerHelper;

internal sealed class SurveyBarChart : Control
{
    private readonly string caption;
    private readonly (string Label,int Count)[] values;
    private readonly int denominator;
    private readonly string unit;
    private readonly bool normalizeToMaximum;
    public SurveyBarChart(string caption,(string Label,int Count)[] values,int denominator,string unit="명",bool normalizeToMaximum=false)
    {
        this.caption=caption; this.values=values; this.denominator=denominator;
        this.unit=unit; this.normalizeToMaximum=normalizeToMaximum;
        Width=800; Height=60+values.Length*42; BackColor=Color.White; ForeColor=Color.Black;
        DoubleBuffered=true;
        Font=new Font("맑은 고딕",10);
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        TextRenderer.DrawText(e.Graphics,caption,Font,new Rectangle(10,8,Width-20,35),ForeColor,
            TextFormatFlags.WordBreak);
        int maximum=Math.Max(1,Math.Max(normalizeToMaximum ? 0 : denominator,values.Select(v=>v.Count).DefaultIfEmpty().Max()));
        for(int i=0;i<values.Length;i++)
        {
            int y=48+i*42;
            TextRenderer.DrawText(e.Graphics,values[i].Label,Font,new Rectangle(10,y,240,38),ForeColor,TextFormatFlags.WordBreak);
            e.Graphics.FillRectangle(Brushes.LightGray,255,y+5,360,22);
            e.Graphics.FillRectangle(Brushes.SteelBlue,255,y+5,360f*values[i].Count/maximum,22);
            double percent=denominator>0 ? 100d*values[i].Count/denominator : 0;
            TextRenderer.DrawText(e.Graphics,$"{values[i].Count}{unit} ({percent:0.0}%)",Font,new Point(625,y+5),ForeColor);
        }
    }
}

internal sealed class SurveyPieChart : Control
{
    private readonly string caption;
    private readonly (string Label,int Count)[] values;
    private static readonly Color[] Palette=[Color.SteelBlue,Color.DarkOrange,Color.SeaGreen,Color.MediumPurple,
        Color.IndianRed,Color.Teal,Color.Goldenrod,Color.CornflowerBlue,Color.SlateGray];
    public SurveyPieChart(string caption,(string Label,int Count)[] values)
    {
        this.caption=caption;this.values=values;
        Width=900;Height=Math.Max(355,70+values.Length*34);BackColor=Color.White;ForeColor=Color.Black;
        Font=new Font("맑은 고딕",10);DoubleBuffered=true;
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
        TextRenderer.DrawText(e.Graphics,caption,Font,new Rectangle(12,10,Width-24,42),ForeColor,TextFormatFlags.WordBreak);
        int total=values.Sum(value=>Math.Max(0,value.Count));
        var circle=new Rectangle(22,65,260,260);
        if(total==0)
        {
            e.Graphics.FillEllipse(Brushes.Gainsboro,circle);
            TextRenderer.DrawText(e.Graphics,"응답 없음",Font,circle,Color.DimGray,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter);
        }
        float angle=-90;
        for(int i=0;i<values.Length;i++)
        {
            Color color=Palette[i%Palette.Length];using var brush=new SolidBrush(color);
            float sweep=total>0 ? 360f*Math.Max(0,values[i].Count)/total : 0;
            if(sweep>0){e.Graphics.FillPie(brush,circle,angle,sweep);e.Graphics.DrawPie(Pens.White,circle,angle,sweep);}
            angle+=sweep;
            int y=65+i*34;
            e.Graphics.FillRectangle(brush,310,y+5,16,16);
            double percent=total>0 ? 100d*values[i].Count/total : 0;
            TextRenderer.DrawText(e.Graphics,$"{values[i].Label}: {values[i].Count}건 ({percent:0.0}%)",Font,
                new Rectangle(337,y,Math.Max(100,Width-349),32),ForeColor,TextFormatFlags.EndEllipsis|TextFormatFlags.VerticalCenter);
        }
    }
}

internal sealed record SurveyTrendValue(DateTime Date,int Total,int Accepted,int Pending);

internal sealed class SurveyLineChart : Control
{
    private readonly SurveyTrendValue[] values;
    public SurveyLineChart(SurveyTrendValue[] values)
    {
        this.values=values;Width=900;Height=430;BackColor=Color.White;ForeColor=Color.Black;
        Font=new Font("맑은 고딕",10);DoubleBuffered=true;
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
        TextRenderer.DrawText(e.Graphics,"제출일별 응답 건수 · 한국 시간 · 현재 반영/보류 상태 · 무응답 날짜는 0건",Font,new Point(12,10),ForeColor);
        if(values.Length==0){TextRenderer.DrawText(e.Graphics,"집계할 응답이 없습니다.",Font,new Point(30,110),Color.DimGray);return;}
        var plot=new RectangleF(65,80,Math.Max(200,Width-100),265);
        int tickStep=Math.Max(1,(int)Math.Ceiling(values.Max(value=>value.Total)/4d));
        int maximum=tickStep*4;
        for(int step=0;step<=4;step++)
        {
            float y=plot.Bottom-plot.Height*step/4;
            e.Graphics.DrawLine(Pens.Gainsboro,plot.Left,y,plot.Right,y);
            TextRenderer.DrawText(e.Graphics,(tickStep*step).ToString(),Font,new Rectangle(5,(int)y-10,50,22),ForeColor,TextFormatFlags.Right);
        }
        e.Graphics.DrawLine(Pens.DimGray,plot.Left,plot.Top,plot.Left,plot.Bottom);
        e.Graphics.DrawLine(Pens.DimGray,plot.Left,plot.Bottom,plot.Right,plot.Bottom);
        (string Label,Color Color,Func<SurveyTrendValue,int> Count)[] series=[
            ("전체",Color.SteelBlue,v=>v.Total),("반영",Color.SeaGreen,v=>v.Accepted),("보류",Color.DarkOrange,v=>v.Pending)];
        foreach(var (label,color,count) in series)
        {
            using var pen=new Pen(color,2);using var brush=new SolidBrush(color);
            var points=values.Select((value,index)=>new PointF(values.Length==1 ? plot.Left+plot.Width/2 : plot.Left+plot.Width*index/(values.Length-1),
                plot.Bottom-plot.Height*count(value)/maximum)).ToArray();
            if(points.Length>1)e.Graphics.DrawLines(pen,points);
            foreach(var point in points)e.Graphics.FillEllipse(brush,point.X-3,point.Y-3,6,6);
        }
        int stride=Math.Max(1,(int)Math.Ceiling(values.Length/7d));
        for(int i=0;i<values.Length;i++)
            if(i%stride==0||i==values.Length-1)
            {
                float x=values.Length==1 ? plot.Left+plot.Width/2 : plot.Left+plot.Width*i/(values.Length-1);
                TextRenderer.DrawText(e.Graphics,values[i].Date.ToString("MM-dd"),Font,new Rectangle((int)x-30,(int)plot.Bottom+8,60,24),ForeColor,TextFormatFlags.HorizontalCenter);
            }
        for(int i=0;i<series.Length;i++)
        {
            using var pen=new Pen(series[i].Color,3);e.Graphics.DrawLine(pen,30+i*135,392,55+i*135,392);
            TextRenderer.DrawText(e.Graphics,series[i].Label,Font,new Point(62+i*135,381),ForeColor);
        }
        TextRenderer.DrawText(e.Graphics,$"{values[0].Date:yyyy-MM-dd} ~ {values[^1].Date:yyyy-MM-dd}",Font,new Point(Math.Max(460,Width-310),381),ForeColor);
    }
}

internal sealed class SurveyOverviewForm : Form
{
    public SurveyOverviewForm(string json,Func<string,Task> openSurvey)
    {
        Text="전체 설문 통계"; Width=1000; Height=760; StartPosition=FormStartPosition.CenterParent;
        using var document=JsonDocument.Parse(json);
        var root=document.RootElement;
        var summary=new Label { Dock=DockStyle.Top, Height=55, Padding=new Padding(10), Text=
            $"설문 {root.GetProperty("surveyCount").GetInt32()}개 · 응답 {root.GetProperty("responseCount").GetInt32()}건 · 참여 계정 {root.GetProperty("participantCount").GetInt32()}명\n"+
            $"반영 {root.GetProperty("acceptedCount").GetInt32()}건 · 보류 {root.GetProperty("pendingCount").GetInt32()}건 (보류는 문항 통계 제외)" };
        var surveys=root.GetProperty("surveys").EnumerateArray().Select(s=>s.Clone()).ToArray();
        var tabs=new TabControl {Dock=DockStyle.Fill};
        int total=root.GetProperty("responseCount").GetInt32();
        (string Label,int Count)[] states=[("반영",root.GetProperty("acceptedCount").GetInt32()),("보류",root.GetProperty("pendingCount").GetInt32())];
        AddGraphPage(tabs,"막대 그래프",
            new SurveyBarChart("설문별 응답 건수",surveys.Select(s=>(s.GetProperty("title").GetString()??"",s.GetProperty("responseCount").GetInt32())).ToArray(),total,"건",true),
            new SurveyBarChart("전체 응답 처리 건수",states,total,"건",true));
        var shares=surveys.Select(s=>(Label:s.GetProperty("title").GetString()??"",Count:s.GetProperty("responseCount").GetInt32()))
            .Where(value=>value.Count>0).OrderByDescending(value=>value.Count).ToArray();
        var pieShares=shares.Take(8).ToList();
        if(shares.Length>8)pieShares.Add(("그 외 설문",shares.Skip(8).Sum(value=>value.Count)));
        AddGraphPage(tabs,"비율 · 파이",
            new SurveyPieChart("전체 응답 중 설문별 비율 (상위 8개 + 그 외)",pieShares.ToArray()),
            new SurveyPieChart("전체 응답 중 반영·보류 비율",states));
        var trend=root.TryGetProperty("responseTrend",out var trendJson) ? trendJson.EnumerateArray().Select(p=>new SurveyTrendValue(
            DateTime.ParseExact(p.GetProperty("date").GetString()!,"yyyy-MM-dd",System.Globalization.CultureInfo.InvariantCulture),
            p.GetProperty("responseCount").GetInt32(),p.GetProperty("acceptedCount").GetInt32(),p.GetProperty("pendingCount").GetInt32())).ToArray() : [];
        AddGraphPage(tabs,"변화 · 꺾은선",new SurveyLineChart(trend));
        var tablePage=new TabPage("설문별 응답 수");
        var table=new DataGridView {Dock=DockStyle.Fill, ReadOnly=true, AllowUserToAddRows=false,
            AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill, SelectionMode=DataGridViewSelectionMode.FullRowSelect};
        foreach(string column in new[]{"설문","상태","전체 응답","반영","보류"}) table.Columns.Add(column,column);
        foreach(var survey in surveys) table.Rows.Add(survey.GetProperty("title").GetString(),
            survey.GetProperty("isActive").GetBoolean()?"진행 중":"비활성",
            survey.GetProperty("responseCount").GetInt32(),survey.GetProperty("acceptedCount").GetInt32(),survey.GetProperty("pendingCount").GetInt32());
        table.CellDoubleClick+=async (_,e)=>{ if(e.RowIndex<0)return; try{await openSurvey(surveys[e.RowIndex].GetProperty("surveyId").GetString()!);}catch(Exception ex){MessageBox.Show(ex.Message);} };
        tablePage.Controls.Add(table); tabs.TabPages.Add(tablePage);
        Controls.Add(tabs); Controls.Add(summary);
    }

    private static void AddGraphPage(TabControl tabs,string title,params Control[] charts)
    {
        var page=new TabPage(title);
        var scroll=new FlowLayoutPanel {Dock=DockStyle.Fill,AutoScroll=true,WrapContents=false,FlowDirection=FlowDirection.TopDown};
        foreach(var chart in charts){chart.Margin=new Padding(8);scroll.Controls.Add(chart);}
        var save=new Button {Text="그래프 그림으로 저장 (PNG)",Dock=DockStyle.Bottom,Height=34};
        save.Click+=(_,_)=>
        {
            using var dialog=new SaveFileDialog {Filter="PNG 이미지|*.png",FileName=$"설문통계-{title.Replace('·','-')}.png"};
            if(dialog.ShowDialog(page.FindForm())!=DialogResult.OK)return;
            try
            {
                using var bitmap=new Bitmap(charts.Max(c=>c.Width)+16,charts.Sum(c=>c.Height+16));
                using var graphics=Graphics.FromImage(bitmap);graphics.Clear(Color.White);
                int y=8;
                foreach(var chart in charts)
                {
                    using var image=new Bitmap(chart.Width,chart.Height);chart.DrawToBitmap(image,new Rectangle(Point.Empty,chart.Size));
                    graphics.DrawImageUnscaled(image,8,y);y+=chart.Height+16;
                }
                bitmap.Save(dialog.FileName,ImageFormat.Png);
            }
            catch(Exception ex){MessageBox.Show(ex.Message,"그래프 저장");}
        };
        page.Controls.Add(scroll);page.Controls.Add(save);tabs.TabPages.Add(page);
    }
}

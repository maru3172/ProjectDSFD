using System.Drawing.Drawing2D;
namespace ProjectProject01BuildServerHelper;

/// <summary>
/// Draws one quest's steps, or one conversation's nodes, as boxes joined by arrows (Docs/QuestGraph.md 6). Boxes can be
/// dragged; clicking selects (a choice node's option rows are selectable on their own); double-click opens; right-click
/// asks the form for a menu at that spot. The links themselves are edited in the form's side panel.
/// </summary>
internal sealed class QuestGraphCanvas:Panel
{
    public QgGraph? Graph;public QgQuest? Quest;public QgTalk? Talk;
    public Dictionary<string,int> Players=new(); // Active players per step id of the shown quest.
    public object? Selected;public int SelectedOption=-1;
    public event Action? SelectionChanged,Moved;
    public event Action<object>? Activated;
    public event Action<Point,object?>? MenuRequested;

    const int StepW=200,StepH=66,NodeW=230,MsgH=58,ChoiceHead=24,OptionH=28;
    static readonly Font TitleFont=new("Malgun Gothic",9.5f,FontStyle.Bold),BodyFont=new("Malgun Gothic",8.5f),SmallFont=new("Malgun Gothic",8f);
    object? dragging;Point dragOffset;bool dragMoved;

    public QuestGraphCanvas(){DoubleBuffered=true;AutoScroll=true;BackColor=Color.FromArgb(30,32,36);ResizeRedraw=true;}
    public void ShowQuest(QgGraph g,QgQuest? q){Graph=g;Quest=q;Talk=null;Selected=null;SelectedOption=-1;AutoScrollPosition=Point.Empty;Refit();}
    public void ShowTalk(QgGraph g,QgTalk t){Graph=g;Talk=t;Selected=null;SelectedOption=-1;AutoScrollPosition=Point.Empty;Refit();}
    public void Refit()
    {
        var rects=Boxes().Select(b=>b.Rect).ToList();
        AutoScrollMinSize=rects.Count==0?Size.Empty:new Size(rects.Max(r=>r.Right)+120,rects.Max(r=>r.Bottom)+120);Invalidate();
    }

    IEnumerable<(object Item,Rectangle Rect)> Boxes()
    {
        if(Talk is not null)foreach(var n in Talk.Nodes)yield return(n,new Rectangle((int)n.X,(int)n.Y,NodeW,n.IsChoice?ChoiceHead+OptionH*Math.Max(1,n.Options!.Count)+6:MsgH));
        else if(Quest is not null)foreach(var s in Quest.Steps)yield return(s,new Rectangle((int)s.X,(int)s.Y,StepW,StepH));
    }
    Rectangle RectOf(object item)=>Boxes().FirstOrDefault(b=>b.Item==item).Rect;
    Point Model(Point screen)=>new(screen.X-AutoScrollPosition.X,screen.Y-AutoScrollPosition.Y);

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);var g=e.Graphics;g.SmoothingMode=SmoothingMode.AntiAlias;g.TranslateTransform(AutoScrollPosition.X,AutoScrollPosition.Y);
        using var link=new Pen(Color.FromArgb(150,170,190),1.6f){CustomEndCap=new AdjustableArrowCap(4,5)};
        using var conditional=new Pen(Color.FromArgb(230,180,90),1.6f){CustomEndCap=new AdjustableArrowCap(4,5),DashStyle=DashStyle.Dash};
        if(Quest is not null&&Talk is null)
        {
            foreach(var s in Quest.Steps)
            {
                var from=RectOf(s);int i=0;
                foreach(var r in s.Next)
                {
                    var to=Quest.Steps.FirstOrDefault(x=>x.Id==r.Step);if(to is null)continue;
                    var label=r.Flag is null?(s.Next.Count>1?"그 외":null):(r.Without?"표시 없음: ":"표시: ")+r.Flag;
                    Arrow(g,r.Flag is null?link:conditional,new Point(from.Right,from.Top+from.Height/2+(i++-(s.Next.Count-1)/2)*8),RectOf(to),label);
                }
            }
            foreach(var s in Quest.Steps)DrawStep(g,s);
            DrawLabels(g);
        }
        else if(Talk is not null)
        {
            foreach(var n in Talk.Nodes)
            {
                var from=RectOf(n);
                if(!n.IsChoice){if(Find(n.Next) is {} t)Arrow(g,link,new Point(from.Right,from.Top+from.Height/2),RectOf(t),null);continue;}
                for(int i=0;i<n.Options!.Count;i++)if(Find(n.Options[i].Next) is {} t)
                {
                    var o=n.Options[i];var label=string.Join(" ",new[]{o.Flags is {Count:>0}?"표시 "+string.Join(",",o.Flags):null,o.Gives is null?null:"퀘스트 "+o.Gives}.Where(x=>x is not null));
                    Arrow(g,o.Flags is {Count:>0}||o.Gives is not null?conditional:link,new Point(from.Right,from.Top+ChoiceHead+i*OptionH+OptionH/2),RectOf(t),label.Length==0?null:label);
                }
            }
            foreach(var n in Talk.Nodes)DrawNode(g,n);
            DrawLabels(g);
        }
        if(Quest is null&&Talk is null)TextRenderer.DrawText(g,"왼쪽에서 퀘스트를 고르세요.",BodyFont,new Point(30,30),Color.Gainsboro);
    }
    QgNode? Find(string? id)=>id is null||Talk is null?null:Talk.Nodes.FirstOrDefault(n=>n.Id==id);
    void Arrow(Graphics g,Pen pen,Point start,Rectangle target,string? label)
    {
        var end=new Point(target.Left,target.Top+target.Height/2);
        if(end.X<start.X+30)
        {
            // A link back to the left (the next row of a wrapped line, or a loop): out to the right, along the gap
            // above the target, then into its left side, so it never leaves the canvas.
            int lane=end.Y>start.Y?target.Top-22:target.Bottom+22,right=start.X+18,left=Math.Max(6,end.X-18);
            g.DrawLines(pen,new[]{start,new Point(right,start.Y),new Point(right,lane),new Point(left,lane),new Point(left,end.Y),end});
        }
        else{float bend=Math.Max(60,Math.Abs(end.X-start.X)/2f);g.DrawBezier(pen,start,new PointF(start.X+bend,start.Y),new PointF(end.X-bend,end.Y),end);}
        if(label is not null)labels.Add((new Point(start.X+6,start.Y-17),label)); // Just past where the arrow leaves its box.
    }
    // Arrow labels are drawn after the boxes, so a box never hides the condition on a short arrow.
    readonly List<(Point At,string Text)> labels=new();
    void DrawLabels(Graphics g)
    {
        using var back=new SolidBrush(Color.FromArgb(235,30,32,36));
        foreach(var (at,text) in labels)
        {
            var size=TextRenderer.MeasureText(text,SmallFont);var box=new Rectangle(at.X,at.Y,size.Width+6,size.Height);
            g.FillRectangle(back,box);TextRenderer.DrawText(g,text,SmallFont,new Point(box.X+3,box.Y),Color.FromArgb(240,200,120));
        }
        labels.Clear();
    }
    void Box(Graphics g,Rectangle r,Color fill,bool selected,bool start)
    {
        using var path=Rounded(r,8);using var brush=new SolidBrush(fill);g.FillPath(brush,path);
        using var pen=new Pen(selected?Color.FromArgb(255,210,90):start?Color.FromArgb(110,200,130):Color.FromArgb(90,96,106),selected||start?2.4f:1.2f);g.DrawPath(pen,path);
        if(start)TextRenderer.DrawText(g,"시작",SmallFont,new Point(r.Left,r.Top-16),Color.FromArgb(110,200,130));
    }
    void DrawStep(Graphics g,QgStep s)
    {
        var r=RectOf(s);Box(g,r,Color.FromArgb(48,54,64),Selected==s,Quest!.Start==s.Id);
        TextRenderer.DrawText(g,s.Title.Length==0?"(제목 없음)":s.Title,TitleFont,new Rectangle(r.X+8,r.Y+6,r.Width-16,20),Color.White,TextFormatFlags.EndEllipsis);
        var goals=string.Join(" · ",s.Goals.Select(x=>QgGraph.MetricNames.GetValueOrDefault(x.Metric,x.Metric)+(x.Target>1?$" {x.Target}회":"")));
        TextRenderer.DrawText(g,goals,BodyFont,new Rectangle(r.X+8,r.Y+26,r.Width-16,18),Color.FromArgb(190,198,210),TextFormatFlags.EndEllipsis);
        var talk=Graph?.TalkOf(s);var foot=(talk is null?"":"대화: "+Graph!.NpcName(talk.Npc))+(Players.TryGetValue(s.Id,out var n)&&n>0?$"   진행 중 {n}명":"");
        TextRenderer.DrawText(g,foot,SmallFont,new Rectangle(r.X+8,r.Y+44,r.Width-16,18),Color.FromArgb(140,200,230),TextFormatFlags.EndEllipsis);
    }
    void DrawNode(Graphics g,QgNode n)
    {
        var r=RectOf(n);
        if(!n.IsChoice)
        {
            Box(g,r,Color.FromArgb(52,58,70),Selected==n,Talk!.Start==n.Id);
            TextRenderer.DrawText(g,Graph!.NpcName(Talk.Npc)+" · "+n.Id,SmallFont,new Rectangle(r.X+8,r.Y+4,r.Width-16,16),Color.FromArgb(150,160,175),TextFormatFlags.EndEllipsis);
            TextRenderer.DrawText(g,n.Text??"",BodyFont,new Rectangle(r.X+8,r.Y+20,r.Width-16,34),Color.White,TextFormatFlags.WordBreak|TextFormatFlags.EndEllipsis);
            return;
        }
        Box(g,r,Color.FromArgb(40,58,52),Selected==n&&SelectedOption<0,Talk!.Start==n.Id);
        TextRenderer.DrawText(g,"플레이어 선택 · "+n.Id,SmallFont,new Rectangle(r.X+8,r.Y+4,r.Width-16,16),Color.FromArgb(150,190,170),TextFormatFlags.EndEllipsis);
        for(int i=0;i<n.Options!.Count;i++)
        {
            var row=new Rectangle(r.X+6,r.Y+ChoiceHead+i*OptionH,r.Width-12,OptionH-4);
            using(var fill=new SolidBrush(Selected==n&&SelectedOption==i?Color.FromArgb(90,80,40):Color.FromArgb(56,78,70)))g.FillRectangle(fill,row);
            TextRenderer.DrawText(g,$"{i+1}. {n.Options[i].Text}",BodyFont,new Rectangle(row.X+4,row.Y+4,row.Width-8,row.Height-6),Color.White,TextFormatFlags.EndEllipsis);
        }
    }
    static GraphicsPath Rounded(Rectangle r,int radius)
    {
        var p=new GraphicsPath();int d=radius*2;p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.Right-d,r.Y,d,d,270,90);p.AddArc(r.Right-d,r.Bottom-d,d,d,0,90);p.AddArc(r.X,r.Bottom-d,d,d,90,90);p.CloseFigure();return p;
    }

    (object? Item,int Option) HitTest(Point model)
    {
        foreach(var (item,rect) in Boxes().Reverse())
        {
            if(!rect.Contains(model))continue;
            if(item is QgNode{IsChoice:true} n){int i=(model.Y-rect.Y-ChoiceHead)/OptionH;return(item,model.Y>=rect.Y+ChoiceHead&&i>=0&&i<n.Options!.Count?i:-1);}
            return(item,-1);
        }
        return(null,-1);
    }
    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);Focus();var model=Model(e.Location);var (item,option)=HitTest(model);
        if(Selected!=item||SelectedOption!=option){Selected=item;SelectedOption=option;SelectionChanged?.Invoke();Invalidate();}
        if(e.Button==MouseButtons.Right){MenuRequested?.Invoke(model,item);return;}
        if(item is not null&&e.Button==MouseButtons.Left){dragging=item;var r=RectOf(item);dragOffset=new Point(model.X-r.X,model.Y-r.Y);dragMoved=false;}
    }
    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);if(dragging is null)return;var p=Model(e.Location);double x=Math.Max(0,p.X-dragOffset.X),y=Math.Max(16,p.Y-dragOffset.Y);
        // Snap to a 10px grid so boxes line up.
        x=Math.Round(x/10)*10;y=Math.Round(y/10)*10;
        if(dragging is QgStep s&&(s.X!=x||s.Y!=y)){s.X=x;s.Y=y;dragMoved=true;Invalidate();}
        if(dragging is QgNode n&&(n.X!=x||n.Y!=y)){n.X=x;n.Y=y;dragMoved=true;Invalidate();}
    }
    protected override void OnMouseUp(MouseEventArgs e){base.OnMouseUp(e);if(dragging is not null&&dragMoved){Refit();Moved?.Invoke();}dragging=null;}
    protected override void OnMouseDoubleClick(MouseEventArgs e){base.OnMouseDoubleClick(e);var (item,_)=HitTest(Model(e.Location));if(item is not null)Activated?.Invoke(item);}
}

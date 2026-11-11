using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.RegularExpressions;
namespace ProjectProject01BuildServerHelper;

/// <summary>
/// 퀘스트·대화 그래프 편집기 (Docs/QuestGraph.md 6). Left: NPCs and quests by kind. Center: the chosen quest's steps,
/// or a step's conversation, as boxes and arrows (drag to place, right-click to add, link, delete). Right: the chosen
/// item's fields. Bottom: the server's checks. Saving asks where players go when a step they stand on disappears.
/// </summary>
internal sealed class QuestGraphEditorForm:Form
{
    readonly Func<HttpMethod,string,object?,HttpRequestMessage> create;
    readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(10)};
    QgGraph graph=new();long revision;bool dirty;
    Dictionary<(string Quest,string Step),int> players=new();
    QgQuest? quest;QgTalk? talk;QgStep? talkStep;QgNpc? npc;

    readonly TreeView tree=new(){Dock=DockStyle.Fill,HideSelection=false,Font=new Font("Malgun Gothic",9.5f)};
    readonly QuestGraphCanvas canvas=new(){Dock=DockStyle.Fill};
    readonly TableLayoutPanel props=new(){Dock=DockStyle.Top,AutoSize=true,ColumnCount=1,Padding=new Padding(8)};
    readonly ListBox errors=new(){Dock=DockStyle.Fill,Font=new Font("Malgun Gothic",9f),HorizontalScrollbar=true};
    readonly Label status=new(){AutoSize=true,Padding=new Padding(8,7,0,0)};
    readonly Label where=new(){AutoSize=true,Padding=new Padding(4,7,0,0),Font=new Font("Malgun Gothic",9.5f,FontStyle.Bold)};
    readonly Button back=new(){Text="퀘스트로 돌아가기",AutoSize=true,Visible=false};
    readonly Button preview=new(){Text="대화 미리보기",AutoSize=true,Visible=false};
    static readonly Regex IdPattern=new("^[a-z0-9_]{1,40}$");

    public QuestGraphEditorForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        this.create=create;Text="퀘스트·대화 그래프";Width=1500;Height=900;MinimumSize=new Size(1100,700);StartPosition=FormStartPosition.CenterParent;
        var load=new Button{Text="다시 불러오기",AutoSize=true};var check=new Button{Text="검사",AutoSize=true};var save=new Button{Text="저장",AutoSize=true};
        var bar=new FlowLayoutPanel{Dock=DockStyle.Top,Height=38,Padding=new Padding(6,4,6,0)};var arrange=new Button{Text="자동 배치",AutoSize=true};bar.Controls.AddRange([load,check,save,arrange,back,preview,where,status]);
        var left=new Panel{Dock=DockStyle.Fill};var leftButtons=new FlowLayoutPanel{Dock=DockStyle.Bottom,Height=70,Padding=new Padding(0,4,0,0)};
        var addNpc=new Button{Text="NPC 추가",AutoSize=true};var addQuest=new Button{Text="퀘스트 추가",AutoSize=true};var remove=new Button{Text="삭제",AutoSize=true};
        leftButtons.Controls.AddRange([addNpc,addQuest,remove]);left.Controls.Add(tree);left.Controls.Add(leftButtons);
        var right=new Panel{Dock=DockStyle.Fill,AutoScroll=true};right.Controls.Add(props);
        var inner=new SplitContainer{Dock=DockStyle.Fill,FixedPanel=FixedPanel.Panel2};inner.Panel1.Controls.Add(canvas);inner.Panel2.Controls.Add(right);
        var outer=new SplitContainer{Dock=DockStyle.Fill,FixedPanel=FixedPanel.Panel1};outer.Panel1.Controls.Add(left);outer.Panel2.Controls.Add(inner);
        var vertical=new SplitContainer{Dock=DockStyle.Fill,Orientation=Orientation.Horizontal,FixedPanel=FixedPanel.Panel2};vertical.Panel1.Controls.Add(outer);
        var errorBox=new GroupBox{Text="검사 결과",Dock=DockStyle.Fill};errorBox.Controls.Add(errors);vertical.Panel2.Controls.Add(errorBox);
        Controls.Add(vertical);Controls.Add(bar);
        Shown+=async(_,_)=>{outer.SplitterDistance=240;inner.SplitterDistance=Math.Max(400,inner.Width-380);vertical.SplitterDistance=Math.Max(300,vertical.Height-130);if(!review)await Load();};
        FormClosing+=(_,e)=>{if(dirty&&MessageBox.Show(this,"저장하지 않은 변경이 있습니다. 닫을까요?",Text,MessageBoxButtons.YesNo)!=DialogResult.Yes)e.Cancel=true;};
        FormClosed+=(_,_)=>http.Dispose();

        load.Click+=async(_,_)=>{if(!dirty||MessageBox.Show(this,"저장하지 않은 변경을 버리고 다시 불러올까요?",Text,MessageBoxButtons.YesNo)==DialogResult.Yes)await Load();};
        check.Click+=async(_,_)=>await Check();save.Click+=async(_,_)=>await Save([]);
        // Re-places the boxes of the shown quest or conversation (positions are saved with the graph).
        arrange.Click+=(_,_)=>
        {
            if(talk is not null){foreach(var n in talk.Nodes)n.X=n.Y=0;QgGraph.Layout(talk.Nodes,talk.Start,n=>n.Id,n=>n.IsChoice?n.Options!.Select(o=>o.Next):new[]{n.Next},n=>n.X,n=>n.Y,(n,x,y)=>{n.X=x;n.Y=y;},310,150);}
            else if(quest is not null){foreach(var s in quest.Steps)s.X=s.Y=0;QgGraph.Layout(quest.Steps,quest.Start,s=>s.Id,s=>s.Next.Select(r=>(string?)r.Step),s=>s.X,s=>s.Y,(s,x,y)=>{s.X=x;s.Y=y;});}
            else return;
            Changed();
        };
        back.Click+=(_,_)=>ShowQuest(quest);preview.Click+=(_,_)=>{if(talk is not null)Preview(talk,canvas.Selected as QgNode);};
        tree.AfterSelect+=(_,e)=>
        {
            switch(e.Node?.Tag){case QgQuest q:npc=null;ShowQuest(q);break;case QgNpc n:npc=n;quest=null;talk=null;canvas.ShowQuest(graph,null);UpdateBar();ShowProps();break;}
        };
        addNpc.Click+=(_,_)=>{var n=new QgNpc{Id=QgGraph.NewId(graph.Npcs.Select(x=>x.Id),"npc"),Name="새 NPC"};graph.Npcs.Add(n);Touch();RebuildTree(n);};
        addQuest.Click+=(_,_)=>
        {
            string kind=tree.SelectedNode?.Tag as string??(tree.SelectedNode?.Tag as QgQuest)?.Kind??"Main";if(kind=="Npc")kind="Main";
            var q=new QgQuest{Id=QgGraph.NewId(graph.Quests.Select(x=>x.Id),"quest"),Name="새 퀘스트",Kind=kind,Start="s1",Npc=kind=="Chain"?graph.Npcs.FirstOrDefault()?.Id:null,
                Steps=[new QgStep{Id="s1",Title="첫 단계",Goals=[new QgGoal{Id="play",Metric="Match",Label="경기 1회 플레이"}],X=30,Y=40}]};
            graph.Quests.Add(q);Touch();RebuildTree(q);
        };
        remove.Click+=(_,_)=>
        {
            if(tree.SelectedNode?.Tag is QgQuest q&&Confirm($"퀘스트 '{q.Name}'를 지울까요? 진행 중인 플레이어가 있으면 저장할 때 기록 처리를 묻습니다."))
            {graph.Quests.Remove(q);foreach(var s in q.Steps)if(s.Talk is not null&&graph.Quests.SelectMany(x=>x.Steps).All(x=>x.Talk!=s.Talk))graph.Talks.RemoveAll(t=>t.Id==s.Talk);quest=null;talk=null;Touch();RebuildTree(null);}
            else if(tree.SelectedNode?.Tag is QgNpc n&&Confirm($"NPC '{n.Name}'를 지울까요? 이 NPC의 대화·연계 퀘스트는 검사에서 오류로 표시됩니다."))
            {graph.Npcs.Remove(n);npc=null;Touch();RebuildTree(null);}
        };
        canvas.SelectionChanged+=ShowProps;canvas.Moved+=Touch;
        canvas.Activated+=item=>{if(item is QgStep s)OpenTalk(s);};
        canvas.MenuRequested+=CanvasMenu;
    }

    // ---- Server ----
    async Task<(HttpStatusCode Status,JsonElement Body)> Send(HttpMethod method,string path,object? body)
    {
        using var request=create(method,path,body);using var response=await http.SendAsync(request);var text=await response.Content.ReadAsStringAsync();
        return(response.StatusCode,text.Length==0?default:JsonDocument.Parse(text).RootElement.Clone());
    }
    async Task Load()
    {
        status.Text="불러오는 중…";
        try
        {
            var (code,body)=await Send(HttpMethod.Get,"/api/admin/quest-graph",null);
            if(code!=HttpStatusCode.OK){status.Text=$"불러오지 못했습니다 ({(int)code}).";return;}
            UseGraph(body.GetProperty("graph").Deserialize<QgGraph>(QgGraph.Json)!,body.GetProperty("revision").GetInt64(),
                body.GetProperty("players").EnumerateArray().Where(p=>p.GetProperty("step").ValueKind==JsonValueKind.String)
                    .ToDictionary(p=>(p.GetProperty("quest").GetString()!,p.GetProperty("step").GetString()!),p=>p.GetProperty("count").GetInt32()));
            status.Text=$"불러왔습니다 (버전 {revision}).";
        }
        catch(Exception ex){status.Text="불러오지 못했습니다: "+ex.Message;}
    }
    void UseGraph(QgGraph g,long rev,Dictionary<(string Quest,string Step),int> standing)
    {
        graph=g;revision=rev;players=standing;
        foreach(var q in graph.Quests)QgGraph.Layout(q.Steps,q.Start,s=>s.Id,s=>s.Next.Select(r=>(string?)r.Step),s=>s.X,s=>s.Y,(s,x,y)=>{s.X=x;s.Y=y;});
        foreach(var t in graph.Talks)QgGraph.Layout(t.Nodes,t.Start,n=>n.Id,n=>n.IsChoice?n.Options!.Select(o=>o.Next):new[]{n.Next},n=>n.X,n=>n.Y,(n,x,y)=>{n.X=x;n.Y=y;},310,150);
        dirty=false;quest=null;talk=null;npc=null;errors.Items.Clear();RebuildTree(graph.Quests.FirstOrDefault());
    }
    bool review;
    /// <summary>
    /// For the review screenshots (FeatureChecks --quest-editor-review): shows a graph without a server, on a quest, a
    /// step, or a step's conversation with one option chosen, and captures the window.
    /// </summary>
    internal static Bitmap Review(QgGraph g,Dictionary<(string Quest,string Step),int> standing,string questId,string? stepId,bool openTalk,string? nodeId=null,int option=-1)
    {
        using var form=new QuestGraphEditorForm((_,_,_)=>throw new InvalidOperationException("review")){review=true,StartPosition=FormStartPosition.Manual,Location=new Point(-4000,-4000),ShowInTaskbar=false};
        form.Show();Application.DoEvents();form.UseGraph(g,1,standing);
        var q=g.Quests.First(x=>x.Id==questId);form.RebuildTree(q);Application.DoEvents();
        var s=stepId is null?null:q.Steps.First(x=>x.Id==stepId);
        if(s is not null&&openTalk){form.OpenTalk(s);if(nodeId is not null){form.canvas.Selected=form.talk!.Nodes.First(n=>n.Id==nodeId);form.canvas.SelectedOption=option;form.ShowProps();}}
        else if(s is not null){form.canvas.Selected=s;form.ShowProps();}
        form.errors.Items.Add("(검사 결과 예시) 퀘스트 '미라의 부탁': 단계 '혼자 경기 1회'에서 퀘스트가 끝날 수 없습니다(고리).");
        Application.DoEvents();var bitmap=new Bitmap(form.Width,form.Height);form.DrawToBitmap(bitmap,new Rectangle(Point.Empty,form.Size));form.dirty=false;form.Close();return bitmap;
    }
    async Task Check()
    {
        try
        {
            var (code,body)=await Send(HttpMethod.Post,"/api/admin/quest-graph/check",graph);
            if(code!=HttpStatusCode.OK){status.Text=$"검사하지 못했습니다 ({(int)code}).";return;}
            ShowErrors(body.GetProperty("errors"));status.Text=errors.Items.Count==0?"검사 통과":$"고칠 곳 {errors.Items.Count}개";
        }
        catch(Exception ex){status.Text="검사하지 못했습니다: "+ex.Message;}
    }
    void ShowErrors(JsonElement list){errors.Items.Clear();foreach(var e in list.EnumerateArray())errors.Items.Add(e.GetString()??"");}
    async Task Save(object[] moves)
    {
        try
        {
            var (code,body)=await Send(HttpMethod.Put,"/api/admin/quest-graph",new{revision,graph,moves});
            if(code==HttpStatusCode.OK){revision=body.GetProperty("revision").GetInt64();dirty=false;errors.Items.Clear();status.Text=$"저장했습니다 (버전 {revision}).";return;}
            if(code==HttpStatusCode.BadRequest&&body.ValueKind==JsonValueKind.Object&&body.TryGetProperty("errors",out var list)){ShowErrors(list);status.Text="검사에 걸려 저장하지 않았습니다. 아래 목록을 고치세요.";return;}
            if(code==HttpStatusCode.Conflict&&body.ValueKind==JsonValueKind.Object&&body.TryGetProperty("stranded",out var stranded))
            {
                var chosen=AskMoves(stranded);if(chosen is null){status.Text="저장을 취소했습니다.";return;}
                await Save(chosen);return;
            }
            status.Text=code==HttpStatusCode.Conflict?"다른 곳에서 먼저 저장했습니다. 다시 불러온 뒤 고치세요.":$"저장하지 못했습니다 ({(int)code}).";
        }
        catch(Exception ex){status.Text="저장하지 못했습니다: "+ex.Message;}
    }
    /// <summary>Players stand on steps (or quests) this save removes: where each group goes, or for a removed quest, dropping its progress.</summary>
    object[]? AskMoves(JsonElement stranded)
    {
        using var dialog=new Form{Text="진행 중인 플레이어 옮기기",Width=640,Height=420,StartPosition=FormStartPosition.CenterParent,MinimizeBox=false,MaximizeBox=false};
        var table=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,AutoScroll=true,Padding=new Padding(12)};table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,55));table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,45));
        table.Controls.Add(new Label{Text="저장하면 아래 단계가 사라집니다. 그 단계에 있는 플레이어를 어디로 옮길지 정하세요.",AutoSize=true,MaximumSize=new Size(580,0)},0,0);table.SetColumnSpan(table.GetControlFromPosition(0,0)!,2);
        var picks=new List<(string Quest,string Step,ComboBox? Box,CheckBox? Drop)>();int row=1;
        foreach(var s in stranded.EnumerateArray())
        {
            string q=s.GetProperty("quest").GetString()!,st=s.GetProperty("step").GetString()!;int count=s.GetProperty("count").GetInt32();bool removed=s.TryGetProperty("questRemoved",out var r)&&r.GetBoolean();
            var name=graph.Quests.FirstOrDefault(x=>x.Id==q)?.Name??q;
            table.Controls.Add(new Label{Text=$"{name} · 단계 '{st}' · {count}명",AutoSize=true,Padding=new Padding(0,6,0,0)},0,row);
            if(removed){var drop=new CheckBox{Text="퀘스트를 지우고 진행 기록도 지움",AutoSize=true};table.Controls.Add(drop,1,row);picks.Add((q,st,null,drop));}
            else
            {
                var box=new ComboBox{DropDownStyle=ComboBoxStyle.DropDownList,Width=240};
                foreach(var step in graph.Quests.First(x=>x.Id==q).Steps)box.Items.Add(new Choice(step.Id,$"{step.Title} ({step.Id})"));
                if(box.Items.Count>0)box.SelectedIndex=0;table.Controls.Add(box,1,row);picks.Add((q,st,box,null));
            }
            row++;
        }
        var ok=new Button{Text="옮기고 저장",DialogResult=DialogResult.OK,AutoSize=true};var cancel=new Button{Text="취소",DialogResult=DialogResult.Cancel,AutoSize=true};
        var buttons=new FlowLayoutPanel{Dock=DockStyle.Bottom,Height=40,FlowDirection=FlowDirection.RightToLeft};buttons.Controls.AddRange([cancel,ok]);
        dialog.Controls.Add(table);dialog.Controls.Add(buttons);dialog.AcceptButton=ok;dialog.CancelButton=cancel;
        if(dialog.ShowDialog(this)!=DialogResult.OK)return null;
        if(picks.Any(p=>p.Drop is {Checked:false})){MessageBox.Show(this,"지운 퀘스트의 기록 처리를 확인해야 저장할 수 있습니다.",Text);return null;}
        return picks.Select(p=>(object)new{quest=p.Quest,from=p.Step,to=p.Box is null?"":((Choice)p.Box.SelectedItem!).Id}).ToArray();
    }

    // ---- Tree, views, bar ----
    void Touch(){dirty=true;UpdateBar();}
    void UpdateBar()
    {
        where.Text=(dirty?"* ":"")+(talk is not null&&quest is not null?$"{quest.Name} › {talkStep?.Title} › 대화 ({graph.NpcName(talk.Npc)})":quest is not null?quest.Name:npc is not null?"NPC · "+npc.Name:"");
        back.Visible=preview.Visible=talk is not null;
    }
    void RebuildTree(object? select)
    {
        tree.BeginUpdate();tree.Nodes.Clear();TreeNode? selected=null;
        var npcs=new TreeNode("NPC"){Tag="Npc"};tree.Nodes.Add(npcs);
        foreach(var n in graph.Npcs){var node=new TreeNode($"{n.Name} ({n.Id})"){Tag=n};npcs.Nodes.Add(node);if(n==select)selected=node;}
        foreach(var (kind,name) in QgGraph.Kinds)
        {
            var group=new TreeNode(name+" 퀘스트"){Tag=kind};tree.Nodes.Add(group);
            foreach(var q in graph.Quests.Where(x=>x.Kind==kind))
            {
                var node=new TreeNode(QuestLabel(q)){Tag=q};group.Nodes.Add(node);if(q==select)selected=node;
            }
        }
        tree.ExpandAll();tree.EndUpdate();
        if(selected is not null)tree.SelectedNode=selected;else{canvas.ShowQuest(graph,quest);ShowProps();}
        UpdateBar();
    }
    void RefreshTreeText()
    {
        foreach(TreeNode group in tree.Nodes)foreach(TreeNode node in group.Nodes)
            node.Text=node.Tag switch{QgNpc n=>$"{n.Name} ({n.Id})",QgQuest q=>QuestLabel(q),_=>node.Text};
    }
    string QuestLabel(QgQuest q){var count=players.Where(p=>p.Key.Quest==q.Id).Sum(p=>p.Value);return(q.Npc is null?"":graph.NpcName(q.Npc)+" · ")+q.Name+(count>0?$"  ({count}명 진행 중)":"");}
    void ShowQuest(QgQuest? q)
    {
        quest=q;talk=null;talkStep=null;
        canvas.Players=q is null?new():players.Where(p=>p.Key.Quest==q.Id).ToDictionary(p=>p.Key.Step,p=>p.Value);
        canvas.ShowQuest(graph,q);UpdateBar();ShowProps();
    }
    void OpenTalk(QgStep s)
    {
        if(quest is null)return;var t=graph.TalkOf(s);
        if(t is null)
        {
            if(!Confirm($"단계 '{s.Title}'에 대화가 없습니다. 새 대화를 만들까요?"))return;
            if(graph.Npcs.Count==0)graph.Npcs.Add(new QgNpc{Id="npc1",Name="새 NPC"});
            t=new QgTalk{Id=QgGraph.NewId(graph.Talks.Select(x=>x.Id),"talk"),Npc=quest.Npc??graph.Npcs[0].Id,Start="n1",Nodes=[new QgNode{Id="n1",Text="...",X=30,Y=40}]};
            graph.Talks.Add(t);s.Talk=t.Id;if(s.Goals.All(g=>g.Metric!="Dialogue"))s.Goals.Add(new QgGoal{Id="talk",Metric="Dialogue",Label="대화하기"});Touch();RefreshTreeText();
        }
        talk=t;talkStep=s;canvas.ShowTalk(graph,t);UpdateBar();ShowProps();
    }

    // ---- Canvas menu: add, delete, start, link ----
    void CanvasMenu(Point at,object? item)
    {
        var menu=new ContextMenuStrip();
        if(talk is not null)
        {
            menu.Items.Add("NPC 메시지 추가",null,(_,_)=>{var n=new QgNode{Id=QgGraph.NewId(talk.Nodes.Select(x=>x.Id),"n"),Text="...",X=at.X,Y=at.Y};talk.Nodes.Add(n);Select(n);});
            menu.Items.Add("플레이어 선택 추가",null,(_,_)=>{var n=new QgNode{Id=QgGraph.NewId(talk.Nodes.Select(x=>x.Id),"n"),Options=[new QgOption{Text="선택 1"},new QgOption{Text="선택 2"}],X=at.X,Y=at.Y};talk.Nodes.Add(n);Select(n);});
            if(item is QgNode node)
            {
                menu.Items.Add(new ToolStripSeparator());
                menu.Items.Add("대화 시작 노드로",null,(_,_)=>{talk.Start=node.Id;Changed();});
                if(!node.IsChoice||canvas.SelectedOption>=0)
                {
                    var option=node.IsChoice?node.Options![canvas.SelectedOption]:null;
                    var link=new ToolStripMenuItem(option is null?"다음 대사 연결":"이 선택지 다음 연결");
                    link.DropDownItems.Add("(대화 끝)",null,(_,_)=>{if(option is null)node.Next=null;else option.Next=null;Changed();});
                    foreach(var target in talk.Nodes.Where(x=>x!=node))
                        link.DropDownItems.Add(Short(target),null,(_,_)=>{if(option is null)node.Next=target.Id;else option.Next=target.Id;Changed();});
                    menu.Items.Add(link);
                }
                menu.Items.Add("미리보기 (여기부터)",null,(_,_)=>Preview(talk,node));
                menu.Items.Add("노드 삭제",null,(_,_)=>{if(Confirm("이 노드를 지울까요?")){graph.RemoveNode(talk,node);Select(null);}});
            }
        }
        else if(quest is not null)
        {
            menu.Items.Add("단계 추가",null,(_,_)=>{var s=new QgStep{Id=QgGraph.NewId(quest.Steps.Select(x=>x.Id),"s"),Title="새 단계",Goals=[new QgGoal{Id="play",Metric="Match",Label="경기 1회 플레이"}],X=at.X,Y=at.Y};quest.Steps.Add(s);Select(s);});
            if(item is QgStep step)
            {
                menu.Items.Add(new ToolStripSeparator());
                menu.Items.Add("시작 단계로",null,(_,_)=>{quest.Start=step.Id;Changed();});
                var link=new ToolStripMenuItem("다음 단계 연결 추가");
                foreach(var target in quest.Steps.Where(x=>x!=step))link.DropDownItems.Add($"{target.Title} ({target.Id})",null,(_,_)=>{step.Next.Add(new QgRoute{Step=target.Id});Changed();});
                menu.Items.Add(link);
                menu.Items.Add(graph.TalkOf(step) is null?"대화 만들기":"대화 그래프 열기",null,(_,_)=>OpenTalk(step));
                menu.Items.Add("단계 삭제",null,(_,_)=>
                {
                    var count=players.GetValueOrDefault((quest.Id,step.Id));
                    if(Confirm(count>0?$"이 단계에 {count}명이 있습니다. 지우면 저장할 때 옮길 단계를 묻습니다. 지울까요?":"이 단계를 지울까요?")){graph.RemoveStep(quest,step);Select(null);}
                });
            }
        }
        if(menu.Items.Count>0)menu.Show(canvas,new Point(at.X+canvas.AutoScrollPosition.X,at.Y+canvas.AutoScrollPosition.Y));
    }
    void Select(object? item){canvas.Selected=item;canvas.SelectedOption=-1;Changed();ShowProps();}
    void Changed(){Touch();canvas.Refit();}
    static string Short(QgNode n)=>n.IsChoice?$"{n.Id} · 선택 ({n.Options!.Count}개)":$"{n.Id} · {(n.Text??"").Replace('\n',' ')[..Math.Min((n.Text??"").Length,24)]}";

    // ---- Side panel ----
    void ShowProps()
    {
        props.SuspendLayout();foreach(Control c in props.Controls)c.Dispose();props.Controls.Clear();props.RowStyles.Clear();
        try
        {
            if(npc is not null){NpcProps(npc);return;}
            if(talk is not null){if(canvas.Selected is QgNode n)NodeProps(talk,n);else TalkProps(talk);return;}
            if(quest is not null){if(canvas.Selected is QgStep s)StepProps(quest,s);else QuestProps(quest);return;}
            Note("왼쪽에서 NPC나 퀘스트를 고르세요. 가운데 화면에서 오른쪽 클릭으로 단계·대화를 추가하고 연결합니다.");
        }
        finally{props.ResumeLayout();}
    }
    void Heading(string text)=>props.Controls.Add(new Label{Text=text,AutoSize=true,Font=new Font("Malgun Gothic",11f,FontStyle.Bold),Padding=new Padding(0,4,0,6)});
    void Note(string text)=>props.Controls.Add(new Label{Text=text,AutoSize=true,MaximumSize=new Size(330,0),ForeColor=Color.DimGray,Padding=new Padding(0,0,0,6)});
    void Caption(string text)=>props.Controls.Add(new Label{Text=text,AutoSize=true,MaximumSize=new Size(330,0),Padding=new Padding(0,6,0,2)});
    TextBox Field(string label,string value,Action<string> set,bool multiline=false,int max=400,int height=80)
    {
        Caption(label);var box=new TextBox{Text=value,Width=330,MaxLength=max,Multiline=multiline,AcceptsReturn=multiline,ScrollBars=multiline?ScrollBars.Vertical:ScrollBars.None};
        if(multiline)box.Height=height;box.TextChanged+=(_,_)=>{set(multiline?box.Text.Replace("\r\n","\n"):box.Text);Touch();canvas.Invalidate();};
        if(multiline)box.Text=value.Replace("\n","\r\n");props.Controls.Add(box);return box;
    }
    /// <summary>An id field: applied when leaving it, renaming every reference; a taken or malformed id is put back.</summary>
    void IdField(string label,string value,IEnumerable<string> taken,Action<string,string> rename)
    {
        Caption(label+" (영문 소문자·숫자·_)");var box=new TextBox{Text=value,Width=330,MaxLength=40};var current=value;var others=taken.Where(x=>x!=value).ToHashSet();
        box.Leave+=(_,_)=>
        {
            var next=box.Text.Trim();if(next==current)return;
            if(!IdPattern.IsMatch(next)||others.Contains(next)){MessageBox.Show(this,"id는 영문 소문자·숫자·_ 40자 이내이고 겹치지 않아야 합니다.",Text);box.Text=current;return;}
            rename(current,next);current=next;Touch();RefreshTreeText();canvas.Invalidate();
        };
        props.Controls.Add(box);
    }
    ComboBox Combo(string label,IEnumerable<Choice> items,string? value,Action<string?> set)
    {
        Caption(label);var box=new ComboBox{DropDownStyle=ComboBoxStyle.DropDownList,Width=330};
        foreach(var i in items)box.Items.Add(i);box.SelectedItem=box.Items.Cast<Choice>().FirstOrDefault(i=>i.Id==value);
        box.SelectedIndexChanged+=(_,_)=>{set((box.SelectedItem as Choice)?.Id);Touch();RefreshTreeText();canvas.Refit();};props.Controls.Add(box);return box;
    }
    Button Act(string text,System.Action act){var b=new Button{Text=text,AutoSize=true};b.Click+=(_,_)=>act();props.Controls.Add(b);return b;}

    void NpcProps(QgNpc n)
    {
        Heading("NPC");IdField("id",n.Id,graph.Npcs.Select(x=>x.Id),(a,b)=>graph.RenameNpc(a,b));
        Field("이름 (20자)",n.Name,v=>n.Name=v,max:20);
        var talks=graph.Talks.Where(t=>t.Npc==n.Id).Select(t=>t.Id).ToList();Note(talks.Count==0?"이 NPC의 대화가 아직 없습니다.":"대화: "+string.Join(", ",talks));
    }
    void QuestProps(QgQuest q)
    {
        Heading("퀘스트");IdField("id",q.Id,graph.Quests.Select(x=>x.Id),(a,b)=>graph.RenameQuest(a,b));
        Field("이름 (100자)",q.Name,v=>q.Name=v,max:100);
        Combo("종류",QgGraph.Kinds.Select(k=>new Choice(k.Id,k.Name)),q.Kind,v=>{q.Kind=v??"Main";if(q.Kind!="Chain")q.Npc=null;else q.Npc??=graph.Npcs.FirstOrDefault()?.Id;ShowProps();RebuildTree(q);});
        if(q.Kind=="Chain")Combo("연계 퀘스트의 NPC",graph.Npcs.Select(n=>new Choice(n.Id,n.Name)),q.Npc,v=>q.Npc=v);
        Combo("시작 단계",q.Steps.Select(s=>new Choice(s.Id,$"{s.Title} ({s.Id})")),q.Start,v=>q.Start=v??"");
        Field("기본 이야기 (퀘스트 화면 맨 위)",q.BaseStory,v=>q.BaseStory=v,true,16000,90);
        Caption("여는 조건 (모두 만족하면 열림, 없으면 처음부터)");
        var grid=Grid(330,150);
        grid.Columns.Add(new DataGridViewComboBoxColumn{Name="kind",HeaderText="조건",DataSource=QgGraph.ConditionKinds.Select(k=>new Choice(k.Id,k.Name)).ToList(),DisplayMember="Name",ValueMember="Id",FillWeight=120});
        grid.Columns.Add(new DataGridViewComboBoxColumn{Name="quest",HeaderText="퀘스트",DataSource=new[]{new Choice("","")}.Concat(graph.Quests.Where(x=>x!=q).Select(x=>new Choice(x.Id,x.Name))).ToList(),DisplayMember="Name",ValueMember="Id",FillWeight=100});
        grid.Columns.Add(new DataGridViewTextBoxColumn{Name="value",HeaderText="단계·표시",FillWeight=80});
        foreach(var c in q.Opens)grid.Rows.Add(c.Kind,c.Quest??"",c.Kind=="StepReached"?c.Step:c.Flag);
        void Apply()
        {
            q.Opens=grid.Rows.Cast<DataGridViewRow>().Where(r=>!r.IsNewRow&&r.Cells[0].Value is string).Select(r=>
            {
                var kind=(string)r.Cells[0].Value;var other=r.Cells[1].Value as string;var value=(r.Cells[2].Value as string)?.Trim();
                return new QgCondition{Kind=kind,Quest=kind is "QuestDone" or "StepReached"&&!string.IsNullOrEmpty(other)?other:null,Step=kind=="StepReached"?value:null,Flag=kind is "Flag" or "NoFlag"?value:null};
            }).ToList();Touch();
        }
        grid.CellValueChanged+=(_,_)=>Apply();grid.RowsRemoved+=(_,_)=>Apply();props.Controls.Add(grid);
        Note("'대화에서 받음'은 어떤 대화의 선택지가 이 퀘스트를 줄 때 열립니다. 가운데 화면에서 오른쪽 클릭으로 단계를 추가하세요.");
    }
    void StepProps(QgQuest q,QgStep s)
    {
        Heading("단계");IdField("id",s.Id,q.Steps.Select(x=>x.Id),(a,b)=>graph.RenameStep(q,a,b));
        Field("제목 (180자)",s.Title,v=>s.Title=v,max:180);
        Caption("목표 (1~4개)");var goals=Grid(330,120);
        goals.Columns.Add(new DataGridViewComboBoxColumn{Name="metric",HeaderText="조건",DataSource=QgGraph.MetricNames.Select(m=>new Choice(m.Key,m.Value)).ToList(),DisplayMember="Name",ValueMember="Id",FillWeight=110});
        goals.Columns.Add(new DataGridViewTextBoxColumn{Name="label",HeaderText="표시 문구",FillWeight=130});goals.Columns.Add(new DataGridViewTextBoxColumn{Name="target",HeaderText="횟수",FillWeight=45});
        foreach(var g in s.Goals)goals.Rows.Add(g.Metric,g.Label,g.Target.ToString());
        void ApplyGoals()
        {
            var rows=goals.Rows.Cast<DataGridViewRow>().Where(r=>!r.IsNewRow&&r.Cells[0].Value is string).ToList();
            s.Goals=rows.Select((r,i)=>new QgGoal{Id=s.Goals.ElementAtOrDefault(i)?.Id??"goal"+(i+1),Metric=(string)r.Cells[0].Value,Label=(r.Cells[1].Value as string)??"",Target=int.TryParse(r.Cells[2].Value as string,out var t)?t:1}).ToList();
            for(int i=0;i<s.Goals.Count;i++)if(s.Goals.Take(i).Any(g=>g.Id==s.Goals[i].Id))s.Goals[i].Id="goal"+(i+1);
            Touch();canvas.Invalidate();
        }
        goals.CellValueChanged+=(_,_)=>ApplyGoals();goals.RowsRemoved+=(_,_)=>ApplyGoals();props.Controls.Add(goals);
        Field("단계를 마치면 공개할 이야기",s.Story,v=>s.Story=v,true,16000,90);
        var t=graph.TalkOf(s);Caption(t is null?"대화: 없음":$"대화: {t.Id} ({graph.NpcName(t.Npc)}, 노드 {t.Nodes.Count}개)");
        Act(t is null?"대화 만들기":"대화 그래프 열기",()=>OpenTalk(s));
        if(t is not null)Act("대화 연결 끊기",()=>{if(Confirm("이 단계에서 대화를 떼어 낼까요? 대화 내용도 지워집니다.")){s.Talk=null;if(graph.Quests.SelectMany(x=>x.Steps).All(x=>x.Talk!=t.Id))graph.Talks.Remove(t);s.Goals.RemoveAll(g=>g.Metric=="Dialogue");Changed();ShowProps();}});
        Caption("다음 단계 (위에서부터 처음 맞는 곳으로, 마지막은 조건 없이; 없으면 퀘스트 끝)");
        var routes=Grid(330,140);
        routes.Columns.Add(new DataGridViewComboBoxColumn{Name="step",HeaderText="다음 단계",DataSource=q.Steps.Where(x=>x!=s).Select(x=>new Choice(x.Id,x.Title)).ToList(),DisplayMember="Name",ValueMember="Id",FillWeight=120});
        routes.Columns.Add(new DataGridViewTextBoxColumn{Name="flag",HeaderText="이야기 표시",FillWeight=90});routes.Columns.Add(new DataGridViewCheckBoxColumn{Name="without",HeaderText="없을 때",FillWeight=50});
        foreach(var r in s.Next)routes.Rows.Add(r.Step,r.Flag,r.Without);
        void ApplyRoutes()
        {
            s.Next=routes.Rows.Cast<DataGridViewRow>().Where(r=>!r.IsNewRow&&r.Cells[0].Value is string).Select(r=>new QgRoute{Step=(string)r.Cells[0].Value,
                Flag=string.IsNullOrWhiteSpace(r.Cells[1].Value as string)?null:((string)r.Cells[1].Value).Trim(),Without=r.Cells[2].Value is true}).ToList();Changed();
        }
        routes.CellValueChanged+=(_,_)=>ApplyRoutes();routes.RowsRemoved+=(_,_)=>ApplyRoutes();routes.CurrentCellDirtyStateChanged+=(_,_)=>{if(routes.IsCurrentCellDirty)routes.CommitEdit(DataGridViewDataErrorContexts.Commit);};
        props.Controls.Add(routes);
        Act("선택한 연결을 위로",()=>{var i=routes.CurrentRow?.Index??-1;if(i>0&&i<s.Next.Count){(s.Next[i-1],s.Next[i])=(s.Next[i],s.Next[i-1]);Changed();ShowProps();}});
        if(players.TryGetValue((q.Id,s.Id),out var n)&&n>0)Note($"지금 {n}명이 이 단계를 진행 중입니다.");
    }
    void TalkProps(QgTalk t)
    {
        Heading("대화");IdField("id",t.Id,graph.Talks.Select(x=>x.Id),(a,b)=>graph.RenameTalk(a,b));
        Combo("NPC",graph.Npcs.Select(n=>new Choice(n.Id,n.Name)),t.Npc,v=>t.Npc=v??"");
        Combo("시작 노드",t.Nodes.Select(n=>new Choice(n.Id,Short(n))),t.Start,v=>t.Start=v??"");
        Note("가운데 화면에서 오른쪽 클릭으로 NPC 메시지·플레이어 선택을 추가하고, 노드를 오른쪽 클릭해 다음 노드를 연결합니다. 선택지 줄을 누르면 그 선택지를 고칩니다.");
        Act("처음부터 미리보기",()=>Preview(t,null));
    }
    void NodeProps(QgTalk t,QgNode n)
    {
        IEnumerable<Choice> Targets()=>new[]{new Choice("","(대화 끝)")}.Concat(t.Nodes.Where(x=>x!=n).Select(x=>new Choice(x.Id,Short(x))));
        if(!n.IsChoice)
        {
            Heading("NPC 메시지");IdField("노드 id",n.Id,t.Nodes.Select(x=>x.Id),(a,b)=>graph.RenameNode(t,a,b));
            Field("대사 (400자)",n.Text??"",v=>n.Text=v,true,400,80);
            Combo("다음",Targets(),n.Next??"",v=>n.Next=string.IsNullOrEmpty(v)?null:v);
            return;
        }
        Heading("플레이어 선택");IdField("노드 id",n.Id,t.Nodes.Select(x=>x.Id),(a,b)=>graph.RenameNode(t,a,b));
        Note($"선택지 {n.Options!.Count}개 (1~4개). 가운데 화면에서 선택지 줄을 누르면 그 선택지를 고칩니다.");
        if(n.Options.Count<4)Act("선택지 추가",()=>{n.Options.Add(new QgOption{Text=$"선택 {n.Options.Count+1}"});canvas.SelectedOption=n.Options.Count-1;Changed();ShowProps();});
        int i=canvas.SelectedOption;if(i<0||i>=n.Options.Count)return;var o=n.Options[i];
        Caption($"― 선택지 {i+1} ―");
        Field("보내는 말 (120자)",o.Text,v=>o.Text=v,max:120);
        Field("NPC 답장 (한 줄에 하나, 6줄까지)",string.Join("\n",o.Replies),v=>o.Replies=v.Split('\n').Select(x=>x.Trim()).Where(x=>x.Length>0).ToList(),true,2400,90);
        Field("남길 이야기 표시 (쉼표로 구분)",string.Join(", ",o.Flags??[]),v=>{var f=v.Split(',').Select(x=>x.Trim()).Where(x=>x.Length>0).ToList();o.Flags=f.Count==0?null:f;});
        Combo("이 선택으로 주는 퀘스트",new[]{new Choice("","(없음)")}.Concat(graph.Quests.Where(q=>q.Opens.Any(c=>c.Kind=="Offered")).Select(q=>new Choice(q.Id,q.Name))),o.Gives??"",v=>o.Gives=string.IsNullOrEmpty(v)?null:v);
        Combo("다음",Targets(),o.Next??"",v=>o.Next=string.IsNullOrEmpty(v)?null:v);
        if(n.Options.Count>1)Act("이 선택지 삭제",()=>{n.Options.RemoveAt(i);canvas.SelectedOption=-1;Changed();ShowProps();});
    }
    static DataGridView Grid(int width,int height)=>new(){Width=width,Height=height,AutoGenerateColumns=false,AllowUserToAddRows=true,AllowUserToDeleteRows=true,
        AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill,RowHeadersVisible=false,EditMode=DataGridViewEditMode.EditOnEnter};

    /// <summary>Plays a conversation from a node like the phone: NPC lines, then option buttons that follow their links.</summary>
    void Preview(QgTalk t,QgNode? from)
    {
        using var dialog=new Form{Text="대화 미리보기 · "+graph.NpcName(t.Npc),Width=520,Height=600,StartPosition=FormStartPosition.CenterParent};
        var log=new TextBox{Dock=DockStyle.Fill,Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical,Font=new Font("Malgun Gothic",10f)};
        var buttons=new FlowLayoutPanel{Dock=DockStyle.Bottom,Height=120,AutoScroll=true,FlowDirection=FlowDirection.TopDown,WrapContents=false};
        dialog.Controls.Add(log);dialog.Controls.Add(buttons);var name=graph.NpcName(t.Npc);int guard=0;
        void Say(string who,string text)=>log.AppendText($"{who}  {text}\r\n\r\n");
        void Show(string? id)
        {
            foreach(Control c in buttons.Controls)c.Dispose();buttons.Controls.Clear();
            var n=id is null?null:t.Nodes.FirstOrDefault(x=>x.Id==id);
            if(n is null||++guard>500){Say("―","대화 끝");buttons.Controls.Add(new Button{Text="닫기",AutoSize=true,DialogResult=DialogResult.OK});return;}
            if(!n.IsChoice){Say(name,n.Text??"");var next=new Button{Text="다음",AutoSize=true};next.Click+=(_,_)=>Show(n.Next);buttons.Controls.Add(next);return;}
            foreach(var o in n.Options!)
            {
                var label=o.Text+(o.Flags is {Count:>0}?$"   [표시 {string.Join(",",o.Flags)}]":"")+(o.Gives is null?"":$"   [퀘스트 {o.Gives}]");
                var b=new Button{Text=label,AutoSize=true,MaximumSize=new Size(470,0)};
                b.Click+=(_,_)=>{Say("나",o.Text);foreach(var r in o.Replies)Say(name,r);Show(o.Next);};buttons.Controls.Add(b);
            }
        }
        Show(from?.Id??t.Start);dialog.ShowDialog(this);
    }
    bool Confirm(string text)=>MessageBox.Show(this,text,Text,MessageBoxButtons.YesNo)==DialogResult.Yes;
    sealed record Choice(string Id,string Name){public override string ToString()=>Name;}
}

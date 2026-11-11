using System.Text.Json;
using System.Text.Json.Serialization;
namespace ProjectProject01BuildServerHelper;

// The quest graph as the 운영 도우미 edits it (Docs/QuestGraph.md). Same JSON as the server's QuestGraph records,
// but mutable: the editor changes it in place and sends the whole graph back. The server checks it again on save.
internal sealed class QgNpc{public string Id{get;set;}="";public string Name{get;set;}="";}
internal sealed class QgCondition{public string Kind{get;set;}="QuestDone";public string? Quest{get;set;}public string? Step{get;set;}public string? Flag{get;set;}}
internal sealed class QgGoal{public string Id{get;set;}="";public string Metric{get;set;}="Match";public string Label{get;set;}="";public int Target{get;set;}=1;}
internal sealed class QgRoute{public string Step{get;set;}="";public string? Flag{get;set;}public bool Without{get;set;}}
internal sealed class QgStep
{
    public string Id{get;set;}="";public string Title{get;set;}="";public List<QgGoal> Goals{get;set;}=new();public string Story{get;set;}="";
    public string? Talk{get;set;}public List<QgRoute> Next{get;set;}=new();public double X{get;set;}public double Y{get;set;}
}
internal sealed class QgQuest
{
    public string Id{get;set;}="";public string Name{get;set;}="";public string Kind{get;set;}="Main";public List<QgCondition> Opens{get;set;}=new();
    public string Start{get;set;}="";public List<QgStep> Steps{get;set;}=new();public string BaseStory{get;set;}="";public string? Npc{get;set;}
}
internal sealed class QgOption
{
    public string Text{get;set;}="";public List<string> Replies{get;set;}=new();public List<string>? Flags{get;set;}public string? Gives{get;set;}public string? Next{get;set;}
}
internal sealed class QgNode
{
    public string Id{get;set;}="";public string? Text{get;set;}public string? Next{get;set;}public List<QgOption>? Options{get;set;}public double X{get;set;}public double Y{get;set;}
    [JsonIgnore]public bool IsChoice=>Options is not null;
}
internal sealed class QgTalk{public string Id{get;set;}="";public string Npc{get;set;}="";public string Start{get;set;}="";public List<QgNode> Nodes{get;set;}=new();}
internal sealed class QgGraph
{
    public List<QgNpc> Npcs{get;set;}=new();public List<QgQuest> Quests{get;set;}=new();public List<QgTalk> Talks{get;set;}=new();

    internal static readonly JsonSerializerOptions Json=new(JsonSerializerDefaults.Web){DefaultIgnoreCondition=JsonIgnoreCondition.WhenWritingNull};
    internal static readonly (string Id,string Name)[] Kinds=[("Main","메인"),("Chain","연계"),("Side","서브")];
    internal static readonly (string Id,string Name)[] ConditionKinds=[("QuestDone","다른 퀘스트 완료"),("StepReached","다른 퀘스트의 단계 도달"),("Flag","이야기 표시 있음"),("NoFlag","이야기 표시 없음"),("Offered","대화에서 받음")];
    internal static readonly Dictionary<string,string> MetricNames=new()
    {
        ["SurvivorTutorial"]="생존자 튜토리얼",["MannequinTutorial"]="마네킹 튜토리얼",["Dialogue"]="대화",["Match"]="경기 시작",["SurvivorMatch"]="생존자로 경기 시작",
        ["ThirdFloor"]="3층 탈출",["SecondFloor"]="2층 탈출",["Escape"]="최종 탈출",["Capture"]="생존자 붙잡기",["MatchComplete"]="경기 완료",["MannequinWin"]="마네킹 승리",
    };

    internal QgGraph Clone()=>JsonSerializer.Deserialize<QgGraph>(JsonSerializer.Serialize(this,Json),Json)!;
    internal QgTalk? TalkOf(QgStep s)=>s.Talk is null?null:Talks.FirstOrDefault(t=>t.Id==s.Talk);
    internal string NpcName(string? id)=>Npcs.FirstOrDefault(n=>n.Id==id)?.Name??id??"";
    internal static string NewId(IEnumerable<string> taken,string prefix){var set=taken.ToHashSet();for(int i=1;;i++)if(!set.Contains(prefix+i))return prefix+i;}

    // ---- Renaming keeps every reference pointing at the same thing. ----
    internal void RenameQuest(string from,string to)
    {
        foreach(var q in Quests){if(q.Id==from)q.Id=to;foreach(var c in q.Opens)if(c.Quest==from)c.Quest=to;}
        foreach(var o in Talks.SelectMany(t=>t.Nodes).SelectMany(n=>n.Options??[]))if(o.Gives==from)o.Gives=to;
    }
    internal void RenameStep(QgQuest quest,string from,string to)
    {
        foreach(var s in quest.Steps){if(s.Id==from)s.Id=to;foreach(var r in s.Next)if(r.Step==from)r.Step=to;}
        if(quest.Start==from)quest.Start=to;
        foreach(var c in Quests.SelectMany(q=>q.Opens))if(c.Quest==quest.Id&&c.Step==from)c.Step=to;
    }
    internal void RenameTalk(string from,string to)
    {
        foreach(var t in Talks)if(t.Id==from)t.Id=to;foreach(var s in Quests.SelectMany(q=>q.Steps))if(s.Talk==from)s.Talk=to;
    }
    internal void RenameNode(QgTalk talk,string from,string to)
    {
        foreach(var n in talk.Nodes){if(n.Id==from)n.Id=to;if(n.Next==from)n.Next=to;foreach(var o in n.Options??[])if(o.Next==from)o.Next=to;}
        if(talk.Start==from)talk.Start=to;
    }
    internal void RenameNpc(string from,string to)
    {
        foreach(var n in Npcs)if(n.Id==from)n.Id=to;foreach(var t in Talks)if(t.Npc==from)t.Npc=to;foreach(var q in Quests)if(q.Npc==from)q.Npc=to;
    }
    // ---- Removing cuts the links that pointed at it (the checks then show anything left dangling). ----
    internal void RemoveStep(QgQuest quest,QgStep step)
    {
        quest.Steps.Remove(step);foreach(var s in quest.Steps)s.Next.RemoveAll(r=>r.Step==step.Id);
        if(quest.Start==step.Id)quest.Start=quest.Steps.FirstOrDefault()?.Id??"";
        if(step.Talk is not null&&Quests.SelectMany(q=>q.Steps).All(s=>s.Talk!=step.Talk))Talks.RemoveAll(t=>t.Id==step.Talk);
    }
    internal void RemoveNode(QgTalk talk,QgNode node)
    {
        talk.Nodes.Remove(node);
        foreach(var n in talk.Nodes){if(n.Next==node.Id)n.Next=null;foreach(var o in n.Options??[])if(o.Next==node.Id)o.Next=null;}
        if(talk.Start==node.Id)talk.Start=talk.Nodes.FirstOrDefault()?.Id??"";
    }

    /// <summary>
    /// Places boxes that were never placed (all at 0,0) in columns by distance from the start. Long chains wrap into
    /// bands of a few columns, so a line like 탐사 (10 steps) fits on screen instead of one very wide row.
    /// </summary>
    internal static void Layout<T>(List<T> items,string start,Func<T,string> id,Func<T,IEnumerable<string?>> next,Func<T,double> x,Func<T,double> y,Action<T,double,double> place,double columnWidth=290,double rowHeight=100,int bandColumns=2)
    {
        if(items.Count==0||items.Any(i=>x(i)!=0||y(i)!=0))return;
        var byId=items.GroupBy(id).ToDictionary(g=>g.Key,g=>g.First());var depth=new Dictionary<string,int>();var queue=new Queue<string>();
        if(byId.ContainsKey(start)){depth[start]=0;queue.Enqueue(start);}
        while(queue.Count>0){var at=queue.Dequeue();foreach(var n in next(byId[at]))if(n is not null&&byId.ContainsKey(n)&&!depth.ContainsKey(n)){depth[n]=depth[at]+1;queue.Enqueue(n);}}
        int extra=depth.Count==0?0:depth.Values.Max()+1;foreach(var i in items)if(!depth.ContainsKey(id(i)))depth[id(i)]=extra;
        var columns=items.GroupBy(i=>depth[id(i)]).OrderBy(c=>c.Key).ToList();double bandTop=40;
        foreach(var band in columns.GroupBy(c=>c.Key/bandColumns))
        {
            int tallest=0;
            foreach(var column in band){int row=0;foreach(var i in column)place(i,30+column.Key%bandColumns*columnWidth,bandTop+row++*rowHeight);tallest=Math.Max(tallest,row);}
            bandTop+=tallest*rowHeight+40;
        }
    }
}

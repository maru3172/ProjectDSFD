using System.Text.Json;
using System.Text.RegularExpressions;

/// <summary>
/// Quests and phone conversations as graphs (Docs/QuestGraph.md): several main lines and side quests, NPCs, story flags
/// set by dialogue choices, step routes picked by flags, and quests opened by conditions or handed out in a conversation.
/// This file is the data model, the checks and the default graph (the original 탐사 line); QuestProgress keeps players.
/// </summary>
internal static class QuestGraph
{
    // Kind: "Main" or "Side". Conditions must all hold for the quest to open; none = open from the start.
    internal sealed record Npc(string Id,string Name);
    // Kind: QuestDone (Quest), StepReached (Quest, Step), Flag (Flag), NoFlag (Flag), Offered (handed out by a talk option).
    internal sealed record Condition(string Kind,string? Quest=null,string? Step=null,string? Flag=null);
    internal sealed record Goal(string Id,string Metric,string Label,int Target=1);
    // Routes are tried in order; a route with a flag is taken when the player has it (or lacks it, with Without).
    // The last route usually has no flag. No route at all ends the quest.
    internal sealed record Route(string Step,string? Flag=null,bool Without=false);
    internal sealed record Step(string Id,string Title,Goal[] Goals,string Story="",string? Talk=null,Route[]? Next=null,double X=0,double Y=0);
    // Kind: "Main", "Chain" (an NPC's own line, Npc set) or "Side".
    internal sealed record Quest(string Id,string Name,string Kind,Condition[] Opens,string Start,Step[] Steps,string BaseStory="",string? Npc=null);
    // A node is an NPC message (Text, Next) or a choice (Options). An option is what the player sends, the NPC's replies,
    // the story flags it leaves, a quest it hands out, and where the conversation goes next (none: it ends).
    internal sealed record Option(string Text,string[] Replies,string[]? Flags=null,string? Gives=null,string? Next=null);
    internal sealed record Node(string Id,string? Text=null,string? Next=null,Option[]? Options=null,double X=0,double Y=0);
    internal sealed record Talk(string Id,string Npc,string Start,Node[] Nodes);
    internal sealed record Graph(Npc[] Npcs,Quest[] Quests,Talk[] Talks);

    internal const string MainLine="main_explore";
    internal static readonly JsonSerializerOptions Json=new(JsonSerializerDefaults.Web){DefaultIgnoreCondition=System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull};
    /// <summary>Game events a goal can count (the server only receives these).</summary>
    internal static readonly string[] Metrics=["SurvivorTutorial","MannequinTutorial","Dialogue","Match","SurvivorMatch","ThirdFloor","SecondFloor","Escape","Capture","MatchComplete","MannequinWin"];
    internal static readonly string[] ConditionKinds=["QuestDone","StepReached","Flag","NoFlag","Offered"];
    static readonly Regex IdPattern=new("^[a-z0-9_]{1,40}$",RegexOptions.Compiled);
    internal static bool IsTutorialStep(Step s)=>s.Goals.Length>0&&s.Goals.All(g=>g.Metric is "SurvivorTutorial" or "MannequinTutorial");

    // ---- The default graph: the original 탐사 line, one talk per talk step, node ids n1..nN in line order. ----
    internal static Talk FromLegacy(string id,MainQuest.Talk talk)
    {
        var nodes=new List<Node>();
        for(int i=0;i<talk.Lines.Length;i++)
        {
            string? next=i+1<talk.Lines.Length?$"n{i+2}":null;var line=talk.Lines[i];
            nodes.Add(line.Options is null?new Node($"n{i+1}",line.Text,next):new Node($"n{i+1}",Options:[..line.Options.Select(o=>new Option(o.Text,o.Replies,Next:next))]));
        }
        return new(id,NpcId(talk.Npc),"n1",[..nodes]);
    }
    internal static string NpcId(string name)=>name switch{"캘린"=>"kalin","아테아"=>"atea",_=>"npc_"+name.Aggregate(17u,(h,c)=>h*31+c)%100000u};
    internal static Graph FromPlan(MainQuest.Plan plan)
    {
        var talks=plan.Steps.Where(s=>s.Talk is not null).Select(s=>FromLegacy(s.Id,s.Talk!)).ToArray();
        var npcs=plan.Steps.Where(s=>s.Talk is not null).Select(s=>new Npc(NpcId(s.Talk!.Npc),s.Talk!.Npc)).DistinctBy(n=>n.Id).ToArray();
        var steps=plan.Steps.Select((s,i)=>new Step(s.Id,s.Title,[..s.Goals.Select(g=>new Goal(g.Id,g.Metric,g.Label,g.Target))],s.Story,
            s.Talk is null?null:s.Id,i+1<plan.Steps.Length?[new Route(plan.Steps[i+1].Id)]:[])).ToArray();
        return new(npcs,[new Quest(MainLine,plan.Name,"Main",[],steps[0].Id,steps,plan.BaseStory)],talks);
    }
    internal static readonly Graph Default=FromPlan(MainQuest.Default);

    // ---- Checks (the editor shows these; the server refuses a graph with any). ----
    internal static List<string> Validate(Graph? g)
    {
        var errors=new List<string>();void E(string m){if(errors.Count<200)errors.Add(m);}
        if(g is null||g.Npcs is null||g.Quests is null||g.Talks is null){E("그래프가 비어 있습니다.");return errors;}
        if(g.Quests.Length is 0 or >200)E("퀘스트는 1~200개여야 합니다.");if(g.Talks.Length>500)E("대화는 500개 이하여야 합니다.");if(g.Npcs.Length>100)E("NPC는 100명 이하여야 합니다.");
        bool Id(string? id)=>id is not null&&IdPattern.IsMatch(id);
        bool Text(string? t,int max)=>!string.IsNullOrWhiteSpace(t)&&t.Length<=max;
        foreach(var dup in g.Npcs.GroupBy(n=>n?.Id).Where(x=>x.Count()>1))E($"NPC id '{dup.Key}'가 겹칩니다.");
        foreach(var dup in g.Quests.GroupBy(q=>q?.Id).Where(x=>x.Count()>1))E($"퀘스트 id '{dup.Key}'가 겹칩니다.");
        foreach(var dup in g.Talks.GroupBy(t=>t?.Id).Where(x=>x.Count()>1))E($"대화 id '{dup.Key}'가 겹칩니다.");
        var npcs=g.Npcs.Where(n=>n is not null).Select(n=>n.Id).ToHashSet();
        var talks=g.Talks.Where(t=>t is not null).GroupBy(t=>t.Id??"").ToDictionary(x=>x.Key,x=>x.First());
        var quests=g.Quests.Where(q=>q is not null).GroupBy(q=>q.Id??"").ToDictionary(x=>x.Key,x=>x.First());
        foreach(var n in g.Npcs){if(n is null||!Id(n.Id)||!Text(n.Name,20))E($"NPC '{n?.Id}': id(영문 소문자·숫자·_ 40자)와 이름(20자)을 확인하세요.");}
        // Flags anything can leave; a route or condition on a flag nobody leaves can never be taken.
        var setFlags=g.Talks.Where(t=>t?.Nodes is not null).SelectMany(t=>t.Nodes).Where(n=>n?.Options is not null).SelectMany(n=>n.Options!).Where(o=>o?.Flags is not null).SelectMany(o=>o.Flags!).ToHashSet();
        var given=g.Talks.Where(t=>t?.Nodes is not null).SelectMany(t=>t.Nodes).Where(n=>n?.Options is not null).SelectMany(n=>n.Options!).Where(o=>o?.Gives is not null).Select(o=>o.Gives!).ToHashSet();
        var usedTalks=new Dictionary<string,string>();
        foreach(var q in g.Quests)
        {
            if(q is null){E("빈 퀘스트가 있습니다.");continue;}
            string Q=$"퀘스트 '{q.Name}'";
            if(!Id(q.Id))E($"{Q}: id를 확인하세요.");if(!Text(q.Name,100))E($"{Q}: 이름은 100자 이내로 입력하세요.");
            if(q.Kind is not("Main" or "Chain" or "Side"))E($"{Q}: 종류는 메인·연계·서브 중 하나입니다.");if(q.Kind=="Chain"!=(q.Npc is not null)||q.Npc is not null&&!npcs.Contains(q.Npc))E($"{Q}: 연계 퀘스트는 NPC를 하나 정해야 하고, 다른 종류는 NPC를 두지 않습니다.");if(q.BaseStory is null||q.BaseStory.Length>16000)E($"{Q}: 기본 이야기는 16000자 이내입니다.");
            if(q.Steps is null||q.Steps.Length is 0 or >100){E($"{Q}: 단계는 1~100개여야 합니다.");continue;}
            foreach(var dup in q.Steps.GroupBy(s=>s?.Id).Where(x=>x.Count()>1))E($"{Q}: 단계 id '{dup.Key}'가 겹칩니다.");
            var steps=q.Steps.Where(s=>s is not null).GroupBy(s=>s.Id??"").ToDictionary(x=>x.Key,x=>x.First());
            if(!steps.ContainsKey(q.Start??""))E($"{Q}: 시작 단계가 없습니다.");
            foreach(var c in q.Opens??[])
            {
                if(c is null||!ConditionKinds.Contains(c.Kind)){E($"{Q}: 여는 조건 종류를 확인하세요.");continue;}
                if(c.Kind is "QuestDone" or "StepReached"&&(c.Quest is null||!quests.ContainsKey(c.Quest)||c.Quest==q.Id))E($"{Q}: 조건의 퀘스트를 확인하세요.");
                if(c.Kind=="StepReached"&&(c.Quest is null||!quests.TryGetValue(c.Quest,out var other)||other.Steps?.All(s=>s?.Id!=c.Step)!=false))E($"{Q}: 조건의 단계를 확인하세요.");
                if(c.Kind is "Flag" or "NoFlag"&&(!Id(c.Flag)||c.Kind=="Flag"&&!setFlags.Contains(c.Flag!)))E($"{Q}: 조건의 이야기 표시 '{c.Flag}'를 남기는 선택지가 없습니다.");
            }
            if(q.Opens?.Any(c=>c?.Kind=="Offered")==true&&!given.Contains(q.Id))E($"{Q}: '대화에서 받음' 조건인데 이 퀘스트를 주는 선택지가 없습니다.");
            foreach(var s in q.Steps)
            {
                if(s is null){E($"{Q}: 빈 단계가 있습니다.");continue;}
                string S=$"{Q} · 단계 '{s.Title}'";
                if(!Id(s.Id))E($"{S}: id를 확인하세요.");if(!Text(s.Title,180))E($"{S}: 제목은 180자 이내입니다.");if(s.Story is null||s.Story.Length>16000)E($"{S}: 이야기는 16000자 이내입니다.");
                if(s.Goals is null||s.Goals.Length is 0 or >4)E($"{S}: 목표는 1~4개여야 합니다.");
                else foreach(var goal in s.Goals)if(goal is null||!Id(goal.Id)||!Metrics.Contains(goal.Metric)||!Text(goal.Label,120)||goal.Target is <1 or >100)E($"{S}: 목표(종류·문구 120자·횟수 1~100)를 확인하세요.");
                if(s.Goals?.Any(x=>x?.Metric=="Dialogue")==true&&s.Talk is null)E($"{S}: 대화 목표가 있는데 대화가 연결되지 않았습니다.");
                if(s.Talk is not null)
                {
                    if(!talks.ContainsKey(s.Talk))E($"{S}: 연결된 대화가 없습니다.");
                    else if(usedTalks.TryGetValue(s.Talk,out var owner))E($"{S}: 대화 '{s.Talk}'는 이미 {owner}에 연결되어 있습니다.");
                    else usedTalks[s.Talk]=S;
                }
                var next=s.Next??[];if(next.Length>6)E($"{S}: 다음 단계 연결은 6개 이하입니다.");
                foreach(var r in next)
                {
                    if(r is null||!steps.ContainsKey(r.Step??"")){E($"{S}: 다음 단계를 확인하세요.");continue;}
                    if(r.Flag is not null&&(!Id(r.Flag)||!r.Without&&!setFlags.Contains(r.Flag)))E($"{S}: 연결 조건의 이야기 표시 '{r.Flag}'를 남기는 선택지가 없습니다.");
                }
                if(next.Length>0&&next[^1]?.Flag is not null)E($"{S}: 마지막 연결은 조건 없이 두어야 아무 표시가 없는 플레이어도 다음으로 갑니다.");
            }
            // Every step is reachable from the start, and from every step the quest can still end (no closed loop).
            if(steps.ContainsKey(q.Start??""))
            {
                var seen=new HashSet<string>();var queue=new Queue<string>([q.Start!]);
                while(queue.Count>0){var id=queue.Dequeue();if(!seen.Add(id)||!steps.TryGetValue(id,out var st))continue;foreach(var r in st.Next??[])if(r?.Step is not null)queue.Enqueue(r.Step);}
                foreach(var s in steps.Keys.Where(k=>!seen.Contains(k)))E($"{Q}: 단계 '{steps[s].Title}'에 닿는 연결이 없습니다.");
                var ends=steps.Values.Where(s=>(s.Next??[]).Length==0).Select(s=>s.Id).ToHashSet();var grew=true;
                while(grew){grew=false;foreach(var s in steps.Values)if(!ends.Contains(s.Id)&&(s.Next??[]).Any(r=>r is not null&&ends.Contains(r.Step))){ends.Add(s.Id);grew=true;}}
                foreach(var s in steps.Values.Where(s=>!ends.Contains(s.Id)))E($"{Q}: 단계 '{s.Title}'에서 퀘스트가 끝날 수 없습니다(고리).");
            }
        }
        foreach(var t in g.Talks)
        {
            if(t is null){E("빈 대화가 있습니다.");continue;}
            string T=$"대화 '{t.Id}'";
            if(!Id(t.Id))E($"{T}: id를 확인하세요.");if(!npcs.Contains(t.Npc??""))E($"{T}: NPC를 확인하세요.");
            if(t.Nodes is null||t.Nodes.Length is 0 or >200){E($"{T}: 노드는 1~200개여야 합니다.");continue;}
            foreach(var dup in t.Nodes.GroupBy(n=>n?.Id).Where(x=>x.Count()>1))E($"{T}: 노드 id '{dup.Key}'가 겹칩니다.");
            var nodes=t.Nodes.Where(n=>n is not null).GroupBy(n=>n.Id??"").ToDictionary(x=>x.Key,x=>x.First());
            if(!nodes.ContainsKey(t.Start??""))E($"{T}: 시작 노드가 없습니다.");
            foreach(var n in t.Nodes)
            {
                if(n is null){E($"{T}: 빈 노드가 있습니다.");continue;}
                if(!Id(n.Id))E($"{T}: 노드 id를 확인하세요.");
                if(n.Options is null)
                {
                    if(!Text(n.Text,400))E($"{T} · {n.Id}: 대사는 400자 이내로 입력하세요.");
                    if(n.Next is not null&&!nodes.ContainsKey(n.Next))E($"{T} · {n.Id}: 다음 노드가 없습니다.");
                    continue;
                }
                if(n.Text is not null)E($"{T} · {n.Id}: 선택 노드에는 대사를 두지 않습니다.");
                if(n.Options.Length is 0 or >4)E($"{T} · {n.Id}: 선택지는 1~4개입니다.");
                foreach(var o in n.Options)
                {
                    if(o is null||!Text(o.Text,120)||o.Replies is null||o.Replies.Length>6||o.Replies.Any(r=>!Text(r,400))){E($"{T} · {n.Id}: 선택지(120자)와 답장(6줄, 줄마다 400자)을 확인하세요.");continue;}
                    if(o.Flags is {Length:>8}||o.Flags?.Any(f=>!Id(f))==true)E($"{T} · {n.Id}: 이야기 표시 이름(영문 소문자·숫자·_)을 확인하세요.");
                    if(o.Gives is not null&&(!quests.TryGetValue(o.Gives,out var gq)||gq.Opens?.Any(c=>c?.Kind=="Offered")!=true))E($"{T} · {n.Id}: 주는 퀘스트 '{o.Gives}'가 없거나 '대화에서 받음' 조건이 아닙니다.");
                    if(o.Next is not null&&!nodes.ContainsKey(o.Next))E($"{T} · {n.Id}: 선택지의 다음 노드가 없습니다.");
                }
            }
            if(nodes.ContainsKey(t.Start??""))
            {
                var seen=new HashSet<string>();var queue=new Queue<string>([t.Start!]);
                while(queue.Count>0)
                {
                    var id=queue.Dequeue();if(!seen.Add(id)||!nodes.TryGetValue(id,out var n))continue;
                    if(n.Next is not null)queue.Enqueue(n.Next);foreach(var o in n.Options??[])if(o?.Next is not null)queue.Enqueue(o.Next);
                }
                foreach(var id in nodes.Keys.Where(k=>!seen.Contains(k)))E($"{T}: 노드 '{id}'에 닿는 연결이 없습니다.");
                // A conversation must be able to end from every node (a message loop would play forever).
                var ends=nodes.Values.Where(n=>n.Options is null?n.Next is null:n.Options.Any(o=>o?.Next is null)).Select(n=>n.Id).ToHashSet();var grew=true;
                while(grew){grew=false;foreach(var n in nodes.Values)if(!ends.Contains(n.Id)&&(n.Next is not null&&ends.Contains(n.Next)||(n.Options??[]).Any(o=>o?.Next is not null&&ends.Contains(o.Next)))){ends.Add(n.Id);grew=true;}}
                foreach(var n in nodes.Values.Where(n=>!ends.Contains(n.Id)))E($"{T}: 노드 '{n.Id}'에서 대화가 끝날 수 없습니다(고리).");
            }
            // Conversations are played from quest steps; one no step uses could never be read.
            if(!usedTalks.ContainsKey(t.Id??""))E($"{T}: 어느 단계에도 연결되지 않았습니다.");
        }
        return errors.Distinct().ToList();
    }

    /// <summary>
    /// Follows a conversation with the player's picks (one option index per choice node met, in order) and returns the
    /// options taken, or null when the picks do not walk it from the start to an end exactly.
    /// </summary>
    internal static List<Option>? Walk(Talk talk,IReadOnlyList<int>? picks)
    {
        if(picks is null||picks.Count>200)return null;
        var nodes=talk.Nodes.ToDictionary(n=>n.Id);var taken=new List<Option>();string? at=talk.Start;int used=0;
        for(int guard=0;at is not null;guard++)
        {
            if(guard>2000||!nodes.TryGetValue(at,out var n))return null;
            if(n.Options is null){at=n.Next;continue;}
            if(used>=picks.Count||picks[used]<0||picks[used]>=n.Options.Length)return null;
            var o=n.Options[picks[used++]];taken.Add(o);at=o.Next;
        }
        return used==picks.Count?taken:null;
    }
    /// <summary>The next step after a finished one: the first route whose flag rule holds; null ends the quest.</summary>
    internal static string? NextStep(Step step,IReadOnlySet<string> flags)=>
        (step.Next??[]).FirstOrDefault(r=>r.Flag is null||flags.Contains(r.Flag)!=r.Without)?.Step;
    internal static bool Opens(Quest q,IReadOnlySet<string> flags,IReadOnlySet<string> offered,Func<string,bool> done,Func<string,string,bool> reached)=>
        (q.Opens??[]).All(c=>c.Kind switch
        {
            "QuestDone"=>done(c.Quest!),"StepReached"=>reached(c.Quest!,c.Step!),"Flag"=>flags.Contains(c.Flag!),"NoFlag"=>!flags.Contains(c.Flag!),"Offered"=>offered.Contains(q.Id),_=>false
        });
}

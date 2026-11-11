using System.Drawing.Imaging;
using System.Text.Json;
using ProjectProject01BuildServerHelper;
using static QuestGraph;

// The 운영 도우미 quest graph editor (Docs/QuestGraph.md 6): its model carries the server's graph unchanged both ways,
// and review screenshots of the window (FeatureChecks --quest-editor-review <folder>).
internal static class QuestGraphEditorReview
{
    /// <summary>A graph with a branch, a side quest handed out in a conversation and an NPC line, used by the checks and the screenshots.</summary>
    internal static Graph Sample()
    {
        var mira=new Talk("mira_talk","mira","n1",[
            new Node("n1","...거기 누구 있어요? 동생이랑 떨어졌어요.","n2"),
            new Node("n2",Options:[new Option("도와줄게요.",["고마워요. 동생을 찾아 줘요."],["help_mira"],"side_lost","n3"),new Option("지금은 안 돼요.",["...알겠어요."])]),
            new Node("n3","2층 어딘가에 있을 거예요.")]);
        var story=new Quest("main_mira","미라의 부탁","Main",[new Condition("QuestDone",Quest:MainLine)],"meet",[
            new Step("meet","미라와 대화하기",[new Goal("talk","Dialogue","미라와 대화하기")],Talk:"mira_talk",Next:[new Route("help",Flag:"help_mira"),new Route("alone")]),
            new Step("help","미라를 도와 경기 1회",[new Goal("play","Match","경기 1회 플레이")],Next:[new Route("end")]),
            new Step("alone","혼자 경기 1회",[new Goal("play","Match","경기 1회 플레이")],Next:[new Route("end")]),
            new Step("end","2층 탈출하기",[new Goal("escape","SecondFloor","2층 탈출")])]);
        var side=new Quest("side_lost","잃어버린 동생","Side",[new Condition("Offered")],"look",[new Step("look","생존자로 경기 1회",[new Goal("play","SurvivorMatch","생존자로 경기 1회")])]);
        var chain=new Quest("kalin_line","캘린의 기록","Chain",[new Condition("StepReached",Quest:MainLine,Step:"kalin_talk")],"read",[new Step("read","경기 완료 3회",[new Goal("done","MatchComplete","경기 완료",3)])],Npc:"kalin");
        return Default with{Npcs=[..Default.Npcs,new Npc("mira","미라")],Quests=[..Default.Quests,story,side,chain],Talks=[..Default.Talks,mira]};
    }
    /// <summary>Server graph -> editor model -> the JSON the editor sends (default .NET naming) -> server graph: nothing changes.</summary>
    internal static bool RoundTrips(Graph g)
    {
        var editor=JsonSerializer.Deserialize<QgGraph>(JsonSerializer.Serialize(g,Json),QgGraph.Json)!;
        var sent=JsonSerializer.Serialize(editor); // As CreateAdminRequest sends it.
        var back=JsonSerializer.Deserialize<Graph>(sent,new JsonSerializerOptions(JsonSerializerDefaults.Web))!;
        string Canon(Graph x)=>JsonSerializer.Serialize(x with{Quests=[..x.Quests.Select(q=>q with{Steps=[..q.Steps.Select(s=>s with{Next=s.Next??[]})]})]},Json);
        return Canon(back)==Canon(g);
    }
    internal static int Run(string folder)
    {
        Directory.CreateDirectory(folder);var g=Sample();
        if(!RoundTrips(g)){Console.WriteLine("FAIL editor model round trip");return 1;}
        var editor=JsonSerializer.Deserialize<QgGraph>(JsonSerializer.Serialize(g,Json),QgGraph.Json)!;
        var standing=new Dictionary<(string Quest,string Step),int>{[(MainLine,"first_match")]=12,[("main_mira","alone")]=3};
        void Shot(string name,string quest,string? step,bool talk,string? node=null,int option=-1)
        {
            using var bitmap=QuestGraphEditorForm.Review(editor.Clone(),standing,quest,step,talk,node,option);
            var path=Path.Combine(folder,name+".png");bitmap.Save(path,ImageFormat.Png);Console.WriteLine("Saved "+path);
        }
        Shot("QuestGraph_MainLine",MainLine,"first_match",false);
        Shot("QuestGraph_Branch","main_mira","meet",false);
        Shot("QuestGraph_Talk","main_mira","meet",true,"n2",0);
        Shot("QuestGraph_ChainQuest","kalin_line",null,false);
        Console.WriteLine("PASS editor model round trip and review screenshots");return 0;
    }
}

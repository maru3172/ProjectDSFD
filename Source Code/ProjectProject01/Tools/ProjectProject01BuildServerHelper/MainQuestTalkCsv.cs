using System.Text;
using Microsoft.VisualBasic.FileIO;

namespace ProjectProject01BuildServerHelper;

/// <summary>
/// Main quest phone conversations as CSV (대화ID,상대,종류,내용,답장), one row per message, in order:
/// 종류 "NPC" is a message from the NPC (내용); 종류 "선택" is one option the player can send (내용) with the NPC's
/// replies to it (답장, one message per line). Consecutive 선택 rows form one choice; a single 선택 row is a choice with
/// one option. Editing source: Docs/QuestStories/MainQuestTalks.csv.
/// </summary>
internal static class MainQuestTalkCsv
{
    internal sealed record Option(string Text,string[] Replies);
    internal sealed record Line(string? Text,Option[]? Options);
    internal sealed record Talk(string Npc,Line[] Lines);
    internal static readonly string[] Ids=["kalin_talk","atea_talk"];
    const string Header="대화ID,상대,종류,내용,답장";

    internal static Dictionary<string,Talk> Read(TextReader reader)
    {
        using var parser=new TextFieldParser(reader){HasFieldsEnclosedInQuotes=true,TrimWhiteSpace=false};
        parser.SetDelimiters(",");
        if(parser.EndOfData||string.Join(",",parser.ReadFields()??[])!=Header)throw new InvalidDataException("CSV 첫 줄은 "+Header+"여야 합니다.");
        var npcs=new Dictionary<string,string>(StringComparer.Ordinal);var lines=new Dictionary<string,List<Line>>(StringComparer.Ordinal);
        string? last=null;bool lastWasChoice=false;
        while(!parser.EndOfData)
        {
            var f=parser.ReadFields();
            if(f is null||f.All(string.IsNullOrWhiteSpace))continue;
            if(f.Length!=5||!Ids.Contains(f[0],StringComparer.Ordinal))throw new InvalidDataException($"{parser.LineNumber}번째 줄: 대화ID({string.Join(", ",Ids)})와 열 5개를 확인하세요.");
            string id=f[0],npc=f[1].Trim(),kind=f[2].Trim(),text=f[3].Trim();
            if(npc.Length is 0 or >20||npcs.TryGetValue(id,out var known)&&known!=npc)throw new InvalidDataException($"{id}: 상대 이름은 1~20자이고 대화마다 하나여야 합니다.");
            if(text.Length is 0 or >400)throw new InvalidDataException($"{id}: 내용은 1~400자여야 합니다.");
            npcs[id]=npc;var list=lines.TryGetValue(id,out var existing)?existing:lines[id]=[];
            if(kind=="NPC"){if(f[4].Trim().Length>0)throw new InvalidDataException($"{id}: NPC 줄에는 답장을 쓰지 않습니다.");list.Add(new(text,null));lastWasChoice=false;}
            else if(kind=="선택")
            {
                if(text.Length>120)throw new InvalidDataException($"{id}: 선택지는 120자 이하여야 합니다.");
                var replies=f[4].Replace("\r\n","\n").Split('\n').Select(r=>r.Trim()).Where(r=>r.Length>0).ToArray();
                if(replies.Length>6||replies.Any(r=>r.Length>400))throw new InvalidDataException($"{id}: 답장은 6줄, 줄마다 400자까지입니다.");
                if(lastWasChoice&&last==id){var options=list[^1].Options!;if(options.Length>=4)throw new InvalidDataException($"{id}: 한 번에 고를 선택지는 4개까지입니다.");list[^1]=new(null,[..options,new(text,replies)]);}
                else list.Add(new(null,[new(text,replies)]));
                lastWasChoice=true;
            }
            else throw new InvalidDataException($"{id}: 종류는 NPC 또는 선택이어야 합니다.");
            last=id;
        }
        if(Ids.Any(id=>!lines.ContainsKey(id)))throw new InvalidDataException("모든 대화("+string.Join(", ",Ids)+")를 포함해야 합니다.");
        if(lines.Values.Any(l=>l.Count>80))throw new InvalidDataException("대화 하나는 80줄까지입니다.");
        return Ids.ToDictionary(id=>id,id=>new Talk(npcs[id],[..lines[id]]),StringComparer.Ordinal);
    }

    internal static void Write(TextWriter writer,IReadOnlyDictionary<string,Talk> talks)
    {
        static string Q(string v)=>"\""+v.Replace("\"","\"\"")+"\"";
        writer.WriteLine(Header);
        foreach(var id in Ids)
        {
            if(!talks.TryGetValue(id,out var talk))throw new InvalidDataException(id+" 대화가 없습니다.");
            foreach(var line in talk.Lines)
                if(line.Options is null)writer.WriteLine(string.Join(",",id,Q(talk.Npc),"NPC",Q(line.Text??""),"\"\""));
                else foreach(var o in line.Options)writer.WriteLine(string.Join(",",id,Q(talk.Npc),"선택",Q(o.Text),Q(string.Join("\n",o.Replies))));
        }
    }

    internal static Dictionary<string,Talk> ReadFile(string path)
    {
        if(new FileInfo(path).Length>1024*1024)throw new InvalidDataException("CSV 파일은 1MB 이하여야 합니다.");
        using var reader=new StreamReader(path,new UTF8Encoding(false,true),detectEncodingFromByteOrderMarks:true);
        return Read(reader);
    }
}

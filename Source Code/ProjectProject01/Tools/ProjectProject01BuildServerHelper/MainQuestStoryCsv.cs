using System.Text;
using Microsoft.VisualBasic.FileIO;

namespace ProjectProject01BuildServerHelper;

internal static class MainQuestStoryCsv
{
    internal static readonly string[] Ids=["base","survivor_tutorial","first_match","third_floor","second_floor","department_escape","mannequin_tutorial","capture","mannequin_win"];

    internal static Dictionary<string,string> Read(TextReader reader)
    {
        using var parser=new TextFieldParser(reader){HasFieldsEnclosedInQuotes=true,TrimWhiteSpace=false};
        parser.SetDelimiters(",");
        if(parser.EndOfData||parser.ReadFields() is not ["단계ID","스토리"])
            throw new InvalidDataException("CSV 첫 줄은 단계ID,스토리여야 합니다.");
        var result=new Dictionary<string,string>(StringComparer.Ordinal);
        while(!parser.EndOfData)
        {
            var fields=parser.ReadFields();
            if(fields is not {Length:2}||!Ids.Contains(fields[0],StringComparer.Ordinal)||fields[1].Length>16000||!result.TryAdd(fields[0],fields[1]))
                throw new InvalidDataException("단계ID·중복 행·스토리 길이를 확인하세요.");
        }
        if(result.Count!=Ids.Length)throw new InvalidDataException("기본 이야기와 8단계의 행을 모두 포함해야 합니다.");
        return result;
    }

    internal static void Write(TextWriter writer,IReadOnlyDictionary<string,string> stories)
    {
        writer.WriteLine("단계ID,스토리");
        foreach(var id in Ids)
        {
            if(!stories.TryGetValue(id,out var story)||story.Length>16000)throw new InvalidDataException("스토리 구성을 확인하세요.");
            writer.WriteLine(id+",\""+story.Replace("\"","\"\"")+"\"");
        }
    }

    internal static Dictionary<string,string> ReadFile(string path)
    {
        if(new FileInfo(path).Length>1024*1024)throw new InvalidDataException("CSV 파일은 1MB 이하여야 합니다.");
        using var reader=new StreamReader(path,new UTF8Encoding(false,true),detectEncodingFromByteOrderMarks:true);
        return Read(reader);
    }
}

using System.Security.Cryptography;
using System.Text;

// Human-looking nicknames for server bots. Derived from the random bot id, so a bot keeps
// the same name for its whole life (room, roster snapshot, results) without a schema change.
internal static class BotNames
{
    private static readonly string[] Adjectives=["조용한","배고픈","졸린","수상한","느긋한","겁많은","용감한","까칠한","행복한","심심한","말없는","빠른","엉뚱한","소심한","무서운","작은"];
    private static readonly string[] Nouns=["고양이","너구리","감자","두부","펭귄","여우","올빼미","다람쥐","만두","호랑이","토끼","부엉이","곰돌이","유령","새벽","구름"];
    private static readonly string[] Latin=["night","shadow","mango","pixel","frost","lucky","sleepy","ghost","neo","blue","rain","echo","mint","nova","dusty","kiwi"];
    private static readonly string[] LatinTail=["fox","cat","owl","bear","bean","wolf","moon","toast","duck","star","seal","bun"];

    internal static string For(string botId,int salt=0)
    {
        byte[] hash=SHA256.HashData(Encoding.UTF8.GetBytes(botId+"|"+salt));
        int Pick(int index,int count)=>hash[index]%count;
        int number=hash[10]%100;
        return Pick(0,6) switch
        {
            0 or 1 => Adjectives[Pick(1,Adjectives.Length)]+Nouns[Pick(2,Nouns.Length)],
            2 => Adjectives[Pick(1,Adjectives.Length)]+Nouns[Pick(2,Nouns.Length)]+number,
            3 => Nouns[Pick(2,Nouns.Length)]+(hash[11]%2==0?"러버":"장인"),
            4 => Latin[Pick(3,Latin.Length)]+"_"+LatinTail[Pick(4,LatinTail.Length)],
            _ => Latin[Pick(3,Latin.Length)]+LatinTail[Pick(4,LatinTail.Length)]+number.ToString("00"),
        };
    }

    // Distinct within one roster: a repeated name would read as a copy-pasted program.
    internal static List<string> ForRoster(IEnumerable<string> botIds,IEnumerable<string>? taken=null)
    {
        var used=new HashSet<string>(taken??[],StringComparer.OrdinalIgnoreCase);var names=new List<string>();
        foreach(var id in botIds)
        {
            string name=For(id);
            for(int salt=1;used.Contains(name)&&salt<32;++salt)name=For(id,salt);
            used.Add(name);names.Add(name);
        }
        return names;
    }
}

using System.Text;
using System.Text.RegularExpressions;

namespace MeetingRecorder;

/// <summary>
/// Turns raw recognition output into text that is easy to read: one line per speaker turn (consecutive pieces by the
/// same speaker are joined, a pause over 4 s starts a new line), Whisper's repeated filler lines removed, and the typed
/// notes placed at their moments.
/// </summary>
internal static partial class TranscriptFormatter
{
    public static string Clock(double seconds) { var t = TimeSpan.FromSeconds(Math.Max(0, seconds)); return $"{(int)t.TotalHours:00}:{t.Minutes:00}:{t.Seconds:00}"; }

    [GeneratedRegex(@"\s+")] private static partial Regex Spaces();
    // Whisper sometimes "hears" these in silence or music; dropped when they are the whole segment.
    private static readonly string[] Hallucinations = ["시청해 주셔서", "시청해주셔서", "구독과 좋아요", "구독 좋아요", "좋아요와 구독", "MBC 뉴스", "자막 제공", "자막 by", "다음 영상에서", "Thank you for watching", "[음악]", "(음악)", "[박수]", "(박수)"];

    public static List<TranscriptSegment> Clean(IEnumerable<TranscriptSegment> segments)
    {
        var result = new List<TranscriptSegment>();
        foreach (var s in segments.OrderBy(s => s.Start))
        {
            var text = Spaces().Replace(s.Text, " ").Trim();
            if (text.Length == 0 || Hallucinations.Any(h => text.Contains(h, StringComparison.OrdinalIgnoreCase) && text.Length <= h.Length + 12)) continue;
            // The same sentence repeated back to back by the same track is a recognition loop, not speech.
            if (result.Count > 0 && result[^1].Speaker == s.Speaker && result[^1].Text.EndsWith(text, StringComparison.Ordinal)) continue;
            result.Add(s with { Text = text });
        }
        return result;
    }

    public static List<TranscriptSegment> Turns(IEnumerable<TranscriptSegment> segments, double pauseSeconds = 4.0)
    {
        var turns = new List<TranscriptSegment>();
        foreach (var s in Clean(segments))
        {
            if (turns.Count > 0 && turns[^1].Speaker == s.Speaker && s.Start - turns[^1].End <= pauseSeconds)
                turns[^1] = turns[^1] with { End = s.End, Text = turns[^1].Text + " " + s.Text, Confidence = Math.Min(turns[^1].Confidence, s.Confidence) };
            else turns.Add(s);
        }
        return turns;
    }

    // Below this average probability a line is shown as recognized but marked, never reworded or guessed at.
    public const float Unsure = 0.45f;
    public static string TurnLine(TranscriptSegment t) => $"**[{Clock(t.Start)}] {t.Speaker}:** {t.Text}{(t.Confidence < Unsure ? " _(인식 불확실)_" : "")}";
    public static string NoteLine(MeetingNote n) => $"> [{Clock(n.Seconds)}] {(n.Important ? "**[중요]** " : "")}메모: {n.Text}";

    public static string Header(MeetingSession session, string kind)
    {
        var sb = new StringBuilder($"# {session.Title} — {kind}\n\n");
        sb.AppendLine($"- 일시: {session.StartedLocal:yyyy-MM-dd HH:mm}, 길이 {Clock(session.DurationSeconds)}");
        if (session.Tracks.Count > 0) sb.AppendLine($"- 참여 음성: {string.Join(", ", session.Tracks.Select(t => t.Speaker))}");
        return sb.AppendLine().ToString();
    }

    /// <summary>The whole meeting as said and typed, in time order: every recognized turn and every note, nothing left out.</summary>
    public static string Body(MeetingSession session, IEnumerable<TranscriptSegment> segments)
    {
        var lines = Turns(segments).Select(t => (t.Start, Line: TurnLine(t)))
            .Concat(session.Notes.Select(n => (Start: n.Seconds, Line: NoteLine(n))))
            .OrderBy(l => l.Start).ToList();
        if (lines.Count == 0) return "(기록된 내용이 없습니다)\n";
        var sb = new StringBuilder();
        foreach (var (_, line) in lines) sb.AppendLine(line).AppendLine();
        return sb.ToString();
    }

    public static string TranscriptMarkdown(MeetingSession session, IEnumerable<TranscriptSegment> segments) =>
        Header(session, "대화문") + "## 대화\n\n" + Body(session, segments);

    public static string NotesMarkdown(MeetingSession session)
    {
        var sb = new StringBuilder($"# {session.Title} — 텍스트 기록\n\n");
        foreach (var n in session.Notes) sb.AppendLine($"- [{Clock(n.Seconds)}] {(n.Important ? "**[중요]** " : "")}{n.Text}");
        return sb.ToString();
    }
}

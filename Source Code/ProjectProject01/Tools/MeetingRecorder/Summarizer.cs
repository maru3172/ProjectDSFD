using System.Text;
using System.Text.RegularExpressions;
using Anthropic;
using Anthropic.Models.Beta.Messages;

namespace MeetingRecorder;

/// <summary>
/// Meeting summaries. The local one never leaves this PC: length, who spoke how much, and the sentences that read as
/// decisions, to-dos or open questions. The Claude one (optional) sends the transcript to the Claude API using the
/// caller's own ANTHROPIC_API_KEY and returns a written summary.
/// </summary>
internal static partial class Summarizer
{
    [GeneratedRegex(@"(결정|확정|하기로|정했|정하자|합시다|하자\b|그렇게 가|채택|승인)")] private static partial Regex Decision();
    [GeneratedRegex(@"(해야|할게요|할게|하겠습니다|해 ?주세요|맡아|담당|까지\s|마감|TODO|할 일|챙겨)")] private static partial Regex Todo();
    [GeneratedRegex(@"(\?|모르겠|확인 ?필요|아직|미정|고민)")] private static partial Regex Question();
    [GeneratedRegex(@"[가-힣A-Za-z0-9]{2,}")] private static partial Regex Word();
    private static readonly HashSet<string> Stop = ["그리고", "그래서", "근데", "그런데", "그냥", "이제", "약간", "진짜", "아니", "네네", "그거", "이거", "저거",
        "하는", "있는", "없는", "같은", "때문에", "그러면", "정도", "우리", "제가", "저는", "이렇게", "그렇게", "합니다", "있어요", "거예요", "같아요", "됩니다", "해서"];

    /// <summary>
    /// summary.md: "한눈에 보기" first (important marks, decisions, to-dos, questions), then "전체 내용" — the whole meeting.
    /// Every line in the overview is a sentence someone actually said or typed, copied as is; nothing is written anew.
    /// </summary>
    public static string Local(MeetingSession session, IReadOnlyList<TranscriptSegment> segments)
    {
        var turns = TranscriptFormatter.Turns(segments);
        var sb = new StringBuilder(TranscriptFormatter.Header(session, "회의 기록"));
        var talk = turns.GroupBy(t => t.Speaker).Select(g => (Speaker: g.Key, Seconds: g.Sum(t => t.End - t.Start))).ToList();
        var total = Math.Max(1, talk.Sum(t => t.Seconds));
        if (talk.Count > 0) sb.AppendLine("- 발언 비율: " + string.Join(", ", talk.Select(t => $"{t.Speaker} {100 * t.Seconds / total:0}%"))).AppendLine();
        sb.AppendLine("_'한눈에 보기'의 모든 줄은 회의에서 실제로 말하거나 적은 문장을 그대로 옮긴 것입니다. 새로 지은 문장은 없습니다._").AppendLine();
        sb.AppendLine("## 한눈에 보기").AppendLine();
        sb.Append(ImportantBlock(session, turns));

        IEnumerable<(double At, string Who, string Text, bool Unsure)> Lines() =>
            turns.SelectMany(t => SplitSentences(t.Text).Select(s => (t.Start, t.Speaker, s, t.Confidence < TranscriptFormatter.Unsure)))
                 .Concat(session.Notes.Select(n => (n.Seconds, "메모", n.Text, false)));
        void Section(string title, Regex pattern)
        {
            var found = Lines().Where(l => pattern.IsMatch(l.Text)).OrderBy(l => l.At).Take(30).ToList();
            sb.AppendLine($"### {title}").AppendLine();
            if (found.Count == 0) sb.AppendLine("- (없음)");
            foreach (var l in found) sb.AppendLine($"- [{TranscriptFormatter.Clock(l.At)}] {l.Who}: {l.Text}{(l.Unsure ? " _(인식 불확실)_" : "")}");
            sb.AppendLine();
        }
        Section("결정 사항", Decision());
        Section("할 일", Todo());
        Section("질문 · 미정", Question());

        var words = Lines().SelectMany(l => Word().Matches(l.Text).Select(m => Stem(m.Value))).Where(w => w.Length >= 2 && !Stop.Contains(w))
            .GroupBy(w => w).Where(g => g.Count() >= 2).OrderByDescending(g => g.Count()).Take(12).Select(g => $"{g.Key}({g.Count()})").ToList();
        sb.AppendLine("### 자주 나온 말").AppendLine();
        sb.AppendLine(words.Count > 0 ? string.Join(", ", words) : "- (두 번 이상 나온 말 없음)").AppendLine();
        sb.AppendLine("## 전체 내용").AppendLine();
        sb.Append(TranscriptFormatter.Body(session, segments));
        return sb.ToString();
    }

    // Seconds of speech around an important mark that are copied under it: what was being said when it was pressed.
    private const double MarkBefore = 20, MarkAfter = 10;

    /// <summary>Each important note, followed word for word by what was said around it.</summary>
    internal static string ImportantBlock(MeetingSession session, IReadOnlyList<TranscriptSegment> turns)
    {
        var sb = new StringBuilder("### 중요 표시\n\n");
        var marks = session.Notes.Where(n => n.Important).OrderBy(n => n.Seconds).ToList();
        if (marks.Count == 0) sb.AppendLine("- (없음. 회의 중 '중요 표시'를 누르거나 메모 앞에 !를 붙이면 그 순간의 말이 여기 그대로 실립니다.)");
        foreach (var n in marks)
        {
            sb.AppendLine($"- [{TranscriptFormatter.Clock(n.Seconds)}] 메모: {n.Text}");
            foreach (var t in turns.Where(t => t.End >= n.Seconds - MarkBefore && t.Start <= n.Seconds + MarkAfter))
                sb.AppendLine($"  - {TranscriptFormatter.TurnLine(t)}");
        }
        return sb.AppendLine().ToString();
    }

    // Verb and sentence endings mark a word that is not a topic ("했는데", "되었습니다"); particles come off the rest ("녹음을" → "녹음").
    private static readonly string[] VerbEndings = ["습니다", "니다", "는데", "려고", "하게", "해요", "어요", "아요", "에요", "예요", "네요", "지만", "하고", "했다", "한다", "된다", "였다", "었다", "았다", "죠", "다", "요"];
    private static readonly string[] Particles = ["에서는", "에서", "으로는", "으로", "에게", "한테", "까지", "부터", "처럼", "보다", "이랑", "랑", "은", "는", "이", "가", "을", "를", "에", "로", "와", "과", "도", "만", "의"];
    internal static string Stem(string word)
    {
        if (!word.Any(c => c >= '가' && c <= '힣')) return word.ToLowerInvariant();
        if (VerbEndings.Any(e => word.EndsWith(e, StringComparison.Ordinal))) return "";
        foreach (var p in Particles) if (word.Length > p.Length + 1 && word.EndsWith(p, StringComparison.Ordinal)) return word[..^p.Length];
        return word;
    }

    private static IEnumerable<string> SplitSentences(string text) =>
        Regex.Split(text, @"(?<=[.!?。])\s+|(?<=(요|다|죠|까))\s+").Select(s => s.Trim()).Where(s => s.Length >= 4 && s is not ("요" or "다" or "죠" or "까"));

    public static bool HasApiKey => !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("ANTHROPIC_API_KEY"));

    private const string Instructions = """
        아래는 회의 녹음을 음성 인식한 대화와 회의 중 입력한 메모입니다. 줄마다 [시:분:초] 시각이 붙어 있고,
        "(인식 불확실)" 표시는 음성 인식이 자신 없던 줄, "[중요]" 표시는 참석자가 중요하다고 표시한 메모입니다.
        한국어로, 한눈에 읽히는 짧은 요약을 다음 순서로 써 주세요. 제목은 "###"만 쓰세요.
        ### 한 줄 요약
        ### 핵심 내용 (주제마다 1~3줄)
        ### 결정 사항
        ### 할 일 (누가, 무엇을, 언제까지. 담당이 말에 없으면 "담당 미정")
        ### 남은 질문
        규칙:
        - 대화와 메모에 실제로 있는 내용만 씁니다. 추측하거나 보태지 마세요. 해당 내용이 없으면 "없음"이라고 씁니다.
        - 모든 항목 끝에 근거가 된 줄의 시각을 [시:분:초]로 붙입니다. 근거 줄을 댈 수 없는 항목은 쓰지 마세요.
        - "(인식 불확실)" 줄에 기댄 항목은 끝에 "(불확실)"을 붙입니다. 틀린 단어를 짐작으로 고치지 마세요.
        - "[중요]" 메모와 그 주변 말은 빠짐없이 다룹니다.
        """;

    /// <summary>Claude's short overview on top; under it the important marks and the whole meeting, copied as recorded.</summary>
    public static async Task<string> WithClaudeAsync(MeetingSession session, IReadOnlyList<TranscriptSegment> segments, string model, CancellationToken ct)
    {
        var overview = await AskClaudeAsync(TranscriptFormatter.TranscriptMarkdown(session, segments), model, ct);
        return TranscriptFormatter.Header(session, "회의 기록")
            + "_'한눈에 보기'는 Claude가 아래 원문만 보고 쓴 요약입니다. 항목 끝의 [시각]으로 원문을 찾아 확인할 수 있습니다._\n\n"
            + "## 한눈에 보기 (Claude 요약)\n\n" + overview.Trim() + "\n\n"
            + "## 중요 표시 (원문)\n\n" + ImportantBlock(session, TranscriptFormatter.Turns(segments)).Replace("### 중요 표시\n\n", "")
            + "## 전체 내용\n\n" + TranscriptFormatter.Body(session, segments);
    }

    private static async Task<string> AskClaudeAsync(string transcriptMarkdown, string model, CancellationToken ct)
    {
        if (!HasApiKey) throw new InvalidOperationException("ANTHROPIC_API_KEY 환경 변수가 없습니다. 키를 설정한 뒤 다시 실행하세요.");
        AnthropicClient client = new();
        var response = await client.Beta.Messages.Create(new MessageCreateParams
        {
            Model = model,
            MaxTokens = 16000,
            // A declined request is re-served by a fallback model in the same call.
            Betas = ["server-side-fallback-2026-07-01"],
            Fallbacks = new Default(), // "default": the server picks the fallback by refusal category.
            System = Instructions,
            Messages = [new BetaUserMessageParam { Content = transcriptMarkdown }],
        }, ct);
        if (response.StopReason == "refusal") throw new InvalidOperationException("요약 요청이 거절되었습니다. 로컬 요약을 사용하세요.");
        var text = new StringBuilder();
        foreach (var block in response.Content) if (block.TryPickText(out var t)) text.Append(t.Text);
        if (response.StopReason == "max_tokens") text.AppendLine("\n\n_(요약이 길어 끝부분이 잘렸습니다.)_");
        return text.ToString();
    }
}

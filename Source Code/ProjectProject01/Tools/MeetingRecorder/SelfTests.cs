using NAudio.Wave;

namespace MeetingRecorder;

/// <summary>Offline checks (no microphone, model or network): <c>MeetingRecorder.exe --selftest</c>.</summary>
internal static class SelfTests
{
    public static int Run()
    {
        int failed = 0;
        void Check(bool ok, string what) { Console.WriteLine($"{(ok ? "PASS" : "FAIL")} {what}"); if (!ok) failed++; }

        var raw = new List<TranscriptSegment>
        {
            new(0, 2, "나", "안녕하세요 오늘 회의 시작할게요"), new(2.5, 4, "나", "먼저 일정부터 보죠"),
            new(5, 6, "상대", "시청해 주셔서 감사합니다"), new(9, 12, "상대", "다음 주 금요일까지 빌드 올리기로 결정했습니다"),
            new(12, 13, "상대", "다음 주 금요일까지 빌드 올리기로 결정했습니다"), new(20, 23, "나", "그럼 제가 문서 정리할게요"),
        };
        var turns = TranscriptFormatter.Turns(raw);
        Check(turns.Count == 3, "Pieces by one speaker within 4 s join into one turn");
        Check(!turns.Any(t => t.Text.Contains("시청해")), "Whisper filler lines are dropped");
        Check(turns.Count(t => t.Text.Contains("결정했습니다")) == 1, "A repeated recognition loop is dropped");

        var session = new MeetingSession { Title = "테스트", DurationSeconds = 30, Tracks = [new("mic.wav", "나"), new("system.wav", "상대")],
            Notes = [new MeetingNote(10, "빌드 일정 확정")] };
        var md = TranscriptFormatter.TranscriptMarkdown(session, raw);
        Check(md.Contains("**[00:00:00] 나:**") && md.Contains("메모: 빌드 일정 확정"), "Transcript shows timed turns and notes");
        Check(md.IndexOf("메모:", StringComparison.Ordinal) < md.IndexOf("[00:00:20]", StringComparison.Ordinal), "Notes sit at their moment");
        var summary = Summarizer.Local(session, raw);
        Check(summary.Contains("결정했습니다") && summary.Contains("정리할게요"), "Local summary picks decisions and to-dos");
        int glance = summary.IndexOf("## 한눈에 보기", StringComparison.Ordinal), all = summary.IndexOf("## 전체 내용", StringComparison.Ordinal);
        Check(glance >= 0 && all > glance && summary.IndexOf("먼저 일정부터 보죠", all, StringComparison.Ordinal) > all, "Overview comes first, then the whole meeting");
        var marked = new MeetingSession { Title = "중요", Notes = [new MeetingNote(5, "빌드 일정", Important: true)] };
        var block = Summarizer.ImportantBlock(marked, TranscriptFormatter.Turns(raw));
        Check(block.Contains("다음 주 금요일까지 빌드 올리기로 결정했습니다") && !block.Contains("문서 정리"), "An important mark copies what was said around it, word for word");
        var unsure = TranscriptFormatter.TurnLine(new TranscriptSegment(0, 1, "나", "흐릿한 말", 0.2f));
        Check(unsure.Contains("흐릿한 말") && unsure.Contains("인식 불확실"), "A low-confidence line is kept as recognized and marked");

        // Silence and room hiss never count as speech (Whisper invents "감사합니다" there); a spoken stretch does.
        var quiet = Transcriber.Loudness.FromSamples(new Tone(16000, 5, 0.002f, -1, -1));
        Check(!quiet.HasSpeech, "A near-silent track is skipped");
        var talk = Transcriber.Loudness.FromSamples(new Tone(16000, 6, 0.002f, 2, 4));
        Check(talk.HasSpeech && talk.IsSpeech(2.2, 3.8) && !talk.IsSpeech(4.5, 5.9), "Only the spoken stretch counts as speech");
        Check(Summarizer.Stem("녹음을") == "녹음" && Summarizer.Stem("했는데") == "" && Summarizer.Stem("되었습니다") == "", "Particles and verb endings come off frequent words");

        var folder = Path.Combine(Path.GetTempPath(), $"meeting-selftest-{Guid.NewGuid():N}");
        try
        {
            Directory.CreateDirectory(folder);
            var stereo = Path.Combine(folder, "in.wav");var mono = Path.Combine(folder, "out.wav");
            using (var w = new WaveFileWriter(stereo, WaveFormat.CreateIeeeFloatWaveFormat(48000, 2)))
                for (int i = 0; i < 48000; i++) { w.WriteSample(0.1f); w.WriteSample(-0.1f); }
            Transcriber.PrepareForWhisper(stereo, mono);
            using var r = new WaveFileReader(mono);
            Check(r.WaveFormat.SampleRate == 16000 && r.WaveFormat.Channels == 1 && r.WaveFormat.BitsPerSample == 16, "Audio becomes 16 kHz mono 16-bit");
            Check(Math.Abs(r.TotalTime.TotalSeconds - 1) < 0.05, "Conversion keeps the length");
            session.Save(folder);
            Check(MeetingSession.Load(folder)?.Notes.Count == 1 && File.Exists(Path.Combine(folder, MeetingSession.NotesFile)), "Meeting saves and loads");
        }
        finally { try { Directory.Delete(folder, true); } catch (IOException) { } }
        Console.WriteLine(failed == 0 ? "All checks passed." : $"{failed} check(s) failed.");
        return failed == 0 ? 0 : 1;
    }
}

/// <summary>Test signal: hiss at <c>noise</c> everywhere, plus a loud tone between <c>from</c> and <c>to</c> seconds.</summary>
internal sealed class Tone(int rate, double seconds, float noise, double from, double to) : NAudio.Wave.ISampleProvider
{
    private long position;private readonly long total = (long)(rate * seconds);private readonly Random random = new(1);
    public NAudio.Wave.WaveFormat WaveFormat { get; } = NAudio.Wave.WaveFormat.CreateIeeeFloatWaveFormat(rate, 1);
    public int Read(float[] buffer, int offset, int count)
    {
        int n = (int)Math.Min(count, total - position);
        for (int i = 0; i < n; i++, position++)
        {
            double t = (double)position / rate;
            buffer[offset + i] = (float)(noise * (random.NextDouble() * 2 - 1) + (t >= from && t < to ? 0.2 * Math.Sin(2 * Math.PI * 220 * t) : 0));
        }
        return n;
    }
}

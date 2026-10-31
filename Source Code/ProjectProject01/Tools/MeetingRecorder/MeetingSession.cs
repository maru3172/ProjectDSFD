using System.Text.Json;

namespace MeetingRecorder;

/// <summary>One meeting: a folder with the audio tracks, the typed notes, the transcript and the summary.</summary>
internal sealed class MeetingSession
{
    public string Title { get; set; } = "회의";
    public DateTime StartedLocal { get; set; } = DateTime.Now;
    public double DurationSeconds { get; set; }
    public List<MeetingNote> Notes { get; set; } = [];
    public List<MeetingTrack> Tracks { get; set; } = [];

    public const string MetaFile = "meeting.json", NotesFile = "notes.md", TranscriptFile = "transcript.md", RawTranscriptFile = "transcript.raw.json",
        SummaryFile = "summary.md";
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    public static string CreateFolder(string root, string title, DateTime started)
    {
        var safe = string.Concat(title.Where(c => !Path.GetInvalidFileNameChars().Contains(c))).Trim();
        if (safe.Length == 0) safe = "회의";
        if (safe.Length > 40) safe = safe[..40];
        var folder = Path.Combine(root, $"{started:yyyy-MM-dd_HHmm}_{safe}");
        for (int i = 2; Directory.Exists(folder); i++) folder = Path.Combine(root, $"{started:yyyy-MM-dd_HHmm}_{safe}_{i}");
        Directory.CreateDirectory(folder);
        return folder;
    }

    public static MeetingSession? Load(string folder)
    {
        var path = Path.Combine(folder, MetaFile);
        try { return File.Exists(path) ? JsonSerializer.Deserialize<MeetingSession>(File.ReadAllText(path)) : null; }
        catch (Exception) { return null; }
    }

    public void Save(string folder)
    {
        File.WriteAllText(Path.Combine(folder, MetaFile), JsonSerializer.Serialize(this, Json));
        File.WriteAllText(Path.Combine(folder, NotesFile), TranscriptFormatter.NotesMarkdown(this));
    }
}

/// <summary>A typed note. Important ones (the 중요 표시 button, or text starting with "!") open the summary word for word.</summary>
internal sealed record MeetingNote(double Seconds, string Text, bool Important = false);
/// <summary>An audio track: who it stands for (microphone = this user, system = everyone heard through the PC).</summary>
internal sealed record MeetingTrack(string File, string Speaker);
/// <summary>One recognized piece. Confidence is Whisper's average token probability (1 for pieces saved before it was kept).</summary>
internal sealed record TranscriptSegment(double Start, double End, string Speaker, string Text, float Confidence = 1);

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
    /// <summary>Free text typed like a notepad. Pictures sit in it as markdown image lines pointing into <see cref="ImagesFolder"/>.</summary>
    public string Memo { get; set; } = "";

    public const string MetaFile = "meeting.json", NotesFile = "notes.md", TranscriptFile = "transcript.md", RawTranscriptFile = "transcript.raw.json",
        SummaryFile = "summary.md", MemoFile = "memo.md", ImagesFolder = "images";
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
        var memo = Path.Combine(folder, MemoFile);
        if (Memo.Length > 0 || File.Exists(memo)) File.WriteAllText(memo, Memo);
    }
}

/// <summary>A typed note. Important ones (the 중요 표시 button, or text starting with "!") open the summary word for word.</summary>
internal sealed record MeetingNote(double Seconds, string Text, bool Important = false);
/// <summary>An audio track: who it stands for (microphone = this user, system = everyone heard through the PC).</summary>
internal sealed record MeetingTrack(string File, string Speaker);
/// <summary>One recognized piece. Confidence is Whisper's average token probability (1 for pieces saved before it was kept).</summary>
internal sealed record TranscriptSegment(double Start, double End, string Speaker, string Text, float Confidence = 1);

/// <summary>Pictures kept with a meeting: copied into its images folder and shown in the memo as markdown image lines.</summary>
internal static partial class MeetingImages
{
    [System.Text.RegularExpressions.GeneratedRegex(@"!\[[^\]]*\]\((images/[^)\s]+)\)")] private static partial System.Text.RegularExpressions.Regex Link();
    public static readonly string[] Extensions = [".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp"];

    /// <summary>Copies a picture file into the meeting and returns the line that shows it.</summary>
    public static string AddFile(string folder, string source)
    {
        var name = Path.GetFileNameWithoutExtension(source);
        var target = UniquePath(folder, name, Path.GetExtension(source).ToLowerInvariant());
        File.Copy(source, target);
        return Line(name, target);
    }

    /// <summary>Saves a picture (e.g. one pasted from the clipboard) into the meeting as PNG and returns the line that shows it.</summary>
    public static string AddImage(string folder, Image image, string name = "붙여넣은 사진")
    {
        var target = UniquePath(folder, $"{DateTime.Now:HHmmss}", ".png");
        image.Save(target, System.Drawing.Imaging.ImageFormat.Png);
        return Line(name, target);
    }

    /// <summary>The pictures the memo shows, in order, as relative paths that exist in the meeting folder.</summary>
    public static List<string> InMemo(string folder, string memo) =>
        Link().Matches(memo).Select(m => m.Groups[1].Value).Distinct().Where(p => File.Exists(Path.Combine(folder, p))).ToList();

    private static string Line(string name, string target) => $"![{name.Replace("]", "")}]({MeetingSession.ImagesFolder}/{Path.GetFileName(target)})";

    private static string UniquePath(string folder, string name, string extension)
    {
        var dir = Path.Combine(folder, MeetingSession.ImagesFolder);Directory.CreateDirectory(dir);
        var safe = string.Concat(name.Where(c => !Path.GetInvalidFileNameChars().Contains(c) && c != ' ' && c != '(' && c != ')')).Trim();
        if (safe.Length == 0) safe = "사진";
        if (safe.Length > 40) safe = safe[..40];
        var path = Path.Combine(dir, safe + extension);
        for (int i = 2; File.Exists(path); i++) path = Path.Combine(dir, $"{safe}_{i}{extension}");
        return path;
    }
}

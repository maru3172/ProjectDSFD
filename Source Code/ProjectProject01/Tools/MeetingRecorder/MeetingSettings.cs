using System.Text.Json;

namespace MeetingRecorder;

/// <summary>
/// Per-user settings, stored outside any project (%AppData%\MeetingRecorder\settings.json) so the tool works the same
/// from any folder or project. No API key is stored here: the Claude summary reads ANTHROPIC_API_KEY from the environment.
/// </summary>
internal sealed class MeetingSettings
{
    public string StorageFolder { get; set; } = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "MeetingRecorder");
    /// <summary>Whisper model size: tiny, base, small, medium, large-v3-turbo. Bigger is more accurate and slower.</summary>
    public string WhisperModel { get; set; } = "small";
    public string Language { get; set; } = "ko";
    public string MicrophoneSpeaker { get; set; } = "나";
    public string SystemAudioSpeaker { get; set; } = "상대";
    public bool RecordSystemAudio { get; set; } = true;
    public string ClaudeModel { get; set; } = "claude-opus-5-5";

    public static string SettingsFolder => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "MeetingRecorder");
    private static string SettingsPath => Path.Combine(SettingsFolder, "settings.json");
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    public static MeetingSettings Load()
    {
        try
        {
            if (File.Exists(SettingsPath)) return JsonSerializer.Deserialize<MeetingSettings>(File.ReadAllText(SettingsPath)) ?? new();
        }
        catch (Exception) { /* A broken file falls back to defaults; the next save rewrites it. */ }
        return new();
    }

    public void Save()
    {
        Directory.CreateDirectory(SettingsFolder);
        File.WriteAllText(SettingsPath, JsonSerializer.Serialize(this, Json));
    }
}

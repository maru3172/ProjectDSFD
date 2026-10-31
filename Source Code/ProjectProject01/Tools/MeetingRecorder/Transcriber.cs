using NAudio.Wave;
using NAudio.Wave.SampleProviders;
using Whisper.net;
using Whisper.net.Ggml;

namespace MeetingRecorder;

/// <summary>
/// Speech to text on this PC with Whisper (no network after the one-time model download, no key). Each track is turned
/// into 16 kHz mono first, transcribed, and tagged with the track's speaker.
/// </summary>
internal static class Transcriber
{
    public static string ModelFolder => Path.Combine(MeetingSettings.SettingsFolder, "models");
    public static string ModelPath(string size) => Path.Combine(ModelFolder, $"ggml-{size}.bin");
    public static bool HasModel(string size) => File.Exists(ModelPath(size));

    private static GgmlType TypeOf(string size) => size switch
    {
        "tiny" => GgmlType.Tiny, "base" => GgmlType.Base, "medium" => GgmlType.Medium, "large-v3-turbo" => GgmlType.LargeV3Turbo, _ => GgmlType.Small
    };

    public static async Task DownloadModelAsync(string size, IProgress<string> progress, CancellationToken ct)
    {
        Directory.CreateDirectory(ModelFolder);
        var temp = ModelPath(size) + ".part";
        progress.Report($"Whisper {size} 모델을 받는 중입니다…");
        await using (var source = await WhisperGgmlDownloader.Default.GetGgmlModelAsync(TypeOf(size), cancellationToken: ct))
        await using (var file = File.Create(temp)) await source.CopyToAsync(file, ct);
        File.Move(temp, ModelPath(size), overwrite: true);
    }

    public static async Task<List<TranscriptSegment>> TranscribeAsync(string folder, MeetingSession session, MeetingSettings settings,
        IProgress<string> progress, CancellationToken ct)
    {
        if (!HasModel(settings.WhisperModel)) throw new InvalidOperationException("음성 인식 모델이 없습니다. 먼저 모델을 받아 주세요.");
        var segments = new List<TranscriptSegment>();
        using var factory = WhisperFactory.FromPath(ModelPath(settings.WhisperModel));
        foreach (var track in session.Tracks)
        {
            var source = Path.Combine(folder, track.File);
            if (!File.Exists(source) || new FileInfo(source).Length < 4096) continue;
            progress.Report($"{track.Speaker} 음성을 텍스트로 바꾸는 중입니다…");
            var prepared = Path.Combine(Path.GetTempPath(), $"meeting-{Guid.NewGuid():N}.wav");
            try
            {
                PrepareForWhisper(source, prepared);
                // Whisper writes words into silence ("감사합니다", "시청해 주셔서…"); keep only pieces where the track was loud enough to be speech.
                var loudness = Loudness.Measure(prepared);
                if (!loudness.HasSpeech) { progress.Report($"{track.Speaker}: 말소리가 없어 건너뜁니다."); continue; }
                // No context: an earlier (possibly wrong) line is not fed back as a prompt, which is what makes Whisper repeat or drift.
                using var processor = factory.CreateBuilder().WithLanguage(settings.Language).WithNoContext().WithProbabilities().Build();
                await using var stream = File.OpenRead(prepared);
                await foreach (var s in processor.ProcessAsync(stream, ct))
                {
                    var text = s.Text.Trim();
                    if (text.Length > 0 && loudness.IsSpeech(s.Start.TotalSeconds, s.End.TotalSeconds))
                        segments.Add(new TranscriptSegment(s.Start.TotalSeconds, s.End.TotalSeconds, track.Speaker, text, (float)s.Probability));
                }
            }
            finally { try { File.Delete(prepared); } catch (IOException) { } }
        }
        return segments.OrderBy(s => s.Start).ToList();
    }

    /// <summary>Any WAV (float or PCM, any rate, any channel count) to 16 kHz mono 16-bit, which Whisper expects.</summary>
    public static void PrepareForWhisper(string source, string target)
    {
        using var reader = new AudioFileReader(source);
        ISampleProvider samples = reader.WaveFormat.Channels == 1 ? reader : new DownmixToMono(reader);
        if (samples.WaveFormat.SampleRate != 16000) samples = new WdlResamplingSampleProvider(samples, 16000);
        WaveFileWriter.CreateWaveFile16(target, samples);
    }

    /// <summary>
    /// Loudness of a 16 kHz mono track in 50 ms windows. A piece counts as speech when its loudest windows rise clearly
    /// above the track's own noise floor and above an absolute minimum, so room hiss and silence never count.
    /// </summary>
    internal sealed class Loudness
    {
        private const double Window = 0.05, MinimumSpeech = 0.012, AboveFloor = 3.0;
        private readonly float[] rms;private readonly double threshold;
        private Loudness(float[] rms)
        {
            this.rms = rms;
            var sorted = rms.OrderBy(v => v).ToArray();
            double floor = sorted.Length == 0 ? 0 : sorted[(int)(sorted.Length * 0.2)];
            threshold = Math.Max(MinimumSpeech, floor * AboveFloor);
        }
        public bool HasSpeech => rms.Count(v => v >= threshold) >= 6; // At least ~0.3 s of speech in the whole track.
        public bool IsSpeech(double start, double end)
        {
            int a = Math.Clamp((int)(start / Window), 0, Math.Max(0, rms.Length - 1)), b = Math.Clamp((int)Math.Ceiling(end / Window), a + 1, rms.Length);
            int loud = 0;for (int i = a; i < b; i++) if (rms[i] >= threshold) loud++;
            return loud >= Math.Max(2, (b - a) / 10); // Loud for at least a tenth of the piece (and 100 ms).
        }
        public static Loudness Measure(string wav16kMono) => FromSamples(new AudioFileReader(wav16kMono));
        public static Loudness FromSamples(ISampleProvider samples)
        {
            int per = (int)(samples.WaveFormat.SampleRate * Window);var buffer = new float[per];var list = new List<float>();
            while (true)
            {
                int n = samples.Read(buffer, 0, per);if (n <= 0) break;
                double sum = 0;for (int i = 0; i < n; i++) sum += buffer[i] * buffer[i];
                list.Add((float)Math.Sqrt(sum / n));
            }
            (samples as IDisposable)?.Dispose();
            return new Loudness([.. list]);
        }
    }

    private sealed class DownmixToMono(ISampleProvider source) : ISampleProvider
    {
        private readonly int channels = source.WaveFormat.Channels;
        private float[] buffer = [];
        public WaveFormat WaveFormat { get; } = WaveFormat.CreateIeeeFloatWaveFormat(source.WaveFormat.SampleRate, 1);
        public int Read(float[] output, int offset, int count)
        {
            int needed = count * channels;
            if (buffer.Length < needed) buffer = new float[needed];
            int read = source.Read(buffer, 0, needed), frames = read / channels;
            for (int f = 0; f < frames; f++)
            {
                float sum = 0;
                for (int c = 0; c < channels; c++) sum += buffer[f * channels + c];
                output[offset + f] = sum / channels;
            }
            return frames;
        }
    }
}

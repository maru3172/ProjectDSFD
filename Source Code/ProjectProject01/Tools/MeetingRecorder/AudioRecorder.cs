using System.Diagnostics;
using NAudio.CoreAudioApi;
using NAudio.Wave;

namespace MeetingRecorder;

/// <summary>
/// Records the default microphone and, optionally, everything the PC plays (WASAPI loopback: Discord, Zoom, a browser
/// call...) into two WAV files. Loopback delivers nothing while the PC is silent, so silence is written in to keep both
/// tracks on the same clock; the transcript then lines them up by time.
/// </summary>
internal sealed class AudioRecorder : IDisposable
{
    private sealed class Track(IWaveIn capture, WaveFileWriter writer, bool padSilence)
    {
        public IWaveIn Capture = capture;public WaveFileWriter Writer = writer;public bool PadSilence = padSilence;public readonly object Lock = new();
    }
    private readonly List<Track> tracks = [];
    private readonly Stopwatch clock = new();
    private System.Threading.Timer? padTimer;
    public bool IsRecording { get; private set; }
    public TimeSpan Elapsed => clock.Elapsed;
    public event Action<string>? Failed;

    public IReadOnlyList<MeetingTrack> Start(string folder, MeetingSettings settings)
    {
        if (IsRecording) throw new InvalidOperationException("이미 녹음 중입니다.");
        var result = new List<MeetingTrack>();
        AddTrack(new WasapiCapture(), Path.Combine(folder, "mic.wav"), padSilence: false);
        result.Add(new MeetingTrack("mic.wav", settings.MicrophoneSpeaker));
        if (settings.RecordSystemAudio)
        {
            AddTrack(new WasapiLoopbackCapture(), Path.Combine(folder, "system.wav"), padSilence: true);
            result.Add(new MeetingTrack("system.wav", settings.SystemAudioSpeaker));
        }
        clock.Restart();
        foreach (var t in tracks) t.Capture.StartRecording();
        padTimer = new System.Threading.Timer(_ => PadAll(), null, 500, 500);
        IsRecording = true;
        return result;
    }

    private void AddTrack(IWaveIn capture, string path, bool padSilence)
    {
        var track = new Track(capture, new WaveFileWriter(path, capture.WaveFormat), padSilence);
        capture.DataAvailable += (_, e) =>
        {
            lock (track.Lock) { if (track.PadSilence) Pad(track); track.Writer.Write(e.Buffer, 0, e.BytesRecorded); }
        };
        capture.RecordingStopped += (_, e) => { if (e.Exception != null) Failed?.Invoke(e.Exception.Message); };
        tracks.Add(track);
    }

    // Fill the gap between what was written and where the clock is, leaving 200 ms for data still in flight.
    private void Pad(Track track)
    {
        var format = track.Writer.WaveFormat;
        long expected = (long)((clock.Elapsed.TotalSeconds - 0.2) * format.AverageBytesPerSecond);
        expected -= expected % format.BlockAlign;
        long missing = expected - track.Writer.Length;
        if (missing <= 0) return;
        var zeros = new byte[Math.Min(missing, format.AverageBytesPerSecond)];
        while (missing > 0) { int n = (int)Math.Min(missing, zeros.Length); track.Writer.Write(zeros, 0, n); missing -= n; }
    }

    private void PadAll()
    {
        foreach (var t in tracks) if (t.PadSilence) lock (t.Lock) { try { Pad(t); } catch (ObjectDisposedException) { } }
    }

    public double Stop()
    {
        if (!IsRecording) return 0;
        padTimer?.Dispose();padTimer = null;
        foreach (var t in tracks) t.Capture.StopRecording();
        var seconds = clock.Elapsed.TotalSeconds;clock.Stop();
        foreach (var t in tracks) lock (t.Lock) { if (t.PadSilence) Pad(t); t.Writer.Dispose(); t.Capture.Dispose(); }
        tracks.Clear();IsRecording = false;
        return seconds;
    }

    public void Dispose() { if (IsRecording) Stop(); }
}

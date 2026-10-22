// File: Tools/ProjectProject01DiagnosticsDashboard/EngineCsvProfileTests.cs
// Target: Win64 .NET 8 Windows desktop companion for Unreal Engine 5.8.2 diagnostic artifacts

namespace ProjectProject01DiagnosticsDashboard;

internal static class EngineCsvProfileTests
{
    internal static void Run(bool includeUi = true)
    {
        void Check(bool value, string name) { if (!value) throw new InvalidOperationException("Engine CSV profile: " + name); }
        string root = Path.Combine(Path.GetTempPath(), "ProjectDFSD-EngineCsv-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            // Mirrors FCsvStreamWriter output: short first header, a stat appended mid-capture, ';'-separated "name##seconds"
            // events, a data row whose event text starts with '[', a final full header row, then metadata with commandline last.
            string file = Path.Combine(root, "Profile(20260101_000000).csv");
            var lines = new List<string> { "EVENTS,FrameTime,GameThreadTime,RenderThreadTime,RHIThreadTime,GPUTime,Exclusive/GameThread/EventWait,Exclusive/GameThread/UI,Ticks/PlayerCharacter" };
            for (int i = 0; i < 100; ++i)
            {
                string events = i == 10 ? "GC##1.500000;Cmd: stat unit##1.600000" : i == 20 ? "[Bracketed] marker##2.0" : "";
                string extra = i >= 50 ? ",0.25" : "";
                lines.Add($"{events},{(i == 99 ? 200 : 50)},30,0,0.5,{45 + (i % 2)},25,2,8{extra}");
            }
            lines.Add("EVENTS,FrameTime,GameThreadTime,RenderThreadTime,RHIThreadTime,GPUTime,Exclusive/GameThread/EventWait,Exclusive/GameThread/UI,Ticks/PlayerCharacter,Exclusive/GameThread/GarbageCollection");
            lines.Add("[HasHeaderRowAtEnd],1,[EventTimestamps],1,[platform],Windows,[loginid],secretlogin,[starttimestamp],1767225600,[endtimestamp],1767225610,[captureduration],10.0,[commandline],\" C:\\Game\\Project.uproject -token=abc\"\"\"");
            File.WriteAllLines(file, lines);

            var result = EngineCsvProfile.Load(file, CancellationToken.None);
            Check(result.Frames == 100, "every data row counted, '[' event row is not metadata");
            Check(result.Stats.Any(s => s.Name == "Exclusive/GameThread/GarbageCollection" && s.Samples == 50 && Math.Abs(s.Mean - 0.25) < 1e-9), "appended column named from final header, mean over recorded frames");
            Check(Math.Abs(result.KeyOf("GPUTime")!.Mean - 45.5) < 1e-9 && result.KeyOf("FrameTime")!.Max == 200 && result.KeyOf("FrameTime")!.P95 == 50, "key series mean/p95/max");
            Check(result.Events.Count == 3 && result.Events[0] == new EngineCsvEvent(10, "GC", 1.5) && result.Events[2].Name == "[Bracketed] marker", "multiple events per frame and timestamp suffix");
            Check(!result.Metadata.ContainsKey("commandline") && !result.Metadata.ContainsKey("loginid") && result.Metadata["platform"] == "Windows", "command line and login id never kept");
            Check(result.CaptureSeconds == 10 && result.StartUtc == DateTimeOffset.FromUnixTimeSeconds(1767225600), "capture window metadata");
            var (verdict, rows) = EngineCsvProfile.Bottleneck(result);
            Check(verdict.StartsWith("GPU", StringComparison.Ordinal) && rows.Single(r => r.항목 == "게임 스레드 실작업").평균 == "2.25" && rows.Single(r => r.항목 == "렌더 스레드 실작업").평균 == "측정값 없음", "GPU bound; waits excluded from thread work");
            Check(!verdict.Contains("FPS 제한", StringComparison.Ordinal), "no throttle warning when work explains frame time");
            Check(EngineCsvProfile.Summary(result, []).Contains("정상 종료됨", StringComparison.Ordinal), "finalized capture recognized");
            Check(EngineCsvProfile.Top(result, 0).Select(r => r.항목).SequenceEqual(["Exclusive/GameThread/UI", "Exclusive/GameThread/GarbageCollection"]), "game thread list excludes waits");
            Check(EngineCsvProfile.Top(result, 5).Single().항목 == "Ticks/PlayerCharacter", "tick counts grouped separately");
            var start = result.StartUtc!.Value;
            var linked = EngineCsvProfile.OverlappingRuns(result, [("inside", start.AddSeconds(2), start.AddSeconds(4)), ("before", start.AddMinutes(-5), start.AddMinutes(-4)), ("edge", start.AddSeconds(12), start.AddSeconds(30))]);
            Check(linked.SequenceEqual(["inside", "edge"]), "diagnostic runs linked by overlapping time window only");
            Check(EngineCsvProfile.Summary(result, linked).Contains("r.GPUCsvStatsEnabled", StringComparison.Ordinal), "missing GPU pass stats explained, not zero-filled");

            Check(EngineCsvProfile.IsGpuPass("GPU/Lumen") && EngineCsvProfile.IsGpuPass("GPU2/BasePass") && !EngineCsvProfile.IsGpuPass("GPUMem/LocalUsedMB")
                && !EngineCsvProfile.IsGpuPass("GPUTime") && !EngineCsvProfile.IsGpuPass("GPUSceneInstanceCount") && !EngineCsvProfile.IsGpuPass("GPU/"), "GPU pass naming");
            string gpuFile = Path.Combine(root, "Profile(gpu).csv");
            File.WriteAllLines(gpuFile, [
                "EVENTS,GPUTime,GPU/BasePass,GPU/Lumen,GPU2/Shadows,GPUMem/LocalUsedMB",
                ",40,10,24,3,7000", ",44,12,26,5,7000",
                "EVENTS,GPUTime,GPU/BasePass,GPU/Lumen,GPU2/Shadows,GPUMem/LocalUsedMB",
                "[HasHeaderRowAtEnd],1,[captureduration],1.0"]);
            var gpu = EngineCsvProfile.Load(gpuFile, CancellationToken.None);
            var passes = EngineCsvProfile.TopGpuPasses(gpu);
            Check(gpu.HasGpuBreakdown && passes.Select(p => p.Name).SequenceEqual(["GPU/Lumen", "GPU/BasePass", "GPU2/Shadows"]) && Math.Abs(passes[0].ShareOfGpuTime!.Value - 25.0 / 42 * 100) < 1e-9,
                "GPU passes ranked with share of GPUTime; GPUMem excluded");
            Check(EngineCsvProfile.Summary(gpu, []).Contains("GPU/Lumen", StringComparison.Ordinal) && EngineCsvProfile.Top(gpu, 3).Count == 3 && EngineCsvProfile.Top(gpu, 6).All(r => r.항목 != "GPU/Lumen"),
                "GPU pass list shown in summary and grouped separately");
            var limited = EngineCsvProfile.Load(file, CancellationToken.None, maxFrames: 40);
            Check(limited.Frames == 40 && limited.Truncated && limited.Metadata.ContainsKey("captureduration"), "frame cap keeps metadata and marks truncation");
            var peaks = EngineCsvProfile.BucketPeaks("FrameTime", result.KeyFrames["FrameTime"], buckets: 10).ToList();
            Check(peaks.Count == 10 && peaks.Any(p => p.Y == 200), "chart input bounded and spikes retained");

            string bad = Path.Combine(root, "NotProfile.csv");
            File.WriteAllText(bad, "a,b\n1,2\n");
            bool rejected = false;
            try { EngineCsvProfile.Load(bad, CancellationToken.None); } catch (InvalidDataException) { rejected = true; }
            Check(rejected, "non-profiler CSV rejected");
            using (var cts = new CancellationTokenSource())
            {
                cts.Cancel(); bool cancelled = false;
                string big = Path.Combine(root, "Big.csv");
                File.WriteAllLines(big, lines.Take(1).Concat(Enumerable.Repeat(lines[2], 5000)));
                try { EngineCsvProfile.Load(big, cts.Token); } catch (OperationCanceledException) { cancelled = true; }
                Check(cancelled, "cancellation honored while streaming");
            }
            string diagnostics = Path.Combine(root, "Diagnostics"), run = Path.Combine(diagnostics, "20260101_000000_ABCDEF12");
            Directory.CreateDirectory(run); Directory.CreateDirectory(Path.Combine(diagnostics, "EmptyRun"));
            File.Copy(file, Path.Combine(run, EngineCsvProfile.RunCaptureFileName));
            File.WriteAllText(Path.Combine(run, "MergedPerformance.csv"), "UtcTime\n");
            var runFiles = EngineCsvProfile.ListRunCsvFiles(diagnostics);
            Check(runFiles.Count == 1 && runFiles[0].파일 == "20260101_000000_ABCDEF12/EngineCsvProfile.csv", "diagnostics-run captures listed, other run CSVs ignored");
            Check(EngineCsvProfile.RunIdFromPath(runFiles[0].FullPath, diagnostics) == "20260101_000000_ABCDEF12" && EngineCsvProfile.RunIdFromPath(file, diagnostics) is null, "run capture linked by folder");
            var listedFiles = EngineCsvProfile.ListCsvFiles(root);
            Check(listedFiles.Count == 4 && EngineCsvProfile.ListCsvFiles(Path.Combine(root, "missing")).Count == 0, "folder listing bounded and tolerant");

            // UI-BEGIN
            if (includeUi)
            {
                using var host = new Form { Width = 1200, Height = 720, ShowInTaskbar = false, Opacity = 0, StartPosition = FormStartPosition.Manual, Location = new Point(-32000, -32000) };
                var panel = new EngineCsvProfilePanel(); host.Controls.Add(panel);
                panel.SetContext(null, [("inside", start.AddSeconds(2), start.AddSeconds(4))]);
                Check(panel.LoadForTest(file)?.Frames == 100, "panel renders a capture");
                // Render evidence from the newest real capture when one exists (read-only), like the other dashboard UI tests.
                var real = EngineCsvProfile.ListCsvFiles(Path.GetFullPath("Saved/Profiling/CSV"), 1).FirstOrDefault();
                if (real is not null) panel.LoadForTest(real.FullPath);
                host.Show(); host.PerformLayout(); Application.DoEvents();
                string output = Path.GetFullPath("Saved/ReviewRemediation/EngineCsvProfile-20261023"); Directory.CreateDirectory(output);
                using (var image = new Bitmap(host.Width, host.Height)) { host.DrawToBitmap(image, new Rectangle(Point.Empty, host.Size)); image.Save(Path.Combine(output, "EngineCsvPanel.png")); }
                host.Hide();
            }
            // UI-END
        }
        finally
        {
            try { Directory.Delete(root, true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }
        Console.WriteLine("Engine CSV profile self-tests passed (format, appended columns, events, hidden metadata, bottleneck, linking, bounds).");
    }
}

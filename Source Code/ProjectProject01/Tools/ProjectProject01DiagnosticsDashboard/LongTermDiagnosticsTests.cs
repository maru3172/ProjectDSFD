using System.Text.Json;

namespace ProjectProject01DiagnosticsDashboard;

internal static class LongTermDiagnosticsTests
{
    internal static void Run(PerformanceSample sample)
    {
        int assertions = 0;
        void Check(bool value, string name) { ++assertions; if (!value) throw new InvalidOperationException("Long-term diagnostics: " + name); }
        static bool Near(double? actual, double expected) => actual.HasValue && Math.Abs(actual.Value - expected) < .000001;
        var empty = LongTermDiagnostics.Aggregate([(null, 1d), (double.NaN, 1d)]);
        Check(empty.ValidSamples == 0 && empty.Recommended is null, "unknown is not normal zero");
        Check(Near(LongTermDiagnostics.Aggregate([(1d, 1d), (3d, 9d)]).Average, 2.8), "time weighted mean");
        Check(Near(LongTermDiagnostics.Aggregate([(10d, 1d), (20d, 1d)], "interval").Recommended, 30), "interval counts summed");
        Check(Near(LongTermDiagnostics.Aggregate([(100d, 1d), (110d, 1d), (3d, 1d)], "cumulative").Total, 13), "counter first baseline excluded and reset handled");
        Check(LongTermDiagnostics.Aggregate([(100d, 1d)], "cumulative").Recommended is null, "one counter sample cannot establish increase");
        var state = LongTermDiagnostics.Aggregate([(1d, 1d), (2d, 1d), (0d, 1d)], "state");
        Check(state.Average is null && state.Total is null && state.Last == 0 && state.Maximum == 2, "state not averaged or summed");

        var legacy = LongTermDiagnostics.Game("legacy", "same map", [sample], []).Single();
        Check(RuntimeMetricAnalysis.Keys.All(k => legacy.Metrics[LongTermDiagnostics.GameKey(k)].ValidSamples == 0), "all legacy runtime counters unknown");
        Check(legacy.Metrics[LongTermDiagnostics.GameKey("GC 관측 횟수")].Recommended is null, "legacy absence cannot prove zero GC");
        var current = sample with { AllocatorUsedMiB = 100, AllocatorCommittedMiB = 150, AllocatorRetainedMiB = 50, AvailablePhysicalMiB = 2048,
            TotalPhysicalMiB = 4096, HasTextureCounters = true, GameSeconds = 0,
            RuntimeCounters = RuntimeMetricAnalysis.Keys.ToDictionary(k => k, k => k == "SampleWallSeconds" ? 5d : k == "HttpCompletedCount" ? 10d : 0d) };
        var second = current with { GameSeconds = 60, UtcTime = "2026-01-01T00:01:00Z", RuntimeCounters = new(current.RuntimeCounters) { ["HttpCompletedCount"] = 20, ["HttpServerFailureCount"] = 2 } };
        var peer = current with { Role = "Client_PIE2_P0" };
        var gc = new DiagnosticEvent(current.UtcTime, current.Role, current.NetMode, 10, "Normal", "GarbageCollectionMemoryDelta",
            "PhysicalBeforeBytes=100;PhysicalAfterBytes=120;VirtualBeforeBytes=200;VirtualAfterBytes=150;UObjectsBefore=50;UObjectsAfter=20;ElapsedMilliseconds=2;AllocatorBeforeAvailable=0;AllocatorAfterAvailable=0;AllocatorUsedBeforeBytes=0;AllocatorUsedAfterBytes=0", "");
        var native = LongTermDiagnostics.Game("current", "map + build + population", [current, second, peer], [gc]);
        var role = native.Single(c => c.Scope.StartsWith(current.Role, StringComparison.Ordinal));
        Check(native.Count == 2 && role.Samples == 2, "PIE roles remain separate, no shared counters added");
        Check(RuntimeMetricAnalysis.Keys.Concat(LongTermDiagnostics.AllocatorKeys).Concat(LongTermDiagnostics.SummaryKeys).All(k => role.Metrics.ContainsKey(LongTermDiagnostics.GameKey(k))), "native catalog coverage");
        Check(Near(role.Metrics[LongTermDiagnostics.GameKey("인증·로비 HTTP 처리량")].Recommended, 3), "native request rate uses full observed interval");
        Check(Near(role.Metrics[LongTermDiagnostics.GameKey("인증·로비 서버·전송 오류율")].Recommended, 100d * 2 / 30), "no rounded display string used for comparison");
        Check(role.Metrics[LongTermDiagnostics.GameKey("GC event.AllocatorUsedAfterBytes")].Recommended is null, "unsupported allocator zero sentinel ignored");
        Check(Near(role.Metrics[LongTermDiagnostics.GameKey("GC net reduction.PhysicalBytes")].Recommended, -20), "negative GC RAM reduction preserved");
        Check(Near(role.Metrics[LongTermDiagnostics.GameKey("GC net reduction.UObjectCount")].Recommended, 30), "actual UObject event field contract");
        Check(role.Notes.Any(n => n.Contains("할당자")), "qualitative cache findings preserved with capture");

        string id = Guid.NewGuid().ToString("N"); var start = DateTimeOffset.Parse("2026-01-01T00:00:00Z");
        var backend = new BackendMetricSnapshot(1, id, start, 42, 1,
            MetricLabels.Keys.ToDictionary(k => k, k => (double?)null), new() { ["FileDescriptors"] = "unavailable" }, []);
        backend.Values["HTTP.Completed"] = 1; backend.Values["HTTP.ServerErrorPercent"] = 100; backend.Values["Cache.Hits"] = 100;
        var later = backend with { UtcTime = start.AddSeconds(9), IntervalSeconds = 9, Values = new(backend.Values) {
            ["HTTP.Completed"] = 99, ["HTTP.ServerErrorPercent"] = 0, ["Cache.Hits"] = 110, ["New.FutureMetric"] = 12 },
            Failures = [new("Result recovery", "retry-scheduled", 503, 2, 2, 4, 10)] };
        var backendCapture = LongTermDiagnostics.Backend([backend, later, later], "fixture").Single();
        Check(backendCapture.Samples == 2, "overlapping JSONL timestamps deduplicated");
        Check(MetricLabels.Keys.All(k => backendCapture.Metrics.ContainsKey(LongTermDiagnostics.BackendKey(k))), "all backend metrics including unsupported represented");
        Check(Near(backendCapture.Metrics[LongTermDiagnostics.BackendKey("HTTP.ServerErrorPercent")].Recommended, 1), "error percentage weighted by requests, not time");
        Check(Near(backendCapture.Metrics[LongTermDiagnostics.BackendKey("Cache.Hits")].Total, 10), "cache counter capture delta not process lifetime total");
        Check(backendCapture.Metrics[LongTermDiagnostics.BackendKey("Process.FileDescriptors")].Recommended is null, "FD unavailable remains null");
        Check(Near(backendCapture.Metrics[LongTermDiagnostics.BackendKey("Errors.retry-scheduled.Count")].Total, 2), "retry details aggregated by kind");
        Check(Near(backendCapture.Metrics[LongTermDiagnostics.BackendKey("Error detail.Result recovery.retry-scheduled.HTTP503.NextDelaySeconds")].Maximum, 10), "retry delay and attempt details retained");
        Check(backendCapture.Metrics.ContainsKey(LongTermDiagnostics.BackendKey("New.FutureMetric")), "future numeric metrics discovered, not filtered out");
        string actualFile = Path.GetFullPath("Saved/ReviewRemediation/LightweightDiagnostics-20261022/BackendFixture.jsonl");
        if (File.Exists(actualFile))
        {
            var actualSnapshots = BackendMetricFiles.Load([actualFile]);
            var actualCaptures = LongTermDiagnostics.Backend(actualSnapshots, "actual backend DTO");
            Check(actualSnapshots.SelectMany(s => s.Values.Keys).Distinct().All(k => actualCaptures.Any(c => c.Metrics.ContainsKey(LongTermDiagnostics.BackendKey(k)))), "actual backend writer metric coverage, not only synthetic catalog");
        }
        Check(LongTermDiagnostics.Backend([backend, later with { CaptureId = Guid.NewGuid().ToString("N") }], "two").Count == 2, "captures not joined across restart");

        string directory = Path.GetFullPath(Path.Combine("Saved", "ReviewRemediation", "LongTermDiagnostics-20261022", Guid.NewGuid().ToString("N"))); Directory.CreateDirectory(directory);
        string raw = Path.Combine(directory, "full-ring.jsonl"), duplicate = Path.Combine(directory, "overlap.jsonl");
        File.WriteAllLines(raw, Enumerable.Range(0, 960).Select(i => JsonSerializer.Serialize(backend with { UtcTime = start.AddSeconds(i * 5), IntervalSeconds = 5 })));
        File.WriteAllText(duplicate, JsonSerializer.Serialize(backend));
        var full = DiagnosticHistoryFiles.ReadBackend([raw, duplicate]).Single();
        Check(full.Samples == 960, "long-term import includes entire 960-record ring, not display-only latest720");
        Check(Near(full.Metrics[LongTermDiagnostics.BackendKey("HTTP.Completed")].Total, 960), "overlap does not duplicate totals");
        string archive = Path.Combine(directory, "comparison.diagnostic-history.json");
        DiagnosticHistoryFiles.Save(archive, [.. native, backendCapture, full]);
        var roundTrip = DiagnosticHistoryFiles.Load(archive);
        Check(roundTrip.Count == 4 && roundTrip[2].Metrics[LongTermDiagnostics.BackendKey("New.FutureMetric")].Last == 12, "archive preserves metrics and scopes");
        Check(roundTrip[0].Metrics[LongTermDiagnostics.GameKey("GC event.AllocatorUsedAfterBytes")].Recommended is null, "archive preserves unknown values");
        bool invalid = false;
        try { DiagnosticHistoryFiles.Save(archive, [full with { Metrics = new() { ["invalid"] = new(double.NaN, null, null, null, null, null, 1) } }]); }
        catch (InvalidDataException) { invalid = true; }
        Check(invalid && DiagnosticHistoryFiles.Load(archive).Count == 4, "invalid write leaves existing archive intact");
        string oversized = Path.Combine(directory, "bad.jsonl"); File.WriteAllText(oversized, new string('x', 65537));
        invalid = false; try { DiagnosticHistoryFiles.ReadBackend([oversized]); } catch (InvalidDataException) { invalid = true; }
        Check(invalid, "bounded lines, not oversized allocations");

        using var host = new Form { Width = 1380, Height = 900, ShowInTaskbar = false, Opacity = 0, StartPosition = FormStartPosition.Manual, Location = new Point(-32000, -32000) };
        var panel = new LongTermDiagnosticsPanel(); host.Controls.Add(panel); panel.LoadForTest([.. native, backendCapture]);
        Check(LongTermDiagnostics.KnownKeys.All(k => panel.MetricKeysForTest.Contains(k)), "every known metric visible in long-term selector");
        Check(backendCapture.Metrics.Keys.All(k => panel.MetricKeysForTest.Contains(k)), "every actual backend metric visible, including error details");
        panel.FilterForTest("백엔드", "캐시");
        Check(panel.MetricKeysForTest.Count > 0 && panel.MetricKeysForTest.All(k => k.StartsWith("Backend | Cache.")), "scope and area filters");
        panel.FilterForTest("전체 범위", "전체 영역"); panel.SelectForTest(LongTermDiagnostics.GameKey("ProcessThreadCount"), "최대");
        Check(panel.VisibleRowsForTest == 2, "native role rows shown independently");
        panel.SelectForTest(LongTermDiagnostics.BackendKey("HTTP.ServerErrorPercent"), "권장 집계");
        Check(panel.VisibleRowsForTest == 1, "backend not incorrectly paired with game run IDs");
        string fixtureRoot = Path.Combine(directory, "NativeIntegration");
        string fixtureRole = Path.Combine(fixtureRoot, "history-test", sample.Role); Directory.CreateDirectory(fixtureRole);
        File.WriteAllText(Path.Combine(fixtureRole, "TestReport.xml"), $"<TestReport TestRunId=\"history-test\" TestName=\"History integration\" Role=\"{sample.Role}\" NetMode=\"{sample.NetMode}\" Map=\"Map\" GameMode=\"GameMode\" Outcome=\"Pass\"><Collection SampleCount=\"2\" RiskCandidateCount=\"0\"/></TestReport>");
        File.WriteAllText(Path.Combine(fixtureRole, "Performance.csv"), "UtcTime,Role,NetMode,GameSeconds,FramesPerSecond,SampleWallSeconds,ProcessThreadCount,HttpCompletedCount,HttpServerFailureCount\n" +
            $"2026-01-01T00:00:00Z,{sample.Role},{sample.NetMode},0,60,5,12,10,1\n2026-01-01T00:01:00Z,{sample.Role},{sample.NetMode},60,60,5,14,20,1\n");
        using (var form = new DashboardForm(fixtureRoot))
        {
            IEnumerable<Control> Descendants(Control root) => root.Controls.Cast<Control>().SelectMany(c => new[] { c }.Concat(Descendants(c)));
            var integrated = Descendants(form).OfType<LongTermDiagnosticsPanel>().Single();
            integrated.SelectForTest(LongTermDiagnostics.GameKey("ProcessThreadCount"), "최대");
            Check(integrated.VisibleRowsForTest == 1, "actual report loader automatically populates extended history");
            Check(Descendants(form).OfType<TabPage>().Count(t => t.Text == "진단 전체, 비교") == 1 &&
                Descendants(form).OfType<TabPage>().All(t => t.Text is not ("Long-term Comparison" or "추가 진단 전체 · 장기 추세" or "기존 성능·위험 비교")), "one unified comparison tab without old nested dialogs");
            Check(LegacyHistoryMetrics.Keys.All(k => integrated.MetricKeysForTest.Contains(LongTermDiagnostics.GameKey(k))), "all original performance/risk choices retained in unified selector");
            integrated.SelectForTest(LongTermDiagnostics.GameKey("Average FPS"), "권장 집계");
            Check(integrated.VisibleRowsForTest == 1, "legacy combined summary does not become duplicated per-role points");
            Check(LegacyHistoryMetrics.Keys.All(k => !System.Text.RegularExpressions.Regex.IsMatch(DiagnosticUiText.Metric(k), @"\b(?:Average|Max|Risk|frame|thread|overhead|memory|count)\b", System.Text.RegularExpressions.RegexOptions.IgnoreCase)), "all original metric display labels translated, keys unchanged");
            Check(LongTermDiagnostics.KnownKeys.All(k => !LongTermDiagnostics.Label(k).Contains("[",StringComparison.Ordinal)), "comparison labels no longer append English raw keys");
            Check(Descendants(form).OfType<TabPage>().All(t => !System.Text.RegularExpressions.Regex.IsMatch(t.Text, @"[A-Za-z]{3,}")), "all diagnostic tab labels localized");
            var selector=Descendants(form).OfType<ComboBox>().First(c=>c.Items.Contains("Frame ms"));
            Check(selector.GetItemText("Frame ms")=="프레임 시간 (밀리초)" && selector.Items.Contains("Frame ms"), "run-trend combo formats Korean without breaking stored metric keys");
            host.Controls.Remove(panel); form.Width=1380; form.Height=900;form.Opacity=0;form.ShowInTaskbar=false;form.Location=new Point(-32000,-32000);
            var page=Descendants(form).OfType<TabPage>().Single(t=>t.Text=="진단 전체, 비교");((TabControl)page.Parent!).SelectedTab=page;
            form.Show();form.PerformLayout();Application.DoEvents();
            using var preview=new Bitmap(form.Width,form.Height);form.DrawToBitmap(preview,new Rectangle(Point.Empty,form.Size));
            string evidenceRoot=Path.GetFullPath("Saved/ReviewRemediation/LongTermDiagnostics-20261022");Directory.CreateDirectory(evidenceRoot);preview.Save(Path.Combine(evidenceRoot,"UnifiedComparison.png"));form.Hide();
        }
        host.Show(); host.PerformLayout(); Application.DoEvents();
        string output = Path.GetFullPath("Saved/ReviewRemediation/LongTermDiagnostics-20261022"); Directory.CreateDirectory(output);
        using var image = new Bitmap(host.Width, host.Height); host.DrawToBitmap(image, new Rectangle(Point.Empty, host.Size)); image.Save(Path.Combine(output, "LongTermComparison.png")); host.Hide();
        File.WriteAllText(Path.Combine(output, "Checks.txt"), $"Long-term diagnostics: {assertions} assertions passed.\r\nNative/Backend catalog coverage, weighted aggregates, missing values, full ring, archives, UI.");
        File.WriteAllText(Path.Combine(output, "MetricCatalog.json"), JsonSerializer.Serialize(panel.MetricKeysForTest.Select(k => new { Key = k, Area = LongTermDiagnostics.Category(k), Label = LongTermDiagnostics.Label(k) }), new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"Long-term diagnostics self-tests passed ({assertions} assertions).");
    }
}

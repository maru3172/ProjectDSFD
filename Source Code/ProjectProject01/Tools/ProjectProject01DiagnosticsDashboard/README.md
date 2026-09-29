# ProjectProject01 Diagnostics Dashboard

`ProjectProject01DiagnosticsDashboard.csproj` is a separate Win64 .NET 8 WinForms program. It only reads diagnostic artifacts; it cannot control PIE, change AI, alter gameplay, or modify tuning data.

## Build and run

```powershell
dotnet build .\ProjectProject01DiagnosticsDashboard.csproj -c Release
dotnet run --project .\ProjectProject01DiagnosticsDashboard.csproj
```

Choose `ProjectProject01/Saved/Diagnostics`. The dashboard groups role-specific reports by `TestRunId`, merges their CSV files, detects position/possession/AI-state mismatch candidates, and launches the selected trace with the installed UE 5.8 Unreal Insights executable.

Search and filters cover TestRunId, map, game mode, outcome, role, player/mannequin count, and tuning fingerprint. The Run Trend tab draws real FPS, frame/Game/Draw/GPU, physical/virtual memory, rolling physical-memory growth per minute, UObject/Actor counts, aggregate streaming/non-streaming texture memory and texture-pool size, network, AI work, line-trace, and path-failure samples.

The aggregate `RiskCount` remains the quick overall indicator. `Risk Breakdown` explains that total by category and diagnostic code, including occurrence count, server/client role, related Actor, first/last game time, the rule that triggered the candidate, and up to three original evidence messages. These are review candidates, not confirmed root causes.

`Merged Timeline` supports field-specific searching over Role, NetMode, GameSeconds, Severity, Code, Message, and Actor. UtcTime is intentionally excluded from free-text search. Role, NetMode, Severity, Code, and Actor also provide an exact-value selector, and every table column can be clicked to alternate between ascending and descending order.

`Long-term Comparison` can graph the aggregate risk total, each risk category, every discovered diagnostic risk code, or an individual performance, memory, network, UObject/Actor, texture, or AI metric. The Summary compares a run with the most recent earlier run that has the same map, game mode, player count, and mannequin count, and labels materially changed values as improvement candidates, regression candidates, or neutral changes. This comparison and risk decomposition run only in the external dashboard; they add no runtime sampling work.

GC is never forced by diagnostics. When Unreal performs GC during a capture, `Events.csv` records the physical memory, virtual memory, and UObject count before GC and after `GarbageCollectComplete`. Texture figures are aggregate RHI counters; the lightweight collector does not enumerate individual assets. Process memory and UObject counts are shared by PIE worlds hosted in the same editor process, while Actor count is per world.

`DiagnosticsOverheadMilliseconds` measures the total elapsed CPU-side wall time of the once-per-second lightweight sample collection callback. The `Diagnostics Overhead` tab decomposes each role into Actor/AI scanning, navigation lookup, network-state lookup, frame/CPU/GPU counter queries, process-memory/UObject queries, texture/RHI-memory queries, and remaining risk/event/bookkeeping work, with average, maximum, and average percentage of total. Run Trend and Long-term Comparison expose the same sections separately while retaining the total.

These section values measure the CPU time spent collecting each category. `GPU` and `Texture/RHI` therefore mean the CPU cost of querying GPU/RHI counters, not additional GPU execution time. The total deliberately does not claim to include every engine trace, final CSV/XML write, or external dashboard rendering cost. Server/client timestamp alignment is calculated offline from matching `ActorStateChanged` events using UtcTime adjusted by GameSeconds; it is a clock-offset estimate and not a network-latency measurement.

The overhead table also reports an estimated one-core duty percentage at the one-second sampling interval and `Buffered diagnostic Samples / Events` in MiB. The latter is a lower-bound estimate of system RAM allocated by the diagnostic sample/event arrays and their strings; it is not VRAM and does not include every allocator, engine trace, or dashboard allocation. Compare diagnostic-OFF and diagnostic-ON process `Physical Memory` runs for the full RAM impact.

Opening or refreshing a run produces these derived artifacts without modifying gameplay data:

- `CombinedReport.xml`
- `MergedEvents.csv`
- `MergedPerformance.csv`

`CombinedReport.xml` also stores `RiskBreakdown`, `PerformanceSummary`, `MemorySummary`, `NetworkSummary`, `DiagnosticsOverheadSummary`, and `ServerClientTimeAlignment`, so the reason and raw evidence behind the aggregate risk count remain available without opening the dashboard.

`Run Build + Automation` builds `ProjectProject01Editor`, runs the `ProjectProject01.*` Automation tests through `UnrealEditor-Cmd`, and stores `Verification.xml`, `Verification/Build.log`, and `Verification/Automation.log`. A combined `Pass` requires role reports, build, Automation, and configured failure criteria to pass.

## Runtime collection contract

- The Unreal Editor menu is **Tools → ProjectProject01 Diagnostics**.
- Start it only after PIE starts. It creates a `TestRunId`, starts the rate-limited world collectors, and starts a default-channel `.utrace` only if tracing is otherwise idle.
- Stop it before ending PIE. Server/client worlds produce separate files under their role folders, sharing the same `TestRunId`; the dashboard treats them as one run.
- Samples run once per second and write on stop. AI timing counters run only while capture is active; expensive comparison and graph work remains in the external dashboard.
- A `RiskCandidate` is evidence for review, never automatic confirmation that a bug exists.

## Interpretation limits

- State mismatch is a time-tolerant candidate check (1.25 seconds, 150 cm), not proof of a replication defect.
- A mannequin uses `ControlSlot` as its cross-role identity only when the slot is non-negative. Unassigned `Slot=-1` mannequins use their PIE-prefix-normalized Actor path so separate mannequins are not collapsed into one identity.
- Server `AIController` versus client `None` is treated as the expected authoritative AI proxy state and is not reported as a controller mismatch.
- Engine Warning/Error collection is bounded to 512 recent records and records matching `Net`, `RPC`, or `replic` as networking candidates.
- Supported `FailureCriteria` rules are `RiskCount==N`, `WarningCount<=N`, `MinFPS>=N`, `MaxPathFailures<=N`, `MaxMemoryGrowthMiBPerSecond<=N`, and `MaxBandwidthBytesPerSecond<=N`, separated by semicolons. Unknown free-form text remains inconclusive.

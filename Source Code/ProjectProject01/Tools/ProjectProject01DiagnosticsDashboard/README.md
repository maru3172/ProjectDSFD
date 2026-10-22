# ProjectProject01 Diagnostics Dashboard

`ProjectProject01DiagnosticsDashboard.csproj` is a separate Win64 .NET 8 WinForms program. It reads diagnostic artifacts and can start/stop backend diagnostic recording with an administrator key; it cannot control PIE, change AI, alter gameplay, or modify tuning data.

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

`GarbageCollectionMemoryDelta` also records `ElapsedMilliseconds` from PreGC to GarbageCollectComplete. This is elapsed interval time, not guaranteed stop-the-world pause time or per-thread CPU work (especially with incremental GC). Sustained process growth is a leak **candidate**, not proof: cache/streaming and allocator retention can also raise the baseline. Compare repeated equivalent scenarios and post-GC baselines; the collector does not identify allocation stacks or automatically certify the absence of leaks.

## Cache and memory analysis

The **캐시·메모리** tab analyzes the selected capture offline. Run Trend also exposes allocator used/OS-held/retained memory, available system RAM, and streaming texture pool percentage. The existing CSV columns remain in place; optional counters are appended, and older captures show unavailable rather than fabricated zero values. `MergedPerformance.csv` and the `CacheMemoryAnalysis` section of `CombinedReport.xml` contain the new counters and findings.

- Allocator counters currently require the `TotalAllocated` / `TotalOSAllocated` accounting provided by supported Binned allocators. Their difference includes reusable cache, empty pools, alignment and fragmentation: it is not an exact cache size or OS working set. An allocator without those counters is explicitly unsupported. The normal Win64 editor automation on 2026-10-22 selected Mimalloc and correctly reported allocator counters unavailable; synthetic Binned accounting and real collector CSV alignment were tested separately. Do not change the production allocator merely to obtain diagnostic counters.
- A large retained pool alone is informational. Retained RAM is a review candidate only when at least 256 MiB and 30% of allocator OS-held memory coincide with low available system RAM (at most 512 MiB or below 10%). The condition must span at least five seconds with three or more consecutive observations and no gap exceeding 2.5 seconds.
- Streaming texture memory is compared with the configured streaming pool, not total GPU VRAM. Sustained usage at or above 95% is informational; sustained usage above 100% is a review candidate requiring engine budget logs and workload context. An unlimited/unknown pool or absent RHI counters has no inferred percentage. A null-RHI dedicated server does not have texture counters.
- Repeated post-GC allocator baselines require at least three supported GC records spanning 30 seconds. At least 64 MiB of growth is a review candidate, not proof of a leak: live game state, loaded assets and retained references can also grow. Physical-memory changes are reported separately. Counter values shared by worlds in a single PIE process are not summed as independent memory consumption.
- These checks do not measure global OS standby/file cache, GPU driver cache, CPU hardware cache hit/miss rates, Zen/DDC service process memory, or per-cache hit/miss ratios. They never clear caches, alter memory budgets or force GC during gameplay. Offline findings do not change the existing runtime `RiskCount`.

The **GC·프로세스·요청** tab now summarizes GC frequency/duration and net memory reductions, process handles/threads/TCP/UDP endpoints, UObject creation rate, instrumented Auth/Lobby request throughput/errors/timeouts/retries, and actual request circuit state. The **백엔드·풀 진단** tab supports admin-authenticated opt-in live capture, bounded offline JSONL import, metric charts and CSV export for backend HTTP/runtime/cache/thread-pool/DB/HTTP-pool/queue counters. See [the Korean scope, cost limits and operating guide](../../Docs/LightweightDiagnostics.md). `NetworkConnectionCount` still means Unreal NetDriver connections, not OS sockets. Unsupported counters are never manufactured as zero.

**진단 전체, 비교** unifies the original performance/risk comparisons and every newly collected native/backend metric in one selector; there are no separate legacy/extended subtabs. All metric choices, tabs, buttons and table headings use Korean display text. Internal keys and original logs/IDs remain unchanged for compatibility. Original run-wide metrics use the `전체 경기` scope, while new game metrics remain per-role. Source/area filters, appropriate summaries, per-capture tables, and offline archive save/reopen (`.diagnostic-history.json`) remain available. Native captures load from the existing report folder; import backend JSONL originals with **백엔드 원본 기록 가져오기**. This import includes the entire selected ring, not the live display's latest 720 samples. Save comparison summaries or original files before ring rotation overwrites them. Backend capture IDs are never silently paired with unrelated game runs. Unknown values stay null; aggregation semantics and bounded limits are documented in the Korean guide.

Verification: `dotnet run --project ProjectProject01DiagnosticsDashboard.csproj -- --self-test` checks legacy/unsupported counters, invalid values, retention versus pressure, texture near-budget versus over-budget, observation gaps and repeated GC baselines. Unreal automation `ProjectProject01.Diagnostics.CacheMemoryCounters` checks real collector export compatibility; its generated capture is a schema fixture, not a performance benchmark.

`DiagnosticsOverheadMilliseconds` measures the total elapsed CPU-side wall time of the once-per-second lightweight sample collection callback. The `Diagnostics Overhead` tab decomposes each role into Actor/AI scanning, navigation lookup, network-state lookup, frame/CPU/GPU counter queries, process-memory/UObject queries, texture/RHI-memory queries, and remaining risk/event/bookkeeping work, with average, maximum, and average percentage of total. Run Trend and Long-term Comparison expose the same sections separately while retaining the total.

These section values measure the CPU time spent collecting each category. `GPU` and `Texture/RHI` therefore mean the CPU cost of querying GPU/RHI counters, not additional GPU execution time. The total deliberately does not claim to include every engine trace, final CSV/XML write, or external dashboard rendering cost. Server/client timestamp alignment is calculated offline from matching `ActorStateChanged` events using UtcTime adjusted by GameSeconds; it is a clock-offset estimate and not a network-latency measurement.

The overhead table also reports an estimated one-core duty percentage at the one-second sampling interval and `Buffered diagnostic Samples / Events` in MiB. The latter is a lower-bound estimate of system RAM allocated by the diagnostic sample/event arrays and their strings; it is not VRAM and does not include every allocator, engine trace, or dashboard allocation. Compare diagnostic-OFF and diagnostic-ON process `Physical Memory` runs for the full RAM impact.

Opening or refreshing a run produces these derived artifacts without modifying gameplay data:

- `CombinedReport.xml`
- `MergedEvents.csv`
- `MergedPerformance.csv`

`CombinedReport.xml` also stores `RiskBreakdown`, `PerformanceSummary`, `MemorySummary`, `NetworkSummary`, `DiagnosticsOverheadSummary`, and `ServerClientTimeAlignment`, so the reason and raw evidence behind the aggregate risk count remain available without opening the dashboard.

**빌드·자동 검사**는 `ProjectDFSDEditor`를 빌드하고 `UnrealEditor-Cmd`에서 `ProjectProject01.*` 검사를 실행한다. 그래픽 검사를 포함하므로 `-nullrhi` 대신 화면 밖 렌더링(`-RenderOffscreen`)을 사용한다. 실행마다 `Verification/Attempts/<시각_고유번호>/`를 새로 만들고 `Build.log`, `Automation.log`, 엔진 원본 `Unreal.log`, `AutomationReport/index.json`, 해당 시도의 `Verification.xml`을 보관한다. 캡처 루트의 `Verification.xml`은 최신 시도를 가리킨다. 기존 시도나 과거 보고서는 삭제하거나 재사용하지 않는다.

프로세스 중단·보고서 누락·검사 실패·미완료·취소·시간 초과를 구분한다. 이번 보고서의 생성 시각·검사별 완료 상태·집계가 일치해야 성공이다. 종료 코드 0만으로 성공 처리하지 않으며, 보고서가 없을 때 과거의 성공 횟수나 가짜 0개를 표시하지 않는다. 경고 포함 성공 횟수는 따로 보존한다. 종합 `Pass`는 원래 역할 보고서/실패 조건, 빌드, 이번 자동 검사 모두 통과해야 한다. 기존 캡처의 위험 후보는 새 검사가 통과해도 지워지지 않는다. 새 진단 캡처를 만들지 않아도 자동 검사는 실행할 수 있다.

## Engine CSV Profiler captures

The **엔진 프로파일러 기록** tab reads Unreal Engine CSV Profiler output (`CsvProfile Start` / `CsvProfile Stop`, written to `Saved/Profiling/CSV/*.csv`). This is a different artifact from the project's lightweight diagnostics capture and from `.utrace` files; open `.utrace` with Unreal Insights and use this tab for the CSV.

- Capture: the editor menu **Tools → Start Lightweight Diagnostics Capture** now also starts the engine CSV Profiler into `Saved/Diagnostics/<RunId>/EngineCsvProfile.csv` and, only if it was off, enables `r.GPUCsvStatsEnabled` for GPU pass timers; **Stop and Save Diagnostics Capture** ends that CSV and restores the cvar. If another CSV capture is already running, the menu neither starts nor stops it (same ownership rule as the `.utrace`). Stop before ending PIE. Manual `CsvProfile Start/Stop` captures in `Saved/Profiling/CSV` are listed too.
- Cost: nothing runs until the tab is opened. Opening lists at most the 200 newest captures: `<RunId>/EngineCsvProfile.csv` in run folders (one level, newest 500 folders checked) plus `*.csv` in the CSV folder (default `Saved/Profiling/CSV`). A run-folder capture links to its own run directly. A file is read only when selected, once, on a background task, and only fixed-size per-column statistics plus five key frame-time series (`FrameTime`, `GameThreadTime`, `RenderThreadTime`, `RHIThreadTime`, `GPUTime`) are kept. The last 4 results are cached by path, size and write time. Limits: 1 GiB per file, 500,000 frames (truncation is reported), 8,192 columns, 2,000 events. Files are opened read-only and never modified.
- Format: the first header row may be shorter than the final header row written at the end of the file; columns added mid-capture are named from the final header. Events use `;` separators and an optional `##<seconds>` suffix. A missing metadata row is reported as an unfinished capture.
- Privacy: the `commandline` and `loginid` metadata entries are dropped and never displayed.
- Bottleneck estimate: thread work is the sum of that thread's `Exclusive/...` timers minus `EventWait` timers; GPU is `GPUTime`. When frame time is far larger than any measured work, the summary warns about a background-window frame limit, vsync, or a fixed frame rate. Per-pass GPU rows require the cvar below.
- GPU passes: with `r.GPUCsvStatsEnabled 1` (or `-csvGpuStats`) the engine writes `GPU/<pass>` columns (`GPU2/...` on multi-GPU) holding graphics-queue exclusive busy+wait milliseconds. The summary lists the 10 largest passes with their share of `GPUTime`; shares need not total 100% because async compute, idle gaps and unlabelled work are outside the pass timers. `GPUMem/*` and other `GPU...` counters are not treated as passes. Without the cvar the summary says so instead of showing zeros. The dashboard reuses the same single file pass, so this adds no extra reading.
- Linking: a capture is linked to diagnostics runs whose performance-sample wall-clock window overlaps the capture's `starttimestamp`–`endtimestamp` (±5 s). Start the project's lightweight capture during the same PIE session to get both views and the run's `.utrace`.
- Interpretation: editor PIE captures include editor UI, every PIE viewport, and the server in one process. Use them to compare like with like, not as packaged-build numbers.

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

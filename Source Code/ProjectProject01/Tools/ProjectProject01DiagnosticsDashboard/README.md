# ProjectProject01 Diagnostics Dashboard

`ProjectProject01DiagnosticsDashboard.csproj` is a separate Win64 .NET 8 WinForms program. It only reads diagnostic artifacts; it cannot control PIE, change AI, alter gameplay, or modify tuning data.

## Build and run

```powershell
dotnet build .\ProjectProject01DiagnosticsDashboard.csproj -c Release
dotnet run --project .\ProjectProject01DiagnosticsDashboard.csproj
```

Choose `ProjectProject01/Saved/Diagnostics`. The dashboard groups role-specific reports by `TestRunId`, merges their CSV files, detects position/possession/AI-state mismatch candidates, and launches the selected trace with the installed UE 5.8 Unreal Insights executable.

Search and filters cover TestRunId, map, game mode, outcome, role, player/mannequin count, and tuning fingerprint. The Run Trend tab draws real FPS, frame/Game/Draw/GPU, memory, network, AI work, line-trace, and path-failure samples. Long-term Comparison groups risk trends by map, player count, mannequin count, and tuning fingerprint without adding a third-party chart package.

Opening or refreshing a run produces these derived artifacts without modifying gameplay data:

- `CombinedReport.xml`
- `MergedEvents.csv`
- `MergedPerformance.csv`

`Run Build + Automation` builds `ProjectProject01Editor`, runs the `ProjectProject01.*` Automation tests through `UnrealEditor-Cmd`, and stores `Verification.xml`, `Verification/Build.log`, and `Verification/Automation.log`. A combined `Pass` requires role reports, build, Automation, and configured failure criteria to pass.

## Runtime collection contract

- The Unreal Editor menu is **Tools → ProjectProject01 Diagnostics**.
- Start it only after PIE starts. It creates a `TestRunId`, starts the rate-limited world collectors, and starts a default-channel `.utrace` only if tracing is otherwise idle.
- Stop it before ending PIE. Server/client worlds produce separate files under their role folders, sharing the same `TestRunId`; the dashboard treats them as one run.
- Samples run once per second and write on stop. AI timing counters run only while capture is active; expensive comparison and graph work remains in the external dashboard.
- A `RiskCandidate` is evidence for review, never automatic confirmation that a bug exists.

## Interpretation limits

- State mismatch is a time-tolerant candidate check (1.25 seconds, 150 cm), not proof of a replication defect.
- Engine Warning/Error collection is bounded to 512 recent records and records matching `Net`, `RPC`, or `replic` as networking candidates.
- Supported `FailureCriteria` rules are `RiskCount==N`, `WarningCount<=N`, `MinFPS>=N`, `MaxPathFailures<=N`, `MaxMemoryGrowthMiBPerSecond<=N`, and `MaxBandwidthBytesPerSecond<=N`, separated by semicolons. Unknown free-form text remains inconclusive.

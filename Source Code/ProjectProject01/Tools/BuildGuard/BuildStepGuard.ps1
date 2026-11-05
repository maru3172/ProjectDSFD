# Runs as a UBT pre/post build step (see Source/*.Target.cs) for every build, whatever starts it:
# Visual Studio, Rider, the editor, Claude, Codex or a script, on any Windows PC with this repo.
#
#   Pre : remember when this target's build started.
#   Post: if a source file was saved while the build ran, the .obj files that read it may hold the
#         old text but look newer than the file, so UBT would never recompile them (2026-11-04
#         crash, Docs/BuildGuard.md). Delete those .obj files and fail this build once, so the
#         next build recompiles them.
#
# Fail-safe: any problem inside this script itself skips the check and lets the build succeed.
# Windows PowerShell 5.1 compatible (no PowerShell 7 features). See Docs/BuildGuard.md.
param(
    [ValidateSet('Pre', 'Post')][string]$Phase = 'Post',
    [string]$Target = 'Unknown'
)
try {
    try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }
    $projectDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    $stampDir = Join-Path $projectDir 'Intermediate\BuildGuard'
    $safeTarget = ($Target -replace '[^A-Za-z0-9_.-]', '_')
    $stamp = Join-Path $stampDir ($safeTarget + '.start')
    # One short line per run, so it is easy to see when the guard ran and what it decided.
    $logFile = Join-Path $projectDir 'Saved\Logs\BuildGuard.log'
    function Write-GuardLog([string]$Text) {
        try {
            New-Item -ItemType Directory -Force -Path (Split-Path $logFile) | Out-Null
            Add-Content -LiteralPath $logFile -Value ((Get-Date).ToString('s') + " [$Target $Phase] " + $Text)
        } catch { }
    }

    if ($Phase -eq 'Pre') {
        New-Item -ItemType Directory -Force -Path $stampDir | Out-Null
        [System.IO.File]::WriteAllText($stamp, [DateTime]::UtcNow.Ticks.ToString())
        Write-GuardLog 'build start recorded'
        exit 0
    }

    if (Test-Path -LiteralPath $stamp) {
        $ticks = [long]([System.IO.File]::ReadAllText($stamp).Trim())
        Remove-Item -LiteralPath $stamp -Force -ErrorAction SilentlyContinue
        $startedUtc = New-Object DateTime $ticks, ([DateTimeKind]::Utc)
    }
    else {
        # No start record: UBT skips pre-build steps on the first build after a Target.cs change.
        # This step runs under the UnrealBuildTool process that is doing the build, so its start
        # time is the build's start time.
        $startedUtc = $null
        $processId = $PID
        for ($depth = 0; $depth -lt 12 -and $processId; $depth++) {
            $process = Get-CimInstance Win32_Process -Filter "ProcessId=$processId" -ErrorAction SilentlyContinue
            if (-not $process) { break }
            if ([string]$process.CommandLine -match 'UnrealBuildTool') { $startedUtc = $process.CreationDate.ToUniversalTime(); break }
            $processId = $process.ParentProcessId
        }
        if (-not $startedUtc) { Write-GuardLog 'no start record and no UnrealBuildTool parent; skipped'; exit 0 }
        Write-GuardLog ('no start record; using UnrealBuildTool start ' + $startedUtc.ToLocalTime().ToString('s'))
    }
    # One second of slack for file-system timestamp resolution.
    $started = $startedUtc.AddSeconds(-1)

    $sourceDir = Join-Path $projectDir 'Source'
    $changed = @(Get-ChildItem -LiteralPath $sourceDir -Recurse -File -Include *.h, *.hpp, *.inl, *.cpp, *.c -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTimeUtc -ge $started })
    if ($changed.Count -eq 0) { Write-GuardLog ('ok; no source saved since ' + $started.ToLocalTime().ToString('s')); exit 0 }

    $names = @($changed | ForEach-Object { $_.FullName.ToLowerInvariant() })
    $removed = 0
    $buildDir = Join-Path $projectDir 'Intermediate\Build'
    foreach ($dep in Get-ChildItem -LiteralPath $buildDir -Recurse -File -Filter *.dep.json -ErrorAction SilentlyContinue) {
        $text = [System.IO.File]::ReadAllText($dep.FullName).ToLowerInvariant().Replace('\\', '\')
        $hit = $false
        foreach ($n in $names) { if ($text.Contains($n)) { $hit = $true; break } }
        if (-not $hit) { continue }
        $obj = $dep.FullName -replace '\.dep\.json$', '.obj'
        if (Test-Path -LiteralPath $obj) { Remove-Item -LiteralPath $obj -Force -ErrorAction SilentlyContinue; $removed++ }
    }

    $list = ($changed | Select-Object -First 8 | ForEach-Object { $_.Name }) -join ', '
    if ($removed -eq 0) {
        # Nothing compiled reads these files, so no stale object can exist: only mention it.
        Write-Host "BuildGuard: note: '$Target' build saw saved files that no compiled object uses: $list"
        Write-GuardLog "note; saved during build but unused: $list"
        exit 0
    }
    Write-GuardLog "FAILED build; saved during build: $list; deleted $removed object file(s)"
    Write-Host "BuildGuard: error: source files were saved while '$Target' was compiling: $list"
    Write-Host "BuildGuard: deleted $removed object file(s) that may contain the old code. Build again (no edits during the build); the next build recompiles them."
    exit 1
}
catch {
    # "warning:" makes Visual Studio/Rider list it; the log keeps it after the output scrolls away.
    $message = "BuildGuard: warning: check skipped for '$Target' ($($_.Exception.Message)). The build is fine; fix Tools/BuildGuard/BuildStepGuard.ps1 so the guard runs again."
    Write-Host $message
    try {
        $logDir = Join-Path (Join-Path $PSScriptRoot '..\..') 'Saved\Logs'
        New-Item -ItemType Directory -Force -Path $logDir | Out-Null
        Add-Content -LiteralPath (Join-Path $logDir 'BuildGuard.log') -Value ((Get-Date).ToString('s') + ' ' + $message)
    } catch { }
    exit 0
}

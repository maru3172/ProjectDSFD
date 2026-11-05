# Builds ProjectDFSDEditor and then checks that no source file changed while it compiled.
# A file saved mid-build can leave an .obj compiled from its old text but stamped newer than
# the file, which UBT then never recompiles. When that happens this script deletes the .obj files
# that depend on the changed files and builds once more. See Docs/BuildGuard.md.
#
#   pwsh -File Tools\BuildGuard\SafeBuild.ps1 [-Target ProjectDFSDEditor] [-Configuration Development]
param(
    [string]$Target = 'ProjectDFSDEditor',
    [string]$Configuration = 'Development',
    [string]$EngineDir = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Log = ''
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$projectDir = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$uproject = Join-Path $projectDir 'ProjectDFSD.uproject'
$sourceDir = Join-Path $projectDir 'Source'
$intermediate = Join-Path $projectDir 'Intermediate\Build\Win64'
if (-not $Log) { $Log = Join-Path $projectDir 'Saved\Logs\SafeBuild.log' }

$blockers = Get-Process -Name 'UnrealEditor', 'LiveCodingConsole', 'UnrealBuildTool' -ErrorAction SilentlyContinue
if ($blockers) {
    Write-Error ("에디터/Live Coding/UBT가 실행 중이라 빌드하지 않습니다: " + (($blockers | Select-Object -ExpandProperty ProcessName -Unique) -join ', '))
    exit 3
}

function Invoke-Build([string]$Label) {
    $started = Get-Date
    "[$Label] $started 빌드 시작" | Tee-Object -FilePath $Log -Append | Out-Host
    & (Join-Path $EngineDir 'Engine\Build\BatchFiles\Build.bat') $Target Win64 $Configuration "-Project=$uproject" -WaitMutex *>> $Log
    $code = $LASTEXITCODE
    "[$Label] 종료 코드 $code" | Tee-Object -FilePath $Log -Append | Out-Host
    return @{ Started = $started; Code = $code }
}

function Get-ChangedDuring([datetime]$Started) {
    # One second of slack for file-system timestamp resolution.
    Get-ChildItem $sourceDir -Recurse -File -Include *.h, *.hpp, *.inl, *.cpp, *.c, *.cs |
        Where-Object { $_.LastWriteTime -ge $Started.AddSeconds(-1) }
}

function Remove-DependentObjects($Changed) {
    $names = $Changed | ForEach-Object { $_.FullName.ToLowerInvariant().Replace('/', '\') }
    $removed = @()
    foreach ($dep in Get-ChildItem $intermediate -Recurse -File -Filter *.dep.json) {
        $text = (Get-Content -LiteralPath $dep.FullName -Raw).ToLowerInvariant().Replace('\\', '\')
        if (-not ($names | Where-Object { $text.Contains($_) })) { continue }
        $obj = $dep.FullName -replace '\.dep\.json$', '.obj'
        if (Test-Path -LiteralPath $obj) { Remove-Item -LiteralPath $obj -Force; $removed += $obj }
    }
    return $removed
}

$first = Invoke-Build 'build'
$changed = @(Get-ChangedDuring $first.Started)
if ($changed.Count -eq 0) {
    "빌드 중 바뀐 소스 없음." | Tee-Object -FilePath $Log -Append | Out-Host
    exit $first.Code
}
"빌드 중 바뀐 소스 $($changed.Count)개: " + (($changed | ForEach-Object Name) -join ', ') | Tee-Object -FilePath $Log -Append | Out-Host
$removed = @(Remove-DependentObjects $changed)
"이 파일들에 의존하는 .obj $($removed.Count)개를 지우고 다시 빌드합니다." | Tee-Object -FilePath $Log -Append | Out-Host
$second = Invoke-Build 'rebuild'
$again = @(Get-ChangedDuring $second.Started)
if ($again.Count -gt 0) {
    Write-Warning ("다시 빌드하는 동안에도 소스가 바뀌었습니다: " + (($again | ForEach-Object Name) -join ', ') + ". 편집을 멈춘 뒤 이 스크립트를 다시 실행하세요.")
    exit 4
}
exit $second.Code

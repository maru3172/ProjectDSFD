# Claude Code PreToolUse hook: refuse edits to Unreal source files while a build is compiling.
# Why: UBT decides what to recompile by file timestamps. A source/header saved while the compiler
# is already reading it leaves an .obj built from the old text but stamped newer than the file,
# so later builds skip it (stale class layout -> crash, see Docs/BuildGuard.md).
# Exit 2 blocks the tool call and shows stderr to Claude. Any other failure lets the edit through.
$ErrorActionPreference = 'Stop'
try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $payload = [Console]::In.ReadToEnd() | ConvertFrom-Json
    $path = [string]$payload.tool_input.file_path
    if (-not $path) { $path = [string]$payload.tool_input.notebook_path }
    if (-not $path) { exit 0 }
    $full = [System.IO.Path]::GetFullPath($path).Replace('/', '\')
    # This repo's Source folder, wherever the repo is checked out.
    $sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\Source')).TrimEnd('\') + '\'
    if (-not $full.StartsWith($sourceRoot, [System.StringComparison]::OrdinalIgnoreCase)) { exit 0 }
    if ($full -notmatch '\.(h|hpp|inl|cpp|c|cs)$') { exit 0 }

    $names = 'UnrealBuildTool', 'cl', 'link', 'UbaHost', 'UbaCli', 'clang-cl', 'lld-link'
    $running = Get-Process -Name $names -ErrorAction SilentlyContinue
    if ($running) {
        $list = ($running | Select-Object -ExpandProperty ProcessName -Unique) -join ', '
        [Console]::Error.WriteLine("빌드가 실행 중이라 소스 편집을 막았습니다 ($list). 빌드 도중 저장한 헤더/소스는 낡은 .obj를 남겨 크래시를 일으킬 수 있습니다. 빌드가 끝난 뒤 다시 편집하세요. 파일: $full")
        exit 2
    }
    exit 0
}
catch {
    exit 0
}

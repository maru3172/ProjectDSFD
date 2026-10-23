param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8\Engine',
    [switch]$NullRhi
)
$ErrorActionPreference = 'Stop'
# No build, packaging, database, server administration or persistent PIE-setting changes.
$botProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$botProjectFile = Join-Path $botProjectRoot 'ProjectDFSD.uproject'
$botEditor = Join-Path $EngineRoot 'Binaries\Win64\UnrealEditor-Cmd.exe'
if (!(Test-Path -LiteralPath $botProjectFile) -or !(Test-Path -LiteralPath $botEditor)) {
    throw 'ProjectDFSD.uproject or UnrealEditor-Cmd.exe is missing. Supply -EngineRoot if necessary.'
}
$botRunId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$botReport = Join-Path $botProjectRoot ('Saved\AdaptiveBotChecks\Repro-' + $botRunId)
$botRegression = @('ProjectProject01.Elevator.Flow','ProjectProject01.Multiplayer.Result.OpaqueHandoff','ProjectProject01.Multiplayer.Spectator.SharedPresentation','ProjectProject01.Review.MatchCarryover','ProjectProject01.Review.TravelRollback','ProjectProject01.Review.NetworkIsolation','ProjectProject01.Review.SingleAIState')
$botFilters = 'ProjectProject01.Bots.+ProjectProject01.Editor.ActualBotMatch+ProjectProject01.Editor.PracticeEnvironment+ProjectProject01.Editor.TutorialPractice+ProjectProject01.Multiplayer.TutorialFlow+ProjectProject01.Multiplayer.PracticeDirectorCommands+ProjectProject01.Multiplayer.ViewOnlyCommands+ProjectProject01.Operations.LobbyReadability+' + ($botRegression -join '+')
$botArguments = @($botProjectFile, '-unattended', '-nop4', '-nosplash', '-NoSound', '-ResX=1280', '-ResY=720',
    '-TestExit=Automation Test Queue Empty', ('-ExecCmds=Automation RunTests ' + $botFilters),
    ('-ReportExportPath=' + $botReport), ('-abslog=' + $botReport + '.log'), ('-ProjectProject01ResultOutbox=' + $botReport + '\Outbox'))
if ($NullRhi) { $botArguments += '-nullrhi' } else { $botArguments += '-RenderOffscreen' }
Write-Output ('Automation report: ' + $botReport)
& $botEditor @botArguments
$botExit = $LASTEXITCODE
$botIndex = Join-Path $botReport 'index.json'
if (!(Test-Path -LiteralPath $botIndex)) { throw ('No fresh automation report. Editor exit code: ' + $botExit) }
$botResult = Get-Content -LiteralPath $botIndex -Raw -Encoding UTF8 | ConvertFrom-Json
$botExpected = @('ProjectProject01.Bots.AdaptivePolicy', 'ProjectProject01.Bots.TrustedRoster', 'ProjectProject01.Multiplayer.TutorialFlow',
    'ProjectProject01.Multiplayer.PracticeDirectorCommands', 'ProjectProject01.Multiplayer.ViewOnlyCommands', 'ProjectProject01.Operations.LobbyReadability')
foreach ($botRole in @('Mannequin','Survivor')) {
    foreach ($botState in @('On','Off')) { $botExpected += "ProjectProject01.Editor.PracticeEnvironment.$botRole.$botState" }
    foreach ($botLevel in 1..5) { $botExpected += "ProjectProject01.Editor.ActualBotMatch.$botRole.Level$botLevel" }
}
foreach ($botRole in @('Mannequin','Survivor','FreeMannequin','FreeSurvivor')) {
    $botExpected += ('ProjectProject01.Editor.TutorialPractice.' + $botRole)
}
$botExpected += @('ProjectProject01.Editor.ActualBotMatch.Observer.Free','ProjectProject01.Editor.ActualBotMatch.Observer.Locked')
$botExpected += $botRegression
foreach ($botName in $botExpected) {
    $botTest = @($botResult.tests | Where-Object { $_.fullTestPath -eq $botName })
    if ($botTest.Count -ne 1 -or $botTest[0].state -ne 'Success') {
        throw ('Missing or failed test: ' + $botName + '. Inspect ' + $botIndex)
    }
}
if ($botExit -ne 0 -or $botResult.failed -gt 0 -or $botResult.inProcess -gt 0 -or $botResult.notRun -gt 0) {
    throw ('Automation did not finish cleanly. Inspect ' + $botIndex)
}
Write-Output ('PASS: ' + $botExpected.Count + ' engine scenarios; warnings retained in the report.')
Write-Output 'These checks do not prove multiplayer admission, EOS voice, balancing, or actual gameplay enjoyment.'

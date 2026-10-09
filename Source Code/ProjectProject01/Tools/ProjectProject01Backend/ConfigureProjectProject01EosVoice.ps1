param(
    [ValidateSet('Configure', 'Status', 'Clear')]
    [string]$Action = 'Configure'
)

$ErrorActionPreference = 'Stop'

$names = @(
    'PROJECTPROJECT01_EOS_PRODUCT_ID',
    'PROJECTPROJECT01_EOS_SANDBOX_ID',
    'PROJECTPROJECT01_EOS_DEPLOYMENT_ID',
    'PROJECTPROJECT01_EOS_VOICE_SERVER_CLIENT_ID',
    'PROJECTPROJECT01_EOS_VOICE_SERVER_CLIENT_SECRET',
    'PROJECTPROJECT01_EOS_GAME_CLIENT_SECRET',
    'PROJECTPROJECT01_EOS_SDK_PATH'
)

function Set-UserAndProcessEnvironment([string]$Name, [string]$Value) {
    [Environment]::SetEnvironmentVariable($Name, $Value, 'User')
    [Environment]::SetEnvironmentVariable($Name, $Value, 'Process')
}

function Read-SecretText([string]$Prompt) {
    $secure = Read-Host $Prompt -AsSecureString
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    }
}

if ($Action -eq 'Clear') {
    foreach ($name in $names) {
        [Environment]::SetEnvironmentVariable($name, $null, 'User')
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    Write-Host 'ProjectProject01 EOS voice environment variables were removed.' -ForegroundColor Yellow
    exit 0
}

if ($Action -eq 'Status') {
    foreach ($name in $names) {
        $present = -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name, 'User'))
        $label = if ($present) { 'configured' } else { 'missing' }
        Write-Host ("{0}: {1}" -f $name, $label)
    }
    exit 0
}

Write-Host 'Copy each Client Secret from Epic Developer Portal > Product Settings > Clients.' -ForegroundColor Cyan
Write-Host 'Secret input is hidden and is not written to source files or Git.' -ForegroundColor Cyan
$gameClientSecret = Read-SecretText 'ProjectProject01GameClient Client Secret'
$voiceServerSecret = Read-SecretText 'ProjectProject01VoiceServer Client Secret'
if ([string]::IsNullOrWhiteSpace($gameClientSecret) -or [string]::IsNullOrWhiteSpace($voiceServerSecret)) {
    throw 'Both EOS client secrets are required.'
}
if ($gameClientSecret.Length -lt 32 -or $gameClientSecret.Length -gt 64 -or
    $voiceServerSecret.Length -lt 32 -or $voiceServerSecret.Length -gt 64) {
    throw 'EOS Client Secret must be the complete 32-64 character value issued by Epic Developer Portal.'
}

Set-UserAndProcessEnvironment 'PROJECTPROJECT01_EOS_PRODUCT_ID' '972ccc2fb54a4121ad7f33b595144f3f'
Set-UserAndProcessEnvironment 'PROJECTPROJECT01_EOS_SANDBOX_ID' '04077b4c35d24156a22bf62a8401554e'
Set-UserAndProcessEnvironment 'PROJECTPROJECT01_EOS_DEPLOYMENT_ID' '8fc294c603da4475b78a93d710508256'
Set-UserAndProcessEnvironment 'PROJECTPROJECT01_EOS_VOICE_SERVER_CLIENT_ID' 'xyza78912dHVjy9vVmmNaS6KCv0Aj08M'
Set-UserAndProcessEnvironment 'PROJECTPROJECT01_EOS_GAME_CLIENT_SECRET' $gameClientSecret
Set-UserAndProcessEnvironment 'PROJECTPROJECT01_EOS_VOICE_SERVER_CLIENT_SECRET' $voiceServerSecret

$sdkCandidates = @(
    $env:PROJECTPROJECT01_EOS_SDK_PATH,
    'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\EOSSDK-Win64-Shipping.dll'
)
$sdkPath = $sdkCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
if ($sdkPath) {
    Set-UserAndProcessEnvironment 'PROJECTPROJECT01_EOS_SDK_PATH' $sdkPath
}

$gameClientSecret = $null
$voiceServerSecret = $null
Write-Host 'EOS voice credentials configured. Restart the helper and Unreal Editor before testing.' -ForegroundColor Green
if (-not $sdkPath) {
    Write-Host 'EOS SDK DLL was not found automatically. Set PROJECTPROJECT01_EOS_SDK_PATH to EOSSDK-Win64-Shipping.dll.' -ForegroundColor Yellow
}

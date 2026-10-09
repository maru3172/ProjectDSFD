param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Status', 'MaintenanceOn', 'MaintenanceOff', 'Announce', 'ClearAnnouncement', 'ForceLogoutAll', 'ForceLogoutAccount')]
    [string]$Action,
    [string]$Message = '',
    [string]$AccountId = '',
    [Nullable[datetime]]$ShutdownAtUtc = $null,
    [string]$BaseUrl = 'http://127.0.0.1:5080',
    [string]$AdminKey = ''
)

$ErrorActionPreference = 'Stop'
$configurationPath = Join-Path $PSScriptRoot 'LocalMySql\ProjectProject01Backend.local.json'
if ([string]::IsNullOrWhiteSpace($AdminKey) -and (Test-Path -LiteralPath $configurationPath)) {
    $configuration = Get-Content -LiteralPath $configurationPath -Raw | ConvertFrom-Json
    $AdminKey = $configuration.Administrator.ApiKey
}
if ([string]::IsNullOrWhiteSpace($AdminKey)) {
    throw '관리자 API 키가 없습니다. ProjectProject01MySql.ps1 -Action SyncLocalConfiguration을 먼저 실행하세요.'
}

$headers = @{ 'X-ProjectProject01-Admin-Key' = $AdminKey }
$BaseUrl = $BaseUrl.TrimEnd('/')

function Invoke-ServiceUpdate([bool]$Maintenance, [string]$Announcement, [Nullable[datetime]]$Shutdown) {
    $body = @{
        maintenanceEnabled = $Maintenance
        announcement = $Announcement
        shutdownAtUtc = if ($null -ne $Shutdown) { $Shutdown.Value.ToUniversalTime().ToString('O') } else { $null }
    } | ConvertTo-Json
    # Windows PowerShell 5.1 may encode a String body with the system ANSI code page.
    # Sending explicit UTF-8 bytes keeps Korean announcements intact end-to-end.
    $bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($body)
    Invoke-RestMethod -Method Put -Uri "$BaseUrl/api/admin/service" -Headers $headers -ContentType 'application/json; charset=utf-8' -Body $bodyBytes
}

function Get-ServiceStatus {
    Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/admin/service" -Headers $headers
}

switch ($Action) {
    'Status' { Get-ServiceStatus }
    'MaintenanceOn' { Invoke-ServiceUpdate $true $Message $ShutdownAtUtc }
    'MaintenanceOff' { Invoke-ServiceUpdate $false $Message $ShutdownAtUtc }
    'Announce' {
        $current = Get-ServiceStatus
        Invoke-ServiceUpdate ([bool]$current.maintenanceEnabled) $Message $ShutdownAtUtc
    }
    'ClearAnnouncement' {
        $current = Get-ServiceStatus
        Invoke-ServiceUpdate ([bool]$current.maintenanceEnabled) '' $null
    }
    'ForceLogoutAll' { Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/admin/sessions/revoke-all" -Headers $headers }
    'ForceLogoutAccount' {
        if ([string]::IsNullOrWhiteSpace($AccountId)) { throw '-AccountId가 필요합니다.' }
        $encoded = [Uri]::EscapeDataString($AccountId.Trim())
        Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/admin/users/$encoded/sessions/revoke" -Headers $headers
    }
}

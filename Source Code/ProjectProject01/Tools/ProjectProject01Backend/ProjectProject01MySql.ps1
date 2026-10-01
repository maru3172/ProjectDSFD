# File: Tools/ProjectProject01Backend/ProjectProject01MySql.ps1
# Target: Windows PowerShell 5.1 or PowerShell 7 / MySQL Server 8.0

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Initialize', 'Start', 'Stop', 'Status', 'SyncLocalConfiguration')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'
$port = 3307
$bindAddress = '127.0.0.1'
$toolDirectory = Split-Path -Parent $PSCommandPath
$projectFile = Join-Path $toolDirectory 'ProjectProject01Backend.csproj'
$instanceDirectory = Join-Path $toolDirectory 'LocalMySql'
$dataDirectory = Join-Path $instanceDirectory 'Data'
$pidFile = Join-Path $instanceDirectory 'ProjectProject01MySql.pid'
$errorLog = Join-Path $instanceDirectory 'ProjectProject01MySql.log'
$provisionedMarker = Join-Path $instanceDirectory 'Provisioned.marker'
$localConfigurationFile = Join-Path $instanceDirectory 'ProjectProject01Backend.local.json'
$mysqlBaseDirectory = Join-Path $env:ProgramFiles 'MySQL\MySQL Server 8.0'
$mysqldPath = Join-Path $mysqlBaseDirectory 'bin\mysqld.exe'
$mysqlPath = Join-Path $mysqlBaseDirectory 'bin\mysql.exe'
$mysqlAdminPath = Join-Path $mysqlBaseDirectory 'bin\mysqladmin.exe'

function Set-ProcessArguments {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.ProcessStartInfo]$StartInfo,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    # ProcessStartInfo.ArgumentList is available in PowerShell 7/.NET, but not
    # in Windows PowerShell 5.1/.NET Framework. All arguments created by this
    # script are controlled locally and never contain embedded quote marks.
    $argumentListProperty = $StartInfo.PSObject.Properties['ArgumentList']
    if ($null -ne $argumentListProperty -and $null -ne $StartInfo.ArgumentList) {
        foreach ($argument in $Arguments) {
            $StartInfo.ArgumentList.Add($argument)
        }
        return
    }

    $quotedArguments = foreach ($argument in $Arguments) {
        if ($argument.Contains('"')) {
            throw 'Process arguments containing quote marks are not supported.'
        }
        if ($argument -match '\s') {
            '"' + $argument + '"'
        }
        else {
            $argument
        }
    }
    $StartInfo.Arguments = $quotedArguments -join ' '
}

function Set-ProcessEnvironmentValue {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.ProcessStartInfo]$StartInfo,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    $environmentProperty = $StartInfo.PSObject.Properties['Environment']
    if ($null -ne $environmentProperty -and $null -ne $StartInfo.Environment) {
        $StartInfo.Environment[$Name] = $Value
    }
    else {
        $StartInfo.EnvironmentVariables[$Name] = $Value
    }
}

function Assert-Requirements {
    foreach ($requiredPath in @($projectFile, $mysqldPath, $mysqlPath, $mysqlAdminPath)) {
        if (-not (Test-Path -LiteralPath $requiredPath)) {
            throw "Required file was not found: $requiredPath"
        }
    }
}

function Test-ProjectProject01Port {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $connectTask = $client.ConnectAsync($bindAddress, $port)
        if (-not $connectTask.Wait(500)) {
            return $false
        }
        return $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

function Start-ProjectProject01Server {
    if (Test-ProjectProject01Port) {
        Write-Host "ProjectProject01 MySQL is already listening on ${bindAddress}:$port."
        return
    }
    if (-not (Test-Path -LiteralPath (Join-Path $dataDirectory 'mysql'))) {
        throw "The isolated data directory is not initialized. Run with -Action Initialize first."
    }

    New-Item -ItemType Directory -Path $instanceDirectory -Force | Out-Null
    $arguments = @(
        '--no-defaults',
        "--basedir=$mysqlBaseDirectory",
        "--datadir=$dataDirectory",
        "--port=$port",
        "--bind-address=$bindAddress",
        '--default-time-zone=+00:00',
        '--mysqlx=OFF',
        '--local-infile=OFF',
        '--secure-file-priv=NULL',
        "--pid-file=$pidFile",
        "--log-error=$errorLog"
    )
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $mysqldPath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    Set-ProcessArguments -StartInfo $startInfo -Arguments $arguments
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw 'Failed to start the isolated ProjectProject01 MySQL process.'
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-ProjectProject01Port) {
            Write-Host "ProjectProject01 MySQL started on ${bindAddress}:$port."
            return
        }
        Start-Sleep -Milliseconds 250
    }
    throw "ProjectProject01 MySQL did not start within 30 seconds. Check $errorLog"
}

function Invoke-MySqlInput {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [string]$StandardInput,
        [string]$Password
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.RedirectStandardInput = -not [string]::IsNullOrEmpty($StandardInput)
    Set-ProcessArguments -StartInfo $startInfo -Arguments $Arguments
    if (-not [string]::IsNullOrEmpty($Password)) {
        Set-ProcessEnvironmentValue -StartInfo $startInfo -Name 'MYSQL_PWD' -Value $Password
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Failed to start $Executable"
    }
    if ($startInfo.RedirectStandardInput) {
        $process.StandardInput.Write($StandardInput)
        $process.StandardInput.Close()
    }
    $standardOutput = $process.StandardOutput.ReadToEnd()
    $standardError = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "$Executable failed with exit code $($process.ExitCode): $standardError"
    }
    if (-not [string]::IsNullOrWhiteSpace($standardOutput)) {
        Write-Verbose $standardOutput
    }
}

function New-RandomSecret {
    $bytes = New-Object byte[] 36
    $generator = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $generator.GetBytes($bytes)
    }
    finally {
        $generator.Dispose()
    }
    return [Convert]::ToBase64String($bytes).Replace('+', '-').Replace('/', '_').TrimEnd('=')
}

function Save-LocalSecrets {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ApiPassword,
        [Parameter(Mandatory = $true)]
        [string]$AdminPassword
    )

    $connectionString = "Server=$bindAddress;Port=$port;User ID=projectproject01_api;Password=$ApiPassword;SslMode=Disabled;Connection Timeout=5;Default Command Timeout=5"
    $localConfiguration = [ordered]@{
        ConnectionStrings = [ordered]@{
            ProjectProject01 = $connectionString
        }
        LocalMySql = [ordered]@{
            AdminUser = 'projectproject01_admin'
            AdminPassword = $AdminPassword
        }
    }
    New-Item -ItemType Directory -Path $instanceDirectory -Force | Out-Null
    $localConfigurationJson = $localConfiguration | ConvertTo-Json -Depth 4
    [System.IO.File]::WriteAllText(
        $localConfigurationFile,
        $localConfigurationJson,
        [System.Text.UTF8Encoding]::new($false))

    $secretObject = [ordered]@{
        'ConnectionStrings:ProjectProject01' = $connectionString
        'LocalMySql:AdminUser' = 'projectproject01_admin'
        'LocalMySql:AdminPassword' = $AdminPassword
    }
    $json = $secretObject | ConvertTo-Json -Compress

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = 'dotnet'
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    Set-ProcessArguments -StartInfo $startInfo -Arguments @('user-secrets', 'set', '--project', $projectFile)
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw 'Failed to start dotnet user-secrets.'
    }
    $process.StandardInput.Write($json)
    $process.StandardInput.Close()
    $standardError = $process.StandardError.ReadToEnd()
    $process.StandardOutput.ReadToEnd() | Out-Null
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "dotnet user-secrets failed: $standardError"
    }
}

function Read-CurrentUserSecrets {
    $rawOutput = & dotnet user-secrets list --json --project $projectFile
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to read ProjectProject01 local MySQL credentials from .NET User Secrets.'
    }
    $jsonLines = $rawOutput | Where-Object { $_ -notmatch '^//(BEGIN|END)$' }
    return ($jsonLines -join [Environment]::NewLine) | ConvertFrom-Json
}

function Sync-LocalConfiguration {
    $secrets = Read-CurrentUserSecrets
    $connectionString = $secrets.'ConnectionStrings:ProjectProject01'
    $adminPassword = $secrets.'LocalMySql:AdminPassword'
    if ([string]::IsNullOrWhiteSpace($connectionString) -or [string]::IsNullOrWhiteSpace($adminPassword)) {
        throw 'The current user secrets do not contain the ProjectProject01 MySQL credentials.'
    }

    $localConfiguration = [ordered]@{
        ConnectionStrings = [ordered]@{
            ProjectProject01 = $connectionString
        }
        LocalMySql = [ordered]@{
            AdminUser = 'projectproject01_admin'
            AdminPassword = $adminPassword
        }
    }
    $localConfigurationJson = $localConfiguration | ConvertTo-Json -Depth 4
    [System.IO.File]::WriteAllText(
        $localConfigurationFile,
        $localConfigurationJson,
        [System.Text.UTF8Encoding]::new($false))
    Write-Host "ProjectProject01 local backend configuration was synchronized without printing credentials."
}

function Read-AdminPassword {
    if (Test-Path -LiteralPath $localConfigurationFile) {
        $localConfiguration = Get-Content -LiteralPath $localConfigurationFile -Raw | ConvertFrom-Json
        $localPassword = $localConfiguration.LocalMySql.AdminPassword
        if (-not [string]::IsNullOrWhiteSpace($localPassword)) {
            return $localPassword
        }
    }

    $secrets = Read-CurrentUserSecrets
    $password = $secrets.'LocalMySql:AdminPassword'
    if ([string]::IsNullOrWhiteSpace($password)) {
        throw 'LocalMySql:AdminPassword is missing from .NET User Secrets.'
    }
    return $password
}

function Initialize-ProjectProject01Server {
    if (Test-Path -LiteralPath $provisionedMarker) {
        throw "The isolated instance is already provisioned at $dataDirectory. Initialization will not overwrite it."
    }
    if (Test-ProjectProject01Port) {
        throw "Port $port is already in use. No existing process was changed."
    }

    if (-not (Test-Path -LiteralPath (Join-Path $dataDirectory 'mysql'))) {
        New-Item -ItemType Directory -Path $dataDirectory -Force | Out-Null
        & $mysqldPath --no-defaults "--basedir=$mysqlBaseDirectory" "--datadir=$dataDirectory" --initialize-insecure --console
        if ($LASTEXITCODE -ne 0) {
            throw "MySQL isolated instance initialization failed with exit code $LASTEXITCODE."
        }
    }
    else {
        Write-Host 'Resuming an incomplete isolated-instance provisioning without replacing its data directory.'
    }

    Start-ProjectProject01Server
    $apiPassword = New-RandomSecret
    $adminPassword = New-RandomSecret
    $provisioningSql = @"
CREATE DATABASE IF NOT EXISTS projectproject01 CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
CREATE USER 'projectproject01_api'@'127.0.0.1' IDENTIFIED BY '$apiPassword';
GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES ON projectproject01.* TO 'projectproject01_api'@'127.0.0.1';
CREATE USER 'projectproject01_admin'@'127.0.0.1' IDENTIFIED BY '$adminPassword';
GRANT ALL PRIVILEGES ON projectproject01.* TO 'projectproject01_admin'@'127.0.0.1';
GRANT SHUTDOWN, PROCESS ON *.* TO 'projectproject01_admin'@'127.0.0.1';
ALTER USER 'root'@'localhost' IDENTIFIED BY '$adminPassword';
FLUSH PRIVILEGES;
"@
    Invoke-MySqlInput -Executable $mysqlPath -Arguments @(
        '--protocol=TCP', "--host=$bindAddress", "--port=$port", '--user=root', '--skip-password'
    ) -StandardInput $provisioningSql
    Save-LocalSecrets -ApiPassword $apiPassword -AdminPassword $adminPassword
    [System.IO.File]::WriteAllText($provisionedMarker, "ProvisionedUtc=$([DateTime]::UtcNow.ToString('O'))")
    Write-Host 'Isolated ProjectProject01 MySQL initialized. Credentials were stored in .NET User Secrets.'
}

function Stop-ProjectProject01Server {
    if (-not (Test-ProjectProject01Port)) {
        Write-Host 'ProjectProject01 MySQL is not running.'
        return
    }
    $adminPassword = Read-AdminPassword
    Invoke-MySqlInput -Executable $mysqlAdminPath -Arguments @(
        '--protocol=TCP', "--host=$bindAddress", "--port=$port", '--user=projectproject01_admin', 'shutdown'
    ) -Password $adminPassword
    Write-Host 'ProjectProject01 MySQL stopped cleanly.'
}

Assert-Requirements
switch ($Action) {
    'Initialize' { Initialize-ProjectProject01Server }
    'Start' { Start-ProjectProject01Server }
    'Stop' { Stop-ProjectProject01Server }
    'SyncLocalConfiguration' { Sync-LocalConfiguration }
    'Status' {
        if (Test-ProjectProject01Port) {
            Write-Host "ProjectProject01 MySQL is running on ${bindAddress}:$port."
        }
        else {
            Write-Host 'ProjectProject01 MySQL is stopped.'
        }
    }
}

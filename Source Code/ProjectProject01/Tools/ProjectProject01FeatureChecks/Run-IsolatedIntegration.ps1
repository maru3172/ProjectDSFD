#Requires -Version 7.0
param([string]$ChecksAssembly='')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$mysqlBase='C:\Program Files\MySQL\MySQL Server 8.0'
$mysqlExe=Join-Path $mysqlBase 'bin\mysqld.exe'
$data=Join-Path $projectRoot 'Saved\FeatureChecks\IsolatedMySql'
$probe=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,23307)
try{$probe.Start()}finally{$probe.Stop()}
New-Item -ItemType Directory -Path $data -Force | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $data 'mysql'))) {
    & $mysqlExe --no-defaults "--basedir=$mysqlBase" "--datadir=$data" --initialize-insecure --console
    if($LASTEXITCODE -ne 0){throw 'Isolated MySQL initialization failed.'}
}
$start=[Diagnostics.ProcessStartInfo]::new($mysqlExe)
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
foreach($argument in @('--no-defaults',"--basedir=$mysqlBase","--datadir=$data",'--bind-address=127.0.0.1','--port=23307','--mysqlx=OFF','--innodb-buffer-pool-size=32M','--skip-log-bin')){$start.ArgumentList.Add($argument)}
$mysqlProcess=[Diagnostics.Process]::Start($start)
try {
    $ready=$false
    for($attempt=0;$attempt -lt 40;$attempt++) {
        if($mysqlProcess.HasExited){throw 'The isolated MySQL process exited.'}
        $socket=[Net.Sockets.TcpClient]::new()
        try{$socket.Connect('127.0.0.1',23307);$ready=$true;break}catch{Start-Sleep -Milliseconds 250}finally{$socket.Dispose()}
    }
    if(-not $ready){throw 'Isolated MySQL did not become ready.'}
    if([string]::IsNullOrWhiteSpace($ChecksAssembly)){$ChecksAssembly=Join-Path $PSScriptRoot 'bin\Debug\net9.0-windows\ProjectProject01FeatureChecks.dll'}
    & dotnet $ChecksAssembly --integration $projectRoot 23307
    if($LASTEXITCODE -ne 0){throw 'Integration checks failed.'}
    & (Join-Path $PSScriptRoot 'RecoveryChecks.ps1') -ProjectRoot $projectRoot
}
finally {
    if(-not $mysqlProcess.HasExited){
        & (Join-Path $mysqlBase 'bin\mysqladmin.exe') '--no-defaults' '--host=127.0.0.1' '--port=23307' '--user=root' shutdown
        if(-not $mysqlProcess.WaitForExit(10000)){$mysqlProcess.Kill()}
    }
    $mysqlProcess.Dispose()
}

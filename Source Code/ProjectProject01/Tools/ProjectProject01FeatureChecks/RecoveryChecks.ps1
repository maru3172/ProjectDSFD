# Invoked only by Run-IsolatedIntegration while its own MySQL instance is running.
param([Parameter(Mandatory)][string]$ProjectRoot)
$ErrorActionPreference='Stop'
$port=23307
$bindAddress='127.0.0.1'
$mysqlBin='C:\Program Files\MySQL\MySQL Server 8.0\bin'
$mysqlPath=Join-Path $mysqlBin 'mysql.exe'
$mysqlDumpPath=Join-Path $mysqlBin 'mysqldump.exe'
$expected=[IO.Path]::GetFullPath((Join-Path $ProjectRoot 'Saved\FeatureChecks\IsolatedMySql')).TrimEnd('\')
$actual=(& $mysqlPath --no-defaults --host=127.0.0.1 --port=23307 --user=root --skip-password -N -B -e 'SELECT @@datadir;').Trim().TrimEnd('\')
if($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($actual) -ne $expected){throw 'Recovery test must only use its isolated data directory.'}
$instanceDirectory=Join-Path $ProjectRoot ('Saved\FeatureChecks\RecoveryFixture-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $instanceDirectory | Out-Null
# Load function definitions only: never execute the production script's initialization or action switch.
$source=Join-Path $ProjectRoot 'Tools\ProjectProject01Backend\ProjectProject01MySql.ps1'
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$null,[ref]$null)
$required=@('Set-ProcessArguments','Set-ProcessEnvironmentValue','Invoke-MySqlInput','Test-ProjectProject01Port','Backup-ProjectProject01Database','Test-ProjectBackupFile','Import-ProjectDatabase','Restore-ProjectDatabase')
foreach($function in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)){
    if($required -contains $function.Name){. ([scriptblock]::Create($function.Extent.Text))}
}
function Read-AdminPassword { return 'isolated-test-only' }
# The test backend has already shut down. Production's port 5080 is deliberately unrelated to this test instance.
function Assert-RecoveryServicesStopped {}
Invoke-MySqlInput -Executable $mysqlPath -Arguments @('--no-defaults','--host=127.0.0.1','--port=23307','--user=root','--skip-password') -StandardInput "CREATE USER IF NOT EXISTS 'projectproject01_admin'@'127.0.0.1' IDENTIFIED BY 'isolated-test-only'; GRANT ALL PRIVILEGES ON projectproject01.* TO 'projectproject01_admin'@'127.0.0.1';"
Invoke-MySqlInput -Executable $mysqlPath -Arguments @('--no-defaults','--host=127.0.0.1','--port=23307','--user=root','--skip-password','--database=projectproject01','--default-character-set=utf8mb4') -StandardInput "DROP TABLE IF EXISTS recovery_fixture; CREATE TABLE recovery_fixture(id INT PRIMARY KEY,message VARCHAR(100)); INSERT INTO recovery_fixture VALUES(1,'체크포인트 설문 복원 검증');"
$BackupFile=Backup-ProjectProject01Database
$ConfirmRestore=$true
Invoke-MySqlInput -Executable $mysqlPath -Arguments @('--no-defaults','--host=127.0.0.1','--port=23307','--user=root','--skip-password','--database=projectproject01') -StandardInput "UPDATE recovery_fixture SET message='changed'; CREATE TABLE recovery_added_later(id INT);"
Restore-ProjectDatabase
$value=& $mysqlPath --no-defaults --host=127.0.0.1 --port=23307 --user=root --skip-password --database=projectproject01 --default-character-set=utf8mb4 -N -B -e 'SELECT message FROM recovery_fixture WHERE id=1; SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=''projectproject01'' AND table_name=''recovery_added_later'';'
if($LASTEXITCODE -ne 0 -or $value[0] -ne '체크포인트 설문 복원 검증' -or $value[1] -ne '0'){throw 'Recovery data/schema mismatch.'}
Write-Host 'PASS actual SQL backup/restore preserves Korean and removes tables newer than the backup'
$original=[IO.File]::ReadAllText($BackupFile)
[IO.File]::AppendAllText($BackupFile,'tampered')
$rejected=$false
try{$null=Test-ProjectBackupFile -File $BackupFile}catch{$rejected=$true}
if(-not $rejected){throw 'Tampered backup was accepted.'}
[IO.File]::WriteAllText($BackupFile,$original,[Text.UTF8Encoding]::new($false))
Write-Host 'PASS changed backup rejected by SHA-256'
$invalid=Join-Path $instanceDirectory 'Backups\invalid.sql'
[IO.File]::WriteAllText($invalid,'THIS IS NOT SQL;',[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($invalid+'.manifest.json',(@{database='projectproject01';createdAtUtc=[DateTime]::UtcNow.ToString('O');sha256=(Get-FileHash -LiteralPath $invalid -Algorithm SHA256).Hash}|ConvertTo-Json))
$BackupFile=$invalid
$rolledBack=$false
try{Restore-ProjectDatabase}catch{$rolledBack=$_.Exception.Message -like '*pre-restore backup was recovered*'}
if(-not $rolledBack){throw 'Failed restore did not roll back.'}
$value=& $mysqlPath --no-defaults --host=127.0.0.1 --port=23307 --user=root --skip-password --database=projectproject01 --default-character-set=utf8mb4 -N -B -e 'SELECT message FROM recovery_fixture WHERE id=1;'
if($value -ne '체크포인트 설문 복원 검증'){throw 'Rollback changed the previous data.'}
Write-Host 'PASS failed restore recovers the automatic pre-restore backup'
$ConfirmRestore=$false
$rejected=$false
try{Restore-ProjectDatabase}catch{$rejected=$_.Exception.Message -like '*requires -ConfirmRestore*'}
if(-not $rejected){throw 'Explicit restore confirmation was not enforced.'}
Write-Host 'PASS restore confirmation enforced; production DB untouched'

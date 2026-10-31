$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$destination=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Saved/GitManager'))
$executable=Join-Path $destination 'ProjectDFSDGitManager.exe'
$inputs=Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object {$_.Extension -in '.cs','.csproj' -or $_.Name -eq 'Review-Merge.ps1'}
$needsBuild=-not(Test-Path -LiteralPath $executable)
if(-not $needsBuild){$needsBuild=@($inputs|Where-Object LastWriteTimeUtc -gt (Get-Item -LiteralPath $executable).LastWriteTimeUtc).Count -gt 0}
if($needsBuild){
    & dotnet publish (Join-Path $PSScriptRoot 'ProjectProject01GitMerge.csproj') -c Release -o $destination --nologo
    if($LASTEXITCODE -ne 0){throw 'Git manager build failed.'}
}
Start-Process -FilePath $executable -ArgumentList @('--repo',('"'+$repository+'"')) -WindowStyle Normal

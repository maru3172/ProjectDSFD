$ErrorActionPreference='Stop'
# Any repository: the one this script sits in. The build goes to <project>/Saved when the tool is in <project>/Tools,
# otherwise to the user's local app data, so the folder can be copied into another project as it is.
$repository=(& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
if($LASTEXITCODE -ne 0 -or -not $repository){throw 'This folder is not inside a git repository.'}
$repository=[IO.Path]::GetFullPath($repository)
$saved=Join-Path $PSScriptRoot '../../Saved'
$destination=if(Test-Path -LiteralPath $saved){[IO.Path]::GetFullPath((Join-Path $saved 'GitManager'))}else{Join-Path $env:LOCALAPPDATA 'GitManager'}
$executable=Join-Path $destination 'ProjectDFSDGitManager.exe'
$inputs=Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object {$_.Extension -in '.cs','.csproj' -or $_.Name -eq 'Review-Merge.ps1'}
$needsBuild=-not(Test-Path -LiteralPath $executable)
if(-not $needsBuild){$needsBuild=@($inputs|Where-Object LastWriteTimeUtc -gt (Get-Item -LiteralPath $executable).LastWriteTimeUtc).Count -gt 0}
if($needsBuild){
    & dotnet publish (Join-Path $PSScriptRoot 'ProjectProject01GitMerge.csproj') -c Release -o $destination --nologo
    if($LASTEXITCODE -ne 0){throw 'Git manager build failed.'}
}
Start-Process -FilePath $executable -ArgumentList @('--repo',('"'+$repository+'"')) -WindowStyle Normal

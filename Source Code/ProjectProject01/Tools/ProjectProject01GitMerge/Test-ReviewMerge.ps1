#Requires -Version 7.0
[CmdletBinding()]
param([string]$OutputRoot='')
$ErrorActionPreference='Stop'
$tool=Join-Path $PSScriptRoot 'Review-Merge.ps1'
if(-not $OutputRoot){$OutputRoot=Join-Path $PSScriptRoot '../../Saved/GitMergeTests'}
$root=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ([Guid]::NewGuid().ToString('N'))
$fixture=Join-Path $root 'fixture';[void][IO.Directory]::CreateDirectory($fixture)
$checks=[Collections.Generic.List[string]]::new()
function Check([bool]$Condition,[string]$Name){if(-not $Condition){throw "검사 실패: $Name"};$checks.Add($Name)}
function Git([string]$At,[string[]]$Arguments,[int[]]$Allowed=@(0),[hashtable]$Environment=@{}){
    $info=[Diagnostics.ProcessStartInfo]::new('git');$info.WorkingDirectory=$At
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $info.StandardOutputEncoding=[Text.UTF8Encoding]::new($false);$info.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
    foreach($arg in $Arguments){$info.ArgumentList.Add($arg)}
    foreach($key in $Environment.Keys){$info.Environment[$key]=[string]$Environment[$key]}
    $proc=[Diagnostics.Process]::Start($info)
    try{
        $o=$proc.StandardOutput.ReadToEndAsync();$e=$proc.StandardError.ReadToEndAsync()
        if(-not $proc.WaitForExit(60000)){$proc.Kill($true);throw '검사용 Git 시간 초과'}
        $text=$o.GetAwaiter().GetResult();$errorText=$e.GetAwaiter().GetResult()
        if($Allowed -notcontains $proc.ExitCode){throw "검사용 Git 실패: $errorText"}
        return $text.TrimEnd("`r","`n")
    }finally{$proc.Dispose()}
}
function Put([string]$Path,[string]$Text){
    $full=Join-Path $fixture $Path;[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($full))
    [IO.File]::WriteAllText($full,$Text,[Text.UTF8Encoding]::new($false))
}
function Commit([string]$Message,[hashtable]$Environment=@{}){
    [void](Git $fixture @('add','--all'))
    [void](Git $fixture @('commit','-m',$Message) @(0) $Environment)
}
try{
    [void](Git $fixture @('init','-b','main'))
    foreach($setting in @(@('user.name','Merge fixture'),@('user.email','fixture@example.invalid'),@('commit.gpgsign','false'),@('core.autocrlf','false'))){[void](Git $fixture (@('config','--local')+$setting))}
    [void](Git $fixture @('lfs','install','--local','--skip-smudge'))
    Put '.gitattributes' "*.uasset filter=lfs diff=lfs merge=lfs -text`n*.umap filter=lfs diff=lfs merge=lfs -text`n"
    foreach($name in @('older.umap','newer.uasset','diverged.uasset','deleted.uasset','removed.uasset','특수 [1].uasset')){Put "Content/$name" "base-$name"}
    Put 'conflict.txt' "base`n";Commit 'base'
    [void](Git $fixture @('branch','feature'))
    Put 'Content/older.umap' 'main-latest'
    Put 'Content/diverged.uasset' 'main-diverged'
    Put 'Content/main-only.umap' 'main-only'
    [void](Git $fixture @('rm','--','Content/removed.uasset'))
    Put 'conflict.txt' "main-code`n"
    Commit 'main assets' @{GIT_AUTHOR_DATE='2020-01-01T00:00:00+0000';GIT_COMMITTER_DATE='2020-01-01T00:00:00+0000'}
    $mainTip=Git $fixture @('rev-parse','main')
    [void](Git $fixture @('switch','feature'))
    Put 'Content/newer.uasset' 'feature-newer'
    Put 'Content/diverged.uasset' 'feature-diverged'
    Put 'Content/removed.uasset' 'feature-modified-deleted-main'
    Put 'Content/new asset.umap' 'new-file'
    Put 'Content/특수 [1].uasset' 'literal-special-path'
    [void](Git $fixture @('rm','--','Content/deleted.uasset'))
    Put 'feature.txt' 'new-feature';Put 'conflict.txt' "feature-code`n"
    Commit 'feature changes' @{GIT_AUTHOR_DATE='2035-01-01T00:00:00+0000';GIT_COMMITTER_DATE='2035-01-01T00:00:00+0000'}
    # Uncommitted files in the active work folder must never enter the preview.
    Put 'dirty.txt' 'keep-working-copy'
    $head=Git $fixture @('rev-parse','HEAD');$before=Git $fixture @('status','--porcelain=v1')
    $planResult=& $tool -SourceRef feature -RepositoryRoot $fixture -ReportRoot (Join-Path $root 'reports')
    $plan=Get-Content -LiteralPath $planResult.PlanFile -Raw|ConvertFrom-Json
    function Decision([string]$Path){return $plan.assets|Where-Object path -CEQ $Path}
    Check ((Decision 'Content/older.umap').kind -eq 'OlderRevision') '미래 날짜의 원본 커밋도 오래된 에셋을 덮어쓰지 않음'
    Check ((Decision 'Content/newer.uasset').choice -eq 'Source') 'main 에셋 이력을 포함한 새 수정 반영'
    Check ((Decision 'Content/new asset.umap').kind -eq 'NewAsset') '새 에셋 반영'
    Check ((Decision 'Content/diverged.uasset').reviewRequired) '갈라진 에셋 변경은 검토 표시'
    Check ((Decision 'Content/deleted.uasset').choice -eq 'Target') '원본의 삭제는 main 보존'
    Check ((Decision 'Content/removed.uasset').choice -eq 'Target') 'main에서 삭제한 구버전은 자동 복구하지 않음'
    Check ((Decision 'Content/main-only.umap').kind -eq 'TargetOnly') 'main 전용 에셋 유지'
    Check ((Git $fixture @('status','--porcelain=v1')) -ceq $before) '계획 검사가 작업 파일 상태 보존'
    $prepared=& $tool -Action Prepare -PlanFile $planResult.PlanFile -RepositoryRoot $fixture -ReportRoot (Join-Path $root 'reports') -ReviewRoot (Join-Path $root 'previews')
    $preview=$prepared.PreviewPath
    Check ((Git $preview @('show',':feature.txt')) -eq 'new-feature') '새 코드 정상 포함'
    Check ($prepared.CodeConflicts -contains 'conflict.txt') '코드 충돌은 덮어쓰지 않고 남김'
    foreach($path in @('Content/older.umap','Content/diverged.uasset','Content/deleted.uasset','Content/main-only.umap')){
        Check ((Git $preview @('rev-parse',":$path")) -eq (Git $fixture @('rev-parse',"main:$path"))) "main 선택 일치: $path"
    }
    foreach($path in @('Content/newer.uasset','Content/new asset.umap','Content/특수 [1].uasset')){
        Check ((Git $preview @('rev-parse',":$path")) -eq (Git $fixture @('rev-parse',"feature:$path"))) "원본 선택 일치: $path"
    }
    Check (-not(Test-Path -LiteralPath (Join-Path $preview 'Content/removed.uasset'))) '삭제한 main 에셋 부활 방지'
    Check ((Get-Content -LiteralPath (Join-Path $preview 'Content/older.umap') -Raw).StartsWith('version https://git-lfs.github.com/spec/v1')) 'LFS 다운로드 없이 포인터로 검토 준비'
    Check (-not(Test-Path -LiteralPath (Join-Path $preview 'dirty.txt'))) '원래 작업 폴더의 미커밋 변경은 포함하지 않음'
    Check ((Git $fixture @('rev-parse','main')) -eq $mainTip) 'main 커밋 불변'
    Check ((Git $fixture @('rev-parse','HEAD')) -eq $head) '현재 브랜치 불변'
    Check ((Git $fixture @('status','--porcelain=v1')) -ceq $before) '검토 준비 후 원래 작업 상태 불변'
    $explicit=& $tool -SourceRef feature -RepositoryRoot $fixture -SourceAsset 'Content/diverged.uasset' -ReportRoot (Join-Path $root 'reports')
    $explicitPreview=& $tool -Action Prepare -PlanFile $explicit.PlanFile -RepositoryRoot $fixture -ReportRoot (Join-Path $root 'reports') -ReviewRoot (Join-Path $root 'previews')
    Check ((Git $explicitPreview.PreviewPath @('rev-parse',':Content/diverged.uasset')) -eq (Git $fixture @('rev-parse','feature:Content/diverged.uasset'))) '명시적 에셋 선택이 계획 재사용 시에도 유지'
    foreach($bad in @('../outside.umap','Content/no-such.umap')){
        $rejected=$false;try{& $tool -SourceRef feature -RepositoryRoot $fixture -SourceAsset $bad -ReportRoot (Join-Path $root 'reports')|Out-Null}catch{$rejected=$true}
        Check $rejected "잘못된 에셋 선택 거절: $bad"
    }
    Put 'later.txt' 'later';Commit 'advance feature'
    $rejected=$false;try{& $tool -Action Prepare -PlanFile $planResult.PlanFile -RepositoryRoot $fixture|Out-Null}catch{$rejected=$_.Exception.Message -like '*브랜치가 변경*'}
    Check $rejected '작성 후 브랜치가 바뀐 계획 거절'
    $rejected=$false;try{& $tool -Action Prepare -SourceRef main -RepositoryRoot $fixture|Out-Null}catch{$rejected=$_.Exception.Message -like '*이미 대상 브랜치*'}
    Check $rejected '이미 포함된 커밋의 불필요한 병합 거절'
    [pscustomobject]@{Success=$true;Checks=$checks.Count;Names=$checks.ToArray();Fixture=$root}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $root 'results.json') -Encoding utf8
    [pscustomobject]@{Success=$true;Checks=$checks.Count;Report=(Join-Path $root 'results.json')}
}catch{
    [pscustomobject]@{Success=$false;Checks=$checks.Count;Failure=$_.Exception.Message;Fixture=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'results.json') -Encoding utf8
    throw
}

#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Plan','Prepare')][string]$Action='Plan',
    [string]$SourceRef='',
    [string]$TargetBranch='main',
    [string]$RepositoryRoot='',
    [string]$PlanFile='',
    [string[]]$SourceAsset=@(),
    [string[]]$TargetAsset=@(),
    [string]$SelectionFile='',
    [switch]$JsonOutput,
    [string]$ReportRoot='',
    [string]$ReviewRoot=''
)
$ErrorActionPreference='Stop'

function Invoke-LocalGit {
    param([string]$At,[string[]]$GitArguments,[int[]]$Allowed=@(0),[hashtable]$Environment=@{})
    $start=[Diagnostics.ProcessStartInfo]::new('git')
    $start.WorkingDirectory=$At;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.StandardOutputEncoding=[Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
    foreach($argument in $GitArguments){$start.ArgumentList.Add($argument)}
    $start.Environment['GIT_TERMINAL_PROMPT']='0'
    foreach($key in $Environment.Keys){$start.Environment[$key]=[string]$Environment[$key]}
    $process=[Diagnostics.Process]::Start($start)
    try {
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit(60000)){$process.Kill($true);throw 'Git 명령이 60초 안에 끝나지 않았습니다. 생성된 검토 폴더는 보존합니다.'}
        $result=[pscustomobject]@{Code=$process.ExitCode;Text=$stdout.GetAwaiter().GetResult();Error=$stderr.GetAwaiter().GetResult()}
        if($Allowed -notcontains $result.Code){throw "Git 오류 ($($result.Code)): $($result.Error.Trim())"}
        return $result
    } finally {$process.Dispose()}
}
function Git-Text([string]$At,[string[]]$GitArguments){return (Invoke-LocalGit $At $GitArguments).Text.TrimEnd("`r","`n")}
function Resolve-Commit([string]$Ref){return Git-Text $script:Repo @('rev-parse','--verify','--end-of-options',"$Ref`^{commit}")}
function Is-Ancestor([string]$Older,[string]$Newer){return (Invoke-LocalGit $script:Repo @('merge-base','--is-ancestor',$Older,$Newer) @(0,1)).Code -eq 0}
function Read-Assets([string]$Commit){
    $assets=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    $tree=(Invoke-LocalGit $script:Repo @('ls-tree','-r','-z','--full-tree',$Commit)).Text
    foreach($line in $tree.Split([char]0,[StringSplitOptions]::RemoveEmptyEntries)){
        if($line -notmatch '(?s)^([0-9]+) blob ([0-9a-f]+)\t(.+)$'){continue}
        $mode=$Matches[1];$blob=$Matches[2];$path=$Matches[3]
        if($path -match '(?i)\.(umap|uasset)$'){$assets.Add($path,[pscustomobject]@{Blob=$blob;Mode=$mode})}
    }
    return ,$assets
}
function Last-AssetCommit([string]$Commit,[string]$Path){return Git-Text $script:Repo @('log','-1','--format=%H',$Commit,'--',":(literal)$Path")}
function Validate-AssetPath([string]$Path){
    if([IO.Path]::IsPathRooted($Path)-or $Path.Contains('\')-or $Path.Split('/') -contains '..'-or $Path -notmatch '(?i)\.(umap|uasset)$'){throw "잘못된 에셋 경로: $Path"}
}

if(-not $RepositoryRoot){$RepositoryRoot=Git-Text $PSScriptRoot @('rev-parse','--show-toplevel')}
$script:Repo=[IO.Path]::GetFullPath((Git-Text ([IO.Path]::GetFullPath($RepositoryRoot)) @('rev-parse','--show-toplevel')))
if($PlanFile){
    $previous=Get-Content -LiteralPath $PlanFile -Raw -Encoding utf8 | ConvertFrom-Json
    if($previous.version -ne 1 -or [IO.Path]::GetFullPath($previous.repository) -ne $script:Repo){throw '이 저장소의 병합 계획 파일이 아닙니다.'}
    $SourceRef=[string]$previous.sourceRef;$TargetBranch=[string]$previous.targetBranch
}
if(-not $SourceRef){throw '-SourceRef에 가져올 브랜치를 지정하세요. 예: -SourceRef Develop/Multiplay'}
# These commands read committed objects only: an open editor and dirty working files are not changed.
$source=Resolve-Commit $SourceRef
$target=Git-Text $script:Repo @('show-ref','--verify','--hash',"refs/heads/$TargetBranch")
if($PlanFile -and ($previous.sourceCommit -ne $source -or $previous.targetCommit -ne $target)){throw '계획 작성 후 브랜치가 변경되었습니다. Plan을 다시 실행하세요.'}
if($Action -eq 'Prepare' -and (Is-Ancestor $source $target)){throw '가져올 커밋이 이미 대상 브랜치에 포함되어 있습니다. 병합할 새 커밋이 없습니다.'}
$bases=@((Git-Text $script:Repo @('merge-base','--all',$target,$source)).Split("`n",[StringSplitOptions]::RemoveEmptyEntries))
if($bases.Count -ne 1){throw '공통 조상이 없거나 여러 개입니다. 자동 에셋 선택 대신 별도 병합 검토가 필요합니다.'}
$base=$bases[0].Trim()
$targetAssets=Read-Assets $target;$sourceAssets=Read-Assets $source;$baseAssets=Read-Assets $base
$paths=[Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
foreach($path in $targetAssets.Keys){[void]$paths.Add($path)}
foreach($path in $sourceAssets.Keys){[void]$paths.Add($path)}
$decisions=[Collections.Generic.List[object]]::new()
foreach($path in $paths){
    $ours=$targetAssets[$path];$theirs=$sourceAssets[$path];$original=$baseAssets[$path]
    if($ours -and $theirs -and $ours.Blob -eq $theirs.Blob -and $ours.Mode -eq $theirs.Mode){continue}
    $choice='Target';$review=$false;$kind='KeepTarget';$detail="대상 브랜치($TargetBranch)의 에셋을 유지합니다.";$oursCommit='';$theirsCommit=''
    if(($ours -and $ours.Mode -eq '120000') -or ($theirs -and $theirs.Mode -eq '120000')){
        $kind='Review';$review=$true;$detail='심볼릭 링크 에셋은 자동 반영하지 않습니다.'
    } elseif(-not $ours -and $theirs -and -not $original){
        $choice='Source';$kind='NewAsset';$detail='대상 브랜치와 공통 조상에 없던 새 에셋입니다.'
    } elseif(-not $theirs){
        if($original){$kind='Review';$review=$true;$detail='가져올 브랜치가 삭제한 에셋입니다. 대상 브랜치를 우선 보존합니다.'}
        else {$kind='TargetOnly';$detail='대상 브랜치에서 추가한 에셋이므로 유지합니다.'}
    } elseif(-not $ours){
        $kind='Review';$review=$true;$detail='대상 브랜치에서 삭제한 에셋입니다. 구버전 파일을 자동 복구하지 않습니다.'
    } else {
        $oursCommit=Last-AssetCommit $target $path;$theirsCommit=Last-AssetCommit $source $path
        if($oursCommit -and $theirsCommit -and $oursCommit -ne $theirsCommit -and (Is-Ancestor $oursCommit $theirsCommit)){
            $choice='Source';$kind='NewRevision';$detail='대상 브랜치의 에셋 변경 이력을 포함한 뒤 수정한 새 버전입니다.'
        } elseif($oursCommit -and $theirsCommit -and (Is-Ancestor $theirsCommit $oursCommit)){
            $kind='OlderRevision';$detail='대상 브랜치보다 이전 에셋 버전이므로 대상 브랜치를 유지합니다.'
        } else {
            $kind='Review';$review=$true;$detail='양쪽 변경 이력이 갈라졌습니다. 날짜로 신구를 단정하지 않습니다.'
        }
    }
    $decisions.Add([pscustomobject]@{path=$path;kind=$kind;choice=$choice;reviewRequired=$review;explicitSelection=$false;reason=$detail;targetBlob=if($ours){$ours.Blob}else{$null};sourceBlob=if($theirs){$theirs.Blob}else{$null};targetAssetCommit=$oursCommit;sourceAssetCommit=$theirsCommit})
}
$selections=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
if($PlanFile){foreach($item in ($previous.assets|Where-Object explicitSelection)){$selections[$item.path]=$item.choice}}
foreach($selected in $SourceAsset){$selections[$selected]='Source'}
foreach($selected in $TargetAsset){$selections[$selected]='Target'}
if($SelectionFile){
    $entries=Get-Content -LiteralPath $SelectionFile -Raw -Encoding utf8|ConvertFrom-Json
    foreach($item in $entries){
        if($item.choice -notin @('Source','Target','Automatic')){throw '올바르지 않은 에셋 선택입니다.'}
        if($item.choice -eq 'Automatic'){[void]$selections.Remove([string]$item.path)}else{$selections[[string]$item.path]=[string]$item.choice}
    }
}
foreach($selected in $selections.Keys){
    Validate-AssetPath $selected
    $entry=$decisions.Find([Predicate[object]]{param($item)$item.path -ceq $selected})
    if(-not $entry){throw "에셋 비교 목록에 없는 경로입니다: $selected"}
    $chosen=if($selections[$selected] -eq 'Source'){$sourceAssets[$selected]}else{$targetAssets[$selected]}
    if($chosen -and $chosen.Mode -eq '120000'){throw '심볼릭 링크 에셋은 선택할 수 없습니다.'}
    $entry.choice=$selections[$selected];$entry.reviewRequired=$false;$entry.explicitSelection=$true;$entry.reason='사용자가 사용할 에셋 버전을 명시적으로 선택했습니다.'
}
if(-not $ReportRoot){$ReportRoot=Join-Path $PSScriptRoot '../../Saved/GitMergeReviews'}
$ReportRoot=[IO.Path]::GetFullPath($ReportRoot)
$id=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
[IO.Directory]::CreateDirectory($ReportRoot)|Out-Null
$reportPath=Join-Path $ReportRoot ("plan-$id.json")
$plan=[pscustomobject]@{version=1;repository=$script:Repo;sourceRef=$SourceRef;sourceCommit=$source;targetBranch=$TargetBranch;targetCommit=$target;baseCommit=$base;createdAtUtc=[DateTimeOffset]::UtcNow.ToString('O');assets=$decisions.ToArray();previewPath=$null;previewBranch=$null;unresolvedPaths=@()}
function Write-Plan {
    $plan|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $reportPath -Encoding utf8
    $lines=[Collections.Generic.List[string]]::new()
    $lines.Add('# 에셋 보호 병합 검토');$lines.Add('');$lines.Add("- 대상: $TargetBranch ($target)");$lines.Add("- 가져올 브랜치: $SourceRef ($source)")
    $lines.Add('- 파일 날짜가 아닌 커밋 이력으로 판별합니다. 새 커밋이라도 내용의 의미까지 최신임을 증명하지는 않습니다.')
    $lines.Add('');$lines.Add('| 에셋 | 판정 | 선택 | 검토 필요 |');$lines.Add('| --- | --- | --- | --- |')
    foreach($item in $decisions){$lines.Add('| '+$item.path.Replace('|','\|')+' | '+$item.kind+' | '+$item.choice+' | '+$item.reviewRequired+' |')}
    if($plan.previewPath){$lines.Add('');$lines.Add("검토 폴더: $($plan.previewPath)");$lines.Add("검토 브랜치: $($plan.previewBranch)");$lines.Add('대상 브랜치와 원래 작업 폴더는 변경하지 않았습니다. 자동 커밋하지 않습니다. LFS 파일은 검토 속도를 위해 포인터 상태이며 에디터 실행 전 이 폴더에서 git lfs checkout이 필요합니다.')}
    $lines|Set-Content -LiteralPath ([IO.Path]::ChangeExtension($reportPath,'.md')) -Encoding utf8
}
Write-Plan
if($Action -eq 'Prepare'){
    # Recheck both tips immediately before creating a review, never merge into the operator's active folder.
    if((Resolve-Commit $SourceRef) -ne $source -or (Git-Text $script:Repo @('show-ref','--verify','--hash',"refs/heads/$TargetBranch")) -ne $target){throw '브랜치가 변경되어 준비를 중단했습니다. 계획을 다시 확인하세요.'}
    if(-not $ReviewRoot){$ReviewRoot=Join-Path ([IO.Directory]::GetParent($script:Repo).FullName) ((Split-Path $script:Repo -Leaf)+'-MergeReviews')}
    $ReviewRoot=[IO.Path]::GetFullPath($ReviewRoot)
    $preview=Join-Path $ReviewRoot $id
    if(Test-Path -LiteralPath $preview){throw '검토 폴더가 이미 존재합니다.'}
    $hooks=Join-Path $ReportRoot ("empty-hooks-$id");[IO.Directory]::CreateDirectory($hooks)|Out-Null
    $settings=@('-c',"core.hooksPath=$hooks",'-c','core.symlinks=false')
    $envOverrides=@{GIT_LFS_SKIP_SMUDGE='1'}
    $branch="merge-review/$id"
    [void](Invoke-LocalGit $script:Repo ($settings+@('worktree','add','-b',$branch,$preview,$target)) @(0) $envOverrides)
    $plan.previewPath=$preview;$plan.previewBranch=$branch;Write-Plan
    try {
        $merge=Invoke-LocalGit $preview ($settings+@('-c','merge.lfs.driver=false','merge','--no-commit','--no-ff',$source)) @(0,1) $envOverrides
        $mergeHead=(Invoke-LocalGit $preview @('rev-parse','--verify','MERGE_HEAD') @(0,128)).Code -eq 0
        if($merge.Code -ne 0 -and -not $mergeHead){throw "병합 준비 실패: $($merge.Error.Trim())"}
        foreach($item in $decisions){
            Validate-AssetPath $item.path
            $from=if($item.choice -eq 'Source'){$source}else{$target}
            $exists=if($item.choice -eq 'Source'){$null -ne $item.sourceBlob}else{$null -ne $item.targetBlob}
            if($exists){[void](Invoke-LocalGit $preview ($settings+@('restore',"--source=$from",'--staged','--worktree','--',(':(literal)'+$item.path))) @(0) $envOverrides)}
            else {[void](Invoke-LocalGit $preview ($settings+@('rm','-f','--ignore-unmatch','--',(':(literal)'+$item.path))) @(0) $envOverrides)}
        }
        $unresolved=(Invoke-LocalGit $preview @('diff','--name-only','--diff-filter=U','-z')).Text
        $plan.unresolvedPaths=@($unresolved.Split([char]0,[StringSplitOptions]::RemoveEmptyEntries))
        if((Git-Text $script:Repo @('show-ref','--verify','--hash',"refs/heads/$TargetBranch")) -ne $target){throw '검토 중 대상 브랜치가 외부에서 바뀌었습니다. 이 검토 결과를 바로 병합하지 마세요.'}
        Write-Plan
    } catch {Write-Plan;throw "검토 폴더를 보존했습니다: $preview`n$($_.Exception.Message)"}
}
$result=[pscustomobject]@{Action=$Action;PlanFile=$reportPath;TargetCommit=$target;SourceCommit=$source;ApplySource=@($decisions|Where-Object choice -eq 'Source').Count;KeepTarget=@($decisions|Where-Object choice -eq 'Target').Count;ReviewRequired=@($decisions|Where-Object reviewRequired).Count;PreviewPath=$plan.previewPath;PreviewBranch=$plan.previewBranch;CodeConflicts=$plan.unresolvedPaths}
if($JsonOutput){$result|ConvertTo-Json -Depth 5 -Compress}else{$result}

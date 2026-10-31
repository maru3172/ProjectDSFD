# Git 관리 도구

`Start-GitManager.cmd`를 더블클릭하면 별도 창이 열립니다. 첫 실행이나 소스 변경 후에는 자동으로 빌드합니다. 실행 파일은 프로젝트의 `Saved/GitManager/ProjectDFSDGitManager.exe`입니다. Git·Git LFS·PowerShell 7·.NET 8 Desktop Runtime이 설치된 개발 PC용 도구입니다.

| 탭 | 기능 |
| --- | --- |
| 에셋 보호 병합 | 가져올 브랜치 → 대상 브랜치 선택, 에셋 판정, 버전별 선택, 독립 검토 폴더 생성 |
| 변경 · 커밋 | 파일별/전체 스테이지, 취소, 변경 비교, 작업 변경 버리기, 커밋, amend, 마지막 커밋 취소 |
| 커밋 이력 | 브랜치별 최신 100개, 커밋 상세, revert, cherry-pick, reset, 마지막 메시지 수정, 중간 커밋 삭제, 태그 |
| 브랜치 | 최신 해시·일시·내용, checkout, 생성·이름 변경·삭제, merge, rebase, 충돌 해결 후 계속·중단 |
| 스태시 | 임시 보관, 내용 확인, 적용, pop, 삭제 |
| 원격 · 태그 · LFS | fetch, pull, push, force-with-lease, 원격 설정, 원격 브랜치 삭제, 태그, LFS, 객체 검사 |
| 작업 폴더 · 복구 | worktree 생성·전환·제거, reflog, 미추적 파일 정리 |
| 추가 Git 명령 | 버튼으로 제공하지 않은 Git 명령을 직접 실행. 별도 셸 명령이 아닌 Git 인수로 전달 |

현재 폴더의 브랜치와 무관하게 두 브랜치를 비교할 수 있습니다. 병합 대상은 로컬 브랜치이며 원격 브랜치는 로컬 추적 브랜치를 만든 후 대상으로 선택합니다. 원격의 최신 상태는 fetch로 갱신합니다.

일반 merge/rebase/reset 등은 상단에 표시된 **현재 작업 폴더**에 적용됩니다. 에셋 보호 병합은 **독립 검토 폴더**에만 준비합니다. 검토 폴더를 관리 대상으로 열어 충돌을 해결하고 커밋한 뒤 대상 브랜치에 병합하세요. 대상 브랜치에 자동 커밋하거나 자동 push하지 않습니다.

커밋 삭제는 현재 브랜치의 일반 커밋과 이후 선형 이력에서 지원합니다. 최초·병합 커밋 삭제와 별도 편집기가 필요한 대화형 명령은 외부 Git 도구를 사용합니다. 원격 인증은 기존 Git 자격 증명 설정을 사용합니다.

## 명령줄에서 에셋 보호 병합

PowerShell 7에서 실행합니다. 실제 작업 폴더와 대상 브랜치는 변경하지 않으며, 커밋하지 않은 작업은 가져오지 않습니다. `-TargetBranch`로 어느 로컬 브랜치든 대상으로 정할 수 있고, 생략할 때만 `main`을 사용합니다.

```powershell
pwsh -File "C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01GitMerge\Review-Merge.ps1" -SourceRef "가져올브랜치" -TargetBranch "대상브랜치"
```

출력된 `PlanFile`의 JSON 옆에 같은 이름의 Markdown 보고서가 생성됩니다.

| 에셋 상태 | 기본 선택 |
| --- | --- |
| 가져올 브랜치에서 새로 추가 | 가져오기 |
| 대상 브랜치의 마지막 에셋 변경을 포함한 후 수정 | 가져오기 |
| 대상 브랜치보다 이전 에셋 | 대상 유지 |
| 양쪽에서 별도로 수정, 삭제 충돌 | 대상 유지 + 직접 검토 표시 |

판별 기준은 Git 변경 이력입니다. 저장 시각·파일 수정 시각·엔진 버전으로 내용의 신구를 판단하지 않습니다. 새 커밋도 의도적으로 옛 내용을 저장할 수 있으므로 실제 내용의 적합성은 에디터에서 확인해야 합니다.

계획을 확인한 뒤 별도 폴더에서 병합을 준비합니다.

```powershell
pwsh -File "C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01GitMerge\Review-Merge.ps1" -Action Prepare -PlanFile "출력된 계획 JSON의 전체 경로"
```

검토 표시된 에셋 중 원본 버전을 가져오려면 저장소 기준 경로를 명시합니다.

```powershell
pwsh -File "C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01GitMerge\Review-Merge.ps1" -Action Prepare -PlanFile "계획 JSON 전체 경로" -SourceAsset "Source Code/ProjectProject01/Content/MyProject/Level/예시.umap"
```

`PreviewPath`에 독립된 worktree, `PreviewBranch`에 검토 브랜치를 만듭니다. 코드 충돌은 `CodeConflicts`에 남으며 직접 해결해야 합니다. 에셋의 `reviewRequired`가 남아 있어도 검토 폴더는 만들지만, 승인·병합 완료를 의미하지 않습니다.

LFS 파일은 이 폴더에서 포인터 상태로 준비합니다. 에디터로 열기 전 검토 폴더에서 `git lfs checkout`을 실행하세요. 로컬에 객체가 없으면 원격에서 가져와야 합니다. 원래 에디터를 이 검토 폴더로 바꾸지 말고 프로젝트 전체를 별도로 여세요.

검토·충돌 해결·실행 확인 후 검토 브랜치를 커밋하고 대상 브랜치에 병합합니다. 중간에 대상이나 원본 브랜치가 바뀌면 계획부터 다시 만드세요. 이 도구는 기존 GitHub Desktop의 일반 병합 버튼 동작을 바꾸지 않습니다.

브랜치 전환 때마다 에디터가 다른 버전의 에셋을 저장하는 일을 줄이려면 브랜치별 worktree를 따로 사용하고, 실행 중인 에디터·테스트가 사용하는 폴더에서는 브랜치를 전환하지 마세요.

회귀 검사는 같은 폴더의 `Test-ReviewMerge.ps1`로 실행합니다. 실제 프로젝트 브랜치 대신 `Saved/GitMergeTests` 아래 독립 저장소에서 검사합니다.

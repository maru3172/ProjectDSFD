# 맵·에셋을 보호하는 병합 검토

PowerShell 7에서 실행합니다. 실제 작업 폴더와 `main`은 변경하지 않으며, 커밋하지 않은 작업은 가져오지 않습니다.

```powershell
pwsh -File "C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01GitMerge\Review-Merge.ps1" -SourceRef "가져올브랜치"
```

출력된 `PlanFile`의 JSON 옆에 같은 이름의 Markdown 보고서가 생성됩니다.

| 에셋 상태 | 기본 선택 |
| --- | --- |
| 원본 브랜치에서 새로 추가 | 가져오기 |
| main의 마지막 에셋 변경을 포함한 후 수정 | 가져오기 |
| main보다 이전 에셋 | main 유지 |
| 양쪽에서 별도로 수정, 삭제 충돌 | main 유지 + 직접 검토 표시 |

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

검토·충돌 해결·실행 확인 후 검토 브랜치를 커밋하고 main에 병합합니다. 중간에 main이나 원본 브랜치가 바뀌면 계획부터 다시 만드세요. 이 도구는 기존 GitHub Desktop의 일반 병합 버튼 동작을 바꾸지 않습니다.

브랜치 전환 때마다 에디터가 다른 버전의 에셋을 저장하는 일을 줄이려면 브랜치별 worktree를 따로 사용하고, 실행 중인 에디터·테스트가 사용하는 폴더에서는 브랜치를 전환하지 마세요.

회귀 검사는 같은 폴더의 `Test-ReviewMerge.ps1`로 실행합니다. 실제 프로젝트 브랜치 대신 `Saved/GitMergeTests` 아래 독립 저장소에서 검사합니다.

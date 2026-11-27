# 에이전트 작업 규칙 (Codex, Claude 공통)

Unreal 프로젝트: `Source Code/ProjectProject01` (UE 5.8, 대상 `ProjectDFSDEditor`).

## 빌드 중 소스 편집 금지

- 빌드가 도는 동안(`UnrealBuildTool`, `cl`, `link`, `UbaHost` 실행 중)에는 `Source Code/ProjectProject01/Source` 아래의 `.h/.cpp/.inl/.c` 파일을 저장하지 않는다. 빌드가 끝난 뒤 편집한다.
- 이유: UBT는 수정 시각으로 재컴파일을 판단한다. 컴파일러가 이미 읽은 파일을 빌드 중에 저장하면, 옛 내용으로 만든 `.obj`가 최신처럼 남아 클래스 배치가 서로 다른 코드가 섞이고 실행 중 크래시가 난다(2026-11-04 데스캠 크래시). 자세한 내용: `Source Code/ProjectProject01/Docs/BuildGuard.md`.
- 빌드는 `pwsh -File "Source Code/ProjectProject01/Tools/BuildGuard/SafeBuild.ps1"`(또는 Windows PowerShell `powershell -ExecutionPolicy Bypass -File ...`)로 한다. 빌드 중 바뀐 소스가 있으면 영향받은 `.obj`를 지우고 한 번 더 빌드한다.
- 모든 빌드는 `Source/*.Target.cs`의 빌드 단계 검사(`Tools/BuildGuard/BuildStepGuard.ps1`)도 거친다. `BuildGuard: error: source files were saved while ... was compiling`로 빌드가 실패하면 편집을 멈추고 다시 빌드하면 된다. `BuildGuard: warning: check skipped`는 빌드는 정상이고 검사 스크립트만 고치면 된다는 뜻이다.
- 에디터, Live Coding, UnrealBuildTool이 실행 중이면 새 빌드를 시작하지 않는다. 여러 세션이 같은 폴더를 쓰면 한 번에 하나만 빌드한다.

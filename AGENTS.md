# 에이전트 작업 규칙 (Codex, Claude 공통)

Unreal 프로젝트: `Source Code/ProjectProject01` (UE 5.8, 대상 `ProjectDFSDEditor`).

## 빌드 중 소스 편집 금지

- 빌드가 도는 동안(`UnrealBuildTool`, `cl`, `link`, `UbaHost` 실행 중)에는 `Source Code/ProjectProject01/Source` 아래의 `.h/.cpp/.inl/.c` 파일을 저장하지 않는다. 빌드가 끝난 뒤 편집한다.
- 이유: UBT는 수정 시각으로 재컴파일을 판단한다. 컴파일러가 이미 읽은 파일을 빌드 중에 저장하면, 옛 내용으로 만든 `.obj`가 최신처럼 남아 클래스 배치가 서로 다른 코드가 섞이고 실행 중 크래시가 난다(2026-11-04 데스캠 크래시). 자세한 내용: `Source Code/ProjectProject01/Docs/BuildGuard.md`.
- 빌드는 `pwsh -File "Source Code/ProjectProject01/Tools/BuildGuard/SafeBuild.ps1"`(또는 Windows PowerShell `powershell -ExecutionPolicy Bypass -File ...`)로 한다. 빌드 중 바뀐 소스가 있으면 영향받은 `.obj`를 지우고 한 번 더 빌드한다.
- 모든 빌드는 `Source/*.Target.cs`의 빌드 단계 검사(`Tools/BuildGuard/BuildStepGuard.ps1`)도 거친다. `BuildGuard: error: source files were saved while ... was compiling`로 빌드가 실패하면 편집을 멈추고 다시 빌드하면 된다. `BuildGuard: warning: check skipped`는 빌드는 정상이고 검사 스크립트만 고치면 된다는 뜻이다.
- 에디터, Live Coding, UnrealBuildTool이 실행 중이면 새 빌드를 시작하지 않는다. 여러 세션이 같은 폴더를 쓰면 한 번에 하나만 빌드한다.

## 개발 기록

- 모든 개발 작업(성공·실패·중단·미완료 포함)은 `Source Code/ProjectProject01/Docs/WorkLogs/YYYY-MM-DD_작업명.md`(한국 시간)에 기록한다. 같은 작업은 같은 파일에 이어 쓰고, 목록은 그 폴더의 `README.md`에 추가한다.
- 항목: 요약(목표·문제·해결·결과·상태), 요청과 범위, 기존 구조, 문제·재현, 가설·결정, 구현·시행착오, 검사(통과·실패·미실행 구분), 정량 결과 표(기준값 없으면 `정량 검증 미완료`), 기여(사용자·팀원·AI 구분), 미완료·인계, 커밋 해시.
- 의도·방향성 대비 달성도: 의도한 행동/효과 → 관찰 가능한 목표/수용 기준 → 실제 결과/근거 → 방향/달성 정도 → 남은 격차를 잇는다. 기능 구현 / 실제 행동 유도 / 사용성·재미·효과를 나눠 쓴다. 분모나 측정 근거가 없는 달성률은 만들지 않고 `달성`·`부분 달성`·`미검증`과 이유를 쓴다.
- 작업 중 주요 판단·실패·검증마다 갱신하고, 끝내거나 넘기기 전에 현재 상태를 남긴다. 확인하지 않은 원인·검사를 지어 쓰지 않는다. 다른 사람의 기록은 덮어쓰지 않는다. 민감정보는 쓰지 않는다.
- 규칙 원문: `Source Code/ProjectProject01/Docs/WorkLogs/RULES.md`.

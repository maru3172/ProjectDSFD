# 백엔드 모듈 구조 (2026-11-01) — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`BackendModules.md`](../BackendModules.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: `Platform/BackendModules.cs`의 `IBackendModule`이 플랫폼과 게임 사이의 유일한 약속입니다. 플랫폼은 정해진 시점마다 등록된 모듈을 순서대로 부르고, 게임 기능의 이름은 모릅니다.
- 기간: 2026-11-01 ~ 2026-11-01 (문서 변경 커밋 2개 기준)
- 결과·검증: 백엔드(`Tools/ProjectProject01Backend`)를 어느 멀티플레이 게임에나 쓰는 **공통 플랫폼**과 이 게임의 규칙인 **게임 모듈**로 나누는 작업의 1단계 기록입니다. 동작은 바뀌지 않았습니다: 기능 검사 PASS 217줄, 실제 DB 통합 검사(격리 MySQL 23307) PASS 1,047줄, 실패 0.
- 현재 상태: 원 문서상 남은 항목 기재 없음(미확인)

## 요청·목표·범위 (9-2)

- `Platform/BackendModules.cs`의 `IBackendModule`이 플랫폼과 게임 사이의 유일한 약속입니다. 플랫폼은 정해진 시점마다 등록된 모듈을 순서대로 부르고, 게임 기능의 이름은 모릅니다.
- 게임 모듈 목록은 `Game/GameModules.cs` 한곳에 있고, 어셈블리가 열릴 때 스스로 등록합니다(`ModuleInitializer`). 그래서 서버와 검사 프로그램이 같은 모듈을 봅니다.
- `BotMatch → CustomRules → Affix → BotRating → CaseRecords → MainQuest`. 봇 레이팅은 사용자 지정 규칙과 어픽스가 기록된 뒤에 "공식 경기인가"를 판단하므로 그 뒤에 와야 합니다. 이전 코드의 실행 순서와 같습니다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`BackendModules.md`](../BackendModules.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `"Source Code/ProjectProject01/Tools/CollaborationTool/Docs/\353\217\205\353\246\275\354\213\244\355\226\211_\354\202\254\354\232\251\353\262\225.md"` (2)
- `ProjectProject01/Tools/CollaborationTool/app/team-tools.tsx` (2)
- `ProjectProject01/Tools/CollaborationTool/scripts/check-team-tools.mjs` (2)
- `ProjectProject01/Tools/CollaborationTool/self-host/desktop.css` (2)
- `ProjectProject01/Tools/CollaborationTool/self-host/features.mjs` (2)
- `ProjectProject01/Tools/CollaborationTool/app/project-tools.tsx` (1)
- `ProjectProject01/Tools/CollaborationTool/self-host/migrations/0006_asset_locks.sql` (1)
- `ProjectProject01/Tools/CollaborationTool/self-host/project-tools.mjs` (1)
- `ProjectProject01/Source/ProjectProject01/Private/FootstepSynthComponent.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MannequinAICharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- `Game/` 대신 자기 게임의 모듈을 `IBackendModule`로 만들고 `GameModules.All`에 넣습니다.
- 2단계(아래 결합을 없애고 프로젝트를 둘로 나누기)는 하지 않습니다. 다음 프로젝트가 같은 형태(언리얼, 소수 인원 세션)일지 정해지지 않았고, 방·매칭 핵심 코드를 바꾸는 위험에 비해 이 게임에는 얻는 것이 없기 때문입니다. 대신 새로 만드는 게임 기능은 `Game/`에 `IBackendModule`로 넣어 결합을 늘리지 않습니다. 같은 형태의 다음 프로젝트가 정해지면 그때 아래 목록부터 진행합니다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 백엔드(`Tools/ProjectProject01Backend`)를 어느 멀티플레이 게임에나 쓰는 **공통 플랫폼**과 이 게임의 규칙인 **게임 모듈**로 나누는 작업의 1단계 기록입니다. 동작은 바뀌지 않았습니다: 기능 검사 PASS 217줄, 실제 DB 통합 검사(격리 MySQL 23307) PASS 1,047줄, 실패 0.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

**정량 검증 미완료** — 원 문서에 측정 기준값·측정값이 없다.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | `Platform/BackendModules.cs`의 `IBackendModule`이 플랫폼과 게임 사이의 유일한 약속입니다. 플랫폼은 정해진 시점마다 등록된 모듈을 순서대로 부르고, 게임 기능의 이름은 모릅니다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 2
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 원 문서에 기재 없음(미확인)

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `0cbdc157` | 2026-11-01 | maru3172 | Add meeting recording tool 2 |
| `0d09d24c` | 2026-11-01 | maru3172 | Add meeting recording tool |

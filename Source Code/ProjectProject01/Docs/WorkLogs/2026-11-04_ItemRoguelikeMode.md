# 아이템 로그라이크 모드 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`ItemRoguelikeMode.md`](../ItemRoguelikeMode.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: PvP 모드다. 오픈매칭과 랜덤매칭 모두에서 고를 수 있다. 랜덤매칭은 모드별로 대기열을 따로 쓴다. 300초 뒤 봇 채우기도 같은 모드 안에서만 한다.
- 기간: 2026-11-04 ~ 2026-11-06 (문서 변경 커밋 2개 기준)
- 결과·검증: 원 문서에 검증 기록 없음
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- PvP 모드다. 오픈매칭과 랜덤매칭 모두에서 고를 수 있다. 랜덤매칭은 모드별로 대기열을 따로 쓴다. 300초 뒤 봇 채우기도 같은 모드 안에서만 한다.
- 경기 시작 때 화면을 암전한 채로 각자에게 카드 3장을 보여 주고, 1장을 고르게 한다. 모두 고르면 암전을 해제하고 경기를 진행한다.
- 다음 층에 도착할 때마다 같은 방식으로 다시 고른다. 맵이 3층이면 한 경기에 3번 고른다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`ItemRoguelikeMode.md`](../ItemRoguelikeMode.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/MannequinAICharacter.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectAffixRuntimeItems.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectItemRoguelikeRuntime.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectLoadingGate.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LobbySubsystem.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Public/MannequinAICharacter.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestGameMode.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestPlayerController.h` (2)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 기준일: 2026-11-04. 설계 단계 문서다. 구현 완료를 뜻하지 않는다. 수치는 구현 후 조정한다.
- 고를 때마다 새로고침을 1번 쓸 수 있다. 새로고침하면 3장을 모두 다시 뽑는다. 제한 시간은 다시 시작하지 않는다.
- 서버가 거절하는 경우: 선택 단계가 아닐 때, 이미 고른 층, 범위를 벗어난 번호, 두 번째 새로고침, 시간이 지난 뒤, 관전자나 다른 사람 명의의 요청.
- 봇 자리는 서버가 대신 고른다. 기존 마네킹 AI는 바꾸지 않는다. 아이템은 수치 변경과 표시로만 건다.
- 백엔드는 모드 선택(방 설정, 랜덤매칭 대기열)과 입장 티켓의 모드 필드만 맡는다. 고른 카드는 백엔드에 저장하지 않는다.
- 바꾼 것: M07 '검은 천'은 실루엣을 어둡게 할 재질 작업이 없어 '녹슨 태엽'(정지 몸체가 시선에서 풀린 직후 1초 +15%)으로 바꿨다. 소리 신호 일부(무전 잡음, 벨, 캔, 방독면 숨, 천 끌림)는 빛 표시(펄스)와 발소리 크기로 대신했다. 클라이언트 펄스가 최대 3초라 S11 발자국은 3초이고, S06과 S12 표시는 서버가 다시 보낸다. M04는 예비 동작 대신 공격 순간에 팔이 빛난다.
- 게임: `ProjectItemRoguelike.h`(카드), `ProjectItemRoguelikeRuntime`(서버 선택·판정·carryover), `ProjectRoguePick`(선택 화면·입력 잠금), `ProjectAffixRuntimeItems.cpp`(아이템 효과), 로딩 게이트 대기, 결과 화면 카드 기록.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 원 문서에 검증 기록 없음. 실행 여부 미확인 — 통과로 간주하지 않는다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

**정량 검증 미완료** — 원 문서에 측정 기준값·측정값이 없다.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | PvP 모드다. 오픈매칭과 랜덤매칭 모두에서 고를 수 있다. 랜덤매칭은 모드별로 대기열을 따로 쓴다. 300초 뒤 봇 채우기도 같은 모드 안에서만 한다. | 원 문서에 수용 기준으로 명시되지 않음 | 검증 기록 없음 | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 2
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 제한 시간은 20초다. 그때까지 고르지 않은 사람은 서버가 남은 카드 중에서 무작위로 정해 준다. 선택하는 동안 경기 시간은 멈춘다.
- 검사: `ProjectProject01.ItemRoguelike.Rules`, `ProjectProject01.ItemRoguelike.PickRender`. 아직 빌드와 실행을 하지 않았다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `342421a` | 2026-11-06 | maru3172 | MiniGame 3 |
| `d0de20f` | 2026-11-04 | maru3172 | Mannequin UI update |

## 작업 세션 직접 기록 (2026-11-04, Claude 세션 local_2e236d8f)

> 위 재구성에서 미확인으로 남은 부분을, 구현한 세션이 직접 본 내용으로 덧붙인다.

### 결정 경위
- 사용자 결정: 암전 상태 시작 시 3장 → 전원 선택 시 해제 → 층마다 반복, "섞인 3장 중 1장", 20초 후 자동, 랜덤·오픈 둘 다(랜덤은 별도 대기열), 결과에 표시 후 소멸, 관전자는 전원 카드. 처음 25종 요청 → "15개로 줄이고 새로고침 1번".
- 개발자 기본값(사용자 확인 없음): 새로고침은 선택마다 1회, 비공식 경기 처리, 3장 중 아이템·버프 각 1장 이상, 팀원 간 버프 카드 중복 방지, 버프는 기존 A/B 어픽스 재사용.
- 원안과 달라진 점: M07 '검은 천' → '녹슨 태엽'(재질 작업 없음), 소리 신호 일부를 빛 펄스·발소리 크기로 대체, 펄스 최대 3초 제약으로 S11 3초·S06/S12 서버 재전송, M04 예비 동작 → 공격 순간 빛.

### 구조 변경 핵심
`MatchModes`(방·랜덤 대기열·티켓 `matchMode`) → `AcceptMatchMode`(어픽스와 동시 불가) → 로딩 게이트 AllReady에서 `HoldGate`(선택 중 게이트·경기 시계 정지) → 버프는 `HasAffixRule`로 어픽스 런타임에 합류, 아이템은 `ProjectAffixRuntimeItems.cpp` → carryover `Rogue` → 결과 `RoguePicks`.

### 시행착오
- 첫 UE 빌드 오류: RPC 매개변수 `Offer`가 멤버를 가림(C4458) → `InOffer`. `PreManualMannequinCommands` 미선언 2건은 빌드 도중 헤더 편집 탓(AGENTS.md 빌드 중 편집 금지의 사례).
- CRLF 파일에 `perl -0pi` 다중 행 치환이 조용히 실패 → Edit 도구로 전환.
- Git Bash 경로의 `-log=`로 UE 검사 로그가 안 남음 → PowerShell `-abslog`.
- 선택 패널 폭 1080 고정은 800×600에서 넘침 → 화면 폭 기준 람다.
- 티켓 없는 PIE에서 UserId가 비어 선택에서 빠짐 → `KeyOf`(이름으로 대체).

### 검증(실행한 것)
| 검사 | 결과 |
|---|---|
| 백엔드 `dotnet build` | 오류 0·경고 0 |
| `ProjectProject01.ItemRoguelike.Rules`, `.PickRender`(1280×720·800×600) | 통과 |
| 회귀 CustomRules 2, MannequinWheel, CaseRecords.RenderPages, ManualAttackRange | 통과 |
| `Run-IsolatedIntegration.ps1 -FrontendOnly`(23307) | 387건 통과(경기 모드 신규 17건) |
| 실제 다중 PC 대전·체감·밸런스 | 미실행 |

### 의도 대비 달성도 (9-9-1)
| 의도 | 수용 기준 | 결과·근거 | 구분 | 판정 | 남은 격차 |
|---|---|---|---|---|---|
| 일반 모드 규칙 불변 | 일반 경기에 로그라이크 규칙·버프 없음, 공식 결과 유지 | `Rules` 검사 단언 통과 | 기능 구현 | 달성 | 일반 경기 실제 회귀 플레이 없음 |
| 서버가 카드·판정 소유, 20초 자동 | 잘못된 요청 거부, 시간 초과 자동, 그때까지 게이트 닫힘 | 판정 단위 검사 통과 | 기능 구현 | 부분 달성 | 실제 클라이언트 암전·입력 잠금 미확인 |
| 층마다 재선택·경기 끝까지 유지 | carryover 후 재선택, 보유 카드 제외 | 단위 검사 통과 | 기능 구현 | 부분 달성 | 실제 층 이동 미확인 |
| 결과에 카드 표시(관전자는 전원) | 결과 구조체·화면 표시 | 구조체 단언 통과, 화면 검사 없음 | 기능 구현 | 부분 달성 | 결과 화면 미확인 |
| 상대 신호를 읽고 대응하는 전략성 | 플레이어 행동 변화 관찰 | 없음 | 행동 유도 | 미검증 | 실제 대전 관찰 |
| 공포 분위기·재미 | 플레이어 반응 | 없음 | 사용성·효과 | 미검증 | 플레이 테스트 |
| 원안 신호 그대로 | 30종 모두 문서 신호대로 | 일부 대체·교체 | 기능 구현 | 부분 달성 | 사운드 신호 미구현 |

### 기여
요구·결정: 사용자. 조사·설계·구현·검사: Claude. 커밋 `d0de20f`: 사용자(다른 세션 변경 포함).

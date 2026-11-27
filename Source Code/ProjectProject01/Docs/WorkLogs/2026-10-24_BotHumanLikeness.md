# 봇의 사람다운 요소 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`BotHumanLikeness.md`](../BotHumanLikeness.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 봇전에서 플레이어가 "이거 사람 아니야?" 하고 느낄 수 있게 넣은 요소를 모아 둔 문서다. 숫자는 모두 현재 코드 값이고, 난이도는 1(쉬움)부터 5(마스터)까지다.
- 기간: 2026-10-24 ~ 2026-11-01 (문서 변경 커밋 4개 기준)
- 결과·검증: 원 문서에 검증 기록 없음
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 봇전에서 플레이어가 "이거 사람 아니야?" 하고 느낄 수 있게 넣은 요소를 모아 둔 문서다. 숫자는 모두 현재 코드 값이고, 난이도는 1(쉬움)부터 5(마스터)까지다.
- 정책: `Source/ProjectProject01/Public/ProjectAdaptiveBotPolicy.h`
- 실행: `Source/ProjectProject01/Private/ProjectMatchBots.cpp`
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`BotHumanLikeness.md`](../BotHumanLikeness.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/ProjectMatchBots.cpp` (4)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectAdaptiveBotPolicy.h` (4)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestGameMode.h` (3)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01GameInstance.h` (3)
- `ProjectProject01/Source/ProjectProject01Editor/Private/ProjectBotSimulationTests.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectLoadingScreenSubsystem.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LobbySubsystem.cpp` (2)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- **출구 문제:** 309초 교착은 출구 문제였다. 봇이 탈출 트리거 근처까지는 갔지만 박스 안으로 들어가지 못했다. 멈춤 감시가 그 상태를 정지로 보고 멀리 보냈고, 봇이 다시 돌아오기를 반복했다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- **이름.** 사람 같은 닉네임을 무작위로 쓴다. 봇이 있다는 것은 경기 전에 알리므로, 이름에 봇 표시는 하지 않는다.
- 관찰·엄호·대기 대신 출구로 향하고, 계속 달린다. 스태미나도 아끼지 않는다.
- **생존자:** 아는 출구에 가까워질수록 마지막 구간을 망설임 없이 달린다. 거리 40% 지점부터 관찰·엄호 대신 출구로 곧장 뛴다.
- 세밀한 보정은 어두운 실내 맵과 방화셔터가 들어온 뒤로 미룬다(사용자 결정). 봇이 빛과 셔터를 인식하게 만든 뒤 다시 잰다.
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
| 기능 구현 | 봇전에서 플레이어가 "이거 사람 아니야?" 하고 느낄 수 있게 넣은 요소를 모아 둔 문서다. 숫자는 모두 현재 코드 값이고, 난이도는 1(쉬움)부터 5(마스터)까지다. | 원 문서에 수용 기준으로 명시되지 않음 | 검증 기록 없음 | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 4
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 경기에 남은 생존자가 한 명뿐이면(다른 한 명이 잡혔거나, 탈출했거나, 출구로 넘어갔을 때) 양쪽이 과감해진다. 이건 전술 목록에 넣지 않은 상태 변화라 패턴 개수(14/14)에는 들어가지 않는다. 그 층이 끝날 때까지 이어지고, 세기는 단계별로 0.6 + 0.4 × 실력이다.
- **마네킹:** 남은 생존자를 잡으려고 든다.
- 3단계 마네킹에만 허점 15%: 43%/41%로 오르지 않았다.
- 1단계를 더 올리려고 해 봤다가 뺀 것: 빈틈 때 조종 몸체를 세우기(60%), 혼자 + 도우미 판의 1단계 빈틈 40→65%(72%), 몸체 전환 직후 세워둘 몸 다시 확인(73%). 1단계는 72~75%에서 움직이지 않았다.
- 기능은 갖췄고, 남은 일은 수치 조정이다. 다음을 실제 플레이에서 확인한다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `0d09d24c` | 2026-11-01 | maru3172 | Add meeting recording tool |
| `3dcf47f9` | 2026-10-30 | maru3172 | Bot win rate adjustment |
| `e448024e` | 2026-10-27 | maru3172 | UI Design Revamp |
| `d2f4865c` | 2026-10-24 | maru3172 | Bot Mode Adjustment |

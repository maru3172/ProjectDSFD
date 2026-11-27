# 퀘스트·대화 그래프 (설계, 2026-11-10) — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`QuestGraph.md`](../QuestGraph.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: **NPC**: `id`, 이름. 휴대폰 메신저의 대화 상대다.
- 기간: 2026-11-10 ~ 2026-11-11 (문서 변경 커밋 2개 기준)
- 결과·검증: 원 문서에 검증 기록 없음
- 현재 상태: 원 문서상 남은 항목 기재 없음(미확인)

## 요청·목표·범위 (9-2)

- **NPC**: `id`, 이름. 휴대폰 메신저의 대화 상대다.
- **퀘스트**: `id`, 이름, 종류(메인/서브), 여는 조건(모두 만족해야 열림, 비어 있으면 처음부터), 시작 단계, 단계 목록.
- 연결은 위에서부터 조건(플래그 있음/없음)을 보고 처음 맞는 곳으로 간다. 마지막은 조건 없는 기본 연결이다. 연결이 없으면 퀘스트 완료다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`QuestGraph.md`](../QuestGraph.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01Tutorial.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectSingleIntro.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01GameInstance.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01LoginWidget.h` (2)
- `ProjectProject01/Tools/ProjectProject01Backend/Game/MainQuest.cs` (2)
- `ProjectProject01/Tools/ProjectProject01Backend/Game/QuestGraph.cs` (2)
- `ProjectProject01/Tools/ProjectProject01Backend/Game/QuestProgress.cs` (2)
- `ProjectProject01/Tools/ProjectProject01Backend/Platform/CommunityEndpoints.cs` (2)
- `ProjectProject01/Tools/ProjectProject01Backend/Platform/OperatorAccess.cs` (2)
- `ProjectProject01/Tools/ProjectProject01Backend/Program.cs` (2)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 지금의 메인 퀘스트(`탐사`)는 한 줄로 이어진 10단계뿐이고, 대화 선택지는 답장만 바꾼다. 앞으로 NPC, 메인 라인, 서브퀘스트를 계속 추가하고 선택에 따라 이야기가 갈라지게 하려고 구조를 바꾼다. 운영 도우미에서 그래프로 보고 고친다.
- **단계**: `id`, 제목, 목표(지금과 같은 게임 이벤트 목록: 튜토리얼, 경기 플레이, 층 탈출, 붙잡기, 승리, 대화), 이야기, 대화(선택), 다음 단계 연결 목록.
- 선택 노드: 선택지 2~4개. 선택지마다 보내는 말, 답장(최대 6줄), 남길 플래그, 줄 퀘스트(서브퀘스트 받기), 다음 노드.
- **플래그**: 플레이어마다 남는 이름표(예: `trust_kalin`). 선택으로만 생긴다.
- 대화 기록: 플레이어, 대화, 고른 경로(선택 노드와 고른 번호 목록). 서버가 그래프를 따라가며 경로가 맞는지 검사한다.
- 플레이어의 단계 번호를 단계 `id`로 옮기고, 저장된 선택 번호를 대화 경로로 옮긴다. 지금 대화는 한 줄이라 그대로 대응된다.
- 휴대폰 메신저: 대화를 그래프로 따라간다. 선택하면 그 선택지의 다음 노드로 간다. 다 읽은 대화는 고른 경로대로 다시 읽을 수 있다(지금과 같음).
- 단계를 두 번 누르면 대화 그래프. 메시지·선택 노드 추가·삭제·연결, 선택지의 플래그와 줄 퀘스트 정하기.
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
| 기능 구현 | **NPC**: `id`, 이름. 휴대폰 메신저의 대화 상대다. | 원 문서에 수용 기준으로 명시되지 않음 | 검증 기록 없음 | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
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
| `9dcab42` | 2026-11-11 | maru3172 | Quest Tool |
| `eb3e400` | 2026-11-10 | maru3172 | Quest |

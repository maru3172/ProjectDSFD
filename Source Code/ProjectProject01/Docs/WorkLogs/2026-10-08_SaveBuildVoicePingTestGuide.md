# 싱글 저장·빌드 도우미·생존자 통신 테스트 가이드 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`SaveBuildVoicePingTestGuide.md`](../SaveBuildVoicePingTestGuide.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 타이틀에서 `싱글 플레이`를 눌러 `LevelTest01`에 들어갑니다.
- 기간: 2026-10-08 ~ 2026-10-08 (문서 변경 커밋 1개 기준)
- 결과·검증: 이 문서는 이번 구현의 실제 사용과 검증 절차를 한곳에 정리합니다. 붙잡힘·구출·공통 상호작용은 이번 범위에 포함하지 않습니다.
- 현재 상태: 원 문서상 남은 항목 기재 없음(미확인)

## 요청·목표·범위 (9-2)

- 타이틀에서 `싱글 플레이`를 눌러 `LevelTest01`에 들어갑니다.
- `F1` 메뉴에서 `현재 체크포인트 저장`을 누릅니다.
- 위치를 옮기거나 스태미나·데스카운트를 변화시킵니다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`SaveBuildVoicePingTestGuide.md`](../SaveBuildVoicePingTestGuide.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01Checkpoint.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01PingMarker.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SaveSubsystem.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SinglePlayerSaveGame.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01VoiceChatSubsystem.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/ProjectProject01.Build.cs` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 마네킹과 탈락·탈출한 생존자가 채널에서 제외되는지 확인합니다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 이 문서는 이번 구현의 실제 사용과 검증 절차를 한곳에 정리합니다. 붙잡힘·구출·공통 상호작용은 이번 범위에 포함하지 않습니다.
- 기존 `ProjectProject01.*` 자동화 테스트: 진단, 타이틀 경로, 멀티 승패, 심박/VFX, 스태미나, 설정 키, 튜닝 검증
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

**정량 검증 미완료** — 원 문서에 측정 기준값·측정값이 없다.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 타이틀에서 `싱글 플레이`를 눌러 `LevelTest01`에 들어갑니다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 원 문서에 기재 없음(미확인)

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `979299a4` | 2026-10-08 | maru3172 | Voice Chatting and Build |

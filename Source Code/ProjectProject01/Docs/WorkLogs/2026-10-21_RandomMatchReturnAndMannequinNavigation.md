# 랜덤 경기 복귀 및 멀티 마네킹 이동 보완 (2026-10-21) — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`RandomMatchReturnAndMannequinNavigation.md`](../RandomMatchReturnAndMannequinNavigation.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 미복귀 참가자의 결과 화면, 기존 경기 참가자 명부, 리더보드 기록은 보존한다. 마지막 참가자가 복귀하면 이전 랜덤 경기 방만 제거한다.
- 기간: 2026-10-21 ~ 2026-10-21 (문서 변경 커밋 1개 기준)
- 결과·검증: 최종 결과: 에디터 빌드 성공, 멀티 자동 검사 8개 통과(실패 0), 격리 랜덤 매칭 검사 53개 통과. 기존 오픈 매칭 방 유지 및 권위 결과 보존을 포함한 격리 API 회귀 검사도 통과했다. 임시 NavMesh 생성·종료 시 발생하는 경고는 별도로 보고서에 남는다.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 미복귀 참가자의 결과 화면, 기존 경기 참가자 명부, 리더보드 기록은 보존한다. 마지막 참가자가 복귀하면 이전 랜덤 경기 방만 제거한다.
- 중복 복귀 요청은 기존 개인 대기 공간을 그대로 반환한다. 종료 확인과 복귀 요청은 같은 잠금 순서로 처리한다.
- 클라이언트 복귀 요청 자체는 진행 중인 경기를 끝낼 권한이 없다. 종료 확인보다 먼저 복귀했다면 서버 종료 확인 시 개인 대기로 분리한다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`RandomMatchReturnAndMannequinNavigation.md`](../RandomMatchReturnAndMannequinNavigation.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/MannequinAICharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayMannequinNavigation.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LobbySubsystem.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LocomotionFeedbackTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01MannequinCameraTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01MannequinNavigationTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01OperationsTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01Tutorial.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 서버가 경기 종료를 확정한 뒤 복귀한 참가자를 즉시 별도의 1인 랜덤 대기 공간으로 옮긴다. 다른 참가자의 복귀를 기다리지 않으며 자동 재신청하지 않는다.
- 역할 스폰 지점의 같은 층 바닥, 보행 가능한 표면 및 캡슐 충돌 여유를 검사한다. 잘못된 지점은 건너뛰며 스폰 지점의 배율을 캐릭터에 복사하지 않는다.
- 추종·추격은 실제 에이전트의 NavMesh에 투영한 목적지와 완전한 A* 경로를 사용한다. 부분 경로나 벽을 관통하는 직선 이동으로 대체하지 않는다.
- AI는 낭떠러지 보행 및 NavMesh 밖으로 벗어날 수 있는 RVO 회피를 사용하지 않는다. 바닥 아래로 빠지면 마지막으로 지지 바닥을 확인한 위치를 재검사한 뒤 복구하고 원래 좌표를 경고 로그에 남긴다. 직접 조작 중에는 AI 복구로 플레이어를 이동시키지 않는다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 격리 HTTP/MySQL 검사: `Saved/ReviewRemediation/RandomReturn-Integration-Verified-20261021.log`.
- 에디터 자동 검사: `Saved/ReviewRemediation/RandomReturn-NavSafety-Verified-20261021/index.json`.
- 최종 결과: 에디터 빌드 성공, 멀티 자동 검사 8개 통과(실패 0), 격리 랜덤 매칭 검사 53개 통과. 기존 오픈 매칭 방 유지 및 권위 결과 보존을 포함한 격리 API 회귀 검사도 통과했다. 임시 NavMesh 생성·종료 시 발생하는 경고는 별도로 보고서에 남는다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 8개 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 53개 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 미복귀 참가자의 결과 화면, 기존 경기 참가자 명부, 리더보드 기록은 보존한다. 마지막 참가자가 복귀하면 이전 랜덤 경기 방만 제거한다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 원 문서에 Claude/Codex(에이전트) 관련 서술이 있다. 개별 변경의 작성 주체는 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 오픈 매칭은 기존의 전원 복귀 및 같은 방 유지 규칙을 변경하지 않았다.
- 싱글플레이의 Behavior Tree 및 추적 알고리즘은 수정하지 않았다.
- 패키징은 실행하지 않았다. 배포 반영 시 백엔드 재빌드·재시작 및 데디 서버 재패키징이 필요하다.
- 실제 문제가 발생한 맵 좌표는 아직 제공되지 않았다. 맵 자체의 빠진 충돌이나 끊긴 NavMesh를 코드가 새로 만들어 주지는 않는다. 재발 시 `Skipping unsafe mannequin spawn` / `Recovered multiplayer mannequin` 로그 좌표로 해당 맵 구간을 확인해야 한다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `ff573889` | 2026-10-21 | maru3172 | Manipulation improvement |

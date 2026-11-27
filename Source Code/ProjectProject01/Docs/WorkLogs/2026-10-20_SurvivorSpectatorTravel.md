# 멀티 생존자 관전과 맵 이동 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`SurvivorSpectatorTravel.md`](../SurvivorSpectatorTravel.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 사망 후 동료가 트리거에 도착하는 경우와, 트리거 대기 후 동료가 사망하는 경우 모두 같은 결과여야 한다.
- 기간: 2026-10-20 ~ 2026-10-20 (문서 변경 커밋 2개 기준)
- 결과·검증: 2026-10-20 에디터 빌드 성공. `Saved/ReviewRemediation/SurvivorSpectatorTravel-20261020/index.json`: 6개 성공, 경고/실패/미실행 0개, 종료 코드 0.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 사망 후 동료가 트리거에 도착하는 경우와, 트리거 대기 후 동료가 사망하는 경우 모두 같은 결과여야 한다.
- 관전 중에는 자동 Pawn 카메라 전환이 시점을 덮지 않도록 하고, `OnRep_Pawn` 이후와 로컬 프레임에서도 관전 카메라 의도를 재적용한다.
- 살아서 스폰하는 경우에는 이전 관전 상태를 지우고 자기 Pawn 시점과 이동 입력을 복구한다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`SurvivorSpectatorTravel.md`](../SurvivorSpectatorTravel.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01ReviewRegressionTests.cpp` (2)
- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01Elevator.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01ElevatorTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LevelTransitionTrigger.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01OperationsTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01Tutorial.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 출발 GameMode는 `bLevelTransitionDeparting`으로 도착 처리를 차단한다. 과거에는 암전 응답 대기 중 0.1초 Tick의 도착 재검사가 기존 마네킹 시점을 새 맵 시점으로 오인해 carryover/응답 플래그를 지웠다. 재시도마다 마네킹 암전이 다시 시작되어 생존자 둘만 응답하고 시간 초과되던 원인이었다. 새 맵의 새 GameMode에서만 도착 처리를 하며 이동 취소 시 출발 플래그를 해제한다.
- 위 통합 검사는 외부 EOS 음성을 임시 비활성화하고 끝난 뒤 원래 값을 복원한다(설정 파일 저장 없음). 이동용 중간 층 두 맵에 최종 탈출 Escape 태그가 없는 기존 진단은 예상 메시지로 명시했다. 이동 실패/네트워크/검사 오류는 무시하지 않는다. 별도 PC·학교망·최종 패키지의 실제 실행은 여전히 별도 확인 대상이다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 사망한 생존자는 나가기 전까지 살아 있는 생존자를 관전한다. 맵 이동 후에도 사망 상태를 유지하고 재스폰하지 않는다.
- 관전 요청 여부와 대상 Pawn을 소유 클라이언트에게만 복제한다. 일회성 카메라 RPC만으로 관전 상태를 표현하지 않는다.
- 서버는 새 맵의 관전 대상/자기 Pawn이 실제로 준비된 뒤에만 이동 완료를 확정한다. 준비가 늦으면 경기 중 0.1초 간격으로 다시 확인한다.
- 관전 대상은 서버가 현재 소유한 살아 있는 생존자 Pawn만 허용한다. 이전 맵/숨겨진/사망 Pawn, 마네킹, 연결 종료/기권 기록은 제외한다.
- 이전 사망 연출의 관전 타이머가 맵 이동으로 취소돼도, 새 맵 도착 시 서버 경기 기록으로 사망자의 관전을 다시 설정한다.
- 레벨 이동 암전이 시작되면 진행 중인 관전 페이드는 취소하고 이동 암전이 우선한다. 결과 화면이 도착하면 전체 오버레이 대신 게임 카메라만 검게 유지하여 UI를 가리지 않는다.
- 멀티 GameMode가 있는 로컬 Standalone 테스트도 싱글 경로로 빠지지 않게 하여, 실제 맵 이동 확정 시 생존자와 마네킹 모두 같은 준비 경로로 암전한다.
- 이동 전 서버의 고정 지연만 믿지 않는다. 생존자·사망 관전자·마네킹 전원에게 요청 번호를 보내고, 각 클라이언트가 전체 화면 암전 완료 후 검은 프레임을 유지한 것을 확인해야 실제 이동한다. 이전 요청의 응답은 무시하고, 암전 시간 + 5초 안에 전원이 응답하지 않으면 이동을 취소하고 현재 맵을 복구한다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 2026-10-20 에디터 빌드 성공. `Saved/ReviewRemediation/SurvivorSpectatorTravel-20261020/index.json`: 6개 성공, 경고/실패/미실행 0개, 종료 코드 0.
- 격리 자동 검사는 실제 여러 클라이언트 사이의 네트워크 전송이나 배치된 맵의 최종 실행 검증을 대신하지 않는다.
- 후속 검사: `Saved/ReviewRemediation/TutorialFinal-Regression/index.json`의 관전 순서/이동 롤백/이전 상태 승계/엘리베이터 검사를 포함한 11개 성공. 실제 다중 PC에서의 시점은 별도 확인한다.
- 암전 완료 확인 보완 후: `Saved/ReviewRemediation/TravelBlackout-Final-20261020/index.json` — 11개 성공, 경고/실패/미실행 0개, 종료 코드 0. 관전자·마네킹의 이동 전 완료 응답, 이전 요청 무시, 부분 암전 응답 방지, 이전 월드 시점 거부, 도착 로딩/시점 안정성, 지연 프레임의 페이드 건너뛰기 방지 검사를 포함한다. 실제 네트워크/다중 PC의 시각적 동기화는 별도 확인 대상이다.
- 출발/도착 충돌 수정 후: `Saved/ReviewRemediation/DepartureAndTutorialPIE-Final-20261020/index.json` — 14개 성공(그중 실제 PIE 3개는 경고 포함), 실패/미실행 0개, 종료 코드 0. `ProjectProject01.Editor.ThreeClientTravel`에서 실제 인프로세스 전용 서버 + 클라이언트 3개를 실행했다. 생존자 한 명 사망 → 다른 생존자만 이동 트리거 요청 → 세 소유자의
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 6개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 11개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 11개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 14개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 사망 후 동료가 트리거에 도착하는 경우와, 트리거 대기 후 동료가 사망하는 경우 모두 같은 결과여야 한다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 2
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 트리거에 먼저 도착한 생존자는 현재 맵에서만 남은 생존자를 관전하며 기다린다. 다음 맵에서 스폰되면 `Active` 상태로 자기 캐릭터를 직접 조작한다.
- 클라이언트는 대상 참조가 아직 복제되지 않았다면 암전을 유지하고, 참조가 준비된 뒤 관전 또는 자기 시점으로 복귀한다.
- RPC 형식이 변경되었으므로 배포 때는 클라이언트와 데디케이티드 서버를 모두 새로 빌드/패키징한다. 이 작업에서는 에디터 빌드와 자동 검사만 수행하고 패키징하지 않았다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `631f95dc` | 2026-10-20 | maru3172 | Directing improvements |
| `f5aa9543` | 2026-10-20 | maru3172 | Server Bug Fix |

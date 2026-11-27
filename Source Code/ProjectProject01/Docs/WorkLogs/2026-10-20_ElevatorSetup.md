# 생존자 비상 엘리베이터 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`ElevatorSetup.md`](../ElevatorSetup.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: `TransitionWaiting`(층 이동 트리거에 먼저 도착), `Escaped`(탈출), 재접속 유예 중인 연결 끊김은 활성화 사유가 아니다.
- 기간: 2026-10-20 ~ 2026-10-20 (문서 변경 커밋 2개 기준)
- 결과·검증: 하차 자동 닫힘 최종 검사: `Saved/ReviewRemediation/TutorialConfirmAndArrivalClose-Final-20261020/index.json`의 `ProjectProject01.Elevator.Flow/Audio` 성공(경고/실패 0). 서버 권위 하차 인원·문 앞 캡슐/여유 범위·한 명이 남은 경우 대기·사망 관전자 제외·하차 벨 상태·닫힘 보간·재진입 시 현재 위치에서 재열림·착륙 위치 정지와 추가 이동/모터 없음 검사를 포
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- `TransitionWaiting`(층 이동 트리거에 먼저 도착), `Escaped`(탈출), 재접속 유예 중인 연결 끊김은 활성화 사유가 아니다.
- 바깥 버튼 볼륨에 기존 상호작용 시선이 적중하고 상호작용 키를 눌러 호출.
- 기본 **20초 대기(작은 우웅 운행음) → 4초 상승(운행음이 서서히 잦아듦) → 띵/1.5초 문 열림**.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`ElevatorSetup.md`](../ElevatorSetup.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01Elevator.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01ElevatorTests.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LevelTransitionTrigger.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01ReviewRegressionTests.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestGameMode.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestPlayerController.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01Elevator.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01LevelTransitionTrigger.h` (2)
- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 클릭 시선 전달 수정 최종 검사: `Saved/ReviewRemediation/ElevatorClickView-Final-20261020/index.json`, 5개 성공, 경고/실패/미실행 0개. 서버 카메라가 클릭 방향과 다른 경우 기존 판정이 실패함을 재현하고, 수정된 RPC 구현으로 바깥 호출/내부 문 닫힘이 가능함을 검사했다. 거리/차폐/비정상 시선/무인증 요청 거부, Tick을 통한 20초 대기 후 상승과 음향 상태 검사도 통과했다. 이 검사는 RPC
- 이동 후 부활 로그 원인: `ProcessServerTravel: Seamless travel is disabled in PIE` 경고 이후 새 로그인과 새 `LocalPIE` 경기 생성이 발생했다. 이동 전 PIE seamless 설정을 활성화하고 티켓 없는 서버 역할 식별자를 인계하도록 수정했다. 설정을 사용할 수 없으면 새 경기로 초기화하는 대신 이동을 취소한다. 인계 플래그 소비 후에도 사망/기권/탈출 기록은 일반 시작 경로에서 재스폰하지 않으며, 반복 
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 멀티 경기 진행 중 다른 생존자가 **사망/포획으로 탈락하거나 기권 확정**된 경우에만 호출 가능.
- 내부에 있는 생존자가 내부 버튼을 바라보고 상호작용 → **띵/2.5초 문 닫힘 → 2초 하강 운행음 → 운행음이 잦아들며 1.5초 화면 전체 암전 → 서버 맵 이동**. 하강 운행음 구간에는 아직 맵 이동/암전을 시작하지 않는다.
- 새 목적지 맵을 만들었다면 패키징 대상 `MapsToCook`에도 추가한다. 목적지 미지정/누락 시 호출이 활성화되지 않는다. 이번 작업은 기존 맵/블루프린트 바이너리를 수정하거나 패키징하지 않는다.
- `Call Delay Seconds`, `Rise Seconds`, `Door Seconds`(열림), `Close Door Seconds`, `Descent Sound Seconds`, `Departure Fade Seconds`, `Arrival Ride Seconds`, `Below Landing Depth`, 좌/우 문 오프셋을 조정할 수 있다. 기존 `Below Landing Offset`은 수평 X/Y 보정만 사용하며 Z 대신 새 깊이 설정을 사용한다
- 단순히 진입하는 것만으로는 출발하지 않는다. 기존 이동 영역과 엘리베이터의 자동 진입 처리도 분리했다.
- 문을 닫은 후 하강 운행음 구간에 나가거나 죽어도 이동하지 않는다. 암전이 끝난 뒤에만 실제 맵 이동을 요청한다.
- PIE에서도 이동 직전에 `net.AllowPIESeamlessTravel`을 활성화해 새 경기로 초기화되지 않도록 한다. 티켓 없는 PIE의 사용자/역할 식별자는 기존 서버 경기 기록에서 인계하며, 이 처리는 에디터 전용이다. 사망자는 관전자로, 기권자는 기권 상태로 유지하고 다음 맵에서 Pawn을 재생성하지 않는다. 원래의 일반 트리거/싱글 페이드 기본 시간은 0.6초 그대로다.
- 자동 검사는 격리된 월드의 상태·충돌·스폰 선택과 합성 파형 검사다. 실제 3클라이언트 RPC 전달, 배치된 층 맵 간 서버 이동, 귀로 듣는 음향 체감까지 검증한 것으로 해석하면 안 된다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 하차 자동 닫힘 최종 검사: `Saved/ReviewRemediation/TutorialConfirmAndArrivalClose-Final-20261020/index.json`의 `ProjectProject01.Elevator.Flow/Audio` 성공(경고/실패 0). 서버 권위 하차 인원·문 앞 캡슐/여유 범위·한 명이 남은 경우 대기·사망 관전자 제외·하차 벨 상태·닫힘 보간·재진입 시 현재 위치에서 재열림·착륙 위치 정지와 추가 이동/모터 없음 검사를 포
- 출발 층에는 실제 승강로와 열린 바닥 공간을 마련한다. 기본 상승 시작점은 도착 높이보다 **1200cm(12m) 아래**다. `Below Landing Depth`에서 깊이(cm)를 바꾼다. 기존 맵 바닥이 승강로를 덮으면 바닥을 통과해 보이므로 공간을 비워야 한다.
- 멀티 클라이언트의 버튼 판정은 소유자에게 복제된 `SurvivorState == Active`를 사용한다. `AuthenticatedRole`은 서버 전용 값이므로 클라이언트에서 요구하지 않는다. 실제 호출/출발 요청에서는 서버가 인증된 생존자 역할까지 다시 검증한다. 이 수정은 멀티 엘리베이터에만 적용하며 기존 싱글 상호작용은 바꾸지 않는다.
- 멀티 RPC는 F를 누른 순간의 카메라 원점/방향도 보낸다. 서버의 원격 카메라 캐시로 재조준하지 않고 이 시선으로 버튼을 직접 검사한다. 카메라와 Pawn 사이 최대 400cm, 카메라 경로의 Visibility 차폐, 서버 설정 상호작용 거리(최대 500cm), 실제 버튼 적중, 내부 탑승 여부, 역할/경기/목적지 검사를 모두 통과해야 한다. 검사 후 다시 오래된 서버 카메라로 재검사하지 않는다.
- 자동 검사 이름: `ProjectProject01.Elevator.Flow`, `ProjectProject01.Elevator.Audio`. 맵 배치 후 실제 멀티 맵 이동/오디오 체감 검증은 별도로 필요하다.
- `ProjectProject01Editor Win64 Development` 빌드 성공.
- 엘리베이터 상태/상호작용/초기 복제 순서/재열림 연속 보간/내부 스폰 지점 검사와 음향 파형 검사 통과.
- 기존 `LevelTransitionResolution`, `MatchCarryover`, `TravelRollback` 회귀 검사도 통과. 총 5개 성공, 경고/실패/미실행 0개.
- 결과: `Saved/ReviewRemediation/Elevator-Verified-20261020/index.json`.
- 클라이언트 역할 판정 수정 후 재검사도 5개 성공, 경고/실패/미실행 0개: `Saved/ReviewRemediation/ElevatorClientRole-20261020/index.json`. 바깥/안쪽 버튼에서 비권한 액터와 빈 인증 역할을 모사해 클라이언트 판정이 가능함을 확인하고, 비활성 생존 상태·인증 없는 서버 요청은 계속 거부됨을 검사했다. 싱글 이동 롤백 회귀 검사도 포함한다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 5개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 5개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | `TransitionWaiting`(층 이동 트리거에 먼저 도착), `Escaped`(탈출), 재접속 유예 중인 연결 끊김은 활성화 사유가 아니다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 2
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 실제 PIE 재시도 로그에서는 요청 송신과 서버 수신이 모두 기록됐으나 `RoleOrSurvivorState`로 거절되고 `Ready`에 머물렀다. `RegisterMatchPlayer`는 로컬 PIE 역할을 경기 기록에만 등록하고 티켓 기반 `AuthenticatedRole`을 채우지 않는데, 엘리베이터가 후자만 요구한 것이 원인이었다. 따라서 호출 단계/운행음/상승은 시작조차 하지 않았다. 시선 수정만으로 이 원인이 해결된 것은 아니었다.
- 도착 맵은 단일 맵 읽기 전용 검사로 `MultiplayTest2`의 `ArrivalId=Elevator_1F`, `ArrivalOnly=true`를 확인했다. 맵/블루프린트는 저장하지 않았다. 도착 활성화는 GameMode 옵션과 서버 스폰 선택의 명시적인 도착 ID도 사용하도록 보완했다. 실제 PIE 네트워크 층 이동과 귀로 듣는 연출 체감은 재확인이 필요하다.
- 기존 맵/블루프린트 바이너리는 수정하지 않았고 패키징도 실행하지 않았다. 배치/연결 후 배포할 때에는 변경된 C++가 들어가도록 클라이언트와 전용 서버를 모두 새로 빌드/패키징한다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `631f95dc` | 2026-10-20 | maru3172 | Directing improvements |
| `bdab1258` | 2026-10-20 | maru3172 | Implementing the elevator functionality |

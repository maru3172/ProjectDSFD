# 127.0.0.1 에디터 실매칭 테스트 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`EditorLocalMatchTesting.md`](../EditorLocalMatchTesting.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 패키징 없이도 에디터 클라이언트에서 실제 백엔드의 오픈 매칭·랜덤 매칭을 거쳐 경기에 입장한다. 경기 맵만 강제로 여는 우회가 아니다.
- 기간: 2026-10-21 ~ 2026-10-21 (문서 변경 커밋 1개 기준)
- 결과·검증: Editor C++ 빌드 및 백엔드·도우미 참조 검사 프로젝트 빌드 통과.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 패키징 없이도 에디터 클라이언트에서 실제 백엔드의 오픈 매칭·랜덤 매칭을 거쳐 경기에 입장한다. 경기 맵만 강제로 여는 우회가 아니다.
- 최신 백엔드 코드로 서버 도우미의 백엔드를 재시작한다. 로컬 MySQL·운영 자격 설정은 기존 도우미 설정을 사용한다.
- Unreal Editor를 새 빌드로 실행하고, 클라이언트 환경설정의 서버 IP를 `127.0.0.1`로 설정한다. 백엔드 주소는 `http://127.0.0.1:5080`이다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`EditorLocalMatchTesting.md`](../EditorLocalMatchTesting.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

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

- 이미 운영자가 활성화한 서버 구성이 있다면 이를 덮어쓰지 않는다. 패키징 서버 대신 에디터 서버로 전환하려면 기존 경기를 종료하고 도우미에서 게임 서버 운영을 중지한 뒤 PIE를 다시 시작한다. 기본 로컬 테스트 동시 경기 수는 1개, 게임 서버 포트는 UDP 17777부터 사용한다.
- 백엔드가 꺼져 있거나 운영 자격이 없으면 안내 후 실패한다. 확인 요청과 설정 요청은 각각 5초 제한이며, 서버 프로세스의 준비도 기존 제한 시간 안에만 대기한다. 정지된 서버를 준비된 것처럼 위장하지 않는다.
- 마네킹 Z/X/C/Q/B는 직접 조작하지 않아도 사용 가능. 시점 선택 상태의 명령은 즉시 적용되고, 직접 조작 중 Z/X/C는 조작 해제 후 수행한다.
- 상태창을 `현재 상태`와 `명령 상태`로 분리한다. 직접 조작 중에는 현재 상태를 `직접 조작`으로 유지하고 별도의 명령 상태에서 정찰·추격·추종·정지를 보여준다. 상태 및 입력 안내에서 `직접 조작 해제 후…` 유형의 표기는 사용하지 않는다. 이동·직접 공격은 직접 조작 상태가 필요하다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 실제 세션의 방·매치·포트 및 역할 검증
- 게임 서버의 티켓 소비·접속 허가 검증
- 테스트 명령: `Tools/ProjectProject01FeatureChecks/Run-IsolatedIntegration.ps1 -EditorServerExecutable '<현재 엔진>/Binaries/Win64/UnrealEditor-Cmd.exe'`. 이 검사는 포트 23307 및 검증 전용 데이터 경로를 확인한 뒤에만 실행하고, 테스트 서버는 종료한다. 운영 DB는 사용하지 않는다.
- Editor C++ 빌드 및 백엔드·도우미 참조 검사 프로젝트 빌드 통과.
- `Saved/ReviewRemediation/CameraCommands-Final-20261021/index.json`: 13개 성공, 실패 0. 카메라 토글·실제 카메라 움직임·시점 선택 상태의 명령·튜토리얼 거리/명령 진행·역할별 PIE 진입 포함.
- `Saved/ReviewRemediation/CameraCommands-PracticeRuntime-20261021/index.json`: 실제 연습맵 런타임 1개 성공, 실패 0. AI 이동·명령·추종 재지정·생존자 상태·역할 전환 회귀 포함.
- `Saved/ReviewRemediation/MatchmakingElapsed-MannequinHUD-Verified-20261021/index.json`: 매칭 경과 시간·초/분 전환·폴링 사이 갱신·취소/재시작, 마네킹 현재 상태/명령 상태 분리 및 기본 정지·명령 권한 관련 4개 검사 성공. 매칭 대기 UI의 `1분 05초` 렌더링도 확인.
- `Saved/ReviewRemediation/EditorLocalMatch-Integration-Verified-20261021.log`: 검증 전용 MySQL과 HTTP API 회귀 성공. 실제 UnrealEditor-Cmd 서버를 오픈 매칭·랜덤 매칭에서 각각 실행하고 카운트다운·준비 heartbeat·역할 1:2·계정별 접속 티켓 발급까지 통과. 준비 이전 티켓 발급 거부도 확인.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 13개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 1개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 4개 검사 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 패키징 없이도 에디터 클라이언트에서 실제 백엔드의 오픈 매칭·랜덤 매칭을 거쳐 경기에 입장한다. 경기 맵만 강제로 여는 우회가 아니다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 랜덤 매칭 대기 표시는 남은 제한 시간이 아니라 시작 버튼을 누른 뒤의 경과 시간이다. `0분 59초 → 1분 00초`처럼 증가하고, 취소 후 새로 시작하면 초기화된다. 서버 폴링이나 PC 시계 변경으로 타이머가 되돌아가지 않으며 기존 서버의 180초 만료 안전장치는 유지한다.
- 패키징·게시·운영 DB 변경은 수행하지 않았다. 검사에서 만든 실제 에디터 서버 프로세스와 격리 MySQL은 종료했다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `ff573889` | 2026-10-21 | maru3172 | Manipulation improvement |

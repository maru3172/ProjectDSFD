# 전수 검토 개선 작업 현황 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`CodeReviewRemediation.md`](../CodeReviewRemediation.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 기준: `FullProjectCodeReview.md`의 F01~F30 및 별도 확인점 C01~C08. 시작일: 2026-10-18.
- 기간: 2026-10-19 ~ 2026-10-19 (문서 변경 커밋 3개 기준)
- 결과·검증: 운영 DB·계정·배포본은 변경하지 않는다. **완료(코드·회귀)**는 검토에서 지적한 결함의 코드 수정과 전용 자동 검사 통과를 뜻한다. 실제 EOS 통화/다중 PC/바이너리 맵의 출시 확인까지 완료했다는 뜻은 아니다. 해당 실환경 확인은 별도로 남기며, 코드 작업 완료와 한 상태로 섞지 않는다.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 기준: `FullProjectCodeReview.md`의 F01~F30 및 별도 확인점 C01~C08. 시작일: 2026-10-18.
- 기존 검토 문서는 당시의 근거로 보존한다. 이 문서에서 코드 수정, 자동 검사, 실환경 확인을 구분한다.
- 이 시점에는 F07/F26/F27과 F08이 남아 있었으며, 이후 최종 작업과 검사에서 완료했다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`CodeReviewRemediation.md`](../CodeReviewRemediation.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LevelTransitionTrigger.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LobbySubsystem.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SaveSubsystem.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01LobbySubsystem.h` (3)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01LoginWidget.h` (3)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01SaveSubsystem.h` (3)
- `ProjectProject01/Tools/ProjectProject01Backend/LobbyEndpoints.cs` (3)
- `ProjectProject01/Tools/ProjectProject01Backend/Program.cs` (3)
- `ProjectProject01/Tools/ProjectProject01BuildServerHelper/Program.cs` (3)
- `ProjectProject01/Tools/ProjectProject01FeatureChecks/DemandSessionChecks.cs` (3)
- `ProjectProject01/Tools/ProjectProject01FeatureChecks/Program.cs` (3)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 비동기 seamless 로딩 진행 중에는 30초가 지났다는 이유만으로 옛 월드의 캐릭터를 노출하지 않는다. 엔진 실패 통지는 엔진의 취소 처리가 끝난 다음 tick에 복구한다. 이후 최종 작업에서는 GameInstance 코어 감시와 120초 안전 종료/클라이언트 복귀 정책도 추가했다.
- 세 개의 **논리 저장소**는 유지한다. 각 저장소는 `_GenerationA`/`_GenerationB`에 번갈아 쓰며, 현재 가장 최신인 정상 세대를 다음 쓰기 대상으로 사용하지 않는다. revision은 시계가 뒤로 바뀌어도 증가한다. 새 파일은 길이와 본문 CRC를 통과한 뒤에만 Unreal 역직렬화 및 기존 상태 무결성/버전 검사를 받는다. 완성된 정상 세대만 선택하므로 중단된 다음 쓰기가 마지막 정상 저장을 덮어쓰지 않는다. 이는 단일 파일 renam
- 기존 primary/backup 및 v1~v6 저장은 읽고 보존한다. 최종 저장 형식은 v7(AI 상태 추가)이며 구버전 미리보기도 유지한다. 최근 플레이 시각은 `_Recent`에 따로 기록해 불러오기 때문에 월드 스냅샷을 재작성하지 않는다. 최근 시각 파일이 망가지면 저장 당시 시각으로 돌아갈 뿐 게임 상태를 잃지 않는다. 미리보기 데이터도 본문과 분리 검증해 이미지만 손상되면 게임 상태는 계속 읽는다. 삭제/새 게임은 해당 논리 저장소의 구버전/교대 세대/
- `Saved/ReviewRemediation/Dashboard-Final-20261019.log`: 대시보드 자체 검사 통과. 양 스트림 6000행·인자 공백·창 닫기 취소 검사 포함.
- Unreal Editor Development와 실제 클라이언트 Win64 Shipping 컴파일 모두 성공. Shipping 컴파일에서도 에디터/자동 검사 전용 코드가 제외되는 조건을 확인했다. Backend/Helper/Launcher/FeatureChecks 및 Dashboard/SmokeTest 빌드도 성공했다. Dashboard/SmokeTest에는 NuGet 취약성 피드 연결 실패 NU1900 경고가 있었으나 컴파일 실패는 아니다.
- 초기 실패 로그는 남겨두었다. 로컬 플레이어 fixture의 월드 연결, 기존 테스트의 클라이언트 단독 종료 가정, 대시보드 취소 후 출력 EOF/동기화 컨텍스트 대기는 수정 후 재실행했다. 최종 근거는 위 보고서다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- **최종 현황(2026-10-19): F01~F30 코드·회귀 30/30 완료. C01~C08도 정책·코드 보완 및 해당 자동 검사를 완료했다.** 아래 중간 기록은 작업 이력이며 현재 미완료 목록이 아니다. 실제 다중 PC/EOS/패키지 맵의 출시 검증은 별도다. 운영·복구 정책은 `ReviewRecoveryPolicies.md`를 참고한다.
- 운영 DB·계정·배포본은 변경하지 않는다. **완료(코드·회귀)**는 검토에서 지적한 결함의 코드 수정과 전용 자동 검사 통과를 뜻한다. 실제 EOS 통화/다중 PC/바이너리 맵의 출시 확인까지 완료했다는 뜻은 아니다. 해당 실환경 확인은 별도로 남기며, 코드 작업 완료와 한 상태로 섞지 않는다.
- 이전 중단 시 이미 반영된 수정도 대조했다. 위의 "수정 존재"는 코드가 있다는 뜻이지, 해당 결함의 실환경 검증까지 완료했다는 뜻은 아니다. 별도 확인점 C01~C08은 이번 30개 개선 범위의 완료 수에 합산하지 않는다.
- Backend/FeatureChecks/BuildServerHelper/ClientLauncher 빌드: 성공(경고 0, 오류 0). 운영 서비스 실행·게시 없음.
- 기본 FeatureChecks: 95개 검사 통과. 재개 다운로드 17개, 설치 rollback/디스크 저널 복구 출력도 통과.
- 격리 API/DB: 최종 `Saved/ReviewRemediation/Integration-Timestamps-Retry-20261019.log`. 운영 MySQL 3307이 아닌 전용 23307 및 `Saved/FeatureChecks/IsolatedMySql`만 사용, 종료 시 해당 테스트 인스턴스 종료.
- 격리 검사 결과: 운영 52 / 소셜 64 / 커뮤니티 36 / 수요 기반 경기 46 / 드레인 14 / 솔로 매칭 45 / API·DB 39 / 백업·복원 4개 통과. 실제 데디 프로세스는 fixture로 대체하므로 다중 PC 경기 증거는 아니다.
- Unreal Editor Development 빌드 성공. `Saved/ReviewRemediation/NativeBatch1/index.json`: 네이티브 15개 성공, 경고·실패·미실행 0. 방 티켓 계약, 2명 다음 맵 참가/기록, 접근성 및 기존 오디오·카메라 회귀 검사 포함.
- F06 추가분도 Editor Development 빌드 성공. 최종 `Saved/ReviewRemediation/NativeBatch2Final/index.json`: 네이티브 18개 성공, 경고·실패·미실행 0. 입력 잠금 중첩, pawn 시점·이동·충돌·숨김 복원, carryover 폐기, 타임라인 재개, 종료 후 부활 방지, 목적지 누락, 싱글 대기 시간 초과 복구 및 기존 타임라인 검사 포함.
- F06 초기 검사에서는 엔진 컨텍스트가 없는 fixture의 조회가 assert를 일으켰다. 컨텍스트 존재 검사를 추가하고, 카메라 시점 검사를 위한 실제 카메라 관리자도 구성해 재실행했다. 초기 실패 로그는 `NativeBatch2.log` / `NativeBatch2Retry.log`로 보존하며, 통과 근거는 위의 `NativeBatch2Final`이다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 95개 검사 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 4개 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 15개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 18개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 기준: `FullProjectCodeReview.md`의 F01~F30 및 별도 확인점 C01~C08. 시작일: 2026-10-18. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 3
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 중간 시점에서 F08/F09/F10/F11/F12/F16/F28/F29/F30의 남은 구현·전용 검사를 마무리했다. 이전 회귀 근거까지 확인된 F02/F03/F04/F05/F13/F14/F15/F22도 코드 완료로 구분했다. 당시 코드·회귀 완료 17개 / 남은 13개였고, 이후 최종 작업에서 남은 항목도 완료했다.
- 완료 범위는 위 결함의 **정책·소스·자동 회귀**다. 실제 EOS 음성 통신/다중 PC/바이너리 맵 이동/장시간 부하·CPU 프로파일은 출시 검증으로 남는다. Cook·패키징·업데이트 게시·운영 DB 변경은 수행하지 않았다. 실제 크래시도 의도적으로 발생시키지 않았다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `21fba4d8` | 2026-10-19 | maru3172 | Fix missing improvements |
| `395f923b` | 2026-10-19 | maru3172 | Improvement Work Phase 1 |
| `507e11ce` | 2026-10-19 | maru3172 | Enhanced VFX and SFX direction |

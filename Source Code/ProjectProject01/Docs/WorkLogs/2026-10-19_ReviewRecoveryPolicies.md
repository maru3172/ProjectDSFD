# 전수 검토 후 복구·운영 정책 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`ReviewRecoveryPolicies.md`](../ReviewRecoveryPolicies.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 설문 응답은 요청 당시 로그인 세대와 월드가 유지되는 경우에만 UI로 전달한다. 로그아웃/계정 교체/레벨 변경 뒤의 응답은 폐기한다. 역할 가이드 저장이 실패하면 같은 로그인에서 안내를 반복하지 않고, 다음 로그인에서 서버가 반환한 확인 상태에 따라 다시 표시한다. 오래된 가이드 요청 완료가 새 요청을 지우지 못한다.
- 기간: 2026-10-19 ~ 2026-10-19 (문서 변경 커밋 1개 기준)
- 결과·검증: 실제 시작을 확정하는 트랜잭션에서 `authoritative_matches`/`authoritative_match_players`에 경기 당시 방 이름·계정·역할을 남긴다. 방 복귀나 방 삭제로 이 원장이 사라지지 않는다. 클라이언트의 전원 복귀 플래그만으로는 진행 경기의 역할/매치 ID를 초기화할 수 없다. 서버 비밀키로 승인된 경기 종료 또는 결과 제출이 있어야 정상 대기방 초기화가 가능하다. 서버 장애로 경기를 폐기하는 기존 운영 정리는 별도이며 성공 결과
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 설문 응답은 요청 당시 로그인 세대와 월드가 유지되는 경우에만 UI로 전달한다. 로그아웃/계정 교체/레벨 변경 뒤의 응답은 폐기한다. 역할 가이드 저장이 실패하면 같은 로그인에서 안내를 반복하지 않고, 다음 로그인에서 서버가 반환한 확인 상태에 따라 다시 표시한다. 오래된 가이드 요청 완료가 새 요청을 지우지 못한다.
- 인증 예산은 공유 IP당 분당 600회이며 계정별 실패 잠금은 DB에서 별도로 적용한다. 인증된 로비/티켓 예산은 로그인 세션 digest로 분리(각 600/30회)하고, 별도 공유 IP 남용 상한은 기본 6000회다. `Security:SharedIpRequestsPerMinute`로 600~60000 범위 조정이 가능하다. 토큰 원문을 partition이나 로그에 남기지 않는다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`ReviewRecoveryPolicies.md`](../ReviewRecoveryPolicies.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MannequinAIController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerHeartbeatComponent.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01AuthSubsystem.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CheckpointAI.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CheckpointAI.h` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CrashFiles.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CrashFiles.h` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LevelTransitionTrigger.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LobbySubsystem.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이전 프로세스의 크래시 마커에 아직 네이티브 증거가 없으면 최초 발견 시각을 파일에 기록하고 120초 기다린다. 빠른 재실행을 반복해도 유예가 무한히 갱신되지 않는다. 유예 중에는 업로드하지 않으며, 끝까지 증거가 없으면 오탐 제외한다. PIE/ensure 제외와 민감정보 제거는 유지한다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 데디는 전송 전에 결과를 `Saved/MatchResultOutbox`에 기록한다. 운영 도우미로 시작한 세션은 프로젝트 공용 Saved 경로를 전달받는다. 비밀키·로그인 토큰은 이 파일에 기록하지 않는다. 본문/빌드 계약과 체크섬만 보관한다. 현재 프로세스의 HTTP 요청은 5초 제한, 최대 8회, 지수 간격(최대 30초)이다. 실패해도 파일은 남아 백엔드 복구 worker가 30초 간격으로 다시 시도한다. 서버 프로세스가 먼저 종료되거나 백엔드가 재시작되어도
- 트리거의 누락/uncooked 목적지는 이동 전에 거부한다. 이동 거부 및 엔진 실패 통지는 입력·시점·pawn·carryover·타임라인을 복구한다. 30초 동안 월드 교체가 전혀 없다면 원래 상태로 복구한다. 엔진이 실제 비동기 seamless 로딩을 소유한 동안에는 옛 캐릭터를 강제로 드러내지 않는다. 최대 120초 유예 후 해당 경기를 닫고 클라이언트를 메인 메뉴로 돌려보낸다. 패키징된 데디 프로세스만 종료하며 에디터 전체를 종료하지 않는다. GameIn
- 데이터 fingerprint가 같으면 그대로 불러오고 조정값만 달라졌다면 안내 문구와 함께 불러온다. 콘텐츠 구조상 호환되지 않는 기존 fingerprint는 `DefaultGame.ini`의 `[ProjectProject01.CheckpointCompatibility]` 아래 `+BlockedAppliedVersions=...`로 명시한다. 호환되지 않는 저장은 삭제/자동 덮어쓰기하지 않는다. 미래 schema 및 무결성 실패는 기존 정책대로 거부한다. 액터 
- 네트워크 키/claim은 GameInstance별이다. 엔진의 전역 암호화 delegate는 실제 callback의 NetConnection 또는 PendingNetGame에 연결된 월드 컨텍스트로 전달한다. 마지막에 초기화된 PIE 계정을 추측해서 사용하지 않는다. 소유자를 찾지 못하면 인증 실패 처리한다. 네트워크 실패 역시 해당 GameInstance의 월드만 처리한다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 실제 시작을 확정하는 트랜잭션에서 `authoritative_matches`/`authoritative_match_players`에 경기 당시 방 이름·계정·역할을 남긴다. 방 복귀나 방 삭제로 이 원장이 사라지지 않는다. 클라이언트의 전원 복귀 플래그만으로는 진행 경기의 역할/매치 ID를 초기화할 수 없다. 서버 비밀키로 승인된 경기 종료 또는 결과 제출이 있어야 정상 대기방 초기화가 가능하다. 서버 장애로 경기를 폐기하는 기존 운영 정리는 별도이며 성공 결과
- 성공 ACK를 받은 파일만 제거한다. 400/401/403/409/426 등 영구 거부는 `.rejected`, 손상된 파일은 `.corrupt`로 보존해 원인 확인을 가능하게 한다. 종료 승인 파일도 같은 큐를 사용한다. OS 파일 핸들 잠금으로 중복 처리를 제한하고, 서버 DB의 멱등 결과 검증으로 중복 HTTP 요청을 안전하게 수용한다. 기존 검증 점수와 다른 값으로 덮어쓰는 재전송은 409다. 복구 worker는 이 PC의 실제 IP·수신 포트에 해당하는
- 미검증 점수 등록 옵션의 생성 기본값은 false다. 예전 설정이 true여도 Production에서는 허용하지 않는다. Development에서 명시적으로 허용한 순수 loopback 요청만 기존 테스트 옵션을 사용할 수 있고 프록시 헤더가 있으면 거부한다. 공개 HTTPS 프록시는 클라이언트가 보낸 Forwarded/X-Forwarded-* 헤더를 그대로 전달하지 말고 신뢰하는 프록시에서 덮어써야 한다. MySQL 외부 공개는 필요하지 않다.
- 스모크는 운영 DB에서 실행하지 않는다. 이번 통합 검사는 전용 23307/격리 데이터 폴더를 확인한 후에만 실행했고 운영 3307은 변경하지 않았다. 합성 결과를 쓰는 스모크 통과는 실제 UE 게임플레이/EOS/다중 PC 통과가 아니다. 게시 검사는 순위 top 50이 아닌 현재 계정의 정확한 MatchId 영수증으로 수행한다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

**정량 검증 미완료** — 원 문서에 측정 기준값·측정값이 없다.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 설문 응답은 요청 당시 로그인 세대와 월드가 유지되는 경우에만 UI로 전달한다. 로그아웃/계정 교체/레벨 변경 뒤의 응답은 폐기한다. 역할 가이드 저장이 실패하면 같은 로그인에서 안내를 반복하지 않고, 다음 로그인에 | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- v7은 싱글 AI의 인스턴스별 정찰/분산 이동·대상·명령·블랙보드·남은 Wait 상태를 명시적으로 저장한다. 이전 월드의 UObject 포인터나 NavMesh 경로 핸들을 저장하지 않는다. 현재 월드의 액터로 참조를 다시 연결하고 이동 경로는 재계산한다. 멀티 AI 명령에는 이 저장 경로를 적용하지 않는다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `21fba4d8` | 2026-10-19 | maru3172 | Fix missing improvements |

# 오픈매칭 커스텀 규칙 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`CustomOpenMatchRules.md`](../CustomOpenMatchRules.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 위치: 방장 `방 관리 → 방 관리 → 커스텀 규칙`.
- 기간: 2026-10-26 ~ 2026-11-06 (문서 변경 커밋 2개 기준)
- 결과·검증: 2026-10-26 최종 확인: .NET·Unreal 빌드 성공, 기능 검사 194개 통과, 분리 DB의 신규 커스텀 규칙 검사 30개와 기존 통합·복구 검사 통과. 최종 Unreal 검사 3개(규칙 런타임·화면·리플레이) 모두 경고·실패 없이 통과했다. 결과 화면·맵 carryover 회귀 검사도 통과했다. 1280×720 및 800×600 화면을 직접 확인했다. 서로 다른 PC에서의 실제 배포 실행은 별도 확인 대상이다.
- 현재 상태: 원 문서상 남은 항목 기재 없음(미확인)

## 요청·목표·범위 (9-2)

- 위치: 방장 `방 관리 → 방 관리 → 커스텀 규칙`.
- 데스카운트 1~20, 마네킹 1~10, 제한 시간 0~120분(0은 무제한).
- `사용자 지정`을 해제하고 적용하면 맵의 기본 경기 규칙을 사용한다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`CustomOpenMatchRules.md`](../CustomOpenMatchRules.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCustomRoomRulesWidget.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCustomRulesTests.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LobbySubsystem.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestGameMode.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestPlayerController.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectCustomMatchRules.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectCustomRoomRulesWidget.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01LobbySubsystem.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01LoginWidget.h` (2)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- `room_custom_rules`는 방 설정, `room_rule_presets`는 계정별 프리셋이다. 경기 확정 트랜잭션에서 `match_custom_rules`에 불변 스냅샷을 만든다. 방 삭제는 과거 경기 스냅샷을 지우지 않는다.
- `CustomRoomRulesChecks`: 실제 HTTP와 분리된 MySQL에서 권한, 범위, 준비 해제, 프리셋, 경기 확정·티켓, 보상 제외, 스냅샷 불변성 검사.
- 운영 DB 변경·운영 서버 재시작·패키징은 수행하지 않는다. 백엔드의 다음 정상 시작 시 추가 테이블이 생성된다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 검증된 일회용 입장 티켓 소비 응답으로만 데디케이티드 서버에 전달한다. 클라이언트 URL의 수치는 사용하지 않는다. 첫 입장에서 고정한 규칙은 재접속·관전자 입장에서도 바꿀 수 없으며 층 전환 시 carryover로 전달한다.
- 커스텀 경기 결과는 결과 화면·리플레이를 위해 검증 보관하되 공식 리더보드·개인 전적·레이팅·경험치·포인트·미션·경기 업적·시즌 조사 보상에서 제외한다. 통계 기반 봇 경험치의 공식 공유 풀에도 반영하지 않는다. 일반 오픈매칭·랜덤매칭·봇전의 기본 규칙과 집계는 유지한다.
- 2026-10-26 최종 확인: .NET·Unreal 빌드 성공, 기능 검사 194개 통과, 분리 DB의 신규 커스텀 규칙 검사 30개와 기존 통합·복구 검사 통과. 최종 Unreal 검사 3개(규칙 런타임·화면·리플레이) 모두 경고·실패 없이 통과했다. 결과 화면·맵 carryover 회귀 검사도 통과했다. 1280×720 및 800×600 화면을 직접 확인했다. 서로 다른 PC에서의 실제 배포 실행은 별도 확인 대상이다.
- 로그: `Saved/FeatureChecks/CustomRoomRulesToolsBuild.log`, `CustomRoomRulesGameBuild.log`, `CustomRoomRulesUnitChecks.log`, `CustomRoomRulesIntegration.log`, `CustomRoomRulesFinalQA/index.json`. 화면: `Saved/CustomRulesUIQA/Rules_1280x720.png`, `Rules_800x600.png`
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 194개 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 위치: 방장 `방 관리 → 방 관리 → 커스텀 규칙`. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
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
| `342421a` | 2026-11-06 | maru3172 | MiniGame 3 |
| `8583fef` | 2026-10-26 | maru3172 | Bot Balance |

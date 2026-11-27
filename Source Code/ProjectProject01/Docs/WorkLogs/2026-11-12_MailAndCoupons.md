# 우편함과 쿠폰 코드 (설계, 2026-11-11) — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`MailAndCoupons.md`](../MailAndCoupons.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 로비 아래쪽 휴대폰 버튼 옆 **우편함** 버튼. 안 받은 우편이 있으면 빨간 점.
- 기간: 2026-11-12 ~ 2026-11-12 (문서 변경 커밋 1개 기준)
- 결과·검증: 원 문서에 검증 기록 없음
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 로비 아래쪽 휴대폰 버튼 옆 **우편함** 버튼. 안 받은 우편이 있으면 빨간 점.
- 우편함 위쪽 **쿠폰 입력** 칸. 띄어쓰기·하이픈·대소문자는 무시한다. 결과: "우편함으로 보냈습니다", "이미 사용한 쿠폰입니다", "없는 쿠폰이거나 기간이 지났습니다".
- 잘못된 코드를 1시간에 10번 넘게 넣으면 잠시 막는다(코드 맞히기 방지).
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`MailAndCoupons.md`](../MailAndCoupons.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultEngine.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MannequinAICharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MannequinReactiveLight.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCaseRecordWidgets.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDUITheme.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDUIThemeTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectGamepad.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectLightVariation.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectMailboxTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectMailboxWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 원 문서에 결정 서술 없음(미확인)
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
| 기능 구현 | 로비 아래쪽 휴대폰 버튼 옆 **우편함** 버튼. 안 받은 우편이 있으면 빨간 점. | 원 문서에 수용 기준으로 명시되지 않음 | 검증 기록 없음 | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 우편 목록(최신순): 제목, 남은 기한, 보상. 하나씩 받기, 모두 받기, 받은 우편 지우기.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `51763cd8` | 2026-11-12 | maru3172 | GamePad |

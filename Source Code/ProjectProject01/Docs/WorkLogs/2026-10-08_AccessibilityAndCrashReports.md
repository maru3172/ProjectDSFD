# 접근성 설정과 크래시 보고서 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`AccessibilityAndCrashReports.md`](../AccessibilityAndCrashReports.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 타이틀 화면과 ESC 게임 메뉴의 `환경설정`에서 다음 값을 변경하고 `적용`을 누르면
- 기간: 2026-10-08 ~ 2026-10-15 (문서 변경 커밋 2개 기준)
- 결과·검증: 실패하면 30초 간격으로 재시도합니다. 모든 선택된 첨부파일 업로드가 성공해야 Pending JSON을 삭제합니다.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 타이틀 화면과 ESC 게임 메뉴의 `환경설정`에서 다음 값을 변경하고 `적용`을 누르면
- `GameUserSettings.ini`에 로컬 사용자별로 저장됩니다.
- 색상 외 형태·문구 정보 표시
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`AccessibilityAndCrashReports.md`](../AccessibilityAndCrashReports.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01GameInstance.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01LoginWidget.h` (2)
- `ProjectProject01/Tools/ProjectProject01Backend/Database/001_CreateAuthSchema.sql` (2)
- `ProjectProject01/Tools/ProjectProject01Backend/Program.cs` (2)
- `ProjectProject01/Source/ProjectProject01/Private/MannequinAICharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CrashFiles.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CrashFiles.h` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01DiagnosticsSubsystem.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 실제 크래시 증거가 있는 실행만 `Pending-...json`으로 전환해 `/api/crash-reports`로 전송합니다.
- 이전 PIE 기록과 크래시 증거가 없는 기록은 `Saved/CrashReports/Excluded`에 보관합니다.
- 단순 강제 종료와 ensure는 확인된 크래시로 집계하지 않습니다. 서버의 이전 PIE 기록도 관리자 목록에서 제외합니다.
- 서버 도우미 → 서버 운영 → `크래시 보고서 보기`에서 보고서를 선택해 ZIP을 다운로드합니다.
- 표준 문자열로 변환합니다. 이 변환 누락으로 발생하던 크래시 목록 HTTP 500을 수정했습니다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 저장된 설문을 선택하고 `선택 설문 불러오기 / 수정`을 누르면 문항을 편집기에 불러옵니다.
- 객관식은 체크 목록으로 표시합니다. 단일 선택에서는 마지막 선택만 남고,
- 복수 선택에서는 최대 개수를 초과할 때 오래된 선택부터 해제합니다. 최대 개수 0은 모든 선택지를 뜻합니다.
- 로그아웃 후 타이틀로 복귀합니다. 로그인 취소·강제 로그아웃 시 자동 열기 요청은 취소됩니다.
- 로그인 화면에서 취소 후 일반 로그인하면 설문이 자동으로 열리지 않아야 합니다. 설문을 열어둔 채
- `Tools/ProjectProject01FeatureChecks`는 기존 JSON 호환·선택 제한·함정문항·비밀값 제거와
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 크래시 요약 JSON의 기존 필드는 유지합니다. JSON 전송 후 해당 크래시 폴더의 `.dmp`, 전체 `.log`,
- 실패하면 30초 간격으로 재시도합니다. 모든 선택된 첨부파일 업로드가 성공해야 Pending JSON을 삭제합니다.
- 설문을 한 번 자동으로 엽니다. 제출 성공 시 `설문에 감사드립니다.` 팝업이 표시되고 확인을 누르면
- 타이틀의 설문 버튼 → 로그인 → 설문 자동 열기를 확인합니다. 제출 성공 후 감사 팝업의 확인을
- `Saved/FeatureChecks/IsolatedMySql`, 포트 23307을 사용해 설문 CRUD·보류 검토·전체 통계·덤프 업로드/다운로드를 검증합니다.
- 검증이 끝나면 테스트 서버는 종료됩니다. Unreal 자동화 테스트는
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

**정량 검증 미완료** — 원 문서에 측정 기준값·측정값이 없다.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 타이틀 화면과 ESC 게임 메뉴의 `환경설정`에서 다음 값을 변경하고 `적용`을 누르면 | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 2
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 정상 종료 시 파일은 삭제됩니다. 다음 실행에서 남은 마커와 Unreal의
- 함정문항 정답은 서버에만 저장하며 클라이언트에 전달하지 않습니다. 틀린 응답은 보류 상태로 저장되어
- 문항 통계에서 빠집니다. `결과/통계 보기` → `보류함`에서 전체 답변을 검토한 뒤 반영 또는 삭제합니다.
- `전체 설문 통계`는 모든 설문의 응답 건수·반영·보류·고유 참여 계정을 집계하며,
- 전체 설문 통계에만 `막대 그래프`(응답 건수 비교), `비율 · 파이`(설문별 및 반영/보류 비율),

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `acf11f76` | 2026-10-15 | maru3172 | Crash report collection |
| `46cc6949` | 2026-10-08 | maru3172 | CrashReport Upload |

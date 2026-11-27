# 성능·보안 정리 (2026-10-22) — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`PerformanceAndSecurityCleanup.md`](../PerformanceAndSecurityCleanup.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: `Config/DefaultEngine.ini` 끝에 커밋돼 있던 `[EOSVoiceChat] ClientSecret` 평문을 제거했다.
- 기간: 2026-10-22 ~ 2026-10-22 (문서 변경 커밋 1개 기준)
- 결과·검증: 2026-10-22 최종 실행: Editor Win64 Development 빌드 성공, `Automation RunTests ProjectProject01`(-RenderOffscreen) 94/94 통과, `ProjectProject01FeatureChecks --packaging-checks` 32/32 통과.
- 현재 상태: 원 문서상 남은 항목 기재 없음(미확인)

## 요청·목표·범위 (9-2)

- `Config/DefaultEngine.ini` 끝에 커밋돼 있던 `[EOSVoiceChat] ClientSecret` 평문을 제거했다.
- 런타임은 `ProjectProject01VoiceChatSubsystem::ApplyRuntimeEosConfiguration()`이 ini 값이 비어 있으면 환경변수 `PROJECTPROJECT01_EOS_GAME_CLIENT_SECRET`을 엔진 설정에 주입한다. `FEOSVoiceChat::Initialize`(UE 5.8 `EOSVoiceChat.cpp`)가 이 값을 초기화 시점에 읽는다.
- 에디터/개발 PC는 `Tools/ProjectProject01Backend/ConfigureProjectProject01EosVoice.ps1` 실행 후 에디터를 재시작해야 음성이 동작한다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`PerformanceAndSecurityCleanup.md`](../PerformanceAndSecurityCleanup.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultEngine.ini` (1)
- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/BTService_MannequinAI.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/HelperRearGuardCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerFeedbackWidgets.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDUIThemeTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01TutorialTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectResultBlackoutTests.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 디버그 표시(`PlayerCharacter::DrawAIRangeDebug`, `HelperRearGuardCharacter::DrawGuardDebug`)는 `ENABLE_DRAW_DEBUG`와 비전용 서버에서만 계산한다. 개발 빌드의 표시는 기존과 같다.
- `ProjectProject01Editor.Build.cs`에 `RenderCore`를 추가했다(`ProjectDFSDTitlePIETests.cpp`의 `FlushRenderingCommands` 링크 오류).
- `ProjectProject01.Multiplayer.TutorialFlow`는 `-nullrhi`에서 스크린샷 구간만 건너뛴다(이전에는 null 렌더 타깃 접근으로 크래시).
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 원 문서에 결정 서술 없음(미확인)
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 2026-10-22 최종 실행: Editor Win64 Development 빌드 성공, `Automation RunTests ProjectProject01`(-RenderOffscreen) 94/94 통과, `ProjectProject01FeatureChecks --packaging-checks` 32/32 통과.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

**정량 검증 미완료** — 원 문서에 측정 기준값·측정값이 없다.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | `Config/DefaultEngine.ini` 끝에 커밋돼 있던 `[EOSVoiceChat] ClientSecret` 평문을 제거했다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
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
| `3bb3ea6e` | 2026-10-22 | maru3172 | Performance Improvement |

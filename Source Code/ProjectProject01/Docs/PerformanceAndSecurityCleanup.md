# 성능·보안 정리 (2026-10-22)

## 보안
- `Config/DefaultEngine.ini` 끝에 커밋돼 있던 `[EOSVoiceChat] ClientSecret` 평문을 제거했다.
  - 런타임은 `ProjectProject01VoiceChatSubsystem::ApplyRuntimeEosConfiguration()`이 ini 값이 비어 있으면 환경변수 `PROJECTPROJECT01_EOS_GAME_CLIENT_SECRET`을 엔진 설정에 주입한다. `FEOSVoiceChat::Initialize`(UE 5.8 `EOSVoiceChat.cpp`)가 이 값을 초기화 시점에 읽는다.
  - 에디터/개발 PC는 `Tools/ProjectProject01Backend/ConfigureProjectProject01EosVoice.ps1` 실행 후 에디터를 재시작해야 음성이 동작한다.
  - 패키징은 BuildServerHelper가 기존처럼 임시 주입 후 원본을 복구한다.
  - 이미 원격 저장소에 올라간 키는 Epic Developer Portal에서 재발급을 권장한다.
- 어디에서도 읽지 않던 `bAllowInsecureDevelopmentCredentials` 설정/프로퍼티를 제거했다. 엔진의 insecure 토큰 경로는 `#if !UE_BUILD_SHIPPING` 콘솔 명령 전용이며 프로젝트는 백엔드 `/api/voice/token`만 사용한다.

## 성능 (동작 변경 없음)
- `BTService_MannequinAI`: 매 틱 검사점마다 반복하던 `GetAllActorsOfClass(AMannequinAICharacter)`와 무시 목록 구성을 틱당 1회로 줄였다(마네킹 1기당 최대 15회 → 1회).
- `HelperRearGuardCharacter`: `CanObserveActor`/`CalculateGuardViewDirection`의 임시 배열 수집을 `TActorIterator` 순회로 바꿨다.
- 디버그 표시(`PlayerCharacter::DrawAIRangeDebug`, `HelperRearGuardCharacter::DrawGuardDebug`)는 `ENABLE_DRAW_DEBUG`와 비전용 서버에서만 계산한다. 개발 빌드의 표시는 기존과 같다.

## 정리
- `APlayerCharacter::ApplyPlayerTuning`과 `ClientApplyPlayerTuning_Implementation`의 중복 본문을 `ApplyPlayerTuningValues`로 합쳤다.
- `ProjectProject01Editor.Build.cs`에 `RenderCore`를 추가했다(`ProjectDFSDTitlePIETests.cpp`의 `FlushRenderingCommands` 링크 오류).

## 테스트
- `ProjectProject01.Multiplayer.TutorialFlow`는 `-nullrhi`에서 스크린샷 구간만 건너뛴다(이전에는 null 렌더 타깃 접근으로 크래시).
- 2026-10-22 최종 실행: Editor Win64 Development 빌드 성공, `Automation RunTests ProjectProject01`(-RenderOffscreen) 94/94 통과, `ProjectProject01FeatureChecks --packaging-checks` 32/32 통과.
- `ProjectProject01.Editor.TitlePresentation`: 뷰포트 캡처를 `FViewport::Draw` 내부 `ProcessScreenShots` → `UGameViewportClient::OnScreenshotCaptured` 경로로 바꿨다. `-RenderOffscreen`에서는 PIE 창이 640x8, 뷰포트가 0x0으로 배치되지 않아 읽을 프레임이 없으므로, 그 조건에서만 픽셀 읽기를 건너뛰고 `SKIPPED ...` 정보를 남긴다. 나머지 타이틀 검사와 일반(보이는 창) 실행의 픽셀 검사는 그대로다.

## EOS 패키징 비밀키 (BuildServerHelper)
- 클라이언트 패키징은 더 이상 Git 추적 `DefaultEngine.ini`를 고치지 않는다. 패키징 동안만 `Config/GeneratedEngine.ini`(UE 5.8 ProjectGenerated 계층, `.gitignore` 대상)에 `[EOSVoiceChat] ClientSecret`을 쓰고, 끝나면 삭제(또는 이전 내용 복원)한다.
- `DefaultEngine.ini`에 평문 `ClientSecret=`이 있으면 패키징을 차단한다. `DefaultGame.ini` `[Staging] +AllowedConfigFiles=ProjectDFSD/Config/GeneratedEngine.ini`로 스테이징을 명시했다.

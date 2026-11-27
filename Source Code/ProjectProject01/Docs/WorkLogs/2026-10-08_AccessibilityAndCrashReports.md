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

## 대화 기록 확인 (예전 세션 대화에서 소급, 2026-11-27)

- 출처: 이 문서를 쓰거나 고친 세션 1개(Claude 0, Codex 1). 아래는 그 구간의 **사용자 발언 원문 발췌**와 **작업 세션이 당시 보고한 원인·실패·결정 발췌**다. 작업 세션의 보고는 그 세션의 주장이며, 이 기록을 쓰며 다시 검증하지 않았다.
- 이 절이 위 표의 "미확인"을 보충한다. 요청·의도는 사용자 발언, 시행착오·원인은 작업 세션 보고 칸을 본다.

### Codex 세션 `01a0acb2` (2026-10-08 13:41 ~ 2026-10-15 18:39 KST, 문서 편집 10회)

| 시각 (KST) | 사용자 발언 (원문 발췌) |
|---|---|
| 2026-10-08 13:41 | 패키징 했는데 싱글 멀티가 각각 자기가 넘어가야할 레벨로 안 넘어간다 |
| 2026-10-08 13:46 | 그럼 login level -> lobby level -> 멀티겜 -> 다시 전환 혹은 타이틀, 싱글도 마찬가지 이런것들도 제대로...해주는거지? |
| 2026-10-08 13:51 | 그 인게임에서 환경설정 변경하고 적용 누르면 왜 마우스가 사라지는거임? |
| 2026-10-08 14:01 | [14:00:28] > powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01Backend\ProjectProject01MySql.ps1" -Action Initialize [14:00:28] [BUILD:ERR] The isolated instance is already provisioned at C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01 [14:00:28] [BUILD:ERR] |
| 2026-10-08 14:07 | > powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01Backend\ProjectProject01MySql.ps1" -Action Initialize [14:06:54] [BUILD] The isolated instance is already provisioned at C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01Backend\LocalMySql\Data. Starting it w |
| 2026-10-08 19:15 | 20. 접근성 설정 보완 - 자막 - 색상 외에도 형태·문구로 정보 표시 - 색약 대응 - 화면 번쩍임 감소 - 화면 일렁임 감소 - 화면 흔들림 감소 - UI 가독성 보완 - 기존 설정 저장·적용 시스템과 연결 G. 안정성 및 운영 21. 크래시 보고서 수집 - 클라이언트 비정상 종료 기록 - 데디케이티드 서버 비정상 종료 기록 - 빌드 버전 - 맵과 게임 모드 - 서버·클라이언트 역할 - 마지막 로그 경로 - 진단 TestRunId 또는 실행 식별자 연결 - 인증 토큰·비밀번호 등 민감정보 제거 이제 이걸 해볼까 |
| 2026-10-08 19:49 | 혹시 테스트는 어떻게 하지? |
| 2026-10-08 19:59 | [19:57:02] > C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat BuildCookRun -project="C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\ProjectProject01.uproject" -noP4 -platform=Win64 -clientconfig=Shipping -build -cook -stage -pak -archive -archivedirectory="C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Builds\Client" -utf8output [19: |
| 2026-10-08 20:04 | # Files mentioned by the user: ## Active-19336-a1c453e7-4a73-103a-eb52-49a920dc1461.json: C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Saved/CrashReports/Active-19336-a1c453e7-4a73-103a-eb52-49a920dc1461.json ## Active-19336-98502bd3-4f70-0f36-90a8-9bb3c2b49171.json: C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Saved/CrashReports/Active-19336-98502bd |
| 2026-10-08 20:13 | 내가 패키징해서 게임 플레이를 켰는데 그 크래시 리포트는 어디서 뜨는거야? C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Saved\CrashReports 여기 안뜨던데 |
| 2026-10-08 20:16 | 22. 전체 게임 흐름 스모크 테스트 검사 흐름: 로그인 → 방 생성 → 3명 참가 → 준비 → 경기 시작 → 역할 배정 → 붙잡힘·구출 또는 탈출 → 승패 결정 → 경기 종료 → 결과 등록 → 기존 방 복귀 추가 사항: - 단계별 제한 시간 - 실패 단계 기록 - 원본 로그 연결 - 기존 진단 프로그램과 연동 - 정상 흐름이 최소한 동작하는지 빠르게 확인 23. 클라이언트 버전 불일치 차단 검사 대상: - 클라이언트 빌드 버전 - 데디케이티드 서버 버전 - 백엔드 API 버전 - 게임 데이터 버전 - 네트워크 프로토콜 버전 처리: - 호환되지 않는 클라이언트 접속 차단 - 필요한 업데이트 안내 - 서로 다른 버전이 같은 방에 들어가는  |
| 2026-10-08 21:02 | 멈췄던 부분부터 계속 진행하자 끝까지 |
| 2026-10-08 21:36 | [21:35:39] [BACKEND] Content root path: C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01Backend [21:35:40] > dotnet run --project "C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01SmokeTest\ProjectProject01SmokeTest.csproj" -- --project-root "C:\DSFD\ProjectDSFD\Source Code\ProjectProject01" [21:35:42] [BUILD] C:\ |
| 2026-10-08 21:37 | 클라이언트 버전 불일치 할때 차단은 음...있긴 한데 확인할순 없겠네 |

| 시각 (KST) | 작업 세션 보고 (원인·실패·결정, 발췌) |
|---|---|
| 2026-10-08 13:41 | 패키징판에서만 두 버튼 모두 맵 전환이 안 된다면 가장 가능성이 큰 원인은 목적 맵이 쿠킹 목록에 포함되지 않았거나, 에디터에서만 통하는 짧은 맵 이름을 사용한 경우입니다. 타이틀 버튼의 실제 경로와 패키징 맵 목록을 함께 확인해서 원인을 잡겠습니다. |
| 2026-10-08 13:43 | 원인을 찾았습니다. 필수 화면·게임 맵을 패키징 목록에 명시하고 다시 쿠킹해 검증하겠습니다. |
| 2026-10-08 13:47 | ```text 싱글: TitleLevel → LevelTest01 → TitleLevel ↘ 체크포인트 저장·불러오기 멀티: TitleLevel → LoginLevel → LobbyLevel → MultiplayTest → LobbyLevel / LeaderBoardTest / TitleLevel ``` 검증 결과: - 재쿠킹: 성공 - 클라이언트 Shipping 패키징: 성공 - 오류: 0 - Cooked Asset Registry에서 위 여섯 맵 모두 존재 확인 - 최종 결과: `BUILD SUCCESSFUL` 수정 사항은 [DefaultGame.ini](</C: |
| 2026-10-08 13:52 | 원인은 설정값 저장 자체보다, `ApplySettings`가 화면 모드/해상도와 입력 설정을 갱신한 뒤 현재 UI의 마우스 표시·포커스를 다시 잡아주지 않는 데 있습니다. |
| 2026-10-08 13:53 | 확인 결과 정확한 원인은 키 재설정을 반영하는 함수가 플레이어 입력을 다시 초기화하면서 `GameOnly + 마우스 숨김`까지 함께 실행한 것이었습니다. 키 설정 갱신은 유지하되, 설정 창이 열려 있으면 즉시 `GameAndUI + 커서 표시`로 되돌리는 수정까지 넣었고 지금 빌드로 검증하겠습니다. |
| 2026-10-08 13:55 | 원인은 키 설정 적용 과정에서 플레이어 입력을 다시 초기화하며 `GameOnly` 모드로 전환하고 마우스를 숨겼기 때문입니다. |
| 2026-10-08 14:01 | 도우미 버튼이 `Initialize`만 호출하고 종료했는지 확인해 바로 원인을 잡겠습니다. |
| 2026-10-08 14:04 | 실제로 `127.0.0.1:3307` 시작 성공도 확인했습니다. |
| 2026-10-08 19:15 | 이번에는 기존 환경설정 저장 구조를 확장해 접근성 옵션을 실제 효과에 연결하고, 크래시 보고서는 클라이언트·서버 공통 메타데이터와 민감정보 제거, 로컬 보존 및 백엔드 수집 경로까지 이어지도록 구현하겠습니다. |
| 2026-10-08 19:19 | 이번에는 기존 설정 화면과 저장 구조에 접근성 항목을 붙이고, 크래시는 “종료 순간에 억지로 전송”하지 않고 안전하게 로컬 표식을 남긴 뒤 다음 실행 때 백엔드로 수집하는 방식으로 구현하겠습니다. |

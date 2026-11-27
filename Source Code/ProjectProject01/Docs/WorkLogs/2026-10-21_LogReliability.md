# 한글 로그·로비 포커스·결과 복구·EOS 틱 보완 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`LogReliability.md`](../LogReliability.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 로비 위젯은 초기화 시 `SetIsFocusable(true)`를 설정한다. 설정/설문에서 로비 위젯으로 포커스를 돌려주는 기존 흐름은 유지한다.
- 기간: 2026-10-21 ~ 2026-10-21 (문서 변경 커밋 1개 기준)
- 결과·검증: `ProjectDFSDEditor Win64 Development`: 빌드 성공.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 로비 위젯은 초기화 시 `SetIsFocusable(true)`를 설정한다. 설정/설문에서 로비 위젯으로 포커스를 돌려주는 기존 흐름은 유지한다.
- 2026-10-20 23:50 시작 로그에서 Zen은 약 37.225초 후 HTTP `Status: OK`가 됐다. 당시 C: 여유 공간은 약 215GB였다. 캐시 인덱스를 열고 정상 실행됐으며, 이 자료만으로 캐시 손상이나 긴 유지보수 작업이라고 단정할 근거는 없다. 에디터가 Zen 수명을 관리하므로 에디터/검사 프로세스를 자주 닫고 다시 실행하면 초기화도 반복된다. 이번 최종 검사에서는 Zen 준비가 약 3.715초였다.
- 도우미와 백엔드는 수정된 빌드로 다시 실행해야 새 한글 출력과 worker/틱 처리가 적용된다. 에디터는 다시 열면 새 DLL을 사용한다. 기존 배포 클라이언트·데디에는 추후 해당 실행 파일을 다시 빌드/패키징할 때 네이티브 수정이 반영된다. 도우미를 별도로 publish한 실행 파일을 사용한다면 그 publish본도 갱신해야 한다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`LogReliability.md`](../LogReliability.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDUITheme.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDUIThemeTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01ResultOutbox.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01ResultOutbox.h` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01ReviewRegressionTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01Tutorial.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- EOS는 같은 전용 소유 스레드에서 먼저 Tick을 호출하고, 명령 처리에 8건/약 2ms 상한을 둔다. Tick/명령 소요 시간을 포함해 다음 대기 시간을 계산해, 무제한 큐 처리와 고정 추가 10ms 대기로 인한 지연을 줄인다. 오류 로그·SDK 경고는 숨기거나 낮추지 않는다. [Epic의 주기적인 Tick 안내](https://dev.epicgames.com/docs/epic-online-services/eos-get-started/get-started-g
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 백엔드 `Program.cs`는 콘솔 출력을 UTF-8(BOM 없음)으로 지정한다. 도우미 `ProcessLogEncoding`은 dotnet 및 `-utf8output` 명령의 stdout/stderr를 UTF-8로 읽고, 하위 .NET CLI도 동일 인코딩으로 실행한다. 기존 언어 선택은 유지하며 레거시 PowerShell/네이티브 명령에 무조건 UTF-8을 적용하지 않는다. 이미 물음표로 손실된 과거 로그를 복원하는 변경은 아니다. [Microsoft CL
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 게임 인스턴스의 결과/경기 완료/기권 전송은 정식 GUID 경기 ID만 대상으로 한다. `LocalPIE-…` 결과 화면은 기존처럼 표시하지만 백엔드 전송과 정식 복구 저널 저장은 하지 않는다. 네이티브 outbox도 저장 전에 경기 ID를 검사한다. 실제 경기 참가·역할·서명 권한은 여전히 백엔드가 검증한다.
- OS 스케줄링 지연, 장시간 걸리는 개별 네이티브 호출, PC 전체 부하까지 이 루프가 선점하거나 보장할 수는 없다. 따라서 EOS 지연 경고가 절대 발생하지 않는다는 뜻은 아니다. 실제 다중 PC 음성 송수신은 별도 실기 검증 대상이다.
- `ProjectDFSDEditor Win64 Development`: 빌드 성공.
- `dotnet run --project Tools/ProjectProject01FeatureChecks/ProjectProject01FeatureChecks.csproj -- --log-reliability-checks`: 21개 통과. 실제 하위 프로세스 한국어 stdout/stderr 왕복, 과거 PIE 기록의 바이트 보존/HTTP 미발송, 정상 경기 재전송, EOS 요청 적체·명령 예외·취소 시나리오를 포함한다.
- 같은 검사 프로그램의 기본 전체 실행: 188개 통과. 실제 운영 DB나 EOS 자격 증명을 사용하지 않는다.
- Unreal 자동 검사: 29개 통과, 실패·미실행 0개. UITheme/Operations/Community/Frontend/Settings/Review/실제 타이틀 PIE 전환을 한 실행에 묶었다. `Saved/ReviewRemediation/LogReliability-20261020/index.json`에 결과가 있다. 실제 PIE 검사 1개는 기존 엔진/꺼진 백엔드 연결 경고를 동반했다. 로비 포커스 가능 설정, PIE 저널 저장 차단, 정상 저널 생성·
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 21개 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 188개 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 29개 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 로비 위젯은 초기화 시 `SetIsFocusable(true)`를 설정한다. 설정/설문에서 로비 위젯으로 포커스를 돌려주는 기존 흐름은 유지한다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 복구 worker는 체크섬·스키마를 확인한 과거 로컬 테스트 저널을 HTTP 없이 `.local-test`로 보존한다. 잘못된 경기 ID는 `.invalid`로 보존하며, 같은 보관본이 있으면 고유 이름을 사용해 덮어쓰지 않는다. 정상 경기의 재시도·서명·ACK 처리는 유지한다. **schema 1 JSON 구조와 payload/sha1/headers/body 내용은 바꾸지 않았다.** 이미 `.rejected`인 기존 파일은 재전송 대상이 아니므로 그대로 둔다
- 엔진 공용 캐시 삭제, Zen 비활성화, 백신/방화벽 예외 추가, 엔진 설정 변경은 하지 않았다. 패키징도 실행하지 않았다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `9ea88d02` | 2026-10-21 | maru3172 | Adjust UI Theme |

## 대화 기록 확인 (예전 세션 대화에서 소급, 2026-11-27)

- 출처: 이 문서를 쓰거나 고친 세션 1개(Claude 0, Codex 1). 아래는 그 구간의 **사용자 발언 원문 발췌**와 **작업 세션이 당시 보고한 원인·실패·결정 발췌**다. 작업 세션의 보고는 그 세션의 주장이며, 이 기록을 쓰며 다시 검증하지 않았다.
- 이 절이 위 표의 "미확인"을 보충한다. 요청·의도는 사용자 발언, 시행착오·원인은 작업 세션 보고 칸을 본다.

### Codex 세션 `01a0acb2` (2026-10-20 20:32 ~ 2026-10-21 00:02 KST, 문서 편집 1회)

| 시각 (KST) | 사용자 발언 (원문 발췌) |
|---|---|
| 2026-10-20 20:32 | 느낌이 비슷하지 않아... |
| 2026-10-20 20:51 | 방금 만든 타이틀 테마랑 똑같은 UI들을 다른 UI에 다 적용시켜보는거지? |
| 2026-10-20 23:33 | # Files pasted by the user: ## "서버가 로그인했습니다. 에디터에서 플레이 총 시작 시간 0.188초입니다. 위젯 ProjectProject01LobbyWidget_0은 포커스…": C:\Users\maru3\.codex/attachments/60402004-6b04-43f5-97fd-7902c077963e/붙여넣은 텍스트.txt Pasted text contains the user's request. ## My request: |
| 2026-10-20 23:49 | 한글 깨진건 고쳐주고 포커스 경고는 수정하고 로그에서 나온 해결해야할 문제는 해결해줘 |
| 2026-10-20 23:51 | 언리얼 젠 스토리지 서버를 실행하는 데 시간이 오래 걸리고 있습니다. 유지보수 작업을 수행하는 중일 수 있습니다. 계속 기다릴까요? 그리고 이건 요즘 왜이리 자주 뜸? |

| 시각 (KST) | 작업 세션 보고 (원인·실패·결정, 발췌) |
|---|---|
| 2026-10-20 20:49 | 기존 로그인·로비·친구·설문·음성 설정·저장소 관련 검사 17개는 통과했어. |
| 2026-10-20 21:04 | 빌드와 관련 검사 21개 통과했고, 재패키징은 하지 않았어. |
| 2026-10-20 23:33 | 첨부한 로그와 안에 적힌 요청을 확인해서 원인을 살펴볼게. |
| 2026-10-20 23:37 | 로그인 실패나 크래시 메시지는 아니지만, 포커스 설정은 별도 수정할 부분이 있어. |
| 2026-10-20 23:39 | 빌드·관련 검사 21개 통과했고, 재패키징은 안 했어. 로그인 실패나 크래시는 아니지만 수정할 부분이야. |
| 2026-10-20 23:50 | EOS 지연 경고도 처리 루프를 확인해서 손볼 원인이 있는지 살펴볼게. |
| 2026-10-20 23:51 | 내가 검증용 에디터를 여러 번 실행한 영향이 있는지도 살펴보고, 캐시는 임의로 지우지 않을게. |
| 2026-10-20 23:52 | 최근 에디터와 검증 실행이 잦았던 영향은 있지만, 정확히 어디서 37초가 소요됐는지는 추가 원인까지 단정할 수 없어. |
| 2026-10-21 00:00 | 새 로그 검사 21개와 기존 .NET 검사까지 **188개가 통과**했어. 이번 검증 실행에서는 Zen이 약 **3.7초 만에 정상 준비**됐어. |
| 2026-10-21 00:02 | - 결과 복구 HTTP 400: PIE 테스트 기록은 정식 전송·저장에서 제외했어. 빌드 성공, .NET 검사 188개·Unreal 검사 29개 통과했어. |

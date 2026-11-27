# ProjectProject01 UI 현지화 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`Localization.md`](../Localization.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 한국어를 기본 원문 언어로 사용하며 코드에서 생성하는 공통 UI는
- 기간: 2026-10-09 ~ 2026-10-09 (문서 변경 커밋 1개 기준)
- 결과·검증: 원 문서에 검증 기록 없음
- 현재 상태: 원 문서상 남은 항목 기재 없음(미확인)

## 요청·목표·범위 (9-2)

- 한국어를 기본 원문 언어로 사용하며 코드에서 생성하는 공통 UI는
- `Content/Localization/ProjectProject01UI.csv` 문자열 테이블을 거칩니다.
- 런타임 수치, 플레이어 이름과 서버가 반환한 동적 문구는 번역 대상 원문에 결합되어 표시됩니다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`Localization.md`](../Localization.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01AuthSubsystem.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01DiagnosticsSubsystem.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01Localization.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01PingMarker.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/ProjectProject01.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestPlayerController.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01AuthSubsystem.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01Localization.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01LoginWidget.h` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 버튼, 제목, 입력 안내, 상태 및 오류 문구는 이 표의 한국어 원문을 안정 키로 사용합니다.
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
| 기능 구현 | 한국어를 기본 원문 언어로 사용하며 코드에서 생성하는 공통 UI는 | 원 문서에 수용 기준으로 명시되지 않음 | 검증 기록 없음 | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
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
| `dcb6f234` | 2026-10-09 | maru3172 | Server Maintenance Notice |

## 대화 기록 확인 (예전 세션 대화에서 소급, 2026-11-27)

- 출처: 이 문서를 쓰거나 고친 세션 1개(Claude 0, Codex 1). 아래는 그 구간의 **사용자 발언 원문 발췌**와 **작업 세션이 당시 보고한 원인·실패·결정 발췌**다. 작업 세션의 보고는 그 세션의 주장이며, 이 기록을 쓰며 다시 검증하지 않았다.
- 이 절이 위 표의 "미확인"을 보충한다. 요청·의도는 사용자 발언, 시행착오·원인은 작업 세션 보고 칸을 본다.

### Codex 세션 `01a0acb2` (2026-10-08 21:02 ~ 2026-10-08 22:19 KST, 문서 편집 1회)

| 시각 (KST) | 사용자 발언 (원문 발췌) |
|---|---|
| 2026-10-08 21:02 | 멈췄던 부분부터 계속 진행하자 끝까지 |
| 2026-10-08 21:36 | [21:35:39] [BACKEND] Content root path: C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01Backend [21:35:40] > dotnet run --project "C:\DSFD\ProjectDSFD\Source Code\ProjectProject01\Tools\ProjectProject01SmokeTest\ProjectProject01SmokeTest.csproj" -- --project-root "C:\DSFD\ProjectDSFD\Source Code\ProjectProject01" [21:35:42] [BUILD] C:\ |
| 2026-10-08 21:37 | 클라이언트 버전 불일치 할때 차단은 음...있긴 한데 확인할순 없겠네 |
| 2026-10-08 21:39 | 스모크 테스트 bin obj 파일은 지워도 되는거지? |
| 2026-10-08 21:47 | 24. 서버 점검·공지·강제 로그아웃 - 점검 상태에서 신규 로그인 제한 - 점검 상태에서 신규 방 생성 제한 - 서버 종료 전 공지 - 전체 공지 전달 - 만료된 토큰 강제 로그아웃 - 폐기된 토큰 강제 로그아웃 - 중복 로그인 세션 처리 - 관리자 기능을 일반 클라이언트와 분리 25. 로컬라이징 구조 - UI 문구를 코드에 직접 고정하지 않음 - 버튼·상태·오류 문구 분리 - 한국어를 기본 언어로 유지 - 다른 언어를 추가할 수 있는 구조 - 언어 변경 시 코드를 다시 수정하지 않도록 구성 이제 마지막으로 이걸 해야하긴 하는데....음 싱글에서는 서버 없이 게임 돌아가게 할 수 있는거지? 업데이트 제외하고 멀티만 |
| 2026-10-08 21:49 | 버전 불일치 안내도 멀티만 그렇게 하고 이대로 한번 해보자 |

| 시각 (KST) | 작업 세션 보고 (원인·실패·결정, 발췌) |
|---|---|
| 2026-10-08 21:03 | 컴파일은 전부 통과했습니다. 이제 실제 MySQL·백엔드를 켠 상태에서 스모크 러너를 한 번 끝까지 실행해 방 생성부터 결과 등록·방 복귀까지 검증하고, 실패가 나오면 그 단계의 구현을 바로 보완하겠습니다. |
| 2026-10-08 21:07 | 핵심 구현과 실제 21단계 종단 스모크 테스트는 이미 통과했고, 지금은 스모크 보고서가 기존 진단 대시보드에서 잘못된 상위 폴더로 합쳐질 수 있는 호환 문제를 고친 뒤 전체 빌드·자체 테스트까지 다시 확인하는 단계입니다. |
| 2026-10-08 21:09 | 관련 도구 4개가 모두 빌드됐고 대시보드 자체 테스트도 통과했습니다. 마지막으로 실제 MySQL·백엔드 흐름을 다시 한 번 실행해 새 보고서 형식까지 검증하겠습니다. |
| 2026-10-08 21:13 | 최종 실제 검증도 다시 통과했습니다: 로그인부터 기존 방 복귀까지 21단계 전부 성공했고, 일부러 틀린 네트워크 프로토콜 요청은 HTTP 426으로 차단됐습니다. |
| 2026-10-08 21:17 | 22번 스모크 테스트와 23번 버전 불일치 차단을 모두 구현하고 실제 검증까지 마쳤습니다. - 로그인부터 기존 방 복귀까지 21단계 전부 통과 - 고의로 틀린 네트워크 프로토콜 요청은 HTTP 426으로 차단 - 클라이언트·데디케이티드 서버·API·게임 데이터·네트워크 프로토콜 버전 검사 - 단계별 10초 제한, 실패 단계, 원본 로그 기록 - 토큰·티켓·암호화 키 자동 마스킹 - 기존 진단 대시보드에서 `Smoke_*` 보고서를 정상 인식 - Unreal C++ 빌드 성공 - 백엔드·스모크 도구·빌드 도우미·진단 대시보드 빌드 성공 - 대시보드 자체 테스트 통과 - |
| 2026-10-08 21:37 | - 21개 검사 단계 모두 `[PASS]` - `RejectIncompatibleClient`도 정상 통과 - 백엔드의 `Mismatches=NetworkProtocol` 경고는 일부러 잘못된 버전을 보내 차단되는지 확인한 테스트입니다. 즉, 현재 결과는 정상적인 최종 통과입니다. |
| 2026-10-08 21:38 | 자동 검증상 차단 로직 자체는 정상입니다. |
| 2026-10-08 21:39 | `.gitignore`에도 이미 제외돼 있어 Git에 올릴 필요가 없습니다. |
| 2026-10-08 21:58 | 먼저 현재 수정 상태와 컴파일 위험 지점을 확인한 뒤, 남은 현지화 정리와 실제 빌드·동작 검증까지 마치겠습니다. |
| 2026-10-08 22:03 | 스모크 테스트 쪽은 코드 오류가 아니라 `obj/project.assets.json`이 지워져 있어 `--no-restore` 빌드만 실패한 상태라, 일반 복원을 포함해 다시 빌드하겠습니다. |

# 한글 로그·로비 포커스·결과 복구·EOS 틱 보완

## 변경 사항

- 백엔드 `Program.cs`는 콘솔 출력을 UTF-8(BOM 없음)으로 지정한다. 도우미 `ProcessLogEncoding`은 dotnet 및 `-utf8output` 명령의 stdout/stderr를 UTF-8로 읽고, 하위 .NET CLI도 동일 인코딩으로 실행한다. 기존 언어 선택은 유지하며 레거시 PowerShell/네이티브 명령에 무조건 UTF-8을 적용하지 않는다. 이미 물음표로 손실된 과거 로그를 복원하는 변경은 아니다. [Microsoft CLI 인코딩 설명](https://learn.microsoft.com/en-us/dotnet/core/compatibility/sdk/8.0/console-encoding).
- 로비 위젯은 초기화 시 `SetIsFocusable(true)`를 설정한다. 설정/설문에서 로비 위젯으로 포커스를 돌려주는 기존 흐름은 유지한다.
- 게임 인스턴스의 결과/경기 완료/기권 전송은 정식 GUID 경기 ID만 대상으로 한다. `LocalPIE-…` 결과 화면은 기존처럼 표시하지만 백엔드 전송과 정식 복구 저널 저장은 하지 않는다. 네이티브 outbox도 저장 전에 경기 ID를 검사한다. 실제 경기 참가·역할·서명 권한은 여전히 백엔드가 검증한다.
- 복구 worker는 체크섬·스키마를 확인한 과거 로컬 테스트 저널을 HTTP 없이 `.local-test`로 보존한다. 잘못된 경기 ID는 `.invalid`로 보존하며, 같은 보관본이 있으면 고유 이름을 사용해 덮어쓰지 않는다. 정상 경기의 재시도·서명·ACK 처리는 유지한다. **schema 1 JSON 구조와 payload/sha1/headers/body 내용은 바꾸지 않았다.** 이미 `.rejected`인 기존 파일은 재전송 대상이 아니므로 그대로 둔다. 기존 사용자 데이터나 보관 기록을 삭제하지 않았다.
- EOS는 같은 전용 소유 스레드에서 먼저 Tick을 호출하고, 명령 처리에 8건/약 2ms 상한을 둔다. Tick/명령 소요 시간을 포함해 다음 대기 시간을 계산해, 무제한 큐 처리와 고정 추가 10ms 대기로 인한 지연을 줄인다. 오류 로그·SDK 경고는 숨기거나 낮추지 않는다. [Epic의 주기적인 Tick 안내](https://dev.epicgames.com/docs/epic-online-services/eos-get-started/get-started-guide/next-steps?lang=en-US).

OS 스케줄링 지연, 장시간 걸리는 개별 네이티브 호출, PC 전체 부하까지 이 루프가 선점하거나 보장할 수는 없다. 따라서 EOS 지연 경고가 절대 발생하지 않는다는 뜻은 아니다. 실제 다중 PC 음성 송수신은 별도 실기 검증 대상이다.

## 검증 (2026-10-21 완료)

- `ProjectDFSDEditor Win64 Development`: 빌드 성공.
- `dotnet run --project Tools/ProjectProject01FeatureChecks/ProjectProject01FeatureChecks.csproj -- --log-reliability-checks`: 21개 통과. 실제 하위 프로세스 한국어 stdout/stderr 왕복, 과거 PIE 기록의 바이트 보존/HTTP 미발송, 정상 경기 재전송, EOS 요청 적체·명령 예외·취소 시나리오를 포함한다.
- 같은 검사 프로그램의 기본 전체 실행: 188개 통과. 실제 운영 DB나 EOS 자격 증명을 사용하지 않는다.
- Unreal 자동 검사: 29개 통과, 실패·미실행 0개. UITheme/Operations/Community/Frontend/Settings/Review/실제 타이틀 PIE 전환을 한 실행에 묶었다. `Saved/ReviewRemediation/LogReliability-20261020/index.json`에 결과가 있다. 실제 PIE 검사 1개는 기존 엔진/꺼진 백엔드 연결 경고를 동반했다. 로비 포커스 가능 설정, PIE 저널 저장 차단, 정상 저널 생성·중복 처리도 검사했다.

## Zen 시작 대기창 진단

2026-10-20 23:50 시작 로그에서 Zen은 약 37.225초 후 HTTP `Status: OK`가 됐다. 당시 C: 여유 공간은 약 215GB였다. 캐시 인덱스를 열고 정상 실행됐으며, 이 자료만으로 캐시 손상이나 긴 유지보수 작업이라고 단정할 근거는 없다. 에디터가 Zen 수명을 관리하므로 에디터/검사 프로세스를 자주 닫고 다시 실행하면 초기화도 반복된다. 이번 최종 검사에서는 Zen 준비가 약 3.715초였다.

엔진 공용 캐시 삭제, Zen 비활성화, 백신/방화벽 예외 추가, 엔진 설정 변경은 하지 않았다. 패키징도 실행하지 않았다.

## 적용

도우미와 백엔드는 수정된 빌드로 다시 실행해야 새 한글 출력과 worker/틱 처리가 적용된다. 에디터는 다시 열면 새 DLL을 사용한다. 기존 배포 클라이언트·데디에는 추후 해당 실행 파일을 다시 빌드/패키징할 때 네이티브 수정이 반영된다. 도우미를 별도로 publish한 실행 파일을 사용한다면 그 publish본도 갱신해야 한다.

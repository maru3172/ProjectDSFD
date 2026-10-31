# 다른 프로젝트로 툴 가져가기

아래 툴은 폴더를 다른 언리얼 프로젝트의 `Tools/` 아래로 복사하는 것만으로 쓸 수 있습니다. 게임 이름·경로·실행 파일·엔진 위치를 코드에 적지 않고 프로젝트에서 직접 읽어 옵니다. 이 프로젝트에서는 모든 값이 이전과 똑같이 나옵니다.

| 툴 | 스스로 알아내는 것 | 바꾸고 싶을 때 |
| --- | --- | --- |
| `MeetingRecorder` (회의 기록) | 프로젝트와 무관. 설정은 `%AppData%\MeetingRecorder` | 화면의 저장 위치, `settings.json` |
| `CollaborationTool` (협업툴 Daybook) | 프로젝트와 무관. 자료는 `%LocalAppData%\DSFD\Daybook`, Git 연결은 화면에서 고른 폴더 | 화면의 **이 PC·Git**, **서버 관리**. 처음 받은 PC에서는 `npm ci` 후 `node self-host/package.mjs`로 배포본(`outputs/Daybook-Windows`)을 만듭니다 |
| `ProjectProject01GitMerge` (Git 관리) | 스크립트가 있는 git 저장소. 저장소 안의 `.uproject`가 있으면 그 `Saved/`에, 없으면 `.git/git-manager/`에 보고서 저장. 창 제목은 프로젝트 이름 | 환경 변수 `GITMANAGER_SAVED` |
| `ProjectProject01ClientLauncher` (런처) | 업데이트 매니페스트의 실행 파일(없으면 설치 폴더의 유일한 .exe), 그 이름의 Shipping 프로세스, `*/Saved` 캐시 | `launcher.json` (게임 이름, 실행 파일, 서버 인자, 포트, 설정 폴더, 캐시 폴더) |
| `ProjectProject01DiagnosticsDashboard` (진단 대시보드) | 위쪽 폴더의 `.uproject`, 엔진 위치(EngineAssociation → Epic 런처 설치 폴더 또는 레지스트리), 에디터 빌드 대상(`Source/*Editor.Target.cs`), 테스트 그룹 `{프로젝트}General`, 봇 시뮬레이션 경로 `{주 모듈}.Simulation.BotMatch` | `dashboard.json` (EngineRoot, EditorTarget, GeneralTestGroup, BotSimulationPrefix, AdminKeyHeader, BackendAddress) |
| `ProjectProject01BuildServerHelper` (빌드·서버 도우미) | 위쪽 폴더의 `.uproject`, 클라이언트 `{이름}.exe`, 서버 `{이름}Server.exe`, 서버 맵(`Config/DefaultEngine.ini`의 ServerDefaultMap), 엔진 위치, 함께 쓰는 툴(`Tools/*Backend`, `Tools/*ClientLauncher`, `Tools/*DiagnosticsDashboard`) | `helper.json` (BackendPort, MySqlPort, GamePort, ServerMap), 환경 변수 `UE_ENGINE_ROOT` |

## 이 프로젝트 전용으로 남긴 것

- `ProjectProject01Backend`(게임 서버 API), `ProjectProject01FeatureChecks`, `ProjectProject01SmokeTest`, `ProjectProject01BotPrototype`: 이 게임의 규칙과 데이터를 다루는 코드라 다른 게임에 그대로 쓸 수 없습니다.
- 빌드·서버 도우미의 운영 화면(공지, 사건 기록, 계정 제한 등)은 이 게임의 백엔드 API를 부릅니다. 같은 백엔드가 없는 프로젝트에서는 빌드·패키징·서버 실행만 쓰고, 백엔드 기능은 "백엔드 프로젝트가 없습니다"로 막힙니다.
- 진단 대시보드의 운영자 수집(BackendDiagnosticsPanel)도 같은 백엔드가 있어야 동작합니다.

## 확인 방법

- 회의 기록: `MeetingRecorder.exe --selftest`
- 협업툴: `node scripts/check-standalone.mjs`, `node scripts/check-standalone-extended.mjs`, `node scripts/check-team-tools.mjs` (CollaborationTool 폴더에서)
- Git 관리: `ProjectDFSDGitManager.exe --self-test --repo <저장소>` (PowerShell 7 필요)
- 진단 대시보드: `ProjectDFSDDiagnosticsDashboard.exe --self-test`
- 런처·빌드 도우미·백엔드: `dotnet run --project Tools/ProjectProject01FeatureChecks/ProjectProject01FeatureChecks.csproj` (프로젝트 폴더에서)

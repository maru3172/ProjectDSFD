# ProjectProject01 Build + Server Helper

`.NET 8` WinForms 기반의 로컬 빌드·서버 실행 도우미입니다. 게임 패키지에는 포함되지 않습니다.

## 실행

```powershell
cd "Tools\ProjectProject01BuildServerHelper"
dotnet run
```

다른 PC에 전달할 독립 실행 파일은 다음 명령으로 만듭니다.

```powershell
dotnet publish -c Release -r win-x64 --self-contained true
```

## 사용 순서

1. `환경 검사`로 UE 경로, 프로젝트, 포트, 백엔드, 서명 비밀키를 확인합니다.
2. `MySQL 초기화/시작`, `백엔드 시작`을 차례로 누릅니다.
3. 클라이언트 또는 데디케이티드 서버 패키징을 실행합니다.
4. 소스 빌드 UE로 서버 패키징한 뒤 `게임 서버 시작`을 누릅니다.
5. MySQL과 백엔드가 정상일 때 `전체 흐름 스모크 테스트`를 누르면 로그인부터 3인 방,
   역할 배정, 서버 권위 결과·리더보드 등록, 기존 방 복귀와 버전 차단을 순서대로 검사합니다.
   결과는 `진단 결과 폴더`의 `Smoke_*` 디렉터리에 저장됩니다.

Launcher Installed Build는 전용 서버 패키징을 지원하지 않으므로 도구가 이를 차단하고 이유를 표시합니다. 비밀키와 DB 연결 문자열은 도구나 Git에 저장하지 않고 환경변수/user-secrets에서 읽습니다.

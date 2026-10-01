# ProjectProject01 로그인 백엔드

이 프로그램은 Unreal 클라이언트와 MySQL 사이에 위치하는 로컬 개발용 ASP.NET Core API입니다. Unreal 클라이언트가 MySQL에 직접 접속하지 않도록 분리합니다.

## 격리된 MySQL 서버

기존 `MySQL80` 서비스와 기존 데이터베이스는 사용하거나 변경하지 않습니다. 프로젝트 전용 인스턴스는 `127.0.0.1:3307`과 `LocalMySql` 데이터 디렉터리를 사용합니다.

최초 한 번만 다음 명령을 실행합니다.

```powershell
.\ProjectProject01MySql.ps1 -Action Initialize
```

무작위 API·관리자 비밀번호와 접속 문자열은 프로젝트 파일이 아니라 .NET User Secrets에 저장됩니다. 이후 시작·종료·상태 확인은 다음 명령을 사용합니다.

```powershell
.\ProjectProject01MySql.ps1 -Action Start
.\ProjectProject01MySql.ps1 -Action Status
.\ProjectProject01MySql.ps1 -Action Stop
```

## API 실행

격리된 MySQL 서버를 시작한 뒤 로컬 API를 실행합니다.

```powershell
dotnet run --urls http://127.0.0.1:5080
```

`http://127.0.0.1:5080/health`가 HTTP 200을 반환하면 Unreal의 `LoginLevel`에서 회원가입과 로그인을 테스트합니다.

## 보안 경계

- 비밀번호 원문은 저장하거나 로그로 출력하지 않습니다.
- DB에는 ASP.NET Core Identity의 `PasswordHasher` 결과만 저장합니다.
- 액세스·리프레시 토큰 원문은 클라이언트에만 반환하고, DB에는 SHA-256 해시만 저장합니다.
- 로그인 실패 5회 시 15분 동안 해당 계정을 잠급니다.
- 인증 API는 IP별 분당 10회로 제한합니다.
- 현재 HTTP 주소는 같은 PC의 개발 테스트 전용입니다. 외부 배포에서는 HTTPS 리버스 프록시와 최소 권한 DB 계정을 사용해야 합니다.

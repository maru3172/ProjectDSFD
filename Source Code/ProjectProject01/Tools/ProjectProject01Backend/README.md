# ProjectProject01 로그인·로비 백엔드

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

## 방 로비

로그인한 사용자는 `LobbyLevel`에서 다음 기능을 사용할 수 있습니다.

- 공개방 생성 및 공개방 목록 참가
- 비공개방 생성 후 방 ID 직접 입력 참가
- 공개·비공개방 모두 선택적으로 4~64자 비밀번호 설정
- 방장을 제외한 참가자의 준비/준비 취소
- 정확히 3명이 모이고 두 참가자가 모두 준비한 경우에만 방장 시작
- 방장이 나가면 방과 채팅을 함께 삭제
- 최대 30개의 대기방 유지
- 방마다 최근 100개의 채팅 표시
- 시작 시 암호학적 난수로 1명은 `Mannequin`, 2명은 `Survivor` 배정

방 비밀번호는 원문을 저장하지 않고 ASP.NET Core `PasswordHasher` 결과만 MySQL에 저장합니다.
공개방만 목록에 나타나며 비공개방은 방 화면에 표시되는 ID를 전달받아 참가합니다.

게임 시작 전에 `MultiplayTest` 전용 서버가 실행 중이어야 합니다. 기본 접속 주소는
`127.0.0.1:7777`이며, 다른 주소는 다음 구성값으로 지정할 수 있습니다.

```json
{
  "Lobby": {
    "GameServerTravelUrl": "게임서버주소:7777"
  }
}
```

클라이언트는 시작된 방을 1초 이내에 확인하고 같은 서버로 이동합니다. 역할은 접속 URL의
`LobbyRole` 옵션으로 `AMultiplayTestGameMode`에 전달됩니다. 이 방식은 현재 로컬 개발 및 기능
검증용이며, 외부 배포 전에는 서버가 백엔드에 역할 티켓을 검증하는 단계를 추가해야 합니다.

정상 종료 시 방 나가기를 먼저 호출합니다. 비정상 종료된 대기방과 참가자는 다른 로비 요청이
들어올 때 마지막 확인 시각 기준 60초 후 정리됩니다.

## 보안 경계

- 비밀번호 원문은 저장하거나 로그로 출력하지 않습니다.
- DB에는 ASP.NET Core Identity의 `PasswordHasher` 결과만 저장합니다.
- 액세스·리프레시 토큰 원문은 클라이언트에만 반환하고, DB에는 SHA-256 해시만 저장합니다.
- 로그인 실패 5회 시 15분 동안 해당 계정을 잠급니다.
- 인증 API는 IP별 분당 10회로 제한합니다.
- 현재 HTTP 주소는 같은 PC의 개발 테스트 전용입니다. 외부 배포에서는 HTTPS 리버스 프록시와 최소 권한 DB 계정을 사용해야 합니다.

# ProjectProject01 인증·로비·게임 접속 백엔드

이 ASP.NET Core API는 Unreal 클라이언트와 MySQL 사이의 신뢰 경계입니다. 클라이언트는 MySQL에 직접 접속하지 않으며, 로그인·방·게임 접속 티켓·서버 검증 경기 결과·리더보드만 API를 통해 처리합니다.

## 로컬 개발 시작

기존 `MySQL80` 서비스와 기존 데이터베이스는 사용하거나 변경하지 않습니다. 프로젝트 전용 인스턴스는 `127.0.0.1:3307`과 Git에서 제외된 `LocalMySql` 데이터 디렉터리를 사용합니다.

새 PC에서는 최초 한 번만 실행합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01MySql.ps1" -Action Initialize
```

이전 버전에서 이미 초기화한 PC는 기존 로그인 데이터를 유지한 채 새 게임 서버 비밀키를 한 번 동기화합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01MySql.ps1" -Action SyncLocalConfiguration
```

평소 시작·상태 확인·종료는 다음과 같습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01MySql.ps1" -Action Start
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01MySql.ps1" -Action Status
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01MySql.ps1" -Action Stop
```

MySQL을 시작한 뒤 API를 실행합니다.

```powershell
dotnet run --urls http://127.0.0.1:5080
```

`http://127.0.0.1:5080/health`가 HTTP 200과 `healthy`를 반환하면 로컬 로그인·로비 테스트 준비가 끝난 것입니다. HTTP는 루프백 개발 접속에만 허용됩니다.

## 점검·공지·강제 로그아웃 관리

관리자 기능은 일반 게임 클라이언트 API와 분리되어 있으며 `X-ProjectProject01-Admin-Key`를 요구합니다.
기존 개발 PC는 다음 명령을 한 번 실행해 Git에 포함되지 않는 로컬 관리자 키를 준비한 뒤 백엔드를 재시작합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01MySql.ps1" -Action SyncLocalConfiguration
```

운영자는 일반 클라이언트가 아니라 전용 스크립트로만 관리 기능을 호출합니다.

```powershell
# 현재 상태
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01Admin.ps1" -Action Status

# 신규 로그인·회원가입·방 생성을 막고 점검 문구 표시
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01Admin.ps1" -Action MaintenanceOn -Message "멀티플레이 서버 점검 중입니다."

# 점검 해제
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01Admin.ps1" -Action MaintenanceOff

# 전체 공지 또는 종료 예정 시각(UTC) 전달
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01Admin.ps1" -Action Announce -Message "10분 뒤 서버를 종료합니다." -ShutdownAtUtc "2026-10-08T13:00:00Z"

# 모든 멀티플레이 로그인 세션 또는 특정 계정 세션 강제 종료
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01Admin.ps1" -Action ForceLogoutAll
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01Admin.ps1" -Action ForceLogoutAccount -AccountId "player01"
```

강제 로그아웃은 다음 인증 요청에서 감지되어 로그인 화면으로 이동합니다. 멀티 경기 중에도 5초 간격의
경량 상태 확인으로 공지·종료 예고를 상단 배너에 표시하고 폐기된 세션을 감지합니다. 같은 계정으로 다시
로그인하면 기존 세션은 폐기되고 가장 최근 로그인 하나만 유지됩니다. 이 기능과 버전 불일치 차단은
로그인·로비·멀티 경기에만 적용되며 싱글플레이는 백엔드가 꺼져 있어도 실행됩니다.

## 전체 흐름 스모크 테스트와 버전 계약

MySQL과 백엔드를 실행한 뒤 Build + Server Helper의 `전체 흐름 스모크 테스트`를 누르면 로그인,
3인 방, 준비·시작, 역할 배정, 일회용 티켓 검증, 서버 권위 경기 결과, 리더보드 등록과 기존 방 복귀를
순서대로 검사합니다. 실패한 단계와 원본 로그는 `Saved/Diagnostics/Smoke_*`에 저장되며 토큰·티켓·암호화
키는 제거됩니다.

클라이언트, 데디케이티드 서버, API, 게임 데이터와 네트워크 프로토콜 버전은 모두 일치해야 합니다.
불일치 요청은 HTTP 426과 업데이트 안내로 거부됩니다. Unreal 기본값은 `Config/DefaultGame.ini`, 백엔드
기본값은 `appsettings.json`의 `Compatibility`에 있으며 배포 환경에서는 다음 환경변수로 덮어쓸 수 있습니다.

```text
Compatibility__ClientBuildVersion=1.0.0
Compatibility__DedicatedServerBuildVersion=1.0.0
Compatibility__ApiVersion=1
Compatibility__GameDataVersion=1
Compatibility__NetworkProtocolVersion=1
Compatibility__UpdateMessage=<사용자 안내 문구>
```

호환되지 않는 변경을 배포할 때 해당 값을 올리고 새 클라이언트·서버·백엔드 설정을 같은 계약으로
배포해야 서로 다른 버전이 로그인하거나 같은 방에 섞이지 않습니다.

## 로비와 보안 게임 접속

- 공개/비공개 방, 선택적 방 비밀번호, 6~8자리 참가 코드, 최대 30개 대기방을 지원합니다.
- 정확히 3명이 모이고 참가자가 준비하면 방장이 시작할 수 있습니다.
- 서버가 암호학적 난수로 `Mannequin` 1명과 `Survivor` 2명을 배정합니다.
- 시작된 각 경기에는 별도 `match_id`가 생성됩니다.
- 각 플레이어는 60초 유효한 일회용 게임 접속 티켓을 발급받습니다.
- 티켓 원문은 DB에 저장하지 않고 SHA-256 해시만 저장하며, 데디케이티드 서버가 한 번 소비하면 재사용할 수 없습니다.
- AES-GCM 256비트 키는 티켓마다 다르게 파생되며, URL에 키를 넣지 않습니다. 클라이언트와 서버는 UE 암호화 핸드셰이크를 통해 메모리에서만 사용합니다.
- `AMultiplayTestGameMode::PreLogin`은 백엔드 검증이 끝난 티켓만 허용하고 역할을 클라이언트 URL 값이 아니라 검증된 claim에서 가져옵니다.

기본 게임 서버 주소는 `127.0.0.1:7777`입니다. 다른 주소는 안전한 서버 설정 또는 환경별 구성에서 다음 값으로 지정합니다.

```json
{
  "Lobby": {
    "GameServerTravelUrl": "게임서버주소:7777"
  }
}
```

UE 설정은 `AESGCMHandlerComponent`, `[PacketHandlerComponents] EncryptionComponent=AESGCMHandlerComponent`, `net.AllowEncryption=2`를 사용합니다. 직접 `IP:7777`로 접속하거나 클라이언트가 역할만 조작해 접속하는 경로는 거부됩니다. 로컬에서 티켓 없는 개별 PIE 로직만 시험할 때에는 테스트용 GameMode 인스턴스의 `bRequireGameJoinTicket`을 명시적으로 꺼야 하며, 공개 서버에서는 켠 상태를 유지해야 합니다.

## 인증과 권한

- 계정 및 방 비밀번호는 ASP.NET Core `PasswordHasher` 결과만 저장합니다.
- 로그인 실패 5회 시 15분 동안 계정을 잠급니다.
- 인증 API는 IP별 분당 10회, 게임 티켓은 분당 30회로 제한합니다.
- 액세스 토큰은 30분, 리프레시 토큰은 30일이며 DB에는 토큰의 SHA-256 해시만 저장합니다.
- 갱신 시 이전 세션을 폐기하고 액세스·리프레시 토큰을 모두 회전합니다.
- 로그아웃 시 해당 세션을 폐기합니다.
- 현재 C++의 모든 Server RPC는 입력/역할/상태를 서버에서 검사하고 RPC별 호출 빈도를 제한합니다.
- 보안 관련 인증, 티켓, 경기 결과, 리더보드 판정은 `security_audit_events`에 기록합니다. 비밀번호·토큰·비밀키 원문은 기록하지 않습니다.

경기 결과는 데디케이티드 서버만 `/api/server/matches/results`에 제출할 수 있습니다. 사용자의 역할과 해당 경기 참가 여부를 다시 검증한 성공 기록만 리더보드 등록에 사용할 수 있습니다. 현재 게임 종료 규칙을 연결할 때 서버에서 `UProjectProject01GameInstance::SubmitAuthoritativeMatchResult`를 호출해야 합니다. 로컬 `LeaderBoardTest` 수동 검증은 Git에서 제외된 개발 설정에서만 예외 허용됩니다.

## 공개 배포 HTTPS와 비밀키

공개 배포에서는 저장소나 패키지에 비밀키를 넣지 않습니다. 백엔드에는 환경변수 또는 배포 환경의 비밀 저장소로 다음 값을 제공합니다.

```text
GameServer__SharedSecret=<32자 이상 무작위 값>
GameServer__TicketKey=<32바이트 무작위 값의 Base64>
ConnectionStrings__ProjectProject01=<운영 DB 최소 권한 연결 문자열>
Administrator__ApiKey=<32자 이상 무작위 관리자 키>
```

데디케이티드 서버 프로세스에는 같은 공유 비밀을 환경변수로 제공합니다.

```text
PROJECTPROJECT01_GAME_SERVER_SECRET=<백엔드와 같은 공유 비밀>
```

Kestrel에 인증서를 직접 적용하는 예시는 다음과 같습니다. 인증서 경로와 암호는 저장소 밖에서 주입합니다.

```powershell
$env:ASPNETCORE_URLS = 'https://0.0.0.0:5081'
$env:ASPNETCORE_Kestrel__Certificates__Default__Path = 'D:\Secrets\projectproject01.pfx'
$env:ASPNETCORE_Kestrel__Certificates__Default__Password = '<비밀 저장소에서 주입>'
dotnet run --configuration Release
```

리버스 프록시를 쓸 경우 외부 TLS는 프록시에서 종료하고 백엔드는 같은 서버의 루프백 주소로만 노출합니다. 공개 HTTP 요청은 애플리케이션에서 거부됩니다.

## DB 차단과 백업

로컬 MySQL은 스크립트가 `--bind-address=127.0.0.1`, `--mysqlx=OFF`, `--local-infile=OFF`로 실행합니다. 운영 환경에서도 MySQL 포트는 인터넷에 공개하지 않고 방화벽에서 백엔드 서버만 허용합니다. 외부 인바운드는 원칙적으로 HTTPS 포트와 필요한 게임 UDP 포트만 엽니다.

수동 백업과 매일 03:00 예약 백업 등록은 다음과 같습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01MySql.ps1" -Action Backup
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ProjectProject01MySql.ps1" -Action InstallBackupTask
```

백업 파일은 Git에서 제외된 `LocalMySql/Backups`에 저장됩니다. 실제 운영에서는 별도 암호화 저장소로 복제하고 복구 시험을 수행해야 합니다.

## 배포 전 확인

1. 공개 API가 유효한 HTTPS 인증서로만 응답하는지 확인합니다.
2. MySQL 포트가 외부에서 닫혀 있고 최소 권한 계정만 사용하는지 확인합니다.
3. 티켓 없는 직접 게임 서버 접속과 사용된 티켓 재접속이 거부되는지 확인합니다.
4. Mannequin/Survivor 권한 밖의 Server RPC와 과다 호출이 거부되는지 확인합니다.
5. 성공한 경기만 서버 검증 결과를 거쳐 리더보드에 등록되는지 확인합니다.
6. 백업 생성과 실제 복구를 별도 테스트 DB에서 확인합니다.

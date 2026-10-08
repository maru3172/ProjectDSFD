# 생존자 근접 보이스 채팅 설정

게임 코드는 생존자만 동일 매치의 positional voice channel에 참가시키고, 위치에 따라 음량을 감쇠합니다. 마네킹 역할과 탈락한 클라이언트에는 채널 정보를 전달하지 않습니다.

## 개발 PIE

`DefaultGame.ini`의 `bAllowInsecureDevelopmentCredentials=True`는 Development/Editor에서만 적용됩니다. EOSVoiceChat 플러그인의 EOS Product/Sandbox/Deployment/Client 자격 증명을 각 개발자의 로컬 EOS 설정에 넣어야 실제 마이크 송수신이 시작됩니다. 자격 증명은 Git에 올리지 않습니다.

개발자 PC의 Git 비추적 로컬 설정에는 엔진이 요구하는 다음 항목이 필요합니다. 실제 값은 Epic Developer Portal의 해당 제품 환경 값을 사용합니다.

```ini
[EOSVoiceChat]
ProductId=<ProductId>
SandboxId=<SandboxId>
DeploymentId=<DeploymentId>
ClientId=<ClientId>
ClientSecret=<ClientSecret>
```

프로젝트 기본 설정에는 거리 감쇠와 10Hz 위치 갱신만 저장되어 있으며 EOS 비밀 값은 포함하지 않습니다. 공급자 설정이 없거나 로그인에 실패하면 경고 로그만 남기고 게임과 핑 기능은 계속 동작합니다.

## Shipping

Shipping에서는 insecure credential 코드가 컴파일 단계에서 비활성화됩니다. 백엔드가 로그인 사용자와 MatchId/Survivor 역할을 검증한 뒤 짧은 수명의 EOS voice login/join token을 발급하도록 연결해야 합니다. 제품 비밀키나 고정 채널 키를 클라이언트 설정/실행 파일에 넣으면 안 됩니다.

## 기본 조작

- `F2`: 누르는 동안 송신
- `U`: 상시 송신 켜기/끄기
- `Z`: 도움 요청 핑
- `X`: 위험 핑
- `C`: 위치 핑

모든 키는 환경설정에서 중복 검사와 함께 변경할 수 있습니다. F1/Escape는 안전 메뉴 복구를 위해 예약되어 있습니다.

## 권한과 노출 범위

- 서버가 `Survivor + Active`로 판정한 사용자만 같은 매치의 생존자 채널을 받습니다.
- 마네킹, 탈락자, 탈출자에게는 채널을 유지하지 않습니다.
- 핑 위치는 클라이언트가 보내지 않고 서버가 플레이어 시점에서 직접 계산합니다.
- 서버는 거리·역할·경기 상태와 5초당 최대 4회 제한을 검증합니다.

# ProjectProject01 EOS Voice setup

EOS 음성 채팅은 두 개의 서로 다른 자격 증명을 사용합니다.

- `ProjectProject01GameClient`: 각 게임 클라이언트가 EOS Connect Device ID로 Product User ID를 얻을 때 사용합니다.
- `ProjectProject01VoiceServer`: 백엔드만 사용하며, 실제 진행 중인 매치의 생존자에게 짧은 수명의 음성 방 토큰을 발급합니다.

## 한 번만 설정

빌드·서버 도우미의 `EOS 보이스 자격 증명` 버튼을 누르거나 다음 명령을 실행합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ConfigureProjectProject01EosVoice.ps1" -Action Configure
```

Developer Portal의 Clients 화면에서 두 Client Secret을 각각 입력합니다. Secret은 Git이나 ini에 저장되지 않고 현재 Windows 사용자 환경변수에만 저장됩니다. 설정 후 빌드·서버 도우미와 Unreal Editor를 다시 실행해야 합니다.

클라이언트 패키징 때 도우미는 GameClient Secret을 설정에 잠시 주입해 패키지에 포함한 뒤 원본 `DefaultEngine.ini`를 즉시 복구합니다. 클라이언트 자격 증명은 배포 프로그램에서 완전히 숨길 수 없으므로 권한이 제한된 `ProjectProject01GameClient` 정책만 연결합니다. `ProjectProject01VoiceServer` Secret은 절대로 클라이언트에 포함하지 않습니다.

상태만 확인할 때:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ConfigureProjectProject01EosVoice.ps1" -Action Status
```

## 동작과 보안 경계

클라이언트는 프로젝트 로그인 토큰과 EOS Product User ID를 `/api/voice/token`에 보냅니다. 백엔드는 해당 계정이 요청한 진행 중 매치의 생존자인지 확인한 뒤에만 그 매치의 생존자 채널 토큰을 발급합니다. 서버용 Client Secret은 클라이언트 패키지에 포함되지 않습니다.

보이스는 위치 기반입니다. 기본값은 350cm까지 완전 음량이며 이후 거리에 따라 작아져 1800cm에서 들리지 않습니다. 설정값은 `DefaultEngine.ini`의 `ProjectProject01VoiceChatSubsystem` 구역에서 변경할 수 있습니다.

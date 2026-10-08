# ProjectProject01 전체 흐름 스모크 테스트

백엔드와 MySQL을 먼저 실행한 뒤 다음 명령을 사용합니다.

```powershell
dotnet run --project .\Tools\ProjectProject01SmokeTest\ProjectProject01SmokeTest.csproj
```

검사 범위는 버전 불일치 차단, 테스트 계정 3개 생성, 방 생성·참가·준비·시작,
마네킹 1명/생존자 2명 역할 배정, 일회용 티켓 발급·서버 검증, 대표 탈출 성공 결과의
서버 권위 저장, 리더보드 등록, 세 플레이어의 기존 방 복귀입니다.

각 단계 제한 시간은 기본 10초이며 `--timeout-seconds 20`처럼 변경할 수 있습니다.
결과는 `Saved/Diagnostics/Smoke_*/TestReport.xml`, `Events.csv`, `SmokeTest.log`에 기록됩니다.
토큰·티켓·암호화 키는 원본 로그에서 제거됩니다.

실제 등록 경로를 검증하기 때문에 실행한 DB에는 `Smoke...` 테스트 계정과 성공 리더보드 기록이 남습니다.
방은 테스트 종료 시 정리됩니다. 이 도구는 로컬/스테이징 DB에서 실행하고 운영 DB에서는 실행하지 마세요.

이 테스트는 네트워크/백엔드의 최소 정상 흐름을 빠르게 확인하는 용도입니다. 실제 캐릭터 이동,
충돌, 애니메이션과 사람이 수행하는 붙잡기·구출 입력은 PIE/패키지 멀티플레이 테스트로 별도 확인합니다.

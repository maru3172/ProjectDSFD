# 시연용 패키징 런타임 점검

이 보완에서는 게임 쿠킹·패키징·업데이트 게시·서버 실행·런타임 설치를 하지 않는다.

## 다음 패키징에 적용되는 설정

- 기본 맵 6개와 `MultiplayTest1`, `MultiplayTest2`를 공통 쿠킹 목록에 포함한다.
- `/Game/MyProject/Data`의 세 데이터테이블 `.uasset`을 쿠킹한다. 개발용 원본 CSV를 별도 배포하지 않는다.
- VFX 폴더와 선택한 추가 심박 머티리얼의 기존 쿠킹 경로를 유지한다.
- 한국어 UI 문구용 `Content/Localization`의 기존 스테이징을 유지한다.
- `IncludePrerequisites=True`, 도우미 UAT 명령 `-prereqs -crashreporter`로 필수 런타임 설치 파일 및 크래시 리포터를 포함한다.
- 도우미는 선택한 UE 5.8 엔진의 VC++ x64/arm64, GameInput 설치 파일이 없으면 패키징 전에 안내하고 중단한다.

런타임 설치 파일을 포함하는 것과 설치하는 것은 다르다. 새 Windows PC에서 VC++ 런타임이 없으면
`Engine/Extras/Redist/en-us/vc_redist.x64.exe`를 한 번 설치해야 한다. 관리자 승인을 우회하거나 자동 설치하지 않는다.
런처는 기존처럼 자체 .NET 런타임을 포함해 게시한다. 게임은 `Windows` 폴더 전체를 배포해야 한다.

## 엔진 없이 백엔드 음성 실행

서버 도우미의 백엔드 시작 전 EOS SDK 및 XAudio DLL을 다음 서버 전용 폴더에 보관한다.

`Tools/ProjectProject01Backend/Native/EOS/win-x64/`

- `EOSSDK-Win64-Shipping.dll`
- `xaudio2_9redist.dll`

복사 시 Windows x64 DLL 형식을 검사하고 SHA-256으로 복사를 검증한다.
정상 보관본이 이미 있으면 사용 중 파일을 덮어쓰지 않는다. 보관본을 우선하므로 엔진 제거 또는 예전 환경변수 경로가 사라져도 동작한다.
백엔드 빌드와 publish에 해당 하위 폴더를 함께 복사한다. 백엔드를 다른 PC로 옮길 때 이 폴더도 유지해야 한다.
SDK를 교체해야 한다면 백엔드를 종료하고 해당 보관본 두 개를 별도로 백업한 뒤 새 SDK로 다시 준비한다.

이 폴더에는 DLL만 들어간다. EOS 서버 비밀키·DB 비밀번호는 클라이언트에 넣지 않으며,
다른 서버 PC에서는 기존 보안 설정 방식으로 별도 지정해야 한다. 백엔드를 framework-dependent로 실행한다면 .NET 9 런타임도 필요하다.
DLL 파일 검사 통과는 실제 EOS 인증·음성 통신 성공을 의미하지 않는다.

## 검증 범위

`ProjectProject01FeatureChecks --packaging-checks`는 패키징 명령, 설치 파일 누락 검사, 맵 목록,
DLL 복사·손상·아키텍처·엔진 없는 경로 복구를 검사한다. 테스트 DLL은 실행하지 않는다.
실제 패키징과 시연 PC의 그래픽 드라이버·네트워크·마이크 권한·EOS 통신·다중 PC 플레이 검증은 별도다.

2026-10-19 확인: 새 패키징 검사 21개 및 기존 검사와 합친 162개가 통과했다.
별도 이어받기 검사 17개와 설치 롤백 검사도 통과했고, 백엔드·도우미·런처·검사 프로그램 빌드는 경고/오류 0개였다.
실제 EOS/XAudio DLL 두 개를 서버 전용 폴더에 보관했으며 백엔드 빌드 출력과 SHA-256 일치를 확인했다.
두 DLL의 향후 publish 복사 메타데이터도 확인했다. publish·게임 패키징·운영 서버 실행·EOS 네이티브 초기화는 실행하지 않았다.

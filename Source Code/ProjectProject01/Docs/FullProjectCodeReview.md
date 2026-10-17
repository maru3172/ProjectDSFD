# 프로젝트 전체 코드 전수 검토 결과

검토일: 2026-10-17 / 범위: 작성된 프로젝트 소스·스크립트·빌드·공개 설정 169개, 51,098줄.

## 결론

새 기능을 더 늘릴 필요와는 별개로, 현재 코드에는 고쳐야 할 기존 기능 문제가 남아 있습니다. 특히 멀티 시작 암전/티켓 경합, 이탈 후 다음 맵 진입, 로그아웃 티켓 폐기, 멈춘 경기 세션 회수부터 우선 처리하는 편이 좋습니다. ‘전부 완성되어 수정할 것이 없다’고 결론 내릴 근거는 없습니다.

이번 작업은 검토만 했습니다. 게임·백엔드·런처 코드는 수정하지 않았고 서버·DB·패키징·배포 작업도 실행하지 않았습니다. 아래 항목은 정적 코드 경로로 확인한 결함/위험이며, 모두 실제 다중 PC에서 재현 시험을 끝냈다는 뜻은 아닙니다. 코드 검토 완료와 무결함 인증은 다릅니다.

## 범위와 확인 수준

- [FullCodeReviewCoverage.md:1](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Docs/FullCodeReviewCoverage.md:1>): 169개 파일별 담당·줄 수·해시·검토 상태. 검토 전후 해시 변경 0개.
- [FullCodeReviewInventory.json:1](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Docs/FullCodeReviewInventory.json:1>): 검토 대상 원본 목록.
- 바이너리 Blueprint/맵 내부, 실제 충돌·NavMesh·오디오 청감, 실제 EOS 다중 PC 통신, 모든 외부 UE/SDK 코드는 전수 검증 대상이 아닙니다. Config/DefaultEditor.ini의 엔진 기본 프리뷰 프로필은 구조/활성 설정 검토입니다.
- 백엔드 보조 검토의 45개에는 README 3개가 포함됩니다. 본문 169개에는 해당 파티션의 코드/설정 42개만 집계했습니다. UI 도구 README 등도 보조 자료로만 취급했습니다.

## 우선순위 목록

P1: 주요 흐름 정지·인증 폐기 실패·세션 회수 불능. P2: 조건부 오동작·상태/기록 유실·안정성. P3: 입력 오류 응답·표기 보완. 우선순위는 모든 정상 플레이에서 항상 발생한다는 의미가 아닙니다.

| 번호 | 우선순위 | 항목 |
|---|---|---|
| F01 | P1 | 멀티 시작 암전 후 티켓 요청이 누락되어 검은 화면에 머무를 수 있음 |
| F02 | P1 | 생존자 이탈 후 다음 맵이 여전히 3명을 기다림 |
| F03 | P1 | 로그아웃된 계정의 미사용 게임 티켓이 계속 유효함 |
| F04 | P1 | 진행 중 heartbeat가 끊긴 서버가 동시 경기 자리를 계속 점유함 |
| F05 | P2 | 다음 맵에서 이전 맵 경기 기록과 시간이 초기화됨 |
| F06 | P2 | 맵 이동 실패 후 암전·입력·상태를 되돌리지 않음 |
| F07 | P2 | 싱글 체크포인트는 AI의 저장 당시 행동을 복원하지 못함 |
| F08 | P2 | 백업 저장 실패를 무시한 채 원본을 덮어씀 |
| F09 | P2 | 최초 배포 런처의 ‘현재 버전 실행’ 경로 오류 |
| F10 | P2 | 다운로드 캐시가 다음 업데이트에 게시되어 전체 클라이언트가 거부함 |
| F11 | P2 | 패키징·게시·임시 EOS 설정 변경이 동시에 실행될 수 있음 |
| F12 | P2·조건부 보안 | 업데이트 경로 검사가 junction/symlink의 실제 목적지를 확인하지 않음 |
| F13 | P2 | 업데이트 설치 중 강제 종료 후 자동 복구 정보가 없음 |
| F14 | P2 | 음성 권한 거부가 403이 아니라 500으로 처리됨 |
| F15 | P2 | 설문 날짜 입력으로 전체 통계가 과도하게 커지거나 실패함 |
| F16 | P2 | 관리자 PowerShell 스크립트의 예약 점검 규약이 UI/API와 다름 |
| F17 | P2 | 백엔드 비정상 재시작 뒤 진행 방·회원 상태가 남음 |
| F18 | P2 | 사용하지 않는 저장된 가상 네트워크 IP 때문에 백엔드 전체 기동 실패 가능 |
| F19 | P2 | 로그인 중 변경한 서버 IP가 저장값과 실제 접속값을 다르게 만듦 |
| F20 | P2 | 리더보드 탭을 빠르게 바꾸면 이전 결과를 다른 열로 보여줌 |
| F21 | P2 | 파트너 AI의 끼임 복구 시간이 계속 초기화됨 |
| F22 | P2 | 일부 피격·사망·점멸 연출에 접근성 설정이 적용되지 않음 |
| F23 | P2 | 신음·쓰러짐·포획 합성음이 끝난 뒤에도 무음 처리를 계속함 |
| F24 | P2 | 중간에 종료된 진단이 Pass가 될 수 있음 |
| F25 | P2 | 진단 대시보드가 동시에 온 표준 출력/오류를 안전하게 수집하지 않음 |
| F26 | P2 | 경기 결과의 서버 제출이 한 번 실패하면 복구 경로가 없음 |
| F27 | P2 | 진행 경기에서 전원이 방 복귀 API를 호출하면 서버 종료 확인 없이 초기화됨 |
| F28 | P2 | 동시에 실패한 로그인이 계정 실패 횟수를 덮어쓸 수 있음 |
| F29 | P3 | null 표시 이름 회원가입 요청이 400 대신 500이 됨 |
| F30 | P3 | 역할 안내의 이동키 표기가 실제 재설정 키와 다름 |

## 항목별 근거·발생 조건·보완 방향

### F01 — 멀티 시작 암전 후 티켓 요청이 누락되어 검은 화면에 머무를 수 있음 (P1)

근거: [ProjectProject01LoginWidget.cpp:5859](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp:5859>), [ProjectProject01LoginWidget.cpp:5828](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp:5828>).

암전 완료 때 방 조회 HTTP가 진행 중이면 접속 티켓 요청을 건너뜁니다. 이미 완료 플래그를 세웠기 때문에 다음 방 조회에서도 암전을 다시 시작하지 않고, 누락된 티켓 요청을 재시도하는 경로도 없습니다. 평범한 1초 주기 방 조회와 겹치는 것만으로 발생 가능한 경합입니다.

보완: 암전 완료와 티켓 발급을 별도 상태로 관리하고, 조회 완료 뒤 미발급 티켓을 제한 시간 안에서 재요청해야 합니다. 요청·MatchId가 바뀌면 취소하고 실패 시 화면과 버튼을 복구해야 합니다.

### F02 — 생존자 이탈 후 다음 맵이 여전히 3명을 기다림 (P1)

근거: [MultiplayTestGameMode.cpp:2133](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp:2133>), [MultiplayTestGameMode.cpp:1872](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp:1872>), [MultiplayTestGameMode.cpp:2193](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp:2193>).

탈락한 생존자가 방으로 나간 뒤 남은 생존자와 마네킹만 다음 맵으로 이동하면 실제 컨트롤러는 2개입니다. 새 GameMode는 여전히 ExpectedMatchPlayers=3을 요구하므로 시작과 FinalizeLevelTransitionArrivals에 도달하지 못합니다. 암전과 입력 잠금도 풀리지 않을 수 있습니다. 엔진 기본 seamless travel도 나간 참가자를 만들어 주지 않습니다.

보완: 최초 경기 시작의 3명 조건과 구간 이동의 생존·기권·재접속 명단 조건을 분리하고, 전환 중 참가 명단을 서버에 보존해야 합니다.

### F03 — 로그아웃된 계정의 미사용 게임 티켓이 계속 유효함 (P1)

근거: [Program.cs:592](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/Program.cs:592>), [ServiceAdministration.cs:545](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/ServiceAdministration.cs:545>), [LobbyEndpoints.cs:993](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/LobbyEndpoints.cs:993>).

정상 로그아웃·관리자 개별 로그아웃은 로그인 세션과 방 참가를 정리하지만, 방에 다른 사람이 남으면 이미 발급한 미사용 게임 티켓은 지우지 않습니다. 소비 API는 현재 로그인 세션/방 참가를 다시 검사하지 않아 만료 전 최대 60초 동안 접속 claim을 발급할 수 있습니다. 네이티브 PreLogin도 받은 claim 뒤에 계정 세션을 재검증하지 않습니다. 전체 로그아웃과 개별 로그아웃을 혼동하면 안 됩니다.

보완: 세션 폐기와 티켓 폐기를 같은 트랜잭션으로 묶고, 소비 시 현재 참가·역할·세션 세대도 검사해야 합니다.

### F04 — 진행 중 heartbeat가 끊긴 서버가 동시 경기 자리를 계속 점유함 (P1)

근거: [GameServerDemandManager.cs:174](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/GameServerDemandManager.cs:174>), [GameServerDemandManager.cs:190](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/GameServerDemandManager.cs:190>).

ObservedMatch 이후에는 준비 제한 시간이 적용되지 않습니다. 프로세스가 살아 있지만 게임 스레드가 멈춰 heartbeat가 사라진 경우, HasExited는 false이고 복귀 ready 신호도 오지 않아 관리 목록과 동시 경기 한도를 계속 점유합니다. 정상 프로세스가 종료된 경우의 정리는 이미 있습니다.

보완: 준비/입장 제한 시간과 별도로 마지막 정상 heartbeat 시각 기반의 경기 중 watchdog을 두고, 충분한 유예 후 방·슬롯·프로세스를 일관되게 정리해야 합니다.

### F05 — 다음 맵에서 이전 맵 경기 기록과 시간이 초기화됨 (P2)

근거: [MultiplayTestGameMode.cpp:1879](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp:1879>), [MultiplayTestGameMode.cpp:2174](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp:2174>), [MultiplayTestGameMode.cpp:2646](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp:2646>), [MultiplayTestGameMode.cpp:2692](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp:2692>).

맵마다 FMultiplayTestServerPlayerRecord를 새로 만들고 경기 시작 시각도 다시 잡습니다. 포획·구출 횟수와 최초 포획 등은 이전 값을 전달하지 않습니다. 타임라인 PreviousLevelsSeconds만 이어지며 실제 결과/리더보드 시간에는 쓰이지 않습니다. 여러 맵으로 구성된 한 판의 결과가 마지막 맵 기준으로 축소됩니다. 다음 맵 데스카운트 초기화 여부는 설계 선택이므로 이 결함에 포함하지 않았습니다.

보완: MatchId에 묶인 서버 경기 기록을 맵을 넘어 보존하고 시간 기준도 경기 전체로 통일해야 합니다. AI 명령 초기화와 경기 성적 초기화를 분리해야 합니다.

### F06 — 맵 이동 실패 후 암전·입력·상태를 되돌리지 않음 (P2)

근거: [ProjectProject01LevelTransitionTrigger.cpp:280](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LevelTransitionTrigger.cpp:280>), [ProjectProject01LevelTransitionTrigger.cpp:333](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LevelTransitionTrigger.cpp:333>), [MultiplayTestGameMode.cpp:2570](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp:2570>).

이동 전에 생존자 상태·입력 잠금·암전·전환 타임라인을 바꿉니다. ServerTravel이 false면 트리거 충돌만 다시 켜고 나머지를 복원하지 않습니다. OpenLevel/비동기 여행 실패도 통합 복구 핸들러가 없습니다. 잘못된 목적지나 이동 거부 시 현재 맵에서 복구되지 않는 경로입니다.

보완: 목적지 유효성 검사, 전환 스냅샷, 여행 실패 이벤트와 제한 시간, 상태·시야·입력의 단일 롤백 경로가 필요합니다.

### F07 — 싱글 체크포인트는 AI의 저장 당시 행동을 복원하지 못함 (P2)

근거: [ProjectProject01SaveSubsystem.cpp:444](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SaveSubsystem.cpp:444>), [ProjectProject01SaveSubsystem.cpp:525](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SaveSubsystem.cpp:525>), [BTService_MannequinAI.h:53](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Public/BTService_MannequinAI.h:53>).

저장은 액터와 컴포넌트의 SaveGame 속성 위주입니다. 별도 AIController/Blackboard/BT 서비스의 발견 이력, 분산 목적지, 진행 중 영역 전환과 일반 멤버로 된 명령은 빠집니다. 적의 위치를 복원해도 추격 중이던 적이 다시 발견부터 시작하는 등 행동 상태가 달라집니다. 요청했던 ‘그 시점 그대로’의 저장은 현재 완성된 것으로 볼 수 없습니다.

보완: 싱글 AI 전용 명시적 스냅샷과 복원 순서를 만들고, 추격·분산 추적·후퇴·명령·타이머가 진행 중인 저장/로드를 확인해야 합니다. 멀티 AI와는 분리해야 합니다.

### F08 — 백업 저장 실패를 무시한 채 원본을 덮어씀 (P2)

근거: [ProjectProject01SaveSubsystem.cpp:114](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SaveSubsystem.cpp:114>), [ProjectProject01SaveSubsystem.cpp:119](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SaveSubsystem.cpp:119>).

기존 정상 저장을 백업하는 SaveGameToSlot 반환값을 검사하지 않습니다. 백업도 없거나 손상된 상태에서 디스크 부족·권한·잠금으로 백업 쓰기가 실패한 뒤 원본 쓰기까지 실패하면 마지막 정상 저장을 잃을 수 있습니다. 항상 손실된다는 의미는 아닙니다.

보완: 백업 성공을 확인하고, 임시 파일 검증 후 교체하는 저장 절차가 필요합니다. 단순 불러오기 메타데이터 갱신도 불필요한 전체 저장 재쓰기를 줄이는 편이 안전합니다.

### F09 — 최초 배포 런처의 ‘현재 버전 실행’ 경로 오류 (P2)

근거: [Program.cs:634](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01ClientLauncher/Program.cs:634>), [Program.cs:640](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01ClientLauncher/Program.cs:640>).

로컬 업데이트 완료 매니페스트가 없는 최초 배포에서는 EnumerateFiles가 찾은 절대 경로를 상대 경로 전용 검사에 넣어 거부합니다. 게임 실행 파일은 있어도 런처 버튼이 실패할 수 있습니다. 온라인 업데이트 후 로컬 매니페스트가 생긴 경우와 구별해야 합니다.

보완: 발견한 파일을 설치 루트 기준 상대 경로로 정규화하거나 검증된 절대 경로 전용 실행 경로를 사용해야 합니다.

### F10 — 다운로드 캐시가 다음 업데이트에 게시되어 전체 클라이언트가 거부함 (P2)

근거: [Program.cs:501](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01BuildServerHelper/Program.cs:501>), [Program.cs:537](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01ClientLauncher/Program.cs:537>).

런처는 .update-cache를 허용하지 않지만 게시 도우미는 .update-temp/.update-backup만 제외합니다. 게시 대상 Windows 폴더에서 런처를 실행해 중단된 캐시가 남은 뒤 재게시하면 금지 경로가 매니페스트에 들어갑니다.

보완: 런처 작업 폴더를 공통 규칙으로 모두 제외하고 게시 직전에 소비자와 동일한 매니페스트 검사를 수행해야 합니다.

### F11 — 패키징·게시·임시 EOS 설정 변경이 동시에 실행될 수 있음 (P2)

근거: [Program.cs:148](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01BuildServerHelper/Program.cs:148>), [Program.cs:249](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01BuildServerHelper/Program.cs:249>), [Program.cs:294](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01BuildServerHelper/Program.cs:294>), [Program.cs:533](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01BuildServerHelper/Program.cs:533>).

누른 버튼 하나만 비활성화하여 다른 패키징/게시 작업은 동시에 시작할 수 있습니다. 동일한 아카이브와 DefaultEngine.ini를 수정하므로 UAT 충돌, 매니페스트와 실제 파일 불일치, 임시 자격 증명 설정 복원 순서 경합이 생길 수 있습니다.

보완: 프로젝트 단위 공통 작업 잠금, 불변 게시 스냅샷, 임시 설정 소유권과 종료 시 정리 절차가 필요합니다.

### F12 — 업데이트 경로 검사가 junction/symlink의 실제 목적지를 확인하지 않음 (P2·조건부 보안)

근거: [Program.cs:594](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01ClientLauncher/Program.cs:594>), [ClientUpdateEndpoints.cs:38](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/ClientUpdateEndpoints.cs:38>).

문자열상 설치/게시 루트 내부인지만 검사합니다. 루트 안에 미리 만들어진 junction이 외부를 가리키면 실제 파일 읽기·쓰기는 루트 밖으로 나갈 수 있습니다. 원격 사용자가 임의로 junction을 생성할 수 있다는 뜻은 아니며, 로컬 파일/게시 경로가 이미 오염된 경우입니다. 이전 검토에서 보고한 항목입니다.

보완: reparse point를 거부하거나 최종 실제 경로를 검사하고 게시 다운로드는 매니페스트 허용 목록으로 한정해야 합니다. 격리된 이전 PathAudit fixture에서 경로 허용을 확인했으며 실제 사용자 파일을 덮어쓰지 않았습니다.

### F13 — 업데이트 설치 중 강제 종료 후 자동 복구 정보가 없음 (P2)

근거: [Program.cs:300](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01ClientLauncher/Program.cs:300>), [Program.cs:308](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01ClientLauncher/Program.cs:308>), [Program.cs:331](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01ClientLauncher/Program.cs:331>).

일반 예외는 메모리의 committed 목록으로 롤백하지만 런처 강제 종료·전원 손실은 catch를 실행하지 않습니다. 파일 교체 중단 후 다음 실행에서 읽는 영속 설치 저널/복구 절차가 없어 신·구 파일이 섞인 상태가 남을 수 있습니다. 이전 검토에서 보고한 항목입니다.

보완: 교체 전 영속 저널, 단계별 원자적 갱신, 다음 실행 복구 후 실행 허용 절차가 필요합니다.

### F14 — 음성 권한 거부가 403이 아니라 500으로 처리됨 (P2)

근거: [VoiceEndpoints.cs:58](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/VoiceEndpoints.cs:58>), [Program.cs:52](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/Program.cs:52>).

일부 잘못된 방/참가자 요청에 Results.Forbid()를 반환하지만 인증 서비스/기본 forbid scheme이 등록되어 있지 않습니다. 의도한 권한 오류 대신 서비스 미등록 예외가 발생하는 경로입니다. 정상 음성 토큰 요청이 전부 실패한다는 의미는 아닙니다.

보완: 직접 구조화된 403을 반환하거나 실제 ASP.NET 인증 체계와 forbid scheme을 구성해야 합니다.

### F15 — 설문 날짜 입력으로 전체 통계가 과도하게 커지거나 실패함 (P2)

근거: [SurveyEndpoints.cs:53](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/SurveyEndpoints.cs:53>), [SurveyEndpoints.cs:538](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/SurveyEndpoints.cs:538>).

응답자의 submittedAtUtc를 거의 그대로 저장하고 통계가 최솟날부터 최댓날까지 매일 전개합니다. 극단적 날짜 하나로 수백만 날짜를 만들거나 날짜 증가 overflow/SQL 시간 변환 NULL을 유발할 수 있습니다. 로그인한 응답자라도 제출 시각을 신뢰하면 안 됩니다.

보완: 서버 수신 시각을 기준으로 저장하고 허용 시각 범위와 통계 조회 기간을 제한해야 합니다.

### F16 — 관리자 PowerShell 스크립트의 예약 점검 규약이 UI/API와 다름 (P2)

근거: [ProjectProject01Admin.ps1:25](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/ProjectProject01Admin.ps1:25>), [ProjectProject01Admin.ps1:43](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/ProjectProject01Admin.ps1:43>), [ServiceAdministration.cs:479](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/ServiceAdministration.cs:479>).

스크립트의 종료 시각만 보내기는 현재 API의 시작/종료 쌍 검증에 걸립니다. 예약 점검 중 공지만 수정하면 effective maintenance 값을 manual maintenance로 보내 예약 종료 후에도 수동 점검이 남을 수 있습니다. 도우미의 모든 예약 UI가 깨졌다는 주장은 아닙니다.

보완: 수동 상태와 예약 계산 상태를 분리하고 스크립트도 현재 API 계약에 맞춰 갱신해야 합니다.

### F17 — 백엔드 비정상 재시작 뒤 진행 방·회원 상태가 남음 (P2)

근거: [GameServerDemandManager.cs:55](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/GameServerDemandManager.cs:55>), [LobbyEndpoints.cs:1815](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/LobbyEndpoints.cs:1815>), [ProjectProject01GameInstance.cpp:1441](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp:1441>).

관리 세션 목록은 메모리이고 Started 방은 일반 오래된 대기방 정리 대상이 아닙니다. 백엔드가 갑자기 재시작하면 세션 소유권을 잃고 이전 방/참가 상태가 남을 수 있습니다. 기존 UE 서버에는 heartbeat 실패 후 30초 자기 종료가 있으므로 ‘프로세스가 영원히 남음’으로 보고하지 않았습니다.

보완: 시작 시 DB의 진행 방·슬롯과 실제 관리 인스턴스를 대조하여 재인수하거나 명시적 중단/정리해야 합니다.

### F18 — 사용하지 않는 저장된 가상 네트워크 IP 때문에 백엔드 전체 기동 실패 가능 (P2)

근거: [Program.cs:617](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01BuildServerHelper/Program.cs:617>), [Program.cs:671](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01BuildServerHelper/Program.cs:671>).

현재 노트북에 존재하는지 확인하지 않은 저장된 25.x 주소를 수신 URL에 넣습니다. 어댑터가 꺼지거나 주소가 바뀌면 비로컬 주소 바인딩이 실패하여 localhost/LAN까지 기동하지 못할 수 있습니다.

보완: 실제로 존재하는 로컬 인터페이스만 바인딩하고, 누락된 가상 어댑터는 경고 후 나머지 주소로 기동해야 합니다.

### F19 — 로그인 중 변경한 서버 IP가 저장값과 실제 접속값을 다르게 만듦 (P2)

근거: [ProjectProject01LoginWidget.cpp:4290](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp:4290>), [ProjectProject01LoginWidget.cpp:4464](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp:4464>), [ProjectProject01GameInstance.cpp:1858](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp:1858>).

IP 입력칸은 활성화되어 있고 설정값을 먼저 저장하지만 로그인 중 실제 주소 변경은 거부합니다. UI는 로그인 상태에서 이 실패를 숨기고 적용 성공처럼 처리해, 표시·저장 IP와 실제 인증/로비 API 주소가 달라집니다.

보완: 접속 중에는 해당 입력만 잠그거나 ‘다음 로그인부터 적용’으로 명시하고 실제 적용 성공 전에 저장하지 않아야 합니다.

### F20 — 리더보드 탭을 빠르게 바꾸면 이전 결과를 다른 열로 보여줌 (P2)

근거: [ProjectProject01LoginWidget.cpp:6449](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp:6449>), [ProjectProject01LoginWidget.cpp:6374](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp:6374>), [ProjectProject01LobbySubsystem.cpp:370](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LobbySubsystem.cpp:370>).

한 요청 중 새 조회는 버리지만 선택 역할/팀 상태는 먼저 바뀝니다. 늦게 온 마네킹 결과를 생존자 열로 그리는 등 결과와 탭이 어긋나며 최신 선택을 재조회하지 않습니다.

보완: 응답에 요청 필터/세대를 묶고 일치하지 않는 응답을 버리며 마지막 선택 하나를 예약 조회해야 합니다.

### F21 — 파트너 AI의 끼임 복구 시간이 계속 초기화됨 (P2)

근거: [HelperRearGuardAIController.cpp:119](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/HelperRearGuardAIController.cpp:119>), [HelperRearGuardAIController.cpp:153](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/HelperRearGuardAIController.cpp:153>).

기본 0.25초 MoveTo 재요청 때 실제 진행 시각도 덮어써서 움직이지 않아도 기본 1.5초 끼임 제한에 도달하지 못합니다.

보완: 마지막 실제 이동/목표 접근 시각과 마지막 경로 요청 시각을 분리해야 합니다.

### F22 — 일부 피격·사망·점멸 연출에 접근성 설정이 적용되지 않음 (P2)

근거: [PlayerCharacter.cpp:1638](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp:1638>), [PlayerCharacter.cpp:1733](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp:1733>), [MannequinReactiveLight.cpp:285](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MannequinReactiveLight.cpp:285>).

생존자 적색 VFX와 카메라 직접 변환, 마네킹 반응 조명의 빠른 점멸은 로컬 번쩍임/흔들림 배율을 읽지 않습니다. StopAllCameraShakes는 직접 카메라 회전을 막지 못합니다. 심박 VFX 등 일부는 정상 연결되어 있으므로 전체 접근성 설정이 무효라는 의미는 아닙니다.

보완: 각 연출에서 로컬 flash/shake/distortion 값을 읽고, 0 설정의 대체 연출을 제공해야 합니다. 조명은 클라이언트별 표현만 완화하여 규칙은 유지할 수 있습니다.

### F23 — 신음·쓰러짐·포획 합성음이 끝난 뒤에도 무음 처리를 계속함 (P2)

근거: [PlayerHeartbeatComponent.cpp:293](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/PlayerHeartbeatComponent.cpp:293>), [PlayerHeartbeatComponent.cpp:376](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/PlayerHeartbeatComponent.cpp:376>), [PlayerHeartbeatComponent.cpp:456](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/PlayerHeartbeatComponent.cpp:456>).

소리 종료는 내부 활성 플래그만 false로 바꿉니다. bAutoDestroy=false이고 개별 종료 Stop이 없어 액터가 살아 있는 동안 무음 버퍼/콜백 작업이 남습니다. 계속 소리가 들린다는 의미는 아닙니다. 포획 합성음의 경기 종료 Stop과 발소리 idle Stop은 이미 있습니다.

보완: 오디오 스레드 종료 신호를 게임 스레드에 전달하여 재생 완료 Stop을 수행하고 다음 재생 때만 다시 켜야 합니다.

### F24 — 중간에 종료된 진단이 Pass가 될 수 있음 (P2)

근거: [ProjectProject01DiagnosticsSubsystem.cpp:205](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01DiagnosticsSubsystem.cpp:205>), [ProjectProject01DiagnosticsSubsystem.cpp:329](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01DiagnosticsSubsystem.cpp:329>).

월드 종료 때 Inconclusive 사유를 전달하지만 최종 판정에서 그 사유를 무시하고 수치 조건만으로 Pass를 쓸 수 있습니다. 짧게 실행 후 맵 이동한 진단이 정상 완주로 오인될 수 있습니다.

보완: 정상 완료 표식·중단 사유를 숫자 기준보다 우선하고 중단은 Inconclusive로 남겨야 합니다.

### F25 — 진단 대시보드가 동시에 온 표준 출력/오류를 안전하게 수집하지 않음 (P2)

근거: [Program.cs:930](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01DiagnosticsDashboard/Program.cs:930>).

별도 프로세스 읽기 콜백 둘이 같은 StringBuilder를 동기화 없이 수정합니다. 타이밍에 따라 로그 손상/예외가 가능하며 창 종료 시 자식 빌드 취소 연결도 없습니다.

보완: 스트림별 ReadToEndAsync 또는 lock/channel로 모으고 제한 시간·취소·창 종료 시 자식 프로세스 정책을 연결해야 합니다.

### F26 — 경기 결과의 서버 제출이 한 번 실패하면 복구 경로가 없음 (P2)

근거: [ProjectProject01GameInstance.cpp:1736](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp:1736>), [MultiplayTestGameMode.cpp:2799](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp:2799>), [MultiplayTestGameMode.cpp:2819](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp:2819>).

결과 POST는 한 번이며 timeout/일시적 5xx 뒤 false로 확정합니다. 서버 재전송 큐가 없으므로 백엔드가 잠깐 느린 시점에 끝난 성공 경기를 나중에 등록할 근거가 사라질 수 있습니다. 결과 화면 자체는 즉시 표시하도록 이미 분리되어 있습니다.

보완: MatchId/UserId 기준 멱등성을 전제로 제한된 재시도와 서버 결과 outbox를 두고, UI의 검증 대기/실패를 구분해야 합니다.

### F27 — 진행 경기에서 전원이 방 복귀 API를 호출하면 서버 종료 확인 없이 초기화됨 (P2)

근거: [LobbyEndpoints.cs:355](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/LobbyEndpoints.cs:355>), [LobbyEndpoints.cs:1988](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/LobbyEndpoints.cs:1988>), [GameServerDemandManager.cs:181](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/GameServerDemandManager.cs:181>).

현재 참가자 모두가 return을 요청하면 경기의 Started·역할·MatchId가 초기화됩니다. 실제 DS가 승패/기권을 확정했는지 검사하지 않습니다. 한 명의 요청만으로 전체 경기를 초기화한다는 뜻은 아니지만 API와 서버 권위 상태가 어긋날 수 있습니다.

보완: 결과 확정 또는 명시적 정상 이탈/기권 절차를 서버 증거와 연결하고 방 재사용을 별도 상태 전이로 처리해야 합니다.

### F28 — 동시에 실패한 로그인이 계정 실패 횟수를 덮어쓸 수 있음 (P2)

근거: [Program.cs:347](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/Program.cs:347>), [Program.cs:715](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/Program.cs:715>).

실패 횟수를 읽은 뒤 별도로 갱신하므로 병렬 실패가 같은 이전 값을 읽고 동일 횟수를 저장할 수 있습니다. 설정한 계정 잠금 횟수가 실제 실패 수보다 적게 누적됩니다. IP 제한이 별도로 있지만 계정 기준 잠금의 정확성을 대신하지는 못합니다.

보완: 원자적 증가나 행 잠금 트랜잭션으로 실패 횟수와 잠금 시각을 갱신해야 합니다.

### F29 — null 표시 이름 회원가입 요청이 400 대신 500이 됨 (P3)

근거: [Program.cs:274](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/Program.cs:274>).

JSON에서 displayName을 명시적으로 null로 보내면 로그인 검증용 null 허용 경로를 통과한 뒤 Trim에서 예외가 날 수 있습니다. 정상적인 회원가입이 항상 실패하는 문제는 아닙니다.

보완: 등록/로그인 입력 모델 검증을 분리하고 누락/null/길이 오류를 구조화된 400으로 반환해야 합니다.

### F30 — 역할 안내의 이동키 표기가 실제 재설정 키와 다름 (P3)

근거: [ProjectProject01LoginWidget.cpp:2313](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp:2313>), [ProjectProject01LoginWidget.cpp:2336](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp:2336>).

이동키는 W/A/S/D로 고정되어 있지만 게임은 이동키 재설정을 지원합니다. 달리기·핑·보이스처럼 실제 활성 매핑에서 읽어야 합니다.

보완: 가이드의 모든 변경 가능한 키를 동일한 실제 매핑 조회 함수로 출력해야 합니다.

## 별도 검증·정책 결정이 필요한 보완점

아래는 확정 결함과 구분한 조건부 위험/검증 공백입니다. 무조건 기능을 새로 만들자는 목록이 아닙니다.

1. **개발용 미검증 점수 옵션과 HTTPS 프록시.** [ProjectProject01MySql.ps1:247](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/ProjectProject01MySql.ps1:247>), [LobbyEndpoints.cs:1340](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/LobbyEndpoints.cs:1340>): 초기화 스크립트가 미검증 점수 허용을 켜는 설정을 만들고, API는 요청 소켓의 loopback만 확인합니다. 같은 PC의 HTTPS 프록시를 통하면 외부 요청도 loopback처럼 보이는 조건이 생깁니다. 현재 직접 IP 테스트 접속과는 다른 배치 조건입니다. 공개 운영에서는 기본 false와 신뢰 프록시 정책을 명시해야 합니다.

2. **학교처럼 여러 사용자가 한 공인 IP를 쓰는 환경.** [Program.cs:107](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01Backend/Program.cs:107>): 인증/티켓 등 일부 요청 예산은 여전히 공유 IP 단위입니다. 인증 10회/분, 티켓 30회/분 같은 한도는 여러 정상 사용자의 요청이 합쳐져 제한될 수 있습니다. 3명 플레이가 항상 막힌다는 의미는 아닙니다. 인증된 요청은 계정 기준 예산과 넓은 IP 남용 제한을 분리하는 편이 적합합니다.

3. **크래시 직후 빠른 재실행의 증거 수집 유예.** [ProjectProject01GameInstance.cpp:804](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp:804>): 네이티브 crash 디렉터리가 아직 보이지 않으면 영구 Excluded로 분류합니다. 실제 UE 크래시 파일 작성 순서에 따라 유예가 필요한지 검증해야 합니다. 15초 업로드 대기는 이 분류 뒤에 있어 분류 시점 문제를 막지 못합니다. 크래시를 일부러 내는 테스트는 이번에 하지 않았습니다.

4. **저장 파일과 게임 데이터 버전의 호환 정책.** [ProjectProject01SaveSubsystem.cpp:144](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SaveSubsystem.cpp:144>), [ProjectProject01SaveSubsystem.cpp:346](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SaveSubsystem.cpp:346>): AppliedGameDataVersion은 기록하지만 불러올 때 현재 데이터와 대조하지 않습니다. 모든 데이터 변경을 차단할 필요는 없으나 호환/마이그레이션/경고 정책이 필요합니다. 액터 ID 중복·런타임 생성 이름 변경과 저장된 맵 이동 실패 후 PendingLoad 정리도 실패 경로 시험 대상입니다.

5. **맵 이름에 의존하는 연결 실패 처리.** [ProjectProject01GameInstance.cpp:1279](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp:1279>): MultiplayTest 이름 또는 아직 남은 접속 티켓으로 멀티 연결 실패를 구분합니다. 현재 접두어가 붙은 맵에는 맞지만 다른 이름의 멀티 맵으로 확장할 때 안전 로그아웃이 누락될 수 있습니다. 이름 대신 실제 네트워크/매치 상태로 판정하는 편이 안전합니다.

6. **동일 프로세스 다중 PIE의 네트워크 보안 전역 상태.** [ProjectProject01GameInstance.cpp:390](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp:390>): pending ticket/key와 claim 저장소 일부가 GameInstance 밖 정적 상태입니다. 실제 인증을 거치는 여러 PIE 클라이언트를 한 프로세스에서 실행할 때 분리 여부를 검증해야 합니다. 보통의 개별 패키징 클라이언트마다 프로세스가 나뉜 상황과 동일한 문제라고 단정하지 않았습니다.

7. **설문·가이드 진행 비동기 응답 수명.** [ProjectProject01GameInstance.cpp:944](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp:944>), [ProjectProject01AuthSubsystem.cpp:240](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Source/ProjectProject01/Private/ProjectProject01AuthSubsystem.cpp:240>): 로그아웃/계정 교체 뒤 설문 HTTP 완료를 화면 세대와 맞추는지, 가이드 진행 저장이 실패했을 때 다음 로그인 재노출/재시도 정책이 의도에 맞는지 추가 회귀 시험이 필요합니다. 사용자에게 실제로 잘못된 팝업이 뜬다고 확정 보고하지는 않았습니다.

8. **진단/스모크 통과의 의미를 제한해야 함.** [Program.cs:224](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01SmokeTest/Program.cs:224>): 현재 스모크는 서버 결과를 합성 제출하며 UE의 실제 이동·탈출·승패 코드를 운전하지 않습니다. [Program.cs:263](<C:/DSFD/ProjectDSFD/Source Code/ProjectProject01/Tools/ProjectProject01SmokeTest/Program.cs:263>)의 top 50 검색은 누적 데이터가 많으면 새 기록이 정상 등록되어도 못 찾을 수 있습니다. 테스트 DB 분리와 방금 생성한 기록 식별자로 조회하는 보완이 적합합니다. 과거 테스트 통과를 전체 게임 종단 검증 완료로 간주하면 안 됩니다.

## 철회·중복·과장하지 않은 부분

- /api/server/offline의 전체 슬롯 정리는 현재 ‘전체 게임 서버 중지’에서 호출하므로 개별 서버 종료 버그로 세지 않았습니다.
- 입력 잠금이 중첩된다는 사실만으로 정상 맵 이동이 항상 멈춘다고 보고하지 않았습니다. UE ClientRestart의 입력 초기화가 있어 호출 순서가 중요합니다. F02는 별도의 고정 인원 조건으로 교차 확인했습니다.
- 실제 EOS 초기화가 이번에도 실패한다고 단정하지 않았습니다. F14는 권한 거부 오류 경로입니다.
- 현재 다루는 IP는 사용자의 개발 접속 방식입니다. 이번 리뷰를 이유로 유료 클라우드·도메인 구매를 필수화하지 않습니다.
- 체크포인트가 위치를 저장하는 기능 자체가 없다고 하지 않았습니다. F07은 요청한 ‘AI 행동 시점까지 그대로’와의 차이입니다.
- 오디오 무음 콜백은 F23의 성능 문제이며, 계속 소리가 난다고 보고하지 않았습니다.
- junction/업데이트 전원 종료 복구, 여행 실패 복구, 저장 백업 실패는 이전 검토와 중복되는 항목이며 새 발견 수로 부풀리지 않았습니다.

## 수정 순서와 필요한 회귀 확인

1. F01~F04: 로그인·경기 진입·세션 회수 핵심 경로. 방 조회를 늦춘 암전 완료, 탈락자 이탈 후 다음 맵, 티켓 발급 후 개별 로그아웃, 진행 서버 heartbeat 정지 상황을 각각 검증.
2. F05~F08과 F26: 한 판/저장 데이터 연속성. 여러 맵을 거친 포획·시간, AI 추격 중 저장/불러오기, 디스크/백업 쓰기 실패, 결과 POST 일시 실패 확인.
3. F09~F13: 최초 배포·게시·설치 복구. 로컬 매니페스트 없는 배포, 중단 캐시 재게시, 동시 게시 차단, 격리 링크 폴더, 파일 교체 중 강제 종료 복구 확인.
4. F14~F20 및 F27~F30: API 오류·운영·UI 일관성. 권한 거부, 극단 날짜, 예약 점검, 재시작, IP 변경, 빠른 탭 전환, 입력 null/동시 실패 확인.
5. F21~F25: AI·접근성·오디오·진단 품질. 실제 끼임 회복, 접근성 0 배율, 소리 종료 후 활성 소스, 진단 중단 결과, stdout/stderr 동시 출력 확인.

이 순서는 새 시스템을 끝없이 늘리자는 뜻이 아니라 이미 있는 기능을 의도대로 마무리하는 순서입니다. 실제 수정 후에는 변경되는 클라이언트·DS·백엔드 범위를 다시 판단해야 하며 이번 검토 문서 생성만으로 재패키징할 필요는 없습니다.


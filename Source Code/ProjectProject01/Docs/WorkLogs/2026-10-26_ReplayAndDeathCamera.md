# 경기 리플레이와 데스캠 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`ReplayAndDeathCamera.md`](../ReplayAndDeathCamera.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: `UProjectMatchReplay`는 Unreal `UReplaySubsystem`과 로컬 `LocalFileNetworkReplayStreaming`으로 실제 네트워크 리플레이를 녹화한다. 서버나 다른 PC를 되감지 않는다. 클라이언트가 실제로 수신한 액터 상태가 녹화 범위이므로 서버 전체의 전지적 기록은 아니다. EOS 음성·인증 토큰·HTTP 계정 정보는 이 녹화 경로에 넣지 않는다.
- 기간: 2026-10-26 ~ 2026-10-26 (문서 변경 커밋 1개 기준)
- 결과·검증: 최신 타깃을 실제 DLL까지 링크했고 최종 통합 엔진 검사 53개가 전부 성공했다. 보고서는 `Saved/FeatureChecks/CaseFinalEngineAcceptance/index.json`이다. 일부 PIE 실행에는 기존 엔진·검사 월드 경고가 있으므로 경고 없는 검사로 표현하지 않는다.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- `UProjectMatchReplay`는 Unreal `UReplaySubsystem`과 로컬 `LocalFileNetworkReplayStreaming`으로 실제 네트워크 리플레이를 녹화한다. 서버나 다른 PC를 되감지 않는다. 클라이언트가 실제로 수신한 액터 상태가 녹화 범위이므로 서버 전체의 전지적 기록은 아니다. EOS 음성·인증 토큰·HTTP 계정 정보는 이 녹화 경로에 넣지 않는다.
- 서버가 확인한 경기 ID와 진행 단계에서만 자동 녹화하며, 심리스 맵 전환은 `RecordMapChanges`로 연결한다. 방에서 나온 뒤 로비에서만 재생할 수 있다. 재생 전용 컨트롤러에는 공격·빙의·게임 결과 제출 바인딩이 없다. 일시정지·시간 이동·배속·자유 관전·로비 복귀를 제공한다. 재생 초기화는 30초, 시간 이동은 10초 제한을 둔다. 파일명은 현재 프로토콜 접두사와 GUID만 허용하며 임의 경로/개발자 수동 녹화 파일은 다루지 않는다.
- `UProjectInstantReplay`는 현재 멀티 경기에서 수신한 캐릭터의 위치와 스켈레탈 포즈를 0.1초 간격으로 제한된 링 버퍼에 보관한다. 최대 60프레임, 24개 몸체, 몸체당 256개 본, 링 데이터 16MiB 제한이며 밀집된 장면에서는 오래된 프레임을 먼저 버린다. 메모리 크기를 제한하는 것으로, 실제 성능 비용을 증명한 것은 아니다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`ReplayAndDeathCamera.md`](../ReplayAndDeathCamera.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/FireShutterActor.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerInteractionComponent.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectAdaptiveBotPolicyTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectAnnouncementSubsystem.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCaseInvestigationRegion.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCaseRecordWidgets.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCaseRecords.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCaseRecordsTests.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 최고의 플레이는 기능 자체를 만들지 않는다. 저장 선택, 별도 하이라이트 파일/보관함도 없다. 기존 사망·탈출 연출과 결과 흐름을 유지한다. 데스캠은 사망 원인 확인을 위한 짧은 시각적 피드백이다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 개인 기록 → 리플레이에서 자동 녹화를 켤 수 있다. **기본값은 꺼짐**이며 패키징된 클라이언트에서 사용한다. 일반 에디터/무인 실행에서는 디스크 녹화를 하지 않는다. 파일 왕복 자동 검사만 별도 임시 폴더에 명시적으로 녹화를 허용하며 사용자 리플레이 폴더는 건드리지 않는다. 보관 한도는 이 버전의 파일 10개/총 2GB, 한 경기 최대 약 512MB/60분이다. 한도에 도달하면 새 녹화를 중지하며 기존 파일을 자동 삭제하지 않는다. 직접 삭제하고 다시 켜면 
- 서버가 확정한 본인 사망 RPC만 데스캠을 요청한다. 기존 사망 연출을 마친 뒤 사망 전 약 2.5초와 이후 약 1.5초를 별도의 미리보기 장면에서 재구성한다. 기록이 짧거나 유효하지 않으면 데스캠을 생략한다. 메모리에만 보관하고 파일로 저장하지 않는다.
- 미리보기에는 메시 컴포넌트만 생성한다. 원래 액터를 움직이거나 게임 상태를 복원하지 않는다. 물리·AI·오디오·내비게이션·서버 연결을 만들지 않으며, 재생되는 포즈에는 보간을 적용한다. 카메라가 벽 안쪽에 놓이지 않도록 현재 맵의 충돌로 위치를 보정한다. 주변 정적 메시/인스턴스 복사는 최대 400개, 반경 약 45m이다. **조명과 가변 환경 상태는 간소화된 재구성이므로 원본 화면을 픽셀 단위로 재생하는 영상은 아니다.** 이 방식은 전체 경기 네트워크 리플레
- 건너뛰기를 제공한다. 끝나면 살아 있는 동료의 기존 관전 시점으로 돌아간다. 이미 경기가 끝났으면 월드를 다시 드러내지 않고 결과 화면으로 연결한다. 결과 대기는 최대 10초로 제한한다. 맵 전환·로그아웃·컨트롤러 소멸 시 예약과 미리보기를 취소하고 렌더 상태를 복구한다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 최신 타깃을 실제 DLL까지 링크했고 최종 통합 엔진 검사 53개가 전부 성공했다. 보고서는 `Saved/FeatureChecks/CaseFinalEngineAcceptance/index.json`이다. 일부 PIE 실행에는 기존 엔진·검사 월드 경고가 있으므로 경고 없는 검사로 표현하지 않는다.
- `ProjectProject01.Replays.BoundsAndIsolation`: 파일명/경로 제한, 기본 녹화 꺼짐, 잘못된 포즈·시간·FOV 거부, 60프레임/16MiB 링 제한과 취소 시 예약 해제 등 경계 검사 통과.
- `ProjectProject01.Editor.Replay.RoundTrip`: 실제 로컬 네트워크 리플레이 파일 녹화→목록→재생→일시정지/2배속→시간 이동→로비 복귀→삭제 통과. 검사 전용 GUID 폴더만 사용하고 정리했다.
- `ProjectProject01.Editor.DeathCamera.Complete`, `.Logout`, `.Travel`: 별도 미리보기 재생 후 정상 완료/로그아웃/맵 전환에서 월드 렌더 상태와 예약 복구 통과. 맵 전환 뒤 `bDisableWorldRendering`이 남아 새 맵이 검게 되는 결함을 수정했다. 소속 GameInstance의 맵 로드 직전에 미리보기를 정리한다.
- 3클라이언트 맵 전환·관전 공유 연출·결과 암전 인계 회귀 검사도 최종 보고서에서 통과했다. 과거의 리플레이 통신/결과/로딩 RPC가 실제 계정·EOS·결과 제출에 영향을 주지 않도록 재생 분기를 유지한다.
- 이 결과는 동일 PC의 에디터/격리 환경 실행 검사다. 패키징된 다중 PC 경기 전체, 실제 EOS 마이크 통화, 모든 실제 맵/환경의 장시간 리플레이 비용까지 검증한 것은 아니다. 사용자 요청에 따라 패키징·운영 DB 변경·운영 서버 재시작은 실행하지 않았다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 53개가 전부 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | `UProjectMatchReplay`는 Unreal `UReplaySubsystem`과 로컬 `LocalFileNetworkReplayStreaming`으로 실제 네트워크 리플레이를 녹화한다. 서버나 다른 PC를  | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 재생 UI는 가로 시간 이동, 앞뒤 10초 이동, 일시정지/재생, 0.5·1·2배속을 제공한다. 뒤로 보기는 해당 시점으로 이동해 다시 앞으로 재생하는 방식이다. 시작·끝 구간 지정 반복 재생은 구현하지 않았다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `e386c5be` | 2026-10-26 | maru3172 | Bot Mode Win Rate Adjustment |

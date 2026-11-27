# 시연용 패키징 런타임 점검 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`PackagingRuntimeReadiness.md`](../PackagingRuntimeReadiness.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 기본 맵 6개와 `MultiplayTest1`, `MultiplayTest2`를 공통 쿠킹 목록에 포함한다.
- 기간: 2026-10-19 ~ 2026-10-19 (문서 변경 커밋 1개 기준)
- 결과·검증: DLL 파일 검사 통과는 실제 EOS 인증·음성 통신 성공을 의미하지 않는다.
- 현재 상태: 원 문서상 남은 항목 기재 없음(미확인)

## 요청·목표·범위 (9-2)

- 기본 맵 6개와 `MultiplayTest1`, `MultiplayTest2`를 공통 쿠킹 목록에 포함한다.
- 한국어 UI 문구용 `Content/Localization`의 기존 스테이징을 유지한다.
- 런타임 설치 파일을 포함하는 것과 설치하는 것은 다르다. 새 Windows PC에서 VC++ 런타임이 없으면
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`PackagingRuntimeReadiness.md`](../PackagingRuntimeReadiness.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MannequinAIController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerHeartbeatComponent.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01AuthSubsystem.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CheckpointAI.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CheckpointAI.h` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CrashFiles.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CrashFiles.h` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LevelTransitionTrigger.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LobbySubsystem.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- `IncludePrerequisites=True`, 도우미 UAT 명령 `-prereqs -crashreporter`로 필수 런타임 설치 파일 및 크래시 리포터를 포함한다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 이 보완에서는 게임 쿠킹·패키징·업데이트 게시·서버 실행·런타임 설치를 하지 않는다.
- `/Game/MyProject/Data`의 세 데이터테이블 `.uasset`을 쿠킹한다. 개발용 원본 CSV를 별도 배포하지 않는다.
- VFX 폴더와 선택한 추가 심박 머티리얼의 기존 쿠킹 경로를 유지한다.
- 도우미는 선택한 UE 5.8 엔진의 VC++ x64/arm64, GameInput 설치 파일이 없으면 패키징 전에 안내하고 중단한다.
- `Engine/Extras/Redist/en-us/vc_redist.x64.exe`를 한 번 설치해야 한다. 관리자 승인을 우회하거나 자동 설치하지 않는다.
- DLL 복사·손상·아키텍처·엔진 없는 경로 복구를 검사한다. 테스트 DLL은 실행하지 않는다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 복사 시 Windows x64 DLL 형식을 검사하고 SHA-256으로 복사를 검증한다.
- DLL 파일 검사 통과는 실제 EOS 인증·음성 통신 성공을 의미하지 않는다.
- 실제 패키징과 시연 PC의 그래픽 드라이버·네트워크·마이크 권한·EOS 통신·다중 PC 플레이 검증은 별도다.
- 2026-10-19 확인: 새 패키징 검사 21개 및 기존 검사와 합친 162개가 통과했다.
- 별도 이어받기 검사 17개와 설치 롤백 검사도 통과했고, 백엔드·도우미·런처·검사 프로그램 빌드는 경고/오류 0개였다.
- 실제 EOS/XAudio DLL 두 개를 서버 전용 폴더에 보관했으며 백엔드 빌드 출력과 SHA-256 일치를 확인했다.
- 두 DLL의 향후 publish 복사 메타데이터도 확인했다. publish·게임 패키징·운영 서버 실행·EOS 네이티브 초기화는 실행하지 않았다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 21개 및 기존 검사와 합친 162개가 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |
| 검사 통과 수 (높을수록 좋음): 17개와 설치 롤백 검사도 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 기본 맵 6개와 `MultiplayTest1`, `MultiplayTest2`를 공통 쿠킹 목록에 포함한다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 원 문서에 기재 없음(미확인)

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `21fba4d8` | 2026-10-19 | maru3172 | Fix missing improvements |

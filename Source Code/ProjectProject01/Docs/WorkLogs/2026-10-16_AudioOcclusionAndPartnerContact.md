# 소리 차폐와 파트너 후퇴 접촉 수정 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`AudioOcclusionAndPartnerContact.md`](../AudioOcclusionAndPartnerContact.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 방의 밀폐 여부 자체로 음량을 낮추지 않는다. 소리 발생점과 각 청취자 사이를 실제 메시 표면 기준으로 검사한다.
- 기간: 2026-10-16 ~ 2026-10-16 (문서 변경 커밋 1개 기준)
- 결과·검증: 가려진 음량 배율은 0.25, 저역 통과는 600Hz, 전환은 0.15초다. 기존 0.4/1400Hz보다 저음 위주 발소리의 벽 차폐를 구분하기 쉽게 한다. 30m 안이어도 벽에 막히면 차폐는 적용된다. 열린 공간의 거리 감쇄는 음량만 바꾸고 벽 필터를 적용하지 않는다.
- 현재 상태: 원 문서상 남은 항목 기재 없음(미확인)

## 요청·목표·범위 (9-2)

- 방의 밀폐 여부 자체로 음량을 낮추지 않는다. 소리 발생점과 각 청취자 사이를 실제 메시 표면 기준으로 검사한다.
- 같은 방/탁 트인 공간에서 사이에 막는 표면이 없다면 추가 차폐는 없다. 거리 배율은 30m까지 1, 30~60m에서 선형으로 감소하며 60m에서 0이 된다. 예: 30m=1, 45m=0.5, 60m=0. 최종 청감은 개별 효과음 자체 음량과 전체/효과음 설정도 함께 따른다.
- 벽/닫힌 문 너머, 다른 밀폐 공간 밖에서 듣거나 바닥을 사이에 둔 다른 층이라면 차폐를 적용한다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`AudioOcclusionAndPartnerContact.md`](../AudioOcclusionAndPartnerContact.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/FootstepSynthComponent.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/HelperRearGuardCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MannequinAICharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerHeartbeatComponent.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SpatialAudio.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01SpatialAudioTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Public/FootstepSynthComponent.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/HelperRearGuardCharacter.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/MannequinAICharacter.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/PlayerCharacter.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01SpatialAudio.h` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- PIE 콘솔 또는 출력 로그의 명령 입력란에 아래를 한 줄씩 실행한다. 새 디버그 유틸이 아니라 언리얼 기본 오디오 표시 기능이다. Shipping에는 이 표시를 넣지 않는다.
- 같은 거리(가능하면 30m 안)에서 문을 열고 닫아 비교한다. 벽 뒤인데도 초록색/1.0이면 청감 튜닝보다 실제 가림/충돌/음원 위치 문제부터 확인해야 한다. 숫자는 맞는데 차이가 작게 느껴지면 실제 음원 특성과 청감 튜닝 문제다. 발소리는 재생이 짧아서 표시도 잠깐 나타날 수 있다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 싱글과 멀티의 기존 공간 효과음에 공통 적용한다. 심장박동, UI, 음악, EOS 음성은 변경하지 않는다.
- 원본 사운드/Blueprint에 오래된 짧은 감쇄 범위가 있어도 런타임 사본/합성음은 동일한 30~60m 정책을 적용한다. Blueprint가 attenuation override나 공간음을 비활성화해 두었어도 재생 전에 복구한다. 복사된 비활성/빈 occlusion plugin 슬롯이 언리얼 기본 차폐 검사를 건너뛰지 않도록 런타임 슬롯을 제거한다. 원본 에셋은 변경하지 않는다.
- `Visibility=Block`과 Query Collision은 유지한다. 일체형 메시를 분리할 필요는 없다. 다만 `Use Simple Collision As Complex`는 복합 검사에서도 단순 충돌을 대신 사용하므로, 빈 방을 채우는 볼록 충돌이 있다면 메시의 Collision Complexity를 `Simple And Complex` 또는 적절한 `Use Complex Collision As Simple`로 바꿔야 한다. 이번 코드 변경은 원본 맵/메
- 예정된 NavMesh 후퇴 경로에 플레이어가 있다는 이유만으로 미리 밀거나 데스카운트를 차감하지 않는다.
- 프레임 사이에 플레이어를 지나친 경우도 이미 이동한 짧은 구간의 캡슐 sweep으로 접촉을 검사한다. 미래 경로는 검사하지 않는다.
- 다른 층은 3차원 캡슐 접촉으로 배제한다. 정지 상태나 10m 초과 순간 이동/불러오기는 밀침으로 취급하지 않는다.
- 같은 거리에서 마네킹 발소리를 직접 듣고 벽/닫힌 문을 사이에 뒀을 때와 비교한다. 같은 방 안이라면 주변 벽만 있다는 이유로 먹먹해지면 안 된다.
- 수정은 에디터 재실행 및 새 클라이언트 패키징 후 반영된다. 이번 작업은 원본 맵/에셋, 운영 DB, 실행 중인 서버를 임의로 변경하지 않는다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 가려진 음량 배율은 0.25, 저역 통과는 600Hz, 전환은 0.15초다. 기존 0.4/1400Hz보다 저음 위주 발소리의 벽 차폐를 구분하기 쉽게 한다. 30m 안이어도 벽에 막히면 차폐는 적용된다. 열린 공간의 거리 감쇄는 음량만 바꾸고 벽 필터를 적용하지 않는다.
- 끄기: `au.3dVisualize.Enabled 0`. 청감 A/B 비교가 필요하면 같은 위치에서 `au.DisableOcclusion 1`로 차폐만 임시 끄고, 반드시 `au.DisableOcclusion 0`으로 복구한다. 이 명령은 실제 에디터 맵에서 사용자가 확인하는 방법이며 이번 무음 자동 검사에서 청감을 검증한 것은 아니다.
- 최종 30m 정책 검증 결과: UE 에디터 및 Win64 Shipping 빌드 성공. `ProjectProject01.Audio` / `Helper` / `Operations` 자동 검사 10개 성공, 실패 0개. 결과는 `Saved/AudioDistance30mChecks/index.json`, 원본 로그는 `Saved/Logs/AudioDistance30mChecks.log`에 있다. 격리 테스트는 30m까지 거리 배율 1, 45m에서 0.5, 60m에서 0
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 10개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 방의 밀폐 여부 자체로 음량을 낮추지 않는다. 소리 발생점과 각 청취자 사이를 실제 메시 표면 기준으로 검사한다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
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
| `059a7c18` | 2026-10-16 | maru3172 | Sound Effect Overhaul |

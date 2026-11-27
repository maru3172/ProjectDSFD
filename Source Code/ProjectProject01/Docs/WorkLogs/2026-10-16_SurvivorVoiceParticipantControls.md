# 생존자 동료별 보이스 제어 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`SurvivorVoiceParticipantControls.md`](../SurvivorVoiceParticipantControls.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 동료별 설정은 계정·MatchId에 한정한다. 같은 경기의 맵 이동·음성 재연결·동료 재참가 시 유지/재적용하고, 경기 종료·로그아웃·다른 경기·다른 계정·마네킹 역할로 전환할 때 초기화한다. 장치/전체 보이스 수신 음량은 기존대로 로컬 PC에 저장한다.
- 기간: 2026-10-16 ~ 2026-10-16 (문서 변경 커밋 2개 기준)
- 결과·검증: Win64 Development Editor 및 Win64 Shipping 빌드 성공.
- 현재 상태: 원 문서상 남은 항목 기재 없음(미확인)

## 요청·목표·범위 (9-2)

- 동료별 설정은 계정·MatchId에 한정한다. 같은 경기의 맵 이동·음성 재연결·동료 재참가 시 유지/재적용하고, 경기 종료·로그아웃·다른 경기·다른 계정·마네킹 역할로 전환할 때 초기화한다. 장치/전체 보이스 수신 음량은 기존대로 로컬 PC에 저장한다.
- 목록은 실제 생존자 음성 채널에 참가한 사람만 보여준다. 백엔드가 확인한 계정 닉네임을 사용하며, 아직 이름을 받지 못한 경우에만 EOS 식별자 끝 6자리로 보조 표시한다.
- 우측 상단 기존 내 보이스 활성/비활성 표기는 그대로 유지한다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`SurvivorVoiceParticipantControls.md`](../SurvivorVoiceParticipantControls.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultGame.ini` (2)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01VersionContract.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01VoiceChatSubsystem.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01VoiceParticipantTests.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01VoicePreferences.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01VoiceSettingsWidget.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestGameMode.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestPlayerController.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01VoiceChatSubsystem.h` (2)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 경기 중 ESC → 환경설정 → 사운드 → **음성 채팅 장치·음량·마이크 테스트**에서 동료 생존자를 선택한다.
- **선택한 동료 음소거 / 해제**는 즉시 내 수신에만 적용한다.
- **선택한 동료 음량** 게이지는 0~100%로 즉시 적용한다. 음소거를 풀어도 이 음량은 유지된다. 0%는 별도의 음소거 체크 상태를 변경하지 않는다.
- 초기화는 장치·전체 수신 음량·동료별 음량/음소거를 즉시 기본값으로 돌린다. 장치와 전체 수신 음량을 바꾼 경우에는 기존 적용 버튼으로 저장한다. 돌아가기는 이미 즉시 적용된 동료별 설정을 취소하지 않는다.
- 동료가 없거나 채널 연결 전에는 선택/음소거/동료 음량을 비활성화한다. 작은 화면에서는 설정 내용을 스크롤할 수 있다.
- 아래 동료 상태는 EOS `IVoiceChatUser::IsPlayerTalking` 결과로 **말하는 중 / 대기**를 표시한다. F2/U를 누른 사실로 상대의 발화를 추측하지 않는다.
- HUD는 0.1초, 열린 설정의 동료 목록/상태는 0.2초마다 갱신한다. 선택은 참가자 순서가 바뀌어도 가능한 한 같은 ID로 유지한다.
- 목록·상태·설정 변경은 생존자 채널 가입 여부를 확인한다. 마네킹은 기존대로 이 채널에 가입하지 않고, 보이스 HUD도 숨긴다. 발화나 음소거 정보를 게임 RPC/복제 속성으로 추가 전송하지 않는다. 다른 생존자는 자신의 수신 설정에 영향을 받지 않는다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 기존 전체 보이스 수신 음량, 전체 음량/음소거/백그라운드 음소거를 대체하지 않는다. 동료별 음량은 서버가 계산한 거리 게인과 곱한다. 기본 3.5m 이내 100%, 18m 이상 무음이며 사이 구간은 점진 감쇄한다. 미구현인 EOS 위치 API 대신 실제 수신 음량에 적용한다. 자세한 내용과 검증 범위는 `VoiceSecurityAndWholeGameReview.md`를 참고한다.
- Win64 Development Editor 및 Win64 Shipping 빌드 성공.
- 기존 회귀 검사와 신규 보이스 검사를 합해 34개 성공, 실패/미실행 0개. 결과: `Saved/VoiceParticipantNative/index.json`.
- 1280×720 / 800×600으로 설정 화면을 렌더링하고 이미지도 확인했다. 작은 화면은 스크롤을 사용하며 제목 여백과 동료 음량 게이지 배치가 유지된다. 결과: `Saved/VoiceParticipantTests/VoiceSettings_*.png`.
- 이 검증은 실제 두 PC 사이 EOS 통화 성공을 의미하지 않는다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 34개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 동료별 설정은 계정·MatchId에 한정한다. 같은 경기의 맵 이동·음성 재연결·동료 재참가 시 유지/재적용하고, 경기 종료·로그아웃·다른 경기·다른 계정·마네킹 역할로 전환할 때 초기화한다. 장치/전체 보이스 수신 | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 2
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 원 문서에 기재 없음(미확인)

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `27b871b5` | 2026-10-16 | maru3172 | Voice enhancement |
| `bd515000` | 2026-10-16 | maru3172 | Mannequin Camera Improvements |

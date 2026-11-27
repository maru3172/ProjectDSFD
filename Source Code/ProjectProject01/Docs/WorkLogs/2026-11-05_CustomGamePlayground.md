# 사용자 지정 게임 (놀이터) 1단계 설계 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`CustomGamePlayground.md`](../CustomGamePlayground.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 규칙 하나를 켤 때는 같은 커밋에서 게임 서버 동작, 두 곳의 `Live` 스위치, 검사를 함께 바꾼다.
- 기간: 2026-11-05 ~ 2026-11-07 (문서 변경 커밋 4개 기준)
- 결과·검증: 검사(2026-11-05, 감염 이후): Unreal 40개 통과(`CustomRules.*` 9개: `AuthorityAndRuntime`·`PlaygroundParse`·`PlaygroundCards`·`InfiniteLives`·`HideReveal`·`Ghosts`·`Infection`·`Presets`·`Render`(네 탭 × 1280×720·800×600, 버튼 겹침·탭 영역 넘침·탭 전환 시 크기 유지), `ItemRoguelike.*`, `Bot
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 규칙 하나를 켤 때는 같은 커밋에서 게임 서버 동작, 두 곳의 `Live` 스위치, 검사를 함께 바꾼다.
- 사용자 지정 게임은 일곱 번째 어트랙션 "놀이터"다. 다른 여섯 가지(싱글, PvP, 봇전, 아이템 로그라이크, 어픽스, 미니게임)를 섞고 규칙을 바꿔 노는 곳이며, 공식 기록과 분리한다.
- 지켜야 할 원칙:
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`CustomGamePlayground.md`](../CustomGamePlayground.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (4)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (4)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCustomRoomRulesWidget.cpp` (4)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCustomRulesTests.cpp` (4)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (4)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestGameMode.h` (4)
- `ProjectProject01/Source/ProjectProject01/Public/MultiplayTestPlayerController.h` (4)
- `ProjectProject01/Source/ProjectProject01/Public/PlayerCharacter.h` (4)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectCustomMatchRules.h` (4)
- `ProjectProject01/Tools/ProjectProject01Backend/Game/CustomRoomRules.cs` (4)
- `ProjectProject01/Tools/ProjectProject01Backend/Game/RuleCodes.cs` (4)
- `ProjectProject01/Tools/ProjectProject01FeatureChecks/CustomRoomRulesChecks.cs` (4)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 세는 판: 정상 종료한 판. 기권 판정이 난 판은 기권 규칙의 결과대로 센다. 서버 오류로 중단된 판은 세지 않는다.
- 맵에 다음 층으로 가는 길(층 이동 장치)이 여러 개일 때, 층마다 그중 1곳~최대 N곳만 움직이고 나머지는 고장이다. 플레이어는 움직이는 곳을 찾아 빠져나가야 한다. 예전에 뺀 "엘리베이터 고장"은 하나뿐인 길을 막아 갈 곳이 없어지는 문제였고, 이 규칙은 길이 항상 남아 있어 "찾는" 규칙이라 다르다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 기준일: 2026-11-05. 설계 문서다. 구현 완료를 뜻하지 않는다. 수치는 구현 후 플레이 확인을 거쳐 조정한다.
- 합의한 규칙 목록은 확정이다. 이 문서는 그중 1단계만 다룬다.
- 공식 기록(리더보드, 개인 전적, 레이팅, 경험치, 포인트, 미션, 업적, 시즌 보상)에 반영하지 않는다.
- 방은 최대 3명이다(`ProjectProject01LobbySubsystem.h`의 `MaxPlayers = 3`). 마네킹 플레이어 1명과 생존자 2명이 기준이다. 역할 비율을 바꾸는 규칙은 제외 목록에 있으므로, 모든 규칙과 프리셋은 생존자 2명 기준으로 맞춘다.
- `itemCards=true`이면 `affixIds`로 이미 걸린 어픽스는 버프 카드로 나오지 않는다. 기존 `ProjectItemRoguelike::Pool`의 `ActiveBuffs` 제외를 그대로 쓴다. 따라서 어픽스를 많이 고를수록 버프 카드가 줄고 아이템 카드가 늘어난다.
- 경기 확정 때 `match_custom_rules`에도 새 열을 함께 스냅샷한다(`CaptureAsync`). `room_rule_presets`에도 같은 열과 `affix_ids`, `item_cards`를 추가한다. 새 열은 기본값을 가지므로 이미 저장된 프리셋은 "새 규칙 꺼짐"으로 그대로 유효하다.
- 켜면 기존 아이템 로그라이크 규칙(층마다 3장, 새로고침 1번, 20초 자동 선택, 고르는 동안 경기 시간 정지)을 그대로 쓴다.
- 잡혀도 데스카운트가 줄지 않고 탈락하지 않는다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 검사(2026-11-05, 감염 이후): Unreal 40개 통과(`CustomRules.*` 9개: `AuthorityAndRuntime`·`PlaygroundParse`·`PlaygroundCards`·`InfiniteLives`·`HideReveal`·`Ghosts`·`Infection`·`Presets`·`Render`(네 탭 × 1280×720·800×600, 버튼 겹침·탭 영역 넘침·탭 전환 시 크기 유지), `ItemRoguelike.*`, `Bot
- 모든 규칙은 클라이언트, 게임 서버, 백엔드 세 곳에서 검증한다.
- 아래 조합 검증은 세 곳이 같은 함수 의미를 가져야 한다.
- 권장 순서: 데이터·검증 → 백엔드 → 섞기 스위치 → 무한 목숨 → 숨기 → 유령 → 감염 → 프리셋 → 규칙 코드. 위험이 가장 큰 감염을 뒤에 두면, 그 전까지의 묶음만으로도 놀이터를 먼저 내놓을 수 있다.
- 합의 목록의 남은 규칙이다. 새 규칙은 더하지 않는다. 각 규칙은 1단계와 같은 방식(필드 + 세 곳 검증 + `Live` 스위치 + 방장 화면 + 검사)으로 넣는다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 40개 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 규칙 하나를 켤 때는 같은 커밋에서 게임 서버 동작, 두 곳의 `Live` 스위치, 검사를 함께 바꾼다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 4
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 유령: 유령 1명이 남은 동료 1명을 돕는 1대1 구도다. 핑과 속삭임 제한은 이 구도에서 정보가 너무 많아지지 않도록 정한 값이다.
- 남은 생존자가 모두 감염되면 마네킹 쪽 승리.
- 남은 생존자 중 누군가 탈출하면 기존 탈출 판정을 따른다.
- 한 명을 잡아도 정보가 끊기지 않으므로, 빨리 잡는 것보다 남은 생존자를 고립시키고, 유령이 알려 주기 어려운 곳(시야가 끊기는 모퉁이, 몸체가 많은 진열대)으로 몰아가는 쪽이 중요해진다.
- 남은 것 (제안대로 진행하고 나중에 조정해도 된다):
- 다른 사람의 코드를 둘러보는 목록은 1단계에 넣지 않았다. 코드가 실제로 많이 공유되는지 본 뒤에 정해도 되는지.
- 생존자가 AI 탈출자를 층마다 다음 층 입구(마지막 층은 출구)까지 데려간다. 시선이 "나를 지키는 무기"에서 "남을 지키는 무기"가 된다. 함께 검토한 점령전은 한자리를 지키면 시선만으로 쉽게 이기고(보면 멈춤), 공포·탈출의 결과 맞지 않으며, 빼기로 한 점수제 경기와 같은 계열이라 넣지 않았다.
- 구현: 필드 `escort`(규칙 코드는 켜졌을 때만 `|v5|1`을 붙여 예전 코드 유지). 서버 `TickPlaygroundEscort`, `SpawnPlaygroundEscapee`, `ChoosePlaygroundEscortGoal`, `IsPlaygroundEscapeeAfraid`, `EndPlaygroundEscortRound`/`ContinuePlaygroundEscort`, `HearPlaygroundEscortPing`, `ApplyPlaygr

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `7c09a5d` | 2026-11-07 | maru3172 | Minigame |
| `3b9316e` | 2026-11-07 | maru3172 | CollaborationTool 2 |
| `342421a` | 2026-11-06 | maru3172 | MiniGame 3 |
| `03b8b57` | 2026-11-05 | maru3172 | UI Adjustments |

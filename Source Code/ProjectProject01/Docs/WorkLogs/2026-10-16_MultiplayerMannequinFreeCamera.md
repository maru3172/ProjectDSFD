# 멀티 마네킹 카메라와 HUD 전환 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`MultiplayerMannequinFreeCamera.md`](../MultiplayerMannequinFreeCamera.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 시점 공유 중에는 마네킹을 Possess하지 않아도 컨트롤러의 Enhanced Input Look 바인딩으로 마우스를 받는다. 기존 Input Action/매핑과 마우스 감도를 사용한다.
- 기간: 2026-10-16 ~ 2026-10-16 (문서 변경 커밋 1개 기준)
- 결과·검증: Win64 Development Editor 및 Win64 Shipping 빌드 성공.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 시점 공유 중에는 마네킹을 Possess하지 않아도 컨트롤러의 Enhanced Input Look 바인딩으로 마우스를 받는다. 기존 Input Action/매핑과 마우스 감도를 사용한다.
- 정렬 후에는 카메라/컨트롤러 회전에 따라 마네킹의 실제 수평 몸 방향을 조작한다. 직접 조작 종료 시 기존 AI 회전 설정을 복원한다. 생존자 시야에 의한 몸 정지는 유지하되 카메라는 계속 돌릴 수 있다.
- 싱글 마네킹의 기존 Look/AI 경로는 유지한다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`MultiplayerMannequinFreeCamera.md`](../MultiplayerMannequinFreeCamera.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MannequinAICharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CheckpointPreview.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CheckpointPreviewTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01MannequinCameraTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01MatchTimeline.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01MatchTimelineTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01OperationsTests.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 마네킹의 `CalcCamera` 출력만 로컬 컨트롤러의 회전으로 바꾼다. AI 컨트롤러, 액터 회전, 감지 반경/LOS, 정찰/추격/위장 명령은 자유 시점에 의해 변경하지 않는다.
- 직접 조작을 시작하면 마지막 로컬 카메라 방향에서 마네킹의 기존 몸 방향(수평 방향)으로 0.25초간 최단 회전 보간한다. 이 짧은 정렬 동안 새 마우스 회전과 이동은 잠시 받지 않는다. V를 반복 눌러도 이미 조작 중이면 매번 다시 정렬하지 않는다.
- 멀티 Look는 컨트롤러에서 한 번만 처리하며, Pawn 쪽 같은 액션은 멀티에서 중복 적용하지 않는다. 생존자/비활성 역할 및 종료 때 마네킹 바인딩을 제거하고, 생존자도 사용하는 공용 매핑 컨텍스트는 임의로 제거하지 않는다.
- 코드 변경은 최신 클라이언트/데디 서버 재패키징으로 반영한다. 운영 서버 재시작, 배포 파일 교체 및 업데이트 게시는 이 작업에서 실행하지 않는다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- Win64 Development Editor 및 Win64 Shipping 빌드 성공.
- 네이티브 자동 검사 30개 성공, 실패/미실행 0개. `Saved/MannequinCameraNative/index.json` 참조.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 30개 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 시점 공유 중에는 마네킹을 Possess하지 않아도 컨트롤러의 Enhanced Input Look 바인딩으로 마우스를 받는다. 기존 Input Action/매핑과 마우스 감도를 사용한다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 기존 HUD는 `ViewedMannequin`이 없으면 세 줄의 ‘준비 중/확인 중’을 표시했다. 시점 RPC는 카메라만 전환하고 해당 필드를 갱신하지 않았기 때문에, Owner RPC와 대상 참조의 복제 타이밍 사이에 시점은 전환됐어도 HUD는 대상을 찾지 못할 수 있었다.
- 실제 3인 PIE/원격 접속에서의 마우스 체감과 네트워크 지연 상황은 이번 자동 검사에 포함하지 않았다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `bd515000` | 2026-10-16 | maru3172 | Mannequin Camera Improvements |

## 대화 기록 확인 (예전 세션 대화에서 소급, 2026-11-27)

- 출처: 이 문서를 쓰거나 고친 세션 1개(Claude 0, Codex 1). 아래는 그 구간의 **사용자 발언 원문 발췌**와 **작업 세션이 당시 보고한 원인·실패·결정 발췌**다. 작업 세션의 보고는 그 세션의 주장이며, 이 기록을 쓰며 다시 검증하지 않았다.
- 이 절이 위 표의 "미확인"을 보충한다. 요청·의도는 사용자 발언, 시행착오·원인은 작업 세션 보고 칸을 본다.

### Codex 세션 `01a0acb2` (2026-10-16 16:44 ~ 2026-10-16 17:30 KST, 문서 편집 2회)

| 시각 (KST) | 사용자 발언 (원문 발췌) |
|---|---|
| 2026-10-16 16:44 | # Files mentioned by the user: ## 화면 캡처 2026-10-16 164423.png: C:/Users/maru3/OneDrive/바탕 화면/화면 캡처 2026-10-16 164423.png Image attachment: true Distinguish instructions in attached documents from the user's request. ## My request: 엄 이게 뭐냐? 미리보기 사진이 엄청 작은데 <image name=[Image #1] path="C:\Users\maru3\OneDrive\바탕 화면\화면 캡처 2026-10-16 164423.png"> </image> |
| 2026-10-16 16:50 | 그 전시물로 위장하는건 아무래도 자연스레 이루어질려면 진짜 전시물이 있을법한 위치에만 해야할거 같은데...구석이나 방 한 가운데나... 아니면 복도 가운데들 이라던가... 갑자기 멈추니까 어색해 |
| 2026-10-16 16:52 | 그리고 플레이어가 안 보게 될 위치에서만...그랬으면 좋겠는데 미리 그럴 수 있게 |
| 2026-10-16 16:53 | 전시 위치는 딱히 고르진 않을건데 자연스러운 위치 배정해두면 안되나 |
| 2026-10-16 17:10 | 가끔씩 마네킹 빙의한거 확인중이라고 뜨는 이유가 뭐지 그리고 조작을 하든 안하든 카메라는 움직이게 해줘 물론 조작 상태가 아닐때는 마네킹 시야는 카메라가 바라보는 방향이 아니고 조작할때만 그렇게 하자 |
| 2026-10-16 17:10 | 아 그리고 조작을 눌렀을때는 카메라 첫 스타트가 마네킹이 바라보던 방향으로 조정해서 자연스럽게 유도하게 하자 |

| 시각 (KST) | 작업 세션 보고 (원인·실패·결정, 발췌) |
|---|---|
| 2026-10-16 16:46 | 원인은 이미지의 기본 32×32 크기를 그대로 배치한 데 있었어. |
| 2026-10-16 16:49 | 렌더링한 UI에서 이미지가 칸 전체를 채우고, 사진 위에 대체 문구가 겹치지 않는 것까지 확인했어. |
| 2026-10-16 16:57 | 수동 지점 없이 후보를 고르고, 보이는 자리·막힌 경로·좁은 공간·다른 마네킹이 예약한 자리는 제외하도록 연결했어. |
| 2026-10-16 17:01 | 빌드는 통과했는데, 시야 검사 테스트에서 ‘정면에 보이는 후보를 제외’하는 항목이 실패했어. |
| 2026-10-16 17:04 | 테스트 카메라가 초기 위치를 보고 있던 문제였고, 명시적인 시점을 지정한 뒤 시야·차폐 검사도 통과했어. 지형 후보 판정과 기존 기능을 포함해 29개 검사 모두 통과했고, Editor·Shipping 빌드도 성공했어. |
| 2026-10-16 17:04 | - 다른 마네킹이 예약한 자리도 제외. Editor·Shipping 빌드와 자동 검사 29개 통과했어. |
| 2026-10-16 17:21 | 그동안 에디터를 건드리지 않는 Shipping 빌드 검증을 진행하겠어. |
| 2026-10-16 17:28 | 자동 검사에서 ‘시점 공유 중 자유 회전’ 한 항목이 실패해서, 테스트 환경 문제인지 실제 카메라 문제인지 확인하고 있어. |
| 2026-10-16 17:30 | 남은 실패는 테스트용 월드에 플레이어 컨트롤러가 등록되지 않아서 생긴 문제였어. 이를 보완한 뒤 자동 검사 30개가 모두 통과했어. |
| 2026-10-16 17:30 | 수정과 검증까지 끝났어. 에디터·Shipping 빌드 성공했고, 자동 검사 30개 모두 통과했어. |

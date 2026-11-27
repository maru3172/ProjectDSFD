# 마네킹 명령 휠 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`MannequinCommandWheel.md`](../MannequinCommandWheel.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 마네킹 플레이어의 명령은 감정표현 같은 원형 UI로만 내린다. 예전 V/Z/X/C/Q/B 단축키는 없앴다(조작키 도움말·튜토리얼 문구도 휠 기준으로 바꿈). 숫자 키 0~9(번호로 마네킹 고르기·빙의)는 그대로다.
- 기간: 2026-11-04 ~ 2026-11-04 (문서 변경 커밋 1개 기준)
- 결과·검증: `Public/ProjectMannequinWheel.h`, `Private/ProjectMannequinWheel.cpp`: 휠 Slate 위젯(그리기·호버·누름·선택·취소) + 자동 검사 `ProjectProject01.MannequinWheel`(방향별 선택, 비활성 칸, 그림 `Saved/MannequinWheelQA`).
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 마네킹 플레이어의 명령은 감정표현 같은 원형 UI로만 내린다. 예전 V/Z/X/C/Q/B 단축키는 없앴다(조작키 도움말·튜토리얼 문구도 휠 기준으로 바꿈). 숫자 키 0~9(번호로 마네킹 고르기·빙의)는 그대로다.
- 오른쪽 클릭: 조준점 방향으로 넓은 스윕(반지름 60, 4000)을 쏜다. 마네킹에 맞지 않으면 조준점에서 7도 안쪽에 보이는(벽에 가리지 않은) 마네킹 중 가장 가까운 각도의 것을 고른다. 그래도 없으면 지금 보고 있는 마네킹(자신)에게 내릴 명령 휠이 열린다.
- 휠이 열려 있는 동안: 마우스 커서가 나오고 화면은 마우스를 따라 돌지 않는다. 이동 키는 계속 쓸 수 있다. 다른 마네킹을 골랐다면 카메라가 그쪽으로 천천히 돈다(초당 2.5 보간, 위아래 -25~15도로 제한해 어지럽지 않게). 자신이면 카메라는 그대로다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`MannequinCommandWheel.md`](../MannequinCommandWheel.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/FootstepSynthComponent.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MannequinAICharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectAffixRuntime.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectAffixRuntimeItems.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCaseCollectionWidgets.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCaseRecordWidgets.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCaseUiTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectFrontendBehaviorTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectItemRoguelikeRuntime.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 열기·닫기 연출: 열 때 0.7배에서 1배로 커지며 0.14초 동안 나타나고, 명령을 내리거나 취소하면 0.75배로 작아지며 0.12초 동안 사라진다. 사라지는 동안에는 입력을 받지 않는다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- `Public/ProjectMannequinWheel.h`, `Private/ProjectMannequinWheel.cpp`: 휠 Slate 위젯(그리기·호버·누름·선택·취소) + 자동 검사 `ProjectProject01.MannequinWheel`(방향별 선택, 비활성 칸, 그림 `Saved/MannequinWheelQA`).
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

**정량 검증 미완료** — 원 문서에 측정 기준값·측정값이 없다.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 마네킹 플레이어의 명령은 감정표현 같은 원형 UI로만 내린다. 예전 V/Z/X/C/Q/B 단축키는 없앴다(조작키 도움말·튜토리얼 문구도 휠 기준으로 바꿈). 숫자 키 0~9(번호로 마네킹 고르기·빙의)는 그대로다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 실제 경기에서 오른쪽 클릭 조준, 카메라 따라가기의 느낌, 생존자 화면에 고리가 안 보이는지는 직접 확인하지 않았다.
- 직접 조작을 끝낼 때(다른 마네킹으로 빙의): 조작 중 명령을 내리지 않았다면 사람 플레이어의 몸체는 직접 조작 전의 명령으로 그대로 돌아간다(기록이 없으면 정찰). 멈추는 것은 정지 명령을 받았을 때뿐이다. 서버 봇은 승률 조정 전제를 지키려고 기존대로 정지한다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `d0de20fa` | 2026-11-04 | maru3172 | Mannequin UI update |

## 작업 세션 직접 기록 (2026-11-04, Claude 세션 local_2e236d8f)

### 사용자 요청
- "직접 조작 → 빙의 해제하면 원래는 멈췄는데, 이젠 정지 명령 내리기 전까진 멈추지 말라."
- "미리 지정할 대상을 보여 줄 표시 효과", "UI 뜰 때 커지면서 드러나고 취소·수행하면 작아지면서 사라지게", "N번 마네킹 바로 아래에 현재 명령".
- (단축키 제거·레이 판정 완화 요청은 휠을 만든 부모 세션으로 전달)

### 원인·수정
| 항목 | 원인/방식 | 파일 |
|---|---|---|
| 해제 후 멈춤 | `TryPossessMannequin`이 명령 미지정 시 의도적으로 `HoldPosition` 지정 → 사람 플레이어는 조작 전 명령 복귀(기록 없으면 정찰). 봇은 승률 조정 전제라 기존 유지 | `MultiplayTestGameMode.*` |
| 대상 미리 보기 | 휠과 같은 `TraceWheelTarget`을 0.1초마다, 옅은 고리+번호(화면에만, 깜빡임 없음) | `ProjectMannequinAimMarker.*`(신규), `MultiplayTestPlayerController.*` |
| 열기·닫기 연출 | 0.7→1배·0.14초 / 1→0.75배·0.12초, 닫히는 동안 입력 없음 | 같은 컨트롤러 |
| 현재 명령 | 제목 아래 줄, 매 프레임 복제된 명령 읽음(정찰·범위 추격·따라오는·정지·직접 조작 중) | `ProjectMannequinWheel.*` |

### 검증
`ProjectProject01.MannequinWheel` 통과. 실제 화면·조작 확인 미실행.

### 의도 대비 달성도 (9-9-1)
| 의도 | 수용 기준 | 결과·근거 | 구분 | 판정 | 남은 격차 |
|---|---|---|---|---|---|
| 정지 명령 때만 멈춤 | 해제 시 조작 전 명령 유지 | 코드 수정, 전용 검사 없음 | 기능 구현 | 미검증 | 실제 조작·해제 확인 |
| 대상을 미리 앎 | 조준 대상 고리·번호 | 구현, 화면 확인 없음 | 기능 구현 | 미검증 | 화면 확인 |
| 대상 오지정 감소 | 오지정 빈도 | 측정 없음 | 행동 유도 | 미검증 | 플레이 관찰 |
| 휠이 자연스럽게 열리고 닫힘 | 크기·투명도 변화 | 구현, 미확인 | 사용성 | 미검증 | 사용자 확인 |

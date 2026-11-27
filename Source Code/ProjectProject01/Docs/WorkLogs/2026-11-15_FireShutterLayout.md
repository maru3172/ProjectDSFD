# 방화셔터 판마다 배치 (3층·2층·1층) — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`FireShutterLayout.md`](../FireShutterLayout.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 참고: `Work/2022184042_HSW/Document/백화점화재사건 방화셔터 기획서.pptx`
- 기간: 2026-11-15 ~ 2026-11-16 (문서 변경 커밋 3개 기준)
- 결과·검증: **실제 맵 200판 검사**: 못 가는 구역은 없다. 2층은 보호되는 구획 통로 때문에 "short" 68건이 나오는데, 통과로 본다.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 참고: `Work/2022184042_HSW/Document/백화점화재사건 방화셔터 기획서.pptx`
- 정상(작동하는) 방화셔터: 매장-복도 격리, 추가 경로·탐색 요소(긍정적 역할). 플레이어가 올리고 내릴 수 있다.
- 손상(작동하지 않는) 방화셔터: 시야·이동 억제, 마네킹 접근 변수·포위 유도(부정적 역할). 움직이지 않는다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`FireShutterLayout.md`](../FireShutterLayout.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/FireShutterActor.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectShutterLayout.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectShutterLayoutRules.cpp` (3)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectShutterLayout.h` (3)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectShutterLayoutRules.h` (3)
- `ProjectProject01/Tools/PlaceFireShutterLayout.py` (3)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectMatchBots.cpp` (2)
- `ProjectProject01/Source/ProjectProject01Editor/Private/ProjectShutterLayoutMapChecks.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/MannequinReactiveLight.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectShutterLayoutTests.cpp` (2)
- `ProjectProject01/Source/ProjectProject01/Public/FireShutterActor.h` (2)
- `ProjectProject01/Source/ProjectProject01/Public/MannequinReactiveLight.h` (2)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 1층은 임시 재질이 밝은 초록이라 노출을 -1.5 더 낮췄지만 여전히 밝다(재질 색 문제). 실제 텍스처가 들어오면 `EXPOSURE_BIAS`·`FLOOR_EXPOSURE`를 다시 맞춘다.
- **증상**: 어두운 셔터 맵에서 처음 돌린 밸런스 측정 12판 중 9판이 10분 안에 끝나지 않았다. 생존자 봇은 목표가 있는데도 서 있었다(Moving=0이 577건 중 463건). 길찾기가 실패하고 있었다.
- **원인**: 맵 길찾기를 Dynamic Modifiers Only로 바꾼 뒤 전체 빌드를 하지 않았다. 이 모드는 저장된 타일 캐시로 셔터 주변 타일을 다시 만든다. 그래서 캐시가 없으면 경기 중 셔터 주변 길이 비어 버린다. 명령줄 빌드(ResavePackages -BuildNavigationData)는 에디터의 로드 잠금(AsyncLoadLock)에 막혀 아무것도 하지 않았다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 공정성 규칙(모두 도달, 우회 한도, 길목 몰림 금지)은 그대로 두고, 그 안에서 고르는 순서에만 가중치를 준다(`Horror Weight`, 0이면 균등).
- 내려진(또는 내려가는) 작동 셔터 밑에는 `NavArea_ProjectShutterDoor` 영역을 씌운다. 마네킹 AI 컨트롤러는 이 영역을 제외하는 필터(`UProjectMannequinNavFilter`)를 기본으로 써서 **다른 길로 돌아온다**(셔터를 내리고 숨으면 반대쪽에서 돌아 들어온다). 셔터가 올라가기 시작하면 영역을 걷는다.
- 포위 갈림길: 좁은 통로가 3방향 이상 열린 곳으로 들어오는 입구(촘촘한 격자에서 넓은 홀 안 노드는 제외).
- **에스컬레이터**: 구멍 둘레 전체 대신 **타는 쪽 한 끝만** 짧은 U자 셔터(구멍 긴 방향의 끝 7m, 양 끝은 짧은 셔터로 막음). 끝은 엘리베이터가 있는 구멍이면 엘리베이터 반대쪽, 아니면 앞쪽이 더 넓게 트인 쪽. 층 이동 트리거도 그 안쪽에만. 나머지 가장자리는 맵 그대로.
- **큰 복도**: 폭 45m까지 벽에서 벽(또는 구멍 가장자리)까지 막는 줄(`main`, 역할은 Corridor). 이웃한 구멍 사이 한가운데에는 **구획 줄**을 항상 먼저 둔다(양쪽이 벽이나 구멍 가장자리에 닿을 때만. 3층 서쪽↔가운데처럼 한쪽이 25m 넘게 트인 홀이면 두지 않음). 그 밖의 큰 복도 줄은 생존자 시작 지점에서 에스컬레이터 입구·탈출구로 가는 최단 경로가 많이 지나는 곳부터 고른다(주차장·옆 홀 제외).
- **큰 복도는 닫지 않는다**: 줄 폭이 10m를 넘는 셔터 줄(큰 복도)은 판마다 닫히는 후보에서 뺀다(`Max Closable Row Width`). 대신 부서진 줄로 자주 보인다(피해 구역 안 85%, 밖 45%: 몇 장만 내려오거나 걸리고, 넓은 칸 하나는 반드시 열림). 길을 막는 셔터는 좁은 복도와 길목에서만 닫힌다.
- **꺼진 등 때문에 껌껌한 길**: 처음부터 꺼진 등은 이제 곁가지에만(30%). 동선·시작·탈출구 등은 꺼지지 않는다. 판마다 피해 구역에서 끄는 등은 35%로 낮췄고 시작·탈출구 등은 제외한다(배치 스크립트가 `LightKind_<종류>` 태그를 단다). 켜진 등: 3층 22개 중 19, 2층 27개 중 22, 1층 25개 중 22.
- **늘 꺼진 등 없앰**: 처음부터 꺼진 고장 등은 넣지 않는다(에디터에서 보이는 등은 전부 켜짐). 경기 중 꺼지는 것은 판마다 피해 구역 35%(시작·탈출구 제외, `Dead Light Chance In Zone`)와 기존 반응 조명의 마네킹 근접 고장뿐이다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 미리보기만: `UnrealEditor-Cmd ProjectDFSD.uproject -run=pythonscript -script="<경로>/Tools/PlaceFireShutterLayout.py"` → `Saved/ShutterLayoutQA/<층>_Placement.bmp`, `PlacementReport.json`.
- 실제 맵 검사 `ProjectProject01.Editor.ShutterLayoutOnMaps`: 층마다 200판, 모두 최소 개수 충족, 매 판 작동·손상 혼합, 막힘·긴 우회 없음. 배치 그림 `Saved/ShutterLayoutQA/<맵>_Layouts.svg`.
- 자동 검사: `ProjectProject01.ShutterLayout.Rules`(3층 모양 견본 500판), `.EdgeCases`(외길, 긴 우회, 1층), `.Runtime`(배치된 액터에 적용, 봇 열기). 견본 배치 그림: `Saved/ShutterLayoutQA/SampleFloor.svg`.
- 검사: 열린 줄은 항상 넓은 칸 하나가 열려 있음, 모습이 닫힘 여부와 일치(작동=Working, 막힘=내려감/걸림), 걸림 틈 ≤ 120cm·매달림 ≥ 210cm, 피해 구역이 나옴, 일부 열린 줄이 부서져 보임. 실제 맵 판 모습 그림: `Saved/ShutterLayoutQA/<맵>_Layouts.svg`(분홍 원=피해 구역, 빨강=완전히 내려감, 주황=걸림, 회색=매달림, 초록=작동).
- 렌더 확인: `ProjectProject01.Editor.MatchMapLook`이 실제 세 맵을 생존자 눈높이에서 찍는다(`Saved/ShutterLayoutQA/Look_<맵>_<n>.png`, 화면 밝기·검은 비율 기록).
- **실제 맵 200판 검사**: 못 가는 구역은 없다. 2층은 보호되는 구획 통로 때문에 "short" 68건이 나오는데, 통과로 본다.
- **전체 테스트**: 78개 통과.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 78개 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 참고: `Work/2022184042_HSW/Document/백화점화재사건 방화셔터 기획서.pptx` | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 3
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 경로를 기준으로 시드를 만든다: ① 에스컬레이터마다 열리는 면을 정하고 ② 그 면들(1층은 탈출구)까지의 길을 기준으로 손상 셔터를 놓고 ③ 남은 최단 경로 위에 작동 셔터를 섞는다. 가는 길은 여러 갈래가 남는다.
- 배치 스크립트가 세 맵의 `RecastNavMesh` → `Runtime Generation`을 `Dynamic Modifiers Only`로 바꿔 저장했다. **하지만 내비메시는 아직 다시 빌드하지 않았다.** 명령줄 재빌드(`ResavePackages -BuildNavigationData`)는 "navigation build is locked"로 거부되었다. 에디터에서 세 맵을 하나씩 열어 Build → Build Paths 후 저장해야 손상 셔터가 마네킹·
- 실제 3층·2층·1층 맵에는 아직 노드·셔터를 배치하지 않았다(맵 파일은 에디터에서 작업). 배치 전까지는 아무것도 바뀌지 않는다.
- 실제 경기에서 체감(찾는 재미, 마네킹 포위)과 봇이 셔터 앞에서 멈칫하는 모습은 보지 않았다.
- 목표: 어둡지만 조명은 남아 있는 폐허 백화점 내부. 깜빡임 효과는 새로 넣지 않았다(화면 전체 깜빡임 금지 규칙). 조명 고장 연출은 기존 마네킹 반응 조명(`AMannequinReactiveLight`, 근처 마네킹 3개 이상일 때만, 접근성 설정 적용)이 그대로 맡는다.
- **조명이 세로로 서 있던 원인**: 반응 조명 블루프린트의 등 몸체·원판이 블루프린트 안에서 90° 세워져 있다(예전 배치처럼 액터를 -90° 눕힌다는 전제). 배치 스크립트가 등마다 부품을 월드 기준으로 맞춘다: 등 몸체·원판은 수평으로 천장에 밀착(천장 6cm 아래), 스포트라이트만 아래로. 블루프린트 기본값은 바꾸지 않았다(싱글 맵 기존 등 보호).
- 실제 데디케이티드/리슨 서버 경기에서 1층과 지하를 오가는 이동은 해 보지 않았다.
- **확인 못 함**: 실제 경기에서 마네킹 플레이어가 F로 셔터를 다루는 것과, F 안내 문구는 확인하지 않았다(조작 후 3초 알림만 있다). 어둠 시야 10m는 사람 눈 기준으로 잰 값이 아니라 임시값이다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `8dd5d3b` | 2026-11-16 | maru3172 | Balance 2 |
| `b763258` | 2026-11-15 | maru3172 | Rescue |
| `7149409` | 2026-11-15 | maru3172 | Algorithm |

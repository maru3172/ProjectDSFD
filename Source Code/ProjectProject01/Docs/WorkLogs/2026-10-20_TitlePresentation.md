# 타이틀 전용 3D 연출 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`TitlePresentation.md`](../TitlePresentation.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: `AProjectProject01TitlePlayerController`만 로컬 `AProjectDFSDTitleScene`을 생성한다. AI/Pawn이나 멀티 복제 객체가 아니다. 타이틀을 나가면 세트가 제거된다.
- 기간: 2026-10-20 ~ 2026-10-20 (문서 변경 커밋 1개 기준)
- 결과·검증: `ProjectDFSDEditor Win64 Development` 빌드 성공. 새 소스가 프로젝트 파일/Rider 목록에도 포함되도록 `.sln`/`.slnx` 목록을 갱신했다.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- `AProjectProject01TitlePlayerController`만 로컬 `AProjectDFSDTitleScene`을 생성한다. AI/Pawn이나 멀티 복제 객체가 아니다. 타이틀을 나가면 세트가 제거된다.
- UI는 왼쪽, 3D 마네킹은 오른쪽이다. 카메라와 메뉴가 동일한 16:9 안전 프레임에 맞춰진다. 4:3/울트라와이드는 비율을 늘이거나 자르지 않고 여백을 둔다.
- 조명은 마네킹 얼굴 정면이 아니라 옆을 비추어 빛의 경계에 서 있는 구도를 만든다. 수동 노출을 고정해 조명이 어두워질 때 자동 노출이 화면을 다시 밝히지 않게 한다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`TitlePresentation.md`](../TitlePresentation.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectDFSD.Target.cs` (1)
- `ProjectProject01/Source/ProjectDFSDEditor.Target.cs` (1)
- `ProjectProject01/Source/ProjectDFSDServer.Target.cs` (1)
- `ProjectProject01/Source/ProjectProject01/Private/InteractionPromptWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDBranding.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDStartupIntro.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDStartupIntroTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDTitleScene.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDTitleSceneTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDUITheme.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectDFSDUIThemeTests.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 실제 경기용 마네킹·AI·공격·피격 VFX·SFX·게임플레이 맵은 수정하지 않는다.
- 최초 실행의 학교 로고 → Team DSFD → 타이틀 순서는 유지한다. 같은 실행에서 타이틀로 돌아오면 로고는 반복하지 않는다.
- 마네킹은 뒤로 물러선 위치에서 몸을 사선으로 세우고 목을 화면 방향으로 돌린 고정 자세다. 런타임 컴포넌트의 포즈만 변경하며 공용 스켈레톤/애니메이션 자산은 변경하지 않는다.
- 첫 조명 변화는 2.5~5.5초 후 무작위로 시작하며, 각 이벤트가 끝나면 7~13초 쉰다. 1.8초 이벤트 안에서 한 번 또는 두 번 짧게 깜빡인다(각 50%). 타이틀 진입마다 새 로컬 시드를 사용하므로 같은 순서를 재생하지 않는다. 각 깜빡임은 0.09초 어두워짐 + 0.10초 유지 + 0.16초 복귀이며, 두 깜빡임 사이에는 정상 밝기로 돌아온다. 주 조명만 최저 16%로 줄이고 보조광/윤곽광/UI는 유지한다. 흰 섬광/전체 화면 소등/연속 고주파 점멸은
- 실제 게임/데디 서버 패키징이나 업데이트 게시, 운영 DB 변경은 하지 않는다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 이미지: `Saved/TitleSceneQA`. 검사 보고서는 `Saved/ReviewRemediation` 아래 저장한다.
- `ProjectDFSDEditor Win64 Development` 빌드 성공. 새 소스가 프로젝트 파일/Rider 목록에도 포함되도록 `.sln`/`.slnx` 목록을 갱신했다.
- `Saved/ReviewRemediation/TitleScene-Final-20261020/index.json`: 관련 7개 검사 성공, 실패/미실행 0. 실제 타이틀 PIE 진입, 최초 인트로, 전용 카메라, 로그인 화면에서 장식 제거, 타이틀 복귀 시 로고 미반복까지 확인했다.
- `Saved/TitleSceneQA/ActualTitleUI.png`는 실제 PIE 창의 메뉴 포함 화면이다. 합성 렌더뿐 아니라 실제 3D 카메라 노출/메뉴 배치를 확인했다.
- .NET 패키징 설정 단독 검사 26건 통과. 게임/서버 패키징은 수행하지 않았다.
- 빌드 성공. `Saved/ReviewRemediation/TitleBlink-20261020/index.json`: 타이틀 동작/실제 렌더/실제 PIE 왕복 3건 성공, 실패·미실행 0. 위와 동일한 엔진 및 백엔드 비실행 경고는 유지되며, 깜빡임의 부드러운 경계·중간 복귀·한 번/두 번 분기·접근성 비활성화를 검사했다. 패키징은 하지 않았다.
- `Saved/ReviewRemediation/TitleGazeRandom-20261020/index.json`: 랜덤 한 번/두 번 패턴·대기 간격·사선 자세·실제 렌더·타이틀 PIE 왕복 검사 3건 성공, 실패·미실행 0. 64개 이벤트를 고정된 테스트 시드로 검사해 두 패턴이 모두 나오고 대기 간격이 변하는 것을 확인했다(런타임은 진입마다 새 시드). 기존 엔진/백엔드 비실행 경고는 동일하다. 빌드 성공, 패키징 미수행.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 7개 검사 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | `AProjectProject01TitlePlayerController`만 로컬 `AProjectDFSDTitleScene`을 생성한다. AI/Pawn이나 멀티 복제 객체가 아니다. 타이틀을 나가면 세트가 제거된다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- PIE 검사는 백엔드를 켜지 않은 상태여서 기존 서비스 상태 조회의 연결 거부 로그가 남았다. 엔진의 `r.MotionVectorSimulation` 렌더 스레드 경고도 1회 발생했다. 이 경고들을 숨기지 않았으며, 머티리얼 사용 플래그 누락 경고는 보완 후 더 이상 발생하지 않았다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `afaa9bd8` | 2026-10-20 | maru3172 | Title Update |

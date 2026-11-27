# 백화점 화재사건 — ProjectDFSD (2026-10-20) — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`StartupIntroAndProjectRename.md`](../StartupIntroAndProjectRename.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 게임 표시명/창 제목: **백화점 화재사건**. 팀: **Team DSFD**.
- 기간: 2026-10-20 ~ 2026-10-20 (문서 변경 커밋 1개 기준)
- 결과·검증: 패키징 게임 최초 실행 시 예전 `%LOCALAPPDATA%/ProjectProject01/Saved`의 `.sav`와 사용자 환경설정만 새 저장 위치로 복사한다. 새 파일은 덮어쓰지 않으며 옛 원본도 삭제하지 않는다. 한 번 성공하면 완료 표시를 남겨 이후 삭제한 슬롯이 다시 살아나지 않게 한다. 인증 파일·로그·크래시 데이터는 가져오지 않는다. 에디터/서버에는 이 자동 가져오기를 실행하지 않는다.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 게임 표시명/창 제목: **백화점 화재사건**. 팀: **Team DSFD**.
- 프로젝트 파일: `ProjectDFSD.uproject`. IDE 파일: `ProjectDFSD.sln` / `ProjectDFSD.slnx`.
- 게임/에디터/서버 빌드 대상: `ProjectDFSD`, `ProjectDFSDEditor`, `ProjectDFSDServer`. 다음 패키징의 게임 실행 파일은 `ProjectDFSD.exe`, 서버는 `ProjectDFSDServer.exe`다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`StartupIntroAndProjectRename.md`](../StartupIntroAndProjectRename.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

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

- 좌클릭 한 번은 현재 로고만 0.25초 페이드 아웃으로 넘긴다. 넘기는 중 연속 클릭은 다음 로고까지 중복 처리하지 않는다. 마지막 타이틀 페이드 인 중 좌클릭하면 즉시 암전을 모두 걷고 타이틀 메뉴를 활성화한다. 이 클릭 자체는 소비해서 뒤의 메뉴 버튼을 함께 누르지 않는다. 키보드/우클릭으로는 넘기지 않는다.
- 게임 실행 후 최초 타이틀 진입에서만 나온다. 싱글 종료·로그아웃 등으로 같은 실행의 타이틀에 복귀할 때는 반복하지 않는다. 새 실행/새 PIE GameInstance에서는 다시 나온다. 게임 맵 직접 PIE·튜토리얼 테스트에는 별도 삽입하지 않는다.
- 이번 작업에서 게임 패키징·업데이트 게시·운영 DB 변경은 하지 않는다. 기존 배포본의 파일명이 저절로 바뀌는 것은 아니다. 새 이름/인트로를 배포할 때 클라이언트와 데디 서버를 같은 엔진으로 다시 패키징해야 한다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 패키징 게임 최초 실행 시 예전 `%LOCALAPPDATA%/ProjectProject01/Saved`의 `.sav`와 사용자 환경설정만 새 저장 위치로 복사한다. 새 파일은 덮어쓰지 않으며 옛 원본도 삭제하지 않는다. 한 번 성공하면 완료 표시를 남겨 이후 삭제한 슬롯이 다시 살아나지 않게 한다. 인증 파일·로그·크래시 데이터는 가져오지 않는다. 에디터/서버에는 이 자동 가져오기를 실행하지 않는다.
- `ProjectProject01.Frontend.StartupIntroRender`: 실제 Slate 렌더, 원본 이미지 비율·중앙 배치·잘림 방지, 학교/팀/타이틀 전환 화면. 이미지 출력은 `Saved/StartupIntroQA`다.
- `ProjectDFSDEditor Win64 Development` 최종 빌드 성공. 새 프로젝트/게임/에디터/서버 이름은 `Intermediate/TargetInfo.json`에도 반영됐다. 새 `.sln`/`.slnx` 생성 성공.
- 런처·도우미·백엔드·진단·스모크 도구 빌드 성공. .NET 어셈블리 이름 변경 뒤 의존성 목록도 복원/재생성했다. 격리된 .NET 기본 기능 검사 166건 통과(별도 이어받기 검사 17건과 설치 롤백 검사도 통과), 패키징 설정 단독 검사 25건 통과. 운영 DB를 사용하는 통합 스모크는 실행하지 않았다.
- `Saved/ReviewRemediation/StartupBranding-Final-20261020/index.json`: 인트로 타임라인/실제 렌더/로그인·회원가입/기존 걷기·마네킹 VFX/체크포인트 저장소 6건 성공, 경고·실패·미실행 0건. 런타임 PNG의 sRGB를 명시해 색이 뜨지 않도록 했고, 최종 16:9·4:3·울트라와이드 렌더 이미지의 중앙·비율·학교/팀 표기를 확인했다.
- Editor Development 최종 빌드 성공. `Saved/ReviewRemediation/StartupTitleSkip-Final-20261020/index.json`: 2건 성공, 경고·실패·미실행 0건. 페이드 시작/중간/종료 직전의 즉시 완료, 실제 좌클릭 이벤트 응답, 암전 완전 해제, 완료 콜백 1회, 우클릭 무시와 기존 로고 타임라인/비율을 검사했다. 패키징은 수행하지 않았다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

**정량 검증 미완료** — 원 문서에 측정 기준값·측정값이 없다.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 게임 표시명/창 제목: **백화점 화재사건**. 팀: **Team DSFD**. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 기존 소스 루트 `Source Code/ProjectProject01`과 내부 `/Script/ProjectProject01` 모듈·클래스명은 유지한다. 기존 맵/블루프린트의 부모 클래스, 저장 슬롯 이름·바이너리 포맷, 설정 섹션, DB·EOS 제품/자격증명, API 헤더/환경변수 이름을 임의 변경하지 않았다.
- 원본 `sig.png`는 수정하지 않았다. 기본 이미지 편집 도구(imagegen)로 새 PNG를 만들었다. 편집 프롬프트 요약: “학교 로고와 한국공학대학교 / TECH UNIVERSITY OF KOREA의 글자·배치·색상·의도된 흰색 테두리는 유지하고 검은 배경과 모든 격자선만 제거하여 실제 투명 PNG로 만들 것.”
- 게임/데디 Shipping 패키징이나 실제 배포본 실행은 수행하지 않았다. 위 검사 결과가 새 패키징의 실제 다중 PC 접속까지 보장하는 것은 아니다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `afaa9bd8` | 2026-10-20 | maru3172 | Title Update |

# 최종 추가 작업 14개 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`FinalFeaturesHandoff.md`](../FinalFeaturesHandoff.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: Unreal에서 100ms 지연·2% 패킷 손실 세 클라이언트, 싱글 타이틀 복귀, 설정, 셔터, 엘리베이터를 검사했다. 6개 실패 0건이며 싱글 복귀와 세 클라이언트 검사에 경고가 있었다.
- 기간: 2026-11-10 ~ 2026-11-10 (문서 변경 커밋 1개 기준)
- 결과·검증: 경기 맵 3개의 역할별 스폰과 설정된 이동 목적지, 엘리베이터·셔터 배치를 검사했다. 프롤로그 전체 진행도 통과했다.
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- Unreal에서 100ms 지연·2% 패킷 손실 세 클라이언트, 싱글 타이틀 복귀, 설정, 셔터, 엘리베이터를 검사했다. 6개 실패 0건이며 싱글 복귀와 세 클라이언트 검사에 경고가 있었다.
- 패키지의 안전 실행·그래픽 복구, 실제 PC 벤치마크, 포토 모드의 PNG 저장, 장면 3개의 개별 다시보기를 확인한다.
- 서버 운영의 **진단·운영**에서 장애 알림, 플레이 분석, 운영 변경 이력, 공개 범위, 예약 지급, Win64 덤프 분석을 연다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`FinalFeaturesHandoff.md`](../FinalFeaturesHandoff.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Config/DefaultGame.ini` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayMannequinNavigation.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectCaseRecordWidgets.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectMatchBots.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01Tutorial.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectSingleIntro.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01GameInstance.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01LoginWidget.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01Tutorial.h` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 실제 경기를 수집해 운영 분석의 집계 수와 종료 결과를 대조한다. 튜토리얼의 ‘이후 기록 없음’은 마지막 기록으로 추정한 값이며 실제 종료 원인을 단정하지 않는다.
- 플레이어는 싱글 저장소의 **프롤로그 · 장면 다시보기**, 일시정지의 **포토 모드**, 환경설정 그래픽의 **PC 성능 측정**, 경기 메뉴의 **버그 제보**에서 새 기능을 사용한다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 원 문서에 결정 서술 없음(미확인)
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 경기 맵 3개의 역할별 스폰과 설정된 이동 목적지, 엘리베이터·셔터 배치를 검사했다. 프롤로그 전체 진행도 통과했다.
- 격리 MySQL에서 이벤트 공개 시간, 운영자 권한, 중복 예약·동시 지급·회수, 버그 제보를 확인했다. SQL 백업, 한글 복원, 해시 검증, 실패한 복원의 롤백도 통과했다.
- Unreal Editor, 백엔드, 서버 도우미, 런처, 진단 대시보드의 개발 빌드가 통과했다. 패키징 설정 검사 32개와 진단 대시보드 자체 검사도 통과했다.
- 실제 Win64 덤프와 같은 빌드의 PDB를 준비해 CDB 스택·줄 번호·원인 묶음을 검증한다. 이 PC의 표준 Windows SDK 경로에서는 CDB를 찾지 못했다.
- 공개 범위의 A/B는 계정별 고정 분할이다. 범용 실험 지표 분석과 대화 그래프에서 연결 자체를 바꾸는 기능은 포함되지 않았다. QA의 문자열 검사는 등록 여부를 확인하며 각 해상도에서 실제로 잘리는지는 화면 검수가 필요하다.
- Unreal 기본 흐름과 네트워크 결과는 Saved/Verification/FinalFeatures/index.json에 있다.
- 맵 배치와 프롤로그 결과는 Saved/Verification/FinalMapsAndIntro/index.json에 있다.
- 최종 포토 모드·타이틀 복귀·프롤로그·맵 결과는 Saved/Verification/FinalPresentation/index.json에 있다.
- 격리 API와 복구 결과는 Saved/FeatureChecks/FinalFeaturesIntegration.log에 있다.
- 게임 이벤트 탭을 연결한 뒤 재실행한 프론트엔드 결과는 Saved/FeatureChecks/FinalFeaturesFrontend.log에 있다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 32개와 진단 대시보드 자체 검사도 통과 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | Unreal에서 100ms 지연·2% 패킷 손실 세 클라이언트, 싱글 타이틀 복귀, 설정, 셔터, 엘리베이터를 검사했다. 6개 실패 0건이며 싱글 복귀와 세 클라이언트 검사에 경고가 있었다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 새 Win64 클라이언트·데디케이티드 서버·런처를 패키징한 뒤 실제 로그인 → 경기 → 층 이동 → 결과 → 로비를 검수한다. 이번 작업에서 운영 환경에 게시하거나 운영 DB를 복원하지 않았다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `eb3e4003` | 2026-11-10 | maru3172 | Quest |

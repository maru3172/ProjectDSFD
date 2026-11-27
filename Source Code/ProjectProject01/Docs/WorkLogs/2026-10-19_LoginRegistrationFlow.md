# 로그인·회원가입 화면 분리 — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`LoginRegistrationFlow.md`](../LoginRegistrationFlow.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 기본 페이지는 아이디·비밀번호와 로그인·회원가입·타이틀 버튼만 표시한다.
- 기간: 2026-10-19 ~ 2026-10-19 (문서 변경 커밋 1개 기준)
- 결과·검증: 가입 성공은 로비 입장이 아니다. 로그인 페이지로 돌아가 새 아이디만 채우고, 비밀번호와 가입 입력을 지운다.
- 현재 상태: 원 문서상 남은 항목 기재 없음(미확인)

## 요청·목표·범위 (9-2)

- 기본 페이지는 아이디·비밀번호와 로그인·회원가입·타이틀 버튼만 표시한다.
- 회원가입 버튼을 눌러야 별도 페이지에서 아이디·비밀번호·닉네임을 입력할 수 있다.
- 새 게임 클라이언트는 `/api/auth/register`에 `loginAfterRegistration:false`를 보낸다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`LoginRegistrationFlow.md`](../LoginRegistrationFlow.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01AuthSubsystem.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01LoginWidget.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01OperationsTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01ReviewRegressionTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01AuthSubsystem.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01GameInstance.h` (1)
- `ProjectProject01/Source/ProjectProject01/Public/ProjectProject01LoginWidget.h` (1)
- `ProjectProject01/Tools/ProjectProject01Backend/Program.cs` (1)
- `ProjectProject01/Tools/ProjectProject01Backend/RegistrationEndpoint.cs` (1)
- `ProjectProject01/Tools/ProjectProject01FeatureChecks/IntegrationChecks.cs` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 가입 실패 또는 중복 아이디는 오류를 표시하고 가입 페이지에 머문다.
- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 로그인/가입 요청 중에는 중복 제출과 입력 변경을 막는다. 상태 조회 응답이 요청 중 버튼을 다시 활성화하지 않는다.
- 가입 페이지의 로그인으로 돌아가기 버튼으로 취소할 수 있다. 진행 중 요청은 완료 또는 시간 초과 후 이동한다.
- 새 흐름 반영에는 변경된 백엔드 재시작과 클라이언트 재패키징이 필요하다. 이 작업에서는 게임 패키징·업데이트 게시·운영 DB 변경을 하지 않는다.
- 화면 렌더 결과는 `Saved/ReviewRemediation/LoginRegistrationUI/`에 기록한다. 인증 테스트는 운영 서버를 호출하지 않는다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 가입 성공은 로비 입장이 아니다. 로그인 페이지로 돌아가 새 아이디만 채우고, 비밀번호와 가입 입력을 지운다.
- 격리 테스트 DB 23307에서 계정 생성 성공, 토큰 미발급, DB 로그인 세션 0개, 같은 아이디 재가입 409를 확인한다.
- 2026-10-19 결과: C++ Editor Development 및 백엔드/도우미/런처/검사 도구 빌드 성공.
- 격리 API·DB 검사 97개와 기존 운영·소셜·매칭·복구 검사가 통과했다.
- 최종 네이티브 보고서 `Saved/ReviewRemediation/LoginRegistration-Final-20261019/index.json`은 위 3개 검사 성공, 경고/실패/미실행 0개다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |
| --- | --- | --- | --- | --- | --- |
| 검사 통과 수 (높을수록 좋음): 3개 검사 성공 | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |

검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 기본 페이지는 아이디·비밀번호와 로그인·회원가입·타이틀 버튼만 표시한다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
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
| `99520446` | 2026-10-19 | maru3172 | Third Improvement |

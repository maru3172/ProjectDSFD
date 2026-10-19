# 로그인·회원가입 화면 분리

- 기본 페이지는 아이디·비밀번호와 로그인·회원가입·타이틀 버튼만 표시한다.
- 회원가입 버튼을 눌러야 별도 페이지에서 아이디·비밀번호·닉네임을 입력할 수 있다.
- 가입 실패 또는 중복 아이디는 오류를 표시하고 가입 페이지에 머문다.
- 가입 성공은 로비 입장이 아니다. 로그인 페이지로 돌아가 새 아이디만 채우고, 비밀번호와 가입 입력을 지운다.
- 로그인/가입 요청 중에는 중복 제출과 입력 변경을 막는다. 상태 조회 응답이 요청 중 버튼을 다시 활성화하지 않는다.
- 가입 페이지의 로그인으로 돌아가기 버튼으로 취소할 수 있다. 진행 중 요청은 완료 또는 시간 초과 후 이동한다.

## API

새 게임 클라이언트는 `/api/auth/register`에 `loginAfterRegistration:false`를 보낸다.
서버는 계정만 생성하고 로그인 세션이나 토큰을 발급하지 않으며 `registrationOnly:true`로 응답한다.
기존 클라이언트/진단 도구와의 호환성을 위해 필드를 생략하는 기존 API 동작은 유지한다.
아이디의 기존 DB 고유 제약과 중복 가입 HTTP 409 처리는 그대로 유지한다.

새 흐름 반영에는 변경된 백엔드 재시작과 클라이언트 재패키징이 필요하다. 이 작업에서는 게임 패키징·업데이트 게시·운영 DB 변경을 하지 않는다.

## 검사

- 격리 테스트 DB 23307에서 계정 생성 성공, 토큰 미발급, DB 로그인 세션 0개, 같은 아이디 재가입 409를 확인한다.
- 네이티브 자동 검사: `ProjectProject01.Operations.LoginRegistration`.
- 관련 회귀: `ProjectProject01.Operations.LobbyReadability`, `ProjectProject01.Review.AsyncState`.
- 화면 렌더 결과는 `Saved/ReviewRemediation/LoginRegistrationUI/`에 기록한다. 인증 테스트는 운영 서버를 호출하지 않는다.

2026-10-19 결과: C++ Editor Development 및 백엔드/도우미/런처/검사 도구 빌드 성공.
격리 API·DB 검사 97개와 기존 운영·소셜·매칭·복구 검사가 통과했다.
최종 네이티브 보고서 `Saved/ReviewRemediation/LoginRegistration-Final-20261019/index.json`은 위 3개 검사 성공, 경고/실패/미실행 0개다.

# 서버 운영 화면 분류

서버 도우미의 메인 화면은 변경하지 않고 서버 운영 창만 5개 항목으로 나눴다. 기존 서버 운영 버튼 18개는 삭제·추가 없이 유지하며 기존 작업 메서드와 확인창을 그대로 사용한다. 백엔드 주소, 상태, 상태 새로고침, 작업 로그는 항목을 바꿔도 보인다. 입력한 계정 ID·점검 예약 문구와 예약 목록 선택도 유지된다.

| 항목 | 기존 버튼 |
|---|---|
| 공통 | 상태 새로고침 |
| 점검·공지 | 점검·공지 적용, 안내 공지, 공지 지우기 |
| 점검 예약 | 점검 예약 추가, 선택 예약 변경, 점검 예약 취소 |
| 계정·신고 | 계정 이용 제한, 플레이어 신고 확인, 선택 계정 로그아웃, 접속자 전원 로그아웃 |
| 콘텐츠·소통 | 런처 뉴스·패치노트·문의, 피드백/설문조사 관리, 사건 기록 조사 수량, 주간 미션 재화 상한 |
| 진단·운영 | 크래시 보고서 보기, 크래시 보관 정책, 최대 방 수 |

## 보존 확인

- 서버 운영: 이번 변경 전 18개 → 변경 후 18개. 이름·작업 연결 불일치 0개.
- 메인 서버 도우미: 분류 도입 커밋 `37df177`의 부모와 현재 코드 비교. 기존 23개(동시 경기 수 적용·방 한도 설정 포함) 모두 유지되고 작업 연결도 동일하다. 개편 후 추가된 것은 진단 대시보드 열기 1개이며 현재 총 24개다. 추가분을 빼면 23개 → 23개다.
- 비교 자료: `Saved/FeatureChecks/HelperButtonPreservationAudit.json`, `ServerAdministrationButtonAudit.json`.

## 검증

2026-10-26 Debug·Release 도구 빌드 경고·오류 0. 네트워크 요청 없이 실시한 서버 운영 UI 검사 98개 통과. 940×760과 최소 크기 740×590에서 항목별 버튼 가시성·글씨 잘림·버튼 겹침·공통 로그 표시·입력 보존 및 메인 도우미의 실제 버튼 구성을 확인했다.

- 실행: `dotnet Tools/ProjectProject01FeatureChecks/bin/Debug/net9.0-windows/ProjectProject01FeatureChecks.dll --server-administration-ui-checks Saved/FeatureChecks/ServerAdministrationUI`
- 로그: `Saved/FeatureChecks/ServerAdministrationUI.log`.
- 화면: `Saved/FeatureChecks/ServerAdministrationUI/server-administration-*.png`.
- 새 실행 파일: `Tools/ProjectProject01BuildServerHelper/bin/Release/net8.0-windows/ProjectDFSDBuildServerHelper.exe`.

게임 패키징, 운영 API 호출·DB 변경·운영 서버 재시작은 하지 않았다. 실행 중인 도우미를 강제로 종료하지 않는다.

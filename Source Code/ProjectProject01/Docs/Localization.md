# ProjectProject01 UI 현지화

한국어를 기본 원문 언어로 사용하며 코드에서 생성하는 공통 UI는
`Content/Localization/ProjectProject01UI.csv` 문자열 테이블을 거칩니다.
버튼, 제목, 입력 안내, 상태 및 오류 문구는 이 표의 한국어 원문을 안정 키로 사용합니다.
런타임 수치, 플레이어 이름과 서버가 반환한 동적 문구는 번역 대상 원문에 결합되어 표시됩니다.

새 언어를 추가할 때 게임 로직 코드를 수정하지 않습니다.

1. Unreal Editor의 `Localization Dashboard`에서 게임용 타깃을 만들거나 기존 타깃을 엽니다.
2. 네이티브 문화를 `ko`로 설정하고 필요한 문화(예: `en`, `ja`)를 추가합니다.
3. `ProjectProject01UI` 문자열 테이블과 프로젝트 소스·에셋의 텍스트를 Gather 합니다.
4. 각 문화의 PO 번역을 가져오고 Compile하여 `.locres`를 생성합니다.
5. 패키징 설정의 Localizations to Package에 해당 문화를 포함합니다.
6. 실행 시 `FProjectProject01Localization::SetCulture("en")` 또는 UE의 문화 설정을 사용합니다.

`ProjectProject01UI.csv` 자체는 기본 한국어 원문과 UI 문구 분리의 기준 파일입니다.
언어별 실제 번역은 Unreal Localization Dashboard가 관리하는 PO/locres에 보관하므로,
번역 추가나 교체 때문에 C++를 다시 편집할 필요가 없습니다.

싱글플레이는 현지화 파일만 사용하며 인증 API나 백엔드 연결을 요구하지 않습니다.
점검, 공지, 강제 로그아웃과 버전 불일치 안내는 멀티플레이 로그인·로비 경로에서만 표시됩니다.

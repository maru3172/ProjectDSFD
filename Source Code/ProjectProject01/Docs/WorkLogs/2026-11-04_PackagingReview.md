# 2026-11-04 패키징 누락 점검(최종 검토)

> 작업 세션 직접 기록 (Claude 세션 local_2e236d8f). 이전 점검 기록: `2026-10-19_PackagingRuntimeReadiness.md`.

## 요약
- 요청(사용자): "시간 남으면 전체 패키징 누락을 확인해줘(최종 검토)".
- 방법: 실제 쿠킹 없이 설정·코드·에셋 참조를 정적으로 대조(당시 에디터·Codex·다른 빌드가 실행 중이라 쿠킹 보류).
- 결과: 누락 확정 1건(한국어 컬처), 확인 필요 2건(PracticeLevel02 참조, 한글 글꼴). 보고만 하고 수정하지 않음. 2026-11-27 기준 `Config/DefaultGame.ini`에도 미반영.

## 조사
- `Config/DefaultGame.ini` 패키징 설정(MapsToCook 9, DirectoriesToAlwaysCook VFX·Title·Data, NonUFS Localization·Branding), `DefaultEngine.ini` 기본 맵.
- 코드의 `/Game/...` 문자열 42종 ↔ 실제 `.uasset` 존재, 원시 파일 읽기, `UE_BUILD_SHIPPING`·`WITH_EDITOR` 분기.
- 엔진 근거: `Engine/Config/BaseGame.ini` 111~113행(`InternationalizationPreset=English`, `+CulturesToStage=en`), `CopyBuildToStagingDirectory.Automation.cs` 1871행(프리셋 ICU만 스테이징).

## 결과
| 구분 | 내용 | 근거 |
|---|---|---|
| 누락 확정 | `Culture=ko`인데 CulturesToStage 미지정 → en ICU만, 패키징본에서 ko 컬처 미발견 가능 | 엔진 설정·스크립트 |
| 확인 필요 | Login·Lobby·LeaderBoardTest 맵에 `/Game/MyProject/Level/PracticeLevel02` 문자열, MapsToCook에 없음 | umap 바이트 검색 |
| 확인 필요 | 기본 글꼴의 한글(DroidSansFallback.ttf) 게임 패키징 포함 근거 없음 | 근거 미발견("모른다") |
| 문제없음 | 코드 참조 에셋 전부 존재, 튜닝 CSV는 에디터 전용, UI 문구 CSV는 튜닝 아님, 백엔드 SQL 2·EOS DLL 2 복사, Shipping은 로컬 비밀 json 미사용 | 코드·csproj |

## 제안(미적용)
`InternationalizationPreset=CJK` + `+CulturesToStage=ko`(en 유지), PracticeLevel02 참조 종류 확인, 첫 패키징에서 한글 표시 확인(검색어: `UE5 packaged game Korean text boxes DroidSansFallback`).

## 검증·정량
쿠킹·패키징·실행 미실행. 정량 해당 없음.

## 의도 대비 달성도 (9-9-1)
| 의도 | 수용 기준 | 결과·근거 | 구분 | 판정 | 남은 격차 |
|---|---|---|---|---|---|
| 패키징 누락을 미리 찾음 | 누락 후보 목록 | 확정 1·확인 필요 2·문제없음 목록 | 기능(점검) | 부분 달성 | 실제 쿠킹 확인 없음 |
| 패키징본이 한국어로 정상 동작 | ko 컬처·한글 표시 | 미실행 | 효과 | 미검증 | 설정 반영 후 패키징 실행 |

## 인계
위 설정 추가 → BuildServerHelper 패키징(클라이언트 Shipping, `PackagingPreflight.cs`의 BuildCookRun) 1회 → 한글·컬처·PracticeLevel02 이동 확인.

# 마네킹 행동·상태 UI·재질 SFX 변경 (2026-10-18) — 개발 기록

> 소급 작성: 2026-11-27 (KST). 원 문서 [`MannequinFeedbackAudioRefresh.md`](../MannequinFeedbackAudioRefresh.md)와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.

## 요약

- 목표: 이후 공포 마네킹 음향, 피격 호흡·사망 비명, 피해 VFX 요청은 `MannequinHorrorAndPainFeedback.md`에 이어 기록했다. 아래의 기존 검사 기록은 당시 작업 결과다.
- 기간: 2026-10-19 ~ 2026-10-19 (문서 변경 커밋 1개 기준)
- 결과·검증: 백화점 석재·단일 박동 후속 조정: 첫 오디오 검사는 5건 성공, 발소리 검사 1건 실패였다. 이는 서로 다른 소리를 총 에너지 차이로만 비교하던 부적절한 검사 기준으로, 음량이 비슷해도 서로 다른 재질일 수 있어 정규화된 파형 차이 비교로 바로잡았다. 최종 `Saved/AudioMaterialRefresh/PolishedStoneSingleBeatVerified/index.json`은 6건 성공, 경고·실패 0건이다. 직전 심장 주 박동의 음색·감쇠·길이는 유
- 현재 상태: 미완료·미검증 항목 있음(아래 9-10)

## 요청·목표·범위 (9-2)

- 이후 공포 마네킹 음향, 피격 호흡·사망 비명, 피해 VFX 요청은 `MannequinHorrorAndPainFeedback.md`에 이어 기록했다. 아래의 기존 검사 기록은 당시 작업 결과다.
- 플레이어: 백화점의 매끈하고 단단한 석재 바닥에 맞춰 짧고 건조한 착지 충격을 중심으로 구성했다. 모래처럼 들리던 지속 마찰 잡음은 계수를 약 90% 줄이고 훨씬 짧게 감쇠한다. 걷기·달리기의 간격은 기존 그대로이며 음색의 지속 시간과 충격 강도가 다르다. 공명도 짧게 감쇠해 맑게 울리는 링잉을 피한다. 급정거 발 끌기는 별도 연출로 유지한다.
- 파트너: 같은 운동화 음색. 기본 음량은 플레이어 0.85 대비 0.425로 50%다. 같은 거리·차폐·사용자 음량 조건에서의 신호 크기 기준이다. 플레이어/파트너가 서로 다른 위치에 있으면 실제 들리는 크기는 달라진다.
- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.

## 기존 구조·흐름 (9-3)

원 문서 [`MannequinFeedbackAudioRefresh.md`](../MannequinFeedbackAudioRefresh.md) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):

- `ProjectProject01/Source/ProjectProject01/Private/FootstepSynthComponent.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/HelperRearGuardAIController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestGameMode.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlasticJointSynthComponent.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerCharacter.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerFeedbackWidgets.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/PlayerHeartbeatComponent.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01CheckpointPreviewTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01DamageFeedbackTests.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01DiagnosticsSubsystem.cpp` (1)
- `ProjectProject01/Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp` (1)

문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).

## 문제·증상 (9-4)

- 이 밖의 발견 계기·재현 절차: 미확인.

## 판단·결정 (9-5)

- 전시물 위장 행동을 삭제했다. 위장 후보 위치 선택, 접근·방향 맞춤·대기, 재사용 시간, 관련 GameMode 설정·상태·옛 테스트를 제거했다. 정찰·주변 동향 관찰·감지 추격·시야 정지·영구 명령은 유지한다. 싱글 AI/분산 추적은 이번 변경 대상이 아니다.
- 기존 좌측 상단 빙의 마네킹 정보 아래에 `생존자 추격 중: 1번, 3번` / `생존자 시야로 정지: 2번`을 표시한다. 없으면 `없음`으로 바뀐다. 번호는 오름차순이며 시야 정지 중인 마네킹은 움직이는 추격 목록에서 제외한다. 수색·추종·정찰은 추격으로 세지 않는다.
- 서버에서 복제되는 실제 행동·시야 정지 상태를 0.2초마다 읽는다. 통신·복제 지연을 제외한 갱신 간격이며 새 감지 판정이나 클라이언트 승패 로직은 추가하지 않는다.
- 소유 클라이언트의 서버 역할 안내로 확인된 마네킹 역할만 표시한다. 서버 전용 인증 문자열에 의존하지 않으며 생존자에게 남아 있는 위젯도 숨긴다. 빙의 대상이 잠깐 미확정이어도 전체 번호 표시는 유지한다.
- 기존 Blueprint의 WalkSounds/RunSounds 할당이 합성 개선을 덮지 않도록 기본은 합성음이다. 외부 WAV를 쓰려면 Footstep 컴포넌트의 `Footstep > Sounds > Use Assigned Sound Assets`를 명시적으로 켠다. 기존 음원 배열이나 에셋은 삭제하지 않는다.
- 청취용 WAV 6개를 `Saved/AudioMaterialRefresh`에 생성했다. 자동 검사는 음색의 주관적 만족도나 실제 멀티 PC의 소리·복제 지연까지 보장하지 않는다.
- 기존 실행 서버와 배포 폴더·DB는 변경하지 않는다. 배포 반영에는 클라이언트와 데디케이티드 서버를 다시 빌드·패키징해야 한다. 이번 작업에 백엔드 코드 변경은 없다. 실제 외부 PC에서의 복제·청취 및 음색에 대한 사용자 확인은 별도다.
- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.

## 구현·시행착오 (9-6)

- 구현 내용: 원 문서와 위 변경 파일 목록 참조.
- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.

## 검사·검증 (9-7)

- 백화점 석재·단일 박동 후속 조정: 첫 오디오 검사는 5건 성공, 발소리 검사 1건 실패였다. 이는 서로 다른 소리를 총 에너지 차이로만 비교하던 부적절한 검사 기준으로, 음량이 비슷해도 서로 다른 재질일 수 있어 정규화된 파형 차이 비교로 바로잡았다. 최종 `Saved/AudioMaterialRefresh/PolishedStoneSingleBeatVerified/index.json`은 6건 성공, 경고·실패 0건이다. 직전 심장 주 박동의 음색·감쇠·길이는 유
- Unreal Editor Development 및 Win64 Shipping 빌드 성공. 실제 패키징·업데이트 게시·데디 서버 패키징은 실행하지 않았다.
- `Saved/AudioMaterialRefresh/FinalNativeReport/index.json`: 10건 성공, 경고·실패 0건. 재질별 발소리·50% 파트너 신호·버퍼 연속성·박동·거리/차폐 회귀 검사, 실제 위젯의 번호 정렬/정지 우선/이전 알림 제거/생존자 숨김, 기존 카메라·시야각·공격 거리·정찰 경로 인계·대형 유지 검사를 포함한다.
- 초기 UI 테스트 실행은 가상 컨트롤러에 LocalPlayer가 없어 테스트 구성 오류로 중단됐다. Engine 소유 LocalPlayer를 연결한 뒤 위 최종 보고서로 재실행했다. 초기 `NativeTests.log`는 이 실패 이력이며 최종 결과는 `FinalNativeTests.log`와 `FinalNativeReport`다.
- `git diff --check` 통과. 기존 보완 작업은 중단 상태이며 이번 빌드 성공이 그 작업 전체의 검증 완료를 뜻하지 않는다.
- 전체 검증 서술은 원 문서 참조.

## 정량 결과 (9-8)

**정량 검증 미완료** — 원 문서에 측정 기준값·측정값이 없다.

## 의도 대비 달성도 (9-9)

| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |
| --- | --- | --- | --- | --- | --- |
| 기능 구현 | 이후 공포 마네킹 음향, 피격 호흡·사망 비명, 피해 VFX 요청은 `MannequinHorrorAndPainFeedback.md`에 이어 기록했다. 아래의 기존 검사 기록은 당시 작업 결과다. | 원 문서에 수용 기준으로 명시되지 않음 | 원 문서 검증 기록 있음(9-7) | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |
| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |
| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |

## 최종 결과·기여 (9-9)

- 커밋 작성자(이 문서를 바꾼 커밋 수): maru3172 1
- AI 관여: 커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.
- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.

## 미완료·후속·인계 (9-10)

- 사용자가 기존 30개 보완·8개 확인 작업을 중단하도록 지시했다. 해당 개선 작업을 재개하지 않고 아래 요청만 처리한다. 이전에 적용된 미완료 개선 코드는 되돌리지 않는다.
- 심장: 기존 거리·조우·BPM·강도·음량 시스템은 유지한다. 사용자 후속 요청으로 0.105초 뒤 닫힘 박동을 제거하고, `UpdateBeatOutput`이 계산한 `60 / CurrentBPM` 간격의 기존 이벤트마다 단일 `둥`만 재생한다. 별도 타이머·추가 박동·BPM 배수는 없다. 기존 하트비트 활성/해제와 위험도 기반 강도 제어를 그대로 따른다.

## 관련 커밋

| 커밋 | 날짜 | 작성자 | 메시지 |
| --- | --- | --- | --- |
| `507e11ce` | 2026-10-19 | maru3172 | Enhanced VFX and SFX direction |

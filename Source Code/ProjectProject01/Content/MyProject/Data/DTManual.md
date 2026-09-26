## 데이터 테이블 사용 방법

1. AITuning.csv, HelperTuning.csv를 수정
2. Tools > ProjectProject01 Tuning > Reimport and Validate Tuning DataTables
3. 이미 PIE 중이면 즉시 반영, 아니면 다음 PIE 시작 때 자동 반영

## 주의사항
※ 첫 행은 반드시 Field,Value
※ 필드 이름은 바꾸지 않기
※ 각 필드는 한 번만 쓰기
※ 숫자 소수점은 0.25처럼 점을 사용하기
※ 엔진 DataTable 에셋을 직접 열면 엔진 구조상 가로 표로 보일 수 있음, 편집 원본은 세로 CSV임.


사용되는 변수

## HelperTuning: 파트너

`HelperWalkSpeed`

파트너의 `CharacterMovement.MaxWalkSpeed`에 적용되는 최대 걷기 속도다. 단위는 cm/s다. 600은 UE 5.8.2 `UCharacterMovementComponent`의 기본 걷기 속도다.

`FollowDistance` 조력자가 플레이어 뒤에서 목표로 삼는 거리

`MinimumFollowSeparation` 플레이어에게 절대 가까워지지 않는 최소 거리

`GuardSightRadius`

파트너가 마네킹을 감시할 수 있는 최대 거리다. 단위는 cm이다. 거리를 넘으면 각도와 장애물 조건을 만족해도 감지하지 않는다.

`GuardHalfAngleDegrees`

파트너가 실제로 마네킹을 감시하는 부채꼴의 한쪽 각도다. 단위는 도이며 기본값 60은 총 120도다. 실제 시야의 중심과 양 끝은 `GuardSearchHalfAngleDegrees` 커버 범위를 절대 넘지 않도록 런타임에서 제한된다.

`GuardSearchHalfAngleDegrees`

파트너가 실제 시야의 중심을 움직일 수 있는 커버 범위의 한쪽 각도다. 단위는 도이며 기본값 90은 총 180도다. 기준은 플레이어 카메라가 아니라 플레이어의 실제 수평 이동 방향 반대다. 커버 안의 가장 가까운 마네킹 2기 사이 각도가 실제 총 시야각 이하면 중간 방향을, 초과하면 가장 가까운 마네킹 방향을 선택한다. 후보가 없으면 이동 반대 방향을 유지한다. 디버그에서는 청록색이 커버 범위, 노란색이 실제 시야, 녹색 선이 실제 시야 중심이다.

`VisionDirectionChangeWindowSeconds`

플레이어가 정지했을 때 파트너의 실제 시야 중심 변경을 누적하는 시간창이다. 단위는 초이며 기본값은 3이다.

`VisionDirectionChangeRequiredCount`

시간창 안에서 실제 시야 중심이 유효하게 변경되어야 하는 최소 횟수다. 기본값은 5다.

`VisionDirectionChangeThresholdDegrees`

직전에 기록한 실제 시야 중심 방향과 이 값 이상 차이 날 때만 유효 변경 한 번으로 센다. 단위는 도이며 기본값은 5다. 시간창 안에서 필요한 횟수에 도달하면 파트너는 실제 시야 중심의 반대 방향으로 `MinimumFollowSeparation × 2`만큼 이동한다. 이동할 NavMesh 경로에 플레이어 캡슐이 있으면 플레이어를 같은 방향으로 밀고 데스카운트를 한 번 차감한다. 후방 이격이 시작된 뒤에는 플레이어가 실제 이동 입력을 넣을 때까지 추적 거리 갱신을 잠근다. 디버그의 주황색 선은 잠긴 후방 이격 목표다.

`RepathInterval` 새 이동 경로 요청 간격

`RepathDistance` 목표가 이만큼 변해야 경로를 다시 요청

`AcceptanceRadius` 목표 지점 도착 판정 거리

`StuckTimeout` 이동 진전이 없을 때 정체로 판단하는 시간

`StuckWaitTime` 정체 뒤 다음 경로 요청까지 대기 시간

`ProgressDistance` “실제로 움직였다”고 인정하는 최소 이동량



## AITuning: 생존자와 마네킹

`PlayerWalkSpeed`

생존자 플레이어의 `CharacterMovement.MaxWalkSpeed`에 적용되는 최대 걷기 속도다. 단위는 cm/s다. 멀티플레이에서는 서버가 적용한 값을 소유 클라이언트에도 복제한다.

`MannequinWalkSpeed`

AI 상태와 플레이어가 조종 중인 상태 모두의 마네킹 `CharacterMovement.MaxWalkSpeed`에 적용되는 최대 걷기 속도다. 단위는 cm/s다. 조종 중인 클라이언트에도 복제한다.

`DirectChaseRadius`

빨간 원의 반경이다. 마네킹이 이 거리 안에 있으면 방향·집결 조건과 무관하게 생존자를 직접 추격한다. 단위는 cm다.

`RoamingOuterRadius`

바깥 원의 반경이자 랜덤 배회와 중간 부채꼴 판정의 최대 길이다. 이 거리 밖의 마네킹은 기존 규칙대로 이동을 중단한다. 단위는 cm다.

`PostPossessionCommandDurationSeconds`

마네킹 플레이어가 다른 마네킹으로 옮겨 이전 마네킹의 빙의가 해제된 뒤, 기록된 명령을 기존 AI보다 우선하는 시간이다. 단위는 초이며 기본값은 5다. 직접 조작 중 `E`를 눌렀다면 이 시간 동안 `RoamingOuterRadius` 안의 가장 가까운 생존자를 추격하고, `E`를 누르지 않았다면 같은 시간 동안 제자리에서 정지한다. 시간이 끝나면 기존 AI 행동 로직으로 복귀한다.

`DirectChaseHalfAngleDegrees`

중간 고리에서 플레이어의 이동 방향을 기준으로 직접 추격과 반대편 랜덤 배회를 나누는 부채꼴의 한쪽 각도다. 단위는 도이며, 부채꼴의 총 각도는 값의 두 배다. 부채꼴의 길이는 `DirectChaseSectorRadius`를 사용한다. 이 값은 각도이므로 실제 동작은 0~180도 범위로 제한된다.

`DirectChaseSectorRadius`

플레이어 이동 방향을 기준으로 한 직접 추격 부채꼴의 끝 반경이다. 단위는 cm이며 기본값 1000이다. 빨간 원(`DirectChaseRadius`) 바깥부터 이 거리까지의 부채꼴에 있는 마네킹은 직접 추격한다. 값이 빨간 원보다 작으면 빨간 원 반경으로, `RoamingOuterRadius`보다 크면 바깥 원 반경으로 안전하게 제한된다.

MannequinGatherRadius 마네킹들이 집결한 것으로 판단하는 거리

RequiredMannequinCount 배회 행동 전환에 필요한 주변 마네킹 수

SurvivorVisionHalfAngleDegrees 생존자가 마네킹을 보고 있다고 판정하는 시야각

SurvivorVisionCheckIntervalSeconds 시야 정지 판정 주기. 낮을수록 즉각적이지만 서버 검사량 증가

DetectionMargin	마네킹 신체 외곽까지 시야 감지 여유

VisionFovMarginMultiplier	카메라 FOV보다 약간 넓게 “봤다”고 판정하는 보정값

### AITuning: 플레이어 심박수

다음 심박 값은 생존자 플레이어의 로컬 `UPlayerHeartbeatComponent`에 적용된다. 멀티플레이에서는 서버가 검증한 값을 각 생존자의 소유 클라이언트로 전달하므로, 서로 다른 생존자의 심박 판정은 각자 시야를 기준으로 독립적으로 계산된다. CSV 저장 후 `Reimport and Validate Tuning DataTables`를 누르면 실행 중인 PIE에는 즉시 적용되며, 심박의 인지·쿨다운 상태는 새 수치로 다시 계산한다.

`MinBPM`

심박 대상이 멀리 있을 때 적용되는 최소 BPM이다.

`MaxDistanceBPM`

심박 대상이 플레이어와 매우 가까울 때 적용되는 거리 기반 최대 BPM이다.

`MaxEncounterBPM`

최초 발견 및 재발견 상승을 포함한 최종 BPM 상한이다.

`MinEncounterBoost`

먼 거리에서 마네킹을 갑자기 발견했을 때 추가되는 최소 BPM이다.

`MaxEncounterBoost`

가까운 거리에서 마네킹을 갑자기 발견했을 때 추가되는 최대 BPM이다.

`BPMDecayPerSecond`

발견으로 상승한 BPM이 초당 감소하는 양이다.

`HeartbeatRange`

마네킹 거리를 BPM으로 변환하고 최초 발견을 판정하는 기준 거리다. 단위는 cm다.

`RetentionRange`

이미 인지한 마네킹을 거리 기반 심박에 계속 반영할 최대 거리다. `bUseRetentionRules`가 `true`일 때만 유지 규칙에 사용된다. 단위는 cm다.

`RecognitionHalfAngleDegrees`

최초 발견과 재발견을 판정하는 플레이어 시야의 한쪽 각도다. 총 시야각은 값의 두 배다. 단위는 도다.

`bUseRetentionRules`

`true`면 이미 인지한 마네킹에 대해 `RetentionRange`, `RetentionHalfAngleDegrees`, `LostSightGraceSeconds`를 사용하는 유지 규칙을 켠다. `false`면 현재 발견 시야와 `HeartbeatRange`만 사용한다. 값은 반드시 `true` 또는 `false`다.

`RetentionHalfAngleDegrees`

인지한 마네킹의 심박 반응을 유지하는 넓은 범위의 한쪽 각도다. `bUseRetentionRules`가 `true`일 때만 사용한다. 단위는 도다.

`LostSightGraceSeconds`

마네킹이 유지 조건을 벗어난 뒤에도 심박 반응을 유지하는 유예 시간이다. `bUseRetentionRules`가 `true`일 때만 사용한다. 단위는 초다.

`SurpriseRearmDelay`

마네킹이 이 시간 이상 보이지 않아야 재발견 상승이 다시 준비되는 시간이다. `bEnableRediscovery`가 `true`일 때만 사용한다. 단위는 초다.

`SurpriseCooldown`

마지막 발견 상승 이후 다음 발견 상승을 허용하기까지의 최소 대기 시간이다. `bEnableRediscovery`가 `true`일 때만 사용한다. 단위는 초다.

`bEnableRediscovery`

`true`면 최초 발견 이후에도 재발견 BPM 상승을 허용한다. `false`면 최초 발견 상승만 허용한다. 값은 반드시 `true` 또는 `false`다.

`VisionCheckInterval`

플레이어 시야와 마네킹 노출 여부를 다시 검사하는 주기다. 낮을수록 반응은 빨라지지만 각 클라이언트의 시야 추적 비용이 늘어난다. 단위는 초다.

`MannequinRefreshInterval`

월드에 생성되거나 파괴된 마네킹 목록을 다시 찾는 주기다. 단위는 초다.

`bEnableHeartbeatLog`

`false`면 BPM 계산은 유지하면서 `LogTemp`의 심박 로그만 끈다. 값은 반드시 `true` 또는 `false`다.

`BPMLogThreshold`

직전 로그 출력 BPM과의 차이가 이 값 이상일 때만 상태 로그를 새로 출력한다. 단위는 BPM이다.


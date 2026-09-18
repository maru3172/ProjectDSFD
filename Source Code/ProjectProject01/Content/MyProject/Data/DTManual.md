## 데이터 테이블 사용 방법

1. AITuning.csv, HelperTuning.csv를 수정
2. Tools > ProjectProject01 Tuning > Reimport and Validate Tuning DataTables
3. 이미 PIE 중이면 즉시 반영, 아니면 다음 PIE 시작 때 자동 반영



사용되는 변수

HelperTuning: 파트너

`FollowDistance` 조력자가 플레이어 뒤에서 목표로 삼는 거리

`MinimumFollowSeparation` 플레이어에게 절대 가까워지지 않는 최소 거리

`GuardSightRadius` 조력자의 후방 감시 거리

`GuardHalfAngleDegrees` 후방 감시 시야. 총 시야각은 140도

`RepathInterval` 새 이동 경로 요청 간격

`RepathDistance` 목표가 이만큼 변해야 경로를 다시 요청

`AcceptanceRadius` 목표 지점 도착 판정 거리

`StuckTimeout` 이동 진전이 없을 때 정체로 판단하는 시간

`StuckWaitTime` 정체 뒤 다음 경로 요청까지 대기 시간

`ProgressDistance` “실제로 움직였다”고 인정하는 최소 이동량



AITuning: 기본 적

DirectChaseRadius 이 거리 안이면 직접 추격

RoamingOuterRadius 이 거리 밖이면 현재 AI가 멈추는 외부 한계

MannequinGatherRadius 마네킹들이 집결한 것으로 판단하는 거리

RequiredMannequinCount 배회 행동 전환에 필요한 주변 마네킹 수

SurvivorVisionHalfAngleDegrees 생존자가 마네킹을 보고 있다고 판정하는 시야각

SurvivorVisionCheckIntervalSeconds 시야 정지 판정 주기. 낮을수록 즉각적이지만 서버 검사량 증가

DetectionMargin	마네킹 신체 외곽까지 시야 감지 여유

VisionFovMarginMultiplier	카메라 FOV보다 약간 넓게 “봤다”고 판정하는 보정값


export const fields = {
  title:'제목', entry:'협업 기록 ID', version:'검증 빌드·버전', intent:'의도', hypothesis:'문제 가정',
  cause:'원인', solution:'해결 방법', steps:'재현 절차', expected:'기대 결과',
  before:'변경 전 결과', result:'실제 결과', logs:'로그·진단 근거', metrics:'성능 수치와 측정 환경',
  testedCommit:'검사 대상 Git 커밋', commit:'수정 커밋', board:'진단 보드 버전', timeline:'실측 타임라인 (시간 · 이벤트 · 상태)',
  risk:'남은 리스크', portfolio:'포트폴리오 기록', change:'버전 변경 내용', checklist:'반복 체크리스트',
} as const;
export const options = {
  category:['서버','클라이언트','기획','AI','UI','빌드','성능'],
  kind:['기능','버그','성능','빌드','플레이테스트'],
  priority:['P0','P1','P2'], status:['구현됨','진행 중','수정 필요','미착수'],
  outcome:['미실행','통과','실패','차단'], regression:['미확인','없음','있음'],
} as const;
export type Item = Record<keyof typeof fields | keyof typeof options, string> & {
  item_id:string; assignee:string; revision:number; demo:boolean; author?:string; saved_at?:string;
};
export const blank = {
  ...Object.fromEntries(Object.keys(fields).map(key=>[key,''])),
  ...Object.fromEntries(Object.entries(options).map(([key,values])=>[key,values[0]])),
  assignee:'', revision:0, demo:true, status:'미착수', priority:'P1',
  checklist:'[ ] 재현 환경과 빌드 확인\n[ ] 테스트 반복 실행\n[ ] 로그와 결과 기록',
} as Omit<Item,'item_id'>;
const cases = [
 ['multiplayer','멀티플레이','클라이언트','기능','두 클라이언트에서 합류·이탈·재접속을 반복한다.','양쪽의 플레이어와 게임 진행 상태가 일치한다.'],
 ['dedicated','데디케이티드 서버','서버','기능','패키징한 서버를 실행하고 외부 클라이언트 두 대를 접속시킨다.','로비에서 게임 시작까지 진행된다.'],
 ['ai','공포 AI 추적과 배회','AI','플레이테스트','플레이어가 시야에 들어왔다가 엄폐한다.','추적·배회 전환과 공포 연출을 확인할 수 있다.'],
 ['affix','어픽스','기획','기능','동일 조건에서 어픽스 적용 전후를 비교한다.','설계된 효과와 중첩 조건을 따른다.'],
 ['pvp','PvP 이벤트전','기획','플레이테스트','이벤트 진입부터 승패 결정, 일반 진행 복귀까지 진행한다.','참가자에게 같은 승패가 표시된다.'],
 ['metrics','로그·성능 측정 유틸리티','성능','성능','동일 맵·설정으로 3회 측정한다.','측정 조건, 프레임 시간과 서버·클라이언트 로그가 남는다.'],
 ['cleanup','AI 자동 정리 누락 보완툴','AI','기능','누락과 정상 항목을 섞어 정리를 두 번 실행한다.','누락 항목이 보완되고 재실행으로 중복이 생기지 않는다.'],
 ['unit','유닛 테스트 실행기','빌드','빌드','정상 케이스와 의도적으로 실패하는 케이스를 실행한다.','통과·실패가 구분되고 실패 사유와 종료 코드가 남는다.'],
 ['ui','UI 실험 테스트','UI','플레이테스트','해상도와 입력 장치를 바꾸며 주요 화면을 이동한다.','조작과 표시가 유지된다.'],
 ['asset','에셋 로딩 테스트','빌드','성능','패키징한 빌드에서 첫 로딩과 재진입을 측정한다.','누락 에셋이 없고 로딩 시간과 메모리를 비교할 수 있다.'],
 ['build','시연용 패키징','빌드','빌드','깨끗한 환경에 시연 빌드를 풀고 서버와 클라이언트를 실행한다.','시연 맵을 열고 종료까지 진행된다.'],
 ['obs-freeze','정지 중 관찰 보존 · 재개','AI','버그','직접 시야 확보 → 플레이어가 마네킹을 봄 → 정지 중 시야를 가림 → 만료 전 재개','정지만으로 관찰이 삭제되지 않는다. 재개 시 만료되지 않은 관찰값을 확인한다.'],
 ['obs-expire','관찰 갱신 · 만료 경계','AI','버그','UpdatePlayerObservation 호출 후 시야를 가리고 수명 직전·경계·직후에 UpdatePlayerObservationConfidence 결과를 기록한다.','직접 시야에서 시각과 위치·속도가 갱신된다. 만료 후 유효 관찰이 제거된다.'],
 ['obs-switch','대상 전환 · 마네킹 간 격리','AI','버그','플레이어 A/B와 마네킹 두 개로 대상 전환을 유도한다.','이전 대상의 값이 다음 대상이나 다른 마네킹으로 섞이지 않는지 확인한다.'],
 ['obs-clear','대상 소실 · 잘못된 대상 타입','AI','버그','플레이어 제거와 비플레이어 대상 조건을 각각 재현한다. ClearPlayerObservation과 TickNode 진입 여부를 기록한다.','유효 플레이어가 없으면 관찰과 추적값을 초기화한다. 비플레이어는 새 관찰을 만들지 않는다.'],
 ['obs-bb','블랙보드 전달 · BT Service','AI','기능','TickNode에서 관찰 구조체를 읽고 네 키를 갱신하는 순서를 정상·정지·만료에서 기록한다.','HasPlayerObservation, ObservationConfidence, LastSeenPlayerLocation, LastSeenPlayerVelocity가 관찰 구조체와 일치한다.'],
 ['obs-sync','데디케이티드 서버 · 클라이언트 동기화','서버','버그','두 클라이언트에서 정지·재개·이탈을 재현하고 서버 진단 시간과 클라이언트 표시를 대조한다.','서버가 추적을 결정하고 클라이언트에서 관찰 가능한 정지·재개 상태가 일치한다.'],
 ['obs-diag','진단 보드 · 함수 단위 검증','AI','기능','최신 진단 보드에서 SetFrozen, UpdatePlayerObservation, UpdatePlayerObservationConfidence, ClearPlayerObservation, TickNode 경로별 호출·결과를 비교한다.','함수별 진단 표시를 로그로 대조할 수 있고, 표시되지 않는 경로는 근거 공백으로 기록한다.'],
];
export const seeds:Item[] = cases.map(([item_id,title,category,kind,steps,expected])=>({
  ...blank,item_id,title,category,kind,steps,expected,intent:expected,
  status:item_id==='build'?'미착수':'구현됨',priority:item_id.startsWith('obs-')?'P0':'P1',
}));

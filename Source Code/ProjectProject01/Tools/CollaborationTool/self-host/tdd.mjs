import {Problem,now,q,rows,run,text} from './runtime.mjs';

const fields=['intent','assumption','alternatives','criteria','before','after','metrics','compatibility','decision','rationale','scope','references','hold','review','contract','flow','impact','change'];
export function tddAction(action,value,workspace,author){
  if(action==='tddList')return {history:rows('SELECT * FROM tdd_revisions WHERE workspace_id=? ORDER BY saved_at DESC, revision DESC',workspace)
    .map(({payload,workspace_id,...row})=>({...JSON.parse(payload),...row}))};
  const id=text(value.decision_id,80,true);
  const payload={title:text(value.title,200,true),system:text(value.system,100,true),category:text(value.category,30,true),status:text(value.status,20,true)};
  if(!['아키텍처','데이터 구조','프레임 예산','디버그 도구','네트워크 계약','서버·클라이언트 역할'].includes(payload.category)||
    !['제안','검증 중','보류','확정'].includes(payload.status))throw new Problem(400,'분류와 상태를 확인하세요.');
  for(const field of fields)payload[field]=text(value[field],5000);
  if(payload.status==='확정'&&(!payload.decision||!payload.rationale))throw new Problem(400,'최종 결정과 근거를 입력하세요.');
  if(payload.status==='보류'&&(!payload.hold||!payload.review))throw new Problem(400,'보류 이유와 재검토 조건을 입력하세요.');
  const latest=q('SELECT revision,payload FROM tdd_revisions WHERE workspace_id=? AND decision_id=? ORDER BY revision DESC LIMIT 1',workspace,id);
  const revision=latest?.revision??0,serialized=JSON.stringify(payload);
  if(latest?.payload===serialized)return {ok:true};
  if(revision!==value.revision)throw new Problem(409,'다른 팀원이 변경했습니다. 이력을 확인하고 다시 열어 주세요.');
  run('INSERT INTO tdd_revisions VALUES (?,?,?,?,?,?)',workspace,id,revision+1,author,now(),serialized);
  return {ok:true};
}

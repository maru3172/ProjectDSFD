import {Problem, now, q, rows, run, text, transaction} from './runtime.mjs';

const choices = {
  category: ['서버','클라이언트','기획','AI','UI','빌드','성능'],
  kind: ['기능','버그','성능','빌드','플레이테스트'],
  priority: ['P0','P1','P2'],
  status: ['구현됨','진행 중','수정 필요','미착수'],
  outcome: ['미실행','통과','실패','차단'],
  regression: ['미확인','없음','있음'],
};
const notes = ['intent','hypothesis','cause','solution','steps','result','metrics','risk','portfolio','checklist','change','expected','before','logs','testedCommit','commit','board','timeline'];

export function ue5Action(action, value, workspace, author) {
  const tracking = action.startsWith('tracking');
  const read = () => rows("SELECT item_id, revision, author, saved_at, payload FROM ue5_revisions WHERE workspace_id=? AND (substr(item_id,1,9)='tracking-')=? ORDER BY saved_at DESC, revision DESC", workspace, Number(tracking))
    .map(({payload, ...row}) => ({assignee:'',testedCommit:'', ...JSON.parse(payload), ...row}));
  if (action === 'ue5List' || action === 'trackingList') return {history: read()};
  const item = text(value.item_id, 80, true);
  if(item.startsWith('tracking-')!==tracking)throw new Problem(400,'검증 항목을 확인하세요.');
  const payload = {title: text(value.title, 200, true), version: text(value.version, 80, true), demo: value.demo === true};
  for (const [field, options] of Object.entries(choices)) {
    if (!options.includes(value[field])) throw new Problem(400, '선택 항목을 확인하세요.');
    payload[field] = value[field];
  }
  payload.assignee = text(value.assignee, 200);
  payload.entry = text(value.entry, 80);
  if (payload.entry && !q('SELECT id FROM entries WHERE id=? AND workspace_id=? AND deleted_at IS NULL', payload.entry, workspace)) throw new Problem(400, '이 공간의 협업 기록을 선택하세요.');
  for (const field of notes) payload[field] = text(value[field], 5000);
  if(tracking){
    if(!/^[0-9a-f]{7,64}$/i.test(payload.version))throw new Problem(400,'검증 기준 Git 커밋을 입력하세요.');
    for(const field of ['blackboard','gameplay','nextEvidence'])payload[field]=text(value[field],5000);
    if(payload.outcome==='통과'&&(!payload.blackboard||!payload.gameplay||!payload.logs||/^\[ \]/m.test(payload.checklist)))
      throw new Problem(400,'통과를 기록하려면 체크리스트를 완료하고 블랙보드 값, 로그와 게임플레이 결과를 입력하세요.');
  }
  if (payload.outcome !== '미실행' && !payload.result) throw new Problem(400, '실제 테스트 결과를 입력하세요.');
  transaction(() => {
    const latest = q('SELECT revision, payload FROM ue5_revisions WHERE workspace_id=? AND item_id=? ORDER BY revision DESC LIMIT 1', workspace, item);
    const revision = latest?.revision ?? 0;
    const serialized = JSON.stringify(payload);
    if (latest?.payload === serialized) return;
    if (revision !== value.revision) throw new Problem(409, '다른 팀원이 수정했습니다. 최신 이력을 확인한 뒤 다시 저장하세요.');
    if (payload.assignee && payload.assignee !== (latest && JSON.parse(latest.payload).assignee)
      && !q("SELECT 1 FROM members WHERE workspace_id=? AND user_id=? AND role<>'viewer'", workspace, payload.assignee)) {
      throw new Problem(400, '현재 편집 가능한 팀원을 담당자로 선택하세요.');
    }
    run('INSERT INTO ue5_revisions VALUES (?,?,?,?,?,?)', workspace, item, revision + 1, author, now(), serialized);
  });
  return {history: read()};
}

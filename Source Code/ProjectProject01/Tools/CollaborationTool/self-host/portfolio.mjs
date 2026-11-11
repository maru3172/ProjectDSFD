import {Problem,q,rows,run,text} from './runtime.mjs';

const notes=['problem','implementation','contribution','cause','solution','result','review','uncertainty','summary','stack','metrics','next'];
export function portfolioAction(action,value,workspace){
  const project=text(value.project,80,true),week=text(value.week,10,true);
  const start=new Date(week+'T00:00:00Z');
  if(!/^\d{4}-\d{2}-\d{2}$/.test(week)||!Number.isFinite(+start)||start.toISOString().slice(0,10)!==week||start.getUTCDay()!==1)
    throw new Problem(400,'주차의 월요일을 선택하세요.');
  if(!q('SELECT id FROM projects WHERE id=? AND workspace_id=?',project,workspace))throw new Problem(404,'프로젝트가 없습니다.');
  if(action==='portfolioList'){
    const end=new Date(+start+7*86400000).toISOString().slice(0,10);
    return {
      claims:rows('SELECT id,revision,payload FROM portfolio_claims WHERE workspace_id=? AND project_id=? AND week=? ORDER BY rowid',workspace,project,week)
        .map(({payload,...row})=>({...JSON.parse(payload),...row})),
      attachments:rows("SELECT a.id,a.name,a.content_type,e.title FROM attachments a JOIN entries e ON e.id=a.entry_id AND e.workspace_id=a.workspace_id WHERE a.workspace_id=? AND e.project_id=? AND a.deleted_at IS NULL AND e.deleted_at IS NULL AND e.day>=? AND e.day<? ORDER BY e.day DESC,a.name",workspace,project,week,end),
      unassigned:rows("SELECT id,title,day,version,substr(body,1,4000) body FROM entries WHERE workspace_id=? AND project_id IS NULL AND kind IN ('log','note') AND deleted_at IS NULL AND day>=? AND day<? ORDER BY day DESC,title",workspace,week,end),
      logs:rows("SELECT id,title,day,substr(body,1,4000) body FROM entries WHERE workspace_id=? AND project_id=? AND kind IN ('log','note') AND deleted_at IS NULL AND day>=? AND day<? ORDER BY day DESC,title",workspace,project,week,end),
    };
  }
  const id=text(value.id,80,true);
  const claim={title:text(value.title,200,true),category:text(value.category,40,true),source:text(value.source,30,true),
    status:text(value.status,30,true),priority:text(value.priority,2,true),log:text(value.log,80),commit:text(value.commit,64)};
  if(!['작업일지 주장','커밋 확인','실행 검증 필요'].includes(claim.source)||
    !['확인됨','부분 확인','추가 증거 필요'].includes(claim.status)||!['P0','P1','P2'].includes(claim.priority))
    throw new Problem(400,'검증 유형, 상태와 우선순위를 확인하세요.');
  if(claim.commit&&!/^[0-9a-f]{7,64}$/i.test(claim.commit))throw new Problem(400,'Git 커밋 ID를 확인하세요.');
  if(claim.log&&!q("SELECT id FROM entries WHERE id=? AND workspace_id=? AND project_id=? AND kind IN ('log','note') AND deleted_at IS NULL",claim.log,workspace,project))
    throw new Problem(400,'이 프로젝트의 작업일지를 선택하세요.');
  for(const field of notes)claim[field]=text(value[field],5000);
  if(!Array.isArray(value.evidence)||value.evidence.length>30)throw new Problem(400,'근거는 항목당 최대 30개까지 연결할 수 있습니다.');
  claim.evidence=value.evidence.map(item=>{
    const kind=text(item?.kind,30,true),url=text(item?.url,2000,true),label=text(item?.label,200,true);
    const attachment=url.match(/^\/api\/file\?id=([a-zA-Z0-9-]+)$/)?.[1];
    const validAttachment=attachment&&q("SELECT 1 FROM attachments a JOIN entries e ON e.id=a.entry_id AND e.workspace_id=a.workspace_id WHERE a.id=? AND a.workspace_id=? AND e.project_id=? AND a.deleted_at IS NULL AND e.deleted_at IS NULL",attachment,workspace,project);
    if(!['커밋','실행 로그','스크린샷','테스트 결과','문서'].includes(kind)||(!validAttachment&&!/^https?:\/\/[^\s]+$/i.test(url)))
      throw new Problem(400,'근거 유형과 HTTP(S) 링크를 확인하세요.');
    return {kind,url,label};
  });
  if(claim.status!=='추가 증거 필요'&&!claim.review)throw new Problem(400,'어디까지 확인했는지 검토 근거를 기록하세요.');
  if(claim.status==='확인됨'&&(claim.source==='작업일지 주장'||
    (claim.source==='커밋 확인'&&!claim.commit&&!claim.evidence.some(item=>item.kind==='커밋'))||
    (claim.source==='실행 검증 필요'&&(!claim.result||!claim.evidence.some(item=>['실행 로그','스크린샷','테스트 결과'].includes(item.kind))))))
    throw new Problem(400,'확인됨으로 저장하려면 주장 유형에 맞는 근거를 연결하세요.');
  const previous=q('SELECT revision,payload FROM portfolio_claims WHERE workspace_id=? AND id=? AND project_id=? AND week=?',workspace,id,project,week);
  const revision=previous?.revision??0,payload=JSON.stringify(claim);
  if(previous?.payload===payload)return {ok:true};
  if(revision!==value.revision)throw new Problem(409,'다른 팀원이 수정했습니다. 새로 고친 뒤 다시 열어 주세요.');
  if(previous)run('UPDATE portfolio_claims SET revision=?,payload=? WHERE workspace_id=? AND id=?',revision+1,payload,workspace,id);
  else run('INSERT INTO portfolio_claims VALUES(?,?,?,?,?,?)',workspace,id,project,week,1,payload);
  return {ok:true};
}

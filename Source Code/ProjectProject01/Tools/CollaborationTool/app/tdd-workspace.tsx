"use client";
import {useEffect,useState} from 'react';
import {copyText,randomId} from './browser-crypto';
import {safeRequest} from './safe-request';
import './ue5-tracker.css';

const categories=['아키텍처','데이터 구조','프레임 예산','디버그 도구','네트워크 계약','서버·클라이언트 역할'];
const statuses=['제안','검증 중','보류','확정'];
const groups=[
  ['문제와 평가',{intent:'의도',assumption:'문제 가정',alternatives:'선택지',criteria:'평가 기준'}],
  ['프로토타입 비교',{before:'검증 전 기준선',after:'프로토타입 검증 결과',metrics:'성능 수치와 측정 조건'}],
  ['시스템 계약과 영향',{contract:'기술 계약 · 소유권 / 입력 / 출력 / 오류 처리',flow:'데이터 흐름 · 출발 → 처리 → 도착',compatibility:'호환성 영향',impact:'변경 영향 · 의존 시스템 / 마이그레이션 / 롤백',scope:'적용 범위',references:'관련 코드·문서'}],
  ['결정과 재검토',{decision:'최종 결정',rationale:'결정 근거',hold:'보류 이유',review:'재검토 조건',change:'이번 변경 요약'}],
] as const;
const fields=groups.flatMap(([,group])=>Object.entries(group));
type Decision={decision_id:string;revision:number;title:string;system:string;category:string;status:string;author?:string;saved_at?:string}&Record<string,string|number|undefined>;
const create=():Decision=>({decision_id:randomId(),revision:0,title:'',system:'',category:categories[0],status:statuses[0]});
const day=(value?:string)=>value?new Date(value).toLocaleString('ko-KR',{timeZone:'Asia/Seoul'}):'';

export default function TddWorkspace({workspace,canWrite}:{workspace:string;canWrite:boolean}){
  const [history,setHistory]=useState<Decision[]>([]);
  const [draft,setDraft]=useState<Decision|null>(null);
  const [dirty,setDirty]=useState(false);
  const [view,setView]=useState('board');
  const [system,setSystem]=useState('');
  const [busy,setBusy]=useState(true);
  const [loaded,setLoaded]=useState(false);
  const [notice,setNotice]=useState('');
  async function request(item?:Decision){
    const response=await safeRequest(item?'/api/features':'/api/features?action=tddList&workspace='+encodeURIComponent(workspace),
      item?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...item,action:'tddSave',workspace})}:undefined);
    const body=await response.json() as {history?:Decision[];error?:string};
    if(!response.ok)throw new Error(body.error||'기술 결정을 불러오지 못했습니다.');
    return body;
  }
  async function load(item?:Decision){
    setBusy(true);setNotice('');
    try{
      if(item){await request(item);setDirty(false);setDraft(null);setNotice('팀 공간에 저장했습니다.');}
      const body=await request();
      setHistory(body.history??[]);setLoaded(true);
    }catch(error){setNotice((error as Error).message)}
    finally{setBusy(false)}
  }
  useEffect(()=>{void load()},[workspace]);
  const latest=new Map<string,Decision>();
  for(const row of history)if(!latest.has(row.decision_id))latest.set(row.decision_id,row);
  const items=[...latest.values()];
  const systems=[...new Set(items.map(item=>item.system))].sort();
  const shown=items.filter(item=>!system||item.system===system);
  const pending=shown.filter(item=>item.status!=='확정');
  const confirmed=shown.filter(item=>item.status==='확정');
  function open(item:Decision|null){
    if(dirty&&!window.confirm('저장하지 않은 변경을 버릴까요?'))return;
    setDraft(item);setDirty(false);
    if(item)requestAnimationFrame(()=>document.getElementById('tdd-editor')?.focus());
  }
  function set(key:string,value:string){
    if(draft){setDraft({...draft,[key]:value});setDirty(true)}
  }
  const markdown=['# DSFD 기술 설계 문서 (TDD)',system?'시스템 · '+system:'범위 · 전체 시스템',
    ...confirmed.map(item=>[
      '## '+item.system+' / '+item.title,
      item.category+' · 개정 '+item.revision+' · '+day(item.saved_at),
      ...fields.filter(([key])=>!['hold','change'].includes(key)&&item[key]).flatMap(([key,label])=>['### '+label,String(item[key])]),
    ].join('\n\n')),
  ].join('\n\n');
  function download(){
    const url=URL.createObjectURL(new Blob([markdown],{type:'text/markdown;charset=utf-8'}));
    const link=document.createElement('a');link.href=url;link.download='DSFD-TDD.md';link.click();
    setTimeout(()=>URL.revokeObjectURL(url),1000);
  }
  const card=(item:Decision)=><button key={item.decision_id} type="button" className="secondary ue5-card" disabled={busy} onClick={()=>open(item)}>
    <strong>{item.title}</strong><small>{item.system} · {item.category} · {item.status}</small>
  </button>;
  return <section className="ue5-tracker">
    <h3>기술 결정·TDD</h3>
    <p className="muted">시스템 설계, 기술 검증, 계약과 변경 영향을 팀 공간에서 관리합니다.</p>
    {notice&&<p role="status">{notice}</p>}
    <div className="ue5-controls">
      <label>시스템<select value={system} onChange={e=>setSystem(e.target.value)}><option value="">전체</option>{systems.map(name=><option key={name}>{name}</option>)}</select></label>
      <button className="secondary" disabled={busy} onClick={()=>void load()}>새로 고침</button>
      {canWrite&&<button className="primary" disabled={busy||!loaded} onClick={()=>open({...create(),system})}>기술 결정 추가</button>}
    </div>
    {loaded&&<>
      <div className="ue5-stats"><div><strong>{pending.length}</strong>미결정</div><div><strong>{pending.filter(item=>item.status==='보류').length}</strong>보류</div><div><strong>{confirmed.length}</strong>확정</div></div>
      <nav className="segmented" aria-label="기술 결정 화면">{Object.entries({board:'미결정 보드',pending:'Pending Decisions',compare:'검증 전·후',contracts:'시스템 계약·흐름',history:'결정 이력',export:'TDD 문서'}).map(([key,label])=>
        <button key={key} className={view===key?'selected':''} onClick={()=>setView(key)}>{label}</button>)}</nav>
      {view==='board'&&<div className="ue5-grid">{statuses.slice(0,3).map(status=><section key={status}><h4>{status}</h4>{pending.filter(item=>item.status===status).map(card)}</section>)}</div>}
      {view==='pending'&&(pending.length?pending.map(item=><details key={item.decision_id}>
        <summary>{item.title} · {item.status}</summary><p>{item.hold||'보류 이유 미기록'}</p><p>재검토 · {item.review||'조건 미기록'}</p>{card(item)}
      </details>):<p>미결정 항목이 없습니다.</p>)}
      {view==='compare'&&shown.map(item=><details key={item.decision_id}>
        <summary>{item.title} · {item.status}</summary>
        <div className="ue5-grid">
          <section><h4>검증 전 기준선</h4><p>{item.before||'미기록'}</p></section>
          <section><h4>검증 후 프로토타입 결과</h4><p>{item.after||'미기록'}</p></section>
          <section><h4>성능 수치와 측정 조건</h4><p>{item.metrics||'미기록'}</p></section>
        </div>{card(item)}
      </details>)}
      {view==='contracts'&&shown.map(item=><details key={item.decision_id}>
        <summary>{item.system} · {item.title} · {item.status}</summary>
        <p>기술 계약 · {item.contract||'미기록'}</p>
        <p>데이터 흐름 · {item.flow||'미기록'}</p>
        <p>호환성 · {item.compatibility||'미기록'}</p>
        <p>변경 영향 · {item.impact||'미기록'}</p>{card(item)}
      </details>)}
      {view==='history'&&history.filter(item=>!system||item.system===system).map(row=><details key={row.decision_id+':'+row.revision}>
        <summary>{row.title} · 개정 {row.revision} · {row.status} · {row.author}</summary>
        <p>{day(row.saved_at)} · {row.change||'변경 요약 미기록'}</p>
        <div className="ue5-grid">{fields.filter(([key])=>row[key]).map(([key,label])=><section key={key}><h4>{label}</h4><p>{row[key]}</p></section>)}</div>
        {card(latest.get(row.decision_id)!)}
      </details>)}
      {view==='export'&&<><h4>확정 결정 기술 설계 문서</h4>{confirmed.length?<>
        <textarea aria-label="기술 설계 문서" rows={15} readOnly value={markdown}/>
        <div className="inline"><button className="secondary" onClick={download}>Markdown 내려받기</button><button className="secondary" onClick={()=>void copyText(markdown).then(()=>setNotice('TDD 문서를 복사했습니다.'),()=>setNotice('문서를 복사하지 못했습니다.'))}>문서 복사</button></div>
      </>:<p>선택한 범위에 확정 결정이 없습니다.</p>}</>}
    </>}
    {draft&&<form className="team-meeting ue5-editor" onSubmit={e=>{e.preventDefault();if(canWrite&&!busy)void load(draft)}}>
      <h4 id="tdd-editor" tabIndex={-1}>{draft.title||'새 기술 결정'}</h4>
      <fieldset disabled={!canWrite||busy}>
        <div className="ue5-controls">
          <label>제목<input required maxLength={200} value={draft.title} onChange={e=>set('title',e.target.value)}/></label>
          <label>시스템<input required maxLength={100} value={draft.system} onChange={e=>set('system',e.target.value)}/></label>
          <label>분류<select value={draft.category} onChange={e=>set('category',e.target.value)}>{categories.map(value=><option key={value}>{value}</option>)}</select></label>
          <label>상태<select value={draft.status} onChange={e=>set('status',e.target.value)}>{statuses.map(value=><option key={value}>{value}</option>)}</select></label>
        </div>
        {groups.map(([label,group])=><section key={label}><h4>{label}</h4><div className="ue5-grid">
          {Object.entries(group).map(([key,name])=><label key={key}>{name}<textarea maxLength={5000} rows={4} value={String(draft[key]??'')} onChange={e=>set(key,e.target.value)}/></label>)}
        </div></section>)}
      </fieldset>
      <div className="inline">{canWrite&&<button className="primary" disabled={busy} type="submit">저장</button>}<button className="secondary" disabled={busy} type="button" onClick={()=>open(null)}>닫기</button></div>
    </form>}
  </section>;
}

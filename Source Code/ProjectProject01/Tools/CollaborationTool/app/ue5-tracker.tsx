"use client";
import {useEffect,useRef,useState} from 'react';
import {safeRequest} from './safe-request';
import {copyText,randomId} from './browser-crypto';
import {blank,fields,options,seeds,type Item} from './ue5-cases';
import './ue5-tracker.css';
import Ue5Inspection from './ue5-inspection';
import Ue5BuildComparison from './ue5-build-comparison';
import type {Member} from './types';

const verified = (item:Item) => item.status==='구현됨' && item.outcome==='통과' &&
  item.regression==='없음' && !!item.result && !/^\[ \]/m.test(item.checklist);
const observation = (item:Item) => item.item_id.startsWith('obs-');
const evidence = (item:Item) => !!item.result && !!item.logs;
const similar = (a:Item,b:Item) => {
  const normalize = (value:string) => value.toLowerCase().replace(/[^\p{L}\p{N}]/gu,'');
  return (a.title && normalize(a.title)===normalize(b.title)) ||
    (a.steps && normalize(a.steps)===normalize(b.steps));
};

const resetRun = (item:Item):Partial<Item> => ({
  before:item.result||item.before, outcome:'미실행', regression:'미확인', result:'', logs:'',
  metrics:'', timeline:'', change:'', testedCommit:'', checklist:item.checklist.replace(/^\[x\]/gm,'[ ]'),
});
function pendingReasons(row:Item|undefined,needsLogs:boolean) {
  if(!row)return ['미실행',...(needsLogs?['근거 부족']:[])];
  const reasons=[];
  if(row.outcome==='미실행')reasons.push('미실행');
  if(row.outcome==='실패'||row.outcome==='차단')reasons.push(row.outcome);
  if(!row.result||(needsLogs&&!row.logs))reasons.push('근거 부족');
  if(row.regression==='있음')reasons.push('회귀 있음');
  if(row.regression==='미확인')reasons.push('회귀 여부 미확인');
  if(row.status!=='구현됨')reasons.push('구현 상태 · '+row.status);
  const remaining=row.checklist.split('\n').filter(line=>/^\[ \]/.test(line)).length;
  if(remaining)reasons.push('체크리스트 '+remaining+'개 남음');
  return reasons;
}
export default function Ue5Tracker({workspace,canWrite,entry,onOpen,members}:{workspace:string;canWrite:boolean;entry:string|null;onOpen:(id:string)=>void;members:Member[]}) {
  const [history,setHistory]=useState<Item[]|null>(null);
  const [draft,setDraft]=useState<Item|null>(null);
  const [category,setCategory]=useState('');
  const [kind,setKind]=useState('');
  const [query,setQuery]=useState('');
  const [version,setVersion]=useState('');
  const [board,setBoard]=useState('');
  const [busy,setBusy]=useState(false);
  const [notice,setNotice]=useState('');
  const [mode,setMode]=useState('board');
  const [userId,setUserId]=useState('');
  const [mine,setMine]=useState(false);
  const [savedDraft,setSavedDraft]=useState<Item|null>(null);
  const storageKey=useRef('');
  const editorRef=useRef<HTMLHeadingElement>(null);
  function keepDraft(item:Item|null) {
    setSavedDraft(item);
    if(!storageKey.current)return;
    try{
      if(item)localStorage.setItem(storageKey.current,JSON.stringify(item));
      else localStorage.removeItem(storageKey.current);
    }catch{setNotice('이 브라우저에서 초안을 자동 저장할 수 없습니다.')}
  }
  function edit(item:Item) {
    setDraft(item);
    keepDraft(item);
  }
  function focusEditor() {
    requestAnimationFrame(()=>editorRef.current?.focus());
  }
  function openItem(item:Item) {
    if(busy)return;
    if(savedDraft&&canWrite){
      if(!window.confirm('저장하지 않은 초안을 버리고 다른 기록을 열까요?'))return;
      keepDraft(null);
    }
    setDraft(item);
    focusEditor();
  }
  async function load(item?:Item) {
    setBusy(true);
    setNotice('');
    try {
      const response=await safeRequest(item?'/api/features':'/api/features?action=ue5List&workspace='+encodeURIComponent(workspace),
        item?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...item,workspace,action:'ue5Save'})}:undefined);
      const body=await response.json() as {history:Item[];userId:string;error?:string};
      if(!response.ok)throw new Error(body.error||'검증 기록을 불러올 수 없습니다.');
      setHistory(body.history);
      setUserId(body.userId);
      const key='daybook-ue5-draft:'+JSON.stringify([workspace,body.userId]);
      if(storageKey.current!==key){
        storageKey.current=key;
        try{
          const stored=localStorage.getItem(key);
          if(stored)setSavedDraft({...blank,...JSON.parse(stored)});
        }catch{setNotice('이 브라우저의 저장된 초안을 읽을 수 없습니다.')}
      }
      if(item){keepDraft(null);setDraft(null);setNotice('팀 공간에 저장했습니다.');window.dispatchEvent(new Event('daybook-saved'))}
    } catch(error){setNotice((error as Error).message)}
    finally {setBusy(false)}
  }
  useEffect(()=>{void load()},[workspace]);
  const revisions=history??[];
  const latest=new Map(seeds.map(item=>[item.item_id,item]));
  for(const item of [...revisions].reverse())latest.set(item.item_id,item);
  const items=[...latest.values()];
  const versions=[...new Set(revisions.map(item=>item.version))];
  const chosenVersion=version||versions[0]||'';
  const boards=[...new Set(revisions.filter(item=>observation(item)&&item.version===chosenVersion&&item.board).map(item=>item.board))];
  const chosenBoard=boards.includes(board)?board:boards[0]||'';
  const demo=items.filter(item=>item.demo);
  const versionRecord=(item:Item)=>revisions.find(row=>row.item_id===item.item_id&&row.version===chosenVersion);
  const demoReady=demo.filter(item=>{const row=versionRecord(item);return row&&verified(row)});
  const ai=items.filter(observation);
  const boardRecord=(item:Item)=>revisions.find(row=>row.item_id===item.item_id&&row.version===chosenVersion&&row.board===chosenBoard);
  const boardReady=ai.filter(item=>{const row=boardRecord(item);return chosenBoard&&row&&verified(row)&&evidence(row)});
  const gaps=ai.filter(item=>{const row=boardRecord(item);return !chosenBoard||!row||!evidence(row)});
  const myItems=mine?items.filter(item=>item.assignee===userId):items;
  const pending=myItems.flatMap(item=>{
    const isAi=observation(item);
    if(!item.demo&&!isAi)return [];
    const row=isAi?(chosenBoard?boardRecord(item):undefined):versionRecord(item);
    const reasons=pendingReasons(row,isAi);
    if(isAi&&!chosenBoard)reasons.push('진단 보드 미선택');
    return reasons.length?[{item,row,reasons}]:[];
  }).sort((left,right)=>left.item.priority.localeCompare(right.item.priority));
  function openPending(item:Item,row?:Item) {

    const target=row?{...row,assignee:item.assignee,revision:item.revision}:{
      ...item,...resetRun(item),version:chosenVersion,board:observation(item)?chosenBoard:'',
    };
    openItem(target);
  }
  const visible=myItems.filter(item=>(!category||item.category===category)&&(!kind||item.kind===kind)&&
    [item.title,item.steps,item.commit,item.testedCommit].some(value=>value.toLowerCase().includes(query.toLowerCase())));
  const duplicates=draft?revisions.filter(row=>row.item_id!==draft.item_id&&similar(draft,row)):[];
  const report=[`UE5 졸업작품 · ${chosenVersion||'빌드 미선택'}`,
    `시연 검증 ${demoReady.length}/${demo.length} · AI 진단 ${boardReady.length}/${ai.length}`,
    '관찰 상태 설계 · 직접 시야에서 관찰을 갱신하고, 시야를 잃으면 신뢰도와 수명을 검사한다. BT Service가 관찰값을 블랙보드에 전달한 뒤 정지 또는 추적을 처리한다.',
    ...items.flatMap(item=>{
      const row=observation(item)?boardRecord(item):versionRecord(item);
      return row&&verified(row)&&(!observation(item)||boardReady.includes(item))?
        [`• ${row.title} — ${row.portfolio||row.result}${row.metrics?' / '+row.metrics:''}`]:[];
    }),
    ...gaps.map(item=>'근거 미확보 · '+item.title),
  ].join('\n');
  const set = (values:Partial<Item>)=>{if(draft)edit({...draft,...values})};
  const memberName=(id:string)=>members.find(member=>member.user_id===id)?.name||(id?'이전 팀원':'미지정');
  async function copyPending(){
    const lines=pending.map(({item,reasons})=>`- [ ] ${item.priority} ${item.title} · ${memberName(item.assignee)} · ${reasons.join(', ')}`);
    try{
      await copyText([`UE5 남은 검증 · ${chosenVersion||'빌드 미선택'} · AI 진단 보드 ${chosenBoard||'미선택'}`,...lines].join('\n'));
      setNotice('남은 검증 목록을 복사했습니다.');
    }catch{setNotice('목록을 복사할 수 없습니다. 브라우저의 클립보드 권한을 확인하세요.')}
  }
  return <section className="ue5-tracker">
    <h3>UE5 졸업작품 검증</h3>
    <p className="muted">기능·버그·성능·빌드·플레이테스트를 같은 빌드 기준으로 확인합니다. 기본 시나리오는 테스트 계획입니다.</p>
    {notice&&<p role="status">{notice}</p>}
    <div className="ue5-controls">
      <label>검증 빌드<select value={chosenVersion} onChange={e=>setVersion(e.target.value)}>{(versions.length?versions:['']).map(value=><option key={value} value={value}>{value||'기록 없음'}</option>)}</select></label>
      <label>AI 진단 보드<select value={chosenBoard} onChange={e=>setBoard(e.target.value)}>{(boards.length?boards:['']).map(value=><option key={value} value={value}>{value||'기록 없음'}</option>)}</select></label>
      <button className="secondary" disabled={busy} onClick={()=>void load()}>새로 고침</button>
      {canWrite&&<button className="primary" disabled={busy||!history} onClick={()=>openItem({...blank,item_id:(mode==='ai'?'obs-':'')+randomId(),version:chosenVersion,entry:entry||'',category:mode==='ai'?'AI':blank.category})}>{mode==='ai'?'AI 시나리오 추가':'항목 추가'}</button>}
    </div>
    {history&&<>
      <div className="ue5-stats">
        <div><strong>{demoReady.length}/{demo.length}</strong>시연 검증 완료</div>
        <div><strong>{Math.round(boardReady.length/ai.length*100)}%</strong>AI 검증 완료 ({boardReady.length}/{ai.length})</div>
        <div><strong>{gaps.length}</strong>AI 근거 미확보</div>
      </div>
      <label className="checkline"><input type="checkbox" checked={mine} onChange={e=>setMine(e.target.checked)}/>내 담당 항목만</label>
      <button type="button" className="secondary" aria-expanded={mode==='pending'} onClick={()=>setMode(mode==='pending'?'board':'pending')}>남은 검증 항목 보기 ({pending.length})</button>
      <nav className="segmented" aria-label="검증 화면">{Object.entries({board:'우선순위 보드',ai:'AI 수명주기',history:'버전 이력',compare:'빌드 비교',inspection:'검사·Git',summary:'실패·발표 요약'}).map(([key,label])=>
        <button key={key} className={mode===key?'selected':''} onClick={()=>setMode(key)}>{label}</button>)}</nav>
      {mode==='pending'&&<section>
        <h4>남은 검증 항목</h4>
        <button type="button" className="secondary" disabled={!pending.length} onClick={()=>void copyPending()}>팀 채팅용으로 목록 복사</button>
        <p className="muted">{chosenVersion||'빌드 미선택'} · AI 진단 보드 {chosenBoard||'미선택'}</p>
        {pending.length===0?<p>표시할 남은 검증 항목이 없습니다.</p>:
          <div className="ue5-grid">{pending.map(({item,row,reasons})=>
            <button key={item.item_id} type="button" className="secondary ue5-card" onClick={()=>openPending(item,row)}>
              <strong>{item.title}</strong>
              <small>{item.priority} · {item.category} · {memberName(item.assignee)} · {reasons.join(' · ')}</small>
            </button>
          )}</div>}
      </section>}
      {mode==='board'&&<>
        <div className="ue5-controls">
          <label>검색<input value={query} onChange={e=>setQuery(e.target.value)}/></label>
          <label>분류<select value={category} onChange={e=>setCategory(e.target.value)}><option value="">전체</option>{options.category.map(value=><option key={value}>{value}</option>)}</select></label>
          <label>유형<select value={kind} onChange={e=>setKind(e.target.value)}><option value="">전체</option>{options.kind.map(value=><option key={value}>{value}</option>)}</select></label>
        </div>
        <div className="ue5-grid">{options.priority.map(priority=><section key={priority}>
          <h4>{priority}</h4>{visible.filter(item=>item.priority===priority).map(item=><button key={item.item_id} className="secondary ue5-card" onClick={()=>openItem(item)}>
            <strong>{item.title}</strong><small>{item.category} · {item.kind} · {memberName(item.assignee)} · {versionRecord(item)?.outcome||'선택 빌드 미실행'}</small>
          </button>)}
        </section>)}</div>
      </>}
      {mode==='ai'&&<>
        <ol className="ue5-timeline"><li>직접 시야 → 위치·속도·관찰 시각 갱신</li><li>시야 상실 → 신뢰도 감소 → 만료 시 관찰 제거</li><li>BT Service → 블랙보드 → 정지·재개 또는 추적·배회</li><li>유효 플레이어 소실 → 관찰·추적 초기화</li></ol>
        {ai.map(item=>{const row=boardRecord(item);return <details key={item.item_id}><summary>{item.title} · {gaps.includes(item)?'근거 미확보':row?.outcome}</summary>
          <p>{item.steps}</p><p>기대 결과 · {item.expected}</p>
          {row&&<><p>변경 전 · {row.before||'미기록'}</p><p>변경 후 · {row.result||'미기록'}</p><ol>{row.timeline.split('\n').filter(Boolean).map((line,index)=><li key={index}>{line}</li>)}</ol></>}
          <button className="secondary" onClick={()=>openItem(item)}>기록 열기</button>
        </details>})}
      </>}
      {mode==='inspection'&&<Ue5Inspection workspace={workspace} onEvidence={canWrite&&!busy?(logs,metadata)=>{
        if(!draft&&savedDraft&&!window.confirm('저장하지 않은 초안을 버리고 새 기록에 로그를 담을까요?'))return;
        if(draft?.version&&metadata.version&&draft.version!==metadata.version&&!window.confirm('보고서와 초안의 빌드 버전이 다릅니다. 현재 초안에 로그를 넣을까요?'))return;
        {
          const current=draft??{...blank,item_id:randomId(),category:'빌드',kind:'빌드',entry:entry||''};
          edit({...current,logs,version:current.version||metadata.version,
            testedCommit:current.testedCommit||(!current.version||!metadata.version||current.version===metadata.version?metadata.testedCommit:'')});
        }
        setNotice('로그를 초안에 담았습니다. 저장 전 빌드와 검사 대상 커밋을 확인하세요.');
        requestAnimationFrame(()=>editorRef.current?.focus());
      }:undefined}/>}
      {mode==='compare'&&<Ue5BuildComparison items={myItems} revisions={revisions} versions={versions} base={chosenVersion}/>}
      {mode==='history'&&<><h4>{chosenVersion||'빌드 미선택'} 변경 이력</h4>{revisions.filter(row=>row.version===chosenVersion).map(row=><details key={row.item_id+':'+row.revision}>
        <summary>{row.title} · 개정 {row.revision} · {row.author}</summary>
        <p>{row.saved_at&&new Date(row.saved_at).toLocaleString('ko-KR',{timeZone:'Asia/Seoul'})} · {row.change||'변경 설명 미기록'} · {row.commit||'커밋 미기록'}</p>
        <p>변경 전 · {row.before||'미기록'}</p><p>변경 후 · {row.result||'미기록'}</p><p>{row.logs}</p>
      </details>)}</>}
      {mode==='summary'&&<><h4>실패·차단 원인</h4>
        <ul>{items.flatMap(item=>{const row=versionRecord(item);return row&&['실패','차단'].includes(row.outcome)?[<li key={row.item_id}>{row.title} · {row.cause||'원인 미확인'} · {row.risk||'리스크 미기록'}</li>]:[]})}</ul>
        <h4>최종 발표·포트폴리오 요약</h4><textarea aria-label="발표 요약" readOnly rows={12} value={report}/>
      </>}
    </>}
    {!draft&&savedDraft&&canWrite&&<div className="inline">
      <span>이 브라우저에 저장된 초안 · {savedDraft.title||'새 검증 항목'}</span>
      <button type="button" className="secondary" onClick={()=>{setDraft(savedDraft);focusEditor()}}>초안 이어서 작성</button>
      <button type="button" className="secondary" onClick={()=>{if(window.confirm('저장되지 않은 초안을 버릴까요?'))keepDraft(null)}}>초안 버리기</button>
    </div>}
    {draft&&<form className="team-meeting ue5-editor" onSubmit={e=>{e.preventDefault();void load(draft)}}>
      <h4 ref={editorRef} tabIndex={-1}>{draft.title||'새 검증 항목'}</h4>
      <fieldset disabled={!canWrite||busy}>
        <div className="ue5-controls">{Object.entries(options).map(([key,values])=><label key={key}>{({category:'분류',kind:'유형',priority:'우선순위',status:'구현 상태',outcome:'테스트 결과',regression:'회귀 여부'})[key as keyof typeof options]}
          <select value={draft[key as keyof typeof options]} onChange={e=>set({[key]:e.target.value})}>{values.map(value=><option key={value}>{value}</option>)}</select>
        </label>)}</div>
        <label>담당자<select value={draft.assignee} onChange={e=>set({assignee:e.target.value})}>
          <option value="">미지정</option>
          {draft.assignee&&!members.some(member=>member.user_id===draft.assignee&&member.role!=='viewer')&&
            <option value={draft.assignee}>{memberName(draft.assignee)} (현재 배정 불가)</option>}
          {members.filter(member=>member.role!=='viewer').map(member=><option key={member.user_id} value={member.user_id}>{member.name}</option>)}
        </select></label>
        <label className="checkline"><input type="checkbox" checked={draft.demo} onChange={e=>set({demo:e.target.checked})}/>최종 시연 필수</label>
        <div className="ue5-grid">{Object.entries(fields).map(([key,label])=><label key={key}>{label}
          {['title','version','commit','testedCommit','board','entry'].includes(key)?
            <input required={key==='title'||key==='version'} maxLength={key==='title'?200:key==='version'?80:5000} value={draft[key as keyof typeof fields]} onChange={e=>set({...(key==='version'||key==='board'?resetRun(draft):{}),[key]:e.target.value})}/>:
            <textarea rows={3} maxLength={5000} value={draft[key as keyof typeof fields]} onChange={e=>set({[key]:e.target.value})}/>}
        </label>)}</div>
        <h4>체크리스트 실행</h4>
        {draft.checklist.split('\n').map((line,index)=>/^\[[ x]\]/.test(line)&&<label key={index} className="checkline">
          <input type="checkbox" checked={line.startsWith('[x]')} onChange={e=>set({checklist:draft.checklist.split('\n').map((value,i)=>i===index?(e.target.checked?'[x]':'[ ]')+value.slice(3):value).join('\n')})}/>{line.slice(3)}
        </label>)}
      </fieldset>
      <details open={duplicates.length>0}><summary>이전 테스트 비교 · 중복 후보 {duplicates.length}건</summary>
        {duplicates.map(row=><p key={row.item_id+':'+row.revision}>{row.title} · {row.version} · {row.outcome}<br/>{row.steps}<br/>{row.result}<br/><button type="button" className="secondary" onClick={()=>{const item=latest.get(row.item_id);if(item)openPending(item,observation(item)&&chosenBoard?boardRecord(item):versionRecord(item))}}>기존 기록 열기</button></p>)}
        {revisions.filter(row=>row.item_id===draft.item_id).slice(0,8).map(row=><p key={row.revision}>{row.version} · 개정 {row.revision} · {row.outcome}<br/>{row.result||'결과 미기록'}</p>)}
      </details>
      <div className="inline">
        {draft.entry&&<button className="secondary" type="button" onClick={()=>onOpen(draft.entry)}>연결된 협업 기록 열기</button>}
        {canWrite&&<button className="primary" disabled={busy} type="submit">저장</button>}
        {canWrite&&<button className="secondary" disabled={busy} type="button" onClick={()=>set(resetRun(draft))}>다음 반복 테스트 준비</button>}
        <button className="secondary" disabled={busy} type="button" onClick={()=>setDraft(null)}>닫기</button>
      </div>
    </form>}
  </section>;
}

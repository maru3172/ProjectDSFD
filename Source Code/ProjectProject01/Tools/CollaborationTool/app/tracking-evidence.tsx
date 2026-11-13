"use client";
import {useEffect,useState} from 'react';
import {blank,type Item} from './ue5-cases';
import {safeRequest} from './safe-request';
import './ue5-tracker.css';

const baseline='9a6dc0d70ecab5e6e3421985664e9f9eb43bc50d';
const commits=[
 ['1946ba7c223f653cb32385c6de02dfbe967d76a0','feat(ai): distinguish player observation from helper observation','코드가 플레이어 시선 플래그를 별도로 저장하고, 플레이어 또는 보조 관측의 합산 결과로 정지를 처리합니다.'],
 ['99403a4f8961098946a1e6c33da4813ccf23bbd9','feat(ai): add tracking evidence blackboard key','Git diff에서 BB_MannequinAIBoard.uasset 변경을 확인했습니다. 에디터에서 키와 값 표시를 확인할 필요가 있습니다.'],
];
const cases=[
 ['direct','P0','직접 관측','시야 없음 → 1차 마네킹이 플레이어 직접 관측 → 추적','NavMesh 투영 성공 시 DirectObservation (1). 2차 마네킹이 1차의 직접 관측을 공유받는 경우에도 값은 1.','1·2차 마네킹의 ChaseTier, TrackingEvidence와 목표 위치를 함께 캡처'],
 ['helper','P0','보조 관측','플레이어 시선 차단 → 보조 관측만 성립 → 정지','합산 시선 상태로 정지하고 플레이어 시선 플래그는 false. TrackingEvidence는 추적 경로에 따라 별도로 결정.','보조 관측만 활성인 장면과 플레이어 시선 플래그를 함께 기록'],
 ['clue','P0','정지 단서','2차가 공유 관측 확보 → 현재 직접 관측 제거 → 플레이어 시선으로 멈춘 단서 제공','단서 계산 성공 시 StoppedMannequinClue (2). 직접 관측이 다시 유효하면 값 1이 우선.','공유 관측, 정지 원인, 추적 목표 및 1→2→1 전환 로그'],
 ['prediction','P1','속도 예측','이동 중 관측 → 시야 차단 → 유효한 마지막 위치·속도 유지 → 정지 단서 제외','예측 위치 계산 성공 시 VelocityPrediction (3). 목표 위치를 기록하고 마지막 관측 위치와 비교.','마지막 위치·속도, 경과 시간 및 예측 목표 좌표 로그'],
 ['release','P0','관측 해제','유효 증거 확보 → 플레이어 제거 또는 모든 추적 근거 소실 → 배회','플레이어가 없거나 배회 경로로 전환되면 TrackingEvidence는 None (0).','해제 전·후 블랙보드 스크린샷과 추적 종료 플레이테스트 기록'],
];
const labels={intent:'의도',hypothesis:'문제 가정',steps:'재현 절차',expected:'기대 결과',result:'실제 결과',blackboard:'TrackingEvidence 값 · 전환 순서',logs:'로그·스크린샷 위치 또는 링크',gameplay:'게임플레이 확인 결과',commit:'수정 커밋',risk:'남은 리스크',nextEvidence:'다음에 남길 증거'};
type Run=Item&{blackboard:string;gameplay:string;nextEvidence:string};
const defaults=(scenario:string[]):Run=>{
 const [id,priority,title,steps,expected,nextEvidence]=scenario;
 return {...blank,item_id:'tracking-'+id,title,priority,steps,expected,nextEvidence,category:'AI',kind:'기능',
   intent:title+' 전환이 추적 동작과 일치하는지 확인',hypothesis:'증거 값과 실제 추적 상태가 어긋날 수 있다.',
   status:'구현됨',version:baseline,blackboard:'',gameplay:'',
   checklist:'[ ] 재현 조건 구성\n[ ] 전환 전후 블랙보드 값 기록\n[ ] 게임플레이와 로그 대조'};
};
export default function TrackingEvidence({workspace,canWrite}:{workspace:string;canWrite:boolean}){
 const [history,setHistory]=useState<Run[]>([]);
 const [draft,setDraft]=useState<Run|null>(null);
 const [notice,setNotice]=useState('');
 const [busy,setBusy]=useState(false);
 const [loaded,setLoaded]=useState(false);
 const [view,setView]=useState('scenarios');
 const [revision,setRevision]=useState(baseline);
 const [week,setWeek]=useState(new Intl.DateTimeFormat('en-CA',{timeZone:'Asia/Seoul',year:'numeric',month:'2-digit',day:'2-digit'}).format(new Date()));
 async function load(run?:Run){
   setBusy(true);setNotice('');
   try{
     const response=await safeRequest(run?'/api/features':'/api/features?action=trackingList&workspace='+encodeURIComponent(workspace),
       run?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...run,action:'trackingSave',workspace})}:undefined);
     const body=await response.json() as {history:Run[];error?:string};
     if(!response.ok)throw new Error(body.error||'추적 검증 기록을 읽지 못했습니다.');
     setHistory(body.history);setLoaded(true);
     if(run){setDraft(null);setNotice('검증 기록을 저장했습니다.');window.dispatchEvent(new Event('daybook-saved'));}
   }catch(error){setNotice((error as Error).message)}
   finally{setBusy(false)}
 }
 useEffect(()=>{void load()},[workspace]);
 const activeRevision=revision.trim().toLowerCase();
 const rows=cases.map(scenario=>{
   const seed=defaults(scenario);
   const latest=history.find(row=>row.item_id===seed.item_id);
   const current=history.find(row=>row.item_id===seed.item_id&&row.version===activeRevision);
   return {seed,current,run:{...(current??seed),priority:latest?.priority??seed.priority,version:activeRevision,revision:latest?.revision??0}};
 }).sort((a,b)=>a.run.priority.localeCompare(b.run.priority));
 const pending=rows.filter(({current})=>current?.outcome!=='통과');
 const reviewDate=new Date(week+'T00:00:00Z');
 reviewDate.setUTCDate(reviewDate.getUTCDate()-(reviewDate.getUTCDay()+6)%7);
 const weekStart=reviewDate.toISOString().slice(0,10);
 const weekEnd=new Date(+reviewDate+7*86400000).toISOString().slice(0,10);
 const date=(value:string)=>new Intl.DateTimeFormat('en-CA',{timeZone:'Asia/Seoul',year:'numeric',month:'2-digit',day:'2-digit'}).format(new Date(value));
 const patch=(values:Partial<Run>)=>setDraft(item=>item?{...item,...values}:null);
 return <section className="ue5-tracker">
   <h3>마네킹 추적 증거</h3>
   {notice&&<p role="status">{notice}</p>}
   <details><summary>관련 커밋과 코드에서 확인한 사실</summary>
     {commits.map(([sha,title,summary])=><p key={sha}><a target="_blank" rel="noreferrer" href={'https://github.com/maru3172/ProjectDSFD/commit/'+sha}>{title} ({sha.slice(0,7)})</a><br/>{summary}</p>)}
     <p>코드의 정수 키는 TrackingEvidence입니다. EMannequinTrackingEvidence는 None (0), DirectObservation (1), StoppedMannequinClue (2), VelocityPrediction (3)입니다.</p>
     <p>2차 마네킹의 추적 증거 우선순위는 공유된 직접 관측 → 정지 단서 → 속도 예측입니다. 보조 관측 자체에는 별도 열거값이 없습니다.</p>
   </details>
   <div className="ue5-controls">
     <label>검증 기준 Git 커밋<input value={revision} maxLength={64} disabled={!!draft||busy} onChange={e=>setRevision(e.target.value)}/></label>
     <button className="secondary" disabled={busy||!!draft} onClick={()=>void load()}>새로 고침</button>
   </div>
   {loaded&&<>
     <nav className="segmented"><button className={view==='scenarios'?'selected':''} onClick={()=>setView('scenarios')}>실행 시나리오</button>
       <button className={view==='week'?'selected':''} onClick={()=>setView('week')}>주간 검토</button></nav>
     {view==='week'&&<>
       <label>검토할 주차<input type="date" value={week} onChange={e=>{if(e.target.value)setWeek(e.target.value)}}/></label>
       <p>{weekStart} 주간 · 기준 커밋에서 통과 근거 미확보 {pending.length}건</p>
       {!pending.length&&<p>선택한 커밋의 다섯 시나리오가 모두 통과했습니다.</p>}
       <ul>{history.filter(row=>row.version===activeRevision&&row.saved_at&&date(row.saved_at)>=weekStart&&date(row.saved_at)<weekEnd).map(row=>
         <li key={row.item_id+':'+row.revision}>{date(row.saved_at!)} · {row.title} · {row.outcome} · 개정 {row.revision}</li>)}</ul>
     </>}
     {(view==='week'?pending:rows).map(({seed,current,run})=><details key={seed.item_id}>
       <summary>{run.priority} · {seed.title} · {current?.outcome??'미실행'}</summary>
       <ol>{seed.steps.split(' → ').map(step=><li key={step}>{step}</li>)}</ol>
       <p>기대 · {run.expected}</p><p>다음 증거 · {run.nextEvidence||seed.nextEvidence}</p>
       <ul>{run.checklist.split('\n').map((line,index)=><li key={index}>{line.startsWith('[x]')?'완료':'미완료'} · {line.slice(4)}</li>)}</ul>
       <button className="secondary" disabled={busy||!!draft||!/^[0-9a-f]{7,64}$/.test(activeRevision)} onClick={()=>setDraft(run)}>검증 기록 열기</button>
     </details>)}
   </>}
   {draft&&<form className="team-meeting ue5-editor" onSubmit={e=>{e.preventDefault();if(canWrite&&!busy)void load(draft)}}>
     <h4>{draft.title} · {draft.version.slice(0,12)}</h4>
     <fieldset disabled={!canWrite||busy}>
       <div className="ue5-controls">
         <label>우선순위<select value={draft.priority} onChange={e=>patch({priority:e.target.value})}>{['P0','P1','P2'].map(value=><option key={value}>{value}</option>)}</select></label>
         <label>검증 상태<select value={draft.outcome} onChange={e=>patch({outcome:e.target.value})}>{['미실행','통과','실패','차단'].map(value=><option key={value}>{value}</option>)}</select></label>
       </div>
       <div className="ue5-grid">{Object.entries(labels).map(([key,label])=><label key={key}>{label}<textarea rows={3} maxLength={5000} value={draft[key as keyof typeof labels]} onChange={e=>patch({[key]:e.target.value})}/></label>)}</div>
       {draft.checklist.split('\n').map((line,index)=><label key={index} className="checkline">
         <input type="checkbox" checked={line.startsWith('[x]')} onChange={e=>patch({checklist:draft.checklist.split('\n').map((value,i)=>i===index?(e.target.checked?'[x] ':'[ ] ')+value.slice(4):value).join('\n')})}/>{line.slice(4)}
       </label>)}
     </fieldset>
     <div className="inline">{canWrite&&<button type="submit" className="primary" disabled={busy}>저장</button>}
       <button type="button" className="secondary" disabled={busy} onClick={()=>setDraft(null)}>닫기</button></div>
   </form>}
 </section>;
}

"use client";
import {useCallback,useEffect,useState} from 'react';
import {safeRequest} from './safe-request';
import {randomId} from './browser-crypto';
import {Lock,Unlock,RefreshCw,TriangleAlert} from 'lucide-react';
import type {Entry} from './types';
type Row=Record<string,any>;
// Game-team panels of the 팀 도구 tab (self-host API, features.mjs → project-tools.mjs).
async function call(url:string,value?:Row){const r=await safeRequest(url,{method:value?'POST':'GET',headers:value?{'Content-Type':'application/json'}:undefined,body:value?JSON.stringify(value):undefined});const result=await r.json() as Row;if(!r.ok)throw new Error(result.error??'요청 실패');if(value)window.dispatchEvent(new Event('daybook-saved'));return result}
const get=(workspace:string,action:string)=>call(`/api/features?action=${action}&workspace=${encodeURIComponent(workspace)}`);
const kst=(iso:string)=>new Date(Date.parse(iso)+9*3600000).toISOString().slice(0,16).replace('T',' ');
function useJob(){
 const [error,setError]=useState(''),[status,setStatus]=useState(''),[busy,setBusy]=useState(false);
 const run=async(job:()=>Promise<unknown>,message='')=>{if(busy)return;setBusy(true);setError('');setStatus('');try{await job();if(message)setStatus(message)}catch(e){setError((e as Error).message)}finally{setBusy(false)}};
 const notice=<>{error&&<div className="operations-error" role="alert">{error}</div>}{status&&<div className="operations-status" role="status">{status}</div>}</>;
 return {run,busy,notice,setError};
}

export function AssetLocks({workspace,entry,onOpen}:{workspace:string;entry:Entry|null;onOpen:(id:string)=>void}){
 const {run,busy,notice,setError}=useJob();
 const [locks,setLocks]=useState<Row[]>([]),[path,setPath]=useState(''),[hours,setHours]=useState('24'),[reason,setReason]=useState(''),[link,setLink]=useState(false),[changes,setChanges]=useState(''),[check,setCheck]=useState<Row|null>(null);
 const load=useCallback(()=>get(workspace,'locks').then(r=>setLocks(r.locks),e=>setError((e as Error).message)),[workspace]);
 useEffect(()=>{void load()},[load]);
 const lock=(target=path)=>run(async()=>{await call('/api/features',{action:'lock',workspace,path:target,hours:Number(hours),reason,entry:link&&entry?entry.id:''});if(target===path)setPath('');await load();if(check)setCheck({...check,unlocked:check.unlocked.filter((p:string)=>p!==target)})},'잠갔습니다. 팀원이 같은 파일을 잠그려 하면 막히고, 기한이 지나면 저절로 풀립니다.');
 const unlock=(l:Row)=>{if(!l.mine&&!confirm(`${l.holder}님의 잠금을 풀까요? 그 사람에게 알림이 갑니다.`))return;void run(async()=>{await call('/api/features',{action:'unlock',workspace,id:l.id});await load()},'잠금을 풀었습니다.')};
 const verify=(fromGit:boolean)=>run(async()=>setCheck(await call('/api/features',{action:'lockCheck',workspace,...(fromGit?{fromGit:true}:{paths:changes})})));
 return <>{notice}
  <h3>작업 잠금</h3>
  <p className="muted">맵(.umap)·에셋(.uasset)은 Git이 합칠 수 없어서, 두 사람이 같은 파일을 고치면 한쪽 작업이 사라집니다. 고치기 전에 여기서 잠가 두세요. 잠금은 알림판입니다. 파일을 실제로 막지는 않으므로, 커밋 전에 아래 "내 변경 확인"을 눌러 보세요.</p>
  <div className="team-meeting">
   <label>저장소 기준 경로 (폴더 전체는 끝에 /)<input value={path} maxLength={400} placeholder="Content/Maps/Lobby.umap" onChange={e=>setPath(e.target.value)}/></label>
   <div className="inline"><label>기간<select value={hours} onChange={e=>setHours(e.target.value)}>{[['4','4시간'],['8','8시간'],['24','하루'],['48','이틀'],['72','사흘'],['168','일주일']].map(([v,l])=><option key={v} value={v}>{l}</option>)}</select></label>
    <label style={{flex:1}}>무엇을 하는지<input value={reason} maxLength={300} placeholder="로비 조명 작업" onChange={e=>setReason(e.target.value)}/></label></div>
   {entry&&<label className="checkline"><input type="checkbox" checked={link} onChange={e=>setLink(e.target.checked)}/>선택한 기록 "{entry.title}"과 연결</label>}
   <div className="inline"><button className="primary" disabled={busy||!path.trim()} onClick={()=>void lock()}><Lock size={15}/>잠그기</button></div>
  </div>
  <div className="inline"><h3 style={{margin:0}}>지금 잠긴 파일 {locks.length}개</h3><button className="secondary" onClick={()=>void load()}><RefreshCw size={15}/>새로 고침</button></div>
  {locks.length===0?<p className="muted">잠긴 파일이 없습니다.</p>:<ul className="team-commits">{locks.map(l=><li key={l.id}><Lock size={14}/><code>{l.path}</code> <strong>{l.holder}</strong>{l.reason&&<> · {l.reason}</>} <small>{kst(l.expires_at)}까지</small>
   {l.entry_id&&<button className="text-button" onClick={()=>onOpen(l.entry_id)}>{l.entry_title??'기록'}</button>}
   {l.mine&&<button className="text-button" onClick={()=>{setPath(l.path);setReason(l.reason)}}>연장</button>}
   <button className="text-button" disabled={busy} onClick={()=>unlock(l)}><Unlock size={13}/>{l.mine?'풀기':'관리자 풀기'}</button></li>)}</ul>}
  <h3>내 변경 확인</h3>
  <p className="muted">내 PC의 저장소 폴더에서 <code>git status --short -uall</code>을 실행한 결과를 붙여 넣으면, 남이 잠근 파일을 고쳤는지와 아직 안 잠근 에셋을 알려 줍니다. 서버가 실행되는 PC에서는 연결한 Git 폴더를 바로 확인할 수 있습니다.</p>
  <textarea rows={5} value={changes} placeholder={' M Content/Maps/Lobby.umap\n?? Content/Characters/New.uasset'} onChange={e=>setChanges(e.target.value)}/>
  <div className="inline"><button className="secondary" disabled={busy||!changes.trim()} onClick={()=>void verify(false)}>붙여 넣은 목록 확인</button><button className="secondary" disabled={busy} onClick={()=>void verify(true)}>이 PC의 Git 폴더로 확인</button></div>
  {check&&<div className="team-meeting">
   <p>확인한 파일 {check.checked}개 · 내가 잠근 파일 {check.mine.length}개</p>
   {check.conflicts.length>0?<><p className="team-warning"><TriangleAlert size={15}/>남이 잠근 파일을 고쳤습니다. 커밋 전에 그 사람과 먼저 이야기하세요.</p>
    <ul className="team-commits">{check.conflicts.map((c:Row)=><li key={c.path}><code>{c.path}</code> <strong>{c.lock.holder}</strong>{c.lock.reason&&<> · {c.lock.reason}</>} <small>{kst(c.lock.expires_at)}까지</small></li>)}</ul></>:<p className="muted">남이 잠근 파일과 겹치는 변경은 없습니다.</p>}
   {check.unlocked.length>0&&<><p>아직 잠그지 않은 에셋</p><ul className="team-commits">{check.unlocked.map((p:string)=><li key={p}><code>{p}</code><button className="text-button" disabled={busy} onClick={()=>void lock(p)}><Lock size={13}/>잠그기</button></li>)}</ul></>}
  </div>}
 </>;
}

const SeverityOptions=[['critical','치명 (진행 불가·크래시)'],['high','높음'],['normal','보통'],['low','낮음']],FrequencyOptions=[['always','항상'],['often','자주'],['sometimes','가끔'],['once','한 번']];
const emptyForm={title:'',severity:'normal',frequency:'sometimes',build:'',place:'',steps:'',expected:'',actual:'',tester:'',good:'',bad:'',idea:''};
export function BugReports({workspace,onOpen}:{workspace:string;onOpen:(id:string)=>void}){
 const {run,busy,notice,setError}=useJob();
 const [type,setType]=useState<'bug'|'playtest'>('bug'),[form,setForm]=useState(emptyForm),[files,setFiles]=useState<File[]>([]),[mutation,setMutation]=useState(randomId()),[list,setList]=useState<Row|null>(null);
 const load=useCallback(()=>get(workspace,'bugs').then(setList,e=>setError((e as Error).message)),[workspace]);
 useEffect(()=>{void load()},[load]);
 const set=(key:keyof typeof emptyForm)=>(e:{target:{value:string}})=>setForm({...form,[key]:e.target.value});
 const submit=()=>run(async()=>{
  const result=await call('/api/features',{action:'bugReport',workspace,type,...form,mutation});
  const failed:string[]=[];
  for(const file of files){const body=new FormData();body.append('entry',result.id);body.append('file',file);const r=await safeRequest('/api/file',{method:'POST',body});if(!r.ok)failed.push(file.name)}
  setForm(emptyForm);setFiles([]);setMutation(randomId());await load();onOpen(result.id);
  if(failed.length)throw new Error(`기록은 저장했지만 첨부 ${failed.length}개를 올리지 못했습니다: ${failed.join(', ')}. 기록을 열어 다시 첨부하세요.`);
 },type==='bug'?'버그를 기록했습니다.':'플레이테스트 의견을 기록했습니다.');
 const ready=form.title.trim()&&(type==='bug'?form.steps.trim()&&form.actual.trim():form.good.trim()||form.bad.trim()||form.idea.trim());
 return <>{notice}
  <h3>버그·플레이테스트 기록</h3>
  <nav className="segmented"><button className={type==='bug'?'selected':''} onClick={()=>setType('bug')}>버그 신고</button><button className={type==='playtest'?'selected':''} onClick={()=>setType('playtest')}>플레이테스트 의견</button></nav>
  <div className="team-meeting">
   <label>제목<input value={form.title} maxLength={200} placeholder={type==='bug'?'로비에서 문을 지나면 캐릭터가 벽에 낌':'첫 판 소감'} onChange={set('title')}/></label>
   <div className="inline"><label>빌드·버전<input value={form.build} maxLength={80} placeholder="0.3.1 또는 커밋" onChange={set('build')}/></label><label style={{flex:1}}>맵·위치<input value={form.place} maxLength={200} placeholder="Lobby, 2층 계단" onChange={set('place')}/></label></div>
   {type==='bug'?<>
    <div className="inline"><label>심각도<select value={form.severity} onChange={set('severity')}>{SeverityOptions.map(([v,l])=><option key={v} value={v}>{l}</option>)}</select></label><label>얼마나 자주<select value={form.frequency} onChange={set('frequency')}>{FrequencyOptions.map(([v,l])=><option key={v} value={v}>{l}</option>)}</select></label></div>
    <label>재현 순서 (필수)<textarea rows={4} value={form.steps} placeholder={'1. 로비에 들어간다\n2. 오른쪽 문으로 달려간다'} onChange={set('steps')}/></label>
    <label>기대 결과<textarea rows={2} value={form.expected} onChange={set('expected')}/></label>
    <label>실제 결과 (필수)<textarea rows={2} value={form.actual} onChange={set('actual')}/></label>
   </>:<>
    <label>플레이어<input value={form.tester} maxLength={80} onChange={set('tester')}/></label>
    <label>좋았던 점<textarea rows={3} value={form.good} onChange={set('good')}/></label>
    <label>불편한 점<textarea rows={3} value={form.bad} onChange={set('bad')}/></label>
    <label>제안<textarea rows={3} value={form.idea} onChange={set('idea')}/></label>
   </>}
   <label>스크린샷·로그 (선택, 파일당 10MB · 로그나 영상은 .zip으로 묶어서)<input type="file" multiple accept=".png,.jpg,.jpeg,.gif,.webp,.txt,.zip" onChange={e=>setFiles([...e.currentTarget.files??[]])}/></label>
   <div className="inline"><button className="primary" disabled={busy||!ready} onClick={()=>void submit()}>기록하기</button></div>
  </div>
  <div className="inline"><h3 style={{margin:0}}>열린 버그 {list?.bugs.length??0}개</h3><button className="secondary" onClick={()=>void load()}><RefreshCw size={15}/>새로 고침</button></div>
  {list&&(list.bugs.length===0?<p className="muted">열린 버그가 없습니다.</p>:<ul className="team-commits">{list.bugs.map((b:Row)=><li key={b.id}><span className={'status-tag '+b.status}>{({todo:'할 일',inprogress:'진행 중',blocked:'막힘',done:'완료'} as Row)[b.status]}</span><button className="text-button" onClick={()=>onOpen(b.id)}>{b.title}</button><small>{b.summary}</small></li>)}</ul>)}
  {list&&list.playtests.length>0&&<><h3>최근 플레이테스트 의견</h3><ul className="team-commits">{list.playtests.map((p:Row)=><li key={p.id}><button className="text-button" onClick={()=>onOpen(p.id)}>{p.title}</button><small>{p.day}{p.summary?' · '+p.summary:''}</small></li>)}</ul></>}
 </>;
}

export function Milestones({workspace,onOpen}:{workspace:string;onOpen:(id:string)=>void}){
 const {notice,setError}=useJob();
 const [data,setData]=useState<Row|null>(null);
 const load=useCallback(()=>get(workspace,'milestones').then(setData,e=>setError((e as Error).message)),[workspace]);
 useEffect(()=>{void load()},[load]);
 const left=(d:number|null)=>d===null?'기한 없음':d>0?`D-${d}`:d===0?'D-day':`${-d}일 지남`;
 return <>{notice}
  <div className="inline"><h3 style={{margin:0}}>마일스톤</h3><button className="secondary" onClick={()=>void load()}><RefreshCw size={15}/>새로 고침</button></div>
  <p className="muted">"목표" 기록이 마일스톤입니다. 업무·계획의 상위 기록을 그 목표로 정하면(여러 단계 아래까지) 진행률에 들어갑니다. 완료된 목표는 30일 동안 보입니다.</p>
  {data&&(data.milestones.length===0?<p className="muted">아직 목표 기록이 없습니다. 예: "알파 빌드", "데모 제출"을 목표로 만들고 기한을 정하세요.</p>:
   <div className="team-milestones">{data.milestones.map((m:Row)=><div key={m.id} className="team-meeting">
    <div className="inline"><button className="text-button" onClick={()=>onOpen(m.id)}><strong>{m.title}</strong></button><span className={'due'+(m.daysLeft!==null&&m.daysLeft<0&&m.status!=='done'?' late':'')}>{m.due_day?`${m.due_day} · `:''}{m.status==='done'?'완료':left(m.daysLeft)}</span></div>
    {m.total===0?<p className="muted">아직 아래에 연결된 업무가 없습니다.</p>:<>
     <div className="team-progress" role="progressbar" aria-valuenow={m.percent} aria-valuemin={0} aria-valuemax={100}><i style={{width:m.percent+'%'}}/></div>
     <p className="muted">{m.percent}% · 완료 {m.done}/{m.total} · 진행 {m.doing} · 막힘 {m.blocked}{m.late?` · 기한 지남 ${m.late}`:''}</p></>}
   </div>)}</div>)}
 </>;
}

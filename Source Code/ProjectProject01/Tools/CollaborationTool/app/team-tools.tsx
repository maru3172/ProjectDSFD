"use client";
import {useEffect,useState} from 'react';
import {safeRequest} from './safe-request';
import {randomId} from './browser-crypto';
import {Copy,Download,FileUp,GitCommit,Save} from 'lucide-react';
import type {Data} from './types';
type Row=Record<string,any>;
// Team tools (self-host API, features.mjs → team.mjs): MeetingRecorder import, weekly report, commit links.
async function call(url:string,value?:Row){const r=await safeRequest(url,{method:value?'POST':'GET',headers:value?{'Content-Type':'application/json'}:undefined,body:value?JSON.stringify(value):undefined});const result=await r.json() as Row;if(!r.ok)throw new Error(result.error??'요청 실패');if(value)window.dispatchEvent(new Event('daybook-saved'));return result}
/** Same code as recordCode() in self-host/team.mjs: put it in a commit message to link the commit to the record. */
export const recordCode=(id:string)=>'DB-'+id.replace(/-/g,'').slice(0,8).toLowerCase();
const kstToday=()=>new Date(Date.now()+9*3600000).toISOString().slice(0,10);
const monday=(day:string)=>{const d=new Date(day+'T00:00:00Z');d.setUTCDate(d.getUTCDate()-(d.getUTCDay()+6)%7);return d.toISOString().slice(0,10)};
const copy=async(value:string)=>{await navigator.clipboard.writeText(value)};
const download=(name:string,value:string)=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([value],{type:'text/markdown;charset=utf-8'}));a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)};

/** To-do lines from a MeetingRecorder summary: the bullets under a "할 일" heading, without the "[00:12:34] " time. */
export function meetingTasks(summary:string){
 const tasks:string[]=[];let inside=false;
 for(const raw of summary.split(/\r?\n/)){const line=raw.trim();
  if(/^#{2,4}\s/.test(line)){inside=/할\s?일/.test(line);continue}
  if(!inside||!line.startsWith('- '))continue;
  const item=line.slice(2).replace(/^\[\d{2}:\d{2}:\d{2}\]\s*/,'').replace(/\s*_\(인식 불확실\)_$/,'').trim();
  if(item&&!/^\(.*\)$/.test(item)&&!tasks.includes(item))tasks.push(item.slice(0,200));
 }
 return tasks.slice(0,50);
}

type Meeting={title:string;day:string;summary:string;transcript:string;notes:string;tasks:string};
async function readMeeting(files:FileList):Promise<Meeting>{
 const byName=new Map([...files].map(f=>[f.name.toLowerCase(),f]));const read=async(name:string)=>byName.has(name)?await byName.get(name)!.text():'';
 const summary=await read('summary.md'),transcript=await read('transcript.md'),notes=await read('notes.md');let title='',day=kstToday();
 try{const meta=JSON.parse(await read('meeting.json')||'{}');title=String(meta.Title??'');if(/^\d{4}-\d{2}-\d{2}/.test(meta.StartedLocal??''))day=meta.StartedLocal.slice(0,10)}catch{}
 if(!summary&&!transcript&&!notes)throw new Error('summary.md, transcript.md, notes.md 중 하나가 있는 회의 폴더를 고르세요.');
 return {title:title||'회의',day,summary,transcript,notes,tasks:meetingTasks(summary).join('\n')};
}

export default function TeamTools({workspace,data,selected,onOpen}:{workspace:string;data:Data|null;selected:string|null;onOpen:(id:string)=>void}){
 const [meeting,setMeeting]=useState<Meeting|null>(null),[weekStart,setWeekStart]=useState(monday(kstToday())),[report,setReport]=useState<Row|null>(null),[commits,setCommits]=useState<Row|null>(null),[error,setError]=useState(''),[status,setStatus]=useState(''),[busy,setBusy]=useState(false);
 const entry=data?.entries.find(e=>e.id===selected)??null;
 const run=async(job:()=>Promise<unknown>,message='')=>{if(busy)return;setBusy(true);setError('');setStatus('');try{await job();if(message)setStatus(message)}catch(e){setError((e as Error).message)}finally{setBusy(false)}};
 useEffect(()=>{setCommits(null);if(entry)void call(`/api/features?action=commits&workspace=${encodeURIComponent(workspace)}&entry=${encodeURIComponent(entry.id)}`).then(setCommits,e=>setError((e as Error).message))},[workspace,entry?.id]);

 const importMeeting=()=>run(async()=>{if(!meeting)return;
  const tasks=meeting.tasks.split('\n').map(t=>t.trim()).filter(Boolean);
  const result=await call('/api/features',{action:'meetingImport',workspace,title:meeting.title,day:meeting.day,summary:meeting.summary,transcript:meeting.transcript,notes:meeting.notes,tasks,mutation:randomId()});
  setMeeting(null);onOpen(result.meeting);setStatus(result.repeated?'이미 가져온 회의라 그 기록을 열었습니다.':`회의 기록과 할 일 ${result.tasks}개를 만들었습니다.`)});
 const makeReport=(save=false)=>run(async()=>{
  const result=save?await call('/api/features',{action:'weeklyReport',workspace,start:weekStart,save:true,mutation:'weekly:'+workspace+':'+weekStart}):await call(`/api/features?action=weeklyReport&workspace=${encodeURIComponent(workspace)}&start=${weekStart}`);
  setReport(result);if(save&&result.saved)onOpen(result.saved)},save?'주간 보고를 작업 로그로 저장했습니다.':'');

 return <>{error&&<div className="operations-error" role="alert">{error}</div>}{status&&<div className="operations-status" role="status">{status}</div>}
  <h3>회의 기록 가져오기</h3>
  <p className="muted">회의 기록 도구(MeetingRecorder)가 만든 회의 폴더를 고르면 요약·메모·대화문이 회의 기록 하나로 들어오고, 요약의 할 일은 그 아래 업무로 만들어집니다. 할 일은 가져오기 전에 고칠 수 있습니다.</p>
  <label className="secondary file-pick"><FileUp size={15}/>회의 폴더 고르기<input type="file" hidden multiple {...{webkitdirectory:''}} onChange={e=>{const files=e.currentTarget.files;e.currentTarget.value='';if(files?.length)void run(async()=>setMeeting(await readMeeting(files)))}}/></label>
  {meeting&&<div className="team-meeting">
   <label>제목<input value={meeting.title} maxLength={200} onChange={e=>setMeeting({...meeting,title:e.target.value})}/></label>
   <label>날짜<input type="date" value={meeting.day} onChange={e=>setMeeting({...meeting,day:e.target.value})}/></label>
   <label>할 일 (한 줄에 하나, 비우면 할 일 없이 가져옴)<textarea rows={6} value={meeting.tasks} onChange={e=>setMeeting({...meeting,tasks:e.target.value})}/></label>
   <p className="muted">가져올 내용: {[meeting.summary&&'요약',meeting.notes&&'메모',meeting.transcript&&'대화문'].filter(Boolean).join(', ')}</p>
   <div className="inline"><button className="primary" disabled={busy||!meeting.title.trim()} onClick={()=>void importMeeting()}>가져오기</button><button className="secondary" onClick={()=>setMeeting(null)}>취소</button></div>
  </div>}

  <h3>주간 보고</h3>
  <p className="muted">한 주(월~일, 한국 시간) 동안 끝낸 일, 진행 중, 막힌 일, 기한이 지난 일, 작업 로그, 회의·결정을 사람별로 모읍니다.</p>
  <div className="inline"><input type="date" aria-label="주 시작일" value={weekStart} onChange={e=>setWeekStart(monday(e.target.value||kstToday()))}/><button className="secondary" disabled={busy} onClick={()=>void makeReport()}>보고서 만들기</button></div>
  {report&&<><p className="muted">끝낸 일 {report.counts.done} · 진행 {report.counts.doing} · 막힘 {report.counts.blocked} · 기한 {report.counts.late} · 회의 {report.counts.meetings} · 로그 {report.counts.logs}</p>
   <div className="inline"><button className="secondary" onClick={()=>void run(()=>copy(report.markdown),'복사했습니다.')}><Copy size={15}/>복사</button><button className="secondary" onClick={()=>download(`주간보고_${weekStart}.md`,report.markdown)}><Download size={15}/>.md 내려받기</button><button className="secondary" disabled={busy} onClick={()=>void makeReport(true)}><Save size={15}/>작업 로그로 저장</button></div>
   <pre className="team-report">{report.markdown}</pre></>}

  <h3>커밋 연결</h3>
  {!entry?<p className="muted">기록을 하나 고르면, 커밋 메시지에 넣을 코드와 그 코드로 연결된 커밋이 여기 보입니다.</p>:<>
   <p>선택한 기록: <strong>{entry.title}</strong></p>
   <div className="inline"><code>{recordCode(entry.id)}</code><button className="secondary" onClick={()=>void run(()=>copy(recordCode(entry.id)),'코드를 복사했습니다. 커밋 메시지에 넣으세요.')}><Copy size={15}/>코드 복사</button></div>
   <p className="muted">예: <code>git commit -m "로그인 오류 수정 {recordCode(entry.id)}"</code></p>
   {!commits?<p className="muted">커밋을 찾는 중…</p>:!commits.available?<p className="muted">{commits.reason}</p>:commits.commits.length===0?<p className="muted">이 코드가 들어간 커밋이 아직 없습니다(최근 1년).</p>:
    <ul className="team-commits">{commits.commits.map((c:Row)=><li key={c.hash}><GitCommit size={14}/><code>{c.hash}</code> {c.subject} <small>{c.author} · {c.date}</small></li>)}</ul>}
  </>}
 </>;
}

// Team tools on top of the standalone server (features.mjs dispatches here):
//  - meetingImport: a meeting from the MeetingRecorder tool (transcript, summary, notes) becomes a meeting record,
//    and the summary's to-do lines become task records under it.
//  - weeklyReport: what each person finished, has in progress, is blocked on or let slip in a week, plus the
//    week's meetings and decisions, as Markdown (optionally saved as a work-log record).
//  - commits: Git commits whose message names a record ("DB-1a2b3c4d"), read-only from the linked Git folder.
import {execFile} from 'node:child_process';
import {existsSync} from 'node:fs';
import {Problem,q,rows,run,text,now,audit} from './runtime.mjs';

const id=()=>crypto.randomUUID();
const validDay=value=>{const d=typeof value==='string'&&/^\d{4}-\d{2}-\d{2}$/.test(value)?new Date(value+'T00:00:00Z'):null;if(!d||Number.isNaN(d.getTime())||d.toISOString().slice(0,10)!==value)throw new Problem(400,'날짜를 확인하세요.');return value};
const addDays=(value,n)=>{const d=new Date(value+'T00:00:00Z');d.setUTCDate(d.getUTCDate()+n);return d.toISOString().slice(0,10)};
/** The short code teammates put in commit messages to link a commit to a record. */
export const recordCode=entryId=>'DB-'+String(entryId).replace(/-/g,'').slice(0,8).toLowerCase();

function insertEntry(w,me,{kind,title,body,day,summary=null,parent=null,assigned=null,mutation}){
 const e=id(),stamp=now();
 run("INSERT INTO entries(id,workspace_id,kind,title,body,author_id,assigned_id,day,status,priority,parent_id,summary,created_at,updated_at,mutation_id,auto_summary) VALUES(?,?,?,?,?,?,?,?,'todo','normal',?,?,?,?,?,'{}')",
  e,w,kind,title,body,me.userId,assigned,day,parent,summary,stamp,stamp,mutation);
 audit(w,'create',e,{kind,title});
 return e;
}

// Sync work inside the caller's transaction.
export function teamAction(action,value,w,me){
 if(action==='meetingImport'){
  const title=text(value.title,200,true),day=validDay(value.day),mutation=text(value.mutation,100,true);
  // A retried request (response lost) returns the meeting it already made instead of a second copy.
  const existing=q('SELECT id FROM entries WHERE workspace_id=? AND mutation_id=?',w,mutation);
  if(existing)return {meeting:existing.id,tasks:rows('SELECT id FROM entries WHERE workspace_id=? AND parent_id=? AND deleted_at IS NULL',w,existing.id).length,repeated:true};
  const summary=text(value.summary??'',50000),transcript=text(value.transcript??'',180000),notes=text(value.notes??'',30000);
  const tasks=Array.isArray(value.tasks)?value.tasks:[];
  if(tasks.length>50||tasks.some(t=>typeof t!=='string'||t.trim().length===0||t.length>200))throw new Problem(400,'할 일은 50개, 한 줄 200자까지 가져올 수 있습니다.');
  if(!summary&&!transcript&&!notes)throw new Problem(400,'가져올 회의 내용이 없습니다.');
  const body=[summary&&'## 요약\n\n'+summary,notes&&'## 메모\n\n'+notes,transcript&&'## 대화문\n\n'+transcript].filter(Boolean).join('\n\n');
  const meeting=insertEntry(w,me,{kind:'meeting',title,body,day,summary:summary.slice(0,2000)||null,mutation});
  tasks.forEach((t,i)=>insertEntry(w,me,{kind:'task',title:t.trim().slice(0,200),body:'회의 "'+title+'"에서 나온 할 일',day,parent:meeting,mutation:mutation+':task:'+i}));
  return {meeting,tasks:tasks.length};
 }
 if(action==='weeklyReport'){
  const start=validDay(value.start),end=addDays(start,6),from=new Date(start+'T00:00:00+09:00').toISOString(),to=new Date(addDays(start,7)+'T00:00:00+09:00').toISOString(); // Weeks run on Korean time, like the app's dates.
  const people=new Map(rows("SELECT m.user_id,coalesce(p.name,'(이름 없음)') name FROM members m LEFT JOIN author_profiles p ON p.user_id=m.user_id WHERE m.workspace_id=?",w).map(r=>[r.user_id,r.name]));
  const name=u=>u?(people.get(u)??'(이전 팀원)'):'담당 미정';
  const work="kind IN ('task','goal','plan') AND deleted_at IS NULL";
  const done=rows(`SELECT * FROM entries WHERE workspace_id=? AND ${work} AND status='done' AND updated_at>=? AND updated_at<? ORDER BY updated_at`,w,from,to);
  const doing=rows(`SELECT * FROM entries WHERE workspace_id=? AND ${work} AND status='inprogress' ORDER BY due_day IS NULL,due_day`,w);
  const blocked=rows(`SELECT * FROM entries WHERE workspace_id=? AND ${work} AND status='blocked' ORDER BY due_day IS NULL,due_day`,w);
  const late=rows(`SELECT * FROM entries WHERE workspace_id=? AND ${work} AND status<>'done' AND due_day IS NOT NULL AND due_day<=? ORDER BY due_day`,w,end);
  const meetings=rows("SELECT * FROM entries WHERE workspace_id=? AND kind IN ('meeting','decision') AND deleted_at IS NULL AND day>=? AND day<=? ORDER BY day",w,start,end);
  const logs=rows("SELECT * FROM entries WHERE workspace_id=? AND kind='log' AND deleted_at IS NULL AND day>=? AND day<=? ORDER BY day",w,start,end);
  const byPerson=(list,key='assigned_id')=>{const g=new Map();for(const e of list){const k=name(e[key]);if(!g.has(k))g.set(k,[]);g.get(k).push(e)}return [...g.entries()].sort((a,b)=>a[0].localeCompare(b[0],'ko'))};
  const line=e=>`- ${e.title}${e.due_day?` (기한 ${e.due_day})`:''} \`${recordCode(e.id)}\``;
  const section=(title,list,key)=>list.length?`## ${title}\n\n`+byPerson(list,key).map(([who,items])=>`### ${who}\n\n${items.map(line).join('\n')}`).join('\n\n'):`## ${title}\n\n- 없음`;
  const workspace=q('SELECT name FROM workspaces WHERE id=?',w)?.name??'';
  const markdown=[`# 주간 보고 ${start} ~ ${end}`,workspace&&`공간: ${workspace}`,
   section('이번 주에 끝낸 일',done),section('진행 중',doing),section('막힌 일',blocked),section('기한이 지났거나 이번 주까지인 일',late),
   section('작업 로그',logs,'author_id'),
   meetings.length?'## 회의 · 결정\n\n'+meetings.map(e=>`- ${e.day} ${e.kind==='decision'?'[결정] ':''}${e.title} \`${recordCode(e.id)}\``).join('\n'):'## 회의 · 결정\n\n- 없음'].filter(Boolean).join('\n\n')+'\n';
  const counts={done:done.length,doing:doing.length,blocked:blocked.length,late:late.length,meetings:meetings.length,logs:logs.length};
  if(value.save){
   const mutation=text(value.mutation,100,true),existing=q('SELECT id FROM entries WHERE workspace_id=? AND mutation_id=?',w,mutation);
   const saved=existing?.id??insertEntry(w,me,{kind:'log',title:`주간 보고 ${start} ~ ${end}`,body:markdown,day:end,mutation});
   return {markdown,counts,saved};
  }
  return {markdown,counts};
 }
 throw new Problem(400,'알 수 없는 팀 도구 요청입니다.');
}

// Read-only "git log" in the linked Git folder (outside any transaction: it waits on a process).
export async function commitsFor(w,entryId,root){
 if(!root||!existsSync(root))return {available:false,reason:'이 PC·Git에서 Git 폴더를 먼저 연결하세요.',commits:[]};
 const entry=q('SELECT id FROM entries WHERE workspace_id=? AND id=? AND deleted_at IS NULL',w,entryId);
 if(!entry)throw new Problem(404,'기록이 없습니다.');
 const code=recordCode(entry.id);
 const out=await new Promise(resolve=>execFile('git',['-C',root,'log','-n','2000','--since=365.days','--date=short','--pretty=format:%h%x1f%an%x1f%ad%x1f%s','--regexp-ignore-case','--grep',code],
  {windowsHide:true,timeout:15000,maxBuffer:4*1024*1024},(error,stdout)=>resolve(error?null:stdout)));
 if(out===null)return {available:false,code,reason:'git 명령을 실행하지 못했습니다. Git이 설치되어 있는지 확인하세요.',commits:[]};
 const commits=out.split('\n').filter(Boolean).slice(0,200).map(l=>{const [hash,author,date,subject]=l.split('\x1f');return {hash,author,date,subject}});
 return {available:true,code,commits};
}

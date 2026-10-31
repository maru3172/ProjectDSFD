// Game-team tools on top of the standalone server (features.mjs dispatches here):
//  - asset locks: "I am working on Content/Maps/Lobby.umap until ..." for files Git cannot merge (.uasset, .umap).
//    A lock is a shared notice, not a file-system lock: it warns, it cannot stop someone who does not look.
//    lockCheck compares changed files (pasted "git status" output, or the linked Git folder on the server PC) with them.
//  - bugReport / bugs: a fixed form for bugs and playtest feedback, saved as normal records under a "버그" or
//    "플레이테스트" record type so boards, search, comments and attachments all work on them.
//  - milestones: each goal with how much of the work under it (any depth) is done, in progress, blocked or late.
import {execFile} from 'node:child_process';
import {existsSync} from 'node:fs';
import {Problem,q,rows,run,text,now,audit,notify} from './runtime.mjs';

const id=()=>crypto.randomUUID();
const kstToday=()=>new Date(Date.now()+9*3600000).toISOString().slice(0,10);
const kst=iso=>new Date(Date.parse(iso)+9*3600000).toISOString().slice(0,16).replace('T',' ');

// Paths are repository-relative with forward slashes ("Content/Maps/Lobby.umap"); a trailing "/" locks a folder.
export function lockPath(value){
 const p=text(value,400,true).trim().replace(/^"|"$/g,'').replace(/\\/g,'/').replace(/^(\.\/)+/,'').replace(/\/{2,}/g,'/');
 if(!p||p.startsWith('/')||/^[a-z]:/i.test(p)||p.split('/').some(s=>s==='..'))throw new Problem(400,'저장소 기준 경로로 적으세요. 예: Content/Maps/Lobby.umap');
 return p;
}
const covers=(lock,file)=>{const a=lock.toLowerCase(),b=file.toLowerCase();return a.endsWith('/')?b.startsWith(a):a===b};
const overlaps=(a,b)=>covers(a,b)||covers(b,a);
const isAsset=p=>/\.(uasset|umap)$/i.test(p);

function openLocks(w){
 return rows(`SELECT l.*,coalesce(m.name,p.name,'(이전 팀원)') holder,e.title entry_title FROM asset_locks l
  LEFT JOIN members m ON m.workspace_id=l.workspace_id AND m.user_id=l.user_id LEFT JOIN author_profiles p ON p.user_id=l.user_id
  LEFT JOIN entries e ON e.id=l.entry_id AND e.deleted_at IS NULL
  WHERE l.workspace_id=? AND l.released_at IS NULL AND l.expires_at>? ORDER BY l.path`,w,now());
}

/** Lines of "git status --short/--porcelain" output, or plain paths, to repository paths (renames count as the new path). */
export function changedPaths(output){
 const paths=new Set();
 for(const raw of String(output).split(/\r?\n/)){
  let line=raw.replace(/\s+$/,'');if(!line.trim())continue;
  // Status columns; the first one is a space for unstaged changes and can be lost when the text is copied or trimmed.
  const status=/^[ MADRCU?!]{2} /.test(line)?3:/^[MADRCU?!] /.test(line)?2:0;line=line.slice(status);
  if(line.includes(' -> '))line=line.split(' -> ').pop();
  try{paths.add(lockPath(line))}catch{}
 }
 return [...paths].slice(0,5000);
}

function checkPaths(w,me,paths){
 const locks=openLocks(w),conflicts=[],mine=[],unlocked=[];
 for(const p of paths){
  const holder=locks.find(l=>covers(l.path,p));
  if(holder&&holder.user_id!==me.userId)conflicts.push({path:p,lock:holder});
  else if(holder)mine.push(p);
  else if(isAsset(p))unlocked.push(p);
 }
 return {checked:paths.length,conflicts,mine,unlocked};
}

// git status on the server PC's linked Git folder: outside any transaction, it waits on a process.
export async function lockCheck(w,me,value,root,loopback){
 let paths;
 if(value.fromGit){
  if(!loopback)throw new Problem(403,'Git 폴더 자동 확인은 서버가 실행되는 PC에서만 됩니다. 다른 PC에서는 git status 결과를 붙여 넣으세요.');
  if(!root||!existsSync(root))throw new Problem(400,'이 PC·Git에서 Git 폴더를 먼저 연결하세요.');
  const out=await new Promise(resolve=>execFile('git',['-c','core.quotepath=false','-C',root,'status','--porcelain=v1','--untracked-files=all'],{windowsHide:true,timeout:15000,maxBuffer:8*1024*1024},(error,stdout)=>resolve(error?null:stdout)));
  if(out===null)throw new Problem(400,'git status를 실행하지 못했습니다. Git이 설치되어 있는지 확인하세요.');
  paths=changedPaths(out);
 }else{if(typeof value.paths!=='string'||!value.paths.trim()||value.paths.length>400000)throw new Problem(400,'확인할 변경 목록을 붙여 넣으세요.');paths=changedPaths(value.paths)}
 return checkPaths(w,me,paths);
}

const recordType=(w,me,name,color,description)=>{
 const found=q('SELECT id FROM record_types WHERE workspace_id=? AND name=? AND archived=0',w,name);if(found)return found.id;
 const t=id();run('INSERT INTO record_types(id,workspace_id,name,description,owner_id,color,archived,created_at) VALUES(?,?,?,?,?,?,0,?)',t,w,name,description,me.userId,color,now());return t;
};
const Severity={critical:['치명','high'],high:['높음','high'],normal:['보통','normal'],low:['낮음','low']};
const Frequency={always:'항상',often:'자주',sometimes:'가끔',once:'한 번'};
const pick=(value,table,fallback)=>Object.hasOwn(table,value)?value:fallback;

// Sync work inside the caller's transaction.
export function projectAction(action,value,w,me,role){
 if(action==='locks')return {locks:openLocks(w).map(l=>({...l,mine:l.user_id===me.userId}))};
 if(action==='lock'){
  const p=lockPath(value.path),hours=Math.min(168,Math.max(1,Math.round(Number(value.hours)||24))),reason=text(value.reason??'',300),entry=text(value.entry??'',80)||null;
  if(entry&&!q('SELECT id FROM entries WHERE id=? AND workspace_id=? AND deleted_at IS NULL',entry,w))throw new Problem(400,'연결할 기록을 확인하세요.');
  const until=new Date(Date.now()+hours*3600000).toISOString(),locks=openLocks(w);
  const other=locks.find(l=>l.user_id!==me.userId&&overlaps(l.path,p));
  if(other)throw new Problem(409,`${other.holder}님이 작업 중입니다: ${other.path} (${kst(other.expires_at)}까지)${other.reason?' - '+other.reason:''}`);
  const own=locks.find(l=>l.user_id===me.userId&&l.path.toLowerCase()===p.toLowerCase());
  if(own){run('UPDATE asset_locks SET expires_at=?,reason=?,entry_id=? WHERE id=?',until,reason,entry,own.id);audit(w,'lock',entry,{path:p,until});return {id:own.id,extended:true}}
  const lockId=id();run('INSERT INTO asset_locks(id,workspace_id,path,user_id,reason,entry_id,created_at,expires_at) VALUES(?,?,?,?,?,?,?,?)',lockId,w,p,me.userId,reason,entry,now(),until);
  audit(w,'lock',entry,{path:p,until});return {id:lockId};
 }
 if(action==='unlock'){
  const lock=q('SELECT * FROM asset_locks WHERE id=? AND workspace_id=? AND released_at IS NULL',text(value.id,80,true),w);
  if(!lock)throw new Problem(404,'이미 풀린 잠금입니다.');
  if(lock.user_id!==me.userId&&!['owner','admin'].includes(role))throw new Problem(403,'잠근 사람이나 관리자만 풀 수 있습니다.');
  run('UPDATE asset_locks SET released_at=?,released_by=? WHERE id=?',now(),me.userId,lock.id);audit(w,'unlock',lock.entry_id,{path:lock.path});
  if(lock.user_id!==me.userId)notify(w,lock.user_id,`관리자가 작업 잠금을 풀었습니다: ${lock.path}`,lock.entry_id);
  return {ok:true};
 }
 if(action==='bugReport'){
  const playtest=value.type==='playtest',title=text(value.title,200,true).trim(),mutation=text(value.mutation,100,true);
  const existing=q('SELECT id FROM entries WHERE workspace_id=? AND mutation_id=?',w,mutation);if(existing)return {id:existing.id,repeated:true};
  const build=text(value.build??'',80).trim(),place=text(value.place??'',200).trim(),day=kstToday();
  let body,summary,priority='normal',kind;
  if(playtest){
   const good=text(value.good??'',5000).trim(),bad=text(value.bad??'',5000).trim(),idea=text(value.idea??'',5000).trim(),tester=text(value.tester??'',80).trim();
   if(!good&&!bad&&!idea)throw new Problem(400,'좋았던 점, 불편한 점, 제안 중 하나는 적어 주세요.');
   kind='note';summary=[tester&&'플레이어: '+tester,build&&'빌드: '+build,place&&'위치: '+place].filter(Boolean).join(' · ')||null;
   body=[summary&&`- ${summary.replaceAll(' · ','\n- ')}`,good&&'## 좋았던 점\n\n'+good,bad&&'## 불편한 점\n\n'+bad,idea&&'## 제안\n\n'+idea].filter(Boolean).join('\n\n');
  }else{
   const severity=pick(value.severity,Severity,'normal'),frequency=pick(value.frequency,Frequency,'sometimes');
   const steps=text(value.steps??'',10000).trim(),expected=text(value.expected??'',3000).trim(),actual=text(value.actual??'',3000).trim();
   if(!steps||!actual)throw new Problem(400,'재현 순서와 실제 결과는 꼭 적어 주세요.');
   kind='task';priority=Severity[severity][1];summary=[`심각도: ${Severity[severity][0]}`,`빈도: ${Frequency[frequency]}`,build&&'빌드: '+build,place&&'위치: '+place].filter(Boolean).join(' · ');
   body=[`- ${summary.replaceAll(' · ','\n- ')}`,'## 재현 순서\n\n'+steps,expected&&'## 기대 결과\n\n'+expected,'## 실제 결과\n\n'+actual].filter(Boolean).join('\n\n');
  }
  const type=playtest?recordType(w,me,'플레이테스트','#7a5cc2','플레이테스트 의견'):recordType(w,me,'버그','#c0503a','버그 신고');
  const e=id(),stamp=now();
  run("INSERT INTO entries(id,workspace_id,kind,title,body,author_id,day,status,priority,custom_type_id,summary,created_at,updated_at,mutation_id,auto_summary) VALUES(?,?,?,?,?,?,?,'todo',?,?,?,?,?,?,'{}')",
   e,w,kind,(playtest?'[플레이테스트] ':'[버그] ')+title,body,me.userId,day,priority,type,summary,stamp,stamp,mutation);
  audit(w,'create',e,{kind,title});
  return {id:e};
 }
 if(action==='bugs'){
  const types=rows("SELECT id,name FROM record_types WHERE workspace_id=? AND name IN ('버그','플레이테스트')",w),bug=types.find(t=>t.name==='버그')?.id,play=types.find(t=>t.name==='플레이테스트')?.id;
  const list=(type,open)=>type?rows(`SELECT id,title,status,priority,summary,assigned_id,day,updated_at FROM entries WHERE workspace_id=? AND custom_type_id=? AND deleted_at IS NULL${open?" AND status<>'done'":''} ORDER BY ${open?"CASE priority WHEN 'high' THEN 0 WHEN 'normal' THEN 1 ELSE 2 END,":''}created_at DESC LIMIT 100`,w,type):[];
  return {bugs:list(bug,true),playtests:list(play,false).slice(0,30)};
 }
 if(action==='milestones'){
  const today=kstToday();
  // Every record under each goal at any depth (UNION stops on a parent cycle); only work kinds count toward progress.
  const counts=rows(`WITH RECURSIVE tree(id,root) AS (SELECT id,id FROM entries WHERE workspace_id=? AND kind='goal' AND deleted_at IS NULL
    UNION SELECT e.id,t.root FROM entries e JOIN tree t ON e.parent_id=t.id WHERE e.deleted_at IS NULL AND e.workspace_id=?)
   SELECT t.root,count(*) total,sum(e.status='done') done,sum(e.status='inprogress') doing,sum(e.status='blocked') blocked,
    sum(e.status<>'done' AND e.due_day IS NOT NULL AND e.due_day<?) late
   FROM tree t JOIN entries e ON e.id=t.id WHERE t.id<>t.root AND e.kind IN ('task','plan','goal') GROUP BY t.root`,w,w,today);
  const byRoot=new Map(counts.map(c=>[c.root,c]));
  const goals=rows("SELECT id,title,status,due_day,assigned_id,updated_at FROM entries WHERE workspace_id=? AND kind='goal' AND deleted_at IS NULL AND (status<>'done' OR updated_at>=?) ORDER BY due_day IS NULL,due_day,created_at",w,new Date(Date.now()-30*86400000).toISOString());
  return {today,milestones:goals.map(g=>{const c=byRoot.get(g.id)??{total:0,done:0,doing:0,blocked:0,late:0};
   const daysLeft=g.due_day?Math.round((Date.parse(g.due_day+'T00:00:00Z')-Date.parse(today+'T00:00:00Z'))/86400000):null;
   return {...g,total:c.total,done:c.done??0,doing:c.doing??0,blocked:c.blocked??0,late:c.late??0,percent:c.total?Math.round(100*(c.done??0)/c.total):null,daysLeft}})};
 }
 throw new Problem(400,'알 수 없는 요청입니다.');
}

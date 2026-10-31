// Team tools (self-host/team.mjs): MeetingRecorder import, weekly report, commit links. Run: node scripts/check-team-tools.mjs
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {startServer} from '../self-host/server.mjs';
import {q,run,now,dataRoot} from '../self-host/runtime.mjs';
import {recordCode} from '../self-host/team.mjs';
const root=path.resolve('outputs/team-tools-'+Date.now()),checks=[];await mkdir(root,{recursive:true});let app,cookie='';const check=(name,value)=>{assert.ok(value,name);checks.push(name)};
const req=async(url,value,status=200)=>{const response=await fetch(app.origin+url,{method:value===undefined?'GET':'POST',headers:{Cookie:cookie,...(value!==undefined?{'Content-Type':'application/json',Origin:app.origin}:{})},body:value===undefined?undefined:JSON.stringify(value)});if(response.headers.get('set-cookie'))cookie=response.headers.get('set-cookie').split(';')[0];const data=await response.json();assert.equal(response.status,status,url+JSON.stringify(data));return data};
try{
 app=await startServer({data:path.join(root,'data'),port:0,desktop:true});await req('/api/auth/launch',{token:app.launch.token});const me=(await req('/api/auth/session')).user,w=(await req('/api/data',{action:'createWorkspace',name:'팀 도구 검사'})).workspace;
 const post=(action,v={},s=200)=>req('/api/features',{workspace:w,action,...v},s);

 const meeting={title:'주간 회의',day:'2026-10-27',summary:'### 할 일\n\n- [00:01:00] 나: 문서 정리할게요',notes:'- [00:00:10] 빌드 일정',transcript:'**[00:00:00] 나:** 시작합니다',tasks:['문서 정리','빌드 올리기'],mutation:'meeting-1'};
 const first=await post('meetingImport',meeting);
 check('회의 가져오기: 회의 기록과 할 일 생성',q("SELECT kind FROM entries WHERE id=?",first.meeting).kind==='meeting'&&q('SELECT count(*) n FROM entries WHERE parent_id=?',first.meeting).n===2);
 check('회의 본문에 요약·메모·대화문 모두 보존',['## 요약','문서 정리할게요','## 메모','빌드 일정','## 대화문','시작합니다'].every(s=>q('SELECT body FROM entries WHERE id=?',first.meeting).body.includes(s)));
 const again=await post('meetingImport',meeting);
 check('같은 요청 재시도는 두 번 만들지 않음',again.repeated&&again.meeting===first.meeting&&q("SELECT count(*) n FROM entries WHERE kind='meeting'").n===1);
 await post('meetingImport',{...meeting,mutation:'meeting-2',summary:'',notes:'',transcript:''},400);checks.push('내용 없는 회의 거부');
 await post('meetingImport',{...meeting,mutation:'meeting-3',tasks:['x'.repeat(201)]},400);checks.push('너무 긴 할 일 거부');

 const done=(await req('/api/data',{workspace:w,action:'create',kind:'task',title:'끝낸 업무',body:'',day:'2026-10-28',assigned_id:me.userId})).id;
 run("UPDATE entries SET status='done',updated_at=? WHERE id=?",'2026-10-28T03:00:00.000Z',done);
 const late=(await req('/api/data',{workspace:w,action:'create',kind:'task',title:'늦은 업무',body:'',day:'2026-10-27',due_day:'2026-10-29'})).id;
 run("UPDATE entries SET status='blocked' WHERE id=?",late);
 const report=await req(`/api/features?action=weeklyReport&workspace=${w}&start=2026-10-26`);
 check('주간 보고: 끝낸 일·막힌 일·기한·회의 집계',report.counts.done===1&&report.counts.blocked===1&&report.counts.late>=1&&report.counts.meetings===1);
 check('주간 보고에 기록 코드 표시',report.markdown.includes(recordCode(done))&&report.markdown.includes('담당 미정'));
 await req(`/api/features?action=weeklyReport&workspace=${w}&start=2026-10-26&save=1`,undefined,405);checks.push('GET으로는 보고서 저장 불가');
 const saved=await post('weeklyReport',{start:'2026-10-26',save:true,mutation:'weekly-1'}),savedAgain=await post('weeklyReport',{start:'2026-10-26',save:true,mutation:'weekly-1'});
 check('보고서 저장은 작업 로그 하나',saved.saved===savedAgain.saved&&q("SELECT kind FROM entries WHERE id=?",saved.saved).kind==='log');
 await req(`/api/features?action=weeklyReport&workspace=${w}&start=2026-13-40`,undefined,400);checks.push('잘못된 날짜 거부');

 const repo=path.join(root,'repo');await mkdir(repo);const git=(...a)=>execFileSync('git',['-C',repo,...a],{stdio:'pipe'});
 git('init','-q');git('config','user.email','check@local.invalid');git('config','user.name','검사');
 await writeFile(path.join(repo,'a.txt'),'a');git('add','.');git('commit','-q','-m',`문서 정리 ${recordCode(first.meeting).toUpperCase()}`);
 await writeFile(path.join(repo,'b.txt'),'b');git('add','.');git('commit','-q','-m','관계없는 커밋');
 await writeFile(path.join(dataRoot,'vault.json'),JSON.stringify({root:repo})); // The linked Git folder, as the 이 PC·Git tab saves it.
 const commits=await req(`/api/features?action=commits&workspace=${w}&entry=${first.meeting}`);
 check('커밋 연결: 코드가 든 커밋만, 대소문자 무관',commits.available&&commits.commits.length===1&&commits.commits[0].subject.startsWith('문서 정리'));

 // Asset locks, between the owner and a second member.
 const ownerCookie=cookie;await req('/api/auth/register',{username:'artist',name:'아티스트',password:'test-password-23456'});
 const artist=(await req('/api/auth/accounts')).accounts.find(a=>a.username==='artist');await req('/api/auth/accounts',{id:artist.id,status:'active'});
 run('INSERT INTO members VALUES(?,?,?,?,?,?,?)',crypto.randomUUID(),w,artist.id,'아티스트','artist@local.invalid','member',now());
 const own=await post('lock',{path:'Content\\Maps\\Lobby.umap',hours:8,reason:'조명'}),extended=await post('lock',{path:'Content/Maps/Lobby.umap',hours:24});
 check('잠금: 역슬래시 경로 정리, 같은 사람은 연장',extended.extended&&extended.id===own.id&&q('SELECT path FROM asset_locks WHERE id=?',own.id).path==='Content/Maps/Lobby.umap');
 for(const bad of ['C:/Game/Lobby.umap','/Content/A.uasset','Content/../Secret.uasset'])await post('lock',{path:bad},400);checks.push('잠금: 절대 경로·상위 폴더 거부');
 cookie='';await req('/api/auth/login',{username:'artist',password:'test-password-23456'});const artistCookie=cookie;
 await post('lock',{path:'content/maps/lobby.umap'},409);await post('lock',{path:'Content/Maps/'},409);checks.push('잠금: 남이 잠근 파일·그 폴더는 대소문자 무관하게 막음');
 const pasted=await post('lockCheck',{paths:' M Content/Maps/Lobby.umap\n?? Content/Characters/New.uasset\nR  Old.umap -> Content/Maps/Arena.umap\n M Source/Game.cpp'});
 check('변경 확인: 남의 잠금과 겹침, 안 잠근 에셋만 골라냄',pasted.checked===4&&pasted.conflicts.length===1&&pasted.conflicts[0].lock.holder&&pasted.unlocked.join()==='Content/Characters/New.uasset,Content/Maps/Arena.umap');
 const artistLock=await post('lock',{path:'Content/Characters/'});await post('unlock',{id:own.id},403);checks.push('잠금: 남의 잠금은 팀원이 못 풂');
 cookie=ownerCookie;await post('unlock',{id:artistLock.id});
 check('관리자가 풀면 잠근 사람에게 알림',!!q("SELECT id FROM notifications WHERE user_id=? AND title LIKE '%Content/Characters/%'",artist.id)&&(await req(`/api/features?action=locks&workspace=${w}`)).locks.length===1);
 run('UPDATE asset_locks SET expires_at=? WHERE id=?','2000-01-01T00:00:00.000Z',own.id);cookie=artistCookie;await post('lock',{path:'Content/Maps/Lobby.umap'});
 check('기한 지난 잠금은 다른 사람이 잡을 수 있음',(await req(`/api/features?action=locks&workspace=${w}`)).locks.every(l=>l.user_id===artist.id));
 cookie=ownerCookie;await writeFile(path.join(repo,'a.txt'),'changed');await mkdir(path.join(repo,'Content','Maps'),{recursive:true});await writeFile(path.join(repo,'Content','Maps','Lobby.umap'),'x');
 const fromGit=await post('lockCheck',{fromGit:true});
 check('서버 PC는 연결한 Git 폴더로 바로 확인',fromGit.conflicts.some(c=>c.path==='Content/Maps/Lobby.umap'));

 // Bug and playtest reports.
 await post('bugReport',{type:'bug',title:'벽에 낌',steps:'',actual:'낌',mutation:'bug-0'},400);checks.push('버그: 재현 순서 없으면 거부');
 const bug=await post('bugReport',{type:'bug',title:'벽에 낌',severity:'critical',frequency:'always',build:'0.3.1',place:'Lobby',steps:'1. 문으로 달린다',expected:'지나감',actual:'벽에 낌',mutation:'bug-1'});
 const bugRow=q('SELECT e.*,t.name type FROM entries e JOIN record_types t ON t.id=e.custom_type_id WHERE e.id=?',bug.id);
 check('버그: 업무·높은 우선순위·버그 항목·요약 정리',bugRow.kind==='task'&&bugRow.priority==='high'&&bugRow.type==='버그'&&bugRow.summary.startsWith('심각도: 치명')&&bugRow.body.includes('## 재현 순서'));
 check('버그: 같은 요청 재시도는 하나만',(await post('bugReport',{type:'bug',title:'벽에 낌',steps:'1',actual:'2',mutation:'bug-1'})).id===bug.id);
 const play=await post('bugReport',{type:'playtest',title:'첫 판',tester:'친구',bad:'시작이 느림',mutation:'play-1'});const bugs=await req(`/api/features?action=bugs&workspace=${w}`);
 check('플레이테스트 의견은 메모로, 목록에 각각 표시',q('SELECT kind FROM entries WHERE id=?',play.id).kind==='note'&&bugs.bugs.some(b=>b.id===bug.id)&&bugs.playtests.some(p=>p.id===play.id)&&q("SELECT count(*) n FROM record_types WHERE workspace_id=? AND name='버그'",w).n===1);

 // Milestones: progress counts work at any depth under a goal.
 const goal=(await req('/api/data',{workspace:w,action:'create',kind:'goal',title:'알파 빌드',body:'',day:'2026-10-27',due_day:'2099-12-31'})).id;
 const plan=(await req('/api/data',{workspace:w,action:'create',kind:'plan',title:'맵 완성',body:'',day:'2026-10-27'})).id;
 const t1=(await req('/api/data',{workspace:w,action:'create',kind:'task',title:'조명',body:'',day:'2026-10-27'})).id;
 const t2=(await req('/api/data',{workspace:w,action:'create',kind:'task',title:'사운드',body:'',day:'2026-10-27',due_day:'2026-01-01'})).id;
 const memo=(await req('/api/data',{workspace:w,action:'create',kind:'note',title:'참고',body:'',day:'2026-10-27'})).id;
 run('UPDATE entries SET parent_id=? WHERE id IN (?,?,?)',goal,plan,t2,memo);run("UPDATE entries SET parent_id=?,status='done' WHERE id=?",plan,t1);run("UPDATE entries SET status='blocked' WHERE id=?",t2);
 const alpha=(await req(`/api/features?action=milestones&workspace=${w}`)).milestones.find(m=>m.id===goal);
 check('마일스톤: 하위 단계까지 업무만 세고 진행률·막힘·기한 지남 계산',alpha.total===3&&alpha.done===1&&alpha.blocked===1&&alpha.late===1&&alpha.percent===33&&alpha.daysLeft>0);
 await writeFile(path.join(root,'results.json'),JSON.stringify({passed:checks.length,checks},null,2));console.log(JSON.stringify({passed:checks.length,checks},null,2));
}finally{if(app)await app.close()}

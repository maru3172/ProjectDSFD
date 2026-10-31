// Team tools (self-host/team.mjs): MeetingRecorder import, weekly report, commit links. Run: node scripts/check-team-tools.mjs
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {startServer} from '../self-host/server.mjs';
import {q,run,dataRoot} from '../self-host/runtime.mjs';
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
 await writeFile(path.join(root,'results.json'),JSON.stringify({passed:checks.length,checks},null,2));console.log(JSON.stringify({passed:checks.length,checks},null,2));
}finally{if(app)await app.close()}

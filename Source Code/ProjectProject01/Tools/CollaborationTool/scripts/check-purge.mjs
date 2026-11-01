// Permanent deletion (self-host/purge.mjs) and its Git folder side (vault.mjs). Run: node scripts/check-purge.mjs
import assert from 'node:assert/strict';
import {mkdir,writeFile,readFile} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {startServer} from '../self-host/server.mjs';
import {q,run,now,safeBlob} from '../self-host/runtime.mjs';
const root=path.resolve('outputs/purge-'+Date.now()),checks=[];await mkdir(root,{recursive:true});let app,cookie='';const check=(name,value)=>{assert.ok(value,name);checks.push(name)};
const req=async(url,value,status=200)=>{const response=await fetch(app.origin+url,{method:value===undefined?'GET':'POST',headers:{Cookie:cookie,...(value!==undefined?{'Content-Type':'application/json',Origin:app.origin}:{})},body:value===undefined?undefined:JSON.stringify(value)});if(response.headers.get('set-cookie'))cookie=response.headers.get('set-cookie').split(';')[0];const data=await response.json();assert.equal(response.status,status,`${url} ${JSON.stringify(data)}`);return data};
const wait=ms=>new Promise(r=>setTimeout(r,ms));
try{
 app=await startServer({data:path.join(root,'data'),port:0,desktop:true});await req('/api/auth/launch',{token:app.launch.token});const owner=(await req('/api/auth/session')).user,w=(await req('/api/data',{action:'createWorkspace',name:'영구 삭제 검사'})).workspace;
 const ownerCookie=cookie;await req('/api/auth/register',{username:'member',name:'팀원',password:'test-password-23456'});
 const member=(await req('/api/auth/accounts')).accounts.find(a=>a.username==='member');await req('/api/auth/accounts',{id:member.id,status:'active'});
 run('INSERT INTO members VALUES(?,?,?,?,?,?,?)',crypto.randomUUID(),w,member.id,'팀원','member@local.invalid','member',now());
 cookie='';await req('/api/auth/login',{username:'member',password:'test-password-23456'});const memberCookie=cookie;
 const as=who=>{cookie=who==='owner'?ownerCookie:memberCookie};
 const post=(action,v={},s=200)=>req('/api/features',{workspace:w,action,...v},s);
 const create=(title,extra={})=>req('/api/data',{workspace:w,action:'create',kind:'task',title,body:title+' 본문',day:'2026-11-01',...extra}).then(r=>r.id);
 const upload=async(entry,name,text)=>{const form=new FormData();form.set('entry',entry);form.set('file',new File([text],name));form.set('comment','true');const r=await fetch(app.origin+'/api/file',{method:'POST',headers:{Cookie:cookie,Origin:app.origin},body:form});assert.equal(r.status,200);return (await r.json()).id};
 const blobOf=id=>safeBlob(q('SELECT key FROM attachments WHERE id=?',id).key);

 // A record with everything that can hang off it.
 as('owner');const parent=await create('부모 기록'),target=await create('지울 기록',{parent_id:parent}),child=await create('하위 기록',{parent_id:target}),other=await create('선행 업무');
 await req('/api/data',{workspace:w,action:'comment',entry:target,body:'지울 기록의 댓글'});
 const file=await upload(target,'첨부.txt','첨부 내용');const fileBlob=blobOf(file);
 await post('check',{entry:target,label:'체크 항목'});await post('dependency',{entry:target,required:other});await post('favorite',{entry:target,enabled:true});
 const vault=path.join(root,'vault');await mkdir(vault,{recursive:true});execFileSync('git',['init','-q'],{cwd:vault,windowsHide:true});
 await req('/api/vault',{action:'configure',root:vault});await req('/api/vault',{action:'export'});
 const vaultEntry=path.join(vault,'daybook',w,'entries',target+'.json'),saved=await readFile(vaultEntry,'utf8');
 check('Git 폴더에 기록이 먼저 저장됨',existsSync(vaultEntry)&&existsSync(path.join(vault,'daybook',w,'files',file)));

 const t=await (async()=>{const e=q('SELECT version FROM entries WHERE id=?',target);await req('/api/data',{workspace:w,action:'delete',id:target,version:e.version});return true})();
 as('member');await post('purgeEntry',{entry:target},403);checks.push('작성자·관리자가 아니면 영구 삭제 불가');
 as('owner');await post('purgeEntry',{entry:target});await wait(150);
 const left=['comments','activity','check_items','attachments','favorites'].filter(t=>q(`SELECT count(*) n FROM ${t} WHERE entry_id=?`,target).n>0);
 check('휴지통의 기록을 영구 삭제하면 댓글·이력·체크리스트·첨부·즐겨찾기까지 사라짐',t&&!q('SELECT id FROM entries WHERE id=?',target)&&left.length===0&&!q('SELECT 1 FROM dependencies WHERE entry_id=? OR requires_id=?',target,target));
 check('첨부 파일 원본도 지워짐',!existsSync(fileBlob));
 check('하위 기록은 남고 연결만 풀림',q('SELECT parent_id FROM entries WHERE id=?',child).parent_id===null&&!!q('SELECT id FROM entries WHERE id=?',parent));
 check('이력에는 내용 없이 영구 삭제 사실만 남음',!q("SELECT 1 FROM activity WHERE workspace_id=? AND (before_json LIKE '%지울 기록%' OR after_json LIKE '%지울 기록%')",w)&&!!q("SELECT 1 FROM activity WHERE workspace_id=? AND action='entry_purged'",w));

 await req('/api/vault',{action:'export'});
 check('다음 Git 내보내기에서 지운 기록과 첨부 파일이 폴더에서 빠짐',!existsSync(vaultEntry)&&!existsSync(path.join(vault,'daybook',w,'files',file)));
 await writeFile(vaultEntry,saved);const preview=await req('/api/vault',{action:'preview',workspace:w});
 check('Git에서 지운 기록을 다시 받아와도 되살아나지 않음',!preview.changes.some(c=>c.file==='entries/'+target+'.json'));

 // Straight away, without the trash.
 const direct=await create('바로 지울 기록');await post('purgeEntry',{entry:direct});
 check('휴지통을 거치지 않고 바로 영구 삭제',!q('SELECT id FROM entries WHERE id=?',direct));
 as('member');const mine=await create('팀원이 쓴 기록');await post('purgeEntry',{entry:mine});
 check('작성자는 자기 기록을 영구 삭제할 수 있음',!q('SELECT id FROM entries WHERE id=?',mine));

 // One file, all of its versions.
 as('owner');const holder=await create('파일 보관 기록');const v1=await upload(holder,'도면.txt','1판');
 const form=new FormData();form.set('file',new File(['2판'],'도면.txt'));
 await post('file',{entry:holder,file:v1,operation:'delete'});await post('file',{entry:holder,file:v1,operation:'restore'});
 const v1Blob=blobOf(v1);await post('purgeFile',{entry:holder,file:v1});await wait(150);
 check('파일 영구 삭제: 첨부·파일 메시지·원본이 함께 사라지고 기록은 남음',!q('SELECT id FROM attachments WHERE id=?',v1)&&!q('SELECT id FROM comments WHERE attachment_id=?',v1)&&!existsSync(v1Blob)&&!!q('SELECT id FROM entries WHERE id=?',holder));
 as('member');const ownerFile=(as('owner'),await upload(holder,'관리자 파일.txt','x'));as('member');await post('purgeFile',{entry:holder,file:ownerFile},403);checks.push('남의 기록 파일은 영구 삭제 불가');

 // Emptying the trash: admin only, takes trashed records and trashed files.
 as('owner');const a=await create('휴지통 A'),b=await create('휴지통 B'),keep=await create('남길 기록');
 for(const id of [a,b]){const e=q('SELECT version FROM entries WHERE id=?',id);await req('/api/data',{workspace:w,action:'delete',id,version:e.version})}
 const trashedFile=await upload(keep,'버린 파일.txt','버림');await post('file',{entry:keep,file:trashedFile,operation:'delete'});
 as('member');await post('emptyTrash',{},403);checks.push('휴지통 비우기는 관리자만');
 as('owner');const emptied=await post('emptyTrash');
 check('휴지통 비우기: 휴지통의 기록과 버린 파일만 영구 삭제',emptied.records===2&&emptied.files===1&&!q('SELECT 1 FROM entries WHERE deleted_at IS NOT NULL AND workspace_id=?',w)&&!q('SELECT id FROM attachments WHERE id=?',trashedFile)&&!!q('SELECT id FROM entries WHERE id=?',keep));
 await post('purgeEntry',{entry:'없는-기록'},404);checks.push('없는 기록은 404');
 await writeFile(path.join(root,'results.json'),JSON.stringify({passed:checks.length,checks},null,2));console.log(JSON.stringify({passed:checks.length,checks},null,2));
}finally{if(app)await app.close()}

// 1:1 conversations and chat/contact alerts (self-host/contacts.mjs). Run: node scripts/check-contacts.mjs
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import path from 'node:path';
import {startServer} from '../self-host/server.mjs';
import {q,run,now} from '../self-host/runtime.mjs';
const root=path.resolve('outputs/contacts-'+Date.now()),checks=[];await mkdir(root,{recursive:true});let app,cookie='';const check=(name,value)=>{assert.ok(value,name);checks.push(name)};
const req=async(url,value,status=200)=>{const response=await fetch(app.origin+url,{method:value===undefined?'GET':'POST',headers:{Cookie:cookie,...(value!==undefined?{'Content-Type':'application/json',Origin:app.origin}:{})},body:value===undefined?undefined:JSON.stringify(value)});if(response.headers.get('set-cookie'))cookie=response.headers.get('set-cookie').split(';')[0];const data=await response.json();assert.equal(response.status,status,`${url} ${JSON.stringify(data)}`);return data};
try{
 app=await startServer({data:path.join(root,'data'),port:0,desktop:true});await req('/api/auth/launch',{token:app.launch.token});const owner=(await req('/api/auth/session')).user,w=(await req('/api/data',{action:'createWorkspace',name:'대화 검사'})).workspace;
 const ownerCookie=cookie,people={};
 for(const [username,name,role] of [['artist','아티스트','member'],['guest','손님','viewer'],['other','다른 공간','none']]){
  cookie=ownerCookie;await req('/api/auth/register',{username,name,password:'test-password-23456'});
  const account=(await req('/api/auth/accounts')).accounts.find(a=>a.username===username);await req('/api/auth/accounts',{id:account.id,status:'active'});
  if(role!=='none')run('INSERT INTO members VALUES(?,?,?,?,?,?,?)',crypto.randomUUID(),w,account.id,name,username+'@local.invalid',role,now());
  cookie='';await req('/api/auth/login',{username,password:'test-password-23456'});people[username]={id:account.id,cookie};
 }
 const as=who=>{cookie=who==='owner'?ownerCookie:people[who].cookie};
 const post=(action,v={},s=200)=>req('/api/features',{workspace:w,action,...v},s);
 const pulse=()=>req(`/api/features?action=pulse&workspace=${w}`);
 const thread=peer=>req(`/api/features?action=dmThread&workspace=${w}&peer=${peer}`);

 as('artist');await post('dmSend',{peer:owner.userId,body:'  조명 작업 끝났어요\n확인 부탁해요  '});
 as('owner');let mine=await pulse();
 check('받은 1:1 메시지는 안 읽음 1개와 미리보기로 알림',mine.direct.length===1&&mine.direct[0].peer===people.artist.id&&mine.direct[0].unread===1&&mine.direct[0].preview==='조명 작업 끝났어요 확인 부탁해요');
 as('artist');check('보낸 사람에게는 알림 없음',(await pulse()).direct.length===0);
 as('owner');const talk=await thread(people.artist.id);
 check('두 사람은 같은 대화를 봄',talk.messages.length===1&&talk.messages[0].body==='조명 작업 끝났어요\n확인 부탁해요'&&talk.messages[0].from_user===people.artist.id);
 await post('dmRead',{peer:people.artist.id,sequence:talk.messages[0].sequence});
 check('읽으면 안 읽음이 사라짐',(await pulse()).direct.length===0);
 as('artist');check('보낸 쪽에서 읽음 확인',(await thread(owner.userId)).read>=talk.messages[0].sequence);
 await post('dmRead',{peer:owner.userId,sequence:999999});
 check('읽음 위치는 실제 받은 메시지를 넘지 않음',(q('SELECT last_row FROM direct_reads WHERE user_id=? AND peer_id=?',people.artist.id,owner.userId)?.last_row??0)===0);

 as('guest');await post('dmSend',{peer:owner.userId,body:'읽기 전용도 1:1은 가능'});
 check('제3자는 남의 1:1을 볼 수 없음',(await thread(people.artist.id)).messages.length===0&&(await thread(owner.userId)).messages.every(m=>m.body==='읽기 전용도 1:1은 가능'));
 check('읽기 전용 팀원도 1:1 대화 가능',q('SELECT count(*) n FROM direct_messages WHERE from_user=?',people.guest.id).n===1);
 const data=await req(`/api/data?workspace=${w}`),search=await req(`/api/data?workspace=${w}&q=${encodeURIComponent('조명 작업')}`);
 const exported=await req(`/api/features?action=exportEntries&workspace=${w}`);
 check('1:1 내용은 기록 목록·검색·내보내기·알림에 나오지 않음',!JSON.stringify(data).includes('조명 작업')&&search.entries.length===0&&!JSON.stringify(exported).includes('조명 작업')&&!q("SELECT 1 FROM notifications WHERE title LIKE '%조명 작업%'"));
 as('owner');const backup=await req(`/api/data?workspace=${w}&mode=export`);
 check('관리자 백업 파일에도 1:1 내용 없음',!JSON.stringify(backup).includes('조명 작업'));

 await post('dmSend',{peer:owner.userId,body:'나에게'},400);await post('dmSend',{peer:people.other.id,body:'다른 공간 사람'},400);
 await post('dmSend',{peer:people.artist.id,body:'   '},400);await post('dmSend',{peer:people.artist.id,body:'x'.repeat(5001)},400);
 checks.push('자신·공간 밖 사람·빈 메시지·너무 긴 메시지 거부');
 as('other');await req(`/api/features?action=pulse&workspace=${w}`,undefined,403);await post('dmSend',{peer:owner.userId,body:'몰래'},403);
 checks.push('공간 밖 사람은 알림 확인·1:1 보내기 불가');

 // Files in a 1:1 conversation: only the two people can open them; nothing reaches record attachments.
 const png=new Uint8Array([137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82]);
 const upload=async(fields,status=200)=>{const form=new FormData();for(const [k,v] of Object.entries(fields))form.set(k,v);const r=await fetch(app.origin+'/api/direct-file',{method:'POST',headers:{Cookie:cookie,Origin:app.origin},body:form});const data=await r.json();assert.equal(r.status,status,JSON.stringify(data));return data};
 const download=async id=>fetch(app.origin+'/api/direct-file?id='+id+'&inline=1',{headers:{Cookie:cookie}});
 as('artist');const photo=await upload({workspace:w,peer:owner.userId,body:'시안입니다',file:new File([png],'시안.png')});
 await upload({workspace:w,peer:owner.userId,file:new File(['메모 내용'],'메모.txt')});
 as('owner');const files=(await thread(people.artist.id)).messages.filter(m=>m.attachment_id);
 check('1:1 사진·파일: 대화에 이름·종류·크기·설명과 함께 표시',files.length===2&&files[0].attachment_name==='시안.png'&&files[0].attachment_type==='image/png'&&files[0].body==='시안입니다'&&files[1].attachment_size===new TextEncoder().encode('메모 내용').length);
 check('설명 없는 파일은 알림 미리보기에 파일 이름',(await pulse()).direct.find(d=>d.peer===people.artist.id)?.preview==='[파일] 메모.txt');
 let got=await download(photo.id);
 check('받는 사람은 사진을 화면에 바로 열 수 있음',got.status===200&&got.headers.get('content-disposition').startsWith('inline')&&Buffer.from(await got.arrayBuffer()).equals(Buffer.from(png)));
 as('artist');check('보낸 사람도 열 수 있음',(await download(photo.id)).status===200);
 as('guest');check('같은 공간의 다른 팀원은 열 수 없음',(await download(photo.id)).status===404);
 as('other');check('공간 밖 사람도 열 수 없음',(await download(photo.id)).status===404);
 as('owner');const listed=await req('/api/data?workspace='+w),legacy=await fetch(app.origin+'/api/file?id='+photo.id,{headers:{Cookie:cookie}});
 check('1:1 파일은 공간 첨부 목록·일반 파일 주소에 나오지 않음',!JSON.stringify(listed).includes('시안.png')&&legacy.status===404);
 check('시스템 백업 대상에 1:1 파일 포함',(await import('../self-host/runtime.mjs')).rows('SELECT DISTINCT key FROM attachments UNION SELECT key FROM direct_attachments').some(r=>r.key==='direct/'+w+'/'+photo.id));
 as('artist');await upload({workspace:w,peer:owner.userId,file:new File(['MZ'],'실행.exe')},400);await upload({workspace:w,peer:owner.userId,file:new File([],'빈.txt')},400);
 await upload({workspace:w,peer:owner.userId,file:new File([new Uint8Array(10*1024*1024+1)],'큰.zip')},400);await upload({workspace:w,peer:people.artist.id,file:new File([png],'나.png')},400);
 as('guest');await upload({workspace:w,peer:people.other.id,file:new File([png],'밖.png')},400);
 checks.push('1:1 파일: 실행 파일·빈 파일·10MB 초과·자신·공간 밖 사람에게 보내기 거부');
 as('other');await upload({workspace:w,peer:owner.userId,file:new File([png],'몰래.png')},403);checks.push('공간 밖 사람은 1:1 파일 보내기 불가');

 // Chat rooms: messages from others raise the room's unread count with the author and a preview.
 as('owner');const room=(await req('/api/data',{workspace:w,action:'create',kind:'chat',title:'아트 대화방',body:'',day:'2026-11-01'})).id;
 as('artist');await req('/api/data',{workspace:w,action:'comment',entry:room,body:'새 캐릭터 시트 올렸습니다'});
 check('대화방 메시지를 쓴 사람에게는 알림 없음',(await pulse()).rooms.length===0);
 as('owner');mine=await pulse();const roomPulse=mine.rooms.find(r=>r.id===room);
 check('대화방 새 메시지: 방 이름·쓴 사람·미리보기',roomPulse?.unread===1&&roomPulse.author_id===people.artist.id&&roomPulse.preview==='새 캐릭터 시트 올렸습니다'&&roomPulse.title==='아트 대화방');
 await post('read',{entry:room,sequence:roomPulse.last_row});
 check('대화방을 읽으면 알림 대상에서 빠짐',(await pulse()).rooms.length===0);

 // Notifications (assignments, mentions, handovers) come through the same pulse.
 await req('/api/data',{workspace:w,action:'create',kind:'task',title:'조명 검수',body:'',day:'2026-11-01',assigned_id:people.artist.id});
 as('artist');check('배정 같은 알림도 함께 전달',(await pulse()).notifications.some(n=>n.title.includes('조명 검수')));
 as('owner');check('아직 1:1이 있었던 사람 목록과 마지막 시각',(await pulse()).conversations.map(c=>c.peer).sort().join()===[people.artist.id,people.guest.id].sort().join());
 await writeFile(path.join(root,'results.json'),JSON.stringify({passed:checks.length,checks},null,2));console.log(JSON.stringify({passed:checks.length,checks},null,2));
}finally{if(app)await app.close()}

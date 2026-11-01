import {Problem,q,rows,run,text,now,json,env,requireUser,membership} from './runtime.mjs';
// 1:1 messages and the "what is new for me" pulse behind chat and contact alerts.
// Direct messages live in their own tables, never in entries/comments, so no record list, search, export, report or
// file route can show them to anyone but the two people in the conversation.
export const contactActions=['pulse','dmThread','dmSend','dmRead'];
const preview=value=>value.replace(/\s+/g,' ').trim().slice(0,120);
const peerOf=(w,me,value)=>{const peer=text(value.peer,120,true);if(peer===me.userId)throw new Problem(400,'다른 팀원을 선택하세요.');return peer};
const currentMember=(w,peer)=>{if(!q('SELECT id FROM members WHERE workspace_id=? AND user_id=?',w,peer))throw new Problem(400,'현재 팀원에게만 보낼 수 있습니다.')};
const messagePreview=row=>row?(row.body?preview(row.body):row.name?(row.content_type?.startsWith('image/')?'[사진] ':'[파일] ')+row.name:''):'';
const sequenceOf=value=>{const n=Number(value);if(!Number.isSafeInteger(n)||n<0)throw new Problem(400,'읽은 위치를 확인하세요.');return n};

export function contactAction(action,value,w,me){
 if(action==='pulse'){
  // Messages from others that I have not read yet, newest position per room/person, plus unread notifications.
  const rooms=rows("SELECT e.id,e.title,(SELECT count(*) FROM comments c WHERE c.workspace_id=e.workspace_id AND c.entry_id=e.id AND c.author_id<>? AND c.rowid>coalesce((SELECT last_row FROM room_reads r WHERE r.entry_id=e.id AND r.user_id=?),0)) unread FROM entries e WHERE e.workspace_id=? AND e.kind='chat' AND e.deleted_at IS NULL",me.userId,me.userId,w)
   .filter(r=>r.unread>0).map(r=>{const last=q('SELECT rowid AS sequence,author_id,body FROM comments WHERE workspace_id=? AND entry_id=? AND author_id<>? ORDER BY rowid DESC LIMIT 1',w,r.id,me.userId);return {...r,last_row:last.sequence,author_id:last.author_id,preview:preview(last.body??'')}});
  const direct=rows('SELECT d.from_user peer,count(*) unread,max(d.rowid) last_row FROM direct_messages d WHERE d.workspace_id=? AND d.to_user=? AND d.rowid>coalesce((SELECT last_row FROM direct_reads r WHERE r.workspace_id=d.workspace_id AND r.user_id=d.to_user AND r.peer_id=d.from_user),0) GROUP BY d.from_user',w,me.userId)
   .map(d=>({...d,preview:messagePreview(q('SELECT m.body,a.name,a.content_type FROM direct_messages m LEFT JOIN direct_attachments a ON a.id=m.attachment_id WHERE m.rowid=?',d.last_row))}));
  const conversations=rows('SELECT CASE WHEN from_user=? THEN to_user ELSE from_user END peer,max(created_at) last_at FROM direct_messages WHERE workspace_id=? AND (from_user=? OR to_user=?) GROUP BY peer',me.userId,w,me.userId,me.userId);
  const notifications=rows('SELECT id,title,entry_id,created_at FROM notifications WHERE workspace_id=? AND user_id=? AND read_at IS NULL ORDER BY created_at DESC LIMIT 20',w,me.userId);
  return {me:me.userId,rooms,direct,conversations,notifications};
 }
 if(action==='dmThread'){
  const peer=peerOf(w,me,value);
  const messages=rows('SELECT * FROM (SELECT m.rowid AS sequence,m.id,m.from_user,m.to_user,m.body,m.created_at,m.attachment_id,a.name AS attachment_name,a.size AS attachment_size,a.content_type AS attachment_type FROM direct_messages m LEFT JOIN direct_attachments a ON a.id=m.attachment_id WHERE m.workspace_id=? AND ((m.from_user=? AND m.to_user=?) OR (m.from_user=? AND m.to_user=?)) ORDER BY m.rowid DESC LIMIT 200) ORDER BY sequence',w,me.userId,peer,peer,me.userId);
  return {messages,read:q('SELECT last_row FROM direct_reads WHERE workspace_id=? AND user_id=? AND peer_id=?',w,peer,me.userId)?.last_row??0};
 }
 if(action==='dmSend'){
  const peer=peerOf(w,me,value);currentMember(w,peer);const body=text(value.body,5000,true);
  run('INSERT INTO direct_messages(id,workspace_id,from_user,to_user,body,created_at) VALUES(?,?,?,?,?,?)',crypto.randomUUID(),w,me.userId,peer,body,now());
  return {ok:true};
 }
 if(action==='dmRead'){
  const peer=peerOf(w,me,value),sequence=sequenceOf(value.sequence);
  const max=q('SELECT coalesce(max(rowid),0) n FROM direct_messages WHERE workspace_id=? AND from_user=? AND to_user=?',w,peer,me.userId).n;
  run('INSERT INTO direct_reads VALUES(?,?,?,?) ON CONFLICT(workspace_id,user_id,peer_id) DO UPDATE SET last_row=max(last_row,excluded.last_row)',w,me.userId,peer,Math.min(sequence,max));
  return {ok:true};
 }
 throw new Problem(400,'지원하지 않는 대화 작업입니다.');
}

// Files sent in a 1:1 conversation. They are kept apart from record attachments (which every member of the space can
// open) and only the sender and the receiver can download them. System backups include them.
const allowed=['png','jpg','jpeg','gif','webp','pdf','txt','md','csv','json','docx','xlsx','pptx','zip'];
const images=['image/png','image/jpeg','image/gif','image/webp'];
function sniff(bytes){const head=n=>new TextDecoder().decode(bytes.slice(0,n));
 if(bytes[0]===137&&bytes[1]===80&&bytes[2]===78&&bytes[3]===71)return 'image/png';
 if(bytes[0]===255&&bytes[1]===216&&bytes[2]===255)return 'image/jpeg';
 if(/^GIF8[79]a$/.test(head(6)))return 'image/gif';
 if(head(4)==='RIFF'&&new TextDecoder().decode(bytes.slice(8,12))==='WEBP')return 'image/webp';
 if(head(5)==='%PDF-')return 'application/pdf';
 return 'application/octet-stream'}
export async function directFileRoute(request,url){
 const me=requireUser();
 if(request.method==='POST'){
  const form=await request.formData(),w=text(form.get('workspace'),80,true),file=form.get('file');membership(w,false);
  const peer=peerOf(w,me,{peer:form.get('peer')});currentMember(w,peer);
  if(!(file instanceof File)||!file.size||file.size>10*1024*1024)throw new Problem(400,'10MB 이하의 파일을 선택하세요.');
  const name=file.name.replace(/[\x00-\x1f/\\]/g,'_').slice(0,180)||'첨부',ext=name.split('.').pop()?.toLowerCase();
  if(!ext||!allowed.includes(ext))throw new Problem(400,'사진·PDF·문서·ZIP 파일을 보낼 수 있습니다.');
  const caption=text(form.get('body')??'',5000),bytes=new Uint8Array(await file.arrayBuffer()),type=sniff(bytes),id=crypto.randomUUID(),key='direct/'+w+'/'+id,at=now();
  await env.BUCKET.put(key,bytes,{httpMetadata:{contentType:type}});
  try{run('INSERT INTO direct_attachments VALUES(?,?,?,?,?,?,?,?,?)',id,w,me.userId,peer,name,file.size,key,type,at);run('INSERT INTO direct_messages(id,workspace_id,from_user,to_user,body,created_at,attachment_id) VALUES(?,?,?,?,?,?,?)',crypto.randomUUID(),w,me.userId,peer,caption,at,id)}
  catch(error){await env.BUCKET.delete(key);throw error}
  return json({ok:true,id,name});
 }
 const file=q('SELECT * FROM direct_attachments WHERE id=?',url.searchParams.get('id')??'');
 // Anyone else gets the same answer as for a file that does not exist.
 if(!file||![file.from_user,file.to_user].includes(me.userId)||!q('SELECT id FROM members WHERE workspace_id=? AND user_id=?',file.workspace_id,me.userId))throw new Problem(404,'파일이 없거나 접근 권한이 없습니다.');
 const object=await env.BUCKET.get(file.key);if(!object)throw new Problem(404,'저장된 파일을 찾지 못했습니다.');
 const inline=url.searchParams.get('inline')==='1'&&images.includes(file.content_type);
 return new Response(object.body,{headers:{'Content-Type':file.content_type,'Content-Length':String(file.size),'Content-Disposition':`${inline?'inline':'attachment'}; filename*=UTF-8''${encodeURIComponent(file.name)}`,'Cache-Control':'private, no-store','X-Content-Type-Options':'nosniff','Content-Security-Policy':"default-src 'none'"}});
}

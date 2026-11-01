"use client";
import {useCallback,useEffect,useRef,useState,type ClipboardEvent,type ReactNode} from 'react';
import {createPortal} from 'react-dom';
import {Bell,FileText,MessageCircle,Paperclip,Send,X} from 'lucide-react';
import {safeRequest} from './safe-request';
import type {Data} from './types';
type Row=Record<string,any>;
type Pulse={me:string;rooms:{id:string;title:string;unread:number;last_row:number;author_id:string;preview:string}[];direct:{peer:string;unread:number;last_row:number;preview:string}[];conversations:{peer:string;last_at:string}[];notifications:{id:string;title:string;entry_id:string|null}[]};
type Alert={key:string;title:string;body:string;open:()=>void};
type Message={sequence:number;id:string;from_user:string;to_user:string;body:string;created_at:string;attachment_id:string|null;attachment_name:string|null;attachment_size:number|null;attachment_type:string|null};
const fileUrl=(id:string,inline=false)=>`/api/direct-file?id=${encodeURIComponent(id)}${inline?'&inline=1':''}`;
const sizeText=(n:number)=>n<1024*1024?`${Math.max(1,Math.round(n/1024))}KB`:`${(n/1048576).toFixed(1)}MB`;
const accept='.png,.jpg,.jpeg,.gif,.webp,.pdf,.txt,.md,.csv,.json,.docx,.xlsx,.pptx,.zip';
// 1:1 conversations and chat/contact alerts (self-host API, features.mjs → contacts.mjs).
async function call<T=Row>(params:Row,post=false):Promise<T>{const r=await safeRequest(post?'/api/features':`/api/features?${new URLSearchParams(params)}`,{method:post?'POST':'GET',headers:post?{'Content-Type':'application/json'}:undefined,body:post?JSON.stringify(params):undefined});const result=await r.json() as Row;if(!r.ok)throw new Error(result.error??'요청 실패');return result as T}
const clock=(iso:string)=>new Intl.DateTimeFormat('ko-KR',{timeZone:'Asia/Seoul',month:'numeric',day:'numeric',hour:'2-digit',minute:'2-digit'}).format(new Date(iso));
const canNotify=()=>typeof window!=='undefined'&&'Notification' in window;

/**
 * Sidebar button for 1:1 conversations, and the watcher that announces what arrives for me: new chat-room messages,
 * 1:1 messages and notifications. It polls every few seconds (slower while the tab is hidden) and shows a card at the
 * bottom right; while the tab is hidden it also shows a browser notification if the person allowed them.
 */
export default function Contacts({workspace,data,selected,onOpen}:{workspace:string;data:Data|null;selected:string|null;onOpen:(entry:string)=>void}){
 const [pulse,setPulse]=useState<Pulse|null>(null),[open,setOpen]=useState(false),[peer,setPeer]=useState<string|null>(null),[alerts,setAlerts]=useState<Alert[]>([]);
 const [permission,setPermission]=useState<NotificationPermission|'unsupported'>(()=>canNotify()?Notification.permission:'unsupported');
 const seen=useRef<Map<string,number>|null>(null),view=useRef({open,peer,selected});view.current={open,peer,selected};
 const name=useCallback((id:string)=>data?.members.find(m=>m.user_id===id)?.name??data?.people?.find(p=>p.user_id===id)?.name??'팀원',[data]);
 const openPeer=useCallback((id:string)=>{setOpen(true);setPeer(id)},[]);
 // Read through refs so the watcher keeps one steady timer while the space data reloads around it.
 const nameRef=useRef(name),openRef=useRef(onOpen),refresh=useRef(()=>{});nameRef.current=name;openRef.current=onOpen;

 // What changed since the last look becomes an alert; the first look after opening a space only sets the baseline.
 const announce=useCallback((next:Pulse)=>{
  const before=seen.current,after=new Map<string,number>(),fresh:Alert[]=[];
  const visible=document.visibilityState==='visible';
  for(const r of next.rooms){const key='room:'+r.id;after.set(key,r.last_row);if(before&&(before.get(key)??0)<r.last_row&&!(visible&&view.current.selected===r.id))fresh.push({key,title:`대화방 · ${r.title}`,body:`${nameRef.current(r.author_id)}: ${r.preview||'(첨부)'}`,open:()=>openRef.current(r.id)})}
  for(const d of next.direct){const key='dm:'+d.peer;after.set(key,d.last_row);if(before&&(before.get(key)??0)<d.last_row&&!(visible&&view.current.open&&view.current.peer===d.peer))fresh.push({key,title:`1:1 대화 · ${nameRef.current(d.peer)}`,body:d.preview,open:()=>openPeer(d.peer)})}
  // A chat message also notifies the room's author; that is the same news as the room card, so it is not shown twice.
  const roomCards=new Set(fresh.filter(f=>f.key.startsWith('room:')).map(f=>f.key.slice(5)));
  for(const n of next.notifications){const key='notice:'+n.id;after.set(key,1);if(before&&!before.has(key)&&!(n.entry_id&&(roomCards.has(n.entry_id)||next.rooms.some(r=>r.id===n.entry_id)&&visible&&view.current.selected===n.entry_id)))fresh.push({key,title:'새 알림',body:n.title,open:()=>{if(n.entry_id)openRef.current(n.entry_id)}})}
  // Keys that were read stay known, so reading a room and getting a new message later still alerts once.
  if(before)for(const [key,value] of before)if(!after.has(key))after.set(key,value);
  seen.current=after;
  if(!fresh.length)return;
  setAlerts(old=>[...fresh,...old.filter(a=>!fresh.some(f=>f.key===a.key))].slice(0,3));
  if(!visible&&canNotify()&&Notification.permission==='granted')for(const a of fresh){const note=new Notification(a.title,{body:a.body,tag:a.key});note.onclick=()=>{window.focus();a.open();note.close()}}
 },[openPeer]);

 useEffect(()=>{seen.current=null;setPulse(null);setPeer(null);setAlerts([])},[workspace]);
 useEffect(()=>{if(!workspace)return;let stop=false,timer:ReturnType<typeof setTimeout>;
  const tick=async()=>{try{const next=await call<Pulse>({action:'pulse',workspace});if(stop)return;setPulse(next);announce(next)}catch{/* offline or signed out: try again on the next tick */}
   if(!stop)timer=setTimeout(tick,document.visibilityState==='visible'?6000:15000)};
  refresh.current=()=>{clearTimeout(timer);void tick()};
  void tick();const wake=()=>{if(document.visibilityState==='visible'){clearTimeout(timer);void tick()}};document.addEventListener('visibilitychange',wake);
  return()=>{stop=true;clearTimeout(timer);document.removeEventListener('visibilitychange',wake)}},[workspace,announce]);
 useEffect(()=>{if(!alerts.length)return;const handle=setTimeout(()=>setAlerts(old=>old.slice(0,-1)),9000);return()=>clearTimeout(handle)},[alerts]);

 const unread=pulse?.direct.reduce((n,d)=>n+d.unread,0)??0;
 const readDone=useCallback(()=>refresh.current(),[]);
 const askPermission=()=>{if(canNotify())void Notification.requestPermission().then(setPermission)};
 // The panel and the cards are portalled to the page body so the sidebar's styles never reach them. While the panel is
 // open the cards sit inside it, above the conversation, instead of covering the message box.
 const cards=alerts.length>0&&<div className={open?'contact-alerts inline':'contact-alerts'} role="status" aria-live="polite">{alerts.map(a=><div className="contact-alert" key={a.key}>
   <button className="contact-alert-body" onClick={()=>{a.open();setAlerts(old=>old.filter(x=>x.key!==a.key))}}><strong>{a.title}</strong><span>{a.body}</span></button>
   <button className="icon" aria-label="알림 닫기" onClick={()=>setAlerts(old=>old.filter(x=>x.key!==a.key))}><X size={14}/></button></div>)}</div>;
 const portal=(node:ReactNode)=>typeof document!=='undefined'?createPortal(node,document.body):null;
 return <>
  <button onClick={()=>setOpen(true)} className="contacts-button"><MessageCircle size={17}/>1:1 대화{unread>0&&<b className="contacts-badge">{unread>99?'99+':unread}</b>}</button>
  {open&&portal(<ContactPanel workspace={workspace} data={data} pulse={pulse} peer={peer} name={name} permission={permission} cards={cards} onPermission={askPermission} onPeer={setPeer} onRead={readDone} onClose={()=>{setOpen(false);setPeer(null)}}/>)}
  {!open&&cards&&portal(cards)}
 </>;
}

function ContactPanel({workspace,data,pulse,peer,name,permission,cards,onPermission,onPeer,onRead,onClose}:{workspace:string;data:Data|null;pulse:Pulse|null;peer:string|null;name:(id:string)=>string;permission:NotificationPermission|'unsupported';cards:ReactNode;onPermission:()=>void;onPeer:(id:string)=>void;onRead:()=>void;onClose:()=>void}){
 const [messages,setMessages]=useState<Message[]>([]),[peerRead,setPeerRead]=useState(0),[body,setBody]=useState(''),[error,setError]=useState(''),[sending,setSending]=useState(false);
 const bottom=useRef<HTMLDivElement>(null),readSent=useRef(0);
 const me=pulse?.me??'',lastAt=new Map(pulse?.conversations.map(c=>[c.peer,c.last_at])??[]),unread=new Map(pulse?.direct.map(d=>[d.peer,d.unread])??[]);
 // Current teammates, people with unread messages first, then the most recent conversations.
 const people=(data?.members??[]).filter(m=>m.user_id!==me).sort((a,b)=>(unread.get(b.user_id)??0)-(unread.get(a.user_id)??0)||(lastAt.get(b.user_id)??'').localeCompare(lastAt.get(a.user_id)??'')||a.name.localeCompare(b.name,'ko'));
 const load=useCallback(async()=>{if(!peer)return;try{const result=await call<{messages:Message[];read:number}>({action:'dmThread',workspace,peer});setMessages(result.messages);setPeerRead(result.read);setError('');
   const last=result.messages.filter(m=>m.from_user===peer).at(-1)?.sequence??0;
   if(last>readSent.current&&document.visibilityState==='visible'){readSent.current=last;void call({action:'dmRead',workspace,peer,sequence:last},true).then(onRead).catch(()=>{})}}catch(e){setError((e as Error).message)}},[workspace,peer,onRead]);
 useEffect(()=>{setMessages([]);setBody('');readSent.current=0;if(!peer)return;void load();const timer=setInterval(()=>{if(document.visibilityState==='visible')void load()},4000);return()=>clearInterval(timer)},[peer,load]);
 useEffect(()=>{bottom.current?.scrollIntoView({block:'nearest'})},[messages.length]);
 useEffect(()=>{const key=(e:KeyboardEvent)=>{if(e.key==='Escape')onClose()};window.addEventListener('keydown',key);return()=>window.removeEventListener('keydown',key)},[onClose]);
 async function attach(files:File[]){if(!peer||sending||!files.length)return;setSending(true);setError('');
  try{for(const [i,file] of files.entries()){if(file.size>10*1024*1024)throw new Error(`${file.name}: 10MB 이하의 파일만 보낼 수 있습니다.`);
    const form=new FormData();form.set('workspace',workspace);form.set('peer',peer);form.set('file',file);if(i===0&&body.trim())form.set('body',body.trim());
    const r=await safeRequest('/api/direct-file',{method:'POST',body:form});const result=await r.json().catch(()=>({})) as Row;if(!r.ok)throw new Error(result.error??'보내지 못했습니다.');if(i===0)setBody('')}
   await load()}catch(e){setError((e as Error).message)}finally{setSending(false)}}
 function paste(e:ClipboardEvent){const files=Array.from(e.clipboardData.files).filter(f=>f.type.startsWith('image/'));if(!files.length)return;e.preventDefault();
  void attach(files.map((f,i)=>f.name&&f.name!=='image.png'?f:new File([f],`붙여넣은 사진 ${new Date().toTimeString().slice(0,8).replace(/:/g,'')}${i?'-'+i:''}.png`,{type:f.type})))}
 async function send(){const text=body.trim();if(!text||!peer||sending)return;setSending(true);try{await call({action:'dmSend',workspace,peer,body:text},true);setBody('');await load()}catch(e){setError((e as Error).message)}finally{setSending(false)}}
 return <aside className="contact-panel" aria-label="1:1 대화">
  <header><h2>1:1 대화</h2>
   {permission==='default'&&<button className="secondary" onClick={onPermission}><Bell size={13}/>브라우저 알림 켜기</button>}
   {permission==='granted'&&<span className="contact-note">창을 내려 둬도 알림이 뜹니다</span>}
   {permission==='denied'&&<span className="contact-note">브라우저 설정에서 이 주소의 알림을 허용하면 창 밖으로도 알려 드립니다</span>}
   <button className="icon" aria-label="닫기" onClick={onClose}><X size={16}/></button></header>
  {cards}
  <div className="contact-body">
   <nav className="contact-people">{people.length?people.map(m=><button key={m.user_id} className={peer===m.user_id?'active':''} onClick={()=>onPeer(m.user_id)}>
    <span className="avatar small">{m.name.slice(0,1)}</span><span className="contact-name">{m.name}<small>{lastAt.get(m.user_id)?clock(lastAt.get(m.user_id)!):'대화 없음'}</small></span>{(unread.get(m.user_id)??0)>0&&<b className="contacts-badge">{unread.get(m.user_id)}</b>}</button>)
    :<p className="contact-empty">이 공간에 다른 팀원이 없습니다. 팀원을 초대하면 1:1로 이야기할 수 있어요.</p>}</nav>
   <section className="contact-thread">{!peer?<p className="contact-empty">왼쪽에서 대화할 사람을 고르세요. 1:1 대화는 두 사람에게만 보이며 기록 목록·검색·내보내기에 나오지 않습니다.</p>:<>
    <header><strong>{name(peer)}</strong></header>
    <div className="contact-messages">{messages.length?messages.map(m=><div key={m.id} className={m.from_user===me?'mine':''}>{m.attachment_id&&(m.attachment_type?.startsWith('image/')
     ?<a className="contact-photo" href={fileUrl(m.attachment_id,true)} target="_blank" rel="noreferrer"><img src={fileUrl(m.attachment_id,true)} alt={m.attachment_name??'사진'} loading="lazy"/></a>
     :<a className="contact-file" href={fileUrl(m.attachment_id)} download={m.attachment_name??undefined}><FileText size={18}/><span>{m.attachment_name}<small>{sizeText(m.attachment_size??0)}</small></span></a>)}{m.body&&<p>{m.body}</p>}<small>{clock(m.created_at)}{m.from_user===me&&m.sequence<=peerRead?' · 읽음':''}</small></div>):<p className="contact-empty">첫 메시지를 보내 보세요.</p>}<div ref={bottom}/></div>
    {error&&<p className="contact-error" role="alert">{error}</p>}
    <form onSubmit={e=>{e.preventDefault();void send()}} onDragOver={e=>e.preventDefault()} onDrop={e=>{e.preventDefault();void attach(Array.from(e.dataTransfer.files))}}>
     <label className="contact-attach" title="사진·파일 보내기 (10MB까지)"><Paperclip size={16}/><input type="file" hidden multiple accept={accept} disabled={sending} onChange={e=>{void attach(Array.from(e.target.files??[]));e.target.value=''}}/></label>
     <textarea onPaste={paste} aria-label="메시지" placeholder="메시지를 입력하세요… (Enter 보내기, Shift+Enter 줄바꿈, 사진은 붙여넣기·끌어 놓기)" value={body} maxLength={5000} onChange={e=>setBody(e.target.value)} onKeyDown={e=>{if(e.key==='Enter'&&!e.shiftKey&&!e.nativeEvent.isComposing){e.preventDefault();void send()}}}/><button disabled={!body.trim()||sending} aria-label="보내기"><Send size={15}/></button></form>
   </>}</section>
  </div>
 </aside>;
}

"use client";
import {useCallback,useEffect,useState} from "react";
import {randomId} from './browser-crypto';
import {Users,WifiOff,RefreshCw,ShieldCheck,Check,X,Clock} from "lucide-react";
export type Person={user_id:string;name:string;seen_at:number};
export function usePresence(workspace:string,entry:string|null,editing:boolean,enabled:boolean){
 const [people,setPeople]=useState<{online:Person[];editing:Person[]}>({online:[],editing:[]}),[connected,setConnected]=useState<boolean|null>(null);
 useEffect(()=>{
  setPeople({online:[],editing:[]});setConnected(null);if(!workspace||!enabled)return;
  const session=randomId();let live=true,running=false;
  const body=(mode:string)=>JSON.stringify({workspace,session,entry,mode});
  const leave=()=>{void fetch("/api/presence",{method:"POST",headers:{"Content-Type":"application/json"},body:body("offline"),keepalive:true}).catch(()=>{})};
  const pulse=async()=>{if(!live||running||document.visibilityState!=="visible"||!navigator.onLine)return;running=true;try{
   const sent=await fetch("/api/presence",{method:"POST",headers:{"Content-Type":"application/json"},body:body(editing?"edit":"view")});if(!sent.ok)throw new Error();
   const response=await fetch(`/api/presence?${new URLSearchParams({workspace,...(entry?{entry}:{})})}`,{cache:"no-store"});if(!response.ok)throw new Error();const value=await response.json() as {online:Person[];editing:Person[]};if(live){setPeople(value);setConnected(true)}
  }catch{if(live)setConnected(false)}finally{running=false}};
  const visibility=()=>{if(document.visibilityState==="visible")void pulse();else leave()};const offline=()=>setConnected(false);
  void pulse();const timer=setInterval(()=>void pulse(),15000);window.addEventListener("pagehide",leave);window.addEventListener("online",pulse);window.addEventListener("offline",offline);document.addEventListener("visibilitychange",visibility);
  return()=>{live=false;clearInterval(timer);window.removeEventListener("pagehide",leave);window.removeEventListener("online",pulse);window.removeEventListener("offline",offline);document.removeEventListener("visibilitychange",visibility);leave()};
 },[workspace,entry,editing,enabled]);
 return {...people,connected};
}
export function PresenceStatus({people,connected}:{people:Person[];connected:boolean|null}){
 if(connected===false)return <div className="collaboration-status disconnected" role="status"><WifiOff size={14}/>연결 확인 중 · 작성 내용은 이 브라우저에 임시 보관됩니다</div>;
 return <div className="collaboration-status" role="status"><Users size={14}/><span>{connected===null?"연결 확인 중":`${people.length}명 접속 중`}</span><div className="presence-avatars">{people.slice(0,5).map(p=><span key={p.user_id} title={p.name}>{p.name.slice(0,1)}</span>)}</div></div>;
}
export function EditingNotice({people,user}:{people:Person[];user:string}){const others=people.filter(p=>p.user_id!==user);return others.length?<div className="editing-notice" role="status"><Users size={15}/>{others.map(p=>p.name).join(", ")}님도 이 기록을 수정하고 있습니다. 저장 전 변경 내용을 확인하세요.</div>:null}
type RequestInfo={id:string;workspace_id?:string;user_id?:string;name:string;email?:string;requested_role?:string;status:string;created_at:string;reviewed_at?:string;expires_at?:string;revoked_at?:string|null};
async function api(url:string,input?:Record<string,unknown>){const response=await fetch(url,{cache:"no-store",...(input?{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(input)}:{})});let data:{error?:string;requests:RequestInfo[]};try{data=await response.json() as typeof data}catch{throw new Error("로그인 상태와 서버 연결을 확인하세요.")}if(!response.ok)throw new Error(data.error??"요청을 처리하지 못했습니다.");return data}
export function AccessReview({workspace,onApproved}:{workspace:string;onApproved:()=>void}){
 const [requests,setRequests]=useState<RequestInfo[]>([]),[error,setError]=useState(""),[busy,setBusy]=useState("");
 const load=useCallback(async()=>{try{const value=await api(`/api/access?workspace=${encodeURIComponent(workspace)}`);setRequests(value.requests);setError("")}catch(e){setError((e as Error).message)}},[workspace]);
 useEffect(()=>{void load();const timer=setInterval(()=>{if(document.visibilityState==="visible")void load()},15000);return()=>clearInterval(timer)},[load]);
 async function review(item:RequestInfo,action:string,role:string){if(busy)return;setBusy(item.id);setError("");try{await api("/api/access",{workspace,id:item.id,action,role});await load();onApproved()}catch(e){setError((e as Error).message)}finally{setBusy("")}}
 const pending=requests.filter(r=>r.status==="pending");
 return <section className="panel access-review"><header className="section-header"><h2><ShieldCheck size={17}/>참여 요청 <span>{pending.length}</span></h2><button className="icon" aria-label="참여 요청 새로고침" onClick={()=>void load()}><RefreshCw size={15}/></button></header><p className="access-help">승인한 계정만 이 공간의 기록을 열 수 있습니다. 사이트 공유 권한은 사이트 공유 설정에서 별도로 관리합니다.</p>{error&&<div className="error-banner">{error}</div>}{pending.map(item=><form className="access-row" key={item.id} onSubmit={e=>{e.preventDefault();void review(item,"approve",String(new FormData(e.currentTarget).get("role")))}}><span className="avatar small">{item.name.slice(0,1)}</span><div><strong>{item.name}</strong><small>{item.email}</small><small>{item.revoked_at?"초대 취소됨":item.expires_at&&item.expires_at<new Date().toISOString()?"초대 만료됨":"승인 대기"}</small></div><select name="role" aria-label={`${item.name}에게 부여할 권한`} defaultValue={item.requested_role}><option value="member">팀원</option><option value="viewer">읽기 전용</option><option value="admin">관리자</option></select><button className="primary" disabled={!!busy||!!item.revoked_at||!!item.expires_at&&item.expires_at<new Date().toISOString()}><Check size={14}/>승인</button><button type="button" className="secondary" disabled={!!busy} onClick={()=>void review(item,"reject","viewer")}><X size={14}/>거절</button></form>)}{!pending.length&&<p className="access-empty">검토할 참여 요청이 없습니다.</p>}<details className="access-history"><summary>처리한 요청 {requests.length-pending.length}건</summary>{requests.filter(r=>r.status!=="pending").map(r=><p key={r.id}><strong>{r.name}</strong><span>{r.status==="approved"?"승인":"거절"}</span><time>{r.reviewed_at?new Date(r.reviewed_at).toLocaleString("ko-KR",{timeZone:"Asia/Seoul"}):""}</time></p>)}</details></section>;
}
export function MyAccessRequests({onOpen}:{onOpen:(workspace:string)=>void}){
 const [requests,setRequests]=useState<RequestInfo[]>([]);
 const load=useCallback(async()=>{try{setRequests((await api("/api/access")).requests)}catch{}},[]);
 useEffect(()=>{void load();const timer=setInterval(()=>{if(document.visibilityState==="visible")void load()},15000);window.addEventListener("daybook-saved",load);return()=>{clearInterval(timer);window.removeEventListener("daybook-saved",load)}},[load]);
 if(!requests.length)return null;
 return <details className="my-access"><summary><Clock size={14}/>내 참여 요청 {requests.filter(r=>r.status==="pending").length}건 대기</summary>{requests.map(r=><div key={r.id}><strong>{r.name}</strong><span>{r.status==="pending"?"소유자 승인 대기":r.status==="approved"?"승인됨":"거절됨"}</span>{r.status==="approved"&&r.workspace_id&&<button onClick={()=>onOpen(r.workspace_id!)}>공간 열기</button>}</div>)}</details>;
}

"use client";
import {useCallback,useEffect,useRef,useState} from "react";
import {createPortal} from "react-dom";
import {Folder,RefreshCw,Pause,Check,Clock} from "lucide-react";
import {writeSnapshot,type Directory,type Snapshot} from "./local-files";
type FolderHandle=Directory&{queryPermission(options:{mode:"readwrite"}):Promise<PermissionState>;requestPermission(options:{mode:"readwrite"}):Promise<PermissionState>};
type PickerWindow=Window&{showDirectoryPicker?:(options:{id:string;mode:"readwrite"})=>Promise<FolderHandle>};
async function handleStore(key:string,value?:FolderHandle|null):Promise<FolderHandle|null>{
 const db=await new Promise<IDBDatabase>((resolve,reject)=>{const request=indexedDB.open("daybook-local-folders",1);request.onupgradeneeded=()=>request.result.createObjectStore("folders");request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error)});
 try{return await new Promise((resolve,reject)=>{const tx=db.transaction("folders",value===undefined?"readonly":"readwrite"),store=tx.objectStore("folders"),request=value===undefined?store.get(key):value===null?store.delete(key):store.put(value,key);let result:FolderHandle|null=null;request.onsuccess=()=>{if(value===undefined)result=request.result??null;else result=value};tx.oncomplete=()=>resolve(result);tx.onerror=()=>reject(tx.error);tx.onabort=()=>reject(tx.error)})}finally{db.close()}
}
export default function LocalFilesPanel({workspace,user,allowed,target}:{workspace:string;user:string;allowed:boolean;target:HTMLElement|null}){
 const [folder,setFolder]=useState<FolderHandle|null>(null),[enabled,setEnabled]=useState(false),[busy,setBusy]=useState(false),[supported,setSupported]=useState(true),[error,setError]=useState(""),[last,setLast]=useState("");
 const scope=useRef(0),running=useRef(false),revision=useRef(""),key=`${user}:${workspace}`;
 useEffect(()=>{const current=++scope.current;setFolder(null);setEnabled(false);setBusy(false);setError("");setLast("");revision.current="";setSupported(!!(window as PickerWindow).showDirectoryPicker);if(allowed&&workspace)void handleStore(key).then(handle=>{if(scope.current===current)setFolder(handle)}).catch(()=>{if(scope.current===current)setError("폴더 기억하기를 사용할 수 없습니다. 다시 선택해 반영할 수 있습니다.")});return()=>{scope.current++}},[key,workspace,allowed]);
 const sync=useCallback(async(handle:FolderHandle,force=false)=>{
  if(running.current||!allowed)return;const current=scope.current;running.current=true;setBusy(true);
  const active=()=>current===scope.current;
  try{
   if(await handle.queryPermission({mode:"readwrite"})!=="granted")throw new Error("폴더 접근을 다시 허용하려면 ‘반영 시작’을 누르세요.");
   const response=await fetch(`/api/local-files?workspace=${encodeURIComponent(workspace)}`,{cache:"no-store",headers:!force&&revision.current?{"If-None-Match":`"${revision.current}"`}:{}});
   if(!active())return;if(response.status===304){setError("");return}
   let result:Snapshot&{error?:string};try{result=await response.json()}catch{throw new Error("로그인 상태와 서버 연결을 확인하세요.")}
   if(!response.ok)throw new Error(result.error??"기록을 읽지 못했습니다.");
   const save=async()=>{if(!active())return;await writeSnapshot(handle,result,async id=>{const file=await fetch(`/api/file?id=${encodeURIComponent(id)}`,{cache:"no-store"});if(!file.ok)throw new Error("첨부파일을 받지 못했습니다.");return file.blob()},active)};
   if(navigator.locks)await navigator.locks.request(`daybook-files:${workspace}`,save);else await save();
   if(active()){revision.current=result.revision;setLast(new Date().toLocaleTimeString("ko-KR"));setError("")}
  }catch(e){if(active()){setError((e as Error).message);setEnabled(false)}}finally{running.current=false;if(active())setBusy(false)}
 },[workspace,allowed]);
 useEffect(()=>{if(!enabled||!folder)return;void sync(folder);const timer=setInterval(()=>{if(document.visibilityState==="visible")void sync(folder)},30000);let debounce:ReturnType<typeof setTimeout>;const saved=()=>{clearTimeout(debounce);debounce=setTimeout(()=>void sync(folder),750)};window.addEventListener("daybook-saved",saved);return()=>{clearInterval(timer);clearTimeout(debounce);window.removeEventListener("daybook-saved",saved)}},[folder,enabled,sync]);
 async function select(){setError("");try{
  const picker=(window as PickerWindow).showDirectoryPicker;if(!picker)throw new Error("Chrome 또는 Edge에서 이 페이지를 직접 열어 폴더를 선택하세요.");
  const handle=await picker.call(window,{id:"daybook-repository",mode:"readwrite"});
  if(handle.name.toLowerCase()===".git")throw new Error(".git 내부 대신 저장소 최상위 폴더를 선택하세요.");
  let git=false;try{await handle.getDirectoryHandle(".git");git=true}catch{}if(!git)try{await handle.getFileHandle(".git");git=true}catch{}if(!git)throw new Error("Git으로 복제한 저장소의 최상위 폴더를 선택하세요.");
  scope.current++;revision.current="";setFolder(handle);setEnabled(false);setLast("");
  try{await handleStore(key,handle)}catch{setError("폴더 기억하기에 실패했습니다. 이번 창에서는 반영할 수 있습니다.")}
  setEnabled(true);
 }catch(e){if((e as Error).name!=="AbortError")setError((e as Error).message)}}
 async function resume(){if(!folder)return;setError("");try{if(await folder.requestPermission({mode:"readwrite"})!=="granted")throw new Error("선택한 폴더에 저장 권한이 필요합니다.");setEnabled(true);await sync(folder,true)}catch(e){setError((e as Error).message)}}
 if(!target)return null;
 return createPortal(<section className="git-panel"><h3><Folder size={16}/>Git 변경사항으로 저장</h3><div className="git-status"><span className={enabled?"ready":"waiting"}>{enabled?<Check size={14}/>:<Clock size={14}/>}</span><div><strong>{enabled?"저장소 파일 반영 중":folder?"폴더 연결됨 · 반영 대기":"저장소 폴더를 선택하세요"}</strong><small>{folder?`${folder.name}/daybook/${workspace}/`:"이 PC에 복제해 둔 Git 저장소"}{last?` · 최근 반영 ${last}`:""}</small></div></div>
 {!supported&&<p className="git-error">Chrome 또는 Edge에서 페이지를 직접 열어 사용하세요.</p>}
 {error&&<p className="git-error" role="alert">{error}</p>}
 <div className="git-actions"><button className="secondary" disabled={!allowed||!supported||busy} onClick={()=>void select()}><Folder size={14}/>{folder?"폴더 변경":"저장소 폴더 선택"}</button>{folder&&<><button className="secondary" disabled={!allowed||busy} onClick={()=>void resume()}><RefreshCw size={14}/>{busy?"반영 중…":enabled?"지금 반영":"반영 시작"}</button>{enabled&&<button className="secondary" disabled={busy} onClick={()=>{scope.current++;setEnabled(false)}}><Pause size={14}/>일시정지</button>}</>}</div>
 <p className="git-note">이 창이 열려 있는 동안 기록·첨부자료를 파일로 반영합니다. Git 변경사항에서 검토하고 직접 커밋하세요.</p>
 </section>,target);
}

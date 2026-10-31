// This module only writes beneath the folder explicitly selected by the user.
// It never invokes Git, stages changes, creates commits, or contacts GitHub.
export type ExportFile={path:string;content?:string;attachment?:{id:string;size:number}};
export type Snapshot={workspace:string;name:string;revision:string;files:ExportFile[]};
export type Directory={name:string;getDirectoryHandle(name:string,options?:{create?:boolean}):Promise<Directory>;getFileHandle(name:string,options?:{create?:boolean}):Promise<LocalFile>};
type LocalFile={getFile():Promise<Blob>;createWritable():Promise<{write(data:Blob|string):Promise<void>;close():Promise<void>;abort():Promise<void>}>};
type Manifest={format:"daybook-local-files";workspace:string;revision:string;files:Record<string,{hash:string;source:string}>};
const encoder=new TextEncoder();
export async function digest(value:string|Blob){const bytes=typeof value==="string"?encoder.encode(value):await value.arrayBuffer();return Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256",bytes))).map(x=>x.toString(16).padStart(2,"0")).join("")}
async function fileDigest(value:Blob|string,textual:boolean){return digest(textual?(typeof value==="string"?value:await value.text()).replace(/\r\n/g,"\n"):value)}
function segments(path:string){const parts=path.split("/");if(!path||parts.some(p=>!p||p==="."||p===".."||p.toLowerCase()===".git"||/[\\\x00-\x1f:*?"<>|]/.test(p)))throw new Error("저장 경로가 올바르지 않습니다.");return parts}
async function locate(root:Directory,path:string,create:boolean){const parts=segments(path),name=parts.pop()!;let dir=root;for(const part of parts)dir=await dir.getDirectoryHandle(part,{create});return {dir,name}}
async function read(root:Directory,path:string):Promise<Blob|null>{try{const {dir,name}=await locate(root,path,false);return await(await dir.getFileHandle(name)).getFile()}catch(e){if(e instanceof Error&&e.name==="NotFoundError")return null;throw e}}
async function write(root:Directory,path:string,value:Blob|string){const {dir,name}=await locate(root,path,true),file=await dir.getFileHandle(name,{create:true}),stream=await file.createWritable();try{await stream.write(value);await stream.close()}catch(e){await stream.abort().catch(()=>{});throw e}}
export async function writeSnapshot(root:Directory,snapshot:Snapshot,fetchBinary:(id:string)=>Promise<Blob>,active=()=>true){
 if(!/^[a-zA-Z0-9_-]+$/.test(snapshot.workspace))throw new Error("공간 식별자가 올바르지 않습니다.");
 if(root.name.toLowerCase()===".git")throw new Error(".git 폴더 대신 저장소 최상위 폴더를 선택하세요.");
 const prefix=`daybook/${snapshot.workspace}/`,manifestPath=prefix+".daybook-local/state.json",ignorePath=prefix+".gitignore",ignoreContent=".daybook-local/\n",original=await read(root,manifestPath);
 let before:Manifest={format:"daybook-local-files",workspace:snapshot.workspace,revision:"",files:{}};
 if(original){try{before=JSON.parse(await original.text()) as Manifest;if(before.format!=="daybook-local-files"||before.workspace!==snapshot.workspace||!before.files)throw new Error()}catch{throw new Error("폴더 반영 상태 파일을 확인하세요. 다른 파일을 덮어쓰지 않았습니다.")}}
 const next:Manifest={format:"daybook-local-files",workspace:snapshot.workspace,revision:snapshot.revision,files:{...before.files}},seen=new Set<string>();let count=0;
 for(const item of snapshot.files){
  segments(item.path);if(!item.path.startsWith(prefix)||item.path.startsWith(prefix+".daybook-local/")||item.path===ignorePath||seen.has(item.path))throw new Error("저장 파일 경로가 중복되거나 공간 밖을 가리킵니다.");seen.add(item.path);
 }
 if(!active())throw new Error("폴더 반영이 중단되었습니다.");
 const ignore=await read(root,ignorePath);
 if(ignore&&(await ignore.text()).replace(/\r\n/g,"\n")!==ignoreContent)throw new Error("기존 기록 폴더의 .gitignore를 보존했습니다. .daybook-local/ 제외 설정을 확인하세요.");
 if(!ignore)await write(root,ignorePath,ignoreContent);
 for(const item of snapshot.files){
  if(!active())throw new Error("폴더 반영이 중단되었습니다.");
  const source=await digest(JSON.stringify(item)),known=before.files[item.path];
  // The manifest is a recovery journal. Persist each completed file so interrupted
  // writes can resume without treating our own saved content as someone else's edit.
  const checkpoint=async(hash:string)=>{next.files[item.path]={hash,source};if(!active())throw new Error("폴더 반영이 중단되었습니다.");await write(root,manifestPath,JSON.stringify(next,null,2)+"\n")};
  let content:Blob|string,hash:string;
  if(item.content!==undefined){content=item.content;hash=await fileDigest(content,true)}
  else if(item.attachment){
   const current=await read(root,item.path);
   if(known?.source===source&&current&&await digest(current)===known.hash){next.files[item.path]=known;continue}
   content=await fetchBinary(item.attachment.id);if(content.size!==item.attachment.size)throw new Error(`첨부파일을 완전히 받지 못했습니다: ${item.path}`);hash=await digest(content);
  }else throw new Error("파일 내용이 없습니다.");
  const current=await read(root,item.path),currentHash=current?await fileDigest(current,item.content!==undefined):null;
  if(currentHash===hash){if(!known||known.hash!==hash||known.source!==source)await checkpoint(hash);else next.files[item.path]=known;continue}
  if(currentHash&&(!known||currentHash!==known.hash))throw new Error(`PC에서 수정된 파일을 보존했습니다: ${item.path}`);
  if(!active())throw new Error("폴더 반영이 중단되었습니다.");
  await write(root,item.path,content);count++;await checkpoint(hash);
 }
 // Do not delete unlisted files or touch the index, HEAD, branches or .git.
 const finalText=JSON.stringify(next,null,2)+"\n";
 if(!active())throw new Error("폴더 반영이 중단되었습니다.");
 if(!original||await original.text()!==finalText)await write(root,manifestPath,finalText);
 return {written:count,revision:snapshot.revision};
}

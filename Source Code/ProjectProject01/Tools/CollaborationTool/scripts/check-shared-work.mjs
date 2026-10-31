import assert from "node:assert/strict";
import {spawnSync} from "node:child_process";
import {build} from "esbuild";
import {mkdirSync,writeFileSync} from "node:fs";
const base="http://127.0.0.1:5173",checks=[];
function check(value,name){assert.ok(value,name);checks.push(name)}
async function request(url,body,expected=200){const response=await fetch(base+url,{method:body?"POST":"GET",headers:{Cookie:"__sites_local_auth=1",...(body?{"Content-Type":"application/json"}:{})},body:body?JSON.stringify(body):undefined});const text=await response.text();let result;try{result=JSON.parse(text)}catch{result={error:text}}assert.equal(response.status,expected,JSON.stringify(result));return result}
function sql(command){const p=spawnSync(process.execPath,["--import","./scripts/sites-env.mjs","./node_modules/wrangler/bin/wrangler.js","d1","execute","DB","--local","--config","dist/server/wrangler.json","--persist-to",".wrangler/state","--command",command],{encoding:"utf8",windowsHide:true,timeout:30000});assert.equal(p.status,0,p.stderr)}
const w=(await request("/api/data",{action:"createWorkspace",name:"공동 작업 QA "+Date.now()})).workspace;
const post=(body,status=200)=>request("/api/data",{workspace:w,...body},status),pulse=(body,status=200)=>request("/api/presence",{workspace:w,session:"qa-local",mode:"view",...body},status);
try{
 check((await fetch(base+"/api/access")).status===401&&(await fetch(base+"/api/presence?workspace="+w)).status===401,"참여 신청·접속 상태 비로그인 접근 차단");
 const entry=(await post({action:"create",kind:"meeting",title:"공동 회의록",body:"한국어 공동 작업",day:"2026-10-31"})).id;
 await pulse({entry,mode:"edit"});let state=await request(`/api/presence?workspace=${w}&entry=${entry}`);check(state.online.length===1&&state.editing.length===1,"접속 및 편집 상태 표시");
 await pulse({session:"qa-another-tab",entry,mode:"edit"});check((await request(`/api/presence?workspace=${w}&entry=${entry}`)).online.length===1,"같은 계정의 여러 탭 접속을 중복 집계하지 않음");
 const at=Math.floor(Date.now()/1000),memberId=crypto.randomUUID();
 sql(`INSERT INTO members VALUES('${memberId}','${w}','qa_other','다른 편집자','other@example.invalid','member','2026-10-31'); INSERT INTO presence_sessions VALUES('${w}','qa_other','qa-other-session','${entry}','edit',${at});`);
 check((await request(`/api/presence?workspace=${w}&entry=${entry}`)).editing.some(p=>p.name==="다른 편집자"),"다른 팀원의 같은 기록 편집을 표시");
 sql(`UPDATE presence_sessions SET seen_at=${at-60} WHERE session_id='qa-other-session';`);
 check(!(await request(`/api/presence?workspace=${w}&entry=${entry}`)).editing.some(p=>p.user_id==="qa_other"),"응답 없는 접속을 45초 후 제외");
 await pulse({entry:"outside",mode:"edit"},404);check(true,"다른 공간의 기록을 편집 중으로 등록할 수 없음");
 await pulse({workspace:"outside"},403);check(true,"공간 밖 접속 정보 등록 차단");
 sql(`UPDATE members SET role='viewer' WHERE workspace_id='${w}' AND user_id='local_seedy';`);
 await pulse({entry,mode:"edit"},403);await pulse({entry,mode:"view"});check(true,"읽기 전용 계정은 열람만 표시");
 sql(`UPDATE members SET role='owner' WHERE workspace_id='${w}' AND user_id='local_seedy';`);
 const invite=(await post({action:"invite",role:"member"})).token;
 sql(`UPDATE workspaces SET owner_id='qa_owner' WHERE id='${w}'; DELETE FROM members WHERE workspace_id='${w}' AND user_id='local_seedy';`);
 const application=await request("/api/data",{action:"join",token:invite});check(application.pending&&!application.workspace,"초대를 사용해도 승인 전에는 공간 참여를 확정하지 않음");
 const again=await request("/api/access",{action:"request",token:invite});check(again.requestId===application.requestId,"동일 참여 요청 중복 생성 방지");
 await request(`/api/data?workspace=${w}`,undefined,403);await request(`/api/local-files?workspace=${w}`,undefined,403);await request(`/api/presence?workspace=${w}`,undefined,403);check(true,"승인 전 기록·파일 내보내기·접속 목록 접근 차단");
 check((await request("/api/access")).requests.some(r=>r.id===application.requestId&&r.status==="pending"),"신청자는 자기 요청의 대기 상태만 확인");
 await request("/api/access",{action:"approve",workspace:w,id:application.requestId,role:"admin"},403);check(true,"신청자가 스스로 승인·권한 상승할 수 없음");
 await request(`/api/access?workspace=${w}`,undefined,403);check(true,"신청자가 전체 신청자 목록을 읽을 수 없음");
 sql(`UPDATE access_requests SET user_id='qa_applicant' WHERE id='${application.requestId}'; UPDATE workspaces SET owner_id='local_seedy' WHERE id='${w}'; INSERT INTO members VALUES('${crypto.randomUUID()}','${w}','local_seedy','Seedy','seedy@sites.test','owner','2026-10-31');`);
 check((await request(`/api/access?workspace=${w}`)).requests.some(r=>r.id===application.requestId),"소유자에게 검토할 요청 목록 표시");
 await request("/api/access",{action:"approve",workspace:w,id:application.requestId,role:"owner"},400);check(true,"참여 승인으로 소유자 권한을 임의 부여하지 못함");
 await request("/api/access",{action:"approve",workspace:w,id:application.requestId,role:"viewer"});
 check((await request(`/api/data?workspace=${w}`)).members.some(m=>m.user_id==="qa_applicant"&&m.role==="viewer"),"소유자가 선택한 권한으로만 회원 등록");
 await request("/api/access",{action:"reject",workspace:w,id:application.requestId},409);check(true,"이미 처리된 요청은 다시 승인·거절하지 않음");
 const invite2=await post({action:"invite",role:"member"}),requestId=crypto.randomUUID();
 sql(`INSERT INTO access_requests(id,workspace_id,user_id,name,email,invite_id,requested_role,status,created_at) SELECT '${requestId}',workspace_id,'qa_race','경합 검사','race@example.invalid',id,'member','pending','2026-10-31' FROM invites WHERE token_hash!='' AND workspace_id='${w}' AND uses=0 ORDER BY created_at DESC LIMIT 1;`);void invite2;
 async function race(action){return (await fetch(base+"/api/access",{method:"POST",headers:{Cookie:"__sites_local_auth=1","Content-Type":"application/json"},body:JSON.stringify({action,workspace:w,id:requestId,role:"member"})})).status}
 const results=await Promise.all([race("approve"),race("reject")]);check(results.includes(200)&&results.includes(409),"동시 승인·거절도 한 가지 결과로만 확정");
 const exportData=await request(`/api/local-files?workspace=${w}`);check(!exportData.files.some(f=>f.path.includes("access_requests")||f.path.includes("presence_sessions")),"참여 신청·접속 상태는 Git 파일에서 제외");
 mkdirSync("outputs/checks/drafts",{recursive:true});await build({entryPoints:["app/drafts.ts"],bundle:true,platform:"node",format:"esm",outfile:"outputs/checks/drafts/drafts.js"});
 const {draftKey,parseDraft,saveDraft}=await import("../outputs/checks/drafts/drafts.js"),memory=new Map(),storage={setItem:(k,v)=>memory.set(k,v)},draft={id:"note",kind:"note",body:"임시 본문\n둘째 줄",version:3,title:"초안",summary:"직접 요약",day:"2026-10-31"};
 const key=draftKey("one",w,draft);saveDraft(storage,key,draft,["문서.xlsx"]);const recovered=parseDraft(memory.get(key));
 check(recovered.draft.body===draft.body&&recovered.draft.summary===draft.summary,"한글 초안·요약·줄바꿈 복구");check(recovered.draft.version===3,"초안의 원래 버전을 보존해 새 서버 내용 덮어쓰기 방지");
 check(draftKey("two",w,draft)!==key&&draftKey("one","another",draft)!==key,"계정·공간별 임시 초안 분리");
 check(recovered.fileNames[0]==="문서.xlsx"&&!JSON.stringify(recovered).includes("data:"),"첨부 원본·비밀값 대신 재선택용 파일 이름만 보관");
 check(parseDraft("bad json")===null&&parseDraft(JSON.stringify({format:"daybook-draft",version:2,draft}))===null,"손상되거나 지원하지 않는 초안 거절");
 writeFileSync("outputs/checks/shared-work-results.json",JSON.stringify({success:true,count:checks.length,checks,workspace:w},null,2));console.log(JSON.stringify({success:true,count:checks.length,checks}));
}catch(error){writeFileSync("outputs/checks/shared-work-results.json",JSON.stringify({success:false,error:error.stack,checks,workspace:w},null,2));console.error(error);process.exitCode=1}

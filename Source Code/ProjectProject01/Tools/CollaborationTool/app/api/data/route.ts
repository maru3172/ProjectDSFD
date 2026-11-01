import { env } from "cloudflare:workers";
import { getChatGPTUser } from "../../chatgpt-auth";
import type { Entry, Member } from "../../types";
import {summarize} from "../../summarize";
import {AccessError,requestAccess} from "../../access-service";
export const dynamic="force-dynamic";
class HttpError extends Error{constructor(public code:number,message:string){super(message)}}
const id=()=>crypto.randomUUID();
const now=()=>new Date().toISOString();
function fail(code:number,message:string):never{throw new HttpError(code,message)}
const db=()=>env.DB??fail(503,"저장소 연결을 준비하고 있습니다.");
const query=(sql:string,...args:(string|number|null)[])=>db().prepare(sql).bind(...args);
const rows=async<T=Record<string,unknown>>(sql:string,...args:(string|number|null)[])=>((await query(sql,...args).all<T>()).results??[]);
const one=async<T=Record<string,unknown>>(sql:string,...args:(string|number|null)[])=>query(sql,...args).first<T>();
const text=(v:unknown,max:number,required=false)=>{if(typeof v!=="string")return required?fail(400,"필수 입력을 확인하세요."):"";const value=v.trim();if((required&&!value)||value.length>max)fail(400,"입력 길이를 확인하세요.");return value};
const date=(v:unknown,required=false)=>{const s=text(v,10,required);if(!s)return null;if(!/^\d{4}-\d{2}-\d{2}$/.test(s)||Number.isNaN(Date.parse(s+"T00:00:00Z"))||new Date(s+"T00:00:00Z").toISOString().slice(0,10)!==s)fail(400,"날짜 형식을 확인하세요.");return s};
const member=async(w:string,u:string)=>await one<Member>("SELECT * FROM members WHERE workspace_id=? AND user_id=?",w,u)??fail(403,"이 공간에 접근할 권한이 없습니다.");
// invite_tokens exists only on the standalone server (self-host/migrations); the older cloud database has no such table.
const inviteTokens=async()=>!!await one("SELECT name FROM sqlite_master WHERE type='table' AND name='invite_tokens'");
const admin=(m:Member)=>{if(!["owner","admin"].includes(m.role))fail(403,"관리자 권한이 필요합니다.")};
const writer=(m:Member)=>{if(m.role==="viewer")fail(403,"읽기 전용 팀원입니다.")};
const hash=async(t:string)=>Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256",new TextEncoder().encode(t)))).map(x=>x.toString(16).padStart(2,"0")).join("");
const audit=(w:string,u:string,action:string,entry:string|null=null,before:unknown=null,after:unknown=null)=>query("INSERT INTO activity(id,workspace_id,entry_id,actor_id,action,before_json,after_json,created_at) VALUES(?,?,?,?,?,?,?,?)",id(),w,entry,u,action,before?JSON.stringify(before):null,after?JSON.stringify(after):null,now());
const response=(value:unknown,status=200)=>Response.json(value,{status,headers:{"Cache-Control":"no-store"}});
const errors=(e:unknown)=>{if(e instanceof HttpError)return response({error:e.message},e.code);if(e instanceof AccessError)return response({error:e.message},e.status);console.error("daybook request failed",e instanceof Error?e.message:"unknown");return response({error:"요청을 완료하지 못했습니다. 잠시 후 다시 시도하세요."},500)};
async function identity(){return await getChatGPTUser()??fail(401,"로그인이 필요합니다.")}
async function entryInput(input:Record<string,unknown>,workspace:string,before:Entry|null=null){
 const kinds=["task","meeting","note","log","decision","event","plan","goal","discussion","chat"],states=["todo","inprogress","blocked","done"],priorities=["low","normal","high"];
 const kind=text(input.kind,20,true);if(!kinds.includes(kind))fail(400,"기록 종류를 확인하세요.");
 const status=text(input.status,20)||"todo",priority=text(input.priority,20)||"normal";
 if(!states.includes(status)||!priorities.includes(priority))fail(400,"진행 상태를 확인하세요.");
 const project=text(input.project_id,80)||null,assigned=text(input.assigned_id,120)||null,parent=text(input.parent_id,80)||null,topic=text(input.topic_id,80)||null,customType=text(input.custom_type_id,80)||null;
 if(customType&&customType!==before?.custom_type_id&&!await one("SELECT id FROM record_types WHERE id=? AND workspace_id=? AND archived=0",customType,workspace))fail(400,"사용할 항목을 확인하세요.");
 if(topic&&topic!==before?.topic_id&&!await one("SELECT id FROM topics WHERE id=? AND workspace_id=? AND archived=0",topic,workspace))fail(400,"사용할 주제를 확인하세요.");
 if(project&&project!==before?.project_id&&!await one("SELECT id FROM projects WHERE id=? AND workspace_id=? AND archived=0",project,workspace))fail(400,"사용할 프로젝트를 확인하세요.");
 if(assigned&&assigned!==before?.assigned_id&&!await one("SELECT id FROM members WHERE user_id=? AND workspace_id=?",assigned,workspace))fail(400,"담당자가 팀에 없습니다.");
 if(parent&&parent!==before?.parent_id&&!await one("SELECT id FROM entries WHERE id=? AND workspace_id=? AND deleted_at IS NULL",parent,workspace))fail(400,"연결할 원본 기록이 없습니다.");
 if(parent&&before){if(parent===before.id)fail(400,"자기 자신을 원본으로 연결할 수 없습니다.");const cycle=await one("WITH RECURSIVE chain(id,parent_id) AS (SELECT id,parent_id FROM entries WHERE id=? AND workspace_id=? UNION SELECT e.id,e.parent_id FROM entries e JOIN chain c ON e.id=c.parent_id WHERE e.workspace_id=?) SELECT id FROM chain WHERE id=? LIMIT 1",parent,workspace,workspace,before.id);if(cycle)fail(400,"연결된 기록이 순환하지 않도록 원본을 바꿔주세요.")}
 return {kind,title:text(input.title,160,true),body:text(input.body,200000),summary:text(input.summary,2000)||null,project_id:project,topic_id:topic,custom_type_id:customType,assigned_id:assigned,parent_id:parent,day:date(input.day,true)!,due_day:date(input.due_day),status,priority,pinned:input.pinned?1:0};
}
export async function GET(request:Request){try{
 const user=await identity(),url=new URL(request.url),workspace=url.searchParams.get("workspace");
 if(!workspace)return response({user,workspaces:await rows("SELECT w.*,m.role FROM workspaces w JOIN members m ON m.workspace_id=w.id WHERE m.user_id=? ORDER BY w.created_at",user.userId)});
 const me=await member(workspace,user.userId);
 const workspaceInfo=await one("SELECT w.*,m.role FROM workspaces w JOIN members m ON m.workspace_id=w.id WHERE w.id=? AND m.user_id=?",workspace,user.userId);
 if(url.searchParams.get("mode")==="export"){
  admin(me);return response({format:"daybook-backup",version:1,exportedAt:now(),workspace:workspaceInfo,projects:await rows("SELECT * FROM projects WHERE workspace_id=?",workspace),topics:await rows("SELECT * FROM topics WHERE workspace_id=?",workspace),entries:await rows("SELECT * FROM entries WHERE workspace_id=?",workspace),comments:await rows("SELECT * FROM comments WHERE workspace_id=?",workspace),activity:await rows("SELECT * FROM activity WHERE workspace_id=?",workspace)});
 }
 const entryId=url.searchParams.get("entry");
 if(entryId){const entry=await one("SELECT * FROM entries WHERE id=? AND workspace_id=?",entryId,workspace)??fail(404,"기록이 없습니다.");return response({entry,comments:await rows("SELECT * FROM comments WHERE entry_id=? AND workspace_id=? ORDER BY created_at",entryId,workspace),history:await rows("SELECT * FROM activity WHERE entry_id=? AND workspace_id=? ORDER BY created_at DESC LIMIT 100",entryId,workspace),files:await rows("SELECT id,name,size,created_at FROM attachments WHERE entry_id=? AND workspace_id=?",entryId,workspace)});}
 const parts=["workspace_id=?"],params:(string|number|null)[]=[workspace];
 const search=text(url.searchParams.get("q")??"",200);if(search){parts.push("(title LIKE ? ESCAPE '\\' OR body LIKE ? ESCAPE '\\')");const pattern="%"+search.replace(/[\\%_]/g,"\\$&")+"%";params.push(pattern,pattern)}
 const from=date(url.searchParams.get("from")),to=date(url.searchParams.get("to"));
 if(!search&&from&&to){parts.push("((day BETWEEN ? AND ?) OR (due_day BETWEEN ? AND ?) OR (kind IN ('task','goal','plan','chat','discussion') AND deleted_at IS NULL))");params.push(from,to,from,to)}
 const listColumns="id,workspace_id,project_id,topic_id,custom_type_id,summary,auto_summary,kind,title,substr(body,1,300) AS body,author_id,assigned_id,day,due_day,status,priority,parent_id,pinned,created_at,updated_at,version,deleted_at";
 const results=await rows<Entry>(`SELECT ${listColumns} FROM entries WHERE ${parts.join(" AND ")} ORDER BY updated_at DESC LIMIT 1001`,...params);
 const recordTypes=await rows("SELECT * FROM record_types WHERE workspace_id=? ORDER BY created_at",workspace);
 return response({workspace:workspaceInfo,recordTypes,members:await rows("SELECT * FROM members WHERE workspace_id=? ORDER BY created_at",workspace),projects:await rows("SELECT * FROM projects WHERE workspace_id=? ORDER BY created_at",workspace),topics:await rows("SELECT * FROM topics WHERE workspace_id=? ORDER BY created_at",workspace),files:await rows("SELECT a.id,a.entry_id,a.name,a.size,a.created_at,e.title FROM attachments a JOIN entries e ON e.id=a.entry_id WHERE a.workspace_id=? AND e.deleted_at IS NULL ORDER BY a.created_at DESC LIMIT 1000",workspace),entries:results.slice(0,1000),hasMore:results.length>1000,activity:await rows("SELECT id,workspace_id,entry_id,actor_id,action,NULL AS before_json,NULL AS after_json,created_at FROM activity WHERE workspace_id=? ORDER BY created_at DESC LIMIT 100",workspace),notifications:await rows("SELECT * FROM notifications WHERE workspace_id=? AND user_id=? ORDER BY created_at DESC LIMIT 100",workspace,user.userId),invites:["owner","admin"].includes(me.role)?(await inviteTokens()
   ?await rows("SELECT i.id,i.email,i.role,i.uses,i.expires_at,i.revoked_at,CASE WHEN i.uses=0 AND i.revoked_at IS NULL AND i.expires_at>? THEN t.token END token FROM invites i LEFT JOIN invite_tokens t ON t.invite_id=i.id WHERE i.workspace_id=? ORDER BY i.created_at DESC LIMIT 30",new Date().toISOString(),workspace)
   :await rows("SELECT id,email,role,uses,expires_at,revoked_at FROM invites WHERE workspace_id=? ORDER BY created_at DESC LIMIT 30",workspace)):[]});
 }catch(e){return errors(e)}}
export async function POST(request:Request){try{
 if(request.headers.get("origin")&&request.headers.get("origin")!==new URL(request.url).origin)fail(403,"다른 사이트에서는 요청할 수 없습니다.");
 if(Number(request.headers.get("content-length")??0)>2_000_000)fail(413,"가져올 기록이 너무 큽니다.");
 const user=await identity();const parsed:unknown=await request.json().catch(()=>fail(400,"요청 형식이 올바르지 않습니다."));if(!parsed||typeof parsed!=="object"||Array.isArray(parsed))fail(400,"요청 형식을 확인하세요.");const input=parsed as Record<string,unknown>;
 const action=text(input.action,40,true),workspace=text(input.workspace,80),stamp=now();
 if(action==="createWorkspace"){
  const w=id(),name=text(input.name,60,true);if((await rows("SELECT id FROM members WHERE user_id=?",user.userId)).length>=20)fail(400,"공간은 최대 20개까지 만들 수 있습니다.");
  await db().batch([query("INSERT INTO workspaces(id,name,owner_id,created_at) VALUES(?,?,?,?)",w,name,user.userId,stamp),query("INSERT INTO members VALUES(?,?,?,?,?,?,?)",id(),w,user.userId,user.displayName,user.email,"owner",stamp)]);return response({workspace:w});
 }
 if(action==="join"){
  return response(await requestAccess(user,text(input.token,160,true)));
 }
 const me=await member(workspace,user.userId);
 if(action==="readNotifications"){await query("UPDATE notifications SET read_at=? WHERE workspace_id=? AND user_id=? AND read_at IS NULL",stamp,workspace,user.userId).run();return response({ok:true})}
 writer(me);
 if(action==="create"||action==="update"||action==="delete"||action==="restore"){
  const entryId=action==="create"?id():text(input.id,80,true);let before:Entry|null=null;
  if(action!=="create"){before=await one<Entry>("SELECT * FROM entries WHERE id=? AND workspace_id=?",entryId,workspace)??fail(404,"기록이 없습니다.");if((action==="delete"||action==="restore")&&before.author_id!==user.userId&&!["owner","admin"].includes(me.role))fail(403,"작성자나 관리자만 삭제·복원할 수 있습니다.");if(action==="update"&&before.deleted_at)fail(409,"휴지통의 기록입니다. 먼저 복원하세요.")}
  const fields=await entryInput(action==="delete"||action==="restore"?before as unknown as Record<string,unknown>:{...before,...input},workspace,before);
  const mutation=id(),deleted=action==="delete"?stamp:null,autoSummary=JSON.stringify(summarize(fields.body));
  const statements:D1PreparedStatement[]=[];
  if(action==="create")statements.push(query("INSERT INTO entries(id,workspace_id,project_id,kind,title,body,author_id,assigned_id,day,due_day,status,priority,parent_id,pinned,created_at,updated_at,version,deleted_at,mutation_id,topic_id,custom_type_id,summary,auto_summary) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,1,NULL,?,?,?,?,?)",entryId,workspace,fields.project_id,fields.kind,fields.title,fields.body,user.userId,fields.assigned_id,fields.day,fields.due_day,fields.status,fields.priority,fields.parent_id,fields.pinned,stamp,stamp,mutation,fields.topic_id,fields.custom_type_id,fields.summary,autoSummary));
  else{if(!Number.isInteger(input.version))fail(400,"기록 버전을 확인하세요.");statements.push(query("UPDATE entries SET project_id=?,topic_id=?,custom_type_id=?,kind=?,title=?,body=?,assigned_id=?,day=?,due_day=?,status=?,priority=?,parent_id=?,pinned=?,updated_at=?,version=version+1,deleted_at=?,mutation_id=?,summary=?,auto_summary=? WHERE id=? AND workspace_id=? AND version=?",fields.project_id,fields.topic_id,fields.custom_type_id,fields.kind,fields.title,fields.body,fields.assigned_id,fields.day,fields.due_day,fields.status,fields.priority,fields.parent_id,fields.pinned,stamp,deleted,mutation,fields.summary,autoSummary,entryId,workspace,input.version as number));}
  const actionName=({create:"created",update:"updated",delete:"deleted",restore:"restored"})[action];
  statements.push(query("INSERT INTO activity(id,workspace_id,entry_id,actor_id,action,before_json,after_json,created_at) SELECT ?,?,?,?,?,?,?,? FROM entries WHERE id=? AND mutation_id=?",id(),workspace,entryId,user.userId,actionName,before?JSON.stringify(before):null,JSON.stringify(fields),stamp,entryId,mutation));
  if(fields.assigned_id&&fields.assigned_id!==user.userId&&fields.assigned_id!==before?.assigned_id)statements.push(query("INSERT INTO notifications(id,workspace_id,user_id,title,entry_id,created_at) SELECT ?,?,?,?,?,? FROM entries WHERE id=? AND mutation_id=?",id(),workspace,fields.assigned_id,`새 할 일이 배정되었습니다: ${fields.title}`,entryId,stamp,entryId,mutation));
  const results=await db().batch(statements);if(!results[0].meta.changes)fail(409,"다른 팀원이 먼저 수정했습니다. 최신 내용을 확인하고 다시 저장하세요.");return response({id:entryId});
 }
 if(action==="comment"){
  const entryId=text(input.entry,80,true),body=text(input.body,5000,true),entry=await one<Entry>("SELECT * FROM entries WHERE id=? AND workspace_id=? AND deleted_at IS NULL",entryId,workspace)??fail(404,"기록이 없습니다.");
  const recipients=new Set([entry.author_id,entry.assigned_id,...(Array.isArray(input.mentions)?input.mentions.filter(x=>typeof x==="string"):[])]);
  const commands=[query("INSERT INTO comments(id,workspace_id,entry_id,author_id,body,created_at) VALUES(?,?,?,?,?,?)",id(),workspace,entryId,user.userId,body,stamp),audit(workspace,user.userId,"commented",entryId)];
  for(const recipient of recipients)if(recipient&&recipient!==user.userId&&await one("SELECT id FROM members WHERE workspace_id=? AND user_id=?",workspace,recipient))commands.push(query("INSERT INTO notifications(id,workspace_id,user_id,title,entry_id,created_at) VALUES(?,?,?,?,?,?)",id(),workspace,recipient,`${me.name}: ${entry.title}에 댓글`,entryId,stamp));
  await db().batch(commands);return response({ok:true});
 }
 if(action==="project"){
  admin(me);const projectId=text(input.id,80)||id(),name=text(input.name,60,true),color=text(input.color,10)||"#197f78";
  if(!/^#[0-9a-f]{6}$/i.test(color))fail(400,"프로젝트 색상을 확인하세요.");
  const existing=await one("SELECT id FROM projects WHERE id=?",projectId);if(existing&&!await one("SELECT id FROM projects WHERE id=? AND workspace_id=?",projectId,workspace))fail(403,"프로젝트에 접근할 수 없습니다.");
  await db().batch([query("INSERT INTO projects(id,workspace_id,name,color,archived,created_at) VALUES(?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,color=excluded.color,archived=excluded.archived",projectId,workspace,name,color,input.archived?1:0,stamp),audit(workspace,user.userId,"project",null,null,{name})]);return response({id:projectId});
 }
 if(action==="topic"||action==="recordType"){
  const table=action==="topic"?"topics":"record_types";
  const topicId=text(input.id,80)||id(),name=text(input.name,60,true),description=text(input.description,1000),color=text(input.color,10)||"#197f78";
  if(!/^#[0-9a-f]{6}$/i.test(color))fail(400,"색상을 확인하세요.");
  const old=await one<{owner_id:string;workspace_id:string}>(`SELECT * FROM ${table} WHERE id=?`,topicId);
  if(old&&(old.workspace_id!==workspace||old.owner_id!==user.userId&&!["owner","admin"].includes(me.role)))fail(403,"주제 담당자나 관리자만 수정할 수 있습니다.");
  await db().batch([query(`INSERT INTO ${table}(id,workspace_id,name,description,owner_id,color,archived,created_at) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,description=excluded.description,color=excluded.color,archived=excluded.archived`,topicId,workspace,name,description,user.userId,color,input.archived?1:0,stamp),audit(workspace,user.userId,"project",null,null,{topic:name})]);return response({id:topicId});
 }
 if(action==="renameWorkspace"){admin(me);await query("UPDATE workspaces SET name=? WHERE id=?",text(input.name,60,true),workspace).run();return response({ok:true})}
 if(action==="invite"){
  admin(me);const role=text(input.role,20)||"member";if(!["member","viewer","admin"].includes(role)||role==="admin"&&me.role!=="owner")fail(403,"부여할 수 없는 권한입니다.");
  const token=Array.from(crypto.getRandomValues(new Uint8Array(24))).map(x=>x.toString(16).padStart(2,"0")).join("");
  const email=text(input.email,200)||null;if(email&&!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email))fail(400,"이메일을 확인하세요.");
  const inviteId=id();
  await query("INSERT INTO invites(id,workspace_id,token_hash,role,email,expires_at,created_by,created_at) VALUES(?,?,?,?,?,?,?,?)",inviteId,workspace,await hash(token),role,email,new Date(Date.now()+7*86400000).toISOString(),user.userId,stamp).run();
  // The standalone server keeps the code so admins can show and copy it again until it is used, revoked or expired.
  if(await inviteTokens())await query("INSERT INTO invite_tokens(invite_id,token) VALUES(?,?)",inviteId,token).run();
  return response({token});
 }
 if(action==="revokeInvite"){admin(me);const inviteId=text(input.id,80,true);await query("UPDATE invites SET revoked_at=? WHERE id=? AND workspace_id=?",stamp,inviteId,workspace).run();if(await inviteTokens())await query("DELETE FROM invite_tokens WHERE invite_id=? AND invite_id IN (SELECT id FROM invites WHERE workspace_id=?)",inviteId,workspace).run();return response({ok:true})}
 if(action==="member"){
  admin(me);const target=text(input.userId,120,true),role=text(input.role,20,true),old=await member(workspace,target);
  if(old.role==="owner"||target===user.userId||me.role!=="owner"&&old.role==="admin")fail(403,"이 팀원의 권한은 바꿀 수 없습니다.");
  if(!["admin","member","viewer","remove"].includes(role)||role==="admin"&&me.role!=="owner")fail(403,"부여할 수 없는 권한입니다.");
  await db().batch([role==="remove"?query("DELETE FROM members WHERE workspace_id=? AND user_id=?",workspace,target):query("UPDATE members SET role=? WHERE workspace_id=? AND user_id=?",role,workspace,target),audit(workspace,user.userId,"member",null,{name:old.name,role:old.role},{role})]);return response({ok:true});
 }
 if(action==="import"){
  admin(me);const backup=input.backup as {format?:string;version?:number;entries?:Record<string,unknown>[]};
  if(!backup||backup.format!=="daybook-backup"||backup.version!==1||!Array.isArray(backup.entries)||backup.entries.length>500)fail(400,"지원하는 백업 파일이 아닙니다. 한 번에 기록 500개까지 가져올 수 있습니다.");
  const commands:D1PreparedStatement[]=[];const records=[];
  for(const raw of backup.entries){if(raw.deleted_at)continue;const fields=await entryInput({...raw,project_id:null,topic_id:null,custom_type_id:null,assigned_id:null,parent_id:null},workspace),entryId=id();commands.push(query("INSERT INTO entries(id,workspace_id,kind,title,body,author_id,day,due_day,status,priority,pinned,created_at,updated_at,version,mutation_id) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,1,?)",entryId,workspace,fields.kind,fields.title,fields.body,user.userId,fields.day,fields.due_day,fields.status,fields.priority,fields.pinned,stamp,stamp,id()));records.push(entryId)}
  if(commands.length){commands.push(audit(workspace,user.userId,"imported",null,null,{count:records.length}));await db().batch(commands)}return response({count:records.length});
 }
 fail(400,"지원하지 않는 작업입니다.");
 }catch(e){return errors(e)}}

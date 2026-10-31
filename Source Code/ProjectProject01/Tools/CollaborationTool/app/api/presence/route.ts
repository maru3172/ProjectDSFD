import {env} from "cloudflare:workers";
import {getChatGPTUser} from "../../chatgpt-auth";
export const dynamic="force-dynamic";
const reply=(value:unknown,status=200)=>Response.json(value,{status,headers:{"Cache-Control":"no-store"}});
export async function GET(request:Request){
 const user=await getChatGPTUser();if(!user)return reply({error:"로그인이 필요합니다."},401);if(!env.DB)return reply({error:"저장소 연결을 확인하세요."},503);
 const params=new URL(request.url).searchParams,workspace=params.get("workspace")??"",entry=params.get("entry");
 if(!await env.DB.prepare("SELECT id FROM members WHERE workspace_id=? AND user_id=?").bind(workspace,user.userId).first())return reply({error:"공간 접근 권한이 없습니다."},403);
 const base="FROM presence_sessions p JOIN members m ON m.workspace_id=p.workspace_id AND m.user_id=p.user_id WHERE p.workspace_id=? AND p.seen_at>? AND p.mode!='offline'",cutoff=Math.floor(Date.now()/1000)-45;
 const [online,editing]=await env.DB.batch([
  env.DB.prepare(`SELECT m.user_id,m.name,MAX(p.seen_at) AS seen_at ${base} GROUP BY m.user_id,m.name ORDER BY m.name LIMIT 50`).bind(workspace,cutoff),
  env.DB.prepare(`SELECT m.user_id,m.name,MAX(p.seen_at) AS seen_at ${base} AND p.entry_id=? AND p.mode='edit' GROUP BY m.user_id,m.name ORDER BY m.name LIMIT 50`).bind(workspace,cutoff,entry)
 ]);
 return reply({online:online.results,editing:editing.results});
}
export async function POST(request:Request){
 if(request.headers.get("origin")&&request.headers.get("origin")!==new URL(request.url).origin)return reply({error:"허용되지 않은 요청입니다."},403);
 const user=await getChatGPTUser();if(!user)return reply({error:"로그인이 필요합니다."},401);if(!env.DB)return reply({error:"저장소 연결을 확인하세요."},503);
 try{
  const input=await request.json() as {workspace?:string;session?:string;entry?:string|null;mode?:string};
  if(typeof input.workspace!=="string"||typeof input.session!=="string"||!/^[a-zA-Z0-9-]{1,80}$/.test(input.session)||!["view","edit","offline"].includes(input.mode??""))return reply({error:"접속 정보를 확인하세요."},400);
  const db=env.DB,me=await db.prepare("SELECT role FROM members WHERE workspace_id=? AND user_id=?").bind(input.workspace,user.userId).first<{role:string}>();if(!me)return reply({error:"공간 접근 권한이 없습니다."},403);
  if(input.mode==="edit"&&me.role==="viewer")return reply({error:"읽기 전용 계정입니다."},403);
  const entry=input.entry??null;if(entry&&(typeof entry!=="string"||!await db.prepare("SELECT id FROM entries WHERE id=? AND workspace_id=? AND deleted_at IS NULL").bind(entry,input.workspace).first()))return reply({error:"기록이 없거나 접근할 수 없습니다."},404);
  const at=Math.floor(Date.now()/1000);
  await db.batch([
   db.prepare("DELETE FROM presence_sessions WHERE workspace_id=? AND seen_at<?").bind(input.workspace,at-90),
   db.prepare("INSERT INTO presence_sessions(session_id,workspace_id,user_id,entry_id,mode,seen_at) VALUES(?,?,?,?,?,?) ON CONFLICT(session_id) DO UPDATE SET entry_id=excluded.entry_id,mode=excluded.mode,seen_at=excluded.seen_at WHERE presence_sessions.workspace_id=excluded.workspace_id AND presence_sessions.user_id=excluded.user_id").bind(input.session,input.workspace,user.userId,entry,input.mode,at)
  ]);return reply({ok:true});
 }catch{return reply({error:"접속 상태를 갱신하지 못했습니다."},400)}
}

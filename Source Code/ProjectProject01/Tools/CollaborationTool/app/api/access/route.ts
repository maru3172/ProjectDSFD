import {env} from "cloudflare:workers";
import {getChatGPTUser} from "../../chatgpt-auth";
import {AccessError,requestAccess} from "../../access-service";
export const dynamic="force-dynamic";
const reply=(value:unknown,status=200)=>Response.json(value,{status,headers:{"Cache-Control":"no-store"}});
export async function GET(request:Request){
 const user=await getChatGPTUser();if(!user)return reply({error:"로그인이 필요합니다."},401);if(!env.DB)return reply({error:"저장소 연결을 확인하세요."},503);
 const workspace=new URL(request.url).searchParams.get("workspace");
 if(!workspace)return reply({requests:(await env.DB.prepare("SELECT r.id,r.workspace_id,w.name,r.status,r.created_at,r.reviewed_at FROM access_requests r JOIN workspaces w ON w.id=r.workspace_id WHERE r.user_id=? AND (r.status!='approved' OR EXISTS(SELECT 1 FROM members m WHERE m.workspace_id=r.workspace_id AND m.user_id=r.user_id)) ORDER BY r.created_at DESC LIMIT 20").bind(user.userId).all()).results});
 const owner=await env.DB.prepare("SELECT id FROM workspaces WHERE id=? AND owner_id=?").bind(workspace,user.userId).first();if(!owner)return reply({error:"공간 소유자만 참여 요청을 검토할 수 있습니다."},403);
 return reply({requests:(await env.DB.prepare("SELECT r.id,r.user_id,r.name,r.email,r.requested_role,r.status,r.created_at,r.reviewed_at,i.revoked_at,i.expires_at FROM access_requests r JOIN invites i ON i.id=r.invite_id WHERE r.workspace_id=? ORDER BY CASE WHEN r.status='pending' THEN 0 ELSE 1 END,r.created_at DESC LIMIT 100").bind(workspace).all()).results});
}
export async function POST(request:Request){
 if(request.headers.get("origin")&&request.headers.get("origin")!==new URL(request.url).origin)return reply({error:"허용되지 않은 요청입니다."},403);
 const user=await getChatGPTUser();if(!user)return reply({error:"로그인이 필요합니다."},401);if(!env.DB)return reply({error:"저장소 연결을 확인하세요."},503);
 try{
  const input=await request.json() as Record<string,unknown>;
  if(input.action==="request"){if(typeof input.token!=="string"||input.token.length>160||!input.token.trim())return reply({error:"초대 코드를 확인하세요."},400);return reply(await requestAccess(user,input.token.trim()))}
  if(!["approve","reject"].includes(String(input.action))||typeof input.workspace!=="string"||typeof input.id!=="string")return reply({error:"요청을 확인하세요."},400);
  const db=env.DB,owner=await db.prepare("SELECT id FROM workspaces WHERE id=? AND owner_id=?").bind(input.workspace,user.userId).first();if(!owner)return reply({error:"공간 소유자만 참여 요청을 검토할 수 있습니다."},403);
  const role=String(input.role??"member");if(input.action==="approve"&&!["member","viewer","admin"].includes(role))return reply({error:"부여할 권한을 확인하세요."},400);
  const stamp=new Date().toISOString(),decision=crypto.randomUUID(),approved=input.action==="approve",status=approved?"approved":"rejected";
  const statements=[db.prepare(`UPDATE access_requests SET status=?,reviewed_at=?,reviewed_by=?,decision_token=? WHERE id=? AND workspace_id=? AND status='pending' ${approved?"AND EXISTS(SELECT 1 FROM invites i WHERE i.id=access_requests.invite_id AND i.revoked_at IS NULL AND i.expires_at>?)":""}`).bind(status,stamp,user.userId,decision,input.id,input.workspace,...(approved?[stamp]:[]))];
  if(approved)statements.push(db.prepare("INSERT OR IGNORE INTO members(id,workspace_id,user_id,name,email,role,created_at) SELECT ?,workspace_id,user_id,name,email,?,? FROM access_requests WHERE id=? AND workspace_id=? AND decision_token=? AND status='approved'").bind(crypto.randomUUID(),role,stamp,input.id,input.workspace,decision));
  statements.push(db.prepare("INSERT INTO activity(id,workspace_id,actor_id,action,after_json,created_at) SELECT ?,workspace_id,?,?,json_object('name',name,'role',?),? FROM access_requests WHERE id=? AND workspace_id=? AND decision_token=?").bind(crypto.randomUUID(),user.userId,approved?"access_approved":"access_rejected",approved?role:"none",stamp,input.id,input.workspace,decision));
  const results=await db.batch(statements);if(!results[0].meta.changes)return reply({error:"이미 처리했거나 초대가 만료·취소된 요청입니다."},409);
  return reply({ok:true});
 }catch(e){if(e instanceof AccessError)return reply({error:e.message},e.status);console.error("access review failed",e instanceof Error?e.message:"unknown");return reply({error:"참여 요청을 처리하지 못했습니다."},500)}
}

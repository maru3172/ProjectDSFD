import {env} from "cloudflare:workers";
type User={userId:string;displayName:string;email:string};
export class AccessError extends Error{constructor(public status:number,message:string){super(message)}}
export async function requestAccess(user:User,token:string){
 if(!env.DB)throw new AccessError(503,"저장소 연결을 확인하세요.");
 const digest=Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256",new TextEncoder().encode(token)))).map(x=>x.toString(16).padStart(2,"0")).join("");
 const db=env.DB,stamp=new Date().toISOString(),invitation=await db.prepare("SELECT * FROM invites WHERE token_hash=?").bind(digest).first<{id:string;workspace_id:string;role:string;email:string|null;expires_at:string;uses:number;revoked_at:string|null;claimed_by:string|null}>();
 if(!invitation||invitation.revoked_at||invitation.expires_at<stamp)throw new AccessError(400,"유효한 초대 코드가 아닙니다.");
 if(invitation.email&&invitation.email.toLowerCase()!==user.email.toLowerCase())throw new AccessError(403,"이 초대는 다른 이메일을 위한 초대입니다.");
 if(await db.prepare("SELECT id FROM members WHERE workspace_id=? AND user_id=?").bind(invitation.workspace_id,user.userId).first())return {workspace:invitation.workspace_id,pending:false};
 const previous=await db.prepare("SELECT id,status,invite_id FROM access_requests WHERE workspace_id=? AND user_id=?").bind(invitation.workspace_id,user.userId).first<{id:string;status:string;invite_id:string}>();
 if(invitation.uses&&invitation.claimed_by!==user.userId)throw new AccessError(400,"이미 사용된 초대 코드입니다.");
 if(previous?.status==="pending")return {pending:true,requestId:previous.id};
 if(invitation.uses)throw new AccessError(400,"새 초대 코드를 받아 다시 요청하세요.");
 const id=crypto.randomUUID();
 const results=await db.batch([
  db.prepare("UPDATE invites SET uses=1,claimed_by=? WHERE id=? AND uses=0 AND revoked_at IS NULL AND expires_at>?").bind(user.userId,invitation.id,stamp),
  db.prepare("INSERT INTO access_requests(id,workspace_id,user_id,name,email,invite_id,requested_role,status,created_at) SELECT ?,workspace_id,?,?,?,?,role,'pending',? FROM invites WHERE id=? AND claimed_by=? AND revoked_at IS NULL ON CONFLICT(workspace_id,user_id) DO UPDATE SET id=excluded.id,name=excluded.name,email=excluded.email,invite_id=excluded.invite_id,requested_role=excluded.requested_role,status='pending',created_at=excluded.created_at,reviewed_at=NULL,reviewed_by=NULL,decision_token=NULL WHERE access_requests.status!='pending'").bind(id,user.userId,user.displayName,user.email,invitation.id,stamp,invitation.id,user.userId),
  db.prepare("INSERT INTO notifications(id,workspace_id,user_id,title,created_at) SELECT ?,w.id,w.owner_id,?,? FROM workspaces w JOIN access_requests r ON r.workspace_id=w.id WHERE r.id=?").bind(crypto.randomUUID(),`${user.displayName}님의 팀 참여 요청`,stamp,id)
 ]);
 if(!results[0].meta.changes){const current=await db.prepare("SELECT id FROM access_requests WHERE workspace_id=? AND user_id=? AND status='pending'").bind(invitation.workspace_id,user.userId).first<{id:string}>();if(current)return {pending:true,requestId:current.id};throw new AccessError(409,"이 초대는 이미 사용되었습니다.")}
 return {pending:true,requestId:id};
}

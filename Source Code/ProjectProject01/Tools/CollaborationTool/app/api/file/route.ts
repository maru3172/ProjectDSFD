import { env } from "cloudflare:workers";
import { getChatGPTUser } from "../../chatgpt-auth";
export const dynamic="force-dynamic";
const reply=(error:string,status:number)=>Response.json({error},{status,headers:{"Cache-Control":"no-store"}});
export async function POST(request:Request){
 const user=await getChatGPTUser();if(!user)return reply("로그인이 필요합니다.",401);
 if(request.headers.get("origin")&&request.headers.get("origin")!==new URL(request.url).origin)return reply("허용되지 않은 요청입니다.",403);
 if(!env.DB||!env.BUCKET)return reply("파일 저장소를 준비하고 있습니다.",503);
 if(Number(request.headers.get("content-length")??0)>11*1024*1024)return reply("파일은 10MB 이하로 올려주세요.",413);
 try{
  const form=await request.formData(),file=form.get("file"),entryId=String(form.get("entry")??"");
  if(!(file instanceof File)||file.size>10*1024*1024||!file.size)return reply("10MB 이하의 파일을 선택하세요.",400);
  const entry=await env.DB.prepare("SELECT * FROM entries WHERE id=? AND deleted_at IS NULL").bind(entryId).first<{workspace_id:string;title:string;author_id:string;assigned_id:string|null}>();if(!entry)return reply("기록을 찾지 못했습니다.",404);
  const member=await env.DB.prepare("SELECT role,name FROM members WHERE workspace_id=? AND user_id=?").bind(entry.workspace_id,user.userId).first<{role:string;name:string}>();if(!member||member.role==="viewer")return reply("파일을 올릴 권한이 없습니다.",403);
  const name=file.name.replace(/[\x00-\x1f/\\]/g,"_").slice(0,180)||"attachment",ext=name.split('.').pop()?.toLowerCase();
  if(!ext||!["png","jpg","jpeg","gif","webp","pdf","txt","md","csv","json","docx","xlsx","pptx","zip"].includes(ext))return reply("사진·PDF·문서·ZIP 파일을 올릴 수 있습니다.",400);
  const bytes=new Uint8Array(await file.arrayBuffer());let contentType="application/octet-stream";
  if(bytes[0]===137&&bytes[1]===80&&bytes[2]===78&&bytes[3]===71)contentType="image/png";
  else if(bytes[0]===255&&bytes[1]===216&&bytes[2]===255)contentType="image/jpeg";
  else if(new TextDecoder().decode(bytes.slice(0,6)).match(/^GIF8[79]a$/))contentType="image/gif";
  else if(new TextDecoder().decode(bytes.slice(0,4))==="RIFF"&&new TextDecoder().decode(bytes.slice(8,12))==="WEBP")contentType="image/webp";
  else if(new TextDecoder().decode(bytes.slice(0,5))==="%PDF-")contentType="application/pdf";
  const id=crypto.randomUUID(),key=`${entry.workspace_id}/${id}`,at=new Date().toISOString();await env.BUCKET.put(key,bytes,{httpMetadata:{contentType}});
  try{
   const commands=[env.DB.prepare("INSERT INTO attachments(id,workspace_id,entry_id,name,size,key,content_type,created_at) VALUES(?,?,?,?,?,?,?,?)").bind(id,entry.workspace_id,entryId,name,file.size,key,contentType,at),env.DB.prepare("INSERT INTO activity(id,workspace_id,entry_id,actor_id,action,created_at) VALUES(?,?,?,?,?,?)").bind(crypto.randomUUID(),entry.workspace_id,entryId,user.userId,"attached",at)];
   if(form.get("comment")==="true")commands.push(env.DB.prepare("INSERT INTO comments(id,workspace_id,entry_id,author_id,body,attachment_id,created_at) VALUES(?,?,?,?,?,?,?)").bind(crypto.randomUUID(),entry.workspace_id,entryId,user.userId,name,id,at));
   for(const recipient of new Set([entry.author_id,entry.assigned_id]))if(recipient&&recipient!==user.userId)commands.push(env.DB.prepare("INSERT INTO notifications(id,workspace_id,user_id,title,entry_id,created_at) VALUES(?,?,?,?,?,?)").bind(crypto.randomUUID(),entry.workspace_id,recipient,`${member.name}님이 파일을 올렸습니다: ${name}`,entryId,at));
   await env.DB.batch(commands);
  }catch(error){await env.BUCKET.delete(key);throw error}
  return Response.json({id,name},{headers:{"Cache-Control":"no-store"}});
 }catch(error){console.error("upload failed",error instanceof Error?error.message:"unknown");return reply("파일을 올리지 못했습니다. 다시 시도하세요.",500)}
}
export async function GET(request:Request){
 const user=await getChatGPTUser();if(!user)return reply("로그인이 필요합니다.",401);if(!env.DB||!env.BUCKET)return reply("저장소를 준비하고 있습니다.",503);
 const url=new URL(request.url),id=url.searchParams.get("id");
 const file=await env.DB.prepare("SELECT a.* FROM attachments a JOIN members m ON m.workspace_id=a.workspace_id AND m.user_id=? WHERE a.id=?").bind(user.userId,id).first<{key:string;content_type:string;name:string;size:number}>();if(!file)return reply("파일이 없거나 접근 권한이 없습니다.",404);
 const object=await env.BUCKET.get(file.key);if(!object)return reply("저장된 파일을 찾지 못했습니다.",404);
 const inline=url.searchParams.get("inline")==="1"&&["image/png","image/jpeg","image/gif","image/webp"].includes(file.content_type);
 return new Response(object.body,{headers:{"Content-Type":file.content_type,"Content-Length":String(file.size),"Content-Disposition":`${inline?"inline":"attachment"}; filename*=UTF-8''${encodeURIComponent(file.name)}`,"Cache-Control":"private, no-store","X-Content-Type-Options":"nosniff","Content-Security-Policy":"default-src 'none'"}});
}

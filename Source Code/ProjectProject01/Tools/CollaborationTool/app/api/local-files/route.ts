import {env} from "cloudflare:workers";
import {getChatGPTUser} from "../../chatgpt-auth";
import {exportTables} from "../../export-data";
import {digest,type ExportFile} from "../../local-files";
export const dynamic="force-dynamic";
const reply=(value:unknown,status=200)=>Response.json(value,{status,headers:{"Cache-Control":"no-store"}});
export async function GET(request:Request){
 const user=await getChatGPTUser();if(!user)return reply({error:"로그인이 필요합니다."},401);
 if(!env.DB)return reply({error:"저장소 연결을 확인하세요."},503);
 const workspace=new URL(request.url).searchParams.get("workspace")??"";
 if(!/^[a-zA-Z0-9_-]+$/.test(workspace))return reply({error:"공간을 선택하세요."},400);
 const member=await env.DB.prepare("SELECT role FROM members WHERE workspace_id=? AND user_id=?").bind(workspace,user.userId).first<{role:string}>();
 if(!member)return reply({error:"공간 접근 권한이 없습니다."},403);
 if(!["owner","admin"].includes(member.role))return reply({error:"관리자만 전체 기록을 파일로 내보낼 수 있습니다."},403);
 try{
  const tables=Object.entries(exportTables),results=await env.DB.batch(tables.map(([table,columns])=>env.DB!.prepare(`SELECT ${columns.map(c=>`"${c}"`).join(",")} FROM "${table}" WHERE ${table==="workspaces"?"id":"workspace_id"}=? ORDER BY id`).bind(workspace)));
  const files:ExportFile[]=[],prefix=`daybook/${workspace}/`;let name="";
  for(let i=0;i<tables.length;i++){
   const [table]=tables[i],rows=(results[i].results??[]) as Record<string,unknown>[];
   if(table==="workspaces")name=String(rows[0]?.name??"");
   const clean=rows.map(row=>{
    if(table!=="attachments")return row;
    const {key:storageKey,...metadata}=row;void storageKey;
    const filename=String(row.name).replace(/[^\p{L}\p{N}_. -]/gu,"_").replace(/[. ]+$/g,"")||"attachment";
    const path=`${prefix}files/${row.id}/${filename}`;
    files.push({path,attachment:{id:String(row.id),size:Number(row.size)}});
    return {...metadata,file_path:path};
   });
   files.push({path:`${prefix}data/${table}.json`,content:JSON.stringify(clean,null,2)+"\n"});
   if(table==="entries")for(const row of rows)files.push({path:`${prefix}records/${row.id}.md`,content:`# ${row.title}\n\n- 종류: ${row.kind}\n- 날짜: ${row.day}\n- 상태: ${row.status}\n- 삭제 여부: ${row.deleted_at?"휴지통":"활성"}\n\n${row.summary?String(row.summary)+"\n\n":""}${row.body}\n`});
  }
  files.sort((a,b)=>a.path.localeCompare(b.path,"en"));
  const revision=await digest(JSON.stringify(files));
  if(request.headers.get("if-none-match")===`"${revision}"`)return new Response(null,{status:304,headers:{ETag:`"${revision}"`,"Cache-Control":"no-store"}});
  return Response.json({workspace,name,revision,files},{headers:{"Cache-Control":"no-store",ETag:`"${revision}"`}});
 }catch(error){console.error("local export failed",error instanceof Error?error.message:"unknown");return reply({error:"파일 반영용 기록을 읽지 못했습니다."},500)}
}

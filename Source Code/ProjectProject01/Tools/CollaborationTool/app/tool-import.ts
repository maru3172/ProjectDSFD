import {unzipSync} from "fflate";
// Pages exported from other tools, read in the browser: a Notion export (.zip of Markdown & CSV, or HTML), an Obsidian
// or any folder of Markdown zipped up, or single .md/.html/.txt/.csv files. Each page becomes one record; pictures the
// page shows are attached to it; sub-pages stay under their parent page.
export type ToolPage={path:string;parent:string|null;title:string;body:string;images:{name:string;bytes:Uint8Array}[];source:string};
export type ToolImportResult={tool:string;pages:ToolPage[];skipped:string[]};
const MaxPages=300,MaxBody=200000,MaxImage=10*1024*1024,MaxTotal=80*1024*1024;
const imageExt=/\.(png|jpe?g|gif|webp)$/i,pageExt=/\.(md|markdown|txt|csv|html?)$/i;
// Notion adds a 32-character id to every exported file and folder name: "회의록 1a2b...e6f.md".
const notionId=/\s+[0-9a-f]{32}$/i;
const clean=(name:string)=>name.replace(/\.[^.\/]+$/,"").replace(/_all$/,"").replace(notionId,"").trim()||"제목 없음";
const decode=(bytes:Uint8Array)=>{if(bytes[0]===0xef&&bytes[1]===0xbb&&bytes[2]===0xbf)bytes=bytes.slice(3);try{return new TextDecoder("utf-8",{fatal:true}).decode(bytes)}catch{return new TextDecoder("euc-kr").decode(bytes)}};
const join=(dir:string,relative:string)=>{const parts=(dir?dir+"/":"").concat(relative).split("/"),out:string[]=[];for(const p of parts){if(p===".."){out.pop()}else if(p&&p!==".")out.push(p)}return out.join("/")};
const dirOf=(path:string)=>path.includes("/")?path.slice(0,path.lastIndexOf("/")):"";
const safeDecode=(value:string)=>{try{return decodeURIComponent(value)}catch{return value}};

/** CSV text → rows (quotes, doubled quotes and line breaks inside quotes). */
export function parseCsv(text:string){const rows:string[][]=[];let row:string[]=[],cell="",quoted=false;
 for(let i=0;i<text.length;i++){const c=text[i];
  if(quoted){if(c==='"'){if(text[i+1]==='"'){cell+='"';i++}else quoted=false}else cell+=c;continue}
  if(c==='"')quoted=true;else if(c===","){row.push(cell);cell=""}else if(c==="\n"||c==="\r"){if(c==="\r"&&text[i+1]==="\n")i++;row.push(cell);rows.push(row);row=[];cell=""}else cell+=c}
 if(cell||row.length){row.push(cell);rows.push(row)}
 return rows.filter(r=>r.some(v=>v.trim()))}
const table=(rows:string[][])=>{if(!rows.length)return "";const width=Math.min(50,Math.max(...rows.map(r=>r.length)));const line=(r:string[])=>"| "+Array.from({length:width},(_,i)=>(r[i]??"").replace(/\|/g,"\\|").replace(/\r?\n/g," ").trim()).join(" | ")+" |";return [line(rows[0]),"| "+Array(width).fill("---").join(" | ")+" |",...rows.slice(1,501).map(line)].join("\n")+(rows.length>501?`\n\n_(처음 500행만 옮겼습니다. 전체 ${rows.length-1}행)_`:"")};

/** HTML page → Markdown-like text (headings, paragraphs, lists, tables), plus the pictures it shows. */
export function htmlToText(html:string){
 const doc=new DOMParser().parseFromString(html,"text/html"),images:string[]=[],out:string[]=[];
 const title=doc.querySelector("h1.page-title")?.textContent?.trim()||doc.title.trim();
 const walk=(node:Element)=>{for(const child of Array.from(node.children)){const tag=child.tagName.toLowerCase();
  if(["script","style","head","nav"].includes(tag))continue;
  if(tag==="h1"&&child.classList.contains("page-title"))continue;
  if(/^h[1-6]$/.test(tag)){out.push("#".repeat(Math.min(3,Number(tag[1])))+" "+child.textContent!.trim());continue}
  if(tag==="img"){const src=child.getAttribute("src");if(src)images.push(src);out.push(`[사진: ${safeDecode(src?.split("/").pop()??"")}]`);continue}
  if(tag==="table"){out.push(table(Array.from(child.querySelectorAll("tr")).map(tr=>Array.from(tr.children).map(td=>td.textContent?.trim()??""))));continue}
  if(tag==="li"){const box=child.querySelector("input[type=checkbox]")??child.querySelector(".checkbox");out.push((box?(box.hasAttribute("checked")||box.classList.contains("checkbox-on")?"- [x] ":"- [ ] "):"- ")+child.textContent!.trim());continue}
  if(["p","pre","blockquote","figcaption"].includes(tag)){const imgs=child.querySelectorAll("img");imgs.forEach(img=>{const src=img.getAttribute("src");if(src){images.push(src);out.push(`[사진: ${safeDecode(src.split("/").pop()??"")}]`)}});const text=child.textContent?.trim();if(text)out.push(tag==="blockquote"?"> "+text:text);continue}
  if(tag==="hr"){out.push("---");continue}
  walk(child)}};
 walk(doc.body);
 return {title,body:out.join("\n\n"),images};
}

/** Notion/Obsidian Markdown → plain record text: links become their words, pictures are listed and attached. */
function markdownPage(text:string,path:string,files:Map<string,Uint8Array>,byName:Map<string,string>){
 const dir=dirOf(path),found:string[]=[];
 let title="",body=text.replace(/\r\n?/g,"\n");
 const heading=body.match(/^\s*# (.+)\n?/);if(heading){title=heading[1].trim();body=body.slice(heading[0].length)}
 // Pictures: ![alt](relative/path.png) and Obsidian ![[name.png]].
 body=body.replace(/!\[([^\]]*)\]\(<?([^)>]+?)>?(?:\s+"[^"]*")?\)/g,(all,alt:string,src:string)=>{if(/^[a-z]+:\/\//i.test(src))return `[사진: ${alt||src}](${src})`;const target=join(dir,safeDecode(src));if(files.has(target)){found.push(target);return `[사진: ${target.split("/").pop()}]`}return `[사진: ${alt||safeDecode(src)} (파일 없음)]`});
 body=body.replace(/!\[\[([^\]|]+)(?:\|[^\]]*)?\]\]/g,(all,name:string)=>{const target=byName.get(name.split("/").pop()!.toLowerCase());if(target&&imageExt.test(target)){found.push(target);return `[사진: ${target.split("/").pop()}]`}return `[첨부: ${name}]`});
 // Links to other exported pages keep their words; web links stay as they are.
 body=body.replace(/\[([^\]]+)\]\(([^)]+)\)/g,(all,label:string,href:string)=>/^[a-z]+:\/\//i.test(href)||href.startsWith("mailto:")?all:label.replace(notionId,""));
 body=body.replace(/\[\[([^\]|]+)(?:\|([^\]]+))?\]\]/g,(all,page:string,label?:string)=>label??page);
 return {title,body:body.trim(),images:found};
}

function readZip(bytes:Uint8Array,files:Map<string,Uint8Array>,skipped:string[],depth=0){
 let total=0,count=0;
 const archive=unzipSync(bytes,{filter:f=>{if(++count>5000)throw new Error("압축 파일 안에 파일이 너무 많습니다.");if(f.name.endsWith("/")||/(^|\/)(__MACOSX|\.obsidian|\.trash)\//.test(f.name))return false;total+=f.originalSize;if(total>MaxTotal*2)throw new Error("압축을 풀면 너무 큽니다. 나눠서 내보내 주세요.");return true}});
 for(const [name,data] of Object.entries(archive)){
  // A Notion export can hold its parts as zips inside the zip.
  if(/\.zip$/i.test(name)&&depth<1){readZip(data,files,skipped,depth+1);continue}
  if(pageExt.test(name)||imageExt.test(name))files.set(name.replace(/\\/g,"/"),data);else skipped.push(name);
 }
}

/** Reads what another tool exported. Throws a readable message when the file is not something it understands. */
export async function readToolExport(fileList:File[]):Promise<ToolImportResult>{
 const files=new Map<string,Uint8Array>(),skipped:string[]=[];let size=0;
 for(const file of fileList){size+=file.size;if(size>MaxTotal)throw new Error("한 번에 80MB까지 가져올 수 있습니다.");const bytes=new Uint8Array(await file.arrayBuffer());
  if(/\.zip$/i.test(file.name))readZip(bytes,files,skipped);else if(pageExt.test(file.name)||imageExt.test(file.name))files.set(file.name,bytes);else skipped.push(file.name)}
 return buildPages(files,skipped);
}

export function buildPages(files:Map<string,Uint8Array>,skipped:string[]=[]):ToolImportResult{
 const names=[...files.keys()],byName=new Map(names.map(n=>[n.split("/").pop()!.toLowerCase(),n]));
 const notion=names.some(n=>notionId.test(n.replace(/\.[^.\/]+$/,""))||n.split("/").some(part=>notionId.test(part)));
 const obsidian=!notion&&[...files.entries()].some(([n,b])=>/\.md$/i.test(n)&&/\[\[[^\]]+\]\]/.test(decode(b.slice(0,20000))));
 const tool=notion?"Notion":obsidian?"Obsidian":"Markdown·HTML";
 const pages:ToolPage[]=[];
 // Notion writes every database twice (visible rows and _all); keep the complete one.
 const csvs=names.filter(n=>/\.csv$/i.test(n)),skipCsv=new Set(csvs.filter(n=>!/_all\.csv$/i.test(n)&&csvs.includes(n.replace(/\.csv$/i,"_all.csv"))));
 for(const path of names.filter(n=>pageExt.test(n)&&!skipCsv.has(n)).sort((a,b)=>a.split("/").length-b.split("/").length||a.localeCompare(b))){
  if(pages.length>=MaxPages){skipped.push(path);continue}
  const text=decode(files.get(path)!),ext=path.split(".").pop()!.toLowerCase(),fallback=clean(path.split("/").pop()!);
  let title=fallback,body="",images:string[]=[];
  if(ext==="csv"){body=table(parseCsv(text))}
  else if(ext==="html"||ext==="htm"){if(typeof DOMParser==="undefined"){skipped.push(path);continue}const page=htmlToText(text);title=page.title||fallback;body=page.body;images=page.images.map(src=>join(dirOf(path),safeDecode(src))).filter(p=>files.has(p))}
  else if(ext==="txt"){body=text.trim()}
  else{const page=markdownPage(text,path,files,byName);title=page.title||fallback;body=page.body;images=page.images}
  if(!body.trim()&&!images.length){skipped.push(path);continue}
  if(body.length>MaxBody)body=body.slice(0,MaxBody)+"\n\n_(내용이 길어 뒷부분을 잘랐습니다)_";
  // A Notion sub-page sits in the folder named after its parent page ("부모 abc.md" + "부모 abc/자식 def.md").
  const folder=dirOf(path),parentPath=folder?names.find(n=>n!==path&&/\.(md|html?)$/i.test(n)&&n.replace(/\.[^.\/]+$/,"")===folder)??null:null;
  pages.push({path,parent:parentPath,title:title.slice(0,160),body,images:[...new Set(images)].filter(p=>files.get(p)!.length<=MaxImage).map(p=>({name:p.split("/").pop()!,bytes:files.get(p)!})),source:path});
 }
 // Folder pictures nobody's page shows are not imported on their own.
 for(const n of names)if(imageExt.test(n)&&!pages.some(p=>p.images.some(i=>i.bytes===files.get(n))))skipped.push(n);
 if(!pages.length)throw new Error("가져올 페이지가 없습니다. Notion은 '내보내기 → Markdown & CSV(또는 HTML)'로 받은 .zip을, 다른 도구는 .md·.html·.txt·.csv 파일이나 그 묶음 .zip을 고르세요.");
 return {tool,pages,skipped};
}

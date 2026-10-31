import { unzipSync, strFromU8 } from "fflate";
export type ImportedDocument={title:string;body:string;sheets?:{name:string;rows:string[][]}[];warning?:string};
const xml=(value:string)=>{const document=new DOMParser().parseFromString(value,"application/xml");if(document.getElementsByTagName("parsererror").length)throw new Error("문서의 XML을 읽지 못했습니다.");return document};
const nodes=(root:Document|Element,name:string)=>Array.from(root.getElementsByTagNameNS("*",name));
const content=(node:Element)=>nodes(node,"t").map(n=>n.textContent??"").join("");
const targetPath=(base:string,path:string)=>{const parts=(path.startsWith("/")?path.slice(1):base+"/"+path).split("/");const result:string[]=[];for(const part of parts){if(part===".."){if(!result.length)throw new Error("잘못된 문서 경로입니다.");result.pop()}else if(part&&part!==".")result.push(part)}return result.join("/")};
const decodeText=(buffer:Uint8Array)=>{if(buffer[0]===255&&buffer[1]===254)return new TextDecoder("utf-16le").decode(buffer.slice(2));if(buffer[0]===254&&buffer[1]===255)return new TextDecoder("utf-16be").decode(buffer.slice(2));try{return new TextDecoder("utf-8",{fatal:true}).decode(buffer)}catch{return new TextDecoder("euc-kr").decode(buffer)}};
const toTable=(rows:string[][])=>{if(!rows.length)return "";const width=Math.min(50,Math.max(...rows.map(x=>x.length)));const line=(r:string[])=>"| "+Array.from({length:width},(_,i)=>(r[i]??"").replace(/\|/g,"\\|").replace(/\r?\n/g," ")).join(" | ")+" |";return [line(rows[0]),"| "+Array(width).fill("---").join(" | ")+" |",...rows.slice(1).map(line)].join("\n")};
export async function readDocument(file:File):Promise<ImportedDocument>{
 if(file.size>10*1024*1024)throw new Error("10MB 이하의 파일을 불러오세요.");
 const ext=file.name.split(".").pop()?.toLowerCase(),bytes=new Uint8Array(await file.arrayBuffer()),title=file.name.replace(/\.[^.]+$/,"");
 if(["txt","md","csv"].includes(ext??"")){const body=decodeText(bytes);if(body.length>200000)throw new Error("텍스트가 너무 깁니다. 문서를 나누거나 원본 파일로 첨부하세요.");return {title,body}}
 if(!["docx","pptx","xlsx"].includes(ext??""))throw new Error(".pptx, .docx, .xlsx, .txt, .md, .csv 파일을 지원합니다. 구형 .ppt/.doc/.xls는 새 형식으로 저장한 뒤 불러오세요.");
 let total=0,count=0;
 const archive=unzipSync(bytes,{filter:f=>{if(++count>3000)throw new Error("문서에 포함된 파일이 너무 많습니다.");if(!/\.(xml|rels)$/i.test(f.name))return false;total+=f.originalSize;if(total>30*1024*1024||f.originalSize>12*1024*1024)throw new Error("압축 해제 크기가 너무 큰 문서입니다.");return true}});
 const get=(name:string)=>{if(!archive[name])throw new Error("필요한 문서 내용이 없습니다: "+name);return xml(strFromU8(archive[name]))};
 let body="",sheets:ImportedDocument["sheets"],warning="";
 if(ext==="docx"){
  const doc=get("word/document.xml");const root=nodes(doc,"body")[0];
  body=Array.from(root?.childNodes??[]).filter(child=>child.nodeType===1).map(node=>{const child=node as Element;if(child.localName==="tbl")return toTable(nodes(child,"tr").map(row=>Array.from(row.childNodes).filter(x=>x.nodeType===1&&(x as Element).localName==="tc").map(cell=>nodes(cell as Element,"p").map(content).join(" "))));if(child.localName==="p")return content(child);return ""}).join("\n\n");
 }else if(ext==="pptx"){
  const relationships=new Map(nodes(get("ppt/_rels/presentation.xml.rels"),"Relationship").filter(x=>x.getAttribute("TargetMode")!=="External").map(x=>[x.getAttribute("Id"),x.getAttribute("Target")??""]));
  body=nodes(get("ppt/presentation.xml"),"sldId").map((slide,i)=>{const key=slide.getAttributeNS("http://schemas.openxmlformats.org/officeDocument/2006/relationships","id");const path=relationships.get(key);return `## 슬라이드 ${i+1}\n\n`+(path?nodes(get(targetPath("ppt",path)),"p").map(content).filter(Boolean).join("\n"):"")}).join("\n\n---\n\n");
 }else{
  const shared=archive["xl/sharedStrings.xml"]?nodes(get("xl/sharedStrings.xml"),"si").map(content):[];
  const relations=new Map(nodes(get("xl/_rels/workbook.xml.rels"),"Relationship").filter(x=>x.getAttribute("TargetMode")!=="External").map(x=>[x.getAttribute("Id"),x.getAttribute("Target")??""]));
  sheets=nodes(get("xl/workbook.xml"),"sheet").map(sheet=>{const target=relations.get(sheet.getAttributeNS("http://schemas.openxmlformats.org/officeDocument/2006/relationships","id"));if(!target)return {name:sheet.getAttribute("name")??"시트",rows:[]};
   const rawRows=nodes(get(targetPath("xl",target)),"row");if(rawRows.length>500)warning="시트마다 처음 500행·50열을 미리 봅니다. 전체 내용은 원본 파일에 보존됩니다.";
   const parsed=rawRows.slice(0,500).map(row=>{const values:string[]=[];for(const cell of nodes(row,"c")){const column=(cell.getAttribute("r")??"A1").replace(/\d/g,"");let index=0;for(const c of column)index=index*26+c.charCodeAt(0)-64;index--;if(index<0||index>=50){warning="시트마다 처음 500행·50열을 미리 봅니다. 전체 내용은 원본 파일에 보존됩니다.";continue}const type=cell.getAttribute("t"),value=nodes(cell,"v")[0]?.textContent??"";values[index]=type==="s"?shared[Number(value)]??"":type==="inlineStr"?content(cell):type==="b"?(value==="1"?"TRUE":"FALSE"):value||(nodes(cell,"f")[0]?.textContent?"="+nodes(cell,"f")[0].textContent:"")}return values});
   return {name:sheet.getAttribute("name")??"시트",rows:parsed}});
  body=sheets.map(s=>"## "+s.name+"\n\n"+toTable(s.rows)).join("\n\n");
 }
 if(!body.trim())throw new Error("읽을 텍스트가 없습니다. 스캔 이미지·그림은 원본 파일로 첨부하세요.");
 if(body.length>200000)throw new Error("추출된 내용이 너무 깁니다. 파일을 나누거나 원본 파일로 첨부하세요.");
 return {title,body,sheets,warning:warning||undefined};
}

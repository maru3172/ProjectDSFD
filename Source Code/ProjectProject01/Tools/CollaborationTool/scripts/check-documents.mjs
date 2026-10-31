import {DOMParser} from "@xmldom/xmldom";
import {readFileSync,writeFileSync} from "node:fs";
import {build} from "esbuild";
import assert from "node:assert/strict";
globalThis.DOMParser=DOMParser;
await build({entryPoints:["app/import-documents.ts"],bundle:true,platform:"node",format:"esm",outfile:"outputs/checks/import-parser.mjs"});
const {readDocument}=await import("../outputs/checks/import-parser.mjs"),checks=[];
for(const [name,expected] of [["회의록.docx",["한국어 회의록","담당자","기록 확인"]],["발표자료.pptx",["슬라이드 1","첫 번째 슬라이드","두 번째 슬라이드"]],["계획표.xlsx",["일정","팀원","예산","3"]],["한국어.md",["주간 계획","모든 팀원이"]],["한국어.txt",["첫 줄\n둘째 줄"]]]){
 const source=readFileSync("outputs/checks/documents/"+name),document=await readDocument(new File([source],name));for(const value of expected)assert.ok(document.body.includes(value),name+": "+value);
 if(name.endsWith("pptx"))assert.ok(document.body.indexOf("첫 번째")<document.body.indexOf("두 번째"),"파일 번호 대신 실제 슬라이드 순서");
 if(name.endsWith("xlsx"))assert.equal(document.sheets.length,2,"엑셀의 여러 시트 유지");checks.push(name);
}
const utf16=await readDocument(new File([Buffer.concat([Buffer.from([255,254]),Buffer.from("한국어 UTF16\n둘째 줄","utf16le")])],"UTF16.txt"));assert.ok(utf16.body.includes("한국어 UTF16"));checks.push("UTF-16 한국어");
await assert.rejects(()=>readDocument(new File(["old format"],"old.doc")),/새 형식/);checks.push("구형 형식 안내");
await assert.rejects(()=>readDocument(new File([new Uint8Array(10*1024*1024+1)],"big.txt")),/10MB/);checks.push("파일 크기 제한");
writeFileSync("outputs/checks/document-results.json",JSON.stringify({success:true,count:checks.length,checks},null,2));console.log(JSON.stringify({success:true,count:checks.length,checks}));

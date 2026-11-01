"use client";
import {useState} from 'react';
import {safeRequest} from './safe-request';
import {FileDown,Printer} from 'lucide-react';
import {makeCsv,makeDocx,makeXlsx,markdownBlocks,xml,type Block} from './office-files';
import {isDeck,parseSlides,slidesText} from './slides';
import type {Entry} from './types';
type Row=Record<string,any>;
// Export records out of Daybook: one record (txt, md, Word, PDF) or a filtered list (csv, Excel, Word, txt).
// Files are written in the browser from /api/features?action=exportEntries; PDF goes through the print dialog.
const Kinds:Record<string,string>={task:'업무',meeting:'회의',note:'메모',log:'작업 로그',decision:'결정',event:'일정',plan:'계획',goal:'목표',discussion:'의논',chat:'대화'};
const States:Record<string,string>={todo:'할 일',inprogress:'진행 중',blocked:'막힘',done:'완료'};
const Priorities:Record<string,string>={low:'낮음',normal:'보통',high:'높음'};
const code=(id:string)=>'DB-'+id.replace(/-/g,'').slice(0,8).toLowerCase();
const safeName=(name:string)=>name.replace(/[\\/:*?"<>|\u0000-\u001f]/g,' ').replace(/\s+/g,' ').trim().slice(0,80)||'Daybook';
export function saveFile(name:string,bytes:Uint8Array|string,type:string){const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([bytes as BlobPart],{type}));a.download=name;document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(a.href),2000)}
const Mime={docx:'application/vnd.openxmlformats-officedocument.wordprocessingml.document',xlsx:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',csv:'text/csv;charset=utf-8',txt:'text/plain;charset=utf-8',md:'text/markdown;charset=utf-8'};

const meta=(e:Row)=>[e.day,Kinds[e.kind]??e.kind,e.type,States[e.status],e.assignee?'담당 '+e.assignee:'',e.due_day?'기한 '+e.due_day:'',e.project,e.topic,code(e.id)].filter(Boolean).join(' · ');
/** A record's body as plain Markdown (a slide deck becomes its slides' titles and text). */
const bodyOf=(e:Row)=>isDeck(e.body)?(parseSlides(e.body)??[]).map((s,i)=>`## ${i+1}. ${s.title}\n\n${s.text}`).join('\n\n'):String(e.body??'');
const blocksOf=(e:Row,level:'title'|'h1'):Block[]=>[{kind:level,text:e.title},{kind:'meta',text:meta(e)},...(e.summary?[{kind:'quote' as const,text:e.summary}]:[]),...markdownBlocks(bodyOf(e))];
const textOf=(e:Row)=>`${e.title}\n${meta(e)}\n${e.summary?'\n요약: '+e.summary+'\n':''}\n${isDeck(e.body)?slidesText(parseSlides(e.body)??[]):bodyOf(e)}`.trim()+'\n';
const markdownOf=(e:Row)=>`# ${e.title}\n\n- ${meta(e).split(' · ').join('\n- ')}\n${e.summary?'\n> '+e.summary+'\n':''}\n${bodyOf(e)}\n`;
const table=(list:Row[])=>[['코드','날짜','종류','항목','제목','상태','우선순위','담당','작성','기한','프로젝트','주제','요약','내용'],
 ...list.map(e=>[code(e.id),e.day,Kinds[e.kind]??e.kind,e.type??'',e.title,States[e.status]??e.status,Priorities[e.priority]??e.priority,e.assignee??'',e.author??'',e.due_day??'',e.project??'',e.topic??'',e.summary??'',isDeck(e.body)?slidesText(parseSlides(e.body)??[]):String(e.body??'')])];

/** Prints blocks through a hidden frame; the browser's print dialog saves it as PDF ("PDF로 저장"). */
export function printBlocks(title:string,blocks:Block[]){
 const tag:Record<Block['kind'],string>={title:'h1',h1:'h2',h2:'h3',h3:'h4',p:'p',bullet:'li',number:'li',quote:'blockquote',code:'pre',meta:'p class="meta"'};
 const html=`<!doctype html><html lang="ko"><head><meta charset="utf-8"><title>${xml(title)}</title><style>@page{size:A4;margin:18mm}body{font-family:"맑은 고딕","Malgun Gothic","Noto Sans KR",sans-serif;color:#1f2d33;line-height:1.7;font-size:11pt}h1{font-size:20pt;margin:0 0 4pt}h2{font-size:15pt;margin:18pt 0 6pt;border-bottom:1px solid #ccd;padding-bottom:3pt}h3{font-size:13pt;margin:14pt 0 4pt}h4{font-size:11.5pt;margin:10pt 0 3pt}.meta{color:#667;font-size:9pt;margin:0 0 10pt}li{margin-left:14pt}blockquote{margin:6pt 0;padding-left:10pt;border-left:3px solid #9cc;color:#445}pre{font-family:Consolas,monospace;font-size:9pt;margin:0;white-space:pre-wrap}h2,h3,h4{break-after:avoid}</style></head><body>`
  +blocks.map(b=>{const t=tag[b.kind],name=t.split(' ')[0];return b.kind==='bullet'?`<ul><li>${xml(b.text)}</li></ul>`:b.kind==='number'?`<ol><li>${xml(b.text)}</li></ol>`:`<${t}>${xml(b.text)}</${name}>`}).join('')+'</body></html>';
 const frame=document.createElement('iframe');frame.style.cssText='position:fixed;right:0;bottom:0;width:0;height:0;border:0';frame.srcdoc=html;
 frame.onload=()=>{frame.contentWindow?.focus();frame.contentWindow?.print();setTimeout(()=>frame.remove(),60000)};document.body.appendChild(frame);
}

export function ExportRecords({workspace,entry}:{workspace:string;entry:Entry|null}){
 const [error,setError]=useState(''),[status,setStatus]=useState(''),[busy,setBusy]=useState(false),[filter,setFilter]=useState({kind:'',status:'',from:'',to:''});
 const load=async(params:Row)=>{const r=await safeRequest('/api/features?'+new URLSearchParams({action:'exportEntries',workspace,...Object.fromEntries(Object.entries(params).filter(([,v])=>v))}));const result=await r.json() as Row;if(!r.ok)throw new Error(result.error??'요청 실패');return result};
 const run=async(job:()=>Promise<string|void>)=>{if(busy)return;setBusy(true);setError('');setStatus('');try{setStatus((await job())??'')}catch(e){setError((e as Error).message)}finally{setBusy(false)}};
 const one=(format:'txt'|'md'|'docx'|'pdf')=>run(async()=>{
  const e=(await load({entry:entry!.id})).entries[0],name=safeName(e.title);
  if(format==='txt')saveFile(name+'.txt',textOf(e),Mime.txt);
  else if(format==='md')saveFile(name+'.md',markdownOf(e),Mime.md);
  else if(format==='docx')saveFile(name+'.docx',makeDocx(e.title,blocksOf(e,'title')),Mime.docx);
  else{printBlocks(e.title,blocksOf(e,'title'));return '인쇄 창에서 프린터를 "PDF로 저장"으로 고르세요.'}
 });
 const many=(format:'csv'|'xlsx'|'docx'|'txt')=>run(async()=>{
  const result=await load(filter),list:Row[]=result.entries;if(!list.length)throw new Error('조건에 맞는 기록이 없습니다.');
  const label=[filter.kind&&Kinds[filter.kind],filter.status&&States[filter.status],filter.from||filter.to?`${filter.from||'처음'}~${filter.to||'지금'}`:''].filter(Boolean).join(' ')||'전체';
  const name=safeName(`${result.workspace} 기록 ${label}`);
  if(format==='csv')saveFile(name+'.csv',makeCsv(table(list)),Mime.csv);
  else if(format==='xlsx')saveFile(name+'.xlsx',makeXlsx(name,[{name:'기록',rows:table(list),widths:[12,11,8,10,30,8,8,10,10,11,12,12,30,60]}]),Mime.xlsx);
  else if(format==='docx')saveFile(name+'.docx',makeDocx(name,[{kind:'title',text:name},{kind:'meta',text:`${list.length}개 기록 · ${new Date().toISOString().slice(0,10)} 내보냄`},...list.flatMap(e=>blocksOf(e,'h1'))]),Mime.docx);
  else saveFile(name+'.txt',list.map(textOf).join('\n────────────────────\n\n'),Mime.txt);
  return `${list.length}개 기록을 내보냈습니다.${result.truncated?' 5,000개가 넘어 앞의 5,000개만 담았습니다. 기간을 나눠 내보내세요.':''}`;
 });
 const set=(key:keyof typeof filter)=>(e:{target:{value:string}})=>setFilter({...filter,[key]:e.target.value});
 return <>{error&&<div className="operations-error" role="alert">{error}</div>}{status&&<div className="operations-status" role="status">{status}</div>}
  <h3>선택한 기록 내보내기</h3>
  {!entry?<p className="muted">기록을 하나 고르면 그 기록을 텍스트, Markdown, Word, PDF로 내보낼 수 있습니다.</p>:<>
   <p>선택한 기록: <strong>{entry.title}</strong></p>
   <div className="inline">{([['txt','텍스트 (.txt)'],['md','Markdown (.md)'],['docx','Word (.docx)']] as const).map(([f,l])=><button key={f} className="secondary" disabled={busy} onClick={()=>void one(f)}><FileDown size={15}/>{l}</button>)}<button className="secondary" disabled={busy} onClick={()=>void one('pdf')}><Printer size={15}/>PDF (인쇄)</button></div>
  </>}
  <h3>기록 목록 내보내기</h3>
  <p className="muted">조건에 맞는 기록을 표(CSV, Excel)나 한 문서(Word, 텍스트)로 모읍니다. 조건을 비우면 공간의 모든 기록입니다. 첨부 파일은 담지 않습니다.</p>
  <div className="team-filters">
   <label>종류<select value={filter.kind} onChange={set('kind')}><option value="">전체</option>{Object.entries(Kinds).map(([k,l])=><option key={k} value={k}>{l}</option>)}</select></label>
   <label>상태<select value={filter.status} onChange={set('status')}><option value="">전체</option>{Object.entries(States).map(([k,l])=><option key={k} value={k}>{l}</option>)}</select></label>
   <label>시작일<input type="date" value={filter.from} onChange={set('from')}/></label><label>종료일<input type="date" value={filter.to} onChange={set('to')}/></label>
  </div>
  <div className="inline">{([['csv','CSV'],['xlsx','Excel (.xlsx)'],['docx','Word (.docx)'],['txt','텍스트 (.txt)']] as const).map(([f,l])=><button key={f} className="secondary" disabled={busy} onClick={()=>void many(f)}><FileDown size={15}/>{l}</button>)}</div>
  <p className="muted">PDF가 필요하면 Word 파일을 열어 PDF로 저장하거나, 기록 하나씩은 위의 "PDF (인쇄)"를 쓰세요.</p>
 </>;
}

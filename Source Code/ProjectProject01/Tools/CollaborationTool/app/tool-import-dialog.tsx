"use client";
import {useEffect,useRef,useState} from 'react';
import {createPortal} from 'react-dom';
import {Import,X} from 'lucide-react';
import {safeRequest} from './safe-request';
import {readToolExport,type ToolImportResult} from './tool-import';
type Row=Record<string,any>;
const kinds=[['note','메모'],['meeting','회의록'],['log','작업 로그'],['plan','계획']] as const;
async function call(path:string,init:RequestInit):Promise<Row>{const r=await safeRequest(path,init);const result=await r.json().catch(()=>({})) as Row;if(!r.ok)throw new Error(result.error??'요청 실패');return result}

/**
 * "다른 도구에서 가져오기": pick a Notion/Obsidian export (or loose .md/.html/.txt/.csv files), check the pages, and
 * each one becomes a record with its pictures attached. Sub-pages are linked under their parent page.
 */
export default function ToolImportButton({workspace,day,disabled,topic,project,onDone}:{workspace:string;day:string;disabled:boolean;topic?:string|null;project?:string|null;onDone:(message:string)=>void}){
 const [open,setOpen]=useState(false),[result,setResult]=useState<ToolImportResult|null>(null),[chosen,setChosen]=useState<Set<string>>(new Set()),[kind,setKind]=useState('note');
 const [error,setError]=useState(''),[progress,setProgress]=useState(''),[busy,setBusy]=useState(false);
 const input=useRef<HTMLInputElement>(null),cancelled=useRef(false);
 useEffect(()=>{if(!open)return;const key=(e:KeyboardEvent)=>{if(e.key==='Escape'&&!busy)close()};window.addEventListener('keydown',key);return()=>window.removeEventListener('keydown',key)});
 function close(){if(busy){cancelled.current=true;return}setOpen(false);setResult(null);setError('');setProgress('')}
 async function pick(files:File[]){if(!files.length)return;setError('');setResult(null);setProgress('파일을 읽는 중…');
  try{const read=await readToolExport(files);setResult(read);setChosen(new Set(read.pages.map(p=>p.path)));setProgress('')}catch(e){setError((e as Error).message);setProgress('')}}
 async function start(){if(!result)return;const pages=result.pages.filter(p=>chosen.has(p.path));if(!pages.length)return;
  setBusy(true);setError('');cancelled.current=false;const made=new Map<string,string>();let pictures=0,failed=0;
  try{for(const [i,page] of pages.entries()){
    if(cancelled.current)break;setProgress(`${i+1}/${pages.length} · ${page.title}`);
    const parent=page.parent?made.get(page.parent)??null:null;
    const body=page.body+`\n\n---\n${result.tool}에서 가져옴: ${page.source}`;
    const created=await call('/api/data',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({workspace,action:'create',kind,title:page.title||'제목 없음',body:body.slice(0,200000),day,status:'todo',priority:'normal',parent_id:parent,topic_id:topic||null,project_id:project||null})});
    made.set(page.path,created.id);
    for(const image of page.images){const form=new FormData();form.set('file',new File([image.bytes as BlobPart],image.name));form.set('entry',created.id);form.set('comment','false');
     try{await call('/api/file',{method:'POST',body:form});pictures++}catch{failed++}}
   }
   window.dispatchEvent(new Event('daybook-saved'));
   const message=`${result.tool}에서 기록 ${made.size}개${pictures?`, 사진 ${pictures}장`:''}을 가져왔습니다.${failed?` 사진 ${failed}장은 올리지 못했습니다.`:''}${cancelled.current?' (중간에 멈춤)':''}`;
   setBusy(false);setOpen(false);setResult(null);setProgress('');onDone(message);
  }catch(e){setBusy(false);setError(`${made.size}개까지 가져온 뒤 멈췄습니다: ${(e as Error).message}`);setProgress('');onDone(`${made.size}개 기록을 가져왔습니다.`)}}
 const all=result?.pages.length??0,count=chosen.size;
 return <>
  <button disabled={disabled} onClick={()=>setOpen(true)}><Import size={14}/>다른 도구에서 가져오기</button>
  {open&&typeof document!=='undefined'&&createPortal(<div className="backdrop tool-import-backdrop" onMouseDown={e=>{if(e.target===e.currentTarget)close()}}>
   <section className="tool-import" role="dialog" aria-modal="true" aria-label="다른 도구에서 가져오기">
    <header><h2>다른 도구에서 가져오기</h2><button className="icon" aria-label="닫기" onClick={close}><X size={16}/></button></header>
    <div className="tool-import-help">
     <p><b>Notion</b>: 페이지 또는 워크스페이스 설정 → 내보내기 → <i>Markdown & CSV</i>(또는 HTML) → 받은 .zip을 그대로 고르세요. 하위 페이지는 원래 페이지 아래에 이어지고, 데이터베이스는 표로, 사진은 첨부로 들어옵니다.</p>
     <p><b>Obsidian·기타</b>: 보관함 폴더를 .zip으로 묶거나 .md·.html·.txt·.csv 파일을 여러 개 고르세요. Google 문서·Confluence·Evernote는 Markdown이나 HTML로 내보낸 파일을 쓰면 됩니다.</p>
    </div>
    <div className="tool-import-pick"><button className="secondary" disabled={busy} onClick={()=>input.current?.click()}>파일 고르기</button><input ref={input} type="file" hidden multiple accept=".zip,.md,.markdown,.txt,.csv,.html,.htm" onChange={e=>{void pick(Array.from(e.target.files??[]));e.target.value=''}}/>
     {result&&<span>{result.tool} 내보내기 · 페이지 {all}개{result.skipped.length?` · 건너뜀 ${result.skipped.length}개`:''}</span>}</div>
    {result&&<>
     <div className="tool-import-options"><label>기록 종류 <select value={kind} disabled={busy} onChange={e=>setKind(e.target.value)}>{kinds.map(([v,l])=><option key={v} value={v}>{l}</option>)}</select></label>
      <label className="check"><input type="checkbox" disabled={busy} checked={count===all} onChange={e=>setChosen(e.target.checked?new Set(result.pages.map(p=>p.path)):new Set())}/>모두 선택 ({count}/{all})</label></div>
     <ul className="tool-import-list">{result.pages.map(p=><li key={p.path} style={{paddingLeft:8+Math.min(4,p.path.split('/').length-1)*14}}><label className="check"><input type="checkbox" disabled={busy} checked={chosen.has(p.path)} onChange={e=>setChosen(old=>{const next=new Set(old);if(e.target.checked)next.add(p.path);else next.delete(p.path);return next})}/>
      <span className="tool-import-title">{p.title}</span><small>{p.body.length.toLocaleString()}자{p.images.length?` · 사진 ${p.images.length}`:''}</small></label></li>)}</ul>
     {result.skipped.length>0&&<details className="tool-import-skipped"><summary>건너뛴 파일 {result.skipped.length}개</summary><p>{result.skipped.slice(0,40).join(', ')}{result.skipped.length>40?' …':''}</p></details>}
    </>}
    {error&&<p className="tool-import-error" role="alert">{error}</p>}
    {progress&&<p className="tool-import-progress" role="status">{progress}</p>}
    <footer><button className="secondary" onClick={close}>{busy?'멈추기':'닫기'}</button><button className="primary" disabled={!result||!count||busy} onClick={()=>void start()}>{count?`${count}개 가져오기`:'가져오기'}</button></footer>
   </section></div>,document.body)}
 </>;
}

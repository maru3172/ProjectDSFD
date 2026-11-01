"use client";
import {useCallback,useEffect,useRef,useState,type KeyboardEvent} from 'react';
import {createPortal} from 'react-dom';
import {safeRequest} from './safe-request';
import {ArrowDown,ArrowUp,ChevronLeft,ChevronRight,Copy,FileDown,ImagePlus,Play,Plus,Save,Trash2,X} from 'lucide-react';
import {makePptx,type SlideImage} from './office-files';
import {parseSlides,writeSlides,type Slide} from './slides';
import {saveFile} from './export-tools';
type Row=Record<string,any>;
// Slide decks: write slides (title, text with "- " bullets, one picture), present them full screen, and download
// them as .pptx to keep editing in PowerPoint. A deck is a "발표 자료" record (format: app/slides.ts).
async function call(url:string,value?:Row){const r=await safeRequest(url,{method:value?'POST':'GET',headers:value?{'Content-Type':'application/json'}:undefined,body:value?JSON.stringify(value):undefined});const result=await r.json() as Row;if(!r.ok)throw new Error(result.error??'요청 실패');if(value)window.dispatchEvent(new Event('daybook-saved'));return result}
const imageUrl=(id:string)=>`/api/file?id=${encodeURIComponent(id)}&inline=1`;
const kstToday=()=>new Date(Date.now()+9*3600000).toISOString().slice(0,10);
const blank=():Slide=>({title:'',text:'',image:null});

/** One slide drawn the way the .pptx lays it out: title-only is a centered cover, a picture sits right of the text. */
export function SlideView({slide,className=''}:{slide:Slide;className?:string}){
 const lines=slide.text.split('\n'),hasText=lines.some(l=>l.trim());
 if(!hasText&&!slide.image)return <div className={'slide-view '+className}><div className="slide-canvas cover"><h2>{slide.title||'제목 없음'}</h2></div></div>;
 return <div className={'slide-view '+className}><div className="slide-canvas"><h2>{slide.title||'제목 없음'}</h2><div className={'slide-body'+(slide.image&&hasText?' split':'')}>
  {hasText&&<div className="slide-text">{lines.map((l,i)=>/^\s*[-*•]\s+/.test(l)?<p key={i} className="bullet">{l.replace(/^\s*[-*•]\s+/,'')}</p>:<p key={i}>{l||' '}</p>)}</div>}
  {slide.image&&<div className="slide-image"><img src={imageUrl(slide.image.id)} alt=""/></div>}
 </div></div></div>;
}

function Presenter({slides,start,onClose}:{slides:Slide[];start:number;onClose:()=>void}){
 const [index,setIndex]=useState(start),host=useRef<HTMLDivElement>(null);
 useEffect(()=>{host.current?.focus();void host.current?.requestFullscreen?.().catch(()=>{});const exit=()=>{if(!document.fullscreenElement)onClose()};document.addEventListener('fullscreenchange',exit);return()=>{document.removeEventListener('fullscreenchange',exit);if(document.fullscreenElement)void document.exitFullscreen().catch(()=>{})}},[onClose]);
 const move=(by:number)=>setIndex(i=>Math.min(slides.length-1,Math.max(0,i+by)));
 const keys=(e:KeyboardEvent)=>{e.stopPropagation(); // Keys stay with the presenter, not the editor behind it.
 if(['ArrowRight','ArrowDown','PageDown',' ','Enter'].includes(e.key)){e.preventDefault();move(1)}else if(['ArrowLeft','ArrowUp','PageUp','Backspace'].includes(e.key)){e.preventDefault();move(-1)}else if(e.key==='Home')setIndex(0);else if(e.key==='End')setIndex(slides.length-1);else if(e.key==='Escape')onClose()};
 return createPortal(<div className="slide-presenter" ref={host} tabIndex={-1} onKeyDown={keys} onClick={()=>move(1)}>
  <SlideView slide={slides[index]} className="present"/>
  <div className="slide-presenter-bar" onClick={e=>e.stopPropagation()}><button onClick={()=>move(-1)} aria-label="이전"><ChevronLeft size={18}/></button><span>{index+1} / {slides.length}</span><button onClick={()=>move(1)} aria-label="다음"><ChevronRight size={18}/></button><button onClick={onClose} aria-label="발표 끝내기"><X size={18}/></button></div>
 </div>,document.body);
}

// Pictures the browser shows but PowerPoint may not (webp, svg...) are redrawn as PNG.
async function asSlideImage(blob:Blob):Promise<SlideImage>{
 const bytes=new Uint8Array(await blob.arrayBuffer()),bitmap=await createImageBitmap(blob),size={width:bitmap.width,height:bitmap.height};
 const type=bytes[0]===0x89&&bytes[1]===0x50?'png':bytes[0]===0xff&&bytes[1]===0xd8?'jpeg':bytes[0]===0x47&&bytes[1]===0x49?'gif':null;
 if(type){bitmap.close();return {bytes,type,...size}}
 const canvas=document.createElement('canvas');canvas.width=size.width;canvas.height=size.height;canvas.getContext('2d')!.drawImage(bitmap,0,0);bitmap.close();
 const png=await new Promise<Blob>((resolve,reject)=>canvas.toBlob(b=>b?resolve(b):reject(new Error('사진을 바꾸지 못했습니다.')),'image/png'));
 return {bytes:new Uint8Array(await png.arrayBuffer()),type:'png',...size};
}

export async function downloadPptx(title:string,slides:Slide[]){
 const images=new Map<string,SlideImage>();
 for(const s of slides)if(s.image&&!images.has(s.image.id)){const r=await safeRequest(imageUrl(s.image.id));if(!r.ok)throw new Error(`"${s.title}" 슬라이드의 사진을 불러오지 못했습니다.`);images.set(s.image.id,await asSlideImage(await r.blob()))}
 saveFile((title.replace(/[\\/:*?"<>|]/g,' ').trim()||'발표 자료')+'.pptx',makePptx(title,slides,images),'application/vnd.openxmlformats-officedocument.presentationml.presentation');
}

type Deck={id:string|null;title:string;slides:Slide[];version:number};
function Editor({workspace,start,onClose,onOpen}:{workspace:string;start:Deck;onClose:()=>void;onOpen:(id:string)=>void}){
 const [deck,setDeck]=useState(start),[index,setIndex]=useState(0),[dirty,setDirty]=useState(false),[busy,setBusy]=useState(false),[error,setError]=useState(''),[status,setStatus]=useState(''),[presenting,setPresenting]=useState(false);
 const file=useRef<HTMLInputElement>(null),slide=deck.slides[index]??deck.slides[0];
 const change=(next:Partial<Deck>)=>{setDeck(d=>({...d,...next}));setDirty(true)};
 const edit=(patch:Partial<Slide>)=>change({slides:deck.slides.map((s,i)=>i===index?{...s,...patch}:s)});
 const run=async(job:()=>Promise<string|void>)=>{if(busy)return;setBusy(true);setError('');setStatus('');try{setStatus((await job())??'')}catch(e){setError((e as Error).message)}finally{setBusy(false)}};
 // Creates the record on first save; later saves update it with its version, so a teammate's newer save is not overwritten.
 const persist=async(current:Deck):Promise<Deck>=>{
  const title=current.title.trim()||'제목 없는 발표 자료',body=writeSlides(current.slides);
  if(!current.id){const type=(await call('/api/features',{action:'deckType',workspace})).id;const r=await call('/api/data',{workspace,action:'create',kind:'note',title,body,day:kstToday(),custom_type_id:type});return {...current,title,id:r.id,version:1}}
  await call('/api/data',{workspace,action:'update',id:current.id,version:current.version,title,body});return {...current,title,version:current.version+1};
 };
 const save=()=>run(async()=>{const saved=await persist(deck);setDeck(saved);setDirty(false);return '저장했습니다.'});
 const addImage=(picked:File)=>run(async()=>{
  if(picked.size>10*1048576)throw new Error('사진은 10MB까지 넣을 수 있습니다.');
  let current=deck;if(!current.id||dirty){current=await persist(current);setDeck(current);setDirty(false)} // A picture is attached to the record, so it must exist first.
  const image=await asSlideImage(picked),name=picked.name.replace(/\.[^.]+$/,'')+(image.type==='jpeg'?'.jpg':'.'+image.type);
  const form=new FormData();form.append('entry',current.id!);form.append('file',new File([image.bytes as BlobPart],name,{type:'image/'+image.type}));
  const r=await safeRequest('/api/file',{method:'POST',body:form});const result=await r.json() as Row;if(!r.ok)throw new Error(result.error??'사진을 올리지 못했습니다.');
  const next={...current,slides:current.slides.map((s,i)=>i===index?{...s,image:{id:result.id,width:image.width,height:image.height}}:s)};
  setDeck(await persist(next));setDirty(false);return '사진을 넣고 저장했습니다.';
 });
 const move=(by:number)=>{const to=index+by;if(to<0||to>=deck.slides.length)return;const slides=[...deck.slides];[slides[index],slides[to]]=[slides[to],slides[index]];change({slides});setIndex(to)};
 const insert=(copy:boolean)=>{const slides=[...deck.slides];slides.splice(index+1,0,copy?{...slide}:blank());change({slides});setIndex(index+1)};
 const remove=()=>{if(deck.slides.length===1){change({slides:[blank()]});return}const slides=deck.slides.filter((_,i)=>i!==index);change({slides});setIndex(Math.min(index,slides.length-1))};
 const close=()=>{if(dirty&&!confirm('저장하지 않은 변경이 있습니다. 닫을까요?'))return;onClose()};
 useEffect(()=>{const warn=(e:BeforeUnloadEvent)=>{if(dirty){e.preventDefault()}};addEventListener('beforeunload',warn);return()=>removeEventListener('beforeunload',warn)},[dirty]);
 return createPortal(<div className="slide-editor" role="dialog" aria-label="발표 자료 편집" onKeyDown={e=>{if(e.key==='Escape'&&!presenting){e.preventDefault();close()}}}>
  <header><input className="slide-deck-title" value={deck.title} maxLength={160} placeholder="발표 자료 제목" onChange={e=>change({title:e.target.value})}/>
   <div className="slide-actions"><button className="primary" disabled={busy||!dirty&&!!deck.id} onClick={()=>void save()}><Save size={15}/>{dirty||!deck.id?'저장':'저장됨'}</button>
    <button className="secondary" onClick={()=>setPresenting(true)}><Play size={15}/>발표</button>
    <button className="secondary" disabled={busy} onClick={()=>void run(async()=>{await downloadPptx(deck.title,deck.slides);return 'PowerPoint 파일을 내려받았습니다.'})}><FileDown size={15}/>.pptx</button>
    {deck.id&&<button className="secondary" onClick={()=>{onOpen(deck.id!)}}>기록 열기</button>}
    <button className="icon" aria-label="닫기" onClick={close}><X size={18}/></button></div></header>
  {error&&<div className="operations-error" role="alert">{error}</div>}{status&&<div className="operations-status" role="status">{status}</div>}
  <div className="slide-workspace">
   <ol className="slide-list">{deck.slides.map((s,i)=><li key={i}><button className={i===index?'active':''} onClick={()=>setIndex(i)} aria-label={`${i+1}번 슬라이드`}><span>{i+1}</span><SlideView slide={s} className="thumb"/></button></li>)}</ol>
   <section className="slide-form">
    <div className="slide-tools"><button className="secondary" onClick={()=>insert(false)}><Plus size={15}/>새 슬라이드</button><button className="secondary" onClick={()=>insert(true)}><Copy size={15}/>복제</button><button className="secondary" disabled={index===0} onClick={()=>move(-1)} aria-label="위로"><ArrowUp size={15}/></button><button className="secondary" disabled={index===deck.slides.length-1} onClick={()=>move(1)} aria-label="아래로"><ArrowDown size={15}/></button><button className="secondary" onClick={remove}><Trash2 size={15}/>삭제</button></div>
    <label>제목<input value={slide.title} maxLength={200} placeholder="슬라이드 제목" onChange={e=>edit({title:e.target.value})}/></label>
    <label>내용 <small className="muted">"- "로 시작하면 글머리표. 내용과 사진이 없으면 표지처럼 가운데 제목만 나옵니다.</small><textarea rows={9} value={slide.text} placeholder={'- 첫째 요점\n- 둘째 요점'} onChange={e=>edit({text:e.target.value})}/></label>
    <div className="inline"><button className="secondary" disabled={busy} onClick={()=>file.current?.click()}><ImagePlus size={15}/>{slide.image?'사진 바꾸기':'사진 넣기'}</button>{slide.image&&<button className="secondary" onClick={()=>edit({image:null})}>사진 빼기</button>}
     <input ref={file} type="file" hidden accept=".png,.jpg,.jpeg,.gif,.webp" onChange={e=>{const f=e.currentTarget.files?.[0];e.currentTarget.value='';if(f)void addImage(f)}}/></div>
    <p className="muted">PowerPoint로 내려받으면 글자, 글머리표, 사진과 배치가 그대로 옮겨지고 PowerPoint에서 이어서 고칠 수 있습니다. 애니메이션·디자인 테마는 없습니다.</p>
   </section>
   <section className="slide-preview"><SlideView slide={slide}/></section>
  </div>
  {presenting&&<Presenter slides={deck.slides} start={index} onClose={()=>setPresenting(false)}/>}
 </div>,document.body);
}

export function SlideDecks({workspace,onOpen}:{workspace:string;onOpen:(id:string)=>void}){
 const [decks,setDecks]=useState<Row[]|null>(null),[open,setOpen]=useState<Deck|null>(null),[present,setPresent]=useState<Slide[]|null>(null),[error,setError]=useState('');
 const load=useCallback(()=>call(`/api/features?action=decks&workspace=${encodeURIComponent(workspace)}`).then(r=>setDecks(r.decks),e=>setError((e as Error).message)),[workspace]);
 useEffect(()=>{void load()},[load]);
 const toDeck=(d:Row):Deck=>({id:d.id,title:d.title,slides:parseSlides(d.body)??[blank()],version:d.version});
 return <>{error&&<div className="operations-error" role="alert">{error}</div>}
  <div className="inline"><h3 style={{margin:0}}>발표 자료</h3><button className="primary" onClick={()=>setOpen({id:null,title:'',slides:[blank()],version:0})}><Plus size={15}/>새 발표 자료</button></div>
  <p className="muted">파워포인트처럼 슬라이드를 만들고, 전체 화면으로 발표하고, .pptx로 내려받아 PowerPoint에서 이어서 고칠 수 있습니다. 발표 자료도 기록이라 댓글·검색·Git 내보내기가 그대로 됩니다.</p>
  {decks&&(decks.length===0?<p className="muted">아직 발표 자료가 없습니다.</p>:<ul className="team-commits">{decks.map(d=>{const deck=toDeck(d);return <li key={d.id}>
   <button className="text-button" onClick={()=>setOpen(deck)}><strong>{d.title}</strong></button><small>슬라이드 {deck.slides.length}장 · {String(d.updated_at).slice(0,10)}</small>
   <button className="text-button" onClick={()=>setPresent(deck.slides)}><Play size={13}/>발표</button><button className="text-button" onClick={()=>void downloadPptx(d.title,deck.slides).catch(e=>setError((e as Error).message))}><FileDown size={13}/>.pptx</button></li>})}</ul>)}
  {open&&<Editor workspace={workspace} start={open} onOpen={id=>{setOpen(null);onOpen(id)}} onClose={()=>{setOpen(null);void load()}}/>}
  {present&&<Presenter slides={present} start={0} onClose={()=>setPresent(null)}/>}
 </>;
}

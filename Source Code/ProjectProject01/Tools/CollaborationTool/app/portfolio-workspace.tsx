"use client";
import {useEffect,useState} from 'react';
import {copyText,randomId} from './browser-crypto';
import {readDocument,type ImportedDocument} from './import-documents';
import {readToolExport,type ToolPage} from './tool-import';
import {safeRequest} from './safe-request';
import type {Project} from './types';
import './ue5-tracker.css';

const categories=['퀘스트','방화 셔터','마네킹 모델 분리','멀티플레이','AI','데디케이티드 서버','성능·빌드 검증','기타'];
const sources=['작업일지 주장','커밋 확인','실행 검증 필요'];
const statuses=['추가 증거 필요','부분 확인','확인됨'];
const priorities=['P0','P1','P2'];
const kinds=['커밋','실행 로그','스크린샷','테스트 결과','문서'];
const fields={problem:'문제 상황',implementation:'구현 내용',contribution:'내 기여 범위',cause:'문제 원인',solution:'해결 과정',result:'검증 결과',review:'검토 근거와 확인한 범위',uncertainty:'남은 불확실성',next:'다음에 남길 증거',summary:'포트폴리오용 요약 문구',stack:'기술 스택',metrics:'수치 · 단위 · 측정 조건'};
type Evidence={kind:string;label:string;url:string};
type Claim=Record<keyof typeof fields,string>&{id:string;revision:number;title:string;category:string;source:string;status:string;priority:string;log:string;commit:string;evidence:Evidence[]};
type Log={id:string;title:string;day:string;body:string;version?:number};
type Attachment={id:string;name:string;content_type:string;title:string};
type Commit={hash:string;author:string;date:string;subject:string};
const blank=(log=''):Claim=>({
  id:randomId(),revision:0,title:'',category:categories[0],source:sources[0],status:statuses[0],priority:'P1',log,commit:'',evidence:[],
  problem:'',implementation:'',contribution:'',cause:'',solution:'',result:'',review:'',uncertainty:'',next:'',summary:'',stack:'',metrics:'',
});
function monday(value:string){
  const date=new Date(value+'T00:00:00Z');
  date.setUTCDate(date.getUTCDate()-(date.getUTCDay()+6)%7);
  return date.toISOString().slice(0,10);
}
const currentWeek=()=>monday(new Intl.DateTimeFormat('en-CA',{timeZone:'Asia/Seoul',year:'numeric',month:'2-digit',day:'2-digit'}).format(new Date()));

export default function PortfolioWorkspace({workspace,projects,onOpen,canWrite,canCreateProject}:{workspace:string;projects:Project[];onOpen:(id:string)=>void;canWrite:boolean;canCreateProject:boolean}){
  const [created,setCreated]=useState<Project[]>([]);
  const [name,setName]=useState('');
  const [unassigned,setUnassigned]=useState<Log[]>([]);
  const projectOptions=[...projects,...created.filter(item=>!projects.some(existing=>existing.id===item.id))];
  const [project,setProject]=useState('');
  const [week,setWeek]=useState(currentWeek);
  const [view,setView]=useState('review');
  const [claims,setClaims]=useState<Claim[]>([]);
  const [logs,setLogs]=useState<Log[]>([]);
  const [attachments,setAttachments]=useState<Attachment[]>([]);
  const [draft,setDraft]=useState<Claim|null>(null);
  const [notice,setNotice]=useState('');
  const [commits,setCommits]=useState<Commit[]>([]);
  const [imported,setImported]=useState<{file:File;document:ImportedDocument;day:string;id?:string}|null>(null);
  const [external,setExternal]=useState<{file:File;pages:ToolPage[]}|null>(null);
  const [busy,setBusy]=useState(false);
  const active=projectOptions.find(item=>item.id===project)??projectOptions.find(item=>!item.archived)??projectOptions[0];
  const projectId=active?.id??'';
  const query=new URLSearchParams({workspace,project:projectId,week});
  const scope=query.toString();
  const [loadedScope,setLoadedScope]=useState('');
  async function json<T>(url:string,body?:object):Promise<T>{
    const response=await safeRequest(url,body?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:undefined);
    const result=await response.json() as T&{error?:string};
    if(!response.ok)throw new Error(result.error||'요청을 완료하지 못했습니다.');
    return result;
  }
  async function refresh(signal?:{cancelled:boolean}){
    const data=await json<{claims:Claim[];logs:Log[];unassigned:Log[];attachments:Attachment[]}>('/api/features?action=portfolioList&'+scope);
    if(!signal?.cancelled){setClaims(data.claims);setLogs(data.logs);setUnassigned(data.unassigned);setAttachments(data.attachments);setLoadedScope(scope);}
  }
  useEffect(()=>{
    if(!projectId)return;
    const signal={cancelled:false};
    void refresh(signal).catch(error=>{if(!signal.cancelled)setNotice(error.message)});
    return ()=>{signal.cancelled=true};
  },[scope,projectId]);
  function edit(item:Claim|null){
    setDraft(item);setCommits([]);setNotice('');
    if(item)requestAnimationFrame(()=>document.getElementById('portfolio-editor')?.focus());
  }
  const patch=(values:Partial<Claim>)=>setDraft(item=>item?{...item,...values}:null);
  async function save(item:Claim){
    setBusy(true);setNotice('');
    try{
      await json('/api/features',{...item,workspace,project:projectId,week,action:'portfolioSave'});
      edit(null);
      await refresh();
      setNotice('주간 검토에 저장했습니다.');
    }catch(error){setNotice((error as Error).message)}
    finally{setBusy(false)}
  }
  async function createProject(){
    const title=name.trim();
    if(!title||busy)return;
    setBusy(true);setNotice('');
    try{
      const {id}=await json<{id:string}>('/api/data',{action:'project',workspace,name:title});
      setCreated(items=>[...items,{id,name:title,color:'#197f78',archived:0}]);
      setProject(id);setName('');setNotice('프로젝트를 만들고 선택했습니다.');
      window.dispatchEvent(new Event('daybook-saved'));
    }catch(error){setNotice((error as Error).message)}
    finally{setBusy(false)}
  }
  async function linkLog(log:Log){
    if(busy||active?.archived)return;
    setBusy(true);setNotice('');
    try{
      await json('/api/data',{action:'update',workspace,id:log.id,version:log.version,project_id:projectId});
      await refresh();
      edit({...blank(log.id),title:log.title});
      setNotice('작업일지를 프로젝트에 연결했습니다. 이어서 성과를 작성할 수 있습니다.');
      window.dispatchEvent(new Event('daybook-saved'));
    }catch(error){setNotice((error as Error).message)}
    finally{setBusy(false)}
  }
  async function chooseFile(file:File){
    setBusy(true);setNotice('');
    try{
      const document=await readDocument(file);
      if(!document.body.trim())throw new Error('문서에 읽을 내용이 없습니다.');
      setImported({file,document:{...document,title:document.title.slice(0,160)},day:week});
    }catch(error){setNotice((error as Error).message)}
    finally{setBusy(false)}
  }
  async function chooseExport(file:File){
    setBusy(true);setNotice('');
    try{
      const {pages}=await readToolExport([file]);
      setExternal({file,pages});
    }catch(error){setNotice((error as Error).message)}
    finally{setBusy(false)}
  }
  function selectPage(page:ToolPage){
    if(!external)return;
    if(!page.body.trim()||page.body.length>200000){
      setNotice('본문이 비어 있거나 너무 깁니다. 내보낸 페이지를 나눠 주세요.');return;
    }
    const title=page.title.slice(0,160);
    const original=external.file.size<=10*1024*1024&&/\.(zip|md|txt|csv)$/i.test(external.file.name);
    const file=original?external.file:new File([page.body],title+'.md',{type:'text/markdown'});
    setImported({file,document:{title,body:page.body,warning:original?undefined:'선택한 페이지의 추출본을 Markdown으로 첨부합니다.'},day:week});
    setExternal(null);setNotice('');
  }
  async function saveDocument(){
    if(!imported||busy)return;
    setBusy(true);setNotice('');
    let id=imported.id;
    try{
      if(!id){
        ({id}=await json<{id:string}>('/api/data',{action:'create',workspace,kind:'log',project_id:projectId,
          day:imported.day,title:imported.document.title,body:imported.document.body}));
        setImported({...imported,id});
        window.dispatchEvent(new Event('daybook-saved'));
      }
      const form=new FormData();
      form.set('entry',id);form.set('file',imported.file);form.set('comment','false');
      const response=await safeRequest('/api/file',{method:'POST',body:form});
      if(!response.ok){
        const error=await response.json() as {error?:string};
        throw new Error(error.error||'원본 첨부에 실패했습니다.');
      }
      const importedWeek=monday(imported.day);
      if(importedWeek===week)await refresh();
      else setWeek(importedWeek);
      setImported(null);setView('review');
      edit({...blank(id),title:imported.document.title});
      setNotice('문서를 작업일지로 가져오고 근거 파일을 첨부했습니다.');
      window.dispatchEvent(new Event('daybook-saved'));
    }catch(error){setNotice((id?'작업일지는 저장되었습니다. ':'')+(error as Error).message)}
    finally{setBusy(false)}
  }
  async function findCommits(log:string){
    setBusy(true);setCommits([]);
    try{
      const data=await json<{commits:Commit[];reason?:string}>('/api/features?action=commits&'+new URLSearchParams({workspace,entry:log}));
      setCommits(data.commits);
      setNotice(data.reason||(data.commits.length?'커밋 메시지에서 작업일지 코드를 찾았습니다. 변경 내용과 기여 범위를 검토한 뒤 연결하세요.':'작업일지 코드가 있는 커밋을 찾지 못했습니다.'));
    }catch(error){setNotice((error as Error).message)}
    finally{setBusy(false)}
  }
  const ordered=[...claims].sort((a,b)=>a.priority.localeCompare(b.priority));
  const confirmed=ordered.filter(item=>item.status==='확인됨');
  const pending=ordered.filter(item=>item.status!=='확인됨');
  const summary=[
    '# '+active?.name+' · '+week+' 주간 포트폴리오',
    '## 확인된 핵심 성과와 문제 해결',
    ...confirmed.map(item=>'- '+(item.summary||item.title)+'\n  - 기여 · '+(item.contribution||'미기록')+'\n  - 문제와 해결 · '+[item.problem,item.solution].filter(Boolean).join(' → ')+'\n  - 검증 · '+[item.result,item.review].filter(Boolean).join(' / ')+
      (item.commit?'\n  - 커밋 · '+item.commit:'')+item.evidence.map(link=>'\n  - '+link.kind+' · '+link.label+' · '+link.url).join('')),
    '## 기술 스택',...new Set(confirmed.flatMap(item=>item.stack.split(/[,\n]/).map(value=>value.trim()).filter(Boolean)).map(value=>'- '+value)),
    '## 검증된 수치',...confirmed.filter(item=>item.metrics&&item.source===sources[2]).map(item=>'- '+item.title+' · '+item.metrics),
    '## 보완할 증거',...pending.map(item=>'- '+item.priority+' '+item.title+' · '+item.status+' · '+(item.next||item.uncertainty||'다음 증거 미기록')),
  ].join('\n');
  const selector=(label:string,value:string,options:string[],change:(value:string)=>void)=><label>{label}<select value={value} onChange={e=>change(e.target.value)}>{options.map(option=><option key={option}>{option}</option>)}</select></label>;
  const card=(item:Claim)=><button type="button" key={item.id} className="secondary ue5-card" disabled={!!draft||!!imported||!!external||busy} onClick={()=>edit(item)}>
    <strong>{item.title}</strong><small>{item.priority} · {item.category} · {item.source} · {item.status}</small>
    {item.status!==statuses[2]&&<small>다음 증거 · {item.next||'미기록'}</small>}
  </button>;
  return <section className="ue5-tracker">
    <h3>포트폴리오 증거</h3>
    {notice&&<p role="status">{notice}</p>}
    {canCreateProject&&<form className="ue5-controls" onSubmit={e=>{e.preventDefault();void createProject()}}>
      <label>새 프로젝트 이름<input required maxLength={60} value={name} disabled={!!draft||!!imported||!!external||busy} onChange={e=>setName(e.target.value)}/></label>
      <button type="submit" className="secondary" disabled={!name.trim()||!!draft||!!imported||!!external||busy}>만들고 선택</button>
    </form>}
    {!active?<p>기록을 연결할 팀 프로젝트를 먼저 만들어 주세요.</p>:<>
      <div className="ue5-controls">
        <label>프로젝트<select value={projectId} disabled={!!draft||!!imported||!!external||busy} onChange={e=>{setNotice('');setProject(e.target.value)}}>{projectOptions.map(item=><option key={item.id} value={item.id}>{item.name}</option>)}</select></label>
        <label>검토 주차 (월요일)<input type="date" value={week} disabled={!!draft||!!imported||!!external||busy} onChange={e=>{if(e.target.value){setNotice('');setWeek(monday(e.target.value))}}}/></label>
        {canWrite&&!active.archived&&<label>작업 문서 가져오기
          <input type="file" accept=".docx,.xlsx,.pptx,.txt,.md,.csv" disabled={!!draft||!!imported||!!external||busy||loadedScope!==scope} onChange={e=>{
            const file=e.target.files?.[0];e.target.value='';if(file)void chooseFile(file);
          }}/>
        </label>}
        {canWrite&&!active.archived&&<label>외부 협업툴 내보내기 가져오기
          <input type="file" accept=".zip,.html,.htm,.md,.txt,.csv" disabled={!!draft||!!imported||!!external||busy||loadedScope!==scope} onChange={e=>{
            const file=e.target.files?.[0];e.target.value='';if(file)void chooseExport(file);
          }}/>
        </label>}
        {canWrite&&<button className="primary" disabled={!!draft||!!imported||!!external||busy||loadedScope!==scope} onClick={()=>edit(blank())}>성과 주장 추가</button>}
      </div>
      {external&&<section className="team-meeting">
        <h4>가져올 페이지 선택</h4>
        <label>{external.file.name}<select value="" onChange={e=>{
          if(e.target.value!=='')selectPage(external.pages[Number(e.target.value)]);
        }}>
          <option value="">페이지 선택</option>{external.pages.map((page,index)=><option key={page.path} value={index}>{page.title} · {page.path}</option>)}
        </select></label>
        <button className="secondary" type="button" onClick={()=>setExternal(null)}>닫기</button>
      </section>}
      {imported&&<form className="team-meeting ue5-editor" onSubmit={e=>{e.preventDefault();void saveDocument()}}>
        <h4>{imported.file.name} 가져오기</h4>
        <fieldset disabled={busy||!!imported.id}>
          <label>작업 날짜<input required type="date" value={imported.day} onChange={e=>setImported({...imported,day:e.target.value})}/></label>
          <label>작업일지 제목<input required maxLength={160} value={imported.document.title} onChange={e=>setImported({...imported,document:{...imported.document,title:e.target.value}})}/></label>
          <label>추출한 본문<textarea required rows={8} maxLength={200000} value={imported.document.body} onChange={e=>setImported({...imported,document:{...imported.document,body:e.target.value}})}/></label>
        </fieldset>
        {imported.document.warning&&<p>{imported.document.warning}</p>}
        <div className="inline">
          <button className="primary" type="submit" disabled={busy}>{imported.id?'원본 첨부 다시 시도':'작업일지로 저장하고 성과 작성'}</button>
          <button className="secondary" type="button" disabled={busy} onClick={()=>{setImported(null);if(imported.id)void refresh().catch(error=>setNotice(error.message))}}>닫기</button>
        </div>
      </form>}
      {loadedScope===scope&&<>
        <div className="ue5-stats">{statuses.map(status=><div key={status}><strong>{claims.filter(item=>item.status===status).length}</strong>{status}</div>)}</div>
        <nav className="segmented" aria-label="포트폴리오 화면">{Object.entries({review:'주간 검토',board:'우선순위 보드',export:'포트폴리오 요약'}).map(([key,label])=><button key={key} className={view===key?'selected':''} onClick={()=>setView(key)}>{label}</button>)}</nav>
        {view==='review'&&<>
          <h4>이번 주 협업툴 작업일지·문서</h4>
          {!logs.length&&<p>선택한 프로젝트와 주차에 연결된 작업일지가 없습니다.</p>}
          {logs.map(log=><details key={log.id}><summary>{log.day} · {log.title}</summary><p>{log.body}</p>
            <div className="inline"><button className="secondary" onClick={()=>onOpen(log.id)}>원본·첨부 열기</button>
              {canWrite&&<button className="secondary" disabled={!!draft||!!imported||!!external||busy} onClick={()=>edit({...blank(log.id),title:log.title})}>이 일지에서 주장 추가</button>}</div>
          </details>)}
          {canWrite&&!active.archived&&unassigned.length>0&&<section>
            <h4>이번 주 프로젝트 미지정 작업일지·문서</h4>
            {unassigned.map(log=><details key={log.id}><summary>{log.day} · {log.title}</summary><p>{log.body}</p>
              <button type="button" className="secondary" disabled={!!draft||!!imported||!!external||busy} onClick={()=>void linkLog(log)}>이 프로젝트에 연결하고 성과 작성</button>
            </details>)}
          </section>}
          {sources.map(source=><section key={source}><h4>{source}</h4>{ordered.filter(item=>item.source===source).map(card)}</section>)}
        </>}
        {view==='board'&&<div className="ue5-grid">{priorities.map(priority=><section key={priority}><h4>{priority}</h4>{pending.filter(item=>item.priority===priority).map(card)}</section>)}</div>}
        {view==='export'&&<><textarea aria-label="주간 포트폴리오 요약" readOnly rows={16} value={summary}/>
          <button className="secondary" onClick={()=>void copyText(summary).then(()=>setNotice('요약을 복사했습니다.'),()=>setNotice('클립보드에 복사하지 못했습니다.'))}>Markdown 요약 복사</button></>}
      </>}
    </>}
    {draft&&<form className="team-meeting ue5-editor" onSubmit={e=>{e.preventDefault();if(canWrite&&!busy)void save(draft)}}>
      <h4 id="portfolio-editor" tabIndex={-1}>{draft.title||'새 성과 주장'}</h4>
      <fieldset disabled={!canWrite||busy}>
        <div className="ue5-controls"><label>주장 제목<input required maxLength={200} value={draft.title} onChange={e=>patch({title:e.target.value})}/></label>
          {selector('분류',draft.category,categories,category=>patch({category}))}
          {selector('주장 유형',draft.source,sources,source=>patch({source}))}
          {selector('검증 수준',draft.status,statuses,status=>patch({status}))}
          {selector('우선순위',draft.priority,priorities,priority=>patch({priority}))}
        </div>
        <div className="ue5-grid">{Object.entries(fields).map(([key,label])=><label key={key}>{label}<textarea maxLength={5000} rows={3} value={draft[key as keyof typeof fields]} onChange={e=>patch({[key]:e.target.value})}/></label>)}</div>
        <h4>근거 자료</h4>
        <div className="ue5-controls"><label>작업일지<select value={draft.log} onChange={e=>{patch({log:e.target.value});setCommits([])}}><option value="">미연결</option>
          {draft.log&&!logs.some(log=>log.id===draft.log)&&<option value={draft.log}>이전 주차 또는 삭제된 일지</option>}
          {logs.map(log=><option key={log.id} value={log.id}>{log.day} · {log.title}</option>)}
        </select></label>
        {draft.log&&<button type="button" className="secondary" onClick={()=>void findCommits(draft.log)}>연결 커밋 찾기</button>}
        <label>Git 커밋 ID<input value={draft.commit} maxLength={64} onChange={e=>patch({commit:e.target.value})}/></label></div>
        {commits.map(commit=><button key={commit.hash} type="button" className="secondary" onClick={()=>patch({commit:commit.hash})}>{commit.hash} · {commit.date} · {commit.author} · {commit.subject}</button>)}
        {attachments.length>0&&<label>이번 주 프로젝트 첨부에서 근거 선택
          <select value="" disabled={draft.evidence.length>=30} onChange={e=>{
            const file=attachments.find(item=>item.id===e.target.value);
            if(file)patch({evidence:[...draft.evidence,{kind:file.content_type.startsWith('image/')?'스크린샷':'문서',label:file.name,url:'/api/file?id='+file.id}]});
          }}>
            <option value="">첨부 선택</option>{attachments.filter(file=>!draft.evidence.some(link=>link.url==='/api/file?id='+file.id)).map(file=><option key={file.id} value={file.id}>{file.title} · {file.name}</option>)}
          </select>
        </label>}
        {draft.evidence.map((link,index)=>{
          const change=(value:Partial<Evidence>)=>patch({evidence:draft.evidence.map((row,i)=>i===index?{...row,...value}:row)});
          return <div className="ue5-controls" key={index}>
            {selector('근거 유형',link.kind,kinds,kind=>change({kind}))}
            <label>자료 이름<input required maxLength={200} value={link.label} onChange={e=>change({label:e.target.value})}/></label>
            <label>근거 링크<input required maxLength={2000} value={link.url} onChange={e=>change({url:e.target.value})}/></label>
            <button type="button" className="secondary" onClick={()=>patch({evidence:draft.evidence.filter((_,i)=>i!==index)})}>근거 제거</button>
          </div>;
        })}
        <button type="button" className="secondary" disabled={draft.evidence.length>=30} onClick={()=>patch({evidence:[...draft.evidence,{kind:kinds[0],label:'',url:''}]})}>근거 링크 추가</button>
      </fieldset>
      <div className="inline">
        {draft.log&&<button type="button" className="secondary" onClick={()=>onOpen(draft.log)}>일지·첨부 열기</button>}
        {draft.evidence.filter(link=>/^(?:https?:\/\/[^\s]+|\/api\/file\?id=[a-z0-9-]+)$/i.test(link.url)).map((link,i)=><a key={i} href={link.url} target="_blank" rel="noopener noreferrer">{link.label||link.kind} 열기</a>)}
        {canWrite&&<button className="primary" disabled={busy} type="submit">저장</button>}
        <button type="button" className="secondary" disabled={busy} onClick={()=>edit(null)}>닫기</button>
      </div>
    </form>}
  </section>;
}

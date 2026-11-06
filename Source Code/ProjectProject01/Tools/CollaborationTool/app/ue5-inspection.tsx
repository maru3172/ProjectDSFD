"use client";
import {useState} from 'react';
import {safeRequest} from './safe-request';
import {reportMetadata} from './ue5-report-metadata';

type Report={label:string;updated:string;output:string;truncated?:boolean;metadata?:ReturnType<typeof reportMetadata>};
type Asset={file:string;staged?:string;working?:string;incoming?:string;overlap:boolean;committed:boolean};
type Risks={upstream:string|null;overlaps:string[]|null;conflicts:string[];assets:Asset[];suspicious:string[];unstaged:string[]};
type Locks={conflicts:{path:string;lock:{holder:string}}[];unlocked:string[]};
const changeLabel=(value?:string)=>value?({A:'추가',M:'수정',D:'삭제',T:'유형 변경',U:'충돌','??':'미추적'}[value]??value):'—';
type Inspection={root:string;commit:string;status:string;project:boolean;checks:{label:string;ok:boolean}[];reports:Report[];risks:Risks|null;locks:Locks|null};

export default function Ue5Inspection({workspace,onEvidence}:{workspace:string;onEvidence?:(logs:string,metadata:ReturnType<typeof reportMetadata>)=>void}) {
  const [snapshot,setSnapshot]=useState<Inspection|null>(null);
  const [report,setReport]=useState<Report|null>(null);
  const [busy,setBusy]=useState(false);
  const [error,setError]=useState('');
  async function refresh(){
    setBusy(true);
    setError('');
    setSnapshot(null);
    try{
      const response=await safeRequest('/api/features?action=ue5Inspection&workspace='+encodeURIComponent(workspace));
      const body=await response.json() as Inspection & {error?:string};
      if(!response.ok)throw new Error(body.error||'저장소 상태를 읽지 못했습니다.');
      setSnapshot(body);
    }catch(cause){setError((cause as Error).message)}
    finally{setBusy(false)}
  }
  async function importReport(file:File){
    setError('');
    try{
      if(file.size>5*1024*1024)throw new Error('5MB 이하의 검사 보고서를 선택하세요.');
      const output=await file.text();
      setReport({label:file.name,updated:new Date(file.lastModified).toISOString(),output:output.slice(-16000),metadata:reportMetadata(output)});
    }catch(cause){setError((cause as Error).message)}
  }
  return <section className="ue5-tracker">
    <h4>프로젝트 검사와 Git 상태</h4>
    <p>서버 PC에 연결된 Git 저장소의 상태, UE5 빌드 전 점검 결과와 최근 검사 로그를 확인합니다.</p>
    <div className="ue5-controls">
      <button className="secondary" disabled={busy} onClick={()=>void refresh()}>{busy?'확인 중…':'저장소·빌드 상태 확인'}</button>
      <label>기능 검사·진단 보드 보고서 열기
        <input type="file" accept=".log,.txt,.json,.xml,.csv" onChange={e=>{
          const file=e.target.files?.[0];
          e.target.value='';
          if(file)void importReport(file);
        }}/>
      </label>
    </div>
    {error&&<p role="alert">{error}</p>}
    {snapshot&&<>
      <details open><summary>Git · {snapshot.status.split('\n')[0]?.replace(/^## /,'')}</summary>
        <p>{snapshot.root}</p><p>최근 커밋 · {snapshot.commit||'아직 커밋 없음'}</p>
        <pre className="ue5-output">{snapshot.status.split('\n').slice(1).join('\n')||'작업 폴더에 변경 없음'}</pre>
      </details>
      <details open><summary>커밋 전 경고</summary>
        {snapshot.risks?<>
          <p>원격 비교는 마지막으로 가져온 {snapshot.risks.upstream||'추적 브랜치'} 기준입니다.</p>
          {snapshot.risks.overlaps===null&&<p>추적 브랜치 또는 공통 커밋이 없어 충돌 후보를 비교할 수 없습니다.</p>}
          {[
            ['해결되지 않은 충돌',snapshot.risks.conflicts],
            ['양쪽에서 변경한 파일 · 충돌 후보',snapshot.risks.overlaps??[]],
            ['커밋에 들어갈 민감 파일명·생성물 후보',snapshot.risks.suspicious],
            ['스테이징 후 추가로 수정한 파일',snapshot.risks.unstaged],
            ['다른 팀원이 잠근 파일',snapshot.locks?.conflicts.map(lock=>lock.path+' · '+lock.lock.holder)??[]],
            ['잠금 없이 변경한 UE 에셋',snapshot.locks?.unlocked??[]],
          ].map(([label,files])=>{
            const entries=files as string[];
            return <details key={String(label)} open={entries.length>0}><summary>{label} · {entries.length}개</summary>
              <ul>{entries.map(file=><li key={file}>{file}</li>)}</ul>
            </details>;
          })}
          {!snapshot.locks&&<p>잠금 비교에 사용할 변경 목록이 없거나 너무 큽니다.</p>}
        </>:<p>커밋 전 위험을 계산하지 못했습니다.</p>}
      </details>
      {snapshot.risks&&<details open><summary>UE 에셋별 차이 · {snapshot.risks.assets.length}개</summary>
        <p>Git 기준으로 .uasset·.umap의 파일 변경을 비교합니다. 에셋 내부 속성은 UE 에디터의 Diff에서 확인할 수 있습니다.</p>
        <div className="ue5-output"><table>
          <thead><tr><th>에셋</th><th>커밋 예정</th><th>작업 폴더</th><th>로컬 커밋</th><th>추적 브랜치</th><th>경고</th></tr></thead>
          <tbody>{snapshot.risks.assets.map(asset=><tr key={asset.file}>
            <td>{asset.file}</td><td>{changeLabel(asset.staged)}</td><td>{changeLabel(asset.working)}</td>
            <td>{asset.committed?'변경':'—'}</td><td>{changeLabel(asset.incoming)}</td>
            <td>{snapshot.risks?.conflicts.includes(asset.file)?'미해결 충돌':asset.overlap?'양쪽 변경':'—'}</td>
          </tr>)}</tbody>
        </table></div>
      </details>}
      <details open><summary>UE5 빌드 전 점검</summary>
        {snapshot.project?<ul>{snapshot.checks.map(check=><li key={check.label}>{check.ok?'확인':'점검 필요'} · {check.label}</li>)}</ul>:
          <p>연결된 저장소에서 ProjectDFSD.uproject를 찾지 못했습니다.</p>}
        {snapshot.project&&!snapshot.reports.length&&<p>기본 경로에 SafeBuild 또는 스모크 테스트 로그가 없습니다.</p>}
      </details>
    </>}
    {[...(snapshot?.reports??[]),...(report?[report]:[])].map((item,index)=>{const metadata=item.metadata??reportMetadata(item.truncated?'':item.output);return <details key={index}>
      <summary>{item.label} · {new Date(item.updated).toLocaleString('ko-KR',{timeZone:'Asia/Seoul'})}</summary>
      <pre className="ue5-output">{item.output}</pre>
      {(metadata.version||metadata.testedCommit)&&<p>보고서에 명시된 값 · 빌드 {metadata.version||'—'} · 검사 대상 커밋 {metadata.testedCommit||'—'}</p>}
      {metadata.ambiguous&&<p>같은 항목에 값이 여러 개 있어 해당 칸은 직접 확인해야 합니다.</p>}
      {onEvidence&&<button className="secondary" onClick={()=>onEvidence((item.label+' · '+item.updated+'\n'+item.output.slice(-4700)).slice(0,5000),metadata)}>검증 기록 초안에 담기</button>}
    </details>})}
  </section>;
}

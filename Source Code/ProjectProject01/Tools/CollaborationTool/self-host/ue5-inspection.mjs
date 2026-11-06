import {execFile} from 'node:child_process';
import {access, open, readdir} from 'node:fs/promises';
import path from 'node:path';
import {promisify} from 'node:util';
import {Problem} from './runtime.mjs';

const exec = promisify(execFile);
const exists = file => access(file).then(()=>true,()=>false);
const git = async (root,args) => (await exec('git',['--no-optional-locks','-c','core.quotepath=false','-C',root,...args],{
  windowsHide:true,timeout:15000,maxBuffer:4*1024*1024,
})).stdout.trimEnd();

const paths = value => value.split('\0').filter(Boolean);

async function commitRisks(root) {
  const [stagedChanges, workingChanges, untracked, conflicts, upstream] = await Promise.all([
    git(root,['diff','--cached','--name-status','--no-renames','-z']),
    git(root,['diff','--name-status','--no-renames','-z']),
    git(root,['ls-files','--others','--exclude-standard','-z']),
    git(root,['diff','--name-only','--diff-filter=U','-z']),
    git(root,['rev-parse','--symbolic-full-name','@{upstream}']).catch(()=>null),
  ]);
  const changes = value => {
    const entries = paths(value);
    const result = new Map();
    for(let index=0;index+1<entries.length;index+=2)result.set(entries[index+1],entries[index]);
    return result;
  };
  const staged = changes(stagedChanges);
  const working = changes(workingChanges);
  const newFiles = new Set(paths(untracked));
  const local = new Set([...staged.keys(),...working.keys(),...newFiles]);
  let remote = new Map();
  const base = upstream && await git(root,['merge-base','HEAD',upstream]).catch(()=>null);
  let overlaps = null;
  let committed = new Set();
  if(base){
    const [incoming, outgoing] = await Promise.all([
      git(root,['diff','--name-status','--no-renames','-z',base,upstream]),
      git(root,['diff','--name-only','--no-renames','-z',base,'HEAD']),
    ]);
    committed=new Set(paths(outgoing));
    for(const file of committed)local.add(file);
    remote=changes(incoming);
    overlaps=[...remote.keys()].filter(file=>local.has(file));
  }
  const overlapSet=new Set(overlaps);
  return {
    upstream, overlaps, conflicts:paths(conflicts),
    assets:[...new Set([...local,...remote.keys()])].filter(file=>/\.(uasset|umap)$/i.test(file)).sort().map(file=>({
      file, staged:staged.get(file), working:working.get(file)??(newFiles.has(file)?'??':undefined),
      incoming:remote.get(file), overlap:overlapSet.has(file),
      committed:committed.has(file),
    })),
    suspicious:[...staged.keys()].filter(file=>/(?:^|\/)(?:\.env(?:\.[^/]+)?|[^/]+\.(?:pfx|p12|pem|key)|id_(?:rsa|ed25519)|credentials(?:\.json)?|Binaries|Intermediate|Saved|DerivedDataCache|node_modules)(?:\/|$)/i.test(file)),
    unstaged:[...working.keys()].filter(file=>staged.has(file)),
  };
}
async function readTail(file,label) {
  const handle = await open(file,'r').catch(error=>{if(error.code!=='ENOENT')throw error;return null});
  if(!handle)return null;
  try {
    const info=await handle.stat();
    if(!info.isFile())return null;
    const header=Buffer.alloc(2);
    await handle.read(header,0,header.length,0);
    const utf16=header[0]===255&&header[1]===254;
    const offset=Math.max(0,info.size-16000);
    const start=utf16?offset-offset%2:offset;
    const buffer=Buffer.alloc(info.size-start);
    const {bytesRead}=await handle.read(buffer,0,buffer.length,start);
    return {label,truncated:start>0,updated:info.mtime.toISOString(),output:buffer.subarray(0,bytesRead).toString(utf16?'utf16le':'utf8').replace(/^\uFEFF/,'')};
  } finally {await handle.close()}
}

export async function ue5Inspection(linkedRoot,loopback) {
  if(!loopback)throw new Problem(403,'연결된 저장소와 로컬 검사 로그는 서버 PC에서 열 수 있습니다.');
  if(!linkedRoot)throw new Problem(400,'이 PC·Git에서 프로젝트 Git 폴더를 먼저 연결하세요.');
  let root,status,commit;
  try {
    root=await git(linkedRoot,['rev-parse','--show-toplevel']);
    [status,commit]=await Promise.all([
      git(root,['status','--short','--branch','--untracked-files=all']),
      git(root,['log','-1','--format=%h %s']).catch(()=>''),
    ]);
  } catch {throw new Problem(400,'Git 상태를 읽지 못했습니다. 연결된 저장소와 Git 설치를 확인하세요.')}
  const candidates=[root,path.join(root,'Source Code','ProjectProject01'),path.join(root,'ProjectDSFD','Source Code','ProjectProject01')];
  const projects=await Promise.all(candidates.map(folder=>exists(path.join(folder,'ProjectDFSD.uproject'))));
  const project=candidates[projects.indexOf(true)];
  const checks=[];
  const reports=[];
  if(project){
    const paths=[
      ['에디터 타깃','Source/ProjectDFSDEditor.Target.cs'],
      ['빌드 보호 검사','Tools/BuildGuard/BuildStepGuard.ps1'],
      ['안전 빌드 스크립트','Tools/BuildGuard/SafeBuild.ps1'],
      ['기능 검사 프로젝트','Tools/ProjectProject01FeatureChecks/ProjectProject01FeatureChecks.csproj'],
      ['진단 보드 프로젝트','Tools/ProjectProject01DiagnosticsDashboard/ProjectProject01DiagnosticsDashboard.csproj'],
    ];
    checks.push(...await Promise.all(paths.map(async ([label,file])=>({label,ok:await exists(path.join(project,file))}))));
    if(process.platform==='win32'){
      const processes=await exec('tasklist.exe',['/FO','CSV','/NH'],{windowsHide:true,timeout:5000,maxBuffer:2*1024*1024}).catch(()=>null);
      const active=processes?.stdout.match(/^"(?:UnrealEditor(?:-Cmd)?|LiveCodingConsole|UnrealBuildTool|cl|link|UbaHost)\.exe"/gim);
      checks.push({label:processes?(active?'실행 중인 빌드 관련 프로세스 · '+[...new Set(active)].join(', '):'빌드 관련 프로세스 없음'):'빌드 관련 프로세스 확인 불가',ok:!!processes&&!active});
    }
    const diagnostics=path.join(project,'Saved','Diagnostics');
    const folders=await readdir(diagnostics,{withFileTypes:true}).catch(error=>{if(error.code!=='ENOENT')throw error;return []});
    const smoke=folders.filter(item=>item.isDirectory()&&item.name.startsWith('Smoke_')).map(item=>item.name).sort().at(-1);
    reports.push(...(await Promise.all([
      readTail(path.join(project,'Saved','Logs','SafeBuild.log'),'SafeBuild 로그'),
      smoke?readTail(path.join(diagnostics,smoke,'SmokeTest.log'),'스모크 테스트 · '+smoke):null,
    ])).filter(Boolean));
  }
  return {root,commit,status,project:!!project,checks,reports,risks:await commitRisks(root).catch(()=>null)};
}

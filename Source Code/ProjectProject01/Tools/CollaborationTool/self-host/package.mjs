import {mkdir,cp,copyFile,writeFile,readFile,stat} from 'node:fs/promises';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const root=process.cwd(),out=path.resolve(process.argv[2]??'outputs/Daybook-Windows');await mkdir(out,{recursive:true});
const run=(cmd,args)=>{const p=spawnSync(cmd,args,{stdio:'inherit',shell:false});if(p.status!==0)throw new Error('배포 빌드 실패: '+cmd)};
run(process.execPath,['self-host/build.mjs']);run(process.execPath,['--input-type=module','-e',"await import('./self-host/server.mjs')"]);run('dotnet',['publish','self-host/launcher/Daybook.csproj','-c','Release','-r','win-x64','--self-contained','true','-p:PublishSingleFile=true','-p:IncludeNativeLibrariesForSelfExtract=true','-o',path.join(out,'launcher-build')]);
await copyFile(path.join(out,'launcher-build','Daybook.exe'),path.join(out,'Daybook.exe'));await mkdir(path.join(out,'runtime'),{recursive:true});await copyFile(process.execPath,path.join(out,'runtime','node.exe'));
await cp('drizzle',path.join(out,'app','drizzle'),{recursive:true,filter:p=>!p.includes(path.sep+'meta')});await cp('self-host',path.join(out,'app','self-host'),{recursive:true,filter:p=>!p.includes(path.sep+'launcher')&&!p.endsWith('package.mjs')&&!p.endsWith('build.mjs')&&!p.endsWith('client.tsx')&&!p.endsWith('desktop.css')});
await writeFile(path.join(out,'app','package.json'),JSON.stringify({type:'module',private:true,name:'daybook-runtime',version:'1.0.0'}));
await writeFile(path.join(out,'먼저 읽기.txt'),'Daybook.exe를 실행하세요. Node·Unreal·유료 호스팅 없이 동작합니다.\r\n자료는 %LOCALAPPDATA%/DSFD/Daybook 에 저장됩니다. 프로그램 업데이트 시 자료 폴더를 지우거나 덮어쓰지 마세요.\r\nGit 작업 폴더는 기록실 → 업무·자료 관리 → 이 PC·Git에서 지정하세요.\r\n공동 작업은 운영자가 공동 서버를 켰을 때 접속합니다. 운영자 비밀번호·계정 승인·공간 초대 승인을 거칩니다.\r\n전체 사용법은 Docs/독립실행_사용법.md를 참고하세요.\r\n');
await cp('Docs',path.join(out,'Docs'),{recursive:true});
await mkdir(path.join(out,'licenses'),{recursive:true});
for(const [from,to] of [['node_modules/react/LICENSE','react.txt'],['node_modules/react-dom/LICENSE','react-dom.txt'],['node_modules/lucide-react/LICENSE','lucide.txt'],['node_modules/fflate/LICENSE','fflate.txt']])await copyFile(from,path.join(out,'licenses',to));
for(const name of ['LICENSE.txt','ThirdPartyNotices.txt'])await copyFile(path.join(process.env.ProgramFiles??'C:/Program Files','dotnet',name),path.join(out,'licenses','dotnet-'+name));
const nodeLicense=path.join(path.dirname(process.execPath),'LICENSE');try{await copyFile(nodeLicense,path.join(out,'licenses','node.txt'))}catch{const r=await fetch('https://raw.githubusercontent.com/nodejs/node/v24.22.1/LICENSE');if(!r.ok)throw new Error('Node 라이선스를 가져올 수 없습니다.');await writeFile(path.join(out,'licenses','node.txt'),await r.text())}
await copyFile('self-host/THIRD-PARTY-NOTICES.txt',path.join(out,'THIRD-PARTY-NOTICES.txt'));
const files=[];async function scan(folder){const {readdir}=await import('node:fs/promises');for(const e of await readdir(folder,{withFileTypes:true})){if(e.name==='launcher-build'||e.name==='DaybookData'||e.name==='manifest.json')continue;const file=path.join(folder,e.name);if(e.isDirectory())await scan(file);else{const bytes=await readFile(file);files.push({file:path.relative(out,file).replaceAll('\\','/'),size:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')})}}}await scan(out);await writeFile(path.join(out,'manifest.json'),JSON.stringify({format:'daybook-program',version:1,files},null,2));console.log('배포본: '+out);

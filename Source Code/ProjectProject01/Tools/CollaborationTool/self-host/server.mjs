import http from 'node:http';
import https from 'node:https';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {readFile,writeFile,stat,unlink} from 'node:fs/promises';
import {randomBytes} from 'node:crypto';
import {openStore,closeStore,context,settings,dataRoot,Problem,q,run,json,log,sha,requireUser,membership} from './runtime.mjs';
import {authenticate,authRoute,bootstrapToken,sessionResponse} from './auth.mjs';
import {featureRoute,augmentData,preflight,jobs} from './features.mjs';
import {directFileRoute} from './contacts.mjs';
import {vaultRoute,queueExport} from './vault.mjs';
import {backupRoute,scheduledBackup,applyPendingRestore} from './backup.mjs';
const project=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
async function writeChunk(response,chunk){if(response.destroyed||response.writableEnded)throw new Problem(499,'응답 연결이 종료되었습니다.');if(response.write(chunk))return;await new Promise((resolve,reject)=>{const clear=()=>{response.off('drain',drain);response.off('close',closed);response.off('error',failed)},drain=()=>{clear();resolve()},closed=()=>{clear();reject(new Problem(499,'응답 연결이 종료되었습니다.'))},failed=error=>{clear();reject(error)};response.once('drain',drain);response.once('close',closed);response.once('error',failed);if(response.destroyed)closed()})}
export async function startServer(options={}){
 const root=options.data??(process.env.LOCALAPPDATA?path.join(process.env.LOCALAPPDATA,'DSFD','Daybook'):path.join(project,'..','DaybookData'));applyPendingRestore(root);openStore(root,project);
 const legacy=await import('./build/legacy-routes.mjs');
 const launchToken=randomBytes(32).toString('hex'),limits=new Map(),inflight=new Map();let active=0,stopping=false;
 if(options.desktop&&!q('SELECT id FROM local_users WHERE operator=1'))run("INSERT INTO local_users(id,username,name,email,status,operator,created_at) VALUES(?,?,?,?, 'active',1,?)",crypto.randomUUID(),'local-owner','내 기록실','local@daybook.invalid',new Date().toISOString());
 const host=options.host??settings.host,port=options.port??settings.port;
 if(host!=='127.0.0.1'&&host!=='::1'&&!q('SELECT id FROM local_users WHERE operator=1 AND password_hash IS NOT NULL'))throw new Error('공동 서버를 열기 전에 운영자 비밀번호를 설정하세요.');
 const tls=settings.tlsCert&&settings.tlsKey?{cert:await readFile(settings.tlsCert),key:await readFile(settings.tlsKey)}:null;
 async function route(request,url){
  if(url.pathname==='/api/control'&&request.method==='POST'){const value=await request.json();if(!context.getStore().loopback||value.token!==launchToken||value.action!=='stop')throw new Problem(403,'로컬 프로그램 제어 권한이 없습니다.');setTimeout(()=>{void(options.onStop?options.onStop():close())},30).unref();return json({ok:true})}
  if(url.pathname==='/api/auth/launch'&&options.desktop&&request.method==='POST'){const value=await request.json();if(!context.getStore().loopback||value.token!==launchToken)throw new Problem(403,'이 PC에서 프로그램을 다시 실행하세요.');return sessionResponse(q('SELECT * FROM local_users WHERE operator=1'),!!tls)}
  if(url.pathname.startsWith('/api/auth/'))return authRoute(request,url);
  if(url.pathname==='/health')return json({ok:true});
  if(url.pathname.startsWith('/api/')){
   const account=requireUser();if(account.mustChange)throw new Problem(403,'임시 비밀번호를 먼저 변경하세요.');
   if(url.pathname==='/api/features')return featureRoute(request,url);
   if(url.pathname==='/api/direct-file')return directFileRoute(request,url);
   if(url.pathname==='/api/vault')return vaultRoute(request,url);
   if(url.pathname==='/api/backup')return backupRoute(request,url);
   const name=({'/api/data':'data','/api/file':'file','/api/access':'access','/api/presence':'presence','/api/local-files':'localFiles'})[url.pathname],handler=legacy[name]?.[request.method];
   if(!handler)throw new Problem(404,'요청한 기능이 없습니다.');
   if(request.method!=='GET')await preflight(request.clone(),url);if(url.pathname==='/api/data'&&request.method==='POST'){const value=await request.clone().json();if(value.action==='comment')context.getStore().mentions=value.mentions??[];if(value.action==='member'&&value.role==='remove')return featureRoute(new Request(request.url,{method:'POST',headers:request.headers,body:JSON.stringify({...value,action:'removeMember',target:value.userId})}),url)}
   if(url.pathname==='/api/file'&&request.method==='GET'){const file=q('SELECT * FROM attachments WHERE id=?',url.searchParams.get('id'));if(file?.deleted_at){membership(file.workspace_id);const next=q("WITH RECURSIVE versions(id,deleted_at) AS (SELECT id,deleted_at FROM attachments WHERE previous_id=? UNION SELECT a.id,a.deleted_at FROM attachments a JOIN versions v ON a.previous_id=v.id) SELECT id FROM versions WHERE deleted_at IS NULL LIMIT 1",file.id);if(next)return new Response(null,{status:302,headers:{Location:'/api/file?id='+next.id}});throw new Problem(410,'첨부가 휴지통에 있습니다. 자료 관리에서 복원하세요.')} }
   if(url.pathname==='/api/file'&&request.method==='POST'){
    const form=await request.clone().formData(),file=form.get('file'),entry=form.get('entry');if(file instanceof File){const hash=sha(Buffer.from(await file.arrayBuffer())),duplicate=q('SELECT id,name FROM attachments WHERE entry_id=? AND content_hash=? AND deleted_at IS NULL',entry,hash);if(duplicate)return json({...duplicate,duplicate:true});const response=await handler(request);if(response.ok){const result=await response.clone().json();run('UPDATE attachments SET content_hash=? WHERE id=?',hash,result.id)}return response}
   }
   const response=await handler(request);return url.pathname==='/api/data'&&request.method==='GET'?augmentData(response,url):response;
  }
  if(url.pathname==='/signout-with-chatgpt'){const response=await authRoute(new Request(url.origin+'/api/auth/logout',{method:'POST',body:'{}'}),new URL(url.origin+'/api/auth/logout'));return new Response(null,{status:303,headers:{Location:'/', 'Set-Cookie':response.headers.get('Set-Cookie')}})}
  if(!['GET','HEAD'].includes(request.method))throw new Problem(405,'지원하지 않는 요청입니다.');
  const file=({'/':'index.html','/app.js':'app.js','/app.css':'app.css'})[url.pathname];if(!file)throw new Problem(404,'페이지가 없습니다.');
  return new Response(request.method==='HEAD'?null:await readFile(path.join(project,'self-host','build','public',file)),{headers:{'Content-Type':file.endsWith('.js')?'text/javascript; charset=utf-8':file.endsWith('.css')?'text/css; charset=utf-8':'text/html; charset=utf-8'}});
 }
 async function handle(req,res){active++;const cancelled=new AbortController();req.once('aborted',()=>cancelled.abort());res.once('close',()=>cancelled.abort());res.on('error',()=>{});try{
  if(stopping)throw new Problem(503,'서버가 종료 중입니다.');
  const loopback=['127.0.0.1','::1','::ffff:127.0.0.1'].includes(req.socket.remoteAddress),authority=req.headers.host??'';
  if(!/^(localhost|127\.0\.0\.1|\[::1\]|\d{1,3}(?:\.\d{1,3}){3})(:\d+)?$/.test(authority))throw new Problem(403,'접속 주소를 확인하세요.');
  const url=new URL((tls?'https':'http')+'://'+authority+req.url);
  if(req.headers.origin&&req.headers.origin!==url.origin||req.headers['sec-fetch-site']==='cross-site')throw new Problem(403,'다른 사이트의 요청은 허용하지 않습니다.');
  if(!['GET','HEAD'].includes(req.method)&&!req.headers.origin&&req.headers['sec-fetch-mode'])throw new Problem(403,'브라우저 요청 출처를 확인하세요.');
  const ip=req.socket.remoteAddress,at=Date.now(),limit=limits.get(ip)??{at,n:0};if(at-limit.at>60000){limit.at=at;limit.n=0}if(++limit.n>900||active>64)throw new Problem(429,'요청이 많습니다. 잠시 후 다시 시도하세요.');limits.set(ip,limit);if(limits.size>10000)for(const [key,value] of limits)if(at-value.at>60000)limits.delete(key);
  const account=authenticate(new Request(url,{headers:req.headers}));if(url.pathname.startsWith('/api/')&&!url.pathname.startsWith('/api/auth/')&&url.pathname!=='/api/control'&&!account)throw new Problem(401,'로그인이 필요합니다.');if(url.pathname==='/api/backup'&&(!account?.operator||!loopback))throw new Problem(403,'운영 PC의 관리자만 백업·복원할 수 있습니다.');
  const max=url.pathname==='/api/backup'?256*1024*1024:url.pathname==='/api/vault'?32*1024*1024:url.pathname==='/api/file'||url.pathname==='/api/direct-file'?11*1024*1024:2*1024*1024;let size=0;const chunks=[];
  if(Number(req.headers['content-length']??0)>max)throw new Problem(413,'요청 용량 제한을 초과했습니다.');
  for await(const chunk of req){size+=chunk.length;if(size>max)throw new Problem(413,'요청 용량 제한을 초과했습니다.');chunks.push(chunk)}
  const request=new Request(url,{method:req.method,headers:req.headers,body:chunks.length?Buffer.concat(chunks):undefined,signal:cancelled.signal});
  const response=await context.run({user:account,ip,loopback,request},async()=>{
   const key=request.headers.get('Idempotency-Key'),mutates=request.method==='POST'&&!url.pathname.startsWith('/api/auth/')&&!['/api/presence','/api/backup','/api/vault'].includes(url.pathname);
   if(!mutates||!key)return route(request,url);
   requireUser();if(!/^[a-zA-Z0-9-]{16,100}$/.test(key))throw new Problem(400,'재시도 키가 올바르지 않습니다.');
   const identity=account.userId+':'+key;let fingerprint;if(url.pathname==='/api/file'){const form=await request.clone().formData(),file=form.get('file');fingerprint=sha(JSON.stringify({path:url.pathname,entry:form.get('entry'),comment:form.get('comment'),name:file?.name,hash:file instanceof File?sha(Buffer.from(await file.arrayBuffer())):''}))}else fingerprint=sha(url.pathname+Buffer.concat(chunks).toString('base64'));
   if(['/api/data','/api/features'].includes(url.pathname)){const value=await request.clone().json();if(value.workspace&&!['createWorkspace','join'].includes(value.action))membership(value.workspace)}if(url.pathname==='/api/file'){const form=await request.clone().formData(),entry=q('SELECT workspace_id FROM entries WHERE id=?',form.get('entry'));if(entry)membership(entry.workspace_id)}
   context.getStore().intent={key,fingerprint,path:url.pathname,action:['/api/data','/api/features'].includes(url.pathname)?JSON.parse(Buffer.concat(chunks).toString()).action:null,mentions:url.pathname==='/api/data'?(JSON.parse(Buffer.concat(chunks).toString()).mentions??[]):[]};const stored=q('SELECT * FROM request_results WHERE user_id=? AND request_id=?',account.userId,key);
   if(stored){const saved=JSON.parse(stored.response);if(saved.fingerprint!==fingerprint)throw new Problem(409,'다른 요청에 같은 재시도 키를 사용할 수 없습니다.');return new Response(saved.body,{status:stored.status,headers:{'Content-Type':'application/json'}})}
   if(inflight.has(identity)){const pending=inflight.get(identity);if(pending.fingerprint!==fingerprint)throw new Problem(409,'다른 요청에 같은 재시도 키를 사용할 수 없습니다.');return (await pending.promise).clone()}
   const promise=(async()=>{const response=await route(request,url);if(response.ok&&response.headers.get('Content-Type')?.includes('application/json'))run('INSERT OR IGNORE INTO request_results VALUES(?,?,?,?,?)',account.userId,key,JSON.stringify({fingerprint,body:await response.clone().text()}),response.status,new Date().toISOString());return response})();inflight.set(identity,{promise,fingerprint});try{return await promise}finally{inflight.delete(identity)}
  });
  if(request.method==='POST'&&response.ok&&(!url.pathname.startsWith('/api/auth/')||url.pathname==='/api/auth/profile')&&!['/api/presence','/api/vault','/api/backup'].includes(url.pathname))queueExport();
  const headers=Object.fromEntries(response.headers);Object.assign(headers,{'X-Content-Type-Options':'nosniff','Referrer-Policy':'no-referrer','Content-Security-Policy':"default-src 'self'; img-src 'self' blob: data:; style-src 'self' 'unsafe-inline'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'; object-src 'none'; base-uri 'none'",'Cache-Control':'no-store'});
  res.writeHead(response.status,headers);if(response.body)for await(const chunk of response.body)await writeChunk(res,chunk);res.end();
 }catch(error){if(!res.destroyed&&!res.writableEnded){if(!res.headersSent){res.writeHead(error.status??500,{'Content-Type':'application/json; charset=utf-8','Cache-Control':'no-store'});res.end(JSON.stringify({error:error.status?error.message:'서버 처리 중 오류가 발생했습니다. 운영 로그를 확인하세요.'}))}else res.end()}if(!error.status)log('request_error',{message:error.message})}finally{active--}}
 const server=tls?https.createServer(tls,handle):http.createServer(handle);server.requestTimeout=30000;server.headersTimeout=10000;server.keepAliveTimeout=5000;
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(port,host,resolve)});
 const address=server.address(),origin=(tls?'https':'http')+'://'+(host==='0.0.0.0'?'127.0.0.1':host==='::1'?'[::1]':host)+':'+address.port;
 const launch={pid:process.pid,url:origin,...(options.desktop?{token:launchToken}:!q('SELECT id FROM local_users WHERE operator=1')?{setupKey:bootstrapToken()}:{})};await writeFile(path.join(dataRoot,'launch.json'),JSON.stringify(launch),{mode:0o600});
 let backupJob=Promise.resolve(),backupRunning=false;const tick=()=>{try{jobs();queueExport();if(!backupRunning){backupRunning=true;backupJob=scheduledBackup().catch(e=>log('backup_failed',{message:e.message})).finally(()=>{backupRunning=false})}}catch(error){log('scheduler_failed',{message:error.message})}};tick();const timer=setInterval(tick,Math.max(5,options.schedulerIntervalMs??60000));timer.unref();
 let closing;const close=()=>{closing??=(async()=>{stopping=true;clearInterval(timer);server.closeIdleConnections();await new Promise(resolve=>server.close(resolve));while(active)await new Promise(resolve=>setTimeout(resolve,10));await backupJob;await queueExport(true);closeStore();await unlink(path.join(dataRoot,'launch.json')).catch(()=>{});log('stopped')})();return closing};
 log('started',{host,port:address.port});return {server,origin,close,launch,stats:()=>({activeRequests:active})};
}
if(process.argv[1]===fileURLToPath(import.meta.url)){const args=process.argv.slice(2),get=name=>args.includes(name)?args[args.indexOf(name)+1]:undefined;let app;app=await startServer({onStop:()=>app.close().then(()=>process.exit(0)),data:get('--data'),host:get('--host'),port:get('--port')?Number(get('--port')):undefined,desktop:args.includes('--desktop')});console.log('Daybook 준비 완료: '+app.origin);const stop=()=>{void app.close().then(()=>process.exit(0))};for(const signal of ['SIGINT','SIGTERM'])process.once(signal,stop);process.stdin.setEncoding('utf8');let commands='';process.stdin.on('data',chunk=>{commands+=chunk;if(commands.includes('stop\n')||commands.includes('stop\r\n'))stop()});}

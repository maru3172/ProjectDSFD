import {scrypt as scryptCallback,randomBytes,timingSafeEqual} from 'node:crypto';
import {promisify} from 'node:util';
import {context,user,requireUser,requireOperator,Problem,q,rows,run,transaction,json,input,text,now,sha,log} from './runtime.mjs';
const scrypt=promisify(scryptCallback),options={N:32768,r:8,p:3,maxmem:64*1024*1024};
let bootstrap;
const attempts=new Map();
const registration=new Map();let hashing=0;
const cookieName=request=>{const url=new URL(request.url);return 'daybook_session_'+(url.port||(url.protocol==='https:'?'443':'80'))};
async function derivePassword(password,salt){if(hashing>=4)throw new Problem(429,'인증 요청이 많습니다. 잠시 후 다시 시도하세요.');hashing++;try{return await scrypt(password,salt,64,options)}finally{hashing--}}
export function bootstrapToken(){bootstrap??=randomBytes(24).toString('hex');return bootstrap}
export async function passwordHash(password){if(typeof password!=='string'||password.length<12||password.length>256)throw new Problem(400,'비밀번호는 12~256자로 설정하세요.');const salt=randomBytes(24).toString('hex'),hash=await derivePassword(password,salt);return 'scrypt$'+salt+'$'+hash.toString('hex')}
async function verify(password,stored){if(typeof password!=='string'||password.length>256||!stored)return false;const [method,salt,encoded]=stored.split('$');if(method!=='scrypt'||!salt||!encoded)return false;const derived=await derivePassword(password,salt),expected=Buffer.from(encoded,'hex');return derived.length===expected.length&&timingSafeEqual(derived,expected)}
export function authenticate(request){const cookie=request.headers.get('cookie')??'',match=cookie.match(new RegExp('(?:^|;\\s*)'+cookieName(request)+'=([a-f0-9]{64})(?:;|$)'));if(!match)return null;const at=Date.now(),session=q('SELECT s.*,u.username,u.name,u.email,u.status,u.operator,u.must_change FROM local_sessions s JOIN local_users u ON u.id=s.user_id WHERE s.hash=?',sha(match[1]));if(!session||session.status!=='active'||session.expires_at<at||session.last_seen<at-8*3600000){if(session)run('DELETE FROM local_sessions WHERE hash=?',session.hash);return null}if(at-session.last_seen>60000)run('UPDATE local_sessions SET last_seen=? WHERE hash=?',at,session.hash);return {userId:session.user_id,displayName:session.name,email:session.email,fullName:session.name,operator:!!session.operator,mustChange:!!session.must_change,sessionHash:session.hash}}
export function sessionResponse(account,secure){const token=randomBytes(32).toString('hex'),at=Date.now();run('INSERT INTO local_sessions VALUES(?,?,?,?,?)',sha(token),account.id,at,at,at+7*86400000);return json({ok:true},200,{'Set-Cookie':`${cookieName(context.getStore().request)}=${token}; HttpOnly; SameSite=Strict; Path=/; Max-Age=604800${secure?'; Secure':''}`})}
export async function authRoute(request,url){const action=url.pathname.split('/').at(-1),value=request.method==='POST'?await input(request,12000):{},secure=url.protocol==='https:';
 if(request.method!=='POST'&&!['session','accounts'].includes(action))throw new Problem(405,'변경은 저장 버튼으로 요청하세요.');
 if(action==='session'&&request.method==='GET')return json({user:user()?{...user(),sessionHash:undefined,needsProfile:user().displayName==='내 기록실'&&!q('SELECT user_id FROM author_profiles WHERE user_id=?',user().userId)}:null,setupNeeded:!q('SELECT id FROM local_users WHERE operator=1')});
 if(action==='profile'){const me=requireUser(),name=text(value.name,80,true);transaction(()=>{run('UPDATE local_users SET name=? WHERE id=?',name,me.userId);run('UPDATE members SET name=? WHERE user_id=?',name,me.userId);run('INSERT INTO author_profiles VALUES(?,?,?) ON CONFLICT(user_id) DO UPDATE SET name=excluded.name',me.userId,name,now())});return json({ok:true})}
 if(action==='setup'){
  if(!context.getStore()?.loopback)throw new Problem(403,'최초 운영자 설정은 서버 PC에서 진행하세요.');
  if(q('SELECT id FROM local_users WHERE operator=1'))throw new Problem(409,'이미 최초 설정이 완료되었습니다.');
  if(value.setupKey!==bootstrapToken())throw new Problem(403,'서버 도우미에 표시된 최초 설정 코드를 입력하세요.');
  const username=text(value.username,64,true).toLowerCase();if(!/^[a-z0-9_.-]{3,64}$/.test(username))throw new Problem(400,'아이디는 영문·숫자·밑줄·점·하이픈 3~64자입니다.');
  const account={id:crypto.randomUUID(),name:text(value.name,80,true),email:text(value.email,200)||username+'@local.invalid'},hash=await passwordHash(value.password);
  transaction(()=>{if(q('SELECT id FROM local_users WHERE operator=1'))throw new Problem(409,'이미 설정되었습니다.');run('INSERT INTO local_users(id,username,name,email,password_hash,status,operator,created_at) VALUES(?,?,?,?,?,\'active\',1,?)',account.id,username,account.name,account.email,hash,now())});bootstrap=null;log('operator_setup');return sessionResponse(account,secure);
 }
 if(action==='register'){
  const ip=context.getStore().ip,at=Date.now(),previous=registration.get(ip),count=previous&&at-previous.at<3600000?previous:{at,n:0};if(++count.n>10)throw new Problem(429,'가입 신청이 많습니다. 한 시간 뒤 다시 시도하세요.');registration.set(ip,count);if(registration.size>5000)for(const [key,value] of registration)if(at-value.at>3600000)registration.delete(key);
  if(!q('SELECT id FROM local_users WHERE operator=1'))throw new Problem(409,'운영자가 먼저 서버를 설정해야 합니다.');
  const username=text(value.username,64,true).toLowerCase();if(!/^[a-z0-9_.-]{3,64}$/.test(username))throw new Problem(400,'아이디 형식을 확인하세요.');
  const requested=value.requestedIdentity?text(value.requestedIdentity,80,true).toLowerCase():null;if(requested&&(!/^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$/i.test(requested)||q('SELECT id FROM local_users WHERE id=?',requested)))throw new Problem(409,'이미 연결된 작성자이거나 연결 형식이 다릅니다. 기존 계정으로 로그인하거나 연결 요청을 해제하세요.');
  const hash=await passwordHash(value.password);try{run('INSERT INTO local_users(id,username,name,email,password_hash,status,operator,created_at,requested_identity) VALUES(?,?,?,?,?,\'pending\',0,?,?)',crypto.randomUUID(),username,text(value.name,80,true),text(value.email,200)||username+'@local.invalid',hash,now(),requested)}catch(e){if(e.code?.includes('SQLITE'))throw new Problem(409,'이미 사용 중인 아이디입니다.');throw e}return json({ok:true,pending:true});
 }
 if(action==='login'){
  const key=context.getStore().ip,at=Date.now(),record=attempts.get(key)??{n:0,until:at+900000};if(record.until<at){record.n=0;record.until=at+900000}if(record.n>=10)throw new Problem(429,'로그인 시도가 많습니다. 잠시 후 다시 시도하세요.');
  const account=q('SELECT * FROM local_users WHERE username=?',String(value.username??'').toLowerCase());const correct=await verify(value.password,account?.password_hash);if(!correct||account.status!=='active'){record.n++;attempts.set(key,record);if(attempts.size>5000)for(const [k,v] of attempts)if(v.until<at)attempts.delete(k);throw new Problem(401,'아이디·비밀번호 또는 승인 상태를 확인하세요.')}attempts.delete(key);return sessionResponse(account,secure);
 }
 if(action==='logout'){if(user())run('DELETE FROM local_sessions WHERE hash=?',user().sessionHash);return json({ok:true},200,{'Set-Cookie':`${cookieName(request)}=; Max-Age=0; HttpOnly; SameSite=Strict; Path=/${secure?'; Secure':''}`})}
 if(action==='password'){
  const current=requireUser(),account=q('SELECT * FROM local_users WHERE id=?',current.userId);if(!await verify(value.currentPassword,account.password_hash))throw new Problem(403,'현재 비밀번호를 확인하세요.');const hash=await passwordHash(value.password);transaction(()=>{run('UPDATE local_users SET password_hash=?,must_change=0 WHERE id=?',hash,account.id);run('DELETE FROM local_sessions WHERE user_id=?',account.id)});log('password_changed');return sessionResponse(account,secure);
 }
 if(action==='accounts'&&request.method==='GET'){requireOperator();return json({accounts:rows('SELECT u.id,u.username,u.name,u.email,u.status,u.operator,u.must_change,u.created_at,u.requested_identity,p.name AS recorded_name FROM local_users u LEFT JOIN author_profiles p ON p.user_id=u.requested_identity ORDER BY u.created_at')})}
 if(action==='accounts'){
  const operator=requireOperator(),target=q('SELECT * FROM local_users WHERE id=?',value.id);if(!target)throw new Problem(404,'계정이 없습니다.');if(target.id===operator.userId||target.operator)throw new Problem(403,'운영자 계정은 이 메뉴에서 변경할 수 없습니다.');
  if(value.action==='reset'){const hash=await passwordHash(value.password);transaction(()=>{run('UPDATE local_users SET password_hash=?,must_change=1 WHERE id=?',hash,target.id);run('DELETE FROM local_sessions WHERE user_id=?',target.id)})}
  else if(['active','disabled','rejected'].includes(value.status))transaction(()=>{if(value.bindIdentity){if(value.status!=='active'||target.status!=='pending'||!target.requested_identity||q('SELECT id FROM local_users WHERE id=?',target.requested_identity)||q('SELECT id FROM members WHERE user_id=?',target.requested_identity))throw new Problem(409,'이미 사용 중인 작성자나 기존 권한으로 연결할 수 없습니다.');run('UPDATE local_users SET id=?,status=\'active\' WHERE id=?',target.requested_identity,target.id)}else run('UPDATE local_users SET status=? WHERE id=?',value.status,target.id);run('DELETE FROM local_sessions WHERE user_id=?',target.id)});else throw new Problem(400,'처리 내용을 확인하세요.');log('account_review',{action:value.action??value.status});return json({ok:true});
 }
 throw new Problem(404,'지원하지 않는 인증 요청입니다.');
}

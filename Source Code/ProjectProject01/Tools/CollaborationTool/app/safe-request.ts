import {digestBytes,randomId} from './browser-crypto';
// Retain a mutation key after a lost response; a deliberate successful second save gets a new key.
const pending=new Map<string,string>();
export async function safeRequest(path:string,options:RequestInit={}){
 const mutation=!!options.method&&!['GET','HEAD'].includes(options.method),standalone=typeof window!=='undefined'&&window.daybookStandalone;
 const headers=new Headers(options.headers);let storageKey='',key='';
 if(mutation&&standalone){let identity=path+'\n';if(options.body instanceof FormData){for(const [name,value] of options.body.entries()){identity+=name+':';if(value instanceof File){const hash=await digestBytes(await value.arrayBuffer());identity+=value.name+':'+Array.from(new Uint8Array(hash)).map(x=>x.toString(16).padStart(2,'0')).join('')}else identity+=value;identity+='\n'}}else identity+=String(options.body??'');const hash=await digestBytes(new TextEncoder().encode(identity));storageKey='daybook-request:'+Array.from(new Uint8Array(hash)).map(x=>x.toString(16).padStart(2,'0')).join('');try{const stored=JSON.parse(sessionStorage.getItem(storageKey)??'null');if(stored&&Date.now()-stored.at<86400000)key=stored.key}catch{}key||=pending.get(storageKey)??randomId();pending.set(storageKey,key);try{sessionStorage.setItem(storageKey,JSON.stringify({key,at:Date.now()}))}catch{}headers.set('Idempotency-Key',key)}
 let response:Response|undefined;for(let attempt=0;attempt<2;attempt++){try{response=await fetch(path,{...options,headers,signal:options.signal??AbortSignal.timeout(20000),cache:'no-store'});break}catch(error){if(options.signal?.aborted||attempt===1||mutation&&!standalone)throw error}}
 if(response!.ok&&storageKey){pending.delete(storageKey);try{sessionStorage.removeItem(storageKey)}catch{}}return response!;
}

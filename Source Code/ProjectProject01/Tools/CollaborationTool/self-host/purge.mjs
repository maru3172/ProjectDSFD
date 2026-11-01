import {Problem,q,rows,run,text,now,env,audit} from './runtime.mjs';
// Permanent deletion: a record (from the trash, or straight away) with everything attached to it, a single file with
// all its versions, or the whole trash. Nothing is kept to restore. The rows are removed in the request's
// transaction; the stored files are removed right after it. Purged ids are remembered so that pulling the shared
// Git folder (vault.mjs) cannot bring them back, and the next export removes their files from that folder.
export const purgeActions=['purgeEntry','purgeFile','emptyTrash'];
const remember=(w,table,id)=>run('INSERT OR IGNORE INTO purged_records VALUES(?,?,?,?)',w,table,id,now());
const removeBlobs=keys=>{if(keys.length)setTimeout(()=>{for(const key of keys)void env.BUCKET.delete(key).catch(()=>{})},0)};

// Every file version linked to this one, older and newer.
function versionFamily(w,id){
 return rows('WITH RECURSIVE up(id,previous_id) AS (SELECT id,previous_id FROM attachments WHERE id=? AND workspace_id=? UNION SELECT a.id,a.previous_id FROM attachments a JOIN up ON a.id=up.previous_id), down(id) AS (SELECT id FROM up UNION SELECT a.id FROM attachments a JOIN down ON a.previous_id=down.id) SELECT a.id,a.key FROM attachments a WHERE a.id IN (SELECT id FROM down) AND a.workspace_id=?',id,w,w);
}
function purgeFiles(w,files){
 for(const f of files){for(const c of rows('SELECT id FROM comments WHERE workspace_id=? AND attachment_id=?',w,f.id)){run('DELETE FROM comments WHERE id=?',c.id);remember(w,'comments',c.id)}run('DELETE FROM attachments WHERE id=?',f.id);remember(w,'attachments',f.id)}
 return files.map(f=>f.key);
}
function purgeEntryRows(w,e){
 const keys=purgeFiles(w,rows('SELECT id,key FROM attachments WHERE workspace_id=? AND entry_id=?',w,e.id));
 for(const table of ['comments','activity','check_items'])for(const r of rows(`SELECT id FROM ${table} WHERE workspace_id=? AND entry_id=?`,w,e.id)){run(`DELETE FROM ${table} WHERE id=?`,r.id);remember(w,table,r.id)}
 for(const d of rows('SELECT entry_id,requires_id FROM dependencies WHERE workspace_id=? AND (entry_id=? OR requires_id=?)',w,e.id,e.id))remember(w,'dependencies',d.entry_id+'_'+d.requires_id);
 run('DELETE FROM dependencies WHERE workspace_id=? AND (entry_id=? OR requires_id=?)',w,e.id,e.id);
 for(const table of ['favorites','ack_targets','acknowledgements','room_reads','handovers','notifications'])run(`DELETE FROM ${table} WHERE entry_id=?`,e.id);
 // Links from other places are cleared, not deleted: sub-records stay, a repeat day stays marked as made.
 for(const [table,column] of [['entries','parent_id'],['repeat_occurrences','entry_id'],['asset_locks','entry_id'],['presence_sessions','entry_id']])run(`UPDATE ${table} SET ${column}=NULL WHERE ${column}=?`,e.id);
 run('DELETE FROM entries WHERE id=?',e.id);remember(w,'entries',e.id);
 return keys;
}
export function purgeAction(action,value,w,me,role){
 const admin=['owner','admin'].includes(role);
 if(action==='purgeEntry'){
  const e=q('SELECT * FROM entries WHERE workspace_id=? AND id=?',w,text(value.entry,80,true));
  if(!e)throw new Problem(404,'기록이 없습니다.');
  if(e.author_id!==me.userId&&!admin)throw new Problem(403,'작성자나 관리자만 영구 삭제할 수 있습니다.');
  removeBlobs(purgeEntryRows(w,e));audit(w,'entry_purged',null,{kind:e.kind,from:e.deleted_at?'trash':'direct'});
  return {ok:true};
 }
 if(action==='emptyTrash'){
  if(!admin)throw new Problem(403,'휴지통 비우기는 공간 관리자만 할 수 있습니다.');
  const trashed=rows('SELECT * FROM entries WHERE workspace_id=? AND deleted_at IS NOT NULL',w),keys=[];
  for(const e of trashed)keys.push(...purgeEntryRows(w,e));
  // Files put in the trash on their own (their record is still there) go too.
  const files=rows('SELECT id FROM attachments WHERE workspace_id=? AND deleted_at IS NOT NULL',w);const seen=new Set();
  for(const f of files)if(!seen.has(f.id)){const family=versionFamily(w,f.id);family.forEach(x=>seen.add(x.id));keys.push(...purgeFiles(w,family))}
  removeBlobs(keys);audit(w,'trash_emptied',null,{records:trashed.length,files:seen.size});
  return {ok:true,records:trashed.length,files:seen.size};
 }
 if(action==='purgeFile'){
  const file=q('SELECT a.*,e.author_id FROM attachments a JOIN entries e ON e.id=a.entry_id WHERE a.id=? AND a.workspace_id=?',text(value.file,80,true),w);
  if(!file)throw new Problem(404,'첨부가 없습니다.');
  if(file.author_id!==me.userId&&!admin)throw new Problem(403,'기록 작성자나 관리자만 파일을 영구 삭제할 수 있습니다.');
  const family=versionFamily(w,file.id);removeBlobs(purgeFiles(w,family));audit(w,'file_purged',file.entry_id,{versions:family.length});
  return {ok:true,versions:family.length};
 }
 throw new Problem(400,'지원하지 않는 삭제 작업입니다.');
}

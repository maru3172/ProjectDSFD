import {useState} from 'react';
import type {Item} from './ue5-cases';

export default function Ue5BuildComparison({items,revisions,versions,base}:{items:Item[];revisions:Item[];versions:string[];base:string}) {
  const [target,setTarget]=useState('');
  const [changedOnly,setChangedOnly]=useState(true);
  const other=versions.filter(version=>version!==base);
  const comparison=other.includes(target)?target:other[0];
  const result=(item?:Item)=>item?.outcome||'미실행';
  const records=new Map<string,Map<string,Item>>();
  for(const row of revisions){
    if(row.version!==base&&row.version!==comparison)continue;
    const byVersion=records.get(row.item_id)??new Map<string,Item>();
    if(!byVersion.has(row.version))byVersion.set(row.version,row);
    records.set(row.item_id,byVersion);
  }
  const rows=items.map(item=>({
    item,left:records.get(item.item_id)?.get(base),right:records.get(item.item_id)?.get(comparison),
  })).filter(({left,right})=>!changedOnly||result(left)!==result(right)||(left?.regression||'미확인')!==(right?.regression||'미확인'));
  return <section className="ue5-tracker">
    <h4>빌드 결과 비교</h4>
    {!comparison?<p>검증 이력이 있는 빌드가 두 개 이상 필요합니다.</p>:<>
      <div className="ue5-controls">
        <p>기준 빌드 · {base}</p>
        <label>비교 빌드<select value={comparison} onChange={e=>setTarget(e.target.value)}>{other.map(value=><option key={value}>{value}</option>)}</select></label>
        <label className="checkline"><input type="checkbox" checked={changedOnly} onChange={e=>setChangedOnly(e.target.checked)}/>결과 또는 회귀 판정이 달라진 항목만</label>
      </div>
      <p className="muted">항목별로 각 빌드에 마지막으로 기록된 결과를 비교합니다.</p>
      {rows.length===0?<p>표시할 차이가 없습니다.</p>:<div className="ue5-output"><table>
        <thead><tr><th>항목</th><th>{base}</th><th>{comparison}</th></tr></thead>
        <tbody>{rows.map(({item,left,right})=><tr key={item.item_id}>
          <th scope="row">{item.title}{result(left)==='통과'&&result(right)!=='통과'&&<small> · 통과 상태 변경</small>}</th>
          {[left,right].map((row,index)=><td key={index}>
            <strong>{result(row)}</strong>{row&&<> · 회귀 {row.regression}{row.board&&<><br/>진단 보드 {row.board}</>}{row.result&&<><br/>{row.result}</>}</>}
          </td>)}
        </tr>)}</tbody>
      </table></div>}
    </>}
  </section>;
}

export type Summary={points:string[];keywords:string[]};
const stop=new Set(["그리고","그러나","하지만","대한","위한","있는","있습니다","합니다","하는","내용","내용을","이것","이번","해당","통해","위해서","따라","확인","필요","the","and","for","with","this","that","from","are","was"]);
const words=(text:string)=>(text.toLowerCase().match(/[\p{L}\p{N}]{2,}/gu)??[]).map(w=>w.length>3?w.replace(/(에서는|으로|에서|에게|까지|부터|은|는|이|가|을|를|의|와|과|도|로)$/,""):w).filter(w=>!stop.has(w)&&!/^\d+$/.test(w));
export function summarize(source:string,maxPoints=3):Summary{
 const plain=source.replace(/```[\s\S]*?```/g," ").replace(/!\[([^\]]*)\]\([^)]*\)/g,"$1").replace(/\[([^\]]+)\]\([^)]*\)/g,"$1");
 const chunks=plain.split(/\r?\n|(?<=[.!?。！？])\s+/).map(x=>x.replace(/^\s*(#{1,6}|[-*>]|\d+[.)])\s*/,"").replace(/^\[[xX ]\]\s*/,"").replace(/[\*_`]/g,"").replace(/\|/g," · ").replace(/\s+/g," ").trim()).filter(x=>x.length>=8&&!/^[-: ·]+$/.test(x));
 const unique=[...new Set(chunks)],frequency=new Map<string,number>();
 for(const term of words(plain))frequency.set(term,(frequency.get(term)??0)+1);
 const keywords=[...frequency].sort((a,b)=>b[1]-a[1]||b[0].length-a[0].length).slice(0,5).map(([term])=>term);
 const ranked=unique.map((text,index)=>{const tokens=[...new Set(words(text))];const score=tokens.reduce((n,w)=>n+Math.log(1+(frequency.get(w)??0)),0)/Math.sqrt(Math.max(tokens.length,1))+(index===0?1.4:0)+(/결정|합의|목표|문제|기한|예정|완료|다음|해야|마감/.test(text)?1.2:0);return {text,index,score}}).sort((a,b)=>b.score-a.score).slice(0,maxPoints).sort((a,b)=>a.index-b.index);
 return {points:ranked.map(x=>x.text.length>180?x.text.slice(0,177)+"…":x.text),keywords};
}
export function recordSummary(entry:{body:string;summary?:string|null;auto_summary?:string|null}):Summary{
 if(entry.summary?.trim())return {points:entry.summary.split(/\r?\n/).map(s=>s.trim()).filter(Boolean),keywords:[]};
 try{const stored=JSON.parse(entry.auto_summary??"null");if(stored&&Array.isArray(stored.points)&&Array.isArray(stored.keywords))return stored}catch{}
 return summarize(entry.body);
}

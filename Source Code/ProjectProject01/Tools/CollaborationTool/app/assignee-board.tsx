"use client";
import {useEffect,useState,type ReactNode} from "react";
import {Flag,ListTodo,Target} from "lucide-react";
import {type Data,type Entry,type Kind,dayOf} from "./types";

const Unassigned="__unassigned";
const sections:[Kind,string,typeof ListTodo,string][]=[["task","할 일",ListTodo,"맡은 할 일이 없어요."],["plan","계획",Flag,"맡은 계획이 없어요."],["goal","목표",Target,"맡은 목표가 없어요."]];
const addDays=(day:string,n:number)=>{const d=new Date(day+"T00:00:00Z");d.setUTCDate(d.getUTCDate()+n);return d.toISOString().slice(0,10)};

/**
 * 담당자별 업무: one teammate at a time (or the unassigned work), with their tasks, plans and goals kept apart.
 * Open items come first, nearest due date on top; finished ones are folded underneath.
 */
export default function AssigneeBoard({workspace,data,entries,me,row,progress}:{workspace:string;data:Data;entries:Entry[];me:string;row:(entry:Entry)=>ReactNode;progress:(entry:Entry)=>{done:number;total:number;percent:number}}){
 // Goals and plans carry the same progress as their cards in 목표와 계획: the share of linked tasks and goals that are done.
 const item=(e:Entry)=>{if(e.kind==="task")return row(e);const p=progress(e);
  return <div className="assignee-goal" key={e.id}>{row(e)}<div className="assignee-progress" title={`진행률 ${p.percent}%`}><div><i style={{width:p.percent+"%"}}/></div><span><strong>{p.percent}%</strong>{p.total?` · 연결된 일 ${p.total}개 중 ${p.done}개 완료`:" · 연결된 일 없음"}</span></div></div>};
 const storageKey="daybook-assignee:"+workspace;
 const [person,setPerson]=useState<string>(()=>{try{return localStorage.getItem(storageKey)??me}catch{return me}});
 useEffect(()=>{try{localStorage.setItem(storageKey,person)}catch{/* private window: just not remembered */}},[storageKey,person]);
 const today=dayOf(),weekEnd=addDays(today,7);
 const work=entries.filter(e=>["task","plan","goal"].includes(e.kind));
 const owned=(id:string)=>work.filter(e=>id===Unassigned?!e.assigned_id||!data.members.some(m=>m.user_id===e.assigned_id):e.assigned_id===id);
 const people=[...data.members.map(m=>({id:m.user_id,name:m.name+(m.user_id===me?" (나)":"")})),{id:Unassigned,name:"담당 미정"}]
  .sort((a,b)=>a.id===me?-1:b.id===me?1:a.id===Unassigned?1:b.id===Unassigned?-1:a.name.localeCompare(b.name,"ko"));
 const selected=people.some(p=>p.id===person)?person:me;
 const mine=owned(selected),open=mine.filter(e=>e.status!=="done");
 const overdue=open.filter(e=>e.due_day&&e.due_day<today).length,thisWeek=open.filter(e=>e.due_day&&e.due_day>=today&&e.due_day<=weekEnd).length;
 const byDue=(a:Entry,b:Entry)=>(a.due_day??"9999").localeCompare(b.due_day??"9999")||a.title.localeCompare(b.title,"ko");
 return <section className="assignee-board">
  <nav className="assignee-people" aria-label="담당자">{people.map(p=>{const count=owned(p.id).filter(e=>e.status!=="done").length;
   return <button key={p.id} className={p.id===selected?"active":""} onClick={()=>setPerson(p.id)}><span>{p.name}</span><b>{count}</b></button>})}</nav>
  <div className="assignee-summary">
   <div><strong>{open.length}</strong><span>남은 일</span></div>
   <div className={overdue?"late":""}><strong>{overdue}</strong><span>기한 지남</span></div>
   <div><strong>{thisWeek}</strong><span>7일 안에 마감</span></div>
   <div><strong>{mine.length-open.length}</strong><span>완료</span></div>
  </div>
  <div className="assignee-columns">{sections.map(([kind,label,Icon,none])=>{const list=mine.filter(e=>e.kind===kind),todo=list.filter(e=>e.status!=="done").sort(byDue),done=list.filter(e=>e.status==="done").sort(byDue);
   return <section className="panel assignee-column" key={kind}>
    <header><Icon size={16}/><h3>{label}</h3><span>{todo.length}</span></header>
    {todo.length?todo.map(item):<p className="assignee-empty">{list.length?"모두 끝냈어요.":none}</p>}
    {done.length>0&&<details><summary>완료 {done.length}</summary>{done.map(item)}</details>}
   </section>})}</div>
 </section>;
}

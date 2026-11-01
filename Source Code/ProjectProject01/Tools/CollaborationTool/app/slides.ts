// A slide deck is an ordinary record whose body is Markdown in this shape, so search, history, comments and the
// Git folder export keep working, and the text still reads well outside the slide editor:
//   <!-- daybook:slides -->
//   # Title
//   text line / - bullet line
//   ![사진](attachment:<id> "1280x720")
//   ---
//   # Next title
import type {Slide} from './office-files';
export type {Slide} from './office-files';

export const SlideMarker='<!-- daybook:slides -->';
const imageLine=/^!\[[^\]]*\]\(attachment:([A-Za-z0-9-]+)(?: "(\d+)x(\d+)")?\)$/;

export const isDeck=(body:string|null|undefined)=>!!body&&body.trimStart().startsWith(SlideMarker);

export function parseSlides(body:string):Slide[]|null{
 if(!isDeck(body))return null;
 const text=body.trimStart().slice(SlideMarker.length).replace(/\r\n?/g,'\n');
 return text.split(/\n---[ \t]*(?=\n|$)/).map(part=>{
  const lines=part.split('\n');while(lines.length&&!lines[0].trim())lines.shift();
  let title='';if(lines[0]?.startsWith('# ')){title=lines.shift()!.slice(2).trim()}
  let image:Slide['image']=null;const rest:string[]=[];
  for(const line of lines){const m=imageLine.exec(line.trim());if(m&&!image)image={id:m[1],width:Number(m[2]??0)||1280,height:Number(m[3]??0)||720};else rest.push(line)}
  while(rest.length&&!rest[rest.length-1].trim())rest.pop();while(rest.length&&!rest[0].trim())rest.shift();
  return {title,text:rest.join('\n'),image};
 }).filter((s,i,all)=>s.title||s.text||s.image||all.length===1);
}

export function writeSlides(slides:Slide[]):string{
 return SlideMarker+'\n'+slides.map(s=>{
  // A body line of only "---" would split the slide in two; it is kept as a long dash line instead.
  const text=s.text.replace(/\r\n?/g,'\n').split('\n').map(l=>/^---[ \t]*$/.test(l)?'———':l).join('\n').trim();
  return [`# ${s.title.replace(/\n/g,' ').trim()||'제목 없음'}`,text,s.image?`![사진](attachment:${s.image.id} "${Math.round(s.image.width)}x${Math.round(s.image.height)}")`:''].filter(Boolean).join('\n\n');
 }).join('\n\n---\n\n')+'\n';
}

/** Plain text of a deck for search, summaries and text export: titles and lines, no markup. */
export const slidesText=(slides:Slide[])=>slides.map((s,i)=>`${i+1}. ${s.title}\n${s.text}`.trim()).join('\n\n');

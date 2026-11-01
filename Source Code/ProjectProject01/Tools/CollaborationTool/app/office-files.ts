// Writes Office files (Word .docx, Excel .xlsx, PowerPoint .pptx) and CSV/text in the browser, with no service or
// library beyond fflate (zip). Pure functions: data in, file bytes out, so they also run under Node for checks.
// Only what the tool needs is written: headings, paragraphs and bullets; a table with a bold header row; slides with
// a title, text, bullets and one picture. Fonts are 맑은 고딕 so Korean shows the same in every Office app.
import {zipSync,strToU8} from 'fflate';

const FONT='맑은 고딕';
// XML 1.0 forbids most control characters; they are dropped rather than breaking the file.
const clean=(value:string)=>value.replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f￾￿]/g,'');
export const xml=(value:unknown)=>clean(String(value??'')).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
const head='<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n';
const zip=(files:Record<string,string|Uint8Array>)=>zipSync(Object.fromEntries(Object.entries(files).map(([name,value])=>[name,typeof value==='string'?strToU8(value):value])),{level:6});
const rels=(items:[string,string,string][])=>head+'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'+items.map(([id,type,target])=>`<Relationship Id="${id}" Type="${type.startsWith('http')?type:'http://schemas.openxmlformats.org/officeDocument/2006/relationships/'+type}" Target="${xml(target)}"/>`).join('')+'</Relationships>';
const core=(title:string)=>{const stamp=new Date().toISOString().replace(/\.\d+Z$/,'Z');return head+`<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"><dc:title>${xml(title)}</dc:title><dc:creator>Daybook</dc:creator><dcterms:created xsi:type="dcterms:W3CDTF">${stamp}</dcterms:created><dcterms:modified xsi:type="dcterms:W3CDTF">${stamp}</dcterms:modified></cp:coreProperties>`};
const packageRels=(main:string)=>rels([['rId1','officeDocument',main],['rId2','http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties','docProps/core.xml']]);
const types=(defaults:[string,string][],overrides:[string,string][])=>head+'<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/>'
 +defaults.map(([ext,type])=>`<Default Extension="${ext}" ContentType="${type}"/>`).join('')
 +overrides.concat([['/docProps/core.xml','application/vnd.openxmlformats-package.core-properties+xml']]).map(([part,type])=>`<Override PartName="${part}" ContentType="${type}"/>`).join('')+'</Types>';

/** A document as simple blocks; markdownBlocks() turns record text into these. */
export type Block={kind:'title'|'h1'|'h2'|'h3'|'p'|'bullet'|'number'|'quote'|'code'|'meta';text:string};
export function markdownBlocks(markdown:string):Block[]{
 const blocks:Block[]=[];let code=false;
 for(const raw of markdown.replace(/\r\n?/g,'\n').split('\n')){
  if(/^\s*```/.test(raw)){code=!code;continue}
  if(code){blocks.push({kind:'code',text:raw});continue}
  const line=raw.trim();if(!line||line==='---'||line.startsWith('<!--'))continue;
  const plain=(s:string)=>s.replace(/\*\*(.+?)\*\*/g,'$1').replace(/!\[([^\]]*)\]\([^)]*\)/g,'[사진: $1]').replace(/\[([^\]]+)\]\([^)]*\)/g,'$1').replace(/`([^`]+)`/g,'$1');
  let m;
  if((m=/^(#{1,3})\s+(.*)$/.exec(line)))blocks.push({kind:(['h1','h2','h3'] as const)[m[1].length-1],text:plain(m[2])});
  else if((m=/^[-*+]\s+(.*)$/.exec(line)))blocks.push({kind:'bullet',text:plain(m[1])});
  else if((m=/^\d+[.)]\s+(.*)$/.exec(line)))blocks.push({kind:'number',text:plain(m[1])});
  else if((m=/^>\s?(.*)$/.exec(line)))blocks.push({kind:'quote',text:plain(m[1])});
  else blocks.push({kind:'p',text:plain(line)});
 }
 return blocks;
}

export function makeDocx(title:string,blocks:Block[]):Uint8Array{
 const W='http://schemas.openxmlformats.org/wordprocessingml/2006/main';
 const run=(text:string,props='')=>`<w:r>${props?`<w:rPr>${props}</w:rPr>`:''}<w:t xml:space="preserve">${xml(text)}</w:t></w:r>`;
 const style:Record<Block['kind'],string>={title:'Title',h1:'Heading1',h2:'Heading2',h3:'Heading3',p:'',bullet:'',number:'',quote:'Quote',code:'Code',meta:'Meta'};
 let n=0;
 const body=blocks.map(b=>{
  const prefix=b.kind==='bullet'?'•\t':b.kind==='number'?`${++n}.\t`:'';if(b.kind!=='number')n=0;
  const ppr=style[b.kind]?`<w:pStyle w:val="${style[b.kind]}"/>`:prefix?'<w:tabs><w:tab w:val="left" w:pos="360"/></w:tabs><w:ind w:left="360" w:hanging="360"/>':'';
  return `<w:p>${ppr?`<w:pPr>${ppr}</w:pPr>`:''}${run(prefix+b.text)}</w:p>`;
 }).join('');
 const font=`<w:rFonts w:ascii="${FONT}" w:hAnsi="${FONT}" w:eastAsia="${FONT}" w:cs="${FONT}"/>`;
 const s=(id:string,name:string,ppr:string,rpr:string)=>`<w:style w:type="paragraph" w:styleId="${id}"><w:name w:val="${name}"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr>${ppr}</w:pPr><w:rPr>${rpr}</w:rPr></w:style>`;
 const styles=head+`<w:styles xmlns:w="${W}"><w:docDefaults><w:rPrDefault><w:rPr>${font}<w:sz w:val="21"/><w:lang w:val="ko-KR" w:eastAsia="ko-KR"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after="120" w:line="300" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>`
  +`<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/></w:style>`
  +s('Title','Title','<w:spacing w:after="240"/>','<w:b/><w:sz w:val="40"/>')+s('Heading1','heading 1','<w:keepNext/><w:spacing w:before="320" w:after="120"/><w:outlineLvl w:val="0"/>','<w:b/><w:sz w:val="32"/>')
  +s('Heading2','heading 2','<w:keepNext/><w:spacing w:before="240" w:after="100"/><w:outlineLvl w:val="1"/>','<w:b/><w:sz w:val="27"/>')+s('Heading3','heading 3','<w:keepNext/><w:spacing w:before="200" w:after="80"/><w:outlineLvl w:val="2"/>','<w:b/><w:sz w:val="23"/>')
  +s('Quote','Quote','<w:ind w:left="400"/>','<w:i/><w:color w:val="555555"/>')+s('Meta','Meta','<w:spacing w:after="60"/>','<w:color w:val="666666"/><w:sz w:val="18"/>')
  +s('Code','Code','<w:spacing w:after="0"/>','<w:rFonts w:ascii="Consolas" w:hAnsi="Consolas" w:eastAsia="'+FONT+'"/><w:sz w:val="19"/>')+'</w:styles>';
 const document=head+`<w:document xmlns:w="${W}" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><w:body>${body||'<w:p/>'}<w:sectPr><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1440" w:right="1300" w:bottom="1440" w:left="1300" w:header="720" w:footer="720" w:gutter="0"/></w:sectPr></w:body></w:document>`;
 return zip({'[Content_Types].xml':types([],[['/word/document.xml','application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml'],['/word/styles.xml','application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml']]),
  '_rels/.rels':packageRels('word/document.xml'),'docProps/core.xml':core(title),'word/document.xml':document,'word/styles.xml':styles,
  'word/_rels/document.xml.rels':rels([['rId1','styles','styles.xml']])});
}

const column=(i:number)=>{let s='';for(i++;i>0;i=Math.floor((i-1)/26))s=String.fromCharCode(65+(i-1)%26)+s;return s};
// Excel keeps each sheet's filter range as a hidden name; all sheets share one definedNames list.
const filters=(sheets:{rows:string[][]}[],names:string[])=>{const items=sheets.map((s,i)=>s.rows.length>1?`<definedName name="_xlnm._FilterDatabase" localSheetId="${i}" hidden="1">'${xml(names[i].replace(/'/g,"''"))}'!$A$1:$${column(Math.max(1,...s.rows.map(r=>r.length))-1)}$${s.rows.length}</definedName>`:'').join('');return items?`<definedNames>${items}</definedNames>`:''};
/** One sheet per entry; the first row is the header (bold, frozen, filterable). Cells are text, so codes and dates stay as typed. */
export function makeXlsx(title:string,sheets:{name:string;rows:string[][];widths?:number[]}[]):Uint8Array{
 const used=new Set<string>();
 const names=sheets.map((s,i)=>{let name=s.name.replace(/[\[\]:*?/\\]/g,' ').trim().slice(0,31)||`Sheet${i+1}`;while(used.has(name.toLowerCase()))name=name.slice(0,28)+'_'+i;used.add(name.toLowerCase());return name});
 const sheet=(s:{rows:string[][];widths?:number[]})=>{
  const width=Math.max(1,...s.rows.map(r=>r.length));
  const cols=Array.from({length:width},(_,c)=>`<col min="${c+1}" max="${c+1}" width="${s.widths?.[c]??Math.min(60,Math.max(8,...s.rows.slice(0,200).map(r=>[...(r[c]??'')].reduce((n,ch)=>n+(ch.charCodeAt(0)>255?2:1),0)+2)))}" customWidth="1"/>`).join('');
  const data=s.rows.map((r,i)=>`<row r="${i+1}">${r.map((v,c)=>`<c r="${column(c)}${i+1}" t="inlineStr"${i===0?' s="1"':' s="2"'}><is><t xml:space="preserve">${xml(String(v??'').slice(0,32000))}</t></is></c>`).join('')}</row>`).join('');
  const last=`${column(width-1)}${Math.max(1,s.rows.length)}`;
  return head+`<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetViews><sheetView workbookViewId="0"><pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/></sheetView></sheetViews><cols>${cols}</cols><sheetData>${data}</sheetData>${s.rows.length>1?`<autoFilter ref="A1:${last}"/>`:''}</worksheet>`;
 };
 const styles=head+`<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><fonts count="2"><font><sz val="10"/><name val="${FONT}"/><family val="2"/></font><font><b/><sz val="10"/><name val="${FONT}"/><family val="2"/></font></fonts><fills count="3"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FFE8F1EE"/><bgColor indexed="64"/></patternFill></fill></fills><borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="3"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="0" fontId="1" fillId="2" borderId="0" xfId="0" applyFont="1" applyFill="1"/><xf numFmtId="49" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1" applyAlignment="1"><alignment vertical="top" wrapText="1"/></xf></cellXfs><cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles></styleSheet>`;
 const files:Record<string,string>={'[Content_Types].xml':types([],[['/xl/workbook.xml','application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml'],['/xl/styles.xml','application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml'],...sheets.map((_,i)=>[`/xl/worksheets/sheet${i+1}.xml`,'application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml'] as [string,string])]),
  '_rels/.rels':packageRels('xl/workbook.xml'),'docProps/core.xml':core(title),
  'xl/workbook.xml':head+`<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>${names.map((n,i)=>`<sheet name="${xml(n)}" sheetId="${i+1}" r:id="rId${i+1}"/>`).join('')}</sheets>${filters(sheets,names)}</workbook>`,
  'xl/_rels/workbook.xml.rels':rels([...sheets.map((_,i)=>[`rId${i+1}`,'worksheet',`worksheets/sheet${i+1}.xml`] as [string,string,string]),[`rId${sheets.length+1}`,'styles','styles.xml']]),'xl/styles.xml':styles};
 sheets.forEach((s,i)=>{files[`xl/worksheets/sheet${i+1}.xml`]=sheet(s)});
 return zip(files);
}

/** CSV with a byte-order mark so Excel opens Korean text correctly. */
export function makeCsv(rows:string[][]):Uint8Array{
 const cell=(v:string)=>/[",\r\n]/.test(v)||/^[=+\-@]/.test(v)?'"'+(/^[=+\-@]/.test(v)?"'":'')+v.replace(/"/g,'""')+'"':v; // A leading = + - @ would run as a formula in Excel.
 return strToU8('﻿'+rows.map(r=>r.map(v=>cell(String(v??''))).join(',')).join('\r\n')+'\r\n');
}

/** A slide: title, text lines ("- " starts a bullet) and at most one picture. */
export type Slide={title:string;text:string;image?:{id:string;width:number;height:number}|null};
export type SlideImage={bytes:Uint8Array;type:'png'|'jpeg'|'gif';width:number;height:number};
const SW=12192000,SH=6858000,M=609600; // 16:9 slide and its margin, in EMU (914400 per inch).

export function makePptx(title:string,slides:Slide[],images:Map<string,SlideImage>):Uint8Array{
 const P='xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"';
 const tree=(inner:string)=>`<p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>${inner}</p:spTree>`;
 const rpr=(size:number,bold=false,color='1F2D33')=>`<a:rPr lang="ko-KR" altLang="en-US" sz="${size*100}"${bold?' b="1"':''} dirty="0"><a:solidFill><a:srgbClr val="${color}"/></a:solidFill><a:latin typeface="${FONT}"/><a:ea typeface="${FONT}"/><a:cs typeface="${FONT}"/></a:rPr>`;
 const para=(text:string,size:number,{bold=false,bullet=false,align='l',color='1F2D33'}={})=>`<a:p><a:pPr algn="${align}"${bullet?' marL="342900" indent="-342900"':''}>${bullet?'<a:buFont typeface="Arial"/><a:buChar char="&#8226;"/>':'<a:buNone/>'}</a:pPr>${text?`<a:r>${rpr(size,bold,color)}<a:t>${xml(text)}</a:t></a:r>`:''}<a:endParaRPr lang="ko-KR" altLang="en-US" sz="${size*100}" dirty="0"/></a:p>`;
 const box=(id:number,name:string,x:number,y:number,w:number,h:number,paragraphs:string,anchor='t')=>`<p:sp><p:nvSpPr><p:cNvPr id="${id}" name="${xml(name)}"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x="${Math.round(x)}" y="${Math.round(y)}"/><a:ext cx="${Math.round(w)}" cy="${Math.round(h)}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/></p:spPr><p:txBody><a:bodyPr wrap="square" lIns="91440" tIns="45720" rIns="91440" bIns="45720" anchor="${anchor}"><a:normAutofit/></a:bodyPr><a:lstStyle/>${paragraphs}</p:txBody></p:sp>`;
 const picture=(id:number,rel:string,x:number,y:number,w:number,h:number)=>`<p:pic><p:nvPicPr><p:cNvPr id="${id}" name="Picture ${id}"/><p:cNvPicPr><a:picLocks noChangeAspect="1"/></p:cNvPicPr><p:nvPr/></p:nvPicPr><p:blipFill><a:blip r:embed="${rel}"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr><a:xfrm><a:off x="${Math.round(x)}" y="${Math.round(y)}"/><a:ext cx="${Math.round(w)}" cy="${Math.round(h)}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></p:spPr></p:pic>`;
 const fit=(img:{width:number;height:number},x:number,y:number,w:number,h:number)=>{const s=Math.min(w/Math.max(1,img.width),h/Math.max(1,img.height));const iw=img.width*s,ih=img.height*s;return [x+(w-iw)/2,y+(h-ih)/2,iw,ih] as const};

 const media:Record<string,Uint8Array>={},files:Record<string,string|Uint8Array>={},usedTypes=new Set<string>();let mediaCount=0;
 const list=slides.length?slides:[{title,text:''}];
 list.forEach((slide,index)=>{
  const lines=slide.text.replace(/\r\n?/g,'\n').split('\n').map(l=>l.trimEnd());while(lines.length&&!lines[lines.length-1])lines.pop();
  const img=slide.image?images.get(slide.image.id):undefined,hasText=lines.some(l=>l.trim());
  let shapes='';const slideRels:[string,string,string][]=[['rId1','slideLayout','../slideLayouts/slideLayout1.xml']];
  if(!hasText&&!img)shapes+=box(2,'Title',M,SH*0.3,SW-2*M,SH*0.4,para(slide.title,40,{bold:true,align:'ctr'}),'ctr'); // A title-only slide is a cover/section slide.
  else{
   shapes+=box(2,'Title',M,M*0.7,SW-2*M,1005840,para(slide.title,30,{bold:true}),'b');
   const top=M*0.7+1005840+182880,height=SH-top-M*0.8;
   const textWidth=img?(SW-2*M)*0.52:SW-2*M;
   if(hasText)shapes+=box(3,'Text',M,top,textWidth,height,lines.map(l=>{const bullet=/^\s*[-*•]\s+/.test(l);return para(l.replace(/^\s*[-*•]\s+/,''),20,{bullet,color:'334148'})}).join(''));
   if(img){
    const name=`image${++mediaCount}.${img.type==='jpeg'?'jpeg':img.type}`;media[`ppt/media/${name}`]=img.bytes;usedTypes.add(img.type);slideRels.push(['rId2','image',`../media/${name}`]);
    const area=hasText?[M+textWidth+228600,top,SW-2*M-textWidth-228600,height]:[M,top,SW-2*M,height];
    shapes+=picture(4,'rId2',...fit(img,area[0],area[1],area[2],area[3]));
   }
  }
  files[`ppt/slides/slide${index+1}.xml`]=head+`<p:sld ${P}><p:cSld>${tree(shapes)}</p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>`;
  files[`ppt/slides/_rels/slide${index+1}.xml.rels`]=rels(slideRels);
 });
 const font=`<a:latin typeface="${FONT}"/><a:ea typeface="${FONT}"/><a:cs typeface=""/>`,scheme=(fill:string)=>`<a:solidFill><a:schemeClr val="${fill}"/></a:solidFill>`;
 const theme=head+`<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Daybook"><a:themeElements><a:clrScheme name="Daybook"><a:dk1><a:srgbClr val="1F2D33"/></a:dk1><a:lt1><a:srgbClr val="FFFFFF"/></a:lt1><a:dk2><a:srgbClr val="2C4B50"/></a:dk2><a:lt2><a:srgbClr val="EEF3F1"/></a:lt2><a:accent1><a:srgbClr val="22877D"/></a:accent1><a:accent2><a:srgbClr val="C0503A"/></a:accent2><a:accent3><a:srgbClr val="7A5CC2"/></a:accent3><a:accent4><a:srgbClr val="D9A441"/></a:accent4><a:accent5><a:srgbClr val="4A7FB5"/></a:accent5><a:accent6><a:srgbClr val="6F8B93"/></a:accent6><a:hlink><a:srgbClr val="1B6FA8"/></a:hlink><a:folHlink><a:srgbClr val="7A5CC2"/></a:folHlink></a:clrScheme>`
  +`<a:fontScheme name="Daybook"><a:majorFont>${font}</a:majorFont><a:minorFont>${font}</a:minorFont></a:fontScheme>`
  +`<a:fmtScheme name="Daybook"><a:fillStyleLst>${scheme('phClr').repeat(3)}</a:fillStyleLst><a:lnStyleLst>${'<a:ln w="9525">'+scheme('phClr')+'</a:ln>'}${'<a:ln w="19050">'+scheme('phClr')+'</a:ln>'}${'<a:ln w="28575">'+scheme('phClr')+'</a:ln>'}</a:lnStyleLst><a:effectStyleLst>${'<a:effectStyle><a:effectLst/></a:effectStyle>'.repeat(3)}</a:effectStyleLst><a:bgFillStyleLst>${scheme('phClr').repeat(3)}</a:bgFillStyleLst></a:fmtScheme></a:themeElements><a:objectDefaults/><a:extraClrSchemeLst/></a:theme>`;
 const level=(size:number)=>`<a:lvl1pPr><a:defRPr sz="${size*100}"><a:latin typeface="+mn-lt"/><a:ea typeface="+mn-ea"/><a:cs typeface="+mn-cs"/></a:defRPr></a:lvl1pPr>`;
 Object.assign(files,media,{
  '[Content_Types].xml':types([['png','image/png'],['jpeg','image/jpeg'],['gif','image/gif']].filter(([ext])=>usedTypes.has(ext)) as [string,string][],[
   ['/ppt/presentation.xml','application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml'],['/ppt/slideMasters/slideMaster1.xml','application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml'],
   ['/ppt/slideLayouts/slideLayout1.xml','application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml'],['/ppt/theme/theme1.xml','application/vnd.openxmlformats-officedocument.theme+xml'],
   ['/ppt/presProps.xml','application/vnd.openxmlformats-officedocument.presentationml.presProps+xml'],['/ppt/viewProps.xml','application/vnd.openxmlformats-officedocument.presentationml.viewProps+xml'],['/ppt/tableStyles.xml','application/vnd.openxmlformats-officedocument.presentationml.tableStyles+xml'],
   ...list.map((_,i)=>[`/ppt/slides/slide${i+1}.xml`,'application/vnd.openxmlformats-officedocument.presentationml.slide+xml'] as [string,string])]),
  '_rels/.rels':packageRels('ppt/presentation.xml'),'docProps/core.xml':core(title),
  'ppt/presentation.xml':head+`<p:presentation ${P} saveSubsetFonts="1"><p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst><p:sldIdLst>${list.map((_,i)=>`<p:sldId id="${256+i}" r:id="rId${i+6}"/>`).join('')}</p:sldIdLst><p:sldSz cx="${SW}" cy="${SH}"/><p:notesSz cx="6858000" cy="9144000"/><p:defaultTextStyle>${level(18)}</p:defaultTextStyle></p:presentation>`,
  'ppt/_rels/presentation.xml.rels':rels([['rId1','slideMaster','slideMasters/slideMaster1.xml'],['rId2','theme','theme/theme1.xml'],['rId3','presProps','presProps.xml'],['rId4','viewProps','viewProps.xml'],['rId5','tableStyles','tableStyles.xml'],...list.map((_,i)=>[`rId${i+6}`,'slide',`slides/slide${i+1}.xml`] as [string,string,string])]),
  'ppt/presProps.xml':head+`<p:presentationPr ${P}/>`,'ppt/viewProps.xml':head+`<p:viewPr ${P}><p:normalViewPr><p:restoredLeft sz="15620"/><p:restoredTop sz="94660"/></p:normalViewPr><p:gridSpacing cx="72008" cy="72008"/></p:viewPr>`,
  'ppt/tableStyles.xml':head+'<a:tblStyleLst xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" def="{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}"/>',
  'ppt/theme/theme1.xml':theme,
  'ppt/slideMasters/slideMaster1.xml':head+`<p:sldMaster ${P}><p:cSld><p:bg><p:bgRef idx="1001"><a:schemeClr val="bg1"/></p:bgRef></p:bg>${tree('')}</p:cSld><p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/><p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/></p:sldLayoutIdLst><p:txStyles><p:titleStyle>${level(30)}</p:titleStyle><p:bodyStyle>${level(20)}</p:bodyStyle><p:otherStyle>${level(18)}</p:otherStyle></p:txStyles></p:sldMaster>`,
  'ppt/slideMasters/_rels/slideMaster1.xml.rels':rels([['rId1','slideLayout','../slideLayouts/slideLayout1.xml'],['rId2','theme','../theme/theme1.xml']]),
  'ppt/slideLayouts/slideLayout1.xml':head+`<p:sldLayout ${P} type="blank" preserve="1"><p:cSld name="Blank">${tree('')}</p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>`,
  'ppt/slideLayouts/_rels/slideLayout1.xml.rels':rels([['rId1','slideMaster','../slideMasters/slideMaster1.xml']]),
 });
 return zip(files);
}


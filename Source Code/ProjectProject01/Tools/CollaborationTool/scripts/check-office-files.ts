// Writes sample .docx/.xlsx/.pptx/.csv with app/office-files.ts for opening in real Office.
// Run: npx tsx scripts/check-office-files.ts [folder]   then scripts/check-office-files.ps1 opens each file.
import {mkdirSync,writeFileSync} from 'node:fs';
import path from 'node:path';
import {deflateSync,crc32} from 'node:zlib';
import {makeDocx,makeXlsx,makeCsv,makePptx,markdownBlocks,type SlideImage} from '../app/office-files';
import {parseSlides,writeSlides} from '../app/slides';

const out=path.resolve(process.argv[2]??'outputs/office-check');mkdirSync(out,{recursive:true});
// A small solid PNG, so the check needs no image file.
function png(width:number,height:number,rgb:[number,number,number]){
 const chunk=(type:string,data:Buffer)=>{const body=Buffer.concat([Buffer.from(type,'ascii'),data]),len=Buffer.alloc(4),crc=Buffer.alloc(4);len.writeUInt32BE(data.length);crc.writeUInt32BE(crc32(body)>>>0);return Buffer.concat([len,body,crc])};
 const ihdr=Buffer.alloc(13);ihdr.writeUInt32BE(width,0);ihdr.writeUInt32BE(height,4);ihdr[8]=8;ihdr[9]=2;
 const row=Buffer.concat([Buffer.from([0]),Buffer.from(Array.from({length:width},()=>rgb).flat())]);
 return new Uint8Array(Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',ihdr),chunk('IDAT',deflateSync(Buffer.concat(Array.from({length:height},()=>row)))),chunk('IEND',Buffer.alloc(0))]));
}

const markdown='# 회의록 "주간" & <점검>\n\n- 맵 조명 마감\n- 사운드 교체\n\n1. 첫째\n2. 둘째\n\n> 결정: 금요일 빌드\n\n```\ncode line\n```\n본문 문단입니다. **굵게**와 [링크](http://x)';
writeFileSync(path.join(out,'record.docx'),makeDocx('주간 회의',[{kind:'title',text:'주간 회의'},{kind:'meta',text:'2026-11-01 · 회의 · 담당 미정'},...markdownBlocks(markdown)]));
writeFileSync(path.join(out,'records.xlsx'),makeXlsx('기록 목록',[{name:'기록 목록',rows:[['제목','종류','상태','내용'],['로비 조명','업무','진행 중','=1+1 수식처럼 보이는 글'],['첫 판 소감','메모','할 일','줄1\n줄2'],['0012','업무','완료','앞자리 0 유지']]},{name:'요약/통계?',rows:[['항목','수'],['완료','1']]}]));
writeFileSync(path.join(out,'records.csv'),makeCsv([['제목','내용'],['쉼표, 포함','"따옴표"'],['=SUM(A1)','줄\n바꿈']]));

const deck=writeSlides([{title:'알파 빌드 발표',text:'',image:null},{title:'이번 주 한 일',text:'- 로비 조명\n- 사운드 교체\n일반 문장',image:{id:'img1',width:320,height:180}},{title:'스크린샷',text:'',image:{id:'img2',width:180,height:320}},{title:'---가 들어간 본문',text:'---\n다음 줄',image:null}]);
const slides=parseSlides(deck);
if(!slides||slides.length!==4||slides[1].image?.id!=='img1'||slides[3].text.startsWith('---'))throw new Error('slide format did not round-trip: '+JSON.stringify(slides));
const images=new Map<string,SlideImage>([['img1',{bytes:png(320,180,[34,135,125]),type:'png',width:320,height:180}],['img2',{bytes:png(180,320,[192,80,58]),type:'png',width:180,height:320}]]);
writeFileSync(path.join(out,'deck.pptx'),makePptx('알파 빌드 발표',slides,images));
console.log(JSON.stringify({out,files:['record.docx','records.xlsx','records.csv','deck.pptx'],slides:slides.length}));

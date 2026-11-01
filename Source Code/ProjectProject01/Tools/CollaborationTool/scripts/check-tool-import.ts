// Reading exports of other tools (app/tool-import.ts): Notion Markdown & CSV with a nested zip, Obsidian, loose files.
// Run: npx tsx scripts/check-tool-import.ts   (HTML pages need a browser's DOMParser and are checked in the app.)
import assert from 'node:assert/strict';
import {zipSync,strToU8} from 'fflate';
import {buildPages,parseCsv,readToolExport} from '../app/tool-import';
const checks:string[]=[];const check=(name:string,value:unknown)=>{assert.ok(value,name);checks.push(name)};
const png=new Uint8Array([137,80,78,71,13,10,26,10,0,0,0,13]);
const id='1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d',sub='abcdefabcdefabcdefabcdefabcdef12',db='00112233445566778899aabbccddeeff';

const inner=zipSync({
 [`기획 ${id}.md`]:strToU8(`# 기획\n\n상태: 진행 중\n\n[회의록](%EA%B8%B0%ED%9A%8D%20${id}/%ED%9A%8C%EC%9D%98%EB%A1%9D%20${sub}.md) 참고. [웹](https://example.com)\n\n![화이트보드](%EA%B8%B0%ED%9A%8D%20${id}/board.png)\n\n![없음](missing.png)`),
 [`기획 ${id}/회의록 ${sub}.md`]:strToU8('# 회의록\n\n- [x] 맵 조명\n- [ ] 사운드'),
 [`기획 ${id}/board.png`]:png,
 [`기획 ${id}/unused.png`]:png,
 [`할 일 ${db}.csv`]:strToU8('이름,상태\n조명,완료\n'),
 [`할 일 ${db}_all.csv`]:strToU8('﻿이름,상태,메모\n조명,완료,"쉼표, 포함"\n사운드,진행,"줄\n바꿈"\n'),
 'notes.bin':new Uint8Array([1,2,3]),
});
const outer=zipSync({'Export-Part-1.zip':inner});
const notion=await readToolExport([new File([outer],'Export.zip')]);
const page=(title:string)=>notion.pages.find(p=>p.title===title)!;
check('Notion: 안쪽 zip까지 풀고 도구 이름 인식',notion.tool==='Notion'&&notion.pages.length===3);
check('Notion: 파일 이름의 32자리 id와 _all을 떼고 첫 제목을 씀',page('기획')&&page('회의록')&&page('할 일'));
check('Notion: 하위 페이지는 부모 페이지 아래로',page('회의록').parent===`기획 ${id}.md`&&page('기획').parent===null);
check('Notion: 다른 페이지 링크는 글자만, 웹 링크는 유지',page('기획').body.includes('[회의록](')===false&&page('기획').body.includes('회의록 참고')&&page('기획').body.includes('[웹](https://example.com)'));
check('Notion: 보이는 사진만 첨부로, 없는 사진은 표시',page('기획').images.length===1&&page('기획').images[0].name==='board.png'&&page('기획').body.includes('[사진: board.png]')&&page('기획').body.includes('(파일 없음)'));
check('Notion: 데이터베이스는 전체(_all) 하나만 표로',notion.pages.filter(p=>p.title==='할 일').length===1&&page('할 일').body.includes('| 사운드 | 진행 | 줄 바꿈 |')&&page('할 일').body.includes('쉼표, 포함'));
check('Notion: 쓰지 않은 사진·모르는 파일은 건너뜀',notion.skipped.some(s=>s.endsWith('unused.png'))&&notion.skipped.includes('notes.bin'));
check('Notion: 체크리스트 유지',page('회의록').body.includes('- [x] 맵 조명'));

const vault=buildPages(new Map([
 ['보관함/일지.md',strToU8('오늘 [[기획|기획 문서]]를 고쳤다.\n\n![[shot.png]]\n![[설계.pdf]]')],
 ['보관함/첨부/shot.png',png],
]));
check('Obsidian: 위키 링크는 글자로, ![[사진]]은 이름으로 찾아 첨부',vault.tool==='Obsidian'&&vault.pages[0].title==='일지'&&vault.pages[0].body.includes('기획 문서를')&&vault.pages[0].images[0]?.name==='shot.png'&&vault.pages[0].body.includes('[첨부: 설계.pdf]'));
const loose=await readToolExport([new File([strToU8('그냥 메모')],'메모.txt'),new File([strToU8('a,b\n1,2')],'표.csv')]);
check('낱개 .txt·.csv도 각각 기록 하나',loose.pages.length===2&&loose.pages.some(p=>p.title==='메모'&&p.body==='그냥 메모'));
check('CSV: 따옴표 안 쉼표·줄바꿈·겹따옴표',JSON.stringify(parseCsv('a,"b,c","d""e"\n"f\ng",h'))===JSON.stringify([['a','b,c','d"e'],['f\ng','h']]));
await assert.rejects(readToolExport([new File([new Uint8Array([1])],'x.exe')]),/가져올 페이지가 없습니다/);checks.push('읽을 페이지가 없으면 방법을 알려 줌');
console.log(JSON.stringify({passed:checks.length,checks},null,2));

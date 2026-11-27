// Commit index: every commit, its author, and the work log(s) of the docs it touched.
const fs=require('fs'),path=require('path'),{execFileSync}=require('child_process');
const repo=path.resolve(__dirname,'../../../..'),out=path.join(repo,'Source Code/ProjectProject01/Docs/WorkLogs');
const git=(...a)=>execFileSync('git',['-C',repo,...a],{encoding:'utf8',maxBuffer:256<<20});
const logs=JSON.parse(fs.readFileSync(path.join(out,'index.json'),'utf8'));
const byDoc={};for(const l of logs)byDoc[l.name.replace(/^\d{4}-\d\d-\d\d_/,'')]=l.name;
const raw=git('log','--name-only','--format=@@%h|%ad|%an|%s','--date=short').split('@@').filter(Boolean);
const rows=[],stats={};
for(const block of raw){const [head,...files]=block.trim().split('\n');const [h,d,a,...s]=head.split('|');
  const linked=[...new Set(files.filter(f=>/ProjectProject01\/Docs\/[^/]+\.md$/.test(f)).map(f=>byDoc[path.basename(f)]).filter(Boolean))];
  const m=d.slice(0,7);stats[m]=stats[m]||{};stats[m][a]=(stats[m][a]||0)+1;
  rows.push(`| \`${h}\` | ${d} | ${a} | ${s.join('|').replace(/\|/g,'/').slice(0,90)} | ${linked.map(n=>`[${n.replace(/\.md$/,'')}](${n})`).join(', ')||'-'} |`);}
const authors=[...new Set(Object.values(stats).flatMap(o=>Object.keys(o)))];
const md=['# 커밋 이력 색인 — 개발 기록','','> 생성: 2026-11-27 (KST), `Tools/WorkLogs/generate-commit-index.js`. 저장소 전체 커밋과, 그 커밋이 바꾼 기능 문서의 개발 기록을 잇는다. "-"는 연결된 기능 문서가 없는 커밋이다(맵·모델·밸런스·에셋 등). 그런 커밋의 목적·검증은 커밋 메시지 외에는 **미확인**이며, 팀원 작업일지는 `Work/<학번_이름>/WorkLog/`(docx)에 있다.','',
'## 월별·작성자별 커밋 수','',`| 월 | ${authors.join(' | ')} |`,`| --- | ${authors.map(()=>'---').join(' | ')} |`,
...Object.keys(stats).sort().map(m=>`| ${m} | ${authors.map(a=>stats[m][a]||0).join(' | ')} |`),'',
`총 ${rows.length}개. 작성자는 git 작성자 기준이며 AI(Claude/Codex) 관여는 구분되지 않는다.`,'',
'## 전체 커밋','','| 커밋 | 날짜 | 작성자 | 메시지 | 개발 기록 |','| --- | --- | --- | --- | --- |',...rows,''].join('\n');
fs.writeFileSync(path.join(out,'2026-11-27_커밋_이력_색인.md'),md);console.log(rows.length);

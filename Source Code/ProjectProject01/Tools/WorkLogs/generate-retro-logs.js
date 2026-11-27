// Builds retroactive work logs (Docs/WorkLogs) from each feature doc and the git history that touched it.
// Only what the doc or git shows is written; everything else is marked 미확인.
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const repo = path.resolve(__dirname, '../../../..');
const docs = path.join(repo, 'Source Code/ProjectProject01/Docs');
const out = path.join(docs, 'WorkLogs');
fs.mkdirSync(out, { recursive: true });
const git = (...a) => { try { return execFileSync('git', ['-C', repo, ...a], { encoding: 'utf8', maxBuffer: 64 << 20 }); } catch { return ''; } };
const rel = f => 'Source Code/ProjectProject01/Docs/' + f;
const today = '2026-11-27';
const skip = new Set(['FullCodeReviewInventory.json']);
const files = fs.readdirSync(docs).filter(f => f.endsWith('.md') && !skip.has(f));
const index = [];
const clean = l => l.trim().replace(/^(?:[-*]|\d+\.)\s+/, '').replace(/^[-*]\s+/, '').slice(0, 260);
let used = new Set();
// Each doc line lands in at most one section; headings, tables and quotes are left to the doc itself.
const pick = (lines, re, n) => { const r = []; for (const l of lines) { if (r.length >= n) break; if (/^\s*(#|\||>|```)/.test(l) || !re.test(l)) continue; const c = clean(l); if (c.length < 8 || used.has(c)) continue; used.add(c); r.push(c); } return r; };
for (const f of files) {
  const text = fs.readFileSync(path.join(docs, f), 'utf8').replace(/\r\n/g, '\n');
  const lines = text.split('\n').filter(l => l.trim());
  const title = (lines.find(l => l.startsWith('# ')) || '# ' + f).slice(2).trim();
  const log = git('log', '--follow', '--format=%h|%ad|%an|%s', '--date=short', '--', rel(f)).trim().split('\n').filter(Boolean).map(l => l.split('|'));
  const first = log.length ? log[log.length - 1][1] : today, last = log.length ? log[0][1] : today;
  const authors = {}; for (const c of log) authors[c[2]] = (authors[c[2]] || 0) + 1;
  // Files changed by the commits that touched this doc (source/tools/config only).
  const changed = {};
  for (const c of log) for (const p of git('show', '--name-only', '--format=', c[0]).split('\n'))
    if (/ProjectProject01\/(Source|Tools|Config)\//.test(p) && !/\/(bin|obj)\//.test(p)) changed[p] = (changed[p] || 0) + 1;
  const topChanged = Object.entries(changed).sort((a, b) => b[1] - a[1]).slice(0, 12);
  used = new Set();
  const verify = pick(lines, /통과|PASS|성공|검증|QA|\.log|index\.json|확인했/, 10);
  const open = pick(lines, /미완료|미검증|남은 |하지 않았|않았다|보류|TODO|후속|다음 작업|간주하지 않/, 8);
  const problems = pick(lines, /증상|문제|오류|크래시|버그|원인|재현/, 6);
  const decisions = pick(lines, /결정|확정|취소|제외|하지 않는다|금지|대신|선택|기각|이유/, 8);
  const goal = pick(lines.slice(1), /./, 3);
  const aiNote = /Claude|Codex|에이전트/.test(text) ? '원 문서에 Claude/Codex(에이전트) 관련 서술이 있다. 개별 변경의 작성 주체는 미확인.' : '커밋 작성자만으로는 AI(Claude/Codex) 관여 범위를 구분할 수 없다. 미확인.';
  const counts = []; for (const l of verify) for (const m of l.matchAll(/(\d[\d,]*)개[^.,;·]{0,24}?(통과|성공)/g)) counts.push([m[0].trim(), l]);
  const name = `${first}_${f.replace(/\.md$/, '')}.md`;
  const q = counts.length
    ? ['| 지표 | 변경 전 | 변경 후 | 변화량(후-전) | 개선율 | 측정 횟수 |', '| --- | --- | --- | --- | --- | --- |',
       ...counts.slice(0, 12).map(([c]) => `| 검사 통과 수 (높을수록 좋음): ${c.replace(/\|/g, '/')} | 기준값 없음 | 원 문서 기재값 | 계산 안 함 | 계산 안 함(기준값 없음) | 1회(원 문서 기준) |`),
       '', '검사 통과 수는 기능 검증 결과이며 성능 개선 지표가 아니다. 변경 전 기준값이 없으므로 개선율을 계산하지 않는다. 성능·시간 등 개선 지표: **정량 검증 미완료**.']
    : ['**정량 검증 미완료** — 원 문서에 측정 기준값·측정값이 없다.'];
  const md = [
    `# ${title} — 개발 기록`, '',
    `> 소급 작성: ${today} (KST). 원 문서 [\`${f}\`](../${f})와 그 문서를 바꾼 커밋으로 재구성했다. 원 문서·커밋에 없는 당시의 가설·시행착오·대화 내용은 **미확인**으로 둔다. 상세 설계·수치는 원 문서가 기준이며 여기서 반복하지 않는다.`, '',
    '## 요약', '',
    `- 목표: ${goal[0] || '미확인'}`,
    `- 기간: ${first} ~ ${last} (문서 변경 커밋 ${log.length}개 기준${log.length ? '' : ', 커밋 전'})`,
    `- 결과·검증: ${verify.find(v => /통과|성공/.test(v)) || verify[0] || '원 문서에 검증 기록 없음'}`,
    `- 현재 상태: ${open.length ? '미완료·미검증 항목 있음(아래 9-10)' : '원 문서상 남은 항목 기재 없음(미확인)'}`, '',
    '## 요청·목표·범위 (9-2)', '',
    ...(goal.length ? goal.map(g => `- ${g}`) : ['- 미확인']),
    '- 원래 요청 원문·사용자 의도: 미확인(원 문서에 요약만 있음). 사용자 발언·해석·합의 구분: 원 문서 서술 기준.', '',
    '## 기존 구조·흐름 (9-3)', '',
    `원 문서 [\`${f}\`](../${f}) 참조. 이 작업의 커밋이 바꾼 주요 파일(변경 횟수):`, '',
    ...(topChanged.length ? topChanged.map(([p, n]) => `- \`${p.replace(/^Source Code\//, '')}\` (${n})`) : ['- 커밋 기록 없음 또는 문서만 변경']),
    '', '문서를 바꾸지 않은 코드 커밋은 이 목록에 들어가지 않는다(미확인 연결).', '',
    '## 문제·증상 (9-4)', '',
    ...problems.map(l => `- ${l}`),
    '- 이 밖의 발견 계기·재현 절차: 미확인.', '',
    '## 판단·결정 (9-5)', '',
    ...(decisions.length ? decisions.map(d => `- ${d}`) : ['- 원 문서에 결정 서술 없음(미확인)']),
    '- 검토했으나 기각한 대안·근거: 원 문서에 적힌 것 외에는 미확인.', '',
    '## 구현·시행착오 (9-6)', '',
    '- 구현 내용: 원 문서와 위 변경 파일 목록 참조.',
    '- 시행착오·되돌린 변경: 원 문서에 적힌 것 외에는 미확인.', '',
    '## 검사·검증 (9-7)', '',
    ...(verify.length ? verify.map(v => `- ${v}`) : ['- 원 문서에 검증 기록 없음. 실행 여부 미확인 — 통과로 간주하지 않는다.']), '- 전체 검증 서술은 원 문서 참조.', '',
    '## 정량 결과 (9-8)', '', ...q, '',
    '## 의도 대비 달성도 (9-9)', '',
    '| 구분 | 의도한 행동/효과 | 관찰 가능한 목표·수용 기준 | 실제 결과·근거 | 달성 정도 | 남은 격차 |',
    '| --- | --- | --- | --- | --- | --- |',
    `| 기능 구현 | ${(goal[0] || '미확인').replace(/\|/g, '/').slice(0, 120)} | 원 문서에 수용 기준으로 명시되지 않음 | ${verify.length ? '원 문서 검증 기록 있음(9-7)' : '검증 기록 없음'} | 미검증 | 자동 재구성 기록이라 의도·기준·결과를 대조하지 않았다. 대조 후 갱신 |`,
    '| 실제 행동 유도 | 미확인 | 미확인 | 관찰 기록 없음 | 미검증 | 플레이 관찰 필요 |',
    '| 사용성·재미·효과 | 미확인 | 미확인 | 측정 없음 | 미검증 | 측정·의견 수집 필요 |', '',
    '## 최종 결과·기여 (9-9)', '',
    `- 커밋 작성자(이 문서를 바꾼 커밋 수): ${Object.entries(authors).map(([a, n]) => `${a} ${n}`).join(', ') || '없음'}`,
    `- AI 관여: ${aiNote}`,
    '- 배운 점·재사용 방법: 원 문서에 적힌 것 외에는 미확인.', '',
    '## 미완료·후속·인계 (9-10)', '',
    ...(open.length ? open.map(o => `- ${o}`) : ['- 원 문서에 기재 없음(미확인)']), '',
    '## 관련 커밋', '',
    ...(log.length ? ['| 커밋 | 날짜 | 작성자 | 메시지 |', '| --- | --- | --- | --- |', ...log.map(c => `| \`${c[0]}\` | ${c[1]} | ${c[2]} | ${c.slice(3).join('|').replace(/\|/g, '/')} |`)] : ['- 커밋 전']), ''
  ].join('\n');
  // Never lose hand-written work: a file this script did not make is left alone, and in one it made, every
  // section whose heading it does not write itself (e.g. "작업 세션 직접 기록") is carried over to the end.
  const target = path.join(out, name);
  if (fs.existsSync(target)) {
    const old = fs.readFileSync(target, 'utf8').replace(/\r\n/g, '\n');
    if (!old.includes('> 소급 작성:')) { index.push({ name, title, first, last, commits: log.length, open: open.length, manual: true }); continue; }
    const own = new Set(md.split('\n').filter(l => l.startsWith('## ')));
    const kept = old.split(/\n(?=## )/).filter(s => s.startsWith('## ') && !own.has(s.split('\n')[0]));
    fs.writeFileSync(target, kept.length ? md.replace(/\n*$/, '\n\n') + kept.join('\n').replace(/\n*$/, '\n') : md);
  } else fs.writeFileSync(target, md);
  index.push({ name, title, first, last, commits: log.length, open: open.length });
}
fs.writeFileSync(path.join(out, 'index.json'), JSON.stringify(index, null, 1));
console.log(index.length + ' logs');

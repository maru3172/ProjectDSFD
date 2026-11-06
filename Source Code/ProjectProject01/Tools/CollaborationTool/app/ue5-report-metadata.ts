export function reportMetadata(output:string) {
  const versions=new Set<string>();
  const commits=new Set<string>();
  const collect=(key:string,value:unknown)=>{
    const normalized=key.toLowerCase().replace(/[\s_-]/g,'');
    const token=typeof value==='string'?value.trim():'';
    if(['buildversion','빌드버전','빌드'].includes(normalized)&&/^[\p{L}\p{N}][\p{L}\p{N}._+/-]{0,79}$/u.test(token))versions.add(token);
    if(['gitcommit','commit','commitsha','gitsha','커밋','깃커밋'].includes(normalized)&&/^[a-f0-9]{7,64}$/i.test(token))commits.add(token.toLowerCase());
  };
  try{
    const report=JSON.parse(output.replace(/^\uFEFF/,''));
    for(const section of [report,report?.metadata]){
      if(section&&typeof section==='object'&&!Array.isArray(section)){
        for(const [key,value] of Object.entries(section))collect(key,value);
      }
    }
  }catch{
    for(const line of output.split(/\r?\n/)){
      const match=line.match(/^\s*([\p{L}_ -]+?)\s*(?:[:=·]\s*|\s+)(\S+)\s*$/u);
      if(match)collect(match[1],match[2]);
    }
  }
  return {
    version:versions.size===1?[...versions][0]:'',
    testedCommit:commits.size===1?[...commits][0]:'',
    ambiguous:versions.size>1||commits.size>1,
  };
}

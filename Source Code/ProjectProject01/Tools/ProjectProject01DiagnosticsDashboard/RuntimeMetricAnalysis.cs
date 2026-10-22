using System.Globalization;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed record RuntimeMetricRow(string Role,string Metric,string Value,string Unit,string Scope)
{
    internal double? Number { get; init; }
}
internal static class RuntimeMetricAnalysis
{
    internal static readonly string[] Keys=["ProcessHandleCount","ProcessThreadCount","TcpEndpointCount","UdpEndpointCount","ProcessCountersAgeSeconds","UObjectCreationsPerSecond","SampleWallSeconds","HttpCompletedCount","HttpServerFailureCount","HttpClientErrorCount","HttpTimeoutCount","HttpRetryCount","BackendCircuitState","BackendCircuitFailures","BackendCircuitRetryAfterSeconds","BackendCircuitRejected"];
    internal static Dictionary<string,double> Read(IReadOnlyDictionary<string,string> row)
    {
        Dictionary<string,double> result=[];
        foreach(string key in Keys)if(row.TryGetValue(key,out var text)&&double.TryParse(text,NumberStyles.Float,CultureInfo.InvariantCulture,out var value)&&double.IsFinite(value)&&value>=0)result[key]=value;
        return result;
    }
    internal static List<RuntimeMetricRow> Analyze(IReadOnlyList<PerformanceSample> samples,IReadOnlyList<DiagnosticEvent> events)
    {
        List<RuntimeMetricRow> rows=[];
        foreach(var group in samples.GroupBy(s=>(s.Role,s.NetMode)))
        {
            string role=group.Key.Role;var ordered=group.OrderBy(s=>s.GameSeconds).ToList();
            void Add(string metric,double? value,string unit,string scope)=>rows.Add(new(role,metric,value?.ToString("0.###",CultureInfo.InvariantCulture)??"측정 불가 / 표본 없음",unit,scope){Number=value});
            foreach(string key in Keys.Where(k=>k!="SampleWallSeconds"&&!k.StartsWith("Http")&&!k.StartsWith("BackendCircuit")))
            {
                var values=ordered.Where(s=>s.RuntimeCounters.ContainsKey(key)).Select(s=>s.RuntimeCounters[key]).ToList();
                Add(key,values.Count>0?values.Max():null,key.EndsWith("Seconds")?"초":key.EndsWith("PerSecond")?"개/초":"개",
                    key=="UObjectCreationsPerSecond"?"프로세스 전체 UObject 생성률 최댓값. C++ 전체 바이트 할당률과 다름":key.Contains("Endpoint")?"5초 주기 PID 소유 TCP 행/UDP 엔드포인트 최댓값. 소켓 핸들 수와 다름":"5초 주기 프로세스 값 최댓값. 같은 프로세스 PIE끼리 합산 금지");
            }
            var http=ordered.Where(s=>s.RuntimeCounters.GetValueOrDefault("SampleWallSeconds")>0&&s.RuntimeCounters.ContainsKey("HttpCompletedCount")).ToList();
            double elapsed=http.Sum(s=>s.RuntimeCounters["SampleWallSeconds"]),completed=http.Sum(s=>s.RuntimeCounters.GetValueOrDefault("HttpCompletedCount"));
            double? HttpSum(string key)=>http.Count>0&&http.All(s=>s.RuntimeCounters.ContainsKey(key))?http.Sum(s=>s.RuntimeCounters[key]):null;
            Add("인증·로비 HTTP 처리량",elapsed>0?completed/elapsed:null,"완료/초","계측된 Auth/Lobby HTTP 시도만. DB TPS 또는 전체 게임 작업량 아님");
            Add("인증·로비 서버·전송 오류율",completed>0?100*HttpSum("HttpServerFailureCount")/completed:null,"%","취소는 전송 오류에서 제외. 4xx는 별도 집계");
            Add("인증·로비 4xx 비율",completed>0?100*HttpSum("HttpClientErrorCount")/completed:null,"%","잘못된 입력·인증 거부 등을 서버 내부 오류와 구분");
            Add("인증·로비 타임아웃",HttpSum("HttpTimeoutCount"),"회","관측 기간 합계; 상세 원인은 HttpAttemptDetail 이벤트");
            Add("인증·로비 재시도 예정",HttpSum("HttpRetryCount"),"회","새로 재시도를 예약한 횟수; 취소로 실제 실행되지 않을 수 있음");
            var gc=events.Where(e=>e.Role==role&&e.NetMode==group.Key.NetMode&&e.Code=="GarbageCollectionMemoryDelta").Select(e=>Fields(e.Message)).ToList();
            double span=ordered.Count>1?ordered[^1].GameSeconds-ordered[0].GameSeconds:0;
            Add("GC 관측 횟수",gc.Count,"회","PreGC~GarbageCollectComplete 이벤트. 공유 PIE 프로세스끼리 중복 합산 금지");
            Add("GC 빈도",span>0?gc.Count*60/span:null,"회/분","첫~마지막 표본의 게임 시간 기준. 짧은 캡처의 빈도는 불안정");
            var durations=gc.Where(v=>v.ContainsKey("ElapsedMilliseconds")).Select(v=>v["ElapsedMilliseconds"]).ToList();
            Add("GC 평균 경과 시간",durations.Count>0?durations.Average():null,"ms","정확한 stop-the-world 정지 시간 또는 스레드 CPU 합계가 아님");
            Add("GC 최대 경과 시간",durations.Count>0?durations.Max():null,"ms","증분 GC의 시작~완료 구간 포함");
            var physical=gc.Where(v=>v.ContainsKey("PhysicalBeforeBytes")&&v.ContainsKey("PhysicalAfterBytes")).Select(v=>(v["PhysicalBeforeBytes"]-v["PhysicalAfterBytes"])/1048576d).ToList();
            Add("GC당 평균 프로세스 RAM 순감소",physical.Count>0?physical.Average():null,"MiB","음수는 증가. GC가 실제 해제한 바이트와 동일하지 않음");
            var allocation=gc.Where(v=>v.GetValueOrDefault("AllocatorBeforeAvailable")==1&&v.GetValueOrDefault("AllocatorAfterAvailable")==1&&v.ContainsKey("AllocatorUsedBeforeBytes")&&v.ContainsKey("AllocatorUsedAfterBytes")).Select(v=>(v["AllocatorUsedBeforeBytes"]-v["AllocatorUsedAfterBytes"])/1048576d).ToList();
            Add("GC당 평균 사용 중 할당 순감소",allocation.Count>0?allocation.Average():null,"MiB","지원 할당자만. 동시 할당·정렬을 포함하는 순변화이며 정확한 객체 해제 바이트 아님");
            Add("Windows 파일 디스크립터",null,"개","전체 핸들 수를 열린 파일 수로 대체하지 않음");
            Add("C++ 전체 메모리 할당률",null,"바이트/초","경량 모드는 전역 malloc 후킹 없음. 필요할 때 Unreal Insights 메모리 추적 사용");
            var circuitSample=ordered.LastOrDefault(s=>s.RuntimeCounters.ContainsKey("BackendCircuitState"));
            string state=circuitSample?.RuntimeCounters["BackendCircuitState"] switch{0=>"정상 (Closed)",1=>"요청 차단 (Open)",2=>"복구 시험 (HalfOpen)",_=>"측정 불가 / 구형 기록"};
            rows.Add(new(role,"인증·로비 서킷 브레이커",state,"최근 상태","일시적 오류 3회 연속 시 10초 차단 → 1건 복구 시험. 일반적인 인증 거부는 장애에 포함하지 않음"));
            double? CircuitValue(string key)=>circuitSample is not null&&circuitSample.RuntimeCounters.TryGetValue(key,out double value)?value:null;
            Add("서킷 차단·복구 대기",CircuitValue("BackendCircuitRetryAfterSeconds"),"초","마지막 표본 기준. 복구 시험 중에는 해당 요청의 제한 시간까지 대기");
            Add("서킷 차단 누적 요청",CircuitValue("BackendCircuitRejected"),"회","동일 게임 인스턴스 수명 기준. 클라이언트끼리 공유하지 않음");
        }
        return rows;
    }
    private static Dictionary<string,double> Fields(string message)
    {
        Dictionary<string,double> values=[];
        foreach(var part in message.Split(';')){var pair=part.Split('=',2);if(pair.Length==2&&double.TryParse(pair[1],NumberStyles.Float,CultureInfo.InvariantCulture,out double number)&&double.IsFinite(number))values[pair[0].Trim()]=number;}
        return values;
    }
}

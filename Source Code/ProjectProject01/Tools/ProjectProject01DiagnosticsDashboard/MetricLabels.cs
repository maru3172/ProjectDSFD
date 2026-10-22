namespace ProjectProject01DiagnosticsDashboard;

internal static class MetricLabels
{
    private static readonly Dictionary<string,string> Labels=new()
    {
        ["HTTP.CompletedPerSecond"]="HTTP 처리량 (완료/초)",["HTTP.Completed"]="구간 내 HTTP 완료 수",
        ["HTTP.ServerErrorPercent"]="서버 오류율 (5xx·미처리 예외, %)",["HTTP.ClientErrorPercent"]="클라이언트 4xx 응답 비율 (%)",
        ["HTTP.Canceled"]="클라이언트 취소 수",["HTTP.ActiveRequests"]="현재 동시 요청 수",["HTTP.PeakActiveRequests"]="구간 최대 동시 요청 수",
        ["HTTP.MeanDurationMs"]="HTTP 평균 처리 시간 (ms)",["Managed.AllocationBytesPerSecond"]="관리 메모리 할당률 (바이트/초)",
        ["GC.CollectionsPerMinute"]="GC 빈도 (회/분)",["GC.Gen0Collections"]="Gen0 수집 횟수 (상위 세대 포함)",
        ["GC.Gen1Collections"]="Gen1 수집 횟수 (상위 세대 포함)",["GC.Gen2Collections"]="Gen2 수집 횟수",
        ["GC.PauseTotalMs"]="구간 내 CLR GC 일시정지 합계 (ms)",["GC.LastPauseMs"]="최근 CLR GC 일시정지 합계 (ms)",
        ["GC.LastHeapBytes"]="최근 GC 관측 힙 크기 (바이트)",["GC.LastFragmentedBytes"]="최근 GC 관측 조각화 (바이트)",
        ["ThreadPool.Threads"]="스레드 풀 생성 스레드 수",["ThreadPool.BusyWorkers"]="스레드 풀 사용 중 작업자 수",
        ["ThreadPool.MaximumWorkers"]="스레드 풀 설정 최대 작업자 수",["ThreadPool.BusyOfCreatedPercent"]="생성된 풀 스레드 대비 사용 중 비율 (%)",
        ["ThreadPool.PendingWorkItems"]="스레드 풀 대기 작업 수",["ThreadPool.BusyIOThreads"]="사용 중 IO 완료 스레드 수",
        ["ThreadPool.CompletedPerSecond"]="풀 작업 처리량 (완료/초)",["Process.Threads"]="프로세스 스레드 수 (실행 중 CPU 수 아님)",
        ["Process.Handles"]="프로세스 전체 핸들 수 (파일 수 아님)",["Process.WorkingSetBytes"]="프로세스 RAM 작업 집합 (바이트)",
        ["Process.TCPEndpoints"]="PID 소유 TCP 엔드포인트 수",["Process.UDPEndpoints"]="PID 소유 UDP 엔드포인트 수",["Process.FileDescriptors"]="열린 파일 디스크립터 수 (미지원)",
        ["db.client.connections.usage.used"]="DB 풀 사용 중 연결 수",["db.client.connections.usage.idle"]="DB 풀 유휴 연결 수",
        ["db.client.connections.max"]="DB 풀 최대 연결 수 합계",["db.client.connections.pending_requests"]="DB 풀 연결 대기 요청 수",
        ["db.client.connections.timeouts"]="DB 풀 연결 획득 타임아웃 누적 횟수",["DB.PoolUsePercent"]="DB 풀 사용률 (%)",
        ["db.client.connections.wait_time.mean_ms"]="DB 연결 획득 평균 대기 시간 (ms)",
        ["http.client.open_connections.active"]="HTTP 클라이언트 풀 활성 연결 수",["http.client.open_connections.idle"]="HTTP 클라이언트 풀 유휴 연결 수",
        ["http.client.active_requests"]="HTTP 클라이언트 진행 요청 수",["HTTP.PoolActiveSharePercent"]="현재 HTTP 풀 연결 중 활성 비율 (%)",
        ["http.client.request.time_in_queue.mean_ms"]="HTTP 연결 풀 평균 대기 시간 (ms)",["http.client.request.duration.mean_ms"]="HTTP 클라이언트 평균 요청 시간 (ms, 헤더 수신까지)",
        ["kestrel.queued_connections"]="Kestrel 대기 연결 수",["kestrel.queued_requests"]="Kestrel 대기 요청 수 (지원 프로토콜 한정)",
        ["Cache.Hits"]="음성 참가자 캐시 히트 누적",["Cache.Misses"]="음성 참가자 캐시 미스 누적",["Cache.HitPercent"]="음성 참가자 캐시 히트율 (%)",
        ["Cache.MissPercent"]="음성 참가자 캐시 미스율 (%)",["Cache.LookupMeanMs"]="캐시 사전 조회 평균 시간 (ms, 락 대기 제외)",
        ["Cache.Entries"]="캐시 참가자 항목 수",["Cache.EstimatedPayloadBytes"]="캐시 문자열 데이터 추정 크기 (바이트)",
        ["Cache.Evictions"]="캐시 용량 제거 누적 횟수",["Cache.Expirations"]="캐시 경기 묶음 TTL 만료 누적 횟수",
        ["Cache.CapacityRejected"]="캐시 용량 한도 등록 거부 누적 횟수",["Diagnostics.SampleMs"]="이번 진단 표본 수집 시간 (ms, 파일 쓰기 제외)",
        ["Circuit.ResultRecovery.State"]="경기 결과 복구 서킷 상태",["Circuit.ResultRecovery.Failures"]="결과 복구 연속 일시적 오류 수",
        ["Circuit.ResultRecovery.RetryAfterSeconds"]="결과 복구 서킷 대기 시간 (초)",["Circuit.ResultRecovery.Rejected"]="결과 복구 서킷 차단 누적 요청 수"
    };
    internal static IEnumerable<string> Keys=>Labels.Keys;
    internal static string Label(string key)=>Labels.GetValueOrDefault(key,key);
    internal static string Value(string key,double? value)=>key=="Circuit.ResultRecovery.State"?value switch{0=>"정상 (Closed)",1=>"요청 차단 (Open)",2=>"복구 시험 (HalfOpen)",_=>"측정 불가"}:value?.ToString("0.###",System.Globalization.CultureInfo.InvariantCulture)??"측정 불가 / 표본 없음";
    internal static string State(string key,string value)=>key switch
    {
        "CircuitBreaker"=>value.StartsWith("NotImplemented")?"서킷 브레이커: 사용 안 함 (이전 버전의 기록)":
            "서킷 브레이커: 경기 결과 복구 HTTP 전송에 적용. 일시적 오류 3회 → 10초 차단 → 1건 복구 시험. 보류 결과는 삭제하지 않습니다.",
        "FileDescriptors"=>"파일 디스크립터: Windows 미지원. 전체 핸들 수에는 파일 외 자원이 포함됩니다.",
        "Sockets"=>"소켓: PID 소유 IPv4/IPv6 TCP 행·UDP 엔드포인트이며 실제 소켓 핸들 수와 다릅니다.",
        "Cache"=>"캐시: 음성 참가자 명단만 계측. 문자열 크기는 컨테이너 오버헤드 제외 추정치, TTL 만료와 용량 제거는 구분합니다.",
        "GC"=>"GC: CLR 수집 통계. 세대별 횟수를 합산하지 마세요. 최근 GC 힙 크기는 정확한 해제량이 아닙니다.",
        "HTTPPoolCapacity"=>"HTTP 풀: 호스트·프로토콜마다 한도가 달라 전체 최대 용량 사용률 대신 연결 수·활성 비율·대기 시간을 표시합니다.",
        "Scope"=>"범위: 5초 간격 프로세스 집계. HTTP 처리량은 DB 트랜잭션 TPS와 다릅니다. 진단 자체가 복잡도 증명은 아닙니다.",
        _=>key+": "+value
    };
}

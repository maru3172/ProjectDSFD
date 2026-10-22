using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;

internal static class CircuitBreakerChecks
{
    private sealed class Clock:TimeProvider
    {
        public double Seconds;
        public override long TimestampFrequency=>1000;
        public override long GetTimestamp()=>(long)(Seconds*1000);
    }
    internal static async Task RunAsync(Action<bool,string> check,string root)
    {
        var clock=new Clock();var circuit=new RequestCircuitBreaker(clock);long ticket=0;
        for(int i=0;i<3;++i){check(circuit.TryAcquire(out ticket),"서킷 정상 상태 요청 허용");circuit.Complete(ticket,true);}
        check(circuit.Snapshot().State==1&&!circuit.TryAcquire(out _),"3회 연속 일시적 오류 이후 요청 차단");
        circuit.Complete(ticket,false);check(circuit.Snapshot().State==1,"늦게 도착한 이전 요청 성공으로 차단 해제 금지");
        clock.Seconds=10;check(circuit.TryAcquire(out ticket)&&circuit.Snapshot().State==2,"냉각 시간 뒤 복구 시험 진입");
        check(!circuit.TryAcquire(out _),"복구 시험은 한 건만 진행");circuit.Complete(ticket,false);
        check(circuit.Snapshot().State==0&&circuit.Snapshot().ConsecutiveFailures==0,"복구 시험 성공 후 정상 복귀");
        circuit.TryAcquire(out ticket);circuit.Complete(ticket,true);circuit.TryAcquire(out ticket);circuit.Complete(ticket,false);
        check(circuit.Snapshot().State==0&&circuit.Snapshot().ConsecutiveFailures==0,"정상 응답·일반 4xx는 연속 장애 초기화");
        circuit.TryAcquire(out ticket);circuit.Complete(ticket,true,true);
        check(circuit.Snapshot().ConsecutiveFailures==0,"사용자 취소는 정상 상태에서 장애로 누적하지 않음");
        for(int i=0;i<3;++i){circuit.TryAcquire(out ticket);circuit.Complete(ticket,true);}
        clock.Seconds=20;circuit.TryAcquire(out ticket);clock.Seconds=27;
        check(!circuit.TryAcquire(out _)&&circuit.Snapshot().State==1,"완료 없는 복구 시험도 유한 제한 시간 뒤 재차단");
        clock.Seconds=37;int admitted=0;Parallel.For(0,100,i=>{if(circuit.TryAcquire(out _))Interlocked.Increment(ref admitted);});
        check(admitted==1,"동시 요청 100개에서도 복구 시험 단 한 건");
        var independent=new RequestCircuitBreaker(clock);check(independent.TryAcquire(out _)&&independent.Snapshot().State==0,"각 보호 경로·인스턴스 상태 분리");
        await OutboxPreservation(check,root);
    }
    private sealed class Responses:HttpMessageHandler
    {
        public int Calls;
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken ct)
        {++Calls;return Task.FromResult(new HttpResponseMessage(Calls<=3?HttpStatusCode.ServiceUnavailable:HttpStatusCode.OK));}
    }
    private static async Task OutboxPreservation(Action<bool,string> check,string root)
    {
        string directory=Path.Combine(root,"CircuitOutbox");Directory.CreateDirectory(directory);
        string match=Guid.NewGuid().ToString();
        string[] names={VersionCompatibilityOptions.ClientBuildHeader,VersionCompatibilityOptions.DedicatedServerBuildHeader,VersionCompatibilityOptions.ApiVersionHeader,VersionCompatibilityOptions.GameDataVersionHeader,VersionCompatibilityOptions.NetworkProtocolHeader};
        var headers=names.ToDictionary(n=>n,_=>"fixture");
        string payload=JsonSerializer.Serialize(new{schema=1,route="/api/server/matches/complete",backend="http://127.0.0.1:19999/",headers,body=JsonSerializer.Serialize(new{matchId=match})});
        string digest=Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(payload))),path=Path.Combine(directory,digest+".json");
        File.WriteAllText(path,JsonSerializer.Serialize(new{payload,sha1=digest}));
        var clock=new Clock();var circuit=new RequestCircuitBreaker(clock);using var handler=new Responses();using var client=new HttpClient(handler);
        for(int i=0;i<4;++i)await MatchResultRecoveryWorker.DrainFileAsync(path,["http://127.0.0.1:19999"],new string('z',32),client,NullLogger.Instance,CancellationToken.None,circuit);
        check(handler.Calls==3&&File.Exists(path)&&circuit.Snapshot().State==1,"실제 결과 복구 경로에서 차단 중 HTTP 미전송·보류 결과 보존");
        clock.Seconds=10;await MatchResultRecoveryWorker.DrainFileAsync(path,["http://127.0.0.1:19999"],new string('z',32),client,NullLogger.Instance,CancellationToken.None,circuit);
        check(handler.Calls==4&&!File.Exists(path)&&circuit.Snapshot().State==0,"복구 시험 성공한 결과만 기존 절차로 완료 처리");
    }
}

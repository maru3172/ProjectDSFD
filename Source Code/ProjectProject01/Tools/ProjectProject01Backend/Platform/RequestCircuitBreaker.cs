// Transport protection only. Does not create requests, retry writes, delete outbox files or block local logout.
internal sealed class RequestCircuitBreaker(TimeProvider? timeProvider=null)
{
    internal sealed record CircuitSnapshot(int State,int ConsecutiveFailures,double RetryAfterSeconds,long Rejected);
    private readonly TimeProvider clock=timeProvider??TimeProvider.System;
    private readonly object gate=new();
    private int state,failures;
    private long epoch=1,rejected;
    private double deadline;
    internal const int FailureThreshold=3;
    internal const double CooldownSeconds=10;
    private double Now=>clock.GetTimestamp()/(double)clock.TimestampFrequency;
    internal bool TryAcquire(out long ticket,double requestTimeoutSeconds=5)
    {
        lock(gate)
        {
            ticket=0;double now=Now;
            if(state==2&&now>=deadline)Trip(now);
            if(state==1)
            {
                if(now<deadline){++rejected;return false;}
                state=2;deadline=now+Math.Clamp(requestTimeoutSeconds,2,60)+1;ticket=++epoch;return true;
            }
            if(state==2){++rejected;return false;}
            ticket=epoch;return true;
        }
    }
    internal void Complete(long ticket,bool transientFailure,bool canceled=false)
    {
        lock(gate)
        {
            if(ticket==0||ticket!=epoch)return;
            if(canceled){if(state==2)Trip(Now);return;}
            if(state==2)
            {
                if(transientFailure)Trip(Now);else{state=0;failures=0;deadline=0;++epoch;}
            }
            else if(state==0)
            {
                if(!transientFailure)failures=0;else if(++failures>=FailureThreshold)Trip(Now);
            }
        }
    }
    internal CircuitSnapshot Snapshot(){lock(gate)return new(state,failures,state==0?0:Math.Max(0,deadline-Now),rejected);}
    private void Trip(double now){state=1;deadline=now+CooldownSeconds;++epoch;}
}

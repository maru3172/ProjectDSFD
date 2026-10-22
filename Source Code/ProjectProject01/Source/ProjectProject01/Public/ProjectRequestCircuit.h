#pragma once
#include "CoreMinimal.h"

enum class EProjectRequestCircuitState:uint8 { Closed,Open,HalfOpen };

/** Game-instance local, game-thread only. No timer, request replay, allocation hook or network side effect. */
struct FProjectRequestCircuit
{
    struct FSnapshot {EProjectRequestCircuitState State;int32 Failures;double RetryAfterSeconds;uint64 Rejected;};
    bool TryAcquire(const FString& InOrigin,double Now,double RequestTimeoutSeconds,uint64& Ticket)
    {
        Ticket=0;
        if(Origin!=InOrigin){Origin=InOrigin;State=EProjectRequestCircuitState::Closed;Failures=0;Deadline=0;++Epoch;}
        if(State==EProjectRequestCircuitState::HalfOpen&&Now>=Deadline)Trip(Now);
        if(State==EProjectRequestCircuitState::Open)
        {
            if(Now<Deadline){++Rejected;return false;}
            State=EProjectRequestCircuitState::HalfOpen;Deadline=Now+FMath::Clamp(RequestTimeoutSeconds,2.0,60.0)+1;++Epoch;
            Ticket=Epoch;return true; // Exactly one trial. Other callers see HalfOpen and wait.
        }
        if(State==EProjectRequestCircuitState::HalfOpen){++Rejected;return false;}
        Ticket=Epoch;return true;
    }
    void Complete(uint64 Ticket,double Now,bool bTransientFailure,bool bCancelled=false)
    {
        if(Ticket==0||Ticket!=Epoch)return; // Late requests from a previous state/origin cannot reset the circuit.
        if(bCancelled)
        {
            if(State==EProjectRequestCircuitState::HalfOpen)Trip(Now);
            return;
        }
        if(State==EProjectRequestCircuitState::HalfOpen)
        {
            if(bTransientFailure)Trip(Now);
            else{State=EProjectRequestCircuitState::Closed;Failures=0;Deadline=0;++Epoch;}
        }
        else if(State==EProjectRequestCircuitState::Closed)
        {
            if(!bTransientFailure)Failures=0;
            else if(++Failures>=FailureThreshold)Trip(Now);
        }
    }
    FSnapshot Snapshot(double Now)const{return {State,Failures,State==EProjectRequestCircuitState::Closed?0:FMath::Max(0.0,Deadline-Now),Rejected};}
    static constexpr int32 FailureThreshold=3;
    static constexpr double CooldownSeconds=10;
private:
    void Trip(double Now){State=EProjectRequestCircuitState::Open;Deadline=Now+CooldownSeconds;++Epoch;}
    EProjectRequestCircuitState State=EProjectRequestCircuitState::Closed;
    FString Origin;
    int32 Failures=0;
    double Deadline=0;
    uint64 Epoch=1,Rejected=0;
};

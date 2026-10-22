#pragma once
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace ProjectRequestProtection
{
    inline void Transition(UGameInstance* Instance,EProjectRequestCircuitState Before,const FProjectRequestCircuit::FSnapshot& After)
    {
        if(Before==After.State||!IsValid(Instance))return;
        UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(Instance->GetWorld(),EProjectProject01DiagnosticSeverity::Normal,TEXT("CircuitStateChanged"),
            FString::Printf(TEXT("Scope=AuthLobby;State=%d;ConsecutiveFailures=%d;RetryAfterSeconds=%.3f;Threshold=3;CooldownSeconds=10"),int32(After.State),After.Failures,After.RetryAfterSeconds));
    }
    inline bool Acquire(UGameInstance* Instance,const FString& Origin,float Timeout,uint64& Ticket,FString& Error)
    {
        Ticket=0;auto* Project=Cast<UProjectProject01GameInstance>(Instance);if(!Project)return true;
        auto& Circuit=Project->BackendRequestCircuit;const double Now=FPlatformTime::Seconds();const auto Before=Circuit.Snapshot(Now);
        const bool bAllowed=Circuit.TryAcquire(Origin,Now,Timeout,Ticket);const auto After=Circuit.Snapshot(Now);Transition(Instance,Before.State,After);
        if(!bAllowed)Error=After.State==EProjectRequestCircuitState::HalfOpen?TEXT("서버 연결 복구를 확인 중입니다. 잠시 후 다시 시도하세요."):
            FString::Printf(TEXT("서버 연결이 반복해서 실패해 요청을 잠시 대기합니다. 약 %d초 후 다시 시도하세요."),FMath::Max(1,FMath::CeilToInt(After.RetryAfterSeconds)));
        return bAllowed;
    }
    inline void Complete(UGameInstance* Instance,uint64 Ticket,const FHttpRequestPtr& Request,const FHttpResponsePtr& Response,bool bConnected)
    {
        auto* Project=Cast<UProjectProject01GameInstance>(Instance);if(!Project||Ticket==0)return;
        const int32 Status=Response.IsValid()?Response->GetResponseCode():0;
        const bool bCancelled=Request.IsValid()&&Request->GetFailureReason()==EHttpFailureReason::Cancelled;
        const bool bTransient=!bConnected||!Response.IsValid()||Status==0||Status==408||Status==429||Status>=500;
        auto& Circuit=Project->BackendRequestCircuit;const double Now=FPlatformTime::Seconds();const auto Before=Circuit.Snapshot(Now);
        Circuit.Complete(Ticket,Now,bTransient,bCancelled);Transition(Instance,Before.State,Circuit.Snapshot(Now));
    }
}

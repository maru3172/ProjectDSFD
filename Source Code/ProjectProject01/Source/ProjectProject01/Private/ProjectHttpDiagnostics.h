#pragma once
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace ProjectHttpDiagnostics
{
    // Main Auth/Lobby attempts only; never serialize URL/body/headers or change request/retry behavior.
    inline void Complete(UWorld* World,const TCHAR* Source,const FHttpRequestPtr& Request,const FHttpResponsePtr& Response,
        bool bConnected,int32 RetryIndex,int32 MaximumRetries,bool bWillRetry,float TimeoutSeconds)
    {
        if(!UProjectProject01DiagnosticsSubsystem::IsCaptureActiveForWorld(World))return;
        const int32 Status=Response.IsValid()?Response->GetResponseCode():0;
        const EHttpFailureReason Reason=Request.IsValid()?Request->GetFailureReason():EHttpFailureReason::Other;
        const bool bTimeout=Reason==EHttpFailureReason::TimedOut||Status==408||Status==504;
        UProjectProject01DiagnosticsSubsystem::AddHttpMetrics(World,(!bConnected&&Reason!=EHttpFailureReason::Cancelled)||Status>=500,Status>=400&&Status<500,bTimeout,bWillRetry);
        if(!bConnected||Status>=400||bWillRetry)
            UProjectProject01DiagnosticsSubsystem::RecordWorldEvent(World,EProjectProject01DiagnosticSeverity::Warning,TEXT("HttpAttemptDetail"),
                FString::Printf(TEXT("Source=%s;Status=%d;Reason=%s;Attempt=%d;MaximumAttempts=%d;WillRetry=%d;NextDelaySeconds=%s;TimeoutSeconds=%.2f;ElapsedSeconds=%.3f;Circuit=SeeCircuitStateChanged"),
                    Source,Status,LexToString(Reason),RetryIndex+1,MaximumRetries+1,bWillRetry?1:0,bWillRetry?TEXT("0"):TEXT("Unavailable"),TimeoutSeconds,Request.IsValid()?Request->GetElapsedTime():0));
    }
}

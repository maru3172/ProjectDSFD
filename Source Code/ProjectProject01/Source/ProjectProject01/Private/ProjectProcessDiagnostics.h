#pragma once
#include "CoreMinimal.h"

namespace ProjectProcessDiagnostics
{
    struct FSnapshot
    {
        int32 Handles=-1,Threads=-1,TcpEndpoints=-1,UdpEndpoints=-1;
        double SampledAtSeconds=0;
    };
    /** Process-wide cached OS snapshot, refreshed at most once per five seconds, capture only. */
    FSnapshot Capture();
    void AcquireObjectCounter();
    void ReleaseObjectCounter();
    uint64 CreatedObjects();
}

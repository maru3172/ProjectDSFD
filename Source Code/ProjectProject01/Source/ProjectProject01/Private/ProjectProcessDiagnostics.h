#pragma once
#include "CoreMinimal.h"

namespace ProjectProcessDiagnostics
{
    struct FSnapshot
    {
        int32 Handles=-1,Threads=-1,TcpEndpoints=-1,UdpEndpoints=-1;
        double SampledAtSeconds=0;
    };
    /** Process-wide cached OS snapshot, capture only. The first call reads synchronously; later refreshes (at most every five seconds) run on a background task, so values may be up to ~5 s old (see SampledAtSeconds). */
    FSnapshot Capture();
    void AcquireObjectCounter();
    void ReleaseObjectCounter();
    uint64 CreatedObjects();
}

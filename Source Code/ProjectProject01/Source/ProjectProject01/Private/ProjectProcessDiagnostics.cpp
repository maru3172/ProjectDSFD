#include "ProjectProcessDiagnostics.h"
#include "UObject/UObjectArray.h"
#include "Misc/ScopeLock.h"
#include "Tasks/Task.h"
#include <atomic>
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#endif

namespace ProjectProcessDiagnostics
{
    namespace
    {
        class FCreationCounter final:public FUObjectArray::FUObjectCreateListener
        {
        public:
            std::atomic<uint64> Count{0};
            int32 Users=0;
            virtual void NotifyUObjectCreated(const UObjectBase*,int32) override{Count.fetch_add(1,std::memory_order_relaxed);}
            virtual void OnUObjectArrayShutdown() override{GUObjectArray.RemoveUObjectCreateListener(this);Users=0;}
        } Counter;
#if PLATFORM_WINDOWS
        int32 CountEndpoints(bool bTcp,ULONG Family)
        {
            ULONG Size=0;
            auto Read=[&](void* Buffer){return bTcp?GetExtendedTcpTable(Buffer,&Size,false,Family,TCP_TABLE_OWNER_PID_ALL,0):GetExtendedUdpTable(Buffer,&Size,false,Family,UDP_TABLE_OWNER_PID,0);};
            DWORD Error=Read(nullptr);
            if(Error!=ERROR_INSUFFICIENT_BUFFER&&Error!=NO_ERROR)return -1;
            for(int32 Attempt=0;Attempt<2;++Attempt)
            {
                if(Size<4||Size>16*1024*1024)return -1;
                TArray<uint8> Buffer;Buffer.SetNumUninitialized(Size);const ULONG Capacity=Size;
                Error=Read(Buffer.GetData());if(Error==ERROR_INSUFFICIENT_BUFFER)continue;if(Error!=NO_ERROR)return -1;
                const uint32 Stride=bTcp?(Family==AF_INET?sizeof(MIB_TCPROW_OWNER_PID):sizeof(MIB_TCP6ROW_OWNER_PID)):(Family==AF_INET?sizeof(MIB_UDPROW_OWNER_PID):sizeof(MIB_UDP6ROW_OWNER_PID));
                uint32 Rows=0;FMemory::Memcpy(&Rows,Buffer.GetData(),4);if(Rows>(Capacity-4)/Stride)return -1;
                int32 Result=0;
                for(uint32 Row=0;Row<Rows;++Row)
                {
                    DWORD Pid=0;FMemory::Memcpy(&Pid,Buffer.GetData()+4+Row*Stride+Stride-4,4);
                    if(Pid==GetCurrentProcessId())++Result;
                }
                return Result;
            }
            return -1;
        }
#endif
    }
    namespace
    {
        // Reads OS-wide tables (system thread snapshot, TCP/UDP owner tables). This took ~88 ms in an editor PIE
        // capture, so after the first snapshot it runs on a background task and the game thread only reads the cache.
        FSnapshot ReadOsCounters()
        {
            FSnapshot Result;
#if PLATFORM_WINDOWS
            DWORD Handles=0;if(GetProcessHandleCount(GetCurrentProcess(),&Handles))Result.Handles=int32(Handles);
            HANDLE Snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
            if(Snapshot!=INVALID_HANDLE_VALUE)
            {
                THREADENTRY32 Entry{};Entry.dwSize=sizeof(Entry);
                if(Thread32First(Snapshot,&Entry)){Result.Threads=0;do{if(Entry.th32OwnerProcessID==GetCurrentProcessId())++Result.Threads;}while(Thread32Next(Snapshot,&Entry));}
                CloseHandle(Snapshot);
            }
            const int32 Tcp4=CountEndpoints(true,AF_INET),Tcp6=CountEndpoints(true,AF_INET6),Udp4=CountEndpoints(false,AF_INET),Udp6=CountEndpoints(false,AF_INET6);
            if(Tcp4>=0&&Tcp6>=0)Result.TcpEndpoints=Tcp4+Tcp6;
            if(Udp4>=0&&Udp6>=0)Result.UdpEndpoints=Udp4+Udp6;
#endif
            Result.SampledAtSeconds=FPlatformTime::Seconds();
            return Result;
        }
        FCriticalSection CacheLock;
        FSnapshot Cached;
        bool bHasCached=false;              // game thread only
        double LastRequestSeconds=-1e9;     // game thread only
        std::atomic<bool> bRefreshInFlight{false};
    }
    FSnapshot Capture()
    {
        check(IsInGameThread());
        const double Now=FPlatformTime::Seconds();
        if(!bHasCached)
        {
            // First snapshot of the session is synchronous so the first sample reports real values, not "unavailable".
            const FSnapshot First=ReadOsCounters();
            {FScopeLock Lock(&CacheLock);Cached=First;}
            bHasCached=true;LastRequestSeconds=Now;
            return First;
        }
        if(Now-LastRequestSeconds>=5&&!bRefreshInFlight.exchange(true))
        {
            LastRequestSeconds=Now;
            UE::Tasks::Launch(UE_SOURCE_LOCATION,[]
            {
                const FSnapshot Fresh=ReadOsCounters();
                {FScopeLock Lock(&CacheLock);Cached=Fresh;}
                bRefreshInFlight.store(false);
            },UE::Tasks::ETaskPriority::BackgroundLow);
        }
        FScopeLock Lock(&CacheLock);
        return Cached;
    }
    void AcquireObjectCounter(){check(IsInGameThread());if(Counter.Users++==0)GUObjectArray.AddUObjectCreateListener(&Counter);}
    void ReleaseObjectCounter(){check(IsInGameThread());if(Counter.Users>0&&--Counter.Users==0)GUObjectArray.RemoveUObjectCreateListener(&Counter);}
    uint64 CreatedObjects(){return Counter.Count.load(std::memory_order_relaxed);}
}
#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

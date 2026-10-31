using System.Runtime.InteropServices;

// A process-owned TCP row / UDP endpoint count, NOT file descriptors or native socket handles.
// IP Helper snapshots are taken once per five-second diagnostics sample, never per request.
internal static class ProcessSocketCounters
{
    [DllImport("iphlpapi.dll")] private static extern uint GetExtendedTcpTable(IntPtr table,ref uint size,bool order,uint family,int tableClass,uint reserved);
    [DllImport("iphlpapi.dll")] private static extern uint GetExtendedUdpTable(IntPtr table,ref uint size,bool order,uint family,int tableClass,uint reserved);
    internal readonly record struct Result(int? Tcp,int? Udp);
    internal static Result Capture()
    {
        if(!OperatingSystem.IsWindows())return new(null,null);
        try
        {
            int? tcp4=Count(true,2),tcp6=Count(true,23),udp4=Count(false,2),udp6=Count(false,23);
            return new(tcp4.HasValue&&tcp6.HasValue?tcp4+tcp6:null,udp4.HasValue&&udp6.HasValue?udp4+udp6:null);
        }
        catch(Exception e)when(e is DllNotFoundException or EntryPointNotFoundException or System.ComponentModel.Win32Exception){return new(null,null);}
    }
    private static int? Count(bool tcp,uint family)
    {
        uint size=0;uint Call(IntPtr p,ref uint n)=>tcp?GetExtendedTcpTable(p,ref n,false,family,5,0):GetExtendedUdpTable(p,ref n,false,family,1,0);
        uint error=Call(IntPtr.Zero,ref size);
        if(error!=122&&error!=0)return null;
        for(int attempt=0;attempt<2;++attempt)
        {
            if(size<4||size>16*1024*1024)return null;
            uint capacity=size;IntPtr buffer=Marshal.AllocHGlobal(checked((int)capacity));
            try
            {
                error=Call(buffer,ref size);if(error==122)continue;if(error!=0)return null;
                int stride=tcp?(family==2?24:56):(family==2?12:28),pidOffset=stride-4;
                uint rows=unchecked((uint)Marshal.ReadInt32(buffer));if(rows>(capacity-4)/stride)return null;
                int result=0;
                for(int row=0;row<rows;++row)if(Marshal.ReadInt32(buffer,4+row*stride+pidOffset)==Environment.ProcessId)++result;
                return result;
            }
            finally{Marshal.FreeHGlobal(buffer);}
        }
        return null;
    }
}

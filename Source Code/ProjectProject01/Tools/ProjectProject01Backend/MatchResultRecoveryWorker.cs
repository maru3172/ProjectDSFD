using System.Net;
using System.Net.NetworkInformation;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;

// Server-local journal only; there is deliberately no public upload/import endpoint.
internal sealed class MatchResultRecoveryWorker(IWebHostEnvironment environment,IServer server,
    GameServerDemandManager manager,ProjectSecurityOptions security,ILogger<MatchResultRecoveryWorker> logger):BackgroundService
{
    private readonly Dictionary<string,DateTime> next=[];
    protected override async Task ExecuteAsync(CancellationToken ct)
    {
        using var client=new HttpClient(new HttpClientHandler{AllowAutoRedirect=false}){Timeout=TimeSpan.FromSeconds(5)};
        while(!ct.IsCancellationRequested)
        {
            try
            {
                string root=Path.GetFullPath(Path.Combine(environment.ContentRootPath,"..",".."));
                var roots=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                if(File.Exists(Path.Combine(root,"ProjectDFSD.uproject")))roots.Add(Path.Combine(root,"Saved","MatchResultOutbox"));
                string? configured=manager.ResultOutboxDirectory;
                if(configured is not null)roots.Add(configured);
                string[] addresses=server.Features.Get<IServerAddressesFeature>()?.Addresses.ToArray()??[];
                foreach(string directory in roots)
                {
                    if(!Directory.Exists(directory)||HasReparseAncestor(directory))continue;
                    foreach(string file in Directory.EnumerateFiles(directory,"*.json",new EnumerationOptions{AttributesToSkip=FileAttributes.ReparsePoint,RecurseSubdirectories=false}).Take(128))
                    {
                        if(next.TryGetValue(file,out var when)&&DateTime.UtcNow<when)continue;
                        if(next.ContainsKey(file))BackendDiagnostics.Shared.Failure("Result recovery","retry-pass",delay:30);
                        next[file]=DateTime.UtcNow.AddSeconds(30);
                        await DrainFileAsync(file,addresses,security.GameServerSharedSecret,client,logger,ct,BackendDiagnostics.Shared.ResultRecoveryCircuit);
                        if(!File.Exists(file))next.Remove(file);
                    }
                }
            }
            catch(OperationCanceledException) when(ct.IsCancellationRequested){break;}
            catch(Exception ex) when(ex is IOException or UnauthorizedAccessException or HttpRequestException or OperationCanceledException)
            {logger.LogWarning(ex,"경기 결과 복구 재시도 대기");}
            await Task.Delay(TimeSpan.FromSeconds(3),ct);
        }
    }
    internal static bool HasReparseAncestor(string path)
    {
        for(string? current=Path.GetFullPath(path);current is not null;current=Path.GetDirectoryName(current))
            if((File.Exists(current)||Directory.Exists(current))&&(File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0)return true;
        return false;
    }
    internal static bool IsTransient(int status)=>status==0||status==408||status==429||status>=500;
    internal static async Task DrainFileAsync(string path,IReadOnlyList<string> listenAddresses,string secret,HttpClient client,ILogger logger,CancellationToken ct,RequestCircuitBreaker? circuit=null)
    {
        if(secret.Length<32||HasReparseAncestor(path)||new FileInfo(path).Length>65536)return;
        FileStream lease;
        try{lease=new FileStream(path+".lock",FileMode.OpenOrCreate,FileAccess.Write,FileShare.None);}
        catch(IOException){return;}
        await using var owned=lease;
        try
        {
            using var envelope=JsonDocument.Parse(await File.ReadAllTextAsync(path,ct));
            string payload=envelope.RootElement.GetProperty("payload").GetString()??"";
            string digest=Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(payload)));
            if(digest!=envelope.RootElement.GetProperty("sha1").GetString()||!Path.GetFileName(path).Equals(digest+".json",StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Journal checksum");
            using var json=JsonDocument.Parse(payload);var item=json.RootElement;
            if(item.GetProperty("schema").GetInt32()!=1)throw new InvalidDataException("Journal schema");
            string route=item.GetProperty("route").GetString()??"";
            if(route is not("/api/server/matches/results" or "/api/server/matches/complete" or "/api/server/case-records/visit"))throw new InvalidDataException("Journal route");
            using var body=JsonDocument.Parse(item.GetProperty("body").GetString()??"");
            string matchId=body.RootElement.GetProperty("matchId").GetString()??"";
            if(!Guid.TryParse(matchId,out _))
            {
                // Old editor journals are evidence, not real matches. Keep their exact bytes without HTTP or deletion.
                bool isLocalTest=matchId.StartsWith("LocalPIE-",StringComparison.OrdinalIgnoreCase);
                string preserved=path+(isLocalTest?".local-test":".invalid");
                if(File.Exists(preserved))preserved=path+"."+Guid.NewGuid().ToString("N")+(isLocalTest?".local-test":".invalid");
                File.Move(path,preserved);
                if(isLocalTest)logger.LogInformation("에디터 로컬 테스트 경기 기록은 전송하지 않고 보관: {Path}",preserved);
                else logger.LogWarning("경기 ID 형식이 잘못된 복구 기록 보관: {Path}",preserved);
                return;
            }
            if(!Uri.TryCreate(item.GetProperty("backend").GetString(),UriKind.Absolute,out var target)||target.Scheme is not("http" or "https")||target.AbsolutePath!="/"||!string.IsNullOrEmpty(target.UserInfo))throw new InvalidDataException("Journal origin");
            // Never send this host's secret to a foreign backend, DNS name or redirect.
            if(!IPAddress.TryParse(target.Host,out var ip))return;
            bool local=IPAddress.IsLoopback(ip)||NetworkInterface.GetAllNetworkInterfaces().SelectMany(n=>n.GetIPProperties().UnicastAddresses).Any(a=>a.Address.Equals(ip));
            if(!local||!listenAddresses.Any(a=>Uri.TryCreate(a,UriKind.Absolute,out var listen)&&listen.Port==target.Port&&listen.Scheme==target.Scheme))return;
            using var request=new HttpRequestMessage(HttpMethod.Post,new Uri(target,route));
            request.Headers.Add("X-ProjectProject01-Server-Secret",secret);
            string[] keys={VersionCompatibilityOptions.ClientBuildHeader,VersionCompatibilityOptions.DedicatedServerBuildHeader,VersionCompatibilityOptions.ApiVersionHeader,VersionCompatibilityOptions.GameDataVersionHeader,VersionCompatibilityOptions.NetworkProtocolHeader};
            var headers=item.GetProperty("headers");
            foreach(string key in keys)request.Headers.Add(key,headers.GetProperty(key).GetString());
            request.Content=new StringContent(item.GetProperty("body").GetString()??"",Encoding.UTF8,"application/json");
            long circuitTicket=0;
            if(circuit is not null&&!circuit.TryAcquire(out circuitTicket))
            {
                BackendDiagnostics.Shared.Failure("Result recovery","circuit-open",delay:Math.Ceiling(circuit.Snapshot().RetryAfterSeconds));return;
            }
            HttpResponseMessage response;
            try
            {
                response=await client.SendAsync(request,HttpCompletionOption.ResponseHeadersRead,ct);
                circuit?.Complete(circuitTicket,IsTransient((int)response.StatusCode));
            }
            catch(Exception ex)when(ex is HttpRequestException or OperationCanceledException)
            {
                circuit?.Complete(circuitTicket,true,ct.IsCancellationRequested);
                BackendDiagnostics.Shared.Failure("Result recovery",ct.IsCancellationRequested?"cancellation":ex is OperationCanceledException?"timeout":"connection-failure");throw;
            }
            using var ownedResponse=response;
            if(response.IsSuccessStatusCode)File.Delete(path);
            else if(!IsTransient((int)response.StatusCode))
            {
                File.Move(path,path+".rejected",true);logger.LogError("경기 결과 복구 거부 HTTP {Status}; 보류 파일 {Path}",(int)response.StatusCode,path+".rejected");
            }
            else BackendDiagnostics.Shared.Failure("Result recovery","retry-scheduled",(int)response.StatusCode,delay:30);
        }
        catch(Exception ex) when(ex is JsonException or KeyNotFoundException or InvalidOperationException or InvalidDataException or FormatException)
        {File.Move(path,path+".corrupt",true);logger.LogError(ex,"손상된 경기 결과 저널 보존: {Path}",path+".corrupt");}
    }
}

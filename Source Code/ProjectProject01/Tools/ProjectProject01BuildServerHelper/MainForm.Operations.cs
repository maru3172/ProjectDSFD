using System.Text.RegularExpressions;

namespace ProjectProject01BuildServerHelper;
internal sealed partial class MainForm
{
    private Task OpenRoomCapacityAsync()
    {
        using var dialog=new RoomCapacityForm((method,endpoint,body)=>
        {
            var request=CreateLocalAdminRequest(method,endpoint);
            if(body is not null)request.Content=new StringContent(System.Text.Json.JsonSerializer.Serialize(body),System.Text.Encoding.UTF8,"application/json");
            return request;
        },maximum=>{ApplyRoomCapacity(maximum);SaveHelperSettings();});
        dialog.ShowDialog(this);return Task.CompletedTask;
    }
    private async Task ApplyConcurrentCapacityAsync()
    {
        int maximum=(int)serverCount.Value;
        using var request=CreateLocalAdminRequest(HttpMethod.Put,"/api/admin/game-server-demand/capacity");
        request.Content=new StringContent(System.Text.Json.JsonSerializer.Serialize(new {maximumConcurrent=maximum}),System.Text.Encoding.UTF8,"application/json");
        using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(8)};
        using var response=await http.SendAsync(request,lifetime.Token);
        using var json=System.Text.Json.JsonDocument.Parse(await response.Content.ReadAsStringAsync());
        if(!response.IsSuccessStatusCode)
            throw new InvalidOperationException(json.RootElement.TryGetProperty("message",out var message)?message.GetString():$"HTTP {(int)response.StatusCode}");
        SaveHelperSettings();
        bool enabled=json.RootElement.GetProperty("enabled").GetBoolean();
        SetStatus($"동시 경기 한도 {maximum}개 적용 완료");
        Append($"[OK] 최대 동시 경기 수 {maximum}개 적용·저장 완료. "+(enabled?"이후 시작하는 경기부터 적용됩니다.":"운영은 중지 상태입니다. '게임 서버 운영 시작'을 누르면 이 한도를 사용합니다."));
        Append("[안내] 동시 경기 한도를 늘렸다면 IP 직접 연결 방화벽 설정의 포트 범위도 확인하세요.");
    }
    private async Task StopBackendAsync()
    {
        // Only the backend owned by this helper is stopped; do not shut down an unrelated backend.
        if(backendProcess is null||backendProcess.HasExited)return;
        try{await ConfigureDemandAsync(new {enabled=false});await WaitForDemandShutdownAsync();}
        catch(Exception ex) when(ex is HttpRequestException or OperationCanceledException or InvalidOperationException or IOException)
        {Append("[주의] 경기 세션 정상 종료 확인 실패. 백엔드 종료를 계속합니다. "+ex.Message);}
        StopProcess(backendProcess,"백엔드");
    }
    private void ApplyRoomCapacity(int maximum)
    {
        maximum=Math.Clamp(maximum,1,100);
        if(serverCount.Value>maximum)serverCount.Value=maximum;
        serverCount.Maximum=maximum;
        serverCapacityLabel.Text=$"방 한도: {maximum}개 · 경기 시작 때만 세션 실행 · 대기 중 0개";
    }
    private async Task ConfigureDemandAsync(object configuration)
    {
        using var request=CreateLocalAdminRequest(HttpMethod.Put,"/api/admin/game-server-demand");
        request.Content=new StringContent(System.Text.Json.JsonSerializer.Serialize(configuration),System.Text.Encoding.UTF8,"application/json");
        using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(8)};
        using var response=await http.SendAsync(request,lifetime.Token);
        if(!response.IsSuccessStatusCode)throw new InvalidOperationException($"경기 세션 운영 설정 실패: HTTP {(int)response.StatusCode} "+await response.Content.ReadAsStringAsync());
    }
    private HttpRequestMessage CreateLocalAdminRequest(HttpMethod method,string endpoint)
    {
        string path=Path.Combine(ProjectRoot(),"Tools","ProjectProject01Backend","LocalMySql","ProjectProject01Backend.local.json");
        using var config=System.Text.Json.JsonDocument.Parse(File.ReadAllText(path));
        string key=config.RootElement.GetProperty("Administrator").GetProperty("ApiKey").GetString()??"";
        var request=new HttpRequestMessage(method,"http://127.0.0.1:5080"+endpoint);
        request.Headers.Add("X-ProjectProject01-Admin-Key",key);return request;
    }
    private async Task WaitForDemandShutdownAsync()
    {
        using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(3)};
        for(int attempt=0;attempt<10;attempt++)
        {
            using var request=CreateLocalAdminRequest(HttpMethod.Get,"/api/admin/game-server-demand");
            using var response=await http.SendAsync(request,lifetime.Token);response.EnsureSuccessStatusCode();
            using var json=System.Text.Json.JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            if(json.RootElement.GetProperty("activeSessions").GetInt32()==0)return;
            await Task.Delay(1000,lifetime.Token);
        }
        Append("[주의] 정상 종료 대기 시간이 지났습니다. 남은 게임 서버 프로세스를 중지합니다.");
    }
    private async Task<int> ReadRoomCapacityAsync()
    {
        string path=Path.Combine(ProjectRoot(),"Tools","ProjectProject01Backend","LocalMySql","ProjectProject01Backend.local.json");
        using var config=System.Text.Json.JsonDocument.Parse(File.ReadAllText(path));
        string key=config.RootElement.GetProperty("Administrator").GetProperty("ApiKey").GetString()??"";
        using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(5)};
        using var request=new HttpRequestMessage(HttpMethod.Get,"http://127.0.0.1:5080/api/admin/room-capacity");
        request.Headers.Add("X-ProjectProject01-Admin-Key",key);
        using var response=await http.SendAsync(request);
        response.EnsureSuccessStatusCode();
        using var body=System.Text.Json.JsonDocument.Parse(await response.Content.ReadAsStringAsync());
        return body.RootElement.GetProperty("maximumRooms").GetInt32();
    }
    private int GetServerFirstPort()
    {
        var matches=Regex.Matches(serverArgs.Text,@"(?i)(?<!\S)-port=(\d+)");
        int port=matches.Count==0?7777:int.Parse(matches[0].Groups[1].Value);
        if(matches.Count>1||port<1024||port+(int)serverCount.Value-1>65535)
            throw new InvalidOperationException("서버 포트는 1024~65535 안의 연속 범위여야 합니다. -port는 한 번만 지정하세요.");
        return port;
    }
    private string GetServerPortRange() => serverCount.Value==1?GetServerFirstPort().ToString():$"{GetServerFirstPort()}-{GetServerFirstPort()+(int)serverCount.Value-1}";

    private Task OpenDatabaseRecoveryAsync()
    {
        using var dialog=new DatabaseRecoveryForm(ProjectRoot(),()=> backendProcess is {HasExited:false}||IsPortListening(5080)||HasPackagedServers(),
            async (action,file)=> await RunDatabaseRecoveryActionAsync(action,file));
        dialog.ShowDialog(this);return Task.CompletedTask;
    }
    private bool HasPackagedServers()
    {
        var processes=FindPackagedGameServerProcesses();
        try{return processes.Count>0;}finally{foreach(var process in processes)process.Dispose();}
    }
    private async Task RunDatabaseRecoveryActionAsync(string action,string? file)
    {
        string script=Path.Combine(ProjectRoot(),"Tools","ProjectProject01Backend","ProjectProject01MySql.ps1");
        string arguments=$"-NoProfile -ExecutionPolicy Bypass -File \"{script}\" -Action {action}";
        if(file is not null) arguments+=$" -BackupFile \"{file}\"";
        if(action=="Restore") arguments+=" -ConfirmRestore";
        int exit=await RunCapturedAsync("powershell.exe",arguments,ProjectRoot());
        if(exit!=0) throw new InvalidOperationException("백업/복원 명령에 실패했습니다. 도우미 로그를 확인하세요.");
    }
}

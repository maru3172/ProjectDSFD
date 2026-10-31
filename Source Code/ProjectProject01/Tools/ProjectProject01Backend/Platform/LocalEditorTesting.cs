using System.Net;

// Local editor testing: an editor client talking to a backend on this same computer may run
// bot and observer matches on an editor dedicated server (UnrealEditor-Cmd -server).
// Packaged clients never send the marker, so they still need the operator flags and a real
// dedicated server. The marker is honoured only over loopback, never from the network.
internal static class LocalEditorTesting
{
    internal const string ClientHeader="X-ProjectProject01-Client";
    private const string DefaultEditor=@"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe";

    internal static bool IsLocalEditorClient(HttpRequest request)=>
        request.HttpContext.Connection.RemoteIpAddress is { } ip&&IPAddress.IsLoopback(ip)&&
        string.Equals(request.Headers[ClientHeader].ToString(),"Editor",StringComparison.Ordinal);

    internal static bool BotsEnabled(HttpRequest request)=>
        Configuration(request).GetValue<bool>("Bots:EnableDedicatedMatches")||IsLocalEditorClient(request);
    internal static bool ObserversEnabled(HttpRequest request)=>
        Configuration(request).GetValue<bool>("Bots:EnableOpenObservers")||IsLocalEditorClient(request);

    // Turns on the editor dedicated server for a local editor client when nothing else is configured.
    // Returns an error message when it cannot, or null when a server path is available.
    internal static async Task<string?> EnsureEditorServerAsync(HttpRequest request,bool manualServerOnline,CancellationToken ct)
    {
        if(!IsLocalEditorClient(request))return null;
        var services=request.HttpContext.RequestServices;var manager=services.GetRequiredService<GameServerDemandManager>();
        // An operator-configured server (packaged or editor) always wins, and so does a manually started one.
        if(manager.Configured)return manager.Enabled?null:"게임 서버 운영이 중지되어 있습니다. 관리자 설정을 확인하세요.";
        if(manualServerOnline)return null;
        var configuration=Configuration(request);
        string editor=configuration["GameServer:EditorExecutable"] is { Length: >0 } custom?custom:DefaultEditor;
        string root=configuration["GameServer:EditorProjectRoot"] is { Length: >0 } project?project:
            Path.GetFullPath(Path.Combine(services.GetRequiredService<IWebHostEnvironment>().ContentRootPath,"..",".."));
        if(!File.Exists(editor)||!File.Exists(Path.Combine(root,"ProjectDFSD.uproject")))
            return "에디터 데디서버를 찾지 못했습니다. GameServer:EditorExecutable과 GameServer:EditorProjectRoot를 확인하세요.";
        // The editor server must call back into this same loopback backend.
        string backend=$"http://127.0.0.1:{request.HttpContext.Connection.LocalPort}";
        try
        {
            await manager.ConfigureAsync(new GameServerDemandConfiguration(true,editor,"",7777,1,root,true,backend),ct);
            services.GetRequiredService<ILoggerFactory>().CreateLogger("LocalEditorTesting")
                .LogInformation("로컬 에디터 테스트: 에디터 데디서버 사용 ({Backend})",backend);
            return null;
        }
        catch(InvalidOperationException ex){return ex.Message;}
    }

    private static IConfiguration Configuration(HttpRequest request)=>request.HttpContext.RequestServices.GetRequiredService<IConfiguration>();
}

using System.Diagnostics;

// Used only after the integration harness checks BOTH port 23307 and @@datadir.
// Real UE network checks must not create a production result outbox or stop files.
internal sealed class IsolatedEditorGameProcessFactory(string projectRoot) : IGameServerProcessFactory
{
    internal string RunRoot {get;}=Path.Combine(projectRoot,"Saved","FeatureChecks","EditorNetwork",Guid.NewGuid().ToString("N"));
    internal List<string> ServerLogs {get;}=[];
    public bool IsPortAvailable(int port)=>new GameServerProcessFactory().IsPortAvailable(port);
    public IManagedGameProcess Start(GameServerDemandConfiguration configuration,int port,string instanceId,string secret)
    {
        if(!configuration.EditorLocalTest||Path.GetFullPath(configuration.ProjectRoot)!=Path.GetFullPath(projectRoot)||port!=27777)
            throw new InvalidOperationException("Only the isolated editor fixture on UDP 27777 is allowed.");
        Directory.CreateDirectory(RunRoot);
        string log=Path.Combine(RunRoot,$"Server-{instanceId}.log"),stop=Path.ChangeExtension(log,".stop");ServerLogs.Add(log);
        var start=new ProcessStartInfo(configuration.Executable,$"{configuration.Arguments} -port={port} -ProjectProject01ManagedId={instanceId} -ProjectProject01ManagedStopFile=\"{stop}\" -ProjectProject01ResultOutbox=\"{Path.Combine(RunRoot,"Outbox")}\" -abslog=\"{log}\"")
            {UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden,RedirectStandardOutput=true,RedirectStandardError=true};
        start.Environment["PROJECTPROJECT01_GAME_SERVER_SECRET"]=secret;
        var process=Process.Start(start)??throw new InvalidOperationException("Isolated UE server did not start");process.BeginOutputReadLine();process.BeginErrorReadLine();
        return new OwnedProcess(process,stop);
    }
    private sealed class OwnedProcess(Process process,string stop) : IManagedGameProcess
    {
        public bool HasExited=>process.HasExited;
        public void RequestStop(){if(!process.HasExited)File.WriteAllText(stop,"stop");}
        public void Stop(){if(process.HasExited)return;RequestStop();if(!process.WaitForExit(5000)){process.Kill(true);process.WaitForExit(1000);}}
        public void Dispose(){process.Dispose();}
    }
}

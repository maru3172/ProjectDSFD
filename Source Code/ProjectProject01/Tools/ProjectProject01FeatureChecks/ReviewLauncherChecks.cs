using System.Diagnostics;
using System.Reflection;
using System.Text.Json;
using ProjectProject01ClientLauncher;
using ProjectProject01BuildServerHelper;

internal static class ReviewLauncherChecks
{
    internal static async Task RunAsync(Action<bool,string> check)
    {
        string fixture=Path.GetFullPath(Path.Combine("Saved","ReviewRemediation","Launcher",Guid.NewGuid().ToString("N")));
        string root=Path.Combine(fixture,"Client"),outside=Path.Combine(fixture,"Outside");
        Directory.CreateDirectory(root);Directory.CreateDirectory(outside);
        string executable=Path.Combine(root,"ProjectProject01.exe");
        await File.WriteAllTextAsync(executable,"fixture only; never executed");
        check(LauncherForm.ResolveInstalledExecutable(root)==executable,"F09 매니페스트 없는 첫 배포의 실행 파일은 설치 루트 안으로 해석");
        string relative="ProjectProject01/Binaries/Win64/ProjectProject01-Win64-Shipping.exe";
        string nested=Path.GetFullPath(Path.Combine(root,relative));Directory.CreateDirectory(Path.GetDirectoryName(nested)!);
        await File.WriteAllTextAsync(nested,"fixture only; never executed");
        var manifest=new ClientUpdateManifest("review","1",DateTime.UtcNow,relative,[]);
        await File.WriteAllTextAsync(Path.Combine(root,".projectproject01-update.json"),JsonSerializer.Serialize(manifest));
        check(LauncherForm.ResolveInstalledExecutable(root)==nested,"F09 설치 매니페스트의 중첩 상대 실행 경로도 정상 해석");
        await File.WriteAllTextAsync(Path.Combine(root,".projectproject01-update.json"),JsonSerializer.Serialize(manifest with {ExecutablePath="../Outside/secret.exe"}));
        ExpectRejected(()=>LauncherForm.ResolveInstalledExecutable(root),check,"F09 외부 실행 경로를 가진 매니페스트 차단");
        foreach(string path in new[]{".update-cache/resume.part",".UPDATE-CACHE/resume.json",".update-temp/release/file.pak",".update-backup/release/old.pak",".update-transaction.json",".update-install.lock","client-update-manifest.json.publishing","Manifest_DebugFiles_Win64.txt","game.pdb"})
            check(MainForm.ShouldExcludeFromClientUpdate(root,Path.Combine(root,path)),"F10 게시 산출물 제외: "+path);
        check(!MainForm.ShouldExcludeFromClientUpdate(root,executable)&&!MainForm.ShouldExcludeFromClientUpdate(root,Path.Combine(root,"Content/Paks/Game.pak")),"F10 실제 게임 실행 파일과 콘텐츠는 게시 대상 유지");

        using(var lease=ProjectOperationLock.Acquire(fixture))
            check(await ChildLockResult(fixture)==23,"F11 다른 프로세스의 패키징·게시 중복 실행 차단");
        check(await ChildLockResult(fixture)==0,"F11 선행 프로세스 잠금 해제 후 새 작업 허용");

        // Junctions need no developer-mode symlink privilege. These link only two
        // directories created by this test; no external/user files are targets.
        string junction=Path.Combine(root,"LinkedContent"),sentinel=Path.Combine(outside,"untouched.txt");
        await File.WriteAllTextAsync(sentinel,"unchanged");
        var start=new ProcessStartInfo("cmd.exe"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
        foreach(string arg in new[]{"/c","mklink","/J",junction,outside})start.ArgumentList.Add(arg);
        using(var process=Process.Start(start)!)
        {
            var stdout=process.StandardOutput.ReadToEndAsync();var stderr=process.StandardError.ReadToEndAsync();
            using var deadline=new CancellationTokenSource(TimeSpan.FromSeconds(10));
            try{await process.WaitForExitAsync(deadline.Token);}
            catch(OperationCanceledException){process.Kill(true);throw;}
            if(process.ExitCode!=0)throw new Exception("Review junction fixture creation failed: "+await stdout+await stderr);
        }
        var resolve=typeof(LauncherForm).GetMethod("ResolveInsideRoot",BindingFlags.Static|BindingFlags.NonPublic)!;
        ExpectRejected(()=>resolve.Invoke(null,[root,"LinkedContent/untouched.txt"]),check,"F12 런처는 설치 루트 안의 junction을 통한 외부 파일 접근 차단");
        check(!ClientUpdateEndpoints.IsSafePublishedPath(root,Path.Combine(junction,"untouched.txt")),"F12 업데이트 서버도 junction을 통한 외부 파일 제공 차단");
        var published=MainForm.EnumerateSafePackageFiles(root).Where(path=>!MainForm.ShouldExcludeFromClientUpdate(root,path)).ToArray();
        check(published.Contains(executable)&&published.All(path=>!path.Contains("LinkedContent",StringComparison.OrdinalIgnoreCase)),"F12 게시 도구도 연결 폴더를 따라 외부 파일을 수집하지 않음");
        ExpectRejected(()=>MainForm.EnumerateSafePackageFiles(junction).ToArray(),check,"F12 연결 폴더 자체를 게시 루트로 지정해도 차단");
        ExpectRejected(()=>resolve.Invoke(null,[junction,"untouched.txt"]),check,"F12 설치 루트 자체가 junction인 경우도 차단");
        check(await File.ReadAllTextAsync(sentinel)=="unchanged","F12 외부 대상 파일은 읽기 검사 뒤에도 변경되지 않음");
        // Keep the fixture/log for diagnosis. Never recursively delete a link.
    }

    private static void ExpectRejected(Action action,Action<bool,string> check,string name)
    {
        try{action();}
        catch(InvalidDataException){check(true,name);return;}
        catch(TargetInvocationException ex)when(ex.InnerException is InvalidDataException){check(true,name);return;}
        check(false,name);
    }
    private static async Task<int> ChildLockResult(string root)
    {
        var start=new ProcessStartInfo("dotnet"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardError=true,RedirectStandardOutput=true};
        start.ArgumentList.Add(typeof(ReviewLauncherChecks).Assembly.Location);start.ArgumentList.Add("--check-project-lock");start.ArgumentList.Add(root);
        using var child=Process.Start(start)!;using var deadline=new CancellationTokenSource(TimeSpan.FromSeconds(15));
        var stdout=child.StandardOutput.ReadToEndAsync();var stderr=child.StandardError.ReadToEndAsync();
        try{await child.WaitForExitAsync(deadline.Token);}
        catch(OperationCanceledException){child.Kill(true);throw;}
        await Task.WhenAll(stdout,stderr);return child.ExitCode;
    }
}

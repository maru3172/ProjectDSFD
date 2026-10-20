using System.Security.Cryptography;
using System.Text.RegularExpressions;
using ProjectProject01BuildServerHelper;

internal static class PackagingReadinessChecks
{
    internal static void Run(Action<bool,string> check)
    {
        string fixture=Path.GetFullPath(Path.Combine("Saved","ReviewRemediation","Packaging",Guid.NewGuid().ToString("N")));
        string engine=Path.Combine(fixture,"Engine"),backend=Path.Combine(fixture,"Backend");
        Directory.CreateDirectory(fixture);
        string sdk=Path.Combine(engine,"Binaries","Win64",global::EosRuntimeBundle.SdkName);
        string audio=Path.Combine(engine,"Binaries","ThirdParty","Windows","XAudio2_9","x64",global::EosRuntimeBundle.AudioName);
        // Minimal PE fixtures are never loaded or executed; only copying and header checks are exercised.
        WriteFixture(sdk,0x8664); WriteFixture(audio,0x8664);
        var bundle=global::EosRuntimeBundle.Prepare(backend,engine,allowInstalledEngineFallback:false);
        check(File.Exists(bundle.Sdk)&&File.Exists(bundle.Audio),"EOS SDK와 XAudio 두 DLL을 서버 전용 폴더에 보관");
        check(SHA256.HashData(File.ReadAllBytes(sdk)).SequenceEqual(SHA256.HashData(File.ReadAllBytes(bundle.Sdk))),"EOS DLL 원본과 보관본 SHA-256 일치");
        string absent=Path.Combine(fixture,"AbsentEngine");
        var moved=global::EosRuntimeBundle.Prepare(backend,absent,"Z:\\stale\\EOS.dll","Z:\\stale\\audio.dll",false);
        check(moved==bundle,"엔진 및 이전 절대 경로가 없어도 보관된 DLL 두 개 사용");
        var helperBundle=ProjectProject01BuildServerHelper.EosRuntimeBundle.Prepare(backend,absent,allowInstalledEngineFallback:false);
        check(helperBundle==bundle,"도우미와 백엔드가 동일한 휴대용 보관 경로 사용");
        string missing=Path.Combine(fixture,"MissingBackend");
        ExpectFailure(()=>global::EosRuntimeBundle.Prepare(missing,absent,allowInstalledEngineFallback:false),check,"EOS DLL 누락 시 명확한 준비 실패");
        string wrong=Path.Combine(fixture,"x86.dll"); WriteFixture(wrong,0x14c);
        ExpectFailure(()=>global::EosRuntimeBundle.ValidateX64Dll(wrong),check,"x86 DLL 혼입 차단");
        string corrupt=Path.Combine(fixture,"corrupt.dll");File.WriteAllText(corrupt,"not a native DLL");
        ExpectFailure(()=>global::EosRuntimeBundle.ValidateX64Dll(corrupt),check,"손상된 DLL 차단");
        using(var lease=new FileStream(bundle.Sdk,FileMode.Open,FileAccess.Read,FileShare.Read))
            check(global::EosRuntimeBundle.Prepare(backend,absent,allowInstalledEngineFallback:false)==bundle,"사용 중인 정상 DLL은 덮어쓰지 않고 재사용");
        check(!Directory.GetFiles(global::EosRuntimeBundle.DirectoryFor(backend),"*.tmp").Any(),"DLL 보관 후 임시 복사본 잔류 없음");
        foreach(bool server in new[]{false,true})
        {
            string command=MainForm.BuildCookRunArguments("C:\\Project Files\\Game.uproject","C:\\Output Files",server);
            check(command.Contains(" -prereqs ")&&command.Contains(" -crashreporter "),"필수 런타임·크래시 리포터 포함 명령: "+(server?"서버":"클라이언트"));
            check(command.Contains("-project=\"C:\\Project Files\\Game.uproject\"")&&command.Contains("-archivedirectory=\"C:\\Output Files"),"공백 포함 패키징 경로 인용 유지");
            check(server?command.Contains("-noclient"):command.Contains("-clientconfig=Shipping"),"서버/클라이언트 기존 빌드 구분 유지");
        }
        check(MainForm.MissingPrerequisiteInstallers(engine).Length==3,"필수 런타임 설치 파일 누락 사전 감지");
        foreach(string name in new[]{"vc_redist.x64.exe","vc_redist.arm64.exe","GameInputRedist.msi"})
        { string path=Path.Combine(engine,"Extras","Redist","en-us",name);Directory.CreateDirectory(Path.GetDirectoryName(path)!);File.WriteAllText(path,"not executable; fixture only"); }
        check(MainForm.MissingPrerequisiteInstallers(engine).Length==0,"필수 설치 파일이 모두 있을 때 사전 검사 허용");
        string config=File.ReadAllText(Path.Combine("Config","DefaultGame.ini"));
        check(config.Contains("IncludePrerequisites=True"),"에디터 패키징 설정에도 필수 런타임 포함");
        check(config.Contains("+DirectoriesToAlwaysCook=(Path=\"/Game/MyProject/Data\")"),"경로 기반 튜닝 데이터테이블 쿠킹 보장");
        check(!Regex.IsMatch(config,@"DirectoriesToAlwaysStage[^\r\n]*MyProject/Data"),"개발용 튜닝 CSV 별도 배포 없음");
        var maps=Regex.Matches(config,@"\+MapsToCook=\(FilePath=""([^""]+)""\)");
        check(maps.Count==9&&maps.All(map=>File.Exists("Content"+map.Groups[1].Value[5..]+".umap")),"기본·목적지·튜토리얼 9개 맵 파일과 쿠킹 설정 일치");
    }

    private static void WriteFixture(string path,ushort machine)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        byte[] bytes=new byte[128];
        BitConverter.GetBytes((ushort)0x5a4d).CopyTo(bytes,0);BitConverter.GetBytes(64).CopyTo(bytes,0x3c);
        BitConverter.GetBytes(0x4550u).CopyTo(bytes,64);BitConverter.GetBytes(machine).CopyTo(bytes,68);
        BitConverter.GetBytes((ushort)0x2000).CopyTo(bytes,86);BitConverter.GetBytes((ushort)0x20b).CopyTo(bytes,88);
        File.WriteAllBytes(path,bytes);
    }
    private static void ExpectFailure(Action action,Action<bool,string> check,string name)
    { try{action();}catch(Exception e)when(e is IOException or InvalidDataException){check(true,name);return;}check(false,name); }
}

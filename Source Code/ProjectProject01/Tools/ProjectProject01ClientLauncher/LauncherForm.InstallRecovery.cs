using System.Text.Json;

namespace ProjectProject01ClientLauncher;

internal sealed record InstallChange(string Path,bool HadOriginal);
internal sealed record InstallTransaction(string OperationId,List<InstallChange> Changes,bool Committed=false);

internal sealed partial class LauncherForm
{
    private const string TransactionFileName=".update-transaction.json";

    private void WriteInstallTransaction(InstallTransaction transaction)
    {
        string path=ResolveInsideRoot(installRoot,TransactionFileName),temporary=ResolveInsideRoot(installRoot,TransactionFileName+".new");
        byte[] json=JsonSerializer.SerializeToUtf8Bytes(transaction,JsonOptions);
        using(var stream=new FileStream(temporary,FileMode.Create,FileAccess.Write,FileShare.None,4096,FileOptions.WriteThrough))
        {stream.Write(json);stream.Flush(flushToDisk:true);}
        File.Move(temporary,path,true);
    }

    // Called under the installation lock before launch, update or repair. Never trust paths from a journal.
    private void RecoverInterruptedInstall()
    {
        string path=ResolveInsideRoot(installRoot,TransactionFileName);
        if(!File.Exists(path))return;
        if(new FileInfo(path).Length>8*1024*1024)throw new InvalidDataException("업데이트 복구 기록이 너무 큽니다.");
        var transaction=JsonSerializer.Deserialize<InstallTransaction>(File.ReadAllText(path),JsonOptions)
            ??throw new InvalidDataException("업데이트 복구 기록을 읽지 못했습니다. 실행을 중단합니다.");
        if(!Guid.TryParseExact(transaction.OperationId,"N",out _)||transaction.Changes is null||transaction.Changes.Count>20001)
            throw new InvalidDataException("업데이트 복구 기록 형식이 올바르지 않습니다.");
        var changes=transaction.Changes.Select(change=>
        {
            string relative=NormalizeRelativePath(change.Path);
            return (Target:ResolveInsideRoot(installRoot,relative),Backup:ResolveInsideRoot(installRoot,Path.Combine(".update-backup",transaction.OperationId,relative)),change.HadOriginal);
        }).ToArray();
        if(!transaction.Committed)
        {
            foreach(var change in changes.Reverse())
            {
                if(change.HadOriginal)
                {
                    // No backup means this entry was not touched, or has already been recovered.
                    if(File.Exists(change.Backup))
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(change.Target)!);
                        File.Move(change.Backup,change.Target,true);
                    }
                }
                else if(File.Exists(change.Target))File.Delete(change.Target);
            }
            Append("[복구] 중단된 업데이트를 적용 전 설치 상태로 되돌렸습니다.");
        }
        File.Delete(path);
        TryDeleteDirectory(ResolveInsideRoot(installRoot,Path.Combine(".update-backup",transaction.OperationId)),"복구 완료 백업");
        TryDeleteDirectory(ResolveInsideRoot(installRoot,Path.Combine(".update-temp",transaction.OperationId)),"복구 완료 임시 폴더");
    }
}

using System.Text.Json;

internal sealed class BoundedDiagnosticWriter(string directory)
{
    internal const int MaximumSlots=8,MaximumRecordsPerSlot=120,MaximumRecordBytes=65536,MaximumFileBytes=4*1024*1024;
    private int slot,records;
    private string? capture;
    internal async Task WriteAsync(BackendDiagnosticSnapshot sample,CancellationToken ct)
    {
        byte[] data=JsonSerializer.SerializeToUtf8Bytes(sample);
        if(data.Length+1>MaximumRecordBytes)throw new InvalidDataException("Diagnostic record exceeded its fixed limit.");
        Directory.CreateDirectory(directory);
        for(string? parent=Path.GetFullPath(directory);parent is not null;parent=Path.GetDirectoryName(parent))
            if((File.Exists(parent)||Directory.Exists(parent))&&(File.GetAttributes(parent)&FileAttributes.ReparsePoint)!=0)
                throw new IOException("Diagnostic output cannot traverse reparse points.");
        string FileName()=>Path.Combine(directory,$"backend-metrics-{slot:00}.jsonl");
        string file=FileName();
        if(capture!=sample.CaptureId||records>=MaximumRecordsPerSlot||records>0&&File.Exists(file)&&new FileInfo(file).Length+data.Length+1>MaximumFileBytes)
        {if(capture is not null)slot=(slot+1)%MaximumSlots;records=0;capture=sample.CaptureId;file=FileName();}
        if(File.Exists(file)&&(File.GetAttributes(file)&FileAttributes.ReparsePoint)!=0)throw new IOException("Diagnostic file cannot be a reparse point.");
        // Only these eight tool-owned files are reused. No directory deletion/enumeration.
        await using var stream=new FileStream(file,records==0?FileMode.Create:FileMode.Append,FileAccess.Write,FileShare.Read,4096,true);
        await stream.WriteAsync(data,ct);await stream.WriteAsync(new byte[]{(byte)'\n'},ct);++records;
    }
}

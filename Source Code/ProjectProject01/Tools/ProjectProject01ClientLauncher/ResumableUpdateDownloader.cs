using System.Net;
using System.Net.Http.Headers;
using System.Security.Cryptography;
using System.Text;

namespace ProjectProject01ClientLauncher;

// Cache identity includes the server, release URL and expected content, never a manifest path.
// Only verified blocks are reused. A partial final block/file is checked before installation.
internal sealed class ResumableUpdateDownloader(string cacheRoot,Action<long> advance,Action<string> log)
{
    private static readonly TimeSpan NetworkIdleTimeout=TimeSpan.FromSeconds(15);
    private sealed class RangeUnsupportedException : Exception;

    private string CacheDirectory(string url,ClientUpdateFile file)
    {
        string key=Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes($"{new Uri(url).AbsoluteUri}\n{file.Size}\n{file.Sha256.ToLowerInvariant()}")));
        string root=Path.GetFullPath(cacheRoot);
        Directory.CreateDirectory(root);CheckNotLink(root);
        string directory=Path.Combine(root,key);Directory.CreateDirectory(directory);CheckNotLink(directory);
        return directory;
    }
    private static void CheckNotLink(string path)
    {
        if((File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)throw new InvalidDataException("업데이트 캐시에 링크된 파일/폴더를 사용할 수 없습니다.");
    }
    private static async Task<bool> MatchesAsync(string path,long size,string hash,CancellationToken ct,long offset=0)
    {
        if(!File.Exists(path))return false;CheckNotLink(path);
        await using var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read,131072,true);
        if(offset==0&&stream.Length!=size||stream.Length-offset<size)return false;
        stream.Position=offset;using var digest=IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        byte[] buffer=new byte[131072];long remaining=size;
        while(remaining>0)
        {
            int read=await stream.ReadAsync(buffer.AsMemory(0,(int)Math.Min(buffer.Length,remaining)),ct);
            if(read==0)return false;digest.AppendData(buffer,0,read);remaining-=read;
        }
        return Convert.ToHexString(digest.GetHashAndReset()).Equals(hash,StringComparison.OrdinalIgnoreCase);
    }
    private static async Task CopyAsync(string source,long offset,long count,Stream destination,CancellationToken ct)
    {
        await using var stream=new FileStream(source,FileMode.Open,FileAccess.Read,FileShare.Read,131072,true);
        stream.Position=offset;byte[] buffer=new byte[131072];
        while(count>0)
        {
            int read=await stream.ReadAsync(buffer.AsMemory(0,(int)Math.Min(count,buffer.Length)),ct);
            if(read==0)throw new EndOfStreamException("업데이트 재사용 파일을 끝까지 읽지 못했습니다.");
            await destination.WriteAsync(buffer.AsMemory(0,read),ct);count-=read;
        }
    }
    public async Task DownloadAsync(HttpClient http,string url,ClientUpdateFile file,string destination,
        string localPath,IReadOnlyList<ClientUpdateBlock>? changedBlocks,CancellationToken ct)
    {
        string cache=CacheDirectory(url,file),complete=Path.Combine(cache,"complete");
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        if(await MatchesAsync(complete,file.Size,file.Sha256,ct))
        {
            log($"[이어받기] 검증 완료된 파일 재사용: {file.Path}");advance(file.Size);
            await using var target=new FileStream(destination,FileMode.Create,FileAccess.Write,FileShare.None,131072,true);
            await CopyAsync(complete,0,file.Size,target,ct);return;
        }
        if(File.Exists(complete))File.Delete(complete);
        if(changedBlocks is not null&&file.Blocks is {Count:>0})
        {
            try
            {
                await BuildBlocksAsync(http,url,file,destination,localPath,changedBlocks,cache,ct);
                if(!await MatchesAsync(destination,file.Size,file.Sha256,ct))throw new InvalidDataException("조립한 업데이트 파일의 SHA-256이 일치하지 않습니다: "+file.Path);
                return;
            }
            catch(RangeUnsupportedException){log("[안내] 부분 다운로드 미지원: 전체 파일 방식으로 안전하게 전환합니다. "+file.Path);}
        }
        await DownloadPayloadAsync(http,url,Path.Combine(cache,"full.part"),file.Size,file.Size,file.Sha256,0,false,ct);
        File.Move(Path.Combine(cache,"full.part"),complete,true);
        await using(var target=new FileStream(destination,FileMode.Create,FileAccess.Write,FileShare.None,131072,true))
            await CopyAsync(complete,0,file.Size,target,ct);
    }
    private async Task BuildBlocksAsync(HttpClient http,string url,ClientUpdateFile file,string destination,
        string localPath,IReadOnlyList<ClientUpdateBlock> changedBlocks,string cache,CancellationToken ct)
    {
        var changed=changedBlocks.Select(b=>b.Offset).ToHashSet();
        await using var output=new FileStream(destination,FileMode.Create,FileAccess.Write,FileShare.None,131072,true);
        foreach(var block in file.Blocks!)
        {
            ct.ThrowIfCancellationRequested();string cached=Path.Combine(cache,$"{block.Offset}.block");
            if(await MatchesAsync(cached,block.Size,block.Sha256,ct))
            {
                log($"[이어받기] 검증된 블록 재사용: {file.Path} / {block.Offset}");
                if(changed.Contains(block.Offset))advance(block.Size);
                await CopyAsync(cached,0,block.Size,output,ct);continue;
            }
            if(File.Exists(cached))File.Delete(cached);
            // Recheck the local source; a plan produced earlier is not proof of its current bytes.
            if(!changed.Contains(block.Offset)&&File.Exists(localPath)&&await LocalBlockMatchesAsync(localPath,block,ct))
            {await CopyAsync(localPath,block.Offset,block.Size,output,ct);continue;}
            string partial=Path.Combine(cache,$"{block.Offset}.part");
            await DownloadPayloadAsync(http,url,partial,block.Size,file.Size,block.Sha256,block.Offset,true,ct);
            File.Move(partial,cached,true);await CopyAsync(cached,0,block.Size,output,ct);
        }
        await output.FlushAsync(ct);
    }
    private static async Task<bool> LocalBlockMatchesAsync(string path,ClientUpdateBlock block,CancellationToken ct)
    {
        // A local installation can contain more than one block, unlike a cached block file.
        await using var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read,131072,true);
        if(stream.Length<block.Offset+block.Size)return false;
        stream.Position=block.Offset;using var digest=IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        byte[] buffer=new byte[131072];int remaining=block.Size;
        while(remaining>0)
        {
            int read=await stream.ReadAsync(buffer.AsMemory(0,Math.Min(remaining,buffer.Length)),ct);
            if(read==0)return false;digest.AppendData(buffer,0,read);remaining-=read;
        }
        return Convert.ToHexString(digest.GetHashAndReset()).Equals(block.Sha256,StringComparison.OrdinalIgnoreCase);
    }
    private async Task DownloadPayloadAsync(HttpClient http,string url,string partial,long size,long totalSize,string hash,long remoteOffset,bool requireRange,CancellationToken ct)
    {
        if(File.Exists(partial))CheckNotLink(partial);
        long received=File.Exists(partial)?new FileInfo(partial).Length:0;
        if(received>size){File.Delete(partial);received=0;}
        if(received==size&&await MatchesAsync(partial,size,hash,ct)){advance(size);return;}
        if(received==size){File.Delete(partial);received=0;}
        if(size==0)
        {
            await File.WriteAllBytesAsync(partial,[],ct);
            if(!await MatchesAsync(partial,0,hash,ct))throw new InvalidDataException("빈 파일의 SHA-256이 일치하지 않습니다.");
            return;
        }
        if(received>0){log($"[이어받기] 중단된 다운로드 {received:N0}바이트부터 재개합니다.");advance(received);}
        using var request=new HttpRequestMessage(HttpMethod.Get,url);
        if(requireRange||received>0)request.Headers.Range=new RangeHeaderValue(remoteOffset+received,remoteOffset+size-1);
        using var response=await SendAsync(http,request,ct);
        if(requireRange&&response.StatusCode==HttpStatusCode.OK)throw new RangeUnsupportedException();
        response.EnsureSuccessStatusCode();
        if(response.StatusCode==HttpStatusCode.PartialContent)
        {
            var range=response.Content.Headers.ContentRange;
            if(range is null||range.Unit!="bytes"||range.From!=remoteOffset+received||range.To!=remoteOffset+size-1||range.Length!=totalSize)
                throw new InvalidDataException("서버가 다른 파일 또는 잘못된 다운로드 범위를 반환했습니다.");
        }
        else if(response.StatusCode==HttpStatusCode.OK&&!requireRange)
        {
            if(received>0){advance(-received);log("[안내] 서버가 이어받기를 지원하지 않아 해당 파일만 처음부터 받습니다.");}
            received=0;
        }
        else throw new InvalidDataException("예상하지 못한 업데이트 응답입니다.");
        if(response.Content.Headers.ContentLength is long length&&length!=size-received)throw new InvalidDataException("다운로드 응답 크기가 파일 목록과 다릅니다.");
        await using(var input=await response.Content.ReadAsStreamAsync(ct))
        await using(var output=new FileStream(partial,FileMode.OpenOrCreate,FileAccess.Write,FileShare.None,131072,true))
        {
            output.SetLength(received);output.Position=received;byte[] buffer=new byte[131072];
            while(received<size)
            {
                int read=await ReadAsync(input,buffer.AsMemory(0,(int)Math.Min(buffer.Length,size-received)),ct);
                if(read==0)throw new EndOfStreamException("다운로드가 중단되었습니다. 다시 시도하면 저장한 부분부터 이어받습니다.");
                await output.WriteAsync(buffer.AsMemory(0,read),ct);received+=read;advance(read);
            }
            if(await ReadAsync(input,buffer.AsMemory(0,1),ct)!=0)throw new InvalidDataException("다운로드 응답이 예상 크기를 초과했습니다.");
            await output.FlushAsync(ct);
        }
        if(!await MatchesAsync(partial,size,hash,ct))
        {File.Delete(partial);throw new InvalidDataException("다운로드 SHA-256 검증에 실패했습니다. 해당 조각은 폐기하고 다시 받아야 합니다.");}
    }
    private static async Task<HttpResponseMessage> SendAsync(HttpClient http,HttpRequestMessage request,CancellationToken ct)
    {
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(ct);timeout.CancelAfter(NetworkIdleTimeout);
        try{return await http.SendAsync(request,HttpCompletionOption.ResponseHeadersRead,timeout.Token);}
        catch(OperationCanceledException) when(!ct.IsCancellationRequested){throw new TimeoutException("업데이트 서버의 응답이 없어 중단했습니다. 받은 부분은 보관합니다.");}
    }
    private static async Task<int> ReadAsync(Stream stream,Memory<byte> buffer,CancellationToken ct)
    {
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(ct);timeout.CancelAfter(NetworkIdleTimeout);
        try{return await stream.ReadAsync(buffer,timeout.Token);}
        catch(OperationCanceledException) when(!ct.IsCancellationRequested){throw new TimeoutException("다운로드가 15초 동안 진행되지 않아 중단했습니다. 받은 부분은 보관합니다.");}
    }
    public void Forget(string url,ClientUpdateFile file)
    {
        string directory=CacheDirectory(url,file);
        foreach(string path in Directory.EnumerateFiles(directory))
        {
            string name=Path.GetFileName(path);
            if(name=="complete"||name=="full.part"||System.Text.RegularExpressions.Regex.IsMatch(name,@"^\d+\.(block|part)$"))
            {CheckNotLink(path);File.Delete(path);}
        }
        if(!Directory.EnumerateFileSystemEntries(directory).Any())Directory.Delete(directory,false);
    }
}

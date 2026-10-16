using System.Net;
using System.Net.Http.Headers;
using System.Security.Cryptography;
using ProjectProject01ClientLauncher;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Logging;

internal static class ResumeDownloadChecks
{
    private static int passed;
    private static void Check(bool condition,string text){if(!condition)throw new Exception(text);passed++;Console.WriteLine("PASS "+text);}
    private static string Hash(byte[] bytes)=>Convert.ToHexString(SHA256.HashData(bytes));
    private sealed class BrokenStream(byte[] bytes,int cut) : MemoryStream(bytes)
    {
        public override ValueTask<int> ReadAsync(Memory<byte> buffer,CancellationToken ct=default)
        {
            ct.ThrowIfCancellationRequested();
            if(Position>=cut)throw new IOException("simulated download interruption");
            return base.ReadAsync(buffer[..Math.Min(buffer.Length,cut-(int)Position)],ct);
        }
    }
    private sealed class Source(byte[] data) : HttpMessageHandler
    {
        public byte[] Data=data;
        public readonly List<long?> Starts=[];
        public int FailRequest,Cut=8192;
        public bool IgnoreRanges,WrongRange;
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken ct)
        {
            var range=request.Headers.Range?.Ranges.Single();Starts.Add(range?.From);
            bool ranged=range is not null&&!IgnoreRanges;
            int from=ranged?(int)range!.From!.Value:0,to=ranged?(int)range!.To!.Value:Data.Length-1;
            byte[] payload=Data[from..(to+1)];
            Stream stream=Starts.Count==FailRequest?new BrokenStream(payload,Cut):new MemoryStream(payload);
            var response=new HttpResponseMessage(ranged?HttpStatusCode.PartialContent:HttpStatusCode.OK){Content=new StreamContent(stream)};
            response.Content.Headers.ContentLength=payload.Length;
            if(ranged)response.Content.Headers.ContentRange=new ContentRangeHeaderValue(from+(WrongRange?1:0),to+(WrongRange?1:0),Data.Length+(WrongRange?1:0));
            return Task.FromResult(response);
        }
    }
    private static ClientUpdateFile FileInfo(byte[] bytes,bool blocks)
    {
        List<ClientUpdateBlock>? list=null;
        if(blocks)
        {
            list=[];
            for(int offset=0;offset<bytes.Length;offset+=65536)
            {int size=Math.Min(65536,bytes.Length-offset);list.Add(new(offset,size,Hash(bytes[offset..(offset+size)])));}
        }
        return new("Content/Test.pak",bytes.Length,Hash(bytes),blocks?65536:null,list);
    }
    internal static async Task RunAsync(string root)
    {
        string test=Path.Combine(root,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(test);
        byte[] data=new byte[200000];RandomNumberGenerator.Fill(data);
        string original=Path.Combine(test,"installed.pak");await File.WriteAllTextAsync(original,"old client must stay untouched");
        string url="http://127.0.0.1:5080/files/test.pak?release=A",cache=Path.Combine(test,"cache");
        var source=new Source(data){FailRequest=2};using var http=new HttpClient(source);
        var blocks=FileInfo(data,true);var downloader=new ResumableUpdateDownloader(cache,_=>{},_=>{});
        try{await downloader.DownloadAsync(http,url,blocks,Path.Combine(test,"staging1"),original,blocks.Blocks,default);throw new Exception("Interruption expected");}
        catch(IOException){Check(await File.ReadAllTextAsync(original)=="old client must stay untouched","중단된 다운로드는 설치 파일을 변경하지 않음");}
        Check(source.Starts.Count==2&&Directory.EnumerateFiles(cache,"*.block",SearchOption.AllDirectories).Count()==1,"중단 전 검증된 블록 보관");
        source.FailRequest=0;int before=source.Starts.Count;
        await new ResumableUpdateDownloader(cache,_=>{},_=>{}).DownloadAsync(http,url,blocks,Path.Combine(test,"staging2"),original,blocks.Blocks,default);
        Check(source.Starts[before]==65536+8192,"재실행 후 중단된 블록의 남은 바이트부터 Range 이어받기");
        Check((await File.ReadAllBytesAsync(Path.Combine(test,"staging2"))).SequenceEqual(data),"이어받은 파일 전체 SHA-256 및 바이트 일치");
        before=source.Starts.Count;
        await downloader.DownloadAsync(http,url,blocks,Path.Combine(test,"staging3"),original,blocks.Blocks,default);
        Check(source.Starts.Count==before,"검증 완료된 블록은 재다운로드하지 않음");
        string cached=Directory.EnumerateFiles(cache,"*.block",SearchOption.AllDirectories).First();await File.WriteAllBytesAsync(cached,[1,2,3]);
        await downloader.DownloadAsync(http,url,blocks,Path.Combine(test,"staging4"),original,blocks.Blocks,default);
        Check(source.Starts.Count==before+1&&(await File.ReadAllBytesAsync(Path.Combine(test,"staging4"))).SequenceEqual(data),"손상 캐시는 해당 블록만 다시 받아 복구");
        before=source.Starts.Count;
        await downloader.DownloadAsync(http,url.Replace("release=A","release=B"),blocks,Path.Combine(test,"new-release"),original,blocks.Blocks,default);
        Check(source.Starts.Count==before+blocks.Blocks!.Count,"서버/릴리스가 다르면 이전 다운로드 조각과 혼합하지 않음");
        int cachedCount=Directory.EnumerateFiles(cache,"*.block",SearchOption.AllDirectories).Count();
        downloader.Forget(url,blocks);
        Check(Directory.EnumerateFiles(cache,"*.block",SearchOption.AllDirectories).Count()==cachedCount-blocks.Blocks.Count,"적용 완료 캐시는 해당 릴리스 조각만 정리");
        var full=FileInfo(data,false);var fullSource=new Source(data){FailRequest=1,Cut=20000};using var fullHttp=new HttpClient(fullSource);
        string fullCache=Path.Combine(test,"full-cache");var fullDownloader=new ResumableUpdateDownloader(fullCache,_=>{},_=>{});
        try{await fullDownloader.DownloadAsync(fullHttp,url,full,Path.Combine(test,"full1"),original,null,default);throw new Exception("Interruption expected");}catch(IOException){}
        fullSource.FailRequest=0;
        await fullDownloader.DownloadAsync(fullHttp,url,full,Path.Combine(test,"full2"),original,null,default);
        Check(fullSource.Starts.Last()==20000&&(await File.ReadAllBytesAsync(Path.Combine(test,"full2"))).SequenceEqual(data),"블록 목록 없는 구형 매니페스트도 바이트 이어받기");
        before=fullSource.Starts.Count;
        await fullDownloader.DownloadAsync(fullHttp,url,full,Path.Combine(test,"full3"),original,null,default);
        Check(fullSource.Starts.Count==before,"완료 파일 캐시 재검증 후 네트워크 없이 재사용");
        var ignore=new Source(data){IgnoreRanges=true};using var ignoreHttp=new HttpClient(ignore);
        await new ResumableUpdateDownloader(Path.Combine(test,"ignore-cache"),_=>{},_=>{}).DownloadAsync(ignoreHttp,url,blocks,Path.Combine(test,"ignored-range"),original,blocks.Blocks,default);
        Check(ignore.Starts.Count==2&&(await File.ReadAllBytesAsync(Path.Combine(test,"ignored-range"))).SequenceEqual(data),"Range 미지원 서버는 전체 파일 방식으로 안전 전환");
        var wrong=new Source(data){WrongRange=true};using var wrongHttp=new HttpClient(wrong);
        try{await new ResumableUpdateDownloader(Path.Combine(test,"wrong-cache"),_=>{},_=>{}).DownloadAsync(wrongHttp,url,blocks,Path.Combine(test,"wrong-range"),original,blocks.Blocks,default);throw new Exception("Wrong range accepted");}
        catch(InvalidDataException){Check(true,"잘못된 Content-Range 응답 거부");}
        var corrupt=full with {Sha256=new string('0',64)};
        try{await fullDownloader.DownloadAsync(fullHttp,url,corrupt,Path.Combine(test,"corrupt"),original,null,default);throw new Exception("Corrupt hash accepted");}
        catch(InvalidDataException){Check(true,"내용 해시가 다른 파일은 적용 전에 차단");}
        using var cancelled=new CancellationTokenSource();cancelled.Cancel();
        try{await downloader.DownloadAsync(http,url,blocks,Path.Combine(test,"cancelled"),original,blocks.Blocks,cancelled.Token);throw new Exception("Cancelled download continued");}
        catch(OperationCanceledException){Check(true,"사용자 중단/닫기 취소 토큰 존중");}
        Check(await File.ReadAllTextAsync(original)=="old client must stay untouched","모든 실패·이어받기 검사에서 기존 설치본 보존");
        await CheckRealHttpAsync(test,data,original);
        Console.WriteLine($"PASS: {passed} resumable download checks (simulated HTTP; isolated fixture files)");
    }
    private static async Task CheckRealHttpAsync(string root,byte[] data,string original)
    {
        var builder=WebApplication.CreateBuilder();builder.Logging.ClearProviders();
        await using var app=builder.Build();app.Urls.Add("http://127.0.0.1:0");
        string receivedRange="";
        app.MapGet("/payload",async(HttpContext context)=>
        {
            receivedRange=context.Request.Headers.Range.ToString();
            await Results.Bytes(data,"application/octet-stream",enableRangeProcessing:true).ExecuteAsync(context);
        });
        await app.StartAsync();
        try
        {
            string url=app.Urls.Single()+"/payload?release=network-check",cache=Path.Combine(root,"live-http-cache");
            var file=FileInfo(data,false);var downloader=new ResumableUpdateDownloader(cache,_=>{},_=>{});
            using(var brokenHttp=new HttpClient(new Source(data){FailRequest=1,Cut=20000}))
            {
                try{await downloader.DownloadAsync(brokenHttp,url,file,Path.Combine(root,"live-interrupted"),original,null,default);throw new Exception("Interruption expected");}
                catch(IOException){}
            }
            using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(10)};
            string target=Path.Combine(root,"live-resumed");
            await downloader.DownloadAsync(http,url,file,target,original,null,default);
            Check(receivedRange=="bytes=20000-199999","실제 Kestrel/HttpClient 경로에서도 Range 이어받기 요청 전달");
            Check((await File.ReadAllBytesAsync(target)).SequenceEqual(data),"실제 HTTP 206 응답으로 조립한 파일 바이트 일치");
        }
        finally{await app.StopAsync();}
    }
}

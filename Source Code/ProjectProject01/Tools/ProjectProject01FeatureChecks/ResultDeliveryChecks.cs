using System.Net;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using MySqlConnector;

internal static class ResultDeliveryChecks
{
    internal static async Task RunAsync(AuthDatabase database,HttpClient host,string output,Action<bool,string> check)
    {
        var users=new[]{await OperationsIntegration.Player(database),await OperationsIntegration.Player(database),await OperationsIntegration.Player(database)};
        string room=Guid.NewGuid().ToString("D"),match=Guid.NewGuid().ToString("D");
        await using(var c=await database.OpenConnectionAsync(default))
        {
            await using var tx=await c.BeginTransactionAsync();
            await using var cmd=new MySqlCommand("INSERT INTO game_rooms(id,join_code,name,host_user_id,status,match_id) VALUES(@room,@code,'보존할 경기 이름',@host,'Started',@match);",c,tx);
            cmd.Parameters.AddWithValue("@room",room);cmd.Parameters.AddWithValue("@code",Guid.NewGuid().ToString("N")[..8]);cmd.Parameters.AddWithValue("@host",users[0].Id);cmd.Parameters.AddWithValue("@match",match);await cmd.ExecuteNonQueryAsync();
            for(int i=0;i<3;i++)
            {
                await using var member=new MySqlCommand("INSERT INTO room_members(room_id,user_id,assigned_role) VALUES(@room,@user,@role);",c,tx);
                member.Parameters.AddWithValue("@room",room);member.Parameters.AddWithValue("@user",users[i].Id);member.Parameters.AddWithValue("@role",i==0?"Mannequin":"Survivor");await member.ExecuteNonQueryAsync();
            }
            await AuthoritativeMatchLedger.CaptureAsync(c,tx,match,default);await tx.CommitAsync();
        }
        using var server=new HttpClient{BaseAddress=host.BaseAddress};server.DefaultRequestHeaders.Add("X-ProjectProject01-Server-Secret",new string('s',48));
        using(var client=new HttpClient{BaseAddress=host.BaseAddress})
        {
            client.DefaultRequestHeaders.Authorization=new("Bearer",users[0].Token);
            using var denied=await client.PostAsJsonAsync("/api/leaderboards/records",new {matchId=Guid.NewGuid().ToString("D"),role="Survivor",success=true,rescueCount=1,escapeSeconds=42.0});
            check(denied.StatusCode==HttpStatusCode.Forbidden,"C01 구형 미검증 허용 옵션 true라도 Production에서 직접 점수 등록 거부");
            client.DefaultRequestHeaders.Add("X-Forwarded-For","198.51.100.7");
            using var proxyDenied=await client.PostAsJsonAsync("/api/leaderboards/records",new {matchId=Guid.NewGuid().ToString("D"),role="Survivor",success=true,rescueCount=1,escapeSeconds=42.0});
            check(proxyDenied.StatusCode==HttpStatusCode.Forbidden,"C01 loopback 프록시 조건도 미검증 점수 허용으로 오인하지 않음");
        }
        var result=new {matchId=match,userId=users[1].Id.ToString(),role="Survivor",success=true,rescueCount=2,escapeSeconds=42.125};
        for(int i=0;i<3;i++)
        {
            using var player=new HttpClient{BaseAddress=host.BaseAddress};player.DefaultRequestHeaders.Authorization=new("Bearer",users[i].Token);
            using var response=await player.PostAsync("/api/rooms/current/return",null);
            check(response.IsSuccessStatusCode,"F27 본인 방 복귀 요청 허용");
        }
        check(await Scalar(database,$"SELECT status='Started' AND match_id='{match}' FROM game_rooms WHERE id='{room}';")==1,"F27 전원 복귀 플래그만으로 진행 경기 초기화 불가");
        using(var denied=await host.PostAsJsonAsync("/api/server/matches/complete",new {matchId=match}))check(denied.StatusCode==HttpStatusCode.Unauthorized,"F27 클라이언트가 경기 종료를 승인할 수 없음");
        using(var ended=await server.PostAsJsonAsync("/api/server/matches/complete",new {matchId=match}))check(ended.IsSuccessStatusCode,"F27 서버 종료 확인 수락");
        check(await Scalar(database,$"SELECT status='Waiting' AND match_id IS NULL FROM game_rooms WHERE id='{room}';")==1,"F27 종료 확인과 전원 복귀가 모두 충족되면 초기화");
        await OperationsIntegration.Execute(database,$"DELETE FROM game_rooms WHERE id='{room}';");
        using(var response=await server.PostAsJsonAsync("/api/server/matches/results",result))check(response.IsSuccessStatusCode,"F26 방 삭제 뒤 지연된 권위 결과 등록");
        using(var response=await server.PostAsJsonAsync("/api/server/matches/results",result))check(response.IsSuccessStatusCode,"F26 동일 결과 재전송은 멱등 성공");
        using(var response=await server.PostAsJsonAsync("/api/server/matches/results",new {matchId=match,userId=users[1].Id.ToString(),role="Survivor",success=true,rescueCount=99,escapeSeconds=42.125}))check(response.StatusCode==HttpStatusCode.Conflict,"F26 재전송을 이용한 검증 점수 변경 거부");
        using(var response=await server.PostAsJsonAsync("/api/server/matches/results",new {matchId=match,userId=users[0].Id.ToString(),role="Survivor",success=true,rescueCount=2,escapeSeconds=42.125}))check(response.StatusCode==HttpStatusCode.Forbidden,"F26 과거 참가자의 역할 위조 거부");
        using(var player=new HttpClient{BaseAddress=host.BaseAddress})
        {
            player.DefaultRequestHeaders.Authorization=new("Bearer",users[1].Token);
            using var publish=await player.PostAsJsonAsync("/api/leaderboards/records",new {matchId=match,role="Survivor",success=true,rescueCount=2,escapeSeconds=42.125});
            check(publish.IsSuccessStatusCode,"F26 방이 없어도 성공 기록 게시 가능");
            await using(var connection=await database.OpenConnectionAsync(default))
            {
                // Only the isolated fixture DB: put this result genuinely below the global first 50.
                for(int index=0;index<51;index++)
                {
                    await using var rank=new MySqlCommand("INSERT INTO leaderboard_records(match_id,user_id,room_name,role,rescue_count,escape_seconds) VALUES(@match,@user,'RankFixture','Survivor',1000,1);",connection);
                    rank.Parameters.AddWithValue("@match",Guid.NewGuid().ToString("D"));rank.Parameters.AddWithValue("@user",users[2].Id);await rank.ExecuteNonQueryAsync();
                }
            }
            using(var ranked=await player.GetAsync("/api/leaderboards/Survivor?sort=overall&scope=individual"))
            {
                using var rankedJson=JsonDocument.Parse(await ranked.Content.ReadAsStringAsync());
                check(rankedJson.RootElement.GetProperty("entries").EnumerateArray().All(e=>e.GetProperty("roomName").GetString()!="보존할 경기 이름"),"C08 검사 대상이 실제 전역 top 50 밖에 있는 fixture");
            }
            using var receipt=await player.GetAsync("/api/leaderboards/records/"+match);
            check(receipt.IsSuccessStatusCode,"C08 top-50 순위 밖에서도 본인 경기 ID로 게시 영수증 조회");
        }
        using(var outsider=new HttpClient{BaseAddress=host.BaseAddress})
        {outsider.DefaultRequestHeaders.Authorization=new("Bearer",users[0].Token);using var denied=await outsider.GetAsync("/api/leaderboards/records/"+match);check(denied.StatusCode==HttpStatusCode.NotFound,"C08 다른 계정의 비공개 게시 영수증 조회 차단");}
        check(await Scalar(database,$"SELECT COUNT(*) FROM leaderboard_records WHERE match_id='{match}' AND room_name='보존할 경기 이름';")==1,"F26 게시 기록에 경기 당시 방 이름 보존");

        string root=Path.Combine(output,"ResultOutboxFixture",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        var headers=new Dictionary<string,string>{{VersionCompatibilityOptions.ClientBuildHeader,"1.0.0"},{VersionCompatibilityOptions.DedicatedServerBuildHeader,"1.0.0"},{VersionCompatibilityOptions.ApiVersionHeader,"1"},{VersionCompatibilityOptions.GameDataVersionHeader,"1"},{VersionCompatibilityOptions.NetworkProtocolHeader,"5"}};
        string Journal(string backend,string route,object body)
        {
            string payload=JsonSerializer.Serialize(new {schema=1,backend,route,body=JsonSerializer.Serialize(body),headers});
            string sha1=Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(payload))),file=Path.Combine(root,sha1+".json");
            File.WriteAllText(file,JsonSerializer.Serialize(new {payload,sha1}),new UTF8Encoding(false));return file;
        }
        string file=Journal(host.BaseAddress!.ToString(),"/api/server/matches/results",result);
        using(var transport=new HttpClient(new FlakyHandler()){Timeout=TimeSpan.FromSeconds(5)})
        {
            await MatchResultRecoveryWorker.DrainFileAsync(file,[host.BaseAddress.ToString()],new string('s',48),transport,NullLogger.Instance,default);
            check(File.Exists(file),"F26 일시적 503 응답 뒤 저널 유지");
        }
        // New transport/worker invocation simulates process restart; no in-memory request is retained.
        using(var transport=new HttpClient(new HttpClientHandler{AllowAutoRedirect=false}))
        {
            await MatchResultRecoveryWorker.DrainFileAsync(file,[host.BaseAddress.ToString()],new string('s',48),transport,NullLogger.Instance,default);
            check(!File.Exists(file),"F26 새 복구 실행이 디스크 저널을 재전송하고 ACK 후 제거");
            string foreign=Journal("http://203.0.113.1:"+host.BaseAddress.Port,"/api/server/matches/results",result);
            await MatchResultRecoveryWorker.DrainFileAsync(foreign,[host.BaseAddress.ToString()],new string('s',48),transport,NullLogger.Instance,default);
            check(File.Exists(foreign),"F26 다른 서버 주소의 저널에 이 호스트 비밀키를 보내지 않음");
            string corrupt=Journal(host.BaseAddress.ToString(),"/api/server/matches/complete",new {matchId=match});File.AppendAllText(corrupt,"broken");
            await MatchResultRecoveryWorker.DrainFileAsync(corrupt,[host.BaseAddress.ToString()],new string('s',48),transport,NullLogger.Instance,default);
            check(File.Exists(corrupt+".corrupt"),"F26 손상 파일은 보존·격리");
            string denied=Journal(host.BaseAddress.ToString(),"/api/server/matches/results",new {matchId=match,userId=users[0].Id.ToString(),role="Survivor",success=true,rescueCount=0,escapeSeconds=3});
            await MatchResultRecoveryWorker.DrainFileAsync(denied,[host.BaseAddress.ToString()],new string('s',48),transport,NullLogger.Instance,default);
            check(File.Exists(denied+".rejected"),"F26 영구 권한 거부는 무한 재시도하지 않고 보류");
            file=Journal(host.BaseAddress.ToString(),"/api/server/matches/results",result);
            using(var lease=new FileStream(file+".lock",FileMode.OpenOrCreate,FileAccess.Write,FileShare.None))
                await MatchResultRecoveryWorker.DrainFileAsync(file,[host.BaseAddress.ToString()],new string('s',48),transport,NullLogger.Instance,default);
            check(File.Exists(file),"F26 다른 프로세스가 소유한 전송 저널 중복 처리 방지");
        }
        check(new[]{0,408,429,500,503}.All(MatchResultRecoveryWorker.IsTransient)&&new[]{400,401,403,409,426}.All(n=>!MatchResultRecoveryWorker.IsTransient(n)),"F26 일시 오류와 영구 거부 분리");
    }
    private static async Task<int> Scalar(AuthDatabase database,string sql)
    {await using var c=await database.OpenConnectionAsync(default);await using var cmd=new MySqlCommand(sql,c);return Convert.ToInt32(await cmd.ExecuteScalarAsync());}
    private sealed class FlakyHandler:HttpMessageHandler
    {protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken ct)=>Task.FromResult(new HttpResponseMessage(HttpStatusCode.ServiceUnavailable));}
}

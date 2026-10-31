using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class CaseRecordChecks
{
    internal static async Task RunAsync(AuthDatabase db,HttpClient admin)
    {
        int count=0;void Check(bool ok,string name){if(!ok)throw new Exception(name);++count;Console.WriteLine("PASS "+name);}
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending){using var r=await pending;string body=await r.Content.ReadAsStringAsync();if(!r.IsSuccessStatusCode)throw new Exception($"Case records {(int)r.StatusCode}: {body}");return JsonDocument.Parse(body).RootElement.Clone();}
        async Task Expect(Task<HttpResponseMessage> pending,HttpStatusCode code,string name){using var r=await pending;Check(r.StatusCode==code,name);}
        var player=await OperationsIntegration.Player(db);using var client=new HttpClient{BaseAddress=admin.BaseAddress};client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);
        using var anonymous=new HttpClient{BaseAddress=admin.BaseAddress};
        async Task Sql(string text,params(string,object?)[] p){await using var c=await db.OpenConnectionAsync(default);await CommunityEndpoints.Exec(c,null,text,default,p);}
        try
        {
            Check(CaseRecords.Level(0)==1&&CaseRecords.Level(399)==2&&CaseRecords.Level(400)==3&&CaseRecords.Level(long.MaxValue)==100,"계정 레벨 경계·상한 안전");
            Check(CaseRecords.Periods(DateTimeOffset.Parse("2026-10-25T14:59:59Z")).Weekly=="W:2026-10-19"&&CaseRecords.Periods(DateTimeOffset.Parse("2026-10-25T15:00:00Z")).Weekly=="W:2026-10-26","한국 시간 월요일 00시 주간 초기화");
            await Expect(anonymous.GetAsync("/api/case-records"),HttpStatusCode.Unauthorized,"로그인 없이 재화·미션 조회 차단");
            var initial=await Read(client.GetAsync("/api/case-records"));Check(initial.GetProperty("level").GetInt32()==1&&initial.GetProperty("credits").GetInt64()==0,"새 계정 레벨 1·재화 0");
            Check(initial.GetProperty("missions").GetArrayLength()==8&&initial.GetProperty("season").GetProperty("free").GetBoolean(),"봇전/PvP 미션 분리·기록 트랙 무료");
            Check(initial.GetProperty("season").GetProperty("incidentTimestamp").GetString()=="202X-XX-XX 18:24:01"&&initial.GetProperty("season").GetProperty("availability").GetString()=="(~ 정식 출시 전까지)","배틀패스는 요청한 사건 시각 양식·정식 출시 전 표기 유지");
            await Expect(client.PostAsJsonAsync("/api/case-records/action",new{action="Buy",id="frame_silver",price=0}),HttpStatusCode.Conflict,"클라이언트가 가격을 0으로 위조해도 재화 부족");
            await Expect(client.PostAsJsonAsync("/api/case-records/action",new{action="Equip",id="frame_silver"}),HttpStatusCode.Conflict,"미보유 꾸미기 장착 차단");
            await Expect(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimMission",id="daily_play",mode="Pvp"}),HttpStatusCode.Conflict,"미완료 미션 보상 차단");
            await Expect(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimSeason",id="1"}),HttpStatusCode.BadRequest,"경험치만으로 조사 기록 해금 차단");
            string save=Guid.NewGuid().ToString();
            await Expect(client.PostAsJsonAsync("/api/case-records/import-campaign",new{saveId=save,codes=new[]{"campaign_checkpoint"},confirmed=false}),HttpStatusCode.BadRequest,"계정 귀속 확인 전 로컬 업적 전송 차단");
            await Expect(client.PostAsJsonAsync("/api/case-records/import-campaign",new{saveId=save,codes=new[]{"mannequin_victory"},confirmed=true}),HttpStatusCode.BadRequest,"로컬 파일로 경쟁 업적 위조 차단");
            for(int i=0;i<2;i++)await Read(client.PostAsJsonAsync("/api/case-records/import-campaign",new{saveId=save,codes=new[]{"campaign_checkpoint"},confirmed=true}));
            var imported=await Read(client.GetAsync("/api/case-records"));Check(imported.GetProperty("achievements").GetArrayLength()==1&&imported.GetProperty("credits").GetInt64()==0&&imported.GetProperty("xp").GetInt64()==0,"업적 병합은 중복되지 않고 재화/경쟁 XP를 지급하지 않음");
            Check(imported.GetProperty("inventory").GetArrayLength()==0,"달성·병합·목록 조회만으로 업적 보상을 자동 지급하지 않음");
            await Expect(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimAchievement",id="campaign_complete"}),HttpStatusCode.Conflict,"미달성 업적 보상 차단");
            var claimedAchievement=await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimAchievement",id="campaign_checkpoint"}));
            Check(claimedAchievement.GetProperty("message").GetString()==""&&claimedAchievement.GetProperty("receipt").GetProperty("title").GetString()=="획득하셨습니다"&&claimedAchievement.GetProperty("receipt").GetProperty("items")[0].GetProperty("itemId").GetString()=="stamp_checkpoint","일반 처리 완료 문구 없이 실제 획득 보상 확인은 유지");
            foreach(var task in await Task.WhenAll(Enumerable.Range(0,5).Select(_=>client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimAchievement",id="campaign_checkpoint"})))){Check(task.IsSuccessStatusCode,"동시 업적 보상 요청 멱등 처리");task.Dispose();}
            var claimedAgain=await Read(client.GetAsync("/api/case-records"));Check(claimedAgain.GetProperty("inventory").EnumerateArray().Count(i=>i.GetProperty("itemId").GetString()=="stamp_checkpoint")==1&&claimedAgain.GetProperty("achievementRewards").EnumerateArray().Single(r=>r.GetProperty("code").GetString()=="campaign_checkpoint").GetProperty("claimed").GetBoolean(),"업적 보상은 정확히 한 번 보유되며 수령 완료 유지");
            CaseAchievementRates.Invalidate(db);
            var global=await Read(anonymous.GetAsync("/api/case-records/achievement-rates"));
            long total=global.GetProperty("totalPlayers").GetInt64();var checkpointRate=global.GetProperty("achievements").EnumerateArray().Single(a=>a.GetProperty("code").GetString()=="campaign_checkpoint");
            Check(total>0&&Math.Abs(checkpointRate.GetProperty("percentage").GetDouble()-100.0*checkpointRate.GetProperty("achieved").GetInt64()/total)<.00001,"전체 계정 대비 업적별 달성률 계산");
            Check(!global.ToString().Contains("userId")&&!global.ToString().Contains("accountId"),"공개 업적 달성률에 개별 계정 정보 없음");
            Check(ReferenceEquals(await CaseAchievementRates.GetAsync(db,default),await CaseAchievementRates.GetAsync(db,default)),"동시 열람은 30초 집계 캐시 사용");
            var another=await OperationsIntegration.Player(db);
            try
            {
                using var other=new HttpClient{BaseAddress=admin.BaseAddress};other.DefaultRequestHeaders.Authorization=new("Bearer",another.Token);
                await Expect(other.PostAsJsonAsync("/api/case-records/import-campaign",new{saveId=save,codes=new[]{"campaign_checkpoint"},confirmed=true}),HttpStatusCode.Conflict,"같은 로컬 저장 ID를 다른 계정에 중복 귀속할 수 없음");
                Check((await Read(other.GetAsync("/api/case-records"))).GetProperty("achievements").GetArrayLength()==0,"귀속 거부 시 다른 계정 업적에 부분 반영 없음");
            }
            finally{await Sql("DELETE FROM users WHERE id=@u",("u",another.Id));}
            for(int i=0;i<5;i++)
            {
                string match=Guid.NewGuid().ToString();
                await Sql("INSERT INTO verified_match_results(match_id,room_id,user_id,role,success) VALUES(@m,@r,@u,'Survivor',TRUE);INSERT INTO case_match_modes(match_id,mode) VALUES(@m,'Pvp')",("m",match),("r",Guid.NewGuid().ToString()),("u",player.Id));
                await using var c=await db.OpenConnectionAsync(default);await using var tx=await c.BeginTransactionAsync(default);
                await CaseRecords.AwardVerifiedAsync(c,tx,match,player.Id,default);await CaseRecords.AwardVerifiedAsync(c,tx,match,player.Id,default);await tx.CommitAsync();
            }
            var earned=await Read(client.GetAsync("/api/case-records"));Check(earned.GetProperty("xp").GetInt64()==750&&earned.GetProperty("level").GetInt32()==3,"검증 결과 재시도는 XP를 중복 지급하지 않음");
            Check(earned.GetProperty("missions").EnumerateArray().Where(m=>m.GetProperty("mode").GetString()=="Bot").All(m=>m.GetProperty("progress").GetInt32()==0),"PvP 기록이 봇전 미션을 올리지 않음");
            foreach(var mission in CaseRecords.Missions)await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimMission",id=mission.Id,mode="Pvp"}));
            var attempts=await Task.WhenAll(Enumerable.Range(0,8).Select(_=>client.PostAsJsonAsync("/api/case-records/action",new{action="ClaimMission",id="daily_play",mode="Pvp"})));
            foreach(var response in attempts){Check(response.IsSuccessStatusCode,"동시 중복 보상 요청은 멱등 처리");response.Dispose();}
            var claimed=await Read(client.GetAsync("/api/case-records"));Check(claimed.GetProperty("credits").GetInt64()==545,"경기 성과 보상과 일일·주간 재화는 각 1회만 지급");
            for(int i=0;i<2;i++){var bought=await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="Buy",id="frame_silver"}));Check(bought.GetProperty("receipt").GetProperty("title").GetString()=="구매하셨습니다"&&bought.GetProperty("receipt").GetProperty("items")[0].GetProperty("itemId").GetString()=="frame_silver","구매 성공 응답에 실제 구매 상품 포함");}
            var purchased=await Read(client.GetAsync("/api/case-records"));Check(purchased.GetProperty("credits").GetInt64()==425&&purchased.GetProperty("inventory").EnumerateArray().Count(i=>i.GetProperty("itemId").GetString()=="frame_silver")==1&&purchased.GetProperty("inventory").GetArrayLength()==claimed.GetProperty("inventory").GetArrayLength()+1,"중복 구매 재과금 방지·가격은 서버 기준, 기존 업적 보상 보존");
            await Read(client.PostAsJsonAsync("/api/case-records/action",new{action="Equip",id="frame_silver"}));
            var equipped=await Read(client.GetAsync("/api/case-records"));Check(equipped.GetProperty("loadout")[0].GetProperty("itemId").GetString()=="frame_silver","보유 꾸미기 실제 장착 저장");
            Check(!(await Read(client.GetAsync("/api/case-records"))).GetProperty("season").TryGetProperty("requiredXp",out _),"조사 기록 트랙에 경험치 해금 조건 없음");
            await using(var c=await db.OpenConnectionAsync(default))
            {
                var publicProfile=JsonSerializer.Serialize(await CaseRecords.PublicProfileAsync(c,player.Id,default));
                Check(publicProfile.Contains("level")&&!publicProfile.Contains("credits"),"공개 프로필은 레벨·꾸미기만 공개하고 지갑은 비공개");
            }
            Console.WriteLine($"PASS: {count} case record progression checks (isolated API/MySQL)");
        }
        finally{await Sql("DELETE FROM case_match_modes WHERE match_id IN (SELECT match_id FROM case_receipts WHERE user_id=@u);DELETE FROM users WHERE id=@u",("u",player.Id));}
    }
}

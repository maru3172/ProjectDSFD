using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

// 우편함과 쿠폰 코드 (Docs/MailAndCoupons.md): sending, claiming (credits, tickets, items), expiry, recall, scheduled
// rewards arriving as mail, shared and one-time coupons, periods, limits and the wrong-code limit.
internal static class MailCouponChecks
{
    internal static async Task RunAsync(AuthDatabase db, HttpClient admin)
    {
        int count = 0; void Check(bool ok, string message) { if (!ok) throw new Exception(message); count++; Console.WriteLine("PASS " + message); }
        async Task<JsonElement> Read(Task<HttpResponseMessage> pending) { using var r = await pending; var text = await r.Content.ReadAsStringAsync(); if (!r.IsSuccessStatusCode) throw new Exception($"Mail/coupon {(int)r.StatusCode}: {text}"); return JsonDocument.Parse(text).RootElement.Clone(); }
        async Task<(HttpStatusCode Code, JsonElement Body)> Send(Task<HttpResponseMessage> pending) { using var r = await pending; var text = await r.Content.ReadAsStringAsync(); return (r.StatusCode, text.Length == 0 ? default : JsonDocument.Parse(text).RootElement.Clone()); }
        async Task<long> Scalar(string sql, params (string, object?)[] args) { await using var c = await db.OpenConnectionAsync(default); return Convert.ToInt64((await CommunityEndpoints.Rows(c, null, sql, default, args))[0]["v"]); }
        async Task Sql(string sql, params (string, object?)[] args) { await using var c = await db.OpenConnectionAsync(default); await CommunityEndpoints.Exec(c, null, sql, default, args); }
        HttpClient Client(string token) { var h = new HttpClient { BaseAddress = admin.BaseAddress }; h.DefaultRequestHeaders.Authorization = new("Bearer", token); return h; }
        var a = await OperationsIntegration.Player(db); var b = await OperationsIntegration.Player(db);
        using var ca = Client(a.Token); using var cb = Client(b.Token);
        long Credits(ulong u) => Scalar("SELECT COALESCE((SELECT credits FROM case_accounts WHERE user_id=@u),0) v", ("u", u)).GetAwaiter().GetResult();
        long Tickets(ulong u) => Scalar("SELECT COALESCE((SELECT tickets FROM case_draw_wallet WHERE user_id=@u),0) v", ("u", u)).GetAwaiter().GetResult();
        var item = CaseRecords.Catalogue[0].Id;

        // ---- Mail ----
        var mail = new { title = "격리 우편 검사", body = "보상입니다.", reward = new { credits = 30, tickets = 2, items = new[] { item } }, everyone = false, accounts = new[] { a.Account, b.Account }, expiresDays = 30, requestId = Guid.NewGuid() };
        var preview = await Read(admin.PostAsJsonAsync("/api/admin/operations/mail/preview", mail));
        Check(preview.GetProperty("recipients").GetInt32() == 2, "우편 미리 보기는 받는 사람 수만 알려 줌");
        var (bad, _) = await Send(admin.PostAsJsonAsync("/api/admin/operations/mail", mail with { reward = new { credits = 999999, tickets = 0, items = Array.Empty<string>() } }));
        Check(bad == HttpStatusCode.BadRequest, "한도를 넘는 보상은 보내지 않음");
        var sent = await Read(admin.PostAsJsonAsync("/api/admin/operations/mail", mail));
        var again = await Read(admin.PostAsJsonAsync("/api/admin/operations/mail", mail));
        Check(sent.GetProperty("id").GetInt64() == again.GetProperty("id").GetInt64() && await Scalar("SELECT COUNT(*) v FROM mail_messages WHERE batch_id=@b", ("b", sent.GetProperty("id").GetInt64())) == 2,
            "같은 요청을 다시 보내도 우편은 한 번만");
        var box = await Read(ca.GetAsync("/api/mail"));
        Check(box.GetProperty("unclaimed").GetInt32() >= 1 && box.GetProperty("mail")[0].GetProperty("items")[0].GetProperty("name").GetString() == CaseRecords.Catalogue[0].Name, "우편함에 보상과 아이템 이름이 보임");
        long mailId = box.GetProperty("mail")[0].GetProperty("id").GetInt64();
        var (cannotDelete, _) = await Send(ca.PostAsync($"/api/mail/{mailId}/delete", null));
        Check(cannotDelete == HttpStatusCode.Conflict, "받지 않은 보상 우편은 지울 수 없음");
        long before = Credits(a.Id), ticketsBefore = Tickets(a.Id);
        await Read(ca.PostAsync($"/api/mail/{mailId}/claim", null));
        Check(Credits(a.Id) == before + 30 && Tickets(a.Id) == ticketsBefore + 2 && await Scalar("SELECT COUNT(*) v FROM case_inventory WHERE user_id=@u AND item_id=@i", ("u", a.Id), ("i", item)) == 1,
            "받으면 크레딧·뽑기권·아이템이 들어옴");
        var (twice, _) = await Send(ca.PostAsync($"/api/mail/{mailId}/claim", null));
        Check(twice == HttpStatusCode.Conflict && Credits(a.Id) == before + 30, "같은 우편은 한 번만 받음");
        await Read(ca.PostAsync($"/api/mail/{mailId}/delete", null));
        Check(!(await Read(ca.GetAsync("/api/mail"))).GetProperty("mail").EnumerateArray().Any(m => m.GetProperty("id").GetInt64() == mailId), "받은 우편은 지울 수 있음");

        // Owned item skipped, expiry, claim-all, recall.
        var second = await Read(admin.PostAsJsonAsync("/api/admin/operations/mail", mail with { requestId = Guid.NewGuid(), accounts = new[] { a.Account } }));
        var expired = await Read(admin.PostAsJsonAsync("/api/admin/operations/mail", mail with { requestId = Guid.NewGuid(), accounts = new[] { a.Account }, reward = new { credits = 7, tickets = 0, items = Array.Empty<string>() } }));
        await Sql("UPDATE mail_batches SET expires_utc=UTC_TIMESTAMP(6)-INTERVAL 1 MINUTE WHERE id=@b", ("b", expired.GetProperty("id").GetInt64()));
        before = Credits(a.Id);
        var all = await Read(ca.PostAsync("/api/mail/claim-all", null));
        Check(all.GetProperty("claimed").GetInt32() == 1 && Credits(a.Id) == before + 30, "모두 받기: 기한 지난 우편은 빠지고, 이미 가진 아이템은 건너뛰고 나머지를 받음");
        var recall = await Read(admin.PostAsJsonAsync("/api/admin/operations/mail", mail with { requestId = Guid.NewGuid(), accounts = new[] { b.Account } }));
        var recalled = await Read(admin.PostAsync($"/api/admin/operations/mail/{recall.GetProperty("id").GetInt64()}/recall", null));
        Check(recalled.GetProperty("removed").GetInt32() == 1 && await Scalar("SELECT COUNT(*) v FROM mail_messages WHERE batch_id=@b", ("b", recall.GetProperty("id").GetInt64())) == 0,
            "회수하면 받지 않은 우편이 사라짐");

        // ---- Scheduled rewards arrive as mail ----
        var reward = new { title = "격리 예약 우편", credits = 40, startsAtUtc = DateTimeOffset.UtcNow, accounts = new[] { a.Account }, requestId = Guid.NewGuid() };
        long rewardId = (await Read(admin.PostAsJsonAsync("/api/admin/operations/rewards", reward))).GetProperty("id").GetInt64();
        before = Credits(a.Id);
        Check(await LiveOperations.Apply(db, rewardId, false, default) && Credits(a.Id) == before, "예약 지급은 크레딧을 바로 넣지 않고 우편으로 보냄");
        var rewardMail = (await Read(ca.GetAsync("/api/mail"))).GetProperty("mail").EnumerateArray().First(m => m.GetProperty("title").GetString() == "격리 예약 우편");
        await Read(ca.PostAsync($"/api/mail/{rewardMail.GetProperty("id").GetInt64()}/claim", null));
        Check(Credits(a.Id) == before + 40, "예약 지급 우편을 받으면 크레딧이 들어옴");
        Check(await LiveOperations.Apply(db, rewardId, true, default) && Credits(a.Id) == before, "회수하면 받은 크레딧도 돌려받음");

        // ---- Coupons ----
        string shared = "QA" + Guid.NewGuid().ToString("N")[..8].ToUpperInvariant();
        var coupon = new { name = "격리 공용 쿠폰", kind = "Shared", code = shared, count = 0, maxUses = 1, startsAtUtc = DateTimeOffset.UtcNow.AddMinutes(-1), endsAtUtc = DateTimeOffset.UtcNow.AddDays(1),
            title = "쿠폰 보상", body = "", reward = new { credits = 5, tickets = 1, items = Array.Empty<string>() }, expiresDays = 30 };
        var (noReward, _) = await Send(admin.PostAsJsonAsync("/api/admin/operations/coupons", coupon with { reward = new { credits = 0, tickets = 0, items = Array.Empty<string>() } }));
        Check(noReward == HttpStatusCode.BadRequest, "보상 없는 쿠폰은 만들 수 없음");
        await Read(admin.PostAsJsonAsync("/api/admin/operations/coupons", coupon));
        var (dup, _) = await Send(admin.PostAsJsonAsync("/api/admin/operations/coupons", coupon));
        Check(dup == HttpStatusCode.Conflict, "같은 공용 코드는 두 번 만들 수 없음");
        var redeemed = await Read(ca.PostAsJsonAsync("/api/coupons/redeem", new { code = " " + shared[..4].ToLowerInvariant() + "-" + shared[4..] + " " }));
        Check(redeemed.GetProperty("message").GetString()!.Contains("우편함") && redeemed.GetProperty("mailbox").GetProperty("mail")[0].GetProperty("title").GetString() == "쿠폰 보상",
            "쿠폰은 띄어쓰기·하이픈·대소문자를 무시하고, 보상은 우편함으로");
        var (again2, msg2) = await Send(ca.PostAsJsonAsync("/api/coupons/redeem", new { code = shared }));
        Check(again2 == HttpStatusCode.BadRequest && msg2.GetProperty("message").GetString()!.Contains("이미 사용"), "같은 계정은 한 번만");
        var (full, msg3) = await Send(cb.PostAsJsonAsync("/api/coupons/redeem", new { code = shared }));
        Check(full == HttpStatusCode.BadRequest && msg3.GetProperty("message").GetString()!.Contains("선착순"), "전체 사용 횟수가 차면 끝");

        var single = await Read(admin.PostAsJsonAsync("/api/admin/operations/coupons", coupon with { name = "격리 1회용", kind = "Single", code = (string?)null, count = 3, maxUses = 0 }));
        var codes = single.GetProperty("codes").EnumerateArray().Select(x => x.GetString()!).ToArray();
        Check(codes.Length == 3 && codes.All(x => x.Length == 14 && x[4] == '-') && codes.Distinct().Count() == 3, "1회용 코드 3개가 서로 다르게 만들어짐");
        await Read(ca.PostAsJsonAsync("/api/coupons/redeem", new { code = codes[0] }));
        var (used, _) = await Send(cb.PostAsJsonAsync("/api/coupons/redeem", new { code = codes[0] }));
        var (sameBatch, _) = await Send(ca.PostAsJsonAsync("/api/coupons/redeem", new { code = codes[1] }));
        Check(used == HttpStatusCode.BadRequest && sameBatch == HttpStatusCode.BadRequest, "1회용 코드는 한 번만, 한 계정은 같은 묶음에서 하나만");
        await Read(cb.PostAsJsonAsync("/api/coupons/redeem", new { code = codes[1] }));
        long campaign = single.GetProperty("id").GetInt64();
        await Read(admin.PostAsync($"/api/admin/operations/coupons/{campaign}/disable", null));
        var p3 = await OperationsIntegration.Player(db); using var c3 = Client(p3.Token);
        var (disabled, _) = await Send(c3.PostAsJsonAsync("/api/coupons/redeem", new { code = codes[2] }));
        Check(disabled == HttpStatusCode.BadRequest, "중지한 쿠폰은 쓸 수 없음");
        var list = await Read(admin.GetAsync($"/api/admin/operations/coupons/{campaign}/codes"));
        Check(list.GetProperty("codes").EnumerateArray().Count(x => x.GetProperty("used").GetBoolean()) == 2, "코드 목록에 사용 여부가 보임");
        for (int i = 0; i < 12; i++) await Send(c3.PostAsJsonAsync("/api/coupons/redeem", new { code = "WRONG" + i }));
        var (limited, msg4) = await Send(c3.PostAsJsonAsync("/api/coupons/redeem", new { code = shared }));
        Check(limited == HttpStatusCode.BadRequest && msg4.GetProperty("message").GetString()!.Contains("1시간"), "잘못된 코드를 많이 넣으면 잠시 막힘");
        Console.WriteLine($"PASS: {count} mail and coupon checks");
    }
}

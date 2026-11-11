using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using MySqlConnector;

internal static class FinalOperationsChecks
{
    internal static async Task RunAsync(AuthDatabase db, HttpClient admin, Action<bool, string> check)
    {
        using var observer = new HttpClient { BaseAddress = admin.BaseAddress };
        observer.DefaultRequestHeaders.Add("X-ProjectProject01-Admin-Key", new string('o', 48));
        using var analytics = await observer.GetAsync("/api/admin/operations/analytics?days=7");
        check(analytics.IsSuccessStatusCode, "조회 전용 운영자가 플레이 분석을 조회할 수 있음");
        using var denied = await observer.PutAsJsonAsync("/api/admin/operations/rollout", new { revision = 0 });
        check(denied.StatusCode == HttpStatusCode.Forbidden, "조회 전용 운영자의 공개 범위 변경은 차단");
        await CheckEventWindow(admin, check);
        await CheckBugInquiry(admin, check);

        string account = "Ops_" + Guid.NewGuid().ToString("N")[..12];
        ulong user;
        await using (var connection = await db.OpenConnectionAsync(default))
        {
            await CommunityEndpoints.Exec(connection, null,
                "INSERT INTO users(account_id,display_name,password_hash) VALUES(@a,@a,'fixture')", default, ("a", account));
            var rows = await CommunityEndpoints.Rows(connection, null, "SELECT id FROM users WHERE account_id=@a", default, ("a", account));
            user = Convert.ToUInt64(rows[0]["id"]);
        }
        try
        {
            using var existing = await admin.GetAsync("/api/admin/operations/rollout");
            using var config = JsonDocument.Parse(await existing.Content.ReadAsStringAsync());
            long revision = config.RootElement.GetProperty("revision").GetInt64();
            using var applied = await admin.PutAsJsonAsync("/api/admin/operations/rollout", new
            {
                revision,
                settings = new { mode = "Testers", percent = 50, testAccounts = new[] { account } }
            });
            check(applied.IsSuccessStatusCode, "계정별 공개 범위 저장");
            await using (var connection = await db.OpenConnectionAsync(default))
                check(await LiveOperations.SurveyAllowed(connection, user, default), "테스트 계정에 설문 공개");
            using var stale = await admin.PutAsJsonAsync("/api/admin/operations/rollout", new
            {
                revision,
                settings = new { mode = "Off", percent = 0, testAccounts = Array.Empty<string>() }
            });
            check(stale.StatusCode == HttpStatusCode.Conflict, "이전 리비전으로 공개 범위를 덮어쓸 수 없음");
            using var restored = await admin.PutAsJsonAsync("/api/admin/operations/rollout", new
            {
                revision = revision + 1,
                settings = config.RootElement.GetProperty("settings").Clone()
            });
            check(restored.IsSuccessStatusCode, "공개 범위 복구");

            var request = new { title = "격리 지급 검사", credits = 100, startsAtUtc = DateTimeOffset.UtcNow, accounts = new[] { account }, requestId = Guid.NewGuid() };
            using var preview = await admin.PostAsJsonAsync("/api/admin/operations/rewards/preview", request);
            using var previewJson = JsonDocument.Parse(await preview.Content.ReadAsStringAsync());
            check(preview.IsSuccessStatusCode && previewJson.RootElement.GetProperty("total").GetInt32() == 100, "예약 지급 대상과 합계 미리보기");
            using var scheduled = await admin.PostAsJsonAsync("/api/admin/operations/rewards", request);
            using var json = JsonDocument.Parse(await scheduled.Content.ReadAsStringAsync());
            long id = json.RootElement.GetProperty("id").GetInt64();
            using var duplicate = await admin.PostAsJsonAsync("/api/admin/operations/rewards", request);
            using var duplicateJson = JsonDocument.Parse(await duplicate.Content.ReadAsStringAsync());
            check(duplicate.IsSuccessStatusCode && duplicateJson.RootElement.GetProperty("id").GetInt64() == id, "예약 재시도에 같은 지급 번호 반환");
            bool[] paid = await Task.WhenAll(LiveOperations.Apply(db, id, false, default), LiveOperations.Apply(db, id, false, default));
            check(paid.Count(value => value) == 1, "동시 예약 처리에서도 지급 한 번");
            using var reverse = await admin.PostAsync($"/api/admin/operations/rewards/{id}/reverse", null);
            using var repeat = await admin.PostAsync($"/api/admin/operations/rewards/{id}/reverse", null);
            check(reverse.IsSuccessStatusCode && repeat.StatusCode == HttpStatusCode.Conflict, "지급 회수 한 번");
            await using var balances = await db.OpenConnectionAsync(default);
            var rows = await CommunityEndpoints.Rows(balances, null, "SELECT credits FROM case_accounts WHERE user_id=@u", default, ("u", user));
            // Rewards arrive as mail (Docs/MailAndCoupons.md): an unclaimed, recalled reward never touched the account.
            check(rows.Count == 0 || Convert.ToInt64(rows[0]["credits"]) == 0, "예약 지급 및 회수 후 잔액 원복");
        }
        finally
        {
            await using var cleanup = await db.OpenConnectionAsync(default);
            await CommunityEndpoints.Exec(cleanup, null, "DELETE FROM users WHERE id=@u", default, ("u", user));
        }
    }

    private static async Task CheckEventWindow(HttpClient admin, Action<bool, string> check)
    {
        string title = "격리 이벤트 " + Guid.NewGuid().ToString("N");
        object Post(DateTimeOffset start, DateTimeOffset end) => new
        {
            kind = "Event", title, body = "이벤트 본문", published = true, setEventWindow = true,
            startsAtUtc = start, endsAtUtc = end
        };
        async Task<bool> Visible()
        {
            using var response = await admin.GetAsync("/api/launcher/posts");
            response.EnsureSuccessStatusCode();
            using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            return json.RootElement.GetProperty("posts").EnumerateArray().Any(post => post.GetProperty("title").GetString() == title);
        }
        DateTimeOffset now = DateTimeOffset.UtcNow;
        using var created = await admin.PostAsJsonAsync("/api/admin/community/posts", Post(now.AddHours(1), now.AddHours(2)));
        check(created.IsSuccessStatusCode && !await Visible(), "공개 시작 전 이벤트 게시물 숨김");
        using var listing = await admin.GetAsync("/api/admin/community");
        using var posts = JsonDocument.Parse(await listing.Content.ReadAsStringAsync());
        string id = posts.RootElement.GetProperty("posts").EnumerateArray()
            .Single(post => post.GetProperty("title").GetString() == title).GetProperty("id").GetString()!;
        try
        {
            using var active = await admin.PutAsJsonAsync("/api/admin/community/posts/" + id, Post(now.AddHours(-1), now.AddHours(1)));
            check(active.IsSuccessStatusCode && await Visible(), "공개 시간에 이벤트 게시물 표시");
            using var ended = await admin.PutAsJsonAsync("/api/admin/community/posts/" + id, Post(now.AddHours(-2), now.AddHours(-1)));
            check(ended.IsSuccessStatusCode && !await Visible(), "공개 종료 후 이벤트 게시물 숨김");
        }
        finally { using var _ = await admin.DeleteAsync("/api/admin/community/posts/" + id); }
    }

    private static async Task CheckBugInquiry(HttpClient admin, Action<bool, string> check)
    {
        string body = "격리 버그 제보 " + Guid.NewGuid().ToString("N");
        string match = Guid.NewGuid().ToString("D");
        using var received = await admin.PostAsJsonAsync("/api/launcher/inquiries", new
        {
            subject = "게임 버그 제보", body, context = new { build = "1.0.0", map = "MultiplayTest", matchId = match, networkErrors = new[] { 2 } }
        });
        received.EnsureSuccessStatusCode();
        using var payload = JsonDocument.Parse(await received.Content.ReadAsStringAsync());
        using var reply = await admin.PostAsJsonAsync("/api/launcher/inquiries/reply", new
        {
            subject = "", body = "", replyCode = payload.RootElement.GetProperty("receipt").GetString()
        });
        using var details = JsonDocument.Parse(await reply.Content.ReadAsStringAsync());
        string saved = details.RootElement.GetProperty("body").GetString()!;
        check(saved.StartsWith(body, StringComparison.Ordinal) && saved.Contains(match), "동의한 버그 제보에 경기 정보만 첨부");
        using var rejected = await admin.PostAsJsonAsync("/api/launcher/inquiries", new
        {
            subject = "게임 버그 제보", body, context = new { build = "Bearer:secrets", map = "MultiplayTest", networkErrors = new[] { 2 } }
        });
        check(rejected.StatusCode == HttpStatusCode.BadRequest, "구조화된 제보 필드의 비허용 형식 거부");
    }
}

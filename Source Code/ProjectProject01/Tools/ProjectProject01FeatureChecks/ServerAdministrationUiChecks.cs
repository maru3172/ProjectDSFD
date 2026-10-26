using ProjectProject01BuildServerHelper;

internal static class ServerAdministrationUiChecks
{
    internal static void Run(string output, Action<bool, string> check)
    {
        Directory.CreateDirectory(output);
        using var form = new ServerAdministrationForm(Path.Combine(output, "offline-fixture"), _ => { }, refreshOnShow: false);
        var tabs = Children(form).OfType<TabControl>().Single(c => c.Name == "ServerAdministrationTabs");
        string[] categories = ["점검·공지", "점검 예약", "계정·신고", "콘텐츠·소통", "진단·운영"];
        var actions = new Dictionary<string, string[]>
        {
            [categories[0]] = ["점검·공지 적용", "안내 공지", "공지 지우기"],
            [categories[1]] = ["점검 예약 추가", "선택 예약 변경", "점검 예약 취소"],
            [categories[2]] = ["계정 이용 제한", "플레이어 신고 확인", "선택 계정 로그아웃", "접속자 전원 로그아웃"],
            [categories[3]] = ["런처 뉴스·패치노트·문의", "피드백/설문조사 관리", "사건 기록 조사 수량", "주간 미션 재화 상한"],
            [categories[4]] = ["크래시 보고서 보기", "크래시 보관 정책", "최대 방 수"]
        };
        check(tabs.TabPages.Cast<TabPage>().Select(p => p.Text).SequenceEqual(categories), "서버 운영 5개 항목 분류");
        var allButtons = Children(form).OfType<Button>().ToArray();
        string[] expected = actions.Values.SelectMany(v => v).Append("상태 새로고침").ToArray();
        check(allButtons.Length == 18 && expected.All(text => allButtons.Count(b => b.Text == text) == 1), "기존 서버 운영 버튼 18개 모두 보존·중복 없음");
        foreach (TabPage page in tabs.TabPages)
            check(Children(page).OfType<Button>().Select(b => b.Text).Order().SequenceEqual(actions[page.Text].Order()), page.Text + "에 기존 작업만 분류");

        var address = Children(form).OfType<TextBox>().Single(c => c.Name == "AdministrationBaseUrl");
        address.Text = "http://127.0.0.1:1";
        var account = Children(form).OfType<TextBox>().Single(c => c.Name == "ForceLogoutAccountId");
        account.Text = "fixture-account";
        var notice = Children(form).OfType<TextBox>().Single(c => c.Name == "MaintenanceScheduleAnnouncement");
        notice.Text = "예약 내용 보존";
        var schedules = Children(form).OfType<ListBox>().Single(c => c.Name == "MaintenanceSchedules");
        schedules.Items.AddRange(["예약 1", "예약 2"]); schedules.SelectedIndex = 1;
        var maintenance = Children(form).OfType<CheckBox>().Single(); maintenance.Checked = true;
        var log = Children(form).OfType<RichTextBox>().Single(c => c.Name == "AdministrationOperationLog");
        log.Text = "[OK] 작업 로그 보존";
        var refresh = allButtons.Single(b => b.Text == "상태 새로고침");

        form.ShowInTaskbar = false; form.Opacity = 0; form.StartPosition = FormStartPosition.Manual;
        form.Location = new Point(-32000, -32000); form.Show();
        foreach (Size size in new[] { new Size(940, 760), new Size(740, 590) })
        {
            form.Size = size;
            foreach (TabPage page in tabs.TabPages)
            {
                tabs.SelectedTab = page; form.PerformLayout(); Application.DoEvents();
                foreach (var button in Children(page).OfType<Button>())
                {
                    Rectangle bounds = page.RectangleToClient(button.RectangleToScreen(button.ClientRectangle));
                    check(button.Visible && page.ClientRectangle.Contains(bounds), $"{size.Width}×{size.Height} {page.Text}: {button.Text} 표시");
                    check(TextRenderer.MeasureText(button.Text, button.Font).Width + 12 <= button.Width, button.Text + " 글씨 잘림 없음");
                }
                var buttons = Children(page).OfType<Button>().ToArray();
                check(!buttons.SelectMany((button, i) => buttons.Skip(i + 1).Select(other => (button, other)))
                    .Any(pair => pair.button.RectangleToScreen(pair.button.ClientRectangle).IntersectsWith(pair.other.RectangleToScreen(pair.other.ClientRectangle))), page.Text + " 버튼 간 겹침 없음");
                check(refresh.Visible && log.Visible && address.Visible && form.ClientRectangle.Contains(form.RectangleToClient(log.RectangleToScreen(log.ClientRectangle))), "주소·새로고침·작업 로그는 분류 전환 중에도 표시");
                using var bitmap = new Bitmap(form.Width, form.Height);
                form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size));
                bitmap.Save(Path.Combine(output, $"server-administration-{tabs.SelectedIndex}-{size.Width}x{size.Height}.png"));
            }
        }
        check(account.Text == "fixture-account" && notice.Text == "예약 내용 보존" && schedules.SelectedIndex == 1 && maintenance.Checked && log.Text == "[OK] 작업 로그 보존", "항목 전환·창 크기 변경 후 입력·예약 선택·점검 체크·로그 유지");
        form.Hide();

        // Pre-category helper inventory (parent of 37df177), including both capacity buttons.
        string[] originalHelperButtons = ["동시 경기 수 적용", "방 한도 설정·적용", "환경 검사", "클라이언트 패키징",
            "클라이언트 업데이트 게시", "데디케이티드 서버 패키징", "MySQL 초기화/시작", "MySQL 중지", "DB 백업·복원",
            "백엔드 시작", "백엔드 중지", "EOS 보이스 자격 증명", "IP 직접 연결 방화벽 설정", "IP 직접 연결 검사",
            "서버 운영", "게임 서버 운영 시작", "경기 종료 후 운영 중지", "종료 대기 취소", "게임 서버 운영 즉시 중지",
            "전체 흐름 스모크 테스트", "로그 폴더", "진단 결과 폴더", "빌드 폴더"];
        using var helper = new MainForm();
        var helperButtons = Children(helper).OfType<Button>().Select(b => b.Text).ToArray();
        check(originalHelperButtons.Length == 23 && originalHelperButtons.All(name => helperButtons.Count(b => b == name) == 1), "서버 도우미 개편 전 버튼 23개 모두 현재 UI에 한 번씩 보존");
        check(helperButtons.Length == 24 && helperButtons.Except(originalHelperButtons).SequenceEqual(new[] { "진단 대시보드 열기" }), "도우미 개편 후 추가는 진단 대시보드 1개이며 기존 버튼 감소 없음");
    }

    private static IEnumerable<Control> Children(Control parent)
    {
        foreach (Control child in parent.Controls)
        {
            yield return child;
            foreach (var descendant in Children(child)) yield return descendant;
        }
    }
}

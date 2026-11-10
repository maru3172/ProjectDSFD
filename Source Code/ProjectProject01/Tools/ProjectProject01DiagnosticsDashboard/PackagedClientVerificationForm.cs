using System.Diagnostics;

namespace ProjectProject01DiagnosticsDashboard;

internal sealed class PackagedClientVerificationForm : Form
{
    private readonly List<Process> clients = [];
    private readonly string directory;
    private readonly CheckedListBox checks = new() { Dock = DockStyle.Fill, CheckOnClick = true };

    public PackagedClientVerificationForm(string executable, string output)
    {
        directory = output;
        Text = "실제 패키지 멀티플레이 검수";
        Size = new Size(600, 390);
        StartPosition = FormStartPosition.CenterParent;
        checks.Items.AddRange(["클라이언트 3개 로그인", "오픈 또는 랜덤 매칭으로 전원 입장",
            "게임 서버 접속 및 전원 로딩 완료", "스폰·역할 확인", "층 이동·엘리베이터·방화셔터",
            "사망·관전·경기 결과·매칭 복귀", "현재 UI 언어의 잘림·겹침 확인"]);
        var top = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true };
        var launch = new Button { Text = "로컬 서버로 테스트 창 3개 실행", AutoSize = true };
        var save = new Button { Text = "검수 기록 저장", AutoSize = true };
        top.Controls.AddRange([launch, save]);
        Controls.Add(checks);
        Controls.Add(top);
        launch.Click += (_, _) =>
        {
            if (clients.Count > 0) return;
            try
            {
                string name = Path.GetFileNameWithoutExtension(executable);
                string binaries = Path.Combine(Path.GetDirectoryName(executable)!, name, "Binaries", "Win64");
                string game = new[] { name + "-Win64-Shipping.exe", name + "-Win64-Test.exe", name + ".exe" }
                    .Select(file => Path.Combine(binaries, file)).FirstOrDefault(File.Exists) ?? executable;
                for (int index = 0; index < 3; index++)
                {
                    string userDirectory = Path.Combine(directory, $"Client-{index + 1}");
                    Directory.CreateDirectory(userDirectory);
                    var start = new ProcessStartInfo(game) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(game)! };
                    foreach (string arg in new[] { "-ProjectProject01Server=127.0.0.1", "-Windowed", "-ResX=960", "-ResY=540",
                        "-WinX=" + (45 + 60 * index), "-WinY=" + (45 + 45 * index),
                        "-UserDir=" + userDirectory,
                        "-abslog=" + Path.Combine(directory, $"Client-{index + 1}.log") })
                        start.ArgumentList.Add(arg);
                    clients.Add(Process.Start(start) ?? throw new InvalidOperationException("클라이언트를 시작하지 못했습니다."));
                }
                launch.Enabled = false;
            }
            catch (Exception ex)
            {
                StopClients();
                MessageBox.Show(this, ex.Message, Text);
            }
        };
        save.Click += (_, _) =>
        {
            File.WriteAllLines(Path.Combine(directory, "Details.txt"),
                checks.Items.Cast<string>().Select((item, index) => $"{(checks.GetItemChecked(index) ? "확인" : "미확인")} · {item}"));
            MessageBox.Show(this, directory, "검수 기록 저장");
        };
        FormClosed += (_, _) => StopClients();
    }

    private void StopClients()
    {
        foreach (var client in clients)
        {
            try { if (!client.HasExited) client.CloseMainWindow(); }
            catch (InvalidOperationException) { }
            finally { client.Dispose(); }
        }
        clients.Clear();
    }
}

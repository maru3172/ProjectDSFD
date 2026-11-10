using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace ProjectProject01BuildServerHelper;

internal sealed class CrashAnalysisForm : Form
{
    private readonly string root;
    private readonly TextBox output = new()
    {
        Dock = DockStyle.Fill, Multiline = true, ScrollBars = ScrollBars.Both, ReadOnly = true, WordWrap = false
    };
    private readonly CancellationTokenSource lifetime = new();

    public CrashAnalysisForm(string projectRoot)
    {
        root = projectRoot;
        Text = "Win64 덤프 분석";
        Size = new Size(1040, 640);
        StartPosition = FormStartPosition.CenterParent;
        var top = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(8) };
        var analyze = new Button { Text = "덤프·심볼 선택 후 분석", AutoSize = true };
        var builds = new Button { Text = "빌드·커밋 기록", AutoSize = true };
        var groups = new Button { Text = "동일 원인 묶음", AutoSize = true };
        top.Controls.AddRange([analyze, builds, groups]);
        Controls.Add(output);
        Controls.Add(top);
        analyze.Click += async (_, _) =>
        {
            analyze.Enabled = builds.Enabled = groups.Enabled = false;
            try { await Analyze(); }
            catch (Exception ex) { if (!IsDisposed && !lifetime.IsCancellationRequested) output.Text = ex.Message; }
            finally { if (!IsDisposed) analyze.Enabled = builds.Enabled = groups.Enabled = true; }
        };
        builds.Click += (_, _) => ShowRecords(false);
        groups.Click += (_, _) => ShowRecords(true);
        FormClosing += (_, _) => lifetime.Cancel();
    }

    private void ShowRecords(bool groups)
    {
        try
        {
            string dir = BuildProvenance.DirectoryFor(root);
            if (groups) dir = Path.Combine(dir, "CrashGroups");
            if (!Directory.Exists(dir)) { output.Text = "아직 기록이 없습니다."; return; }
            if (groups)
            {
                var names = Directory.EnumerateFiles(dir, "*.bucket").Select(File.ReadAllText);
                output.Text = string.Join(Environment.NewLine, names.GroupBy(name => name)
                    .OrderByDescending(group => group.Count()).Select(group => $"{group.Count(),4}건  {group.Key}"));
                return;
            }
            output.Text = string.Join(Environment.NewLine + Environment.NewLine,
                Directory.EnumerateFiles(dir, "*.json").OrderByDescending(File.GetLastWriteTimeUtc).Take(30).Select(File.ReadAllText));
        }
        catch (Exception ex) { output.Text = ex.Message; }
    }

    private async Task Analyze()
    {
        string debugger = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            "Windows Kits", "10", "Debuggers", "x64", "cdb.exe");
        if (!File.Exists(debugger))
        {
            using var executable = new OpenFileDialog { Title = "Windows SDK의 x64 cdb.exe 선택", Filter = "CDB (cdb.exe)|cdb.exe" };
            if (executable.ShowDialog(this) != DialogResult.OK) return;
            debugger = executable.FileName;
        }
        using var dump = new OpenFileDialog { Title = "다운로드한 Win64 덤프 선택", Filter = "크래시 덤프 (*.dmp)|*.dmp" };
        if (dump.ShowDialog(this) != DialogResult.OK) return;
        string build = MatchBuild(dump.FileName);
        using var symbols = new FolderBrowserDialog { Description = "해당 빌드의 PDB 폴더", UseDescriptionForTitle = true };
        if (symbols.ShowDialog(this) != DialogResult.OK) return;
        if (symbols.SelectedPath.Contains(';')) throw new InvalidOperationException("심볼 폴더 경로를 확인하세요.");
        var info = new ProcessStartInfo(debugger)
        {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true
        };
        foreach (string argument in new[] { "-z", dump.FileName, "-y", symbols.SelectedPath, "-c", ".symopt+ 0x400; .reload /f; !analyze -v; lmv; q" })
            info.ArgumentList.Add(argument);
        output.Text = "일치하는 심볼과 호출 스택을 확인하고 있습니다.";
        using var process = Process.Start(info) ?? throw new InvalidOperationException("CDB를 실행하지 못했습니다.");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        timeout.CancelAfter(TimeSpan.FromMinutes(2));
        try { await process.WaitForExitAsync(timeout.Token); }
        catch (OperationCanceledException)
        {
            if (!process.HasExited) process.Kill();
            if (lifetime.IsCancellationRequested) throw;
            throw new TimeoutException("덤프 분석 시간이 초과되었습니다.");
        }
        string report = await stdout + Environment.NewLine + await stderr;
        var bucket = Regex.Match(report, @"(?m)^FAILURE_BUCKET_ID:\s*([^\r\n]+)");
        if (bucket.Success && process.ExitCode == 0)
        {
            await using var data = File.OpenRead(dump.FileName);
            string identity = Convert.ToHexString(await SHA256.HashDataAsync(data, lifetime.Token));
            string groupDirectory = Path.Combine(BuildProvenance.DirectoryFor(root), "CrashGroups");
            Directory.CreateDirectory(groupDirectory);
            await File.WriteAllTextAsync(Path.Combine(groupDirectory, identity + ".bucket"), bucket.Groups[1].Value.Trim(), Encoding.UTF8);
        }
        if (!IsDisposed) output.Text = build + Environment.NewLine + Environment.NewLine + report;
    }

    private string MatchBuild(string dump)
    {
        using var reader = new BinaryReader(File.OpenRead(dump));
        var stream = reader.BaseStream;
        bool Contains(long offset, long length) => offset >= 0 && length >= 0 && offset <= stream.Length - length;
        if (!Contains(0, 32) || reader.ReadUInt32() != 0x504d444d) throw new InvalidDataException("Win64 미니덤프 형식이 아닙니다.");
        reader.ReadUInt32();
        uint entries = reader.ReadUInt32();
        uint directory = reader.ReadUInt32();
        if (entries > 2048 || !Contains(directory, entries * 12L)) throw new InvalidDataException("덤프의 스트림 목록이 손상되었습니다.");
        for (int index = 0; index < entries; index++)
        {
            stream.Position = directory + 12L * index;
            uint type = reader.ReadUInt32();
            uint size = reader.ReadUInt32();
            uint location = reader.ReadUInt32();
            if (type != 4 || size < 4 || !Contains(location, size)) continue; // ModuleListStream
            stream.Position = location;
            uint modules = reader.ReadUInt32();
            if (modules > 4096 || 4L + modules * 108L > size) break;
            var matches = new List<string>();
            for (int module = 0; module < modules; module++)
            {
                // MINIDUMP_MODULE is packed on 4-byte boundaries. CvRecord is at byte 76.
                stream.Position = location + 4L + module * 108L + 76;
                uint codeViewSize = reader.ReadUInt32();
                uint codeViewOffset = reader.ReadUInt32();
                if (codeViewSize < 24 || !Contains(codeViewOffset, codeViewSize)) continue;
                stream.Position = codeViewOffset;
                if (reader.ReadUInt32() != 0x53445352) continue; // RSDS
                Guid id = new(reader.ReadBytes(16));
                uint age = reader.ReadUInt32();
                string record = Path.Combine(BuildProvenance.DirectoryFor(root), $"{id:N}-{age}.json");
                if (File.Exists(record)) matches.Add(File.ReadAllText(record));
            }
            if (matches.Count > 0) return "덤프와 일치하는 로컬 빌드 기록" + Environment.NewLine + string.Join(Environment.NewLine, matches);
        }
        return "덤프와 일치하는 로컬 빌드 기록이 없습니다.";
    }
}

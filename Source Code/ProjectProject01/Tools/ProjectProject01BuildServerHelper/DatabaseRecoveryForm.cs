using System.Security.Cryptography;
using System.IO.Compression;
using System.Text.Json;

namespace ProjectProject01BuildServerHelper;
internal sealed class DatabaseRecoveryForm : Form
{
    private readonly string folder;
    private readonly ListBox files=new(){Dock=DockStyle.Fill};
    private readonly TextBox preview=new(){Dock=DockStyle.Bottom,Height=140,Multiline=true,ReadOnly=true};
    public DatabaseRecoveryForm(string root,Func<bool> backendRunning,Func<string,string?,Task> run)
    {
        folder=Path.Combine(root,"Tools","ProjectProject01Backend","LocalMySql","Backups");
        Text="DB 백업·복원 — 복원은 기존 데이터를 변경합니다";Width=850;Height=550;
        var buttons=new FlowLayoutPanel{Dock=DockStyle.Bottom,Height=48};
        void RefreshFiles(){files.Items.Clear();if(Directory.Exists(folder))foreach(var file in Directory.GetFiles(folder,"*.sql").OrderDescending())files.Items.Add(file);}
        var refresh=new Button{Text="목록 새로고침",AutoSize=true};refresh.Click+=(_,_)=>RefreshFiles();buttons.Controls.Add(refresh);
        var backup=new Button{Text="현재 DB 백업",AutoSize=true};buttons.Controls.Add(backup);
        var restore=new Button{Text="선택 백업 복원",AutoSize=true};buttons.Controls.Add(restore);
        var rehearse = new Button { Text = "격리 복원 시험", AutoSize = true };
        var export = new Button { Text = "선택 백업 외부 저장", AutoSize = true };
        var import = new Button { Text = "외부 백업 가져오기", AutoSize = true };
        buttons.Controls.AddRange([rehearse, export, import]);
        async Task RunWithControlsDisabled(Func<Task> action)
        {
            buttons.Enabled = files.Enabled = false;
            try { await action(); }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, Text); }
            finally { if (!IsDisposed) buttons.Enabled = files.Enabled = true; }
        }
        FormClosing += (_, e) => e.Cancel = !buttons.Enabled;
        rehearse.Click += async (_, _) =>
        {
            if (files.SelectedItem is not string file) return;
            await RunWithControlsDisabled(async () =>
            {
                await run("TestRestore", file);
                preview.Text = "격리 복원 및 테이블 조회를 확인했습니다.";
            });
        };
        export.Click += async (_, _) =>
        {
            if (files.SelectedItem is not string file) return;
            using var destination = new SaveFileDialog
            {
                Title = "접근이 제한된 외부 저장 위치 선택",
                FileName = Path.GetFileNameWithoutExtension(file) + ".zip",
                Filter = "DB 백업 ZIP (*.zip)|*.zip",
                OverwritePrompt = false
            };
            if (destination.ShowDialog(this) != DialogResult.OK) return;
            await RunWithControlsDisabled(async () =>
            {
                await Task.Run(() => ExportBackup(file, destination.FileName));
                preview.Text = "SQL과 검증 매니페스트를 외부 저장했습니다.";
            });
        };
        import.Click += async (_, _) =>
        {
            using var source = new OpenFileDialog { Title = "DB 백업 ZIP 선택", Filter = "DB 백업 ZIP (*.zip)|*.zip" };
            if (source.ShowDialog(this) != DialogResult.OK) return;
            await RunWithControlsDisabled(async () =>
            {
                string file = await Task.Run(() => ImportBackup(source.FileName, folder));
                RefreshFiles();
                files.SelectedItem = file;
            });
        };
        files.SelectedIndexChanged+=(_,_)=>
        {
            try {if(files.SelectedItem is string file) preview.Text=DescribeBackup(file);}
            catch(Exception ex){preview.Text=ex.Message;}
        };
        backup.Click+=async(_,_)=>{backup.Enabled=false;try{await run("Backup",null);RefreshFiles();}catch(Exception ex){MessageBox.Show(ex.Message);}finally{backup.Enabled=true;}};
        restore.Click+=async(_,_)=>
        {
            if(files.SelectedItem is not string file) return;
            try
            {
                string description=DescribeBackup(file);
                if(backendRunning())throw new InvalidOperationException("백엔드와 모든 게임 서버를 먼저 중지하세요. MySQL만 실행된 상태에서 복원합니다.");
                if(MessageBox.Show(description+"\n\n현재 DB를 자동 백업한 뒤 이 파일로 덮어씁니다. 복원할까요?","DB 복원 확인",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes)return;
                restore.Enabled=false;await run("Restore",file);preview.Text="복원 완료. 백엔드를 다시 시작하세요.";RefreshFiles();
            }
            catch(Exception ex){MessageBox.Show(ex.Message,"DB 복원");}
            finally{restore.Enabled=true;}
        };
        Controls.Add(files);Controls.Add(preview);Controls.Add(buttons);RefreshFiles();
    }
    internal static string DescribeBackup(string file)
    {
        using var document=JsonDocument.Parse(File.ReadAllText(file+".manifest.json"));
        using var input = File.OpenRead(file);
        string hash=Convert.ToHexString(SHA256.HashData(input));
        if(!hash.Equals(document.RootElement.GetProperty("sha256").GetString(),StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("백업 파일 검증 실패: 해시가 다릅니다.");
        return $"대상 DB: projectproject01\r\n파일: {Path.GetFileName(file)}\r\n크기: {new FileInfo(file).Length:N0} bytes\r\nSHA-256 검증 완료\r\n{document.RootElement.GetProperty("createdAtUtc").GetString()} (UTC)";
    }

    private static void ExportBackup(string source, string destination)
    {
        string temporary = destination + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using var archiveFile = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None);
            using var sql = File.OpenRead(source);
            using var manifest = File.OpenRead(source + ".manifest.json");
            using (var json = JsonDocument.Parse(manifest))
            {
                var hash = Convert.ToHexString(SHA256.HashData(sql));
                if (json.RootElement.GetProperty("database").GetString() != "projectproject01" ||
                    !hash.Equals(json.RootElement.GetProperty("sha256").GetString(), StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("백업 파일 또는 매니페스트 검증에 실패했습니다.");
            }
            sql.Position = manifest.Position = 0;
            using (var archive = new ZipArchive(archiveFile, ZipArchiveMode.Create, leaveOpen: true))
            {
                using (var entry = archive.CreateEntry(Path.GetFileName(source)).Open()) sql.CopyTo(entry);
                using (var entry = archive.CreateEntry(Path.GetFileName(source) + ".manifest.json").Open()) manifest.CopyTo(entry);
            }
            archiveFile.Dispose();
            File.Move(temporary, destination);
        }
        finally { File.Delete(temporary); }
    }

    private static string ImportBackup(string source, string directory)
    {
        using var archive = ZipFile.OpenRead(source);
        var sql = archive.Entries.SingleOrDefault(entry => entry.Name.EndsWith(".sql", StringComparison.OrdinalIgnoreCase));
        if (archive.Entries.Count != 2 || sql is null || sql.Length is <= 0 or > 2_000_000_000 ||
            archive.GetEntry(sql.FullName + ".manifest.json") is not { Length: > 0 and <= 16384 } manifest)
            throw new InvalidDataException("SQL과 검증 매니페스트를 포함한 백업 ZIP을 선택하세요.");
        Directory.CreateDirectory(directory);
        string target = Path.Combine(directory, "Imported_" + Guid.NewGuid().ToString("N") + ".sql");
        string temporary = target + ".tmp";
        try
        {
            using (var data = sql.Open())
            using (var output = new FileStream(temporary, FileMode.CreateNew))
            {
                byte[] buffer = new byte[65536];
                long total = 0;
                int read;
                while ((read = data.Read(buffer)) > 0)
                {
                    total += read;
                    if (total > sql.Length) throw new InvalidDataException("백업 ZIP의 SQL 크기가 올바르지 않습니다.");
                    output.Write(buffer, 0, read);
                }
                if (total != sql.Length) throw new InvalidDataException("백업 ZIP이 잘렸습니다.");
            }
            using (var input = manifest.Open())
            using (var json = JsonDocument.Parse(input))
            using (var data = File.OpenRead(temporary))
            {
                var value = json.RootElement;
                string hash = Convert.ToHexString(SHA256.HashData(data));
                if (value.GetProperty("database").GetString() != "projectproject01" ||
                    !hash.Equals(value.GetProperty("sha256").GetString(), StringComparison.OrdinalIgnoreCase) ||
                    !value.GetProperty("createdAtUtc").TryGetDateTimeOffset(out _))
                    throw new InvalidDataException("가져온 백업의 매니페스트와 SHA-256을 확인하세요.");
            }
            using (var input = manifest.Open())
            using (var output = new FileStream(target + ".manifest.json", FileMode.CreateNew))
                input.CopyTo(output);
            File.Move(temporary, target);
            return target;
        }
        finally
        {
            File.Delete(temporary);
            if (!File.Exists(target)) File.Delete(target + ".manifest.json");
        }
    }
}

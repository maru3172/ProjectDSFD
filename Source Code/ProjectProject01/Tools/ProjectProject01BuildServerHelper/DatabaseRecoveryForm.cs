using System.Security.Cryptography;
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
        string hash=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file)));
        if(!hash.Equals(document.RootElement.GetProperty("sha256").GetString(),StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("백업 파일 검증 실패: 해시가 다릅니다.");
        return $"대상 DB: projectproject01\r\n파일: {Path.GetFileName(file)}\r\n크기: {new FileInfo(file).Length:N0} bytes\r\nSHA-256 검증 완료\r\n{document.RootElement.GetProperty("createdAtUtc").GetString()} (UTC)";
    }
}

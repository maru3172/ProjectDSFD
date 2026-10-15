using System.IO.Compression;
namespace ProjectProject01BuildServerHelper;

internal sealed class CrashReportDownloadForm : Form
{
    public CrashReportDownloadForm((string Id,string Label)[] reports,Func<string,Task<byte[]>> download)
    {
        Text="크래시 보고서 첨부파일"; Width=800; Height=500; StartPosition=FormStartPosition.CenterParent;
        var list=new ListBox {Dock=DockStyle.Fill,ForeColor=Color.Black,BackColor=Color.White};
        foreach(var report in reports)list.Items.Add($"{report.Label} · {report.Id}");
        if(list.Items.Count>0)list.SelectedIndex=0;
        var button=new Button {Text="선택 보고서 덤프·로그 ZIP 다운로드",Dock=DockStyle.Bottom,Height=40};
        button.Click+=async (_,_)=>
        {
            if(list.SelectedIndex<0)return;
            string id=reports[list.SelectedIndex].Id;
            using var dialog=new SaveFileDialog {FileName=$"Crash-{id}.zip",Filter="ZIP 파일|*.zip"};
            if(dialog.ShowDialog(this)!=DialogResult.OK)return;
            button.Enabled=false;
            try
            {
                await SaveZipAsync(dialog.FileName,await download(id));
                MessageBox.Show(this,$"ZIP을 저장했습니다.\n{dialog.FileName}","크래시 첨부파일");
            }
            catch(Exception ex){MessageBox.Show(ex.Message,"크래시 첨부파일");}
            finally{button.Enabled=true;}
        };
        var description=new Label {Dock=DockStyle.Top,Height=50,Text="서버에는 원본 첨부파일(.dmp·로그 등)을 보관합니다. ZIP은 아래 다운로드 버튼을 누른 뒤 선택한 PC 경로에 저장됩니다.",Padding=new Padding(8)};
        Controls.Add(list);Controls.Add(button);Controls.Add(description);
    }
    internal static async Task SaveZipAsync(string destination,byte[] bytes)
    {
        // Validate before replacing a previously downloaded archive; never leave a partial ZIP.
        using(var stream=new MemoryStream(bytes,false))
        using(var archive=new ZipArchive(stream,ZipArchiveMode.Read))
        {
            if(archive.Entries.Count==0)throw new InvalidDataException("첨부파일 ZIP이 비어 있습니다.");
            foreach(var entry in archive.Entries)
            {using var content=entry.Open();await content.CopyToAsync(Stream.Null);}
        }
        string full=Path.GetFullPath(destination);
        string temporary=full+"."+Guid.NewGuid().ToString("N")+".download";
        try{await File.WriteAllBytesAsync(temporary,bytes);File.Move(temporary,full,true);}
        finally{if(File.Exists(temporary))File.Delete(temporary);}
    }
}

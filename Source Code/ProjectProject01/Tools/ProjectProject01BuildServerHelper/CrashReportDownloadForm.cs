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
            try {await File.WriteAllBytesAsync(dialog.FileName,await download(id));MessageBox.Show("저장했습니다.");}
            catch(Exception ex){MessageBox.Show(ex.Message,"크래시 첨부파일");}
            finally{button.Enabled=true;}
        };
        Controls.Add(list);Controls.Add(button);
    }
}

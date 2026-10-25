namespace ProjectProject01BuildServerHelper;

internal sealed class AnnouncementEditorForm:Form
{
    public AnnouncementEditorForm(string initial,Func<string,Task> publish)
    {
        Text="안내 공지";Width=680;Height=420;MinimumSize=new Size(480,300);StartPosition=FormStartPosition.CenterParent;
        var root=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(16),ColumnCount=1,RowCount=4};
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));root.RowStyles.Add(new RowStyle(SizeType.Percent,100));root.RowStyles.Add(new RowStyle(SizeType.AutoSize));root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var input=new TextBox{Name="AnnouncementContent",Dock=DockStyle.Fill,Multiline=true,AcceptsReturn=true,ScrollBars=ScrollBars.Vertical,MaxLength=512,Text=initial};
        root.Controls.Add(new Label{Text="안내 공지 내용",AutoSize=true},0,0);root.Controls.Add(input,0,1);
        var status=new Label{AutoSize=true,MaximumSize=new Size(600,0)};root.Controls.Add(status,0,2);
        var buttons=new FlowLayoutPanel{Dock=DockStyle.Fill,AutoSize=true,FlowDirection=FlowDirection.RightToLeft};
        var close=new Button{Text="닫기",AutoSize=true};var send=new Button{Text="띄우기",AutoSize=true};buttons.Controls.AddRange([close,send]);root.Controls.Add(buttons,0,3);Controls.Add(root);
        bool busy=false;FormClosing+=(_,e)=>{if(busy)e.Cancel=true;};close.Click+=(_,_)=>Close();
        send.Click+=async(_,_)=>
        {
            if(string.IsNullOrWhiteSpace(input.Text)){status.Text="안내 공지 내용을 입력하세요.";return;}
            busy=true;send.Enabled=close.Enabled=input.Enabled=false;status.Text="전송 중...";
            try{await publish(input.Text);busy=false;DialogResult=DialogResult.OK;Close();}
            catch(Exception ex){status.Text="전송 실패: "+ex.Message;}
            finally{busy=false;if(!IsDisposed)send.Enabled=close.Enabled=input.Enabled=true;}
        };
    }
}

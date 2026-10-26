namespace ProjectProject01BuildServerHelper;

internal sealed class AnnouncementEditorForm:Form
{
    public AnnouncementEditorForm(string initial,Func<string,string,int,DateTimeOffset?,DateTimeOffset?,Task> publish,string initialTag="",int initialRepeats=1,DateTimeOffset? initialStart=null,DateTimeOffset? initialEnd=null,Func<Task>? remove=null)
    {
        Text="안내 공지";Width=720;Height=510;MinimumSize=new Size(520,390);StartPosition=FormStartPosition.CenterParent;
        var root=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(16),ColumnCount=1,RowCount=6};root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        foreach(var type in new[]{SizeType.AutoSize,SizeType.AutoSize,SizeType.AutoSize,SizeType.Percent,SizeType.AutoSize,SizeType.AutoSize})root.RowStyles.Add(new RowStyle(type,type==SizeType.Percent?100:0));
        var options=new FlowLayoutPanel{Dock=DockStyle.Fill,AutoSize=true};var tag=new TextBox{Name="AnnouncementTag",Width=280,MaxLength=64,Text=initialTag,PlaceholderText="-긴급 공지-"};var repeats=new NumericUpDown{Name="AnnouncementRepeats",Minimum=1,Maximum=100,Value=Math.Clamp(initialRepeats,1,100),Width=70};
        options.Controls.AddRange([new Label{Text="태그",AutoSize=true,Padding=new Padding(0,6,0,0)},tag,new Label{Text="횟수",AutoSize=true,Padding=new Padding(12,6,0,0)},repeats]);root.Controls.Add(options,0,0);
        var schedule=new FlowLayoutPanel{Dock=DockStyle.Fill,AutoSize=true};var useWindow=new CheckBox{Name="UseAnnouncementWindow",Text="표시 시간 지정 (한국 시간)",AutoSize=true,Checked=initialStart.HasValue&&initialEnd.HasValue};
        DateTime kstNow=DateTimeOffset.UtcNow.ToOffset(TimeSpan.FromHours(9)).DateTime;
        var start=new DateTimePicker{Name="AnnouncementStart",Width=170,Format=DateTimePickerFormat.Custom,CustomFormat="yyyy-MM-dd HH:mm:ss",ShowUpDown=true,Value=initialStart?.ToOffset(TimeSpan.FromHours(9)).DateTime??kstNow};
        var end=new DateTimePicker{Name="AnnouncementEnd",Width=170,Format=DateTimePickerFormat.Custom,CustomFormat="yyyy-MM-dd HH:mm:ss",ShowUpDown=true,Value=initialEnd?.ToOffset(TimeSpan.FromHours(9)).DateTime??kstNow.AddHours(1)};
        start.Enabled=end.Enabled=useWindow.Checked;useWindow.CheckedChanged+=(_,_)=>start.Enabled=end.Enabled=useWindow.Checked;
        schedule.Controls.AddRange([useWindow,new Label{Text="시작",AutoSize=true},start,new Label{Text="종료",AutoSize=true},end]);root.Controls.Add(schedule,0,1);
        var input=new TextBox{Name="AnnouncementContent",Dock=DockStyle.Fill,Multiline=true,AcceptsReturn=true,ScrollBars=ScrollBars.Vertical,MaxLength=512,Text=initial};root.Controls.Add(new Label{Text="안내 공지 내용",AutoSize=true},0,2);root.Controls.Add(input,0,3);
        var status=new Label{AutoSize=true,MaximumSize=new Size(650,0)};root.Controls.Add(status,0,4);var buttons=new FlowLayoutPanel{Dock=DockStyle.Fill,AutoSize=true,FlowDirection=FlowDirection.RightToLeft};
        var close=new Button{Text="닫기",AutoSize=true};var send=new Button{Text="띄우기",AutoSize=true};var removeButton=new Button{Text="제거",AutoSize=true,Enabled=remove is not null};buttons.Controls.AddRange([close,send,removeButton]);root.Controls.Add(buttons,0,5);Controls.Add(root);
        bool busy=false;FormClosing+=(_,e)=>{if(busy)e.Cancel=true;};close.Click+=(_,_)=>Close();
        async Task Run(Func<Task> action,bool clearing)
        {
            busy=true;send.Enabled=close.Enabled=removeButton.Enabled=input.Enabled=options.Enabled=schedule.Enabled=false;status.Text=clearing?"제거 중...":"전송 중...";
            try{await action();busy=false;DialogResult=DialogResult.OK;Close();}catch(Exception ex){if(!IsDisposed)status.Text="실패: "+ex.Message;}
            finally{busy=false;if(!IsDisposed){send.Enabled=close.Enabled=input.Enabled=options.Enabled=schedule.Enabled=true;removeButton.Enabled=remove is not null;}}
        }
        send.Click+=async(_,_)=>
        {
            if(string.IsNullOrWhiteSpace(input.Text)){status.Text="안내 공지 내용을 입력하세요.";return;}
            DateTimeOffset? from=useWindow.Checked?new DateTimeOffset(DateTime.SpecifyKind(start.Value,DateTimeKind.Unspecified),TimeSpan.FromHours(9)).ToUniversalTime():null;
            DateTimeOffset? until=useWindow.Checked?new DateTimeOffset(DateTime.SpecifyKind(end.Value,DateTimeKind.Unspecified),TimeSpan.FromHours(9)).ToUniversalTime():null;
            if(useWindow.Checked&&(until<=from||until<=DateTimeOffset.UtcNow)){status.Text="시작·종료 시각을 확인하세요.";return;}
            await Run(()=>publish(input.Text,tag.Text,(int)repeats.Value,from,until),false);
        };
        removeButton.Click+=async(_,_)=>{if(remove is not null)await Run(remove,true);};
    }
}

using System.Net;
using System.Text.Json;
namespace ProjectProject01BuildServerHelper;

/// <summary>
/// 우편·쿠폰 (Docs/MailAndCoupons.md). 우편 보내기: everyone or accounts, title, text, rewards, expiry; preview then send;
/// sent batches with claims and recall. 쿠폰: shared code or one-time codes, period, uses, rewards; list, stop, export codes.
/// </summary>
internal sealed class MailCouponAdministrationForm:Form
{
    readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(15)};
    readonly Func<HttpMethod,string,object?,HttpRequestMessage> create;
    readonly Label status=new(){AutoSize=true,Padding=new Padding(8,7,0,0)};
    List<(string Id,string Name)> items=new();

    public MailCouponAdministrationForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        this.create=create;Text="우편 · 쿠폰";Width=1060;Height=760;MinimumSize=new Size(900,640);StartPosition=FormStartPosition.CenterParent;
        var tabs=new TabControl{Dock=DockStyle.Fill};var mailTab=new TabPage("우편 보내기");var couponTab=new TabPage("쿠폰");tabs.TabPages.AddRange([mailTab,couponTab]);
        var bottom=new FlowLayoutPanel{Dock=DockStyle.Bottom,Height=36};var close=new Button{Text="닫기",AutoSize=true};close.Click+=(_,_)=>Close();bottom.Controls.AddRange([close,status]);
        Controls.Add(tabs);Controls.Add(bottom);
        BuildMail(mailTab);BuildCoupons(couponTab);
        FormClosed+=(_,_)=>http.Dispose();
    }

    async Task<(HttpStatusCode Code,JsonElement Body)> Send(HttpMethod method,string path,object? body)
    {
        using var request=create(method,path,body);using var response=await http.SendAsync(request);var text=await response.Content.ReadAsStringAsync();
        return(response.StatusCode,text.Length==0?default:JsonDocument.Parse(text).RootElement.Clone());
    }
    static string Message(JsonElement body,string fallback)=>body.ValueKind==JsonValueKind.Object&&body.TryGetProperty("message",out var m)?m.GetString()??fallback:fallback;

    // ---- Reward editor shared by both tabs ----
    sealed class RewardBox
    {
        public readonly NumericUpDown Credits=new(){Maximum=10000,Width=90},Tickets=new(){Maximum=100,Width=70},Days=new(){Maximum=365,Value=30,Width=70};
        public readonly CheckedListBox Items=new(){Width=260,Height=110,CheckOnClick=true};
        public object Reward()=>new{credits=(int)Credits.Value,tickets=(int)Tickets.Value,items=Items.CheckedItems.Cast<Choice>().Select(c=>c.Id).ToArray()};
        public void Fill(IEnumerable<(string Id,string Name)> list){Items.Items.Clear();foreach(var (id,name) in list)Items.Items.Add(new Choice(id,name));}
        public Control Panel()
        {
            var p=new TableLayoutPanel{AutoSize=true,ColumnCount=2};
            p.Controls.Add(new Label{Text="크레딧",AutoSize=true,Padding=new Padding(0,6,0,0)});p.Controls.Add(Credits);
            p.Controls.Add(new Label{Text="뽑기권",AutoSize=true,Padding=new Padding(0,6,0,0)});p.Controls.Add(Tickets);
            p.Controls.Add(new Label{Text="꾸미기 아이템",AutoSize=true,Padding=new Padding(0,6,0,0)});p.Controls.Add(Items);
            p.Controls.Add(new Label{Text="받을 수 있는 기한(일, 0=없음)",AutoSize=true,Padding=new Padding(0,6,0,0)});p.Controls.Add(Days);
            return p;
        }
    }
    sealed record Choice(string Id,string Name){public override string ToString()=>Name;}
    static TextBox Box(int width,bool multiline=false,int height=0)=>new(){Width=width,Multiline=multiline,Height=multiline?height:23,ScrollBars=multiline?ScrollBars.Vertical:ScrollBars.None};
    static Label Caption(string text)=>new(){Text=text,AutoSize=true,Padding=new Padding(0,8,0,2)};

    void BuildMail(TabPage page)
    {
        var split=new SplitContainer{Dock=DockStyle.Fill,SplitterDistance=420};page.Controls.Add(split);
        var form=new FlowLayoutPanel{Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown,WrapContents=false,AutoScroll=true,Padding=new Padding(8)};split.Panel1.Controls.Add(form);
        var everyone=new RadioButton{Text="전체 플레이어",AutoSize=true,Checked=true};var some=new RadioButton{Text="계정 목록 (한 줄에 하나)",AutoSize=true};
        var accounts=Box(380,true,80);accounts.Enabled=false;some.CheckedChanged+=(_,_)=>accounts.Enabled=some.Checked;
        var title=Box(380);title.MaxLength=80;var body=Box(380,true,70);body.MaxLength=1000;var reward=new RewardBox();
        var preview=new Button{Text="미리 보기",AutoSize=true};var send=new Button{Text="보내기",AutoSize=true};
        form.Controls.AddRange([Caption("받는 사람"),everyone,some,accounts,Caption("제목 (80자)"),title,Caption("내용 (1000자)"),body,Caption("보상"),reward.Panel(),new FlowLayoutPanel{AutoSize=true,Controls={preview,send}}]);
        var list=new ListView{Dock=DockStyle.Fill,View=View.Details,FullRowSelect=true};
        foreach(var (h,w) in new[]{("번호",50),("제목",200),("보상",160),("보낸 곳",90),("받음/받는 사람",110),("기한",110),("상태",60)})list.Columns.Add(h,w);
        var recall=new Button{Text="선택한 우편 회수 (받지 않은 것만)",AutoSize=true,Dock=DockStyle.Bottom};var refresh=new Button{Text="새로 고침",AutoSize=true,Dock=DockStyle.Bottom};
        split.Panel2.Controls.Add(list);split.Panel2.Controls.Add(recall);split.Panel2.Controls.Add(refresh);
        Guid requestId=Guid.NewGuid();
        object Request()=>new{title=title.Text,body=body.Text,reward=reward.Reward(),everyone=everyone.Checked,accounts=accounts.Lines.Select(l=>l.Trim()).Where(l=>l.Length>0).ToArray(),expiresDays=(int)reward.Days.Value,requestId};
        async Task Load()
        {
            try
            {
                var (code,b)=await Send(HttpMethod.Get,"/api/admin/operations/mail",null);if(code!=HttpStatusCode.OK){status.Text=Message(b,$"불러오지 못했습니다 ({(int)code}).");return;}
                if(items.Count==0){items=b.GetProperty("items").EnumerateArray().Select(i=>(i.GetProperty("id").GetString()!,i.GetProperty("name").GetString()!)).ToList();reward.Fill(items);couponReward?.Fill(items);}
                list.Items.Clear();
                foreach(var m in b.GetProperty("batches").EnumerateArray())
                {
                    string rw=string.Join(" · ",new[]{m.GetProperty("credits").GetInt32()>0?$"크레딧 {m.GetProperty("credits").GetInt32()}":null,m.GetProperty("tickets").GetInt32()>0?$"뽑기권 {m.GetProperty("tickets").GetInt32()}":null,
                        m.GetProperty("items").GetString() is {Length:>0} it?$"아이템 {it.Split(',').Length}":null}.Where(x=>x is not null));
                    var row=new ListViewItem([m.GetProperty("id").GetInt64().ToString(),m.GetProperty("title").GetString()??"",rw,m.GetProperty("source").GetString()??"",
                        $"{m.GetProperty("claimed").GetInt32()} / {m.GetProperty("recipients").GetInt32()}",m.GetProperty("expiresUtc").ValueKind==JsonValueKind.Null?"없음":m.GetProperty("expiresUtc").GetDateTime().ToLocalTime().ToString("MM-dd HH:mm"),
                        m.GetProperty("recalled").GetBoolean()?"회수":""]){Tag=m.GetProperty("id").GetInt64()};
                    list.Items.Add(row);
                }
            }
            catch(Exception ex){status.Text="불러오지 못했습니다: "+ex.Message;}
        }
        preview.Click+=async(_,_)=>{try{var (code,b)=await Send(HttpMethod.Post,"/api/admin/operations/mail/preview",Request());status.Text=code==HttpStatusCode.OK?$"받는 사람 {b.GetProperty("recipients").GetInt32()}명":Message(b,$"확인하지 못했습니다 ({(int)code}).");}catch(Exception ex){status.Text=ex.Message;}};
        send.Click+=async(_,_)=>
        {
            if(MessageBox.Show(this,everyone.Checked?"전체 플레이어에게 보낼까요?":"입력한 계정에 보낼까요?",Text,MessageBoxButtons.YesNo)!=DialogResult.Yes)return;
            try
            {
                var (code,b)=await Send(HttpMethod.Post,"/api/admin/operations/mail",Request());
                if(code==HttpStatusCode.OK){status.Text=$"{b.GetProperty("recipients").GetInt32()}명에게 보냈습니다.";requestId=Guid.NewGuid();await Load();}else status.Text=Message(b,$"보내지 못했습니다 ({(int)code}).");
            }
            catch(Exception ex){status.Text=ex.Message;}
        };
        recall.Click+=async(_,_)=>
        {
            if(list.SelectedItems.Count!=1||MessageBox.Show(this,"받지 않은 우편을 모두 지울까요? 이미 받은 보상은 그대로입니다.",Text,MessageBoxButtons.YesNo)!=DialogResult.Yes)return;
            var (code,b)=await Send(HttpMethod.Post,$"/api/admin/operations/mail/{list.SelectedItems[0].Tag}/recall",null);
            status.Text=code==HttpStatusCode.OK?$"받지 않은 우편 {b.GetProperty("removed").GetInt32()}통을 지웠습니다.":Message(b,"회수하지 못했습니다.");await Load();
        };
        refresh.Click+=async(_,_)=>await Load();Shown+=async(_,_)=>await Load();
    }

    RewardBox? couponReward;
    void BuildCoupons(TabPage page)
    {
        var split=new SplitContainer{Dock=DockStyle.Fill,SplitterDistance=420};page.Controls.Add(split);
        var form=new FlowLayoutPanel{Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown,WrapContents=false,AutoScroll=true,Padding=new Padding(8)};split.Panel1.Controls.Add(form);
        var name=Box(380);name.MaxLength=80;var shared=new RadioButton{Text="공용 코드 (계정당 1번)",AutoSize=true,Checked=true};var single=new RadioButton{Text="1회용 코드 묶음",AutoSize=true};
        var code=Box(200);code.MaxLength=20;var uses=new NumericUpDown{Maximum=1000000,Width=110};var count=new NumericUpDown{Minimum=1,Maximum=5000,Value=100,Width=90,Enabled=false};
        single.CheckedChanged+=(_,_)=>{code.Enabled=uses.Enabled=shared.Checked;count.Enabled=single.Checked;};
        var starts=new DateTimePicker{Format=DateTimePickerFormat.Custom,CustomFormat="yyyy-MM-dd HH:mm",Width=170};var ends=new DateTimePicker{Format=DateTimePickerFormat.Custom,CustomFormat="yyyy-MM-dd HH:mm",Width=170,Value=DateTime.Now.AddDays(14)};
        var title=Box(380);title.MaxLength=80;title.Text="쿠폰 보상";var body=Box(380,true,50);couponReward=new RewardBox();if(items.Count>0)couponReward.Fill(items);
        var make=new Button{Text="쿠폰 만들기",AutoSize=true};
        form.Controls.AddRange([Caption("이름 (운영용)"),name,shared,new FlowLayoutPanel{AutoSize=true,Controls={new Label{Text="코드",AutoSize=true,Padding=new Padding(0,6,0,0)},code,new Label{Text="선착순(0=제한 없음)",AutoSize=true,Padding=new Padding(0,6,0,0)},uses}},
            single,new FlowLayoutPanel{AutoSize=true,Controls={new Label{Text="개수",AutoSize=true,Padding=new Padding(0,6,0,0)},count}},
            Caption("사용 기간"),new FlowLayoutPanel{AutoSize=true,Controls={starts,new Label{Text="~",AutoSize=true,Padding=new Padding(0,6,0,0)},ends}},
            Caption("우편 제목"),title,Caption("우편 내용"),body,Caption("보상"),couponReward.Panel(),make]);
        var list=new ListView{Dock=DockStyle.Fill,View=View.Details,FullRowSelect=true};
        foreach(var (h,w) in new[]{("번호",50),("이름",150),("종류",60),("코드",110),("사용",80),("기간",170),("상태",60)})list.Columns.Add(h,w);
        var stop=new Button{Text="선택한 쿠폰 중지",AutoSize=true,Dock=DockStyle.Bottom};var export=new Button{Text="1회용 코드 내보내기 (CSV)",AutoSize=true,Dock=DockStyle.Bottom};var refresh=new Button{Text="새로 고침",AutoSize=true,Dock=DockStyle.Bottom};
        split.Panel2.Controls.Add(list);split.Panel2.Controls.Add(stop);split.Panel2.Controls.Add(export);split.Panel2.Controls.Add(refresh);
        async Task Load()
        {
            try
            {
                var (c,b)=await Send(HttpMethod.Get,"/api/admin/operations/coupons",null);if(c!=HttpStatusCode.OK){status.Text=Message(b,$"불러오지 못했습니다 ({(int)c}).");return;}
                list.Items.Clear();
                foreach(var m in b.GetProperty("campaigns").EnumerateArray())
                {
                    bool isShared=m.GetProperty("kind").GetString()=="Shared";int max=m.GetProperty("maxUses").GetInt32();
                    list.Items.Add(new ListViewItem([m.GetProperty("id").GetInt64().ToString(),m.GetProperty("name").GetString()??"",isShared?"공용":"1회용",isShared?m.GetProperty("sharedCode").GetString()??"":$"{m.GetProperty("codes").GetInt32()}개",
                        isShared&&max>0?$"{m.GetProperty("used").GetInt32()} / {max}":m.GetProperty("used").GetInt32().ToString(),
                        $"{m.GetProperty("startsUtc").GetDateTime().ToLocalTime():MM-dd HH:mm} ~ {m.GetProperty("endsUtc").GetDateTime().ToLocalTime():MM-dd HH:mm}",m.GetProperty("disabled").GetBoolean()?"중지":""])
                        {Tag=(m.GetProperty("id").GetInt64(),isShared)});
                }
            }
            catch(Exception ex){status.Text="불러오지 못했습니다: "+ex.Message;}
        }
        make.Click+=async(_,_)=>
        {
            try
            {
                var (c,b)=await Send(HttpMethod.Post,"/api/admin/operations/coupons",new{name=name.Text,kind=shared.Checked?"Shared":"Single",code=code.Text,count=(int)count.Value,maxUses=(int)uses.Value,
                    startsAtUtc=new DateTimeOffset(starts.Value),endsAtUtc=new DateTimeOffset(ends.Value),title=title.Text,body=body.Text,reward=couponReward!.Reward(),expiresDays=(int)couponReward.Days.Value});
                if(c!=HttpStatusCode.OK){status.Text=Message(b,$"만들지 못했습니다 ({(int)c}).");return;}
                status.Text=shared.Checked?$"공용 코드 {b.GetProperty("sharedCode").GetString()}를 만들었습니다.":$"1회용 코드 {b.GetProperty("codes").GetArrayLength()}개를 만들었습니다. 목록에서 내보내세요.";await Load();
            }
            catch(Exception ex){status.Text=ex.Message;}
        };
        stop.Click+=async(_,_)=>
        {
            if(list.SelectedItems.Count!=1||MessageBox.Show(this,"이 쿠폰을 중지할까요? 다시 켤 수 없습니다.",Text,MessageBoxButtons.YesNo)!=DialogResult.Yes)return;
            var (id,_)=((long,bool))list.SelectedItems[0].Tag!;var (c,b)=await Send(HttpMethod.Post,$"/api/admin/operations/coupons/{id}/disable",null);status.Text=Message(b,c==HttpStatusCode.OK?"중지했습니다.":"중지하지 못했습니다.");await Load();
        };
        export.Click+=async(_,_)=>
        {
            if(list.SelectedItems.Count!=1)return;var (id,isShared)=((long,bool))list.SelectedItems[0].Tag!;if(isShared){status.Text="1회용 쿠폰을 고르세요.";return;}
            var (c,b)=await Send(HttpMethod.Get,$"/api/admin/operations/coupons/{id}/codes",null);if(c!=HttpStatusCode.OK){status.Text=Message(b,"불러오지 못했습니다.");return;}
            using var save=new SaveFileDialog{Filter="CSV (*.csv)|*.csv",FileName=$"coupon_{id}.csv"};if(save.ShowDialog(this)!=DialogResult.OK)return;
            var lines=new[]{"코드,사용"}.Concat(b.GetProperty("codes").EnumerateArray().Select(x=>$"{x.GetProperty("code").GetString()},{(x.GetProperty("used").GetBoolean()?"사용함":"")}"));
            await File.WriteAllLinesAsync(save.FileName,lines,new System.Text.UTF8Encoding(true));status.Text="내보냈습니다.";
        };
        refresh.Click+=async(_,_)=>await Load();Shown+=async(_,_)=>await Load();
    }
}

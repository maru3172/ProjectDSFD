using System.Net;
using System.Text.Json;
namespace ProjectProject01BuildServerHelper;

/// <summary>
/// 금칙어 관리 (Docs/ChatFilter.md): banned words and allowed words (an allowed word protects a banned word inside it,
/// e.g. 시발점), one per line. A sentence can be tried against the lists as edited before saving.
/// </summary>
internal sealed class ChatFilterAdministrationForm:Form
{
    readonly HttpClient http=new(){Timeout=TimeSpan.FromSeconds(8)};
    public ChatFilterAdministrationForm(Func<HttpMethod,string,object?,HttpRequestMessage> create)
    {
        Text="금칙어 관리";Width=900;Height=680;MinimumSize=new Size(760,560);StartPosition=FormStartPosition.CenterParent;long revision=-1;
        var root=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=5,Padding=new Padding(12)};
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,60));root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,40));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));root.RowStyles.Add(new RowStyle(SizeType.AutoSize));root.RowStyles.Add(new RowStyle(SizeType.Percent,100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));root.RowStyles.Add(new RowStyle(SizeType.AutoSize));Controls.Add(root);
        var intro=new Label{AutoSize=true,MaximumSize=new Size(840,0),Padding=new Padding(0,0,0,8),Text=
            "방 채팅·귓속말은 금칙어를 *로 가려서 보내고, 표시 이름·방 이름·상태메시지·소개글·공유 게시판 글은 금칙어가 있으면 저장하지 않습니다. "+
            "띄어쓰기·기호를 사이에 넣어도 찾고, 허용 단어 안에 든 금칙어(예: 시발점)는 막지 않습니다. 이미 남겨진 글에는 적용되지 않습니다."};
        root.Controls.Add(intro,0,0);root.SetColumnSpan(intro,2);
        root.Controls.Add(new Label{Text="금칙어 (한 줄에 하나)",AutoSize=true},0,1);root.Controls.Add(new Label{Text="허용 단어 (한 줄에 하나)",AutoSize=true},1,1);
        TextBox List()=>new(){Dock=DockStyle.Fill,Multiline=true,ScrollBars=ScrollBars.Vertical,AcceptsReturn=true,Font=new Font("Malgun Gothic",10f)};
        var words=List();var allowed=List();root.Controls.Add(words,0,2);root.Controls.Add(allowed,1,2);
        var test=new TextBox{Dock=DockStyle.Fill,PlaceholderText="시험할 문장을 입력하고 '시험'을 누르세요",MaxLength=500};
        var tryIt=new Button{Text="시험",AutoSize=true};var result=new Label{AutoSize=true,MaximumSize=new Size(840,0),Padding=new Padding(0,6,0,0)};
        var testRow=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,AutoSize=true};testRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));testRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        testRow.Controls.Add(test,0,0);testRow.Controls.Add(tryIt,1,0);testRow.Controls.Add(result,0,1);testRow.SetColumnSpan(result,2);
        root.Controls.Add(testRow,0,3);root.SetColumnSpan(testRow,2);
        var actions=new FlowLayoutPanel{Dock=DockStyle.Fill,AutoSize=true};var save=new Button{Text="저장",AutoSize=true,Enabled=false};var reload=new Button{Text="다시 불러오기",AutoSize=true};
        var close=new Button{Text="닫기",AutoSize=true};var status=new Label{AutoSize=true,Padding=new Padding(8,7,0,0)};actions.Controls.AddRange([save,reload,close,status]);
        root.Controls.Add(actions,0,4);root.SetColumnSpan(actions,2);
        string[] Lines(TextBox box)=>box.Lines.Select(l=>l.Trim()).Where(l=>l.Length>0).ToArray();
        async Task<(HttpStatusCode Code,JsonElement Body)> Send(HttpMethod method,string path,object? body)
        {
            using var request=create(method,path,body);using var response=await http.SendAsync(request);var text=await response.Content.ReadAsStringAsync();
            return(response.StatusCode,text.Length==0?default:JsonDocument.Parse(text).RootElement.Clone());
        }
        string Message(JsonElement body,string fallback)=>body.ValueKind==JsonValueKind.Object&&body.TryGetProperty("message",out var m)?m.GetString()??fallback:fallback;
        async Task Load()
        {
            save.Enabled=false;status.Text="불러오는 중…";
            try
            {
                var (code,body)=await Send(HttpMethod.Get,"/api/admin/community/chat-filter",null);
                if(code!=HttpStatusCode.OK){status.Text=$"불러오지 못했습니다 ({(int)code}).";return;}
                revision=body.GetProperty("revision").GetInt64();
                words.Lines=[..body.GetProperty("words").EnumerateArray().Select(w=>w.GetString()??"")];allowed.Lines=[..body.GetProperty("allowed").EnumerateArray().Select(w=>w.GetString()??"")];
                save.Enabled=true;status.Text=$"금칙어 {words.Lines.Length}개 · 허용 단어 {allowed.Lines.Length}개";
            }
            catch(Exception ex){status.Text="불러오지 못했습니다: "+ex.Message;}
        }
        save.Click+=async(_,_)=>
        {
            save.Enabled=false;
            try
            {
                var (code,body)=await Send(HttpMethod.Put,"/api/admin/community/chat-filter",new{revision,words=Lines(words),allowed=Lines(allowed)});
                if(code==HttpStatusCode.OK){revision=body.GetProperty("revision").GetInt64();status.Text=Message(body,"저장했습니다.");}
                else status.Text=Message(body,$"저장하지 못했습니다 ({(int)code}).");
            }
            catch(Exception ex){status.Text="저장하지 못했습니다: "+ex.Message;}
            finally{save.Enabled=revision>=0;}
        };
        tryIt.Click+=async(_,_)=>
        {
            try
            {
                var (code,body)=await Send(HttpMethod.Post,"/api/admin/community/chat-filter/test",new{text=test.Text,words=Lines(words),allowed=Lines(allowed)});
                result.Text=code!=HttpStatusCode.OK?Message(body,$"시험하지 못했습니다 ({(int)code})."):
                    $"채팅으로 보내면: {body.GetProperty("masked").GetString()}\r\n이름·글로 쓰면: {(body.GetProperty("blocked").GetBoolean()?"저장 거부":"저장됨")}";
            }
            catch(Exception ex){result.Text="시험하지 못했습니다: "+ex.Message;}
        };
        test.KeyDown+=(_,e)=>{if(e.KeyCode==Keys.Enter){e.SuppressKeyPress=true;tryIt.PerformClick();}};
        reload.Click+=async(_,_)=>await Load();close.Click+=(_,_)=>Close();Shown+=async(_,_)=>await Load();FormClosed+=(_,_)=>http.Dispose();
    }
}

using System.Net.Http.Json;
using System.Text.Json;
namespace ProjectProject01BuildServerHelper;

internal sealed class RoomCapacityForm : Form
{
    public RoomCapacityForm(Func<HttpMethod,string,object?,HttpRequestMessage> request,Action<int> applied)
    {
        Text="최대 방 수";Width=570;Height=260;StartPosition=FormStartPosition.CenterParent;
        var root=new FlowLayoutPanel {Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown,Padding=new Padding(16),WrapContents=false};
        var explanation=new Label {AutoSize=true,MaximumSize=new Size(510,0),Text="대기·시작 준비·경기 중인 방을 모두 포함합니다. 한도를 낮춰도 기존 방과 경기는 삭제하지 않습니다. 동시 경기 한도는 메인 창에서 숫자를 변경한 뒤 '동시 경기 수 적용'을 누르세요."};
        var maximum=new NumericUpDown {Minimum=1,Maximum=100,Value=30,Width=160,Enabled=false};
        var status=new Label {AutoSize=true,MaximumSize=new Size(510,0),Text="조회 중..."};
        var save=new Button {Text="서버에 적용",AutoSize=true,Enabled=false};
        root.Controls.Add(explanation);root.Controls.Add(maximum);root.Controls.Add(save);root.Controls.Add(status);Controls.Add(root);
        Shown+=async(_,_)=>
        {
            try
            {
                using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(8)};
                using var get=request(HttpMethod.Get,"/api/admin/room-capacity",null);
                using var response=await http.SendAsync(get);response.EnsureSuccessStatusCode();
                using var json=JsonDocument.Parse(await response.Content.ReadAsStringAsync());
                maximum.Value=json.RootElement.GetProperty("maximumRooms").GetInt32();
                status.Text=$"현재 방 {json.RootElement.GetProperty("currentRooms").GetInt32()}개 / 설정 가능 1~100개";
                maximum.Enabled=true;save.Enabled=true;
            }
            catch(Exception ex){status.Text="조회 실패: "+ex.Message;}
        };
        save.Click+=async(_,_)=>
        {
            save.Enabled=false;
            try
            {
                using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(8)};
                using var put=request(HttpMethod.Put,"/api/admin/room-capacity",new {maximumRooms=(int)maximum.Value});
                using var response=await http.SendAsync(put);response.EnsureSuccessStatusCode();
                applied((int)maximum.Value);status.Text="저장했습니다. 백엔드 재시작 후에도 유지됩니다.";
            }
            catch(Exception ex){status.Text="저장 실패: "+ex.Message;}
            finally{if(!IsDisposed)save.Enabled=true;}
        };
    }
}

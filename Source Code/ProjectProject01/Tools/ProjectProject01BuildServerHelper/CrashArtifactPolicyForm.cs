using System.Net.Http.Json;

namespace ProjectProject01BuildServerHelper;

internal sealed class CrashArtifactPolicyForm : Form
{
    private sealed record Policy(bool AutoCleanupEnabled,int RetentionDays,int MaximumTotalMiB,bool AllowMinidumps);
    private sealed record Status(Policy Policy,long StoredBytes,int Files,string Warning);
    public CrashArtifactPolicyForm(Func<HttpMethod,string,object?,HttpRequestMessage> request)
    {
        Text="크래시 첨부 보관 정책";Width=670;Height=410;StartPosition=FormStartPosition.CenterParent;
        var root=new FlowLayoutPanel {Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown,WrapContents=false,Padding=new Padding(16),AutoScroll=true};
        var cleanup=new CheckBox {AutoSize=true,Text="자동 정리 사용 (10분 간격, 오래된 첨부 영구 삭제)",Enabled=false};
        var dumps=new CheckBox {AutoSize=true,Text="원본 덤프 수집 허용 (메모리의 토큰·비밀번호 등이 포함될 수 있음)",Enabled=false};
        var days=new NumericUpDown {Minimum=1,Maximum=3650,Value=30,Enabled=false};
        var capacity=new NumericUpDown {Minimum=1,Maximum=65536,Value=2048,Enabled=false};
        var state=new Label {AutoSize=true,MaximumSize=new Size(610,0),Text="조회 중..."};
        var save=new Button {Text="정책 적용",AutoSize=true,Enabled=false};
        root.Controls.Add(new Label {AutoSize=true,MaximumSize=new Size(610,0),Text="기본값: 자동 삭제·덤프 수집 꺼짐. 자동 정리를 켜면 보관 기간을 넘긴 첨부를 지우고, 용량 초과 시 오래된 것부터 지웁니다. DB 보고서와 클라이언트 원본은 유지합니다. 정리가 꺼져도 용량 한도를 넘는 업로드는 거부합니다."});
        root.Controls.Add(cleanup);root.Controls.Add(dumps);
        root.Controls.Add(new Label {AutoSize=true,Text="보관 기간 (일)"});root.Controls.Add(days);
        root.Controls.Add(new Label {AutoSize=true,Text="최대 첨부 용량 (MiB)"});root.Controls.Add(capacity);
        root.Controls.Add(save);root.Controls.Add(state);Controls.Add(root);
        Shown+=async(_,_)=>
        {
            try
            {
                using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(8)};
                using var get=request(HttpMethod.Get,"/api/admin/crash-artifact-policy",null);
                using var response=await http.SendAsync(get);response.EnsureSuccessStatusCode();
                var status=await response.Content.ReadFromJsonAsync<Status>() ?? throw new IOException("정책 응답 없음");
                if(IsDisposed)return;
                cleanup.Checked=status.Policy.AutoCleanupEnabled;dumps.Checked=status.Policy.AllowMinidumps;
                days.Value=status.Policy.RetentionDays;capacity.Value=status.Policy.MaximumTotalMiB;
                state.Text=$"저장된 첨부 {status.Files}개 · {status.StoredBytes/1048576.0:F1} MiB";
                cleanup.Enabled=dumps.Enabled=days.Enabled=capacity.Enabled=save.Enabled=true;
            }
            catch(Exception ex){if(!IsDisposed)state.Text="조회 실패: "+ex.Message;}
        };
        save.Click+=async(_,_)=>
        {
            if((cleanup.Checked || dumps.Checked) && MessageBox.Show(this,"자동 정리는 첨부를 영구 삭제하며, 원본 덤프에는 민감정보가 포함될 수 있습니다. 이 정책을 적용할까요?","크래시 자료 정책 확인",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes)return;
            save.Enabled=false;
            try
            {
                using var http=new HttpClient {Timeout=TimeSpan.FromSeconds(8)};
                using var put=request(HttpMethod.Put,"/api/admin/crash-artifact-policy",new Policy(cleanup.Checked,(int)days.Value,(int)capacity.Value,dumps.Checked));
                using var response=await http.SendAsync(put);response.EnsureSuccessStatusCode();
                if(!IsDisposed)state.Text="정책 저장 완료. 자동 정리는 다음 10분 주기에 실행됩니다. 기존 덤프는 수집 해제로 삭제되지 않습니다.";
            }
            catch(Exception ex){if(!IsDisposed)state.Text="적용 실패: "+ex.Message;}
            finally {if(!IsDisposed)save.Enabled=true;}
        };
    }
}

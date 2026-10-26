using System.Diagnostics;
using System.Globalization;
using System.Text.Json;

internal static class AdminScriptChecks
{
    internal static async Task RunAsync(string projectRoot,HttpClient admin,AuthDatabase database,Action<bool,string> check)
    {
        if(admin.BaseAddress is null||!admin.BaseAddress.IsLoopback||admin.BaseAddress.Port==5080)throw new InvalidOperationException("Admin script checks require an isolated HTTP fixture.");
        // IntegrationChecks verifies the MySQL port AND data directory before here.
        await OperationsIntegration.Execute(database,"DELETE FROM maintenance_schedules; UPDATE service_control SET maintenance_enabled=FALSE,announcement='';");
        async Task<JsonElement> Status()
        {
            using var response=await admin.GetAsync("/api/admin/service");response.EnsureSuccessStatusCode();
            using var json=JsonDocument.Parse(await response.Content.ReadAsStringAsync());return json.RootElement.Clone();
        }
        async Task<bool> WaitForState(bool maintenance)
        {
            var elapsed=Stopwatch.StartNew();
            do
            {
                if((await Status()).GetProperty("maintenanceEnabled").GetBoolean()==maintenance)return true;
                await Task.Delay(50);
            }while(elapsed.Elapsed<TimeSpan.FromSeconds(4));
            return false;
        }
        async Task<int> Run(string action,params string[] extra)
        {
            var start=new ProcessStartInfo("powershell.exe"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
            foreach(string argument in new[]{"-NoProfile","-ExecutionPolicy","Bypass","-File",Path.Combine(projectRoot,"Tools","ProjectProject01Backend","ProjectProject01Admin.ps1"),"-Action",action,"-BaseUrl",admin.BaseAddress.ToString(),"-AdminKey",new string('a',48)}.Concat(extra))start.ArgumentList.Add(argument);
            using var child=Process.Start(start)!;using var deadline=new CancellationTokenSource(TimeSpan.FromSeconds(15));
            var stdout=child.StandardOutput.ReadToEndAsync();var stderr=child.StandardError.ReadToEndAsync();
            try{await child.WaitForExitAsync(deadline.Token);}catch(OperationCanceledException){child.Kill(true);throw;}
            await Task.WhenAll(stdout,stderr);
            if(child.ExitCode!=0)
            {string details=((await stdout)+"\n"+(await stderr)).Replace(new string('a',48),"[test-admin-key]");Console.WriteLine("Admin script exit "+child.ExitCode+": "+details[..Math.Min(2400,details.Length)]);}
            return child.ExitCode;
        }
        static string Iso(DateTime value)=>value.ToString("O",CultureInfo.InvariantCulture);
        string future=Iso(DateTime.UtcNow.AddMinutes(10)),end=Iso(DateTime.UtcNow.AddMinutes(20));
        check(await Run("MaintenanceOn","-MaintenanceStartAtUtc",future)!=0,"F16 시작 시각만 있는 잘못된 예약은 스크립트에서 거부");
        check(!(await Status()).GetProperty("manualMaintenanceEnabled").GetBoolean(),"F16 잘못된 예약이 수동 점검을 켜지 않음");
        check(await Run("MaintenanceOn","-MaintenanceStartAtUtc",future,"-ShutdownAtUtc",end,"-Message","예약 점검 한글 공지")==0,"F16 실제 PowerShell 5.1에서 시작·종료 예약 API 호출 성공");
        var scheduled=await Status();
        check(!scheduled.GetProperty("manualMaintenanceEnabled").GetBoolean()&&!scheduled.GetProperty("maintenanceEnabled").GetBoolean()&&scheduled.GetProperty("maintenanceStartAtUtc").ValueKind==JsonValueKind.String,"F16 미래 예약은 영구 수동 점검 없이 예약으로 저장");
        check(scheduled.GetProperty("announcement").GetString()=="예약 점검 한글 공지","F16 PowerShell 5.1 한글 공지 UTF-8 왕복 유지");
        await OperationsIntegration.Execute(database,"UPDATE maintenance_schedules SET starts_at_utc=UTC_TIMESTAMP()-INTERVAL 1 MINUTE;");
        check(await WaitForState(true),"F16 예약 시간에 도달하면 실제 worker가 점검 상태 갱신");
        check(await Run("Announce","-Message","예약 중 새 한글 공지")==0,"F16 예약 중 공지 변경 실행");
        check(!(await Status()).GetProperty("manualMaintenanceEnabled").GetBoolean(),"F16 예약의 유효 점검 상태를 영구 수동 점검으로 잘못 저장하지 않음");
        await OperationsIntegration.Execute(database,"UPDATE maintenance_schedules SET ends_at_utc=UTC_TIMESTAMP()-INTERVAL 1 SECOND;");
        check(await WaitForState(false),"F16 공지 변경 후에도 예약 종료 시 worker가 점검 자동 해제");
        check(await Run("MaintenanceOn")==0&&(await Status()).GetProperty("manualMaintenanceEnabled").GetBoolean(),"F16 시각 없는 기존 수동 점검 지원 유지");
        check(await Run("MaintenanceOff")==0&&!(await Status()).GetProperty("maintenanceEnabled").GetBoolean(),"F16 수동 점검 해제 지원 유지");
        await OperationsIntegration.Execute(database,"DELETE FROM maintenance_schedules; UPDATE service_control SET maintenance_enabled=FALSE,announcement='';");
    }
}

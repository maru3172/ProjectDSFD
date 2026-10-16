internal static class VoiceSecurityChecks
{
    private sealed class Clock : TimeProvider
    {
        internal DateTimeOffset Now=new(2026,10,16,0,0,0,TimeSpan.Zero);
        public override DateTimeOffset GetUtcNow()=>Now;
    }
    public static void Run(Action<bool,string> check)
    {
        var clock=new Clock();var limits=new VoiceAccountRateLimit(clock);
        for(int i=0;i<VoiceAccountRateLimit.RequestsPerMinute;i++)check(limits.TryAcquire(1,out _),$"계정 한도 내 음성 요청 {i+1}");
        check(!limits.TryAcquire(1,out var retry) && retry is >=1 and <=60,"같은 계정의 다른 IP 재요청도 공유 한도");
        check(limits.TryAcquire(2,out _),"같은 학교 IP의 다른 계정은 별도 요청 예산");
        clock.Now=clock.Now.AddSeconds(61);check(limits.TryAcquire(1,out _),"1분 후 계정 예산 복원");
        for(ulong account=2;account<=VoiceAccountRateLimit.MaximumAccounts;account++)limits.TryAcquire(account,out _);
        check(!limits.TryAcquire(99999,out _),"요청 제한 캐시 용량 상한");
        clock.Now=clock.Now.AddMinutes(2);check(limits.TryAcquire(99999,out _),"만료 계정 캐시 정리");
        check(!EosVoiceTokenService.IsIdentityProofText(null),"EOS ID 증명 누락 거부");
        check(!EosVoiceTokenService.IsIdentityProofText("not.a.valid.jwt"),"잘못된 EOS ID 증명 형식 거부");
        check(!EosVoiceTokenService.IsIdentityProofText(new string('a',17000)),"과도한 ID 증명 크기 거부");
        check(EosVoiceTokenService.IsIdentityProofText("aaaaaaaaaaaa.bbbbbbbbbbbb.cccccccccccc"),"JWT 형식 검사와 EOS 서명 검증 분리");
        check(!CrashArtifactEndpoints.ScrubText("JsonWebToken=eyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiJmaXh0dXJlIn0.c2lnbmF0dXJl").Contains("eyJ"),"EOS SDK 진단에서도 원본 JWT 제거");
        var native=typeof(EosVoiceTokenService).GetNestedType("EosNative",System.Reflection.BindingFlags.NonPublic)!;
        Type Layout(string name)=>native.GetNestedType(name,System.Reflection.BindingFlags.NonPublic)!;
        check(System.Runtime.InteropServices.Marshal.SizeOf(Layout("ConnectIdToken"))==24 &&
            System.Runtime.InteropServices.Marshal.SizeOf(Layout("VerifyIdTokenOptions"))==16 &&
            System.Runtime.InteropServices.Marshal.SizeOf(Layout("VerifyIdTokenCallbackInfo"))==88,"로컬 EOS SDK Win64 Pack8 구조체 크기 일치");
    }
}

internal static class VoiceDirectoryChecks
{
    private sealed class Clock : TimeProvider
    {
        public DateTimeOffset Now = new(2026,10,16,0,0,0,TimeSpan.Zero);
        public override DateTimeOffset GetUtcNow() => Now;
    }
    public static void Run(Action<bool,string> check)
    {
        var clock=new Clock();var directory=new VoiceParticipantDirectory(clock);var match=Guid.NewGuid();
        const string first="0123456789abcdef0123456789abcdef",second="fedcba9876543210fedcba9876543210";
        Dictionary<ulong,string> members=new(){[1]="생존자 하나",[2]="생존자 둘"};
        check(directory.TryBind(match,1,first,"client-name-is-not-used"),"음성 ID를 인증 계정에 연결");
        check(!directory.TryBind(match,2,first,"충돌"),"다른 계정의 음성 ID 중복 연결 차단");
        check(directory.TryBind(match,2,second,"old name"),"두 번째 생존자 연결");
        var roster=directory.Read(match,1,members);
        check(roster.Participants.Length==2&&!roster.NeedsIdentityRefresh,"인증된 같은 경기 생존자 목록");
        check(roster.Participants.Any(p=>p.ProductUserId==first&&p.DisplayName=="생존자 하나"),"현재 DB 닉네임 사용");
        check(roster.Participants.Any(p=>p.ProductUserId==first&&p.UserId=="1"),"거리 게인 연결용 계정 ID는 서버가 제공");
        check(directory.Read(match,3,members).Participants.Length==0,"마네킹·경기 밖 계정에는 이름 비공개");
        check(directory.Read(Guid.NewGuid(),1,members).Participants.Length==0,"다른 매치 이름 혼합 차단");
        members.Remove(2);
        check(directory.Read(match,1,members).Participants.Length==1,"이탈한 생존자 이름 제외");
        clock.Now=clock.Now.AddMinutes(6);
        check(directory.Read(match,1,members).NeedsIdentityRefresh,"만료·재시작 후 재등록 필요 안내");
        check(directory.TryBind(match,1,first,"복구"),"새 음성 토큰 발급 후 이름 복구");
        for(int index=1;index<VoiceParticipantDirectory.MaximumMatches;++index)
            directory.TryBind(Guid.NewGuid(),1,first,"테스트");
        check(!directory.TryBind(Guid.NewGuid(),1,first,"초과"),"음성 이름 메모리 상한 유지");
    }
}

namespace ProjectProject01DiagnosticsDashboard;

// Display-only localization. Files, metric keys, filtering and sorting keep the original schema.
internal static class DiagnosticUiText
{
    private static readonly Dictionary<string,string> Names = new(StringComparer.OrdinalIgnoreCase)
    {
        ["FPS"]="초당 프레임 수", ["Frame ms"]="프레임 시간 (밀리초)", ["Game thread ms"]="게임 스레드 시간 (밀리초)",
        ["Draw thread ms"]="렌더 스레드 시간 (밀리초)", ["GPU ms"]="그래픽 처리 시간 (밀리초)", ["Physical memory MiB"]="물리 메모리 (MiB)",
        ["Virtual memory MiB"]="가상 메모리 (MiB)", ["Memory growth MiB/s"]="메모리 증가량 (MiB/초)", ["Memory growth MiB/min"]="메모리 증가량 (MiB/분)",
        ["UObject count"]="언리얼 객체 수", ["Actor count"]="액터 수", ["Streaming texture MiB"]="스트리밍 텍스처 메모리 (MiB)",
        ["Non-streaming texture MiB"]="비스트리밍 텍스처 메모리 (MiB)", ["Texture pool MiB"]="텍스처 풀 (MiB)",
        ["Inbound KiB/s"]="수신량 (KiB/초)", ["Outbound KiB/s"]="송신량 (KiB/초)", ["AI tick ms"]="인공지능 갱신 시간 (밀리초)",
        ["Line traces"]="직선 충돌 검사 횟수", ["Path failures"]="경로 탐색 실패 횟수", ["Connections"]="게임 연결 수",
        ["Diagnostics total overhead ms"]="전체 진단 수집 시간 (밀리초)", ["Diagnostics overhead ms"]="진단 수집 시간 (밀리초)",
        ["Diagnostics Actor/AI overhead ms"]="액터·인공지능 진단 수집 시간 (밀리초)", ["Diagnostics navigation overhead ms"]="경로 탐색 진단 수집 시간 (밀리초)",
        ["Diagnostics network overhead ms"]="네트워크 진단 수집 시간 (밀리초)", ["Diagnostics frame timing overhead ms"]="프레임 진단 수집 시간 (밀리초)",
        ["Diagnostics memory overhead ms"]="메모리 진단 수집 시간 (밀리초)", ["Diagnostics texture/RHI overhead ms"]="텍스처·렌더링 진단 수집 시간 (밀리초)",
        ["Diagnostics bookkeeping overhead ms"]="진단 기록 관리 시간 (밀리초)", ["Diagnostics buffered memory MiB"]="진단 기록 버퍼 메모리 (MiB)",
        ["Server-client timestamp offset ms"]="서버·클라이언트 시각 차이 (밀리초)", ["Total risk candidates"]="전체 위험 후보 수",
        ["All maps"]="모든 맵", ["All outcomes"]="모든 결과", ["Pass"]="통과", ["ReviewRequired"]="검토 필요", ["TimedOut"]="시간 초과", ["Inconclusive"]="판단 보류",
        ["All searchable fields"]="전체 검색 항목", ["All values"]="모든 값", ["Performance"]="성능", ["Memory"]="메모리", ["Network"]="네트워크",
        ["AI/Navigation"]="인공지능·경로 탐색", ["Tuning/Data"]="튜닝·데이터", ["Multiplayer"]="멀티플레이", ["Engine/Logs"]="엔진·로그", ["Other"]="기타",
        ["Role"]="역할", ["NetMode"]="네트워크 모드", ["GameSeconds"]="게임 경과 시간", ["Severity"]="심각도", ["Code"]="분석 코드", ["Message"]="내용", ["Actor"]="액터",
        ["TestRunId"]="실행 식별자", ["Roles"]="역할", ["Map"]="맵", ["GameMode"]="게임 모드", ["Outcome"]="결과", ["RiskCount"]="위험 후보 수", ["Samples"]="표본 수",
        ["Players"]="플레이어 수", ["Mannequins"]="마네킹 수", ["Tuning"]="튜닝 식별값", ["RunDirectory"]="기록 폴더", ["Metrics"]="집계 지표", ["TimeAlignment"]="시각 정렬",
        ["RiskByCategory"]="영역별 위험", ["RiskByCode"]="코드별 위험", ["ExtendedCaptures"]="상세 캡처", ["UtcTime"]="기록 시각",
        ["Category"]="영역", ["Count"]="횟수", ["RelatedActors"]="관련 액터", ["FirstGameSeconds"]="최초 발생 (초)", ["LastGameSeconds"]="최종 발생 (초)",
        ["Reason"]="검토 이유", ["Evidence"]="관측 근거", ["Metric"]="항목", ["Value"]="값", ["Unit"]="단위", ["Scope"]="측정 범위",
        ["Average"]="평균", ["Maximum"]="최대", ["AveragePercentOfTotalTime"]="전체 수집 시간 대비 평균 비율 (%)",
        ["Normal"]="일반", ["RiskCandidate"]="위험 후보", ["Risk"]="위험", ["Warning"]="경고", ["Error"]="오류", ["Client"]="클라이언트",
        ["HTTP server"]="요청 서버", ["HTTP client"]="요청 클라이언트", ["DB"]="데이터베이스", ["Result recovery"]="경기 결과 복구",
        ["정상 (Closed)"]="정상", ["요청 차단 (Open)"]="요청 차단", ["복구 시험 (HalfOpen)"]="복구 시험",
        ["DedicatedServer"]="전용 서버", ["ListenServer"]="호스트 서버", ["Standalone"]="단독 실행", ["Unknown"]="확인 불가", ["None"]="없음",
        ["Unavailable"]="측정 불가", ["Succeeded"]="성공", ["Failed"]="실패",
        ["Backend"]="백엔드",
        ["Actor / AI state scan"]="액터·인공지능 상태 조회", ["Navigation query"]="경로 탐색 조회", ["Network state query"]="네트워크 상태 조회",
        ["Frame / CPU / GPU timing query"]="프레임·처리 시간 조회", ["Process memory / UObject query"]="프로세스 메모리·언리얼 객체 조회",
        ["Texture / RHI memory query"]="텍스처·렌더링 메모리 조회", ["Risk, events and bookkeeping"]="위험·이벤트·기록 관리", ["Total"]="전체",
        ["Estimated one-core duty at 1 Hz"]="단일 코어 대비 추정 수집 점유율", ["Buffered diagnostic Samples / Events"]="진단 표본·이벤트 버퍼",
        ["ms / sample"]="밀리초/표본", ["% of one CPU core"]="단일 코어 대비 %", ["MiB system RAM (lower-bound)"]="시스템 메모리 MiB (하한 추정)"
    };
    internal static string Common(string raw) => Names.GetValueOrDefault(raw, raw);
    internal static string ComparisonLabel(string text) => text.Replace("Game thread", "게임 스레드").Replace("Draw thread", "렌더 스레드")
        .Replace("Frame timing", "프레임 시간").Replace("Frame", "프레임").Replace("Actor/AI", "액터·인공지능").Replace("Navigation", "경로 탐색")
        .Replace("Network", "네트워크").Replace("Memory", "메모리").Replace("Texture/RHI", "텍스처·렌더링").Replace("Bookkeeping", "기록 관리")
        .Replace("Inbound", "수신").Replace("Outbound", "송신").Replace("AI Tick", "인공지능 갱신").Replace("UObject", "언리얼 객체").Replace("Actor", "액터");
    internal static string Metric(string raw)
    {
        if (Names.TryGetValue(raw, out string? known)) return known;
        if (raw.StartsWith("Max ", StringComparison.Ordinal)) return "최대 " + Metric(raw[4..]);
        if (raw.StartsWith("Average ", StringComparison.Ordinal)) return "평균 " + Metric(raw[8..]);
        if (raw.StartsWith("Risk: ", StringComparison.Ordinal)) return Common(raw[6..]) + " 위험 후보 수";
        if (raw.StartsWith("Risk code: ", StringComparison.Ordinal)) return "위험 후보 · " + DashboardForm.RiskReason(raw[11..]);
        return LongTermDiagnostics.Label(LongTermDiagnostics.GameKey(raw)).Replace("게임 · ", "");
    }
    internal static void Localize(ComboBox selector, Func<string,string>? label = null)
    {
        selector.FormattingEnabled = true;
        selector.Format += (_, e) => { if (e.ListItem is string raw) e.Value = (label ?? Common)(raw); };
    }
    internal static void Localize(DataGridView grid)
    {
        grid.DataBindingComplete += (_, _) =>
        {
            foreach (DataGridViewColumn column in grid.Columns) column.HeaderText = Common(column.Name);
            foreach (string name in new[] { "Metrics", "RiskByCategory", "RiskByCode", "TimeAlignment", "ExtendedCaptures", "Number" })
                if (grid.Columns.Contains(name)) grid.Columns[name]!.Visible = false;
        };
        grid.CellFormatting += (_, e) =>
        {
            if (e.Value is not string raw) return;
            string field = grid.Columns[e.ColumnIndex].Name;
            if (field == "Metric") e.Value = Metric(raw);
            else if (field != "Message" && field != "Evidence" && field != "Actor" && field != "RelatedActors") e.Value = Common(raw);
        };
    }
}

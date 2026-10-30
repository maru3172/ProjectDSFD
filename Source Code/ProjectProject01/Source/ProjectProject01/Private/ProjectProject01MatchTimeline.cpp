#include "ProjectProject01MatchTimeline.h"

void FProjectMatchTimelineLog::Add(double Seconds, EProjectMatchEvent Kind, const FString& PlayerName, const FString& Detail)
{
    if (!FMath::IsFinite(Seconds)) return;
    if (Events.Num() >= MaximumEvents)
    {
        Events.RemoveAt(1); // Keep the start, retain recent events, and disclose the omitted count.
        ++OmittedEvents;
    }
    auto Clean = [](const FString& Value, int32 Limit)
    {
        FString Result = Value.Left(Limit);
        for (TCHAR& Character : Result) if (Character < TEXT(' ')) Character = TEXT(' ');
        return Result;
    };
    FProjectMatchTimelineEvent& Event = Events.AddDefaulted_GetRef();
    Event.Seconds = FMath::Max(Events.Num() > 1 ? Events[Events.Num()-2].Seconds : 0.0, FMath::Max(0.0, Seconds));
    Event.Kind = Kind;
    Event.PlayerName = Clean(PlayerName, 48);
    Event.Detail = Clean(Detail, 96);
}

FText ProjectProject01Timeline::FormatEvent(const FProjectMatchTimelineEvent& Event)
{
    FText Action;
    switch (Event.Kind)
    {
    case EProjectMatchEvent::Started: Action = NSLOCTEXT("MatchTimeline", "Started", "경기 시작"); break;
    case EProjectMatchEvent::LevelStarted: Action = NSLOCTEXT("MatchTimeline", "LevelStarted", "다음 맵 시작"); break;
    case EProjectMatchEvent::LevelTravel: Action = NSLOCTEXT("MatchTimeline", "LevelTravel", "다음 맵 이동"); break;
    case EProjectMatchEvent::DeathCountLost: Action = NSLOCTEXT("MatchTimeline", "DeathCountLost", "피격 · 데스카운트 감소"); break;
    case EProjectMatchEvent::Eliminated: Action = NSLOCTEXT("MatchTimeline", "Eliminated", "탈락"); break;
    case EProjectMatchEvent::Escaped: Action = NSLOCTEXT("MatchTimeline", "Escaped", "탈출 성공"); break;
    case EProjectMatchEvent::Disconnected: Action = NSLOCTEXT("MatchTimeline", "Disconnected", "연결 끊김 · 재접속 대기"); break;
    case EProjectMatchEvent::Reconnected: Action = NSLOCTEXT("MatchTimeline", "Reconnected", "재접속"); break;
    case EProjectMatchEvent::Forfeited: Action = NSLOCTEXT("MatchTimeline", "Forfeited", "기권"); break;
    case EProjectMatchEvent::Ended: Action = NSLOCTEXT("MatchTimeline", "Ended", "경기 종료"); break;
    case EProjectMatchEvent::ReplacedByBot: Action = NSLOCTEXT("MatchTimeline", "ReplacedByBot", "봇이 이어서 플레이"); break;
    }
    const int32 Seconds = FMath::Clamp(FMath::FloorToInt(Event.Seconds), 0, 359999);
    FText Detail = FText::FromString(Event.Detail);
    if (Event.Kind == EProjectMatchEvent::DeathCountLost)
        Detail = FText::Format(NSLOCTEXT("MatchTimeline","Remaining","남은 데카: {0}"), Detail);
    else if (Event.Kind == EProjectMatchEvent::Forfeited)
        Detail = Event.Detail == TEXT("ReconnectTimeout") ? NSLOCTEXT("MatchTimeline","Timeout","재접속 유예 만료")
            : Event.Detail == TEXT("Deserted") ? NSLOCTEXT("MatchTimeline","Deserted","경기 이탈") : NSLOCTEXT("MatchTimeline","Exit","직접 나가기");
    else if (Event.Kind == EProjectMatchEvent::Ended)
    {
        if (Event.Detail == TEXT("TimeLimit")) Detail = NSLOCTEXT("MatchTimeline","TimeLimit","제한 시간 종료");
        else if (Event.Detail == TEXT("MannequinForfeit")) Detail = NSLOCTEXT("MatchTimeline","MannequinForfeit","마네킹 기권 · 생존자 승리");
        else Detail = NSLOCTEXT("MatchTimeline","Resolved","승패 확정");
    }
    return FText::Format(NSLOCTEXT("MatchTimeline", "Row", "[{0}] {1}{2}{3}"),
        FText::FromString(FString::Printf(TEXT("%02d:%02d"), Seconds/60, Seconds%60)),
        Event.PlayerName.IsEmpty() ? FText::GetEmpty() : FText::FromString(Event.PlayerName + TEXT(" · ")),
        Action, Event.Detail.IsEmpty() ? FText::GetEmpty() : FText::Format(NSLOCTEXT("MatchTimeline","Detail"," · {0}"), Detail));
}

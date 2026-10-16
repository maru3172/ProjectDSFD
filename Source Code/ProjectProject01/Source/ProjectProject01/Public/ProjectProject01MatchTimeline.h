#pragma once

#include "CoreMinimal.h"
#include "ProjectProject01MatchTimeline.generated.h"

UENUM(BlueprintType)
enum class EProjectMatchEvent : uint8
{
    Started, LevelStarted, LevelTravel, DeathCountLost, Eliminated, Escaped,
    Disconnected, Reconnected, Forfeited, Ended
};

/** Final-result-only data. No actor references, account IDs, hidden positions or live replication. */
USTRUCT(BlueprintType)
struct PROJECTPROJECT01_API FProjectMatchTimelineEvent
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) double Seconds = 0.0;
    UPROPERTY(BlueprintReadOnly) EProjectMatchEvent Kind = EProjectMatchEvent::Started;
    UPROPERTY(BlueprintReadOnly) FString PlayerName;
    UPROPERTY(BlueprintReadOnly) FString Detail;
};

/** Bounded server memory, carried only across a committed level transition of the same match. */
struct PROJECTPROJECT01_API FProjectMatchTimelineLog
{
    static constexpr int32 MaximumEvents = 128;
    FString MatchId;
    TArray<FProjectMatchTimelineEvent> Events;
    int32 OmittedEvents = 0;
    double PreviousLevelsSeconds = 0.0;
    bool bAwaitingNextLevel = false;
    void Add(double Seconds, EProjectMatchEvent Kind, const FString& PlayerName = {}, const FString& Detail = {});
};

namespace ProjectProject01Timeline
{
    PROJECTPROJECT01_API FText FormatEvent(const FProjectMatchTimelineEvent& Event);
}

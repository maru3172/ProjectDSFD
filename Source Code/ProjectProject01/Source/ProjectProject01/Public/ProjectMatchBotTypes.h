#pragma once
#include "CoreMinimal.h"

// Trusted backend-to-server admission metadata, never read from a client's travel URL.
struct PROJECTPROJECT01_API FProjectMatchBotSpec
{
    FString BotId,DisplayName,Role;
    int32 Difficulty=2;
    // Stored experience for this role and level from earlier matches (optional). Not part of the
    // identity: another match may update it between a player's admission and their reconnect.
    FString Experience;
    bool operator==(const FProjectMatchBotSpec& Other)const
    {return BotId==Other.BotId&&DisplayName==Other.DisplayName&&Role==Other.Role&&Difficulty==Other.Difficulty;}
    static bool Parse(const TArray<TSharedPtr<class FJsonValue>>& Values,TArray<FProjectMatchBotSpec>& Out,FString& Error);
    static bool IsExperienceText(const FString& Text);
};

// Experience a finished match hands back to the backend, one pool per role and level.
struct PROJECTPROJECT01_API FProjectBotExperienceUpload
{
    FString Role,Payload;
    int32 Difficulty=2;
};

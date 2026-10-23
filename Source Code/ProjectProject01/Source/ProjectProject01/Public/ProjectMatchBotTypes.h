#pragma once
#include "CoreMinimal.h"

// Trusted backend-to-server admission metadata, never read from a client's travel URL.
struct PROJECTPROJECT01_API FProjectMatchBotSpec
{
    FString BotId,DisplayName,Role;
    int32 Difficulty=2;
    bool operator==(const FProjectMatchBotSpec& Other)const
    {return BotId==Other.BotId&&DisplayName==Other.DisplayName&&Role==Other.Role&&Difficulty==Other.Difficulty;}
    static bool Parse(const TArray<TSharedPtr<class FJsonValue>>& Values,TArray<FProjectMatchBotSpec>& Out,FString& Error);
};

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ProjectCaseAchievements
{
    struct FDefinition{FString Area,Code;};
    inline const TArray<FDefinition>& Definitions()
    {
        static const TArray<FDefinition> Values{{TEXT("체크포인트"),TEXT("campaign_checkpoint")},{TEXT("탈출"),TEXT("campaign_escape")},{TEXT("완주"),TEXT("campaign_complete")}};return Values;
    }
    inline TSet<FString> Earned(const TSet<FString>& Local,const TSharedPtr<FJsonObject>& State)
    {
        TSet<FString> All=Local;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(State&&State->TryGetArrayField(TEXT("achievements"),Rows))for(const auto& Value:*Rows)
        {FString Code;const auto Entry=Value->AsObject();if(Entry&&Entry->TryGetStringField(TEXT("code"),Code))All.Add(Code);}
        TSet<FString> Result;for(const auto& Def:Definitions())if(All.Contains(Def.Code))Result.Add(Def.Code);return Result;
    }
    inline FString GlobalRate(const TSharedPtr<FJsonObject>& Rates,const FString& Code)
    {
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(Rates&&Rates->TryGetArrayField(TEXT("achievements"),Rows))for(const auto& Value:*Rows)
        {
            const auto Entry=Value->AsObject();FString Id;double Percentage=0;
            if(Entry&&Entry->TryGetStringField(TEXT("code"),Id)&&Id==Code&&Entry->TryGetNumberField(TEXT("percentage"),Percentage)&&FMath::IsFinite(Percentage)&&Percentage>=0&&Percentage<=100)
                return FString::Printf(TEXT("전체 달성률 %.1f%%"),Percentage);
        }
        return TEXT("전체 달성률 —");
    }
}

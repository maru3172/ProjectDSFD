#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ProjectCaseStoryArchive
{
    inline FString Identity(const TSharedPtr<FJsonValue>& Value)
    {
        if(!Value||Value->Type!=EJson::Object)return FString();const auto Entry=Value->AsObject();FString Season;
        const TArray<TSharedPtr<FJsonValue>>* Stages=nullptr;
        if(!Entry||!Entry->TryGetStringField(TEXT("season"),Season)||Season.IsEmpty()||Season.Len()>24||!Entry->TryGetArrayField(TEXT("stages"),Stages)||Stages->Num()!=13)return FString();
        TSet<int32> Seen;
        for(const auto& StageValue:*Stages)
        {
            if(!StageValue||StageValue->Type!=EJson::Object)return FString();const auto Stage=StageValue->AsObject();double Index=0;FString Story;
            if(!Stage->TryGetNumberField(TEXT("stage"),Index)||Index!=FMath::FloorToDouble(Index)||Index<1||Index>13||Seen.Contains(int32(Index))||!Stage->TryGetStringField(TEXT("story"),Story)||Story.Len()>20000)return FString();
            Seen.Add(int32(Index));
        }
        return Season;
    }
    // Public stories are append-only locally too. A stale/empty server response
    // cannot erase already archived seasons or replace their frozen text.
    inline bool Merge(const TSharedPtr<FJsonObject>& Existing,const TSharedPtr<FJsonObject>& Incoming,TSharedPtr<FJsonObject>& Out,FString& Encoded)
    {
        const TArray<TSharedPtr<FJsonValue>>* NewRows=nullptr,*OldRows=nullptr;
        if(!Incoming||!Incoming->TryGetArrayField(TEXT("archives"),NewRows)||NewRows->Num()>512)return false;
        if(Existing)Existing->TryGetArrayField(TEXT("archives"),OldRows);
        TMap<FString,TSharedPtr<FJsonValue>> Old;
        if(OldRows)for(const auto& Value:*OldRows){const FString Id=Identity(Value);if(!Id.IsEmpty())Old.FindOrAdd(Id)=Value;}
        TArray<TSharedPtr<FJsonValue>> Rows;TSet<FString> Added;
        for(const auto& Value:*NewRows){const FString Id=Identity(Value);if(!Id.IsEmpty()&&!Added.Contains(Id)){Rows.Add(Old.Contains(Id)?Old[Id]:Value);Added.Add(Id);}}
        if(OldRows)for(const auto& Value:*OldRows){const FString Id=Identity(Value);if(!Id.IsEmpty()&&!Added.Contains(Id)){Rows.Add(Value);Added.Add(Id);}}
        auto Result=MakeShared<FJsonObject>();Result->SetArrayField(TEXT("archives"),Rows);FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Encoded));
        if(Encoded.Len()>8*1024*1024)return false;Out=Result;return true;
    }
}

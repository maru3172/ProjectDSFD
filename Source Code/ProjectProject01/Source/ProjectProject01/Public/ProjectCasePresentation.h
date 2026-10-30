#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ProjectCasePresentation
{
    inline FString Equipped(const TSharedPtr<FJsonObject>& State,const TCHAR* SlotName)
    {
        const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;if(!State||!State->TryGetArrayField(TEXT("loadout"),Entries))return {};
        for(const auto& Entry:*Entries)
        {
            const auto Item=Entry->AsObject();FString Slot,Id;
            if(Item&&Item->TryGetStringField(TEXT("slot"),Slot)&&Slot==SlotName&&Item->TryGetStringField(TEXT("itemId"),Id))return Id;
        }
        return {};
    }
    inline FString ItemName(const FString& Id)
    {
        if(Id.StartsWith(TEXT("level_frame_")))return TEXT("레벨 ")+Id.RightChop(12)+TEXT(" 기념 테두리");
        if(Id.StartsWith(TEXT("investigation_seal_")))return TEXT("희귀 사건 완성 봉인 ")+Id.RightChop(19);
        if(Id==TEXT("frame_crimson"))return TEXT("붉은 봉인 테두리");
        if(Id==TEXT("frame_silver"))return TEXT("조사관 테두리");
        if(Id==TEXT("icon_file"))return TEXT("사건 기록 아이콘");
        if(Id==TEXT("icon_seal"))return TEXT("증거 봉인 아이콘");
        if(Id==TEXT("title_witness"))return TEXT("목격자 명찰");return TEXT("기본");
    }
    inline FLinearColor FrameColor(const TSharedPtr<FJsonObject>& State)
    {
        const FString Id=Equipped(State,TEXT("Frame"));
        if(Id==TEXT("frame_archive"))return FLinearColor(.24f,.26f,.28f);
        if(Id.StartsWith(TEXT("level_frame_")))return FMath::Lerp(FLinearColor(.22f,.12f,.06f),FLinearColor(.46f,.12f,.10f),FMath::Clamp(FCString::Atoi(*Id.RightChop(12))/100.f,0.f,1.f));
        if(Id.StartsWith(TEXT("investigation_seal_")))return FMath::Lerp(FLinearColor(.25f,.16f,.05f),FLinearColor(.50f,.36f,.10f),FMath::Clamp(FCString::Atoi(*Id.RightChop(19))/13.f,0.f,1.f));
        return Id==TEXT("frame_crimson")?FLinearColor(.32f,.065f,.05f):Id==TEXT("frame_silver")?FLinearColor(.29f,.32f,.35f):FLinearColor(.065f,.07f,.075f);
    }
    inline FString Identity(const TSharedPtr<FJsonObject>& State,const FString& Name)
    {
        const FString Icon=Equipped(State,TEXT("Icon"));
        const FString Frame=Equipped(State,TEXT("Frame"));
        const FString Seal=Frame.StartsWith(TEXT("investigation_seal_"))?TEXT("[조사 ")+Frame.RightChop(19)+TEXT("] "):FString();
        return Seal+(Icon==TEXT("icon_file")?FString(TEXT("▣ ")):Icon==TEXT("icon_seal")?FString(TEXT("◆ ")):FString())+Name+
            (Equipped(State,TEXT("Nameplate"))==TEXT("title_witness")?TEXT(" · 목격자"):TEXT(""));
    }
}

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FProjectCustomMatchRules
{
    bool bEnabled=false;int32 DeathCount=3,MannequinCount=10,TimeLimitSeconds=1800;
    bool IsValid()const{return DeathCount>=1&&DeathCount<=20&&MannequinCount>=1&&MannequinCount<=10&&TimeLimitSeconds>=0&&TimeLimitSeconds<=7200&&TimeLimitSeconds%60==0;}
    bool operator==(const FProjectCustomMatchRules& R)const{return bEnabled==R.bEnabled&&DeathCount==R.DeathCount&&MannequinCount==R.MannequinCount&&TimeLimitSeconds==R.TimeLimitSeconds;}
    static bool Parse(const TSharedPtr<FJsonObject>& Json,FProjectCustomMatchRules& Out)
    {
        if(!Json)return false;FProjectCustomMatchRules R;double D=0,N=0,T=0;
        if(!Json->TryGetBoolField(TEXT("enabled"),R.bEnabled)||!Json->TryGetNumberField(TEXT("deathCount"),D)||!Json->TryGetNumberField(TEXT("mannequinCount"),N)||!Json->TryGetNumberField(TEXT("timeLimitSeconds"),T)||
           !FMath::IsFinite(D)||!FMath::IsFinite(N)||!FMath::IsFinite(T)||D<1||D>20||N<1||N>10||T<0||T>7200||D!=FMath::FloorToDouble(D)||N!=FMath::FloorToDouble(N)||T!=FMath::FloorToDouble(T))return false;
        R.DeathCount=int32(D);R.MannequinCount=int32(N);R.TimeLimitSeconds=int32(T);if(!R.IsValid())return false;Out=R;return true;
    }
    TSharedRef<FJsonObject> ToJson()const
    {auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("enabled"),bEnabled);J->SetNumberField(TEXT("deathCount"),DeathCount);J->SetNumberField(TEXT("mannequinCount"),MannequinCount);J->SetNumberField(TEXT("timeLimitSeconds"),TimeLimitSeconds);return J;}
};

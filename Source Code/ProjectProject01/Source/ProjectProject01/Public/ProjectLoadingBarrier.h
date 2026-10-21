#pragma once
#include "CoreMinimal.h"
struct FProjectLoadingBarrier
{
	FString Epoch;
	TMap<FString,uint8> Progress;
	double StartedAt=0;
	int32 Expected=3;
	bool Released=false;
	void Begin(int32 Count,double Now){Epoch=FGuid::NewGuid().ToString();Progress.Reset();Expected=FMath::Max(1,Count);StartedAt=Now;Released=false;}
	void Join(const FString& User){Progress.FindOrAdd(User,70);}
	void Leave(const FString& User){Progress.Remove(User);}
	bool Report(const FString& User,const FString& IncomingEpoch,uint8 Value)
	{auto* Existing=Progress.Find(User);if(!Existing || IncomingEpoch!=Epoch || Value>100)return false;*Existing=FMath::Max(*Existing,Value);return true;}
	bool AllReady() const
	{if(Progress.Num()!=Expected)return false;for(const auto& P:Progress)if(P.Value<100)return false;return true;}
	bool Expired(double Now) const{return StartedAt>0 && !Released && Now-StartedAt>120;}
};

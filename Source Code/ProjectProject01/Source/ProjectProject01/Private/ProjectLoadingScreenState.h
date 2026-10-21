#pragma once
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "ProjectLoadingScreenSubsystem.h"

/** Slate/MoviePlayer may read this on their loading thread. Never stores UObjects. */
struct FProjectLoadingScreenState
{
	struct FSnapshot
	{
		bool Active=false,Multiplayer=false,Released=false,Closing=false,CanCancel=false;
		uint8 Progress=0;
		int32 Expected=1;
		FString Stage=TEXT("연결 준비 중"),OwnId,OwnName=TEXT("플레이어"),OwnRole,Map;
		TArray<FProjectLoadingParticipant> Participants;
		double Started=0,ClosingAt=0;
	};
	mutable FCriticalSection Mutex;
	FSnapshot Data;
	FSnapshot Read() const {FScopeLock Lock(&Mutex);return Data;}
	void Begin(bool Multiplayer,double Now)
	{FScopeLock Lock(&Mutex);Data={};Data.Active=true;Data.Multiplayer=Multiplayer;Data.Released=!Multiplayer;Data.Expected=Multiplayer?3:1;Data.Started=Now;}
	void Advance(uint8 Progress,const FString& Stage)
	{FScopeLock Lock(&Mutex);if(Data.Active && Progress>=Data.Progress){Data.Progress=FMath::Min<uint8>(Progress,100);Data.Stage=Stage;}}
	bool TryClose(double Now)
	{
		FScopeLock Lock(&Mutex);
		if(!Data.Active || Data.Progress<100 || !Data.Released || Now-Data.Started<.7) return false;
		if(!Data.Closing){Data.Closing=true;Data.ClosingAt=Now;Data.Stage=Data.Multiplayer?TEXT("전원 준비 완료 — 게임으로 이동합니다"):TEXT("준비 완료 — 게임으로 이동합니다");}
		return true;
	}
	float ContentOpacity(double Now) const
	{const auto S=Read();return S.Closing?1.f-FMath::Clamp(float((Now-S.ClosingAt)/.4),0.f,1.f):FMath::Clamp(float((Now-S.Started)/.35),0.f,1.f);}
};

TSharedRef<SWidget> MakeProjectLoadingScreenWidget(TSharedPtr<FProjectLoadingScreenState,ESPMode::ThreadSafe> State,FSimpleDelegate OnCancel=FSimpleDelegate());

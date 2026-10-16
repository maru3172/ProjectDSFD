#pragma once

#include "CoreMinimal.h"

/** Local receive preferences only; never replicated or sent to the other participant. */
struct FProjectVoiceParticipantControls
{
	TSet<FString> Muted;
	TMap<FString, float> Volumes;

	static bool IsPeer(const FString& Id, const FString& LocalId, const TArray<FString>& Peers)
	{
		return !Id.IsEmpty() && Id != LocalId && Peers.Contains(Id);
	}
	bool SetMuted(const FString& Id, const FString& LocalId, const TArray<FString>& Peers, bool bMuted)
	{
		if (!IsPeer(Id, LocalId, Peers)) return false;
		if (bMuted) Muted.Add(Id); else Muted.Remove(Id);
		return true;
	}
	bool SetVolume(const FString& Id, const FString& LocalId, const TArray<FString>& Peers, float Volume)
	{
		if (!IsPeer(Id, LocalId, Peers) || !FMath::IsFinite(Volume)) return false;
		Volumes.Add(Id, FMath::Clamp(Volume, 0.0f, 1.0f));
		return true;
	}
	float GetVolume(const FString& Id) const
	{
		const float* Value = Volumes.Find(Id);
		return Value ? *Value : 1.0f;
	}
	TSet<FString> GetAdjustedIds() const
	{
		TSet<FString> Result = Muted;
		for (const auto& Pair : Volumes) Result.Add(Pair.Key);
		return Result;
	}
	void Reset() { Muted.Reset(); Volumes.Reset(); }
};

struct FProjectVoiceParticipantState
{
	FString Id;
	FString DisplayName;
	float Volume = 1.0f;
	bool bMuted = false;
	bool bTalking = false;
	FString GetStatusLabel() const
	{
		return bMuted?TEXT("음소거됨"):Volume<=0?TEXT("수신 음량 0%"):bTalking?TEXT("말하는 중"):TEXT("대기");
	}
};

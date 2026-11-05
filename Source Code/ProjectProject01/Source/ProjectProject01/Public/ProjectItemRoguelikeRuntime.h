// File: Source/ProjectProject01/Public/ProjectItemRoguelikeRuntime.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Math/RandomStream.h"
#include "ProjectItemRoguelike.h"
#include "ProjectItemRoguelikeRuntime.generated.h"

class AMultiplayTestGameMode;
class AMultiplayTestPlayerController;
struct FMultiplayTestServerPlayerRecord;

/** One player's cards across the match (kept by user id so a reconnect keeps them). */
struct FProjectRoguePlayer
{
    FString UserId,Name,Role;
    bool bBot=false;
    TArray<FString> Picks;TArray<bool> Auto; // In pick order (floor 0, 1, ...).
    TArray<FString> Offer;TArray<FString> FirstOffer;
    TArray<FString> Spent; // One-use items already used up this match (S03).
    bool bPicked=true,bRerollUsed=false;
    bool bInfected=false; // Custom game infection: a survivor now playing a mannequin body; offered mannequin items only.
    int32 Serial=0;
};

/** What crosses a floor change with the match (FMultiplayTestMatchCarryover). */
struct FProjectRogueCarry
{
    EProjectMatchMode Mode=EProjectMatchMode::Normal;
    FString MatchId;
    TMap<FString,FProjectRoguePlayer> Players;
    TArray<FString> Buffs;
    int32 Rounds=0;
};

/**
 * Server side of the item roguelike mode (Docs/ItemRoguelikeMode.md). While the loading gate is black, every player in
 * play gets three cards and picks one; the gate opens when all have picked or after 20 s (the server picks at random for
 * the rest). The server keeps the offer, so a client only says which of its three cards (or one reroll per pick).
 * Effects are applied by UProjectAffixRuntime: buffs join the active affix rules, items are read through HasItem.
 */
UCLASS()
class PROJECTPROJECT01_API UProjectItemRoguelikeRuntime : public UObject
{
    GENERATED_BODY()
public:
    bool Accept(const FString& Match,EProjectMatchMode InMode,FString& Error);
    bool IsActive()const{return Mode==EProjectMatchMode::ItemRoguelike;}
    EProjectMatchMode GetMode()const{return Mode;}
    bool IsPicking()const{return bPicking;}
    int32 GetRounds()const{return Rounds;}
    /** Called while the loading gate is ready (or a pick is open). True while the gate must stay black. */
    bool HoldGate(AMultiplayTestGameMode* GM,double RealNow);
    bool Pick(AMultiplayTestGameMode* GM,AMultiplayTestPlayerController* PC,int32 Serial,int32 Index);
    bool Reroll(AMultiplayTestGameMode* GM,AMultiplayTestPlayerController* PC,int32 Serial);
    /** A player who (re)joins during the pick sees the open offer again. */
    void Resend(AMultiplayTestGameMode* GM,AMultiplayTestPlayerController* PC);
    /** Who a match record is for these cards: the user id, or the display name in ticket-free editor play. */
    static FString KeyOf(const FMultiplayTestServerPlayerRecord& Record);
    bool HasItem(const FString& UserId,const TCHAR* Id)const;
    bool HasBuff(const TCHAR* Id)const{return IsActive()&&Buffs.Contains(Id);}
    bool HasAnyBuff()const{return IsActive()&&!Buffs.IsEmpty();}
    /** Affixes already on for the whole match (custom game mix switch); never offered as buff cards. */
    void SetFixedBuffs(const TArray<FString>& Ids){FixedBuffs=Ids;}
    /** Uses up a one-use item this player owns. True the first time only. */
    bool Spend(const FString& Key,const TCHAR* Id){auto* P=IsActive()?Players.Find(Key):nullptr;if(!P||!P->Picks.Contains(Id)||P->Spent.Contains(Id))return false;P->Spent.Add(Id);return true;}
    bool IsSpent(const FString& Key,const TCHAR* Id)const{const auto* P=Players.Find(Key);return P&&P->Spent.Contains(Id);}
    /** True for the first query after this floor's pick closed (effects that fire once per floor start). */
    bool ConsumeFloorStart(){const bool B=bFloorStartPending;bFloorStartPending=false;return B;}
    FProjectRoguePickRecord RecordFor(const FString& UserId)const;
    TArray<FProjectRoguePickRecord> Records()const;
    FProjectRogueCarry Export()const;
    void Import(const FProjectRogueCarry& Carry);

    // Exposed for the automation test.
    const FProjectRoguePlayer* FindPlayer(const FString& UserId)const{return Players.Find(UserId);}
    void SeedForTest(int32 Seed){Rng.Initialize(Seed);}
    void OpenForTest(const TArray<FMultiplayTestServerPlayerRecord>& Records,double RealNow);
    bool PickForTest(const FString& UserId,int32 Serial,int32 Index);
    bool RerollForTest(const FString& UserId,int32 Serial);
    bool TickForTest(double RealNow){return TickOpen(nullptr,RealNow);}

private:
    EProjectMatchMode Mode=EProjectMatchMode::Normal;
    FString MatchId;
    TMap<FString,FProjectRoguePlayer> Players;
    TArray<FString> Buffs;
    TArray<FString> FixedBuffs; // From the match ticket's affixes; set again on every floor.
    int32 Rounds=0;
    bool bPicking=false,bFloorDone=false,bFloorStartPending=false;
    double EndsAt=0,NextStatusAt=0;
    FRandomStream Rng;

    static bool InPlay(const FMultiplayTestServerPlayerRecord& Record);
    void Open(AMultiplayTestGameMode* GM,const TArray<const FMultiplayTestServerPlayerRecord*>& Records,double RealNow);
    bool TickOpen(AMultiplayTestGameMode* GM,double RealNow);
    void Close(AMultiplayTestGameMode* GM);
    void RollFor(FProjectRoguePlayer& Player,const TArray<FString>& Avoid);
    bool Choose(FProjectRoguePlayer& Player,int32 Index,bool bAuto);
    TArray<FString> TeammateBuffsOffered(const FProjectRoguePlayer& Except)const;
    int32 WaitingCount()const;
    void Send(AMultiplayTestGameMode* GM,const FProjectRoguePlayer& Player,double RealNow)const;
    void SendAll(AMultiplayTestGameMode* GM,double RealNow)const;
    AMultiplayTestPlayerController* FindController(AMultiplayTestGameMode* GM,const FString& UserId)const;
    FProjectRoguePlayer* PlayerOf(AMultiplayTestGameMode* GM,AMultiplayTestPlayerController* PC);
};

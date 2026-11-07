// File: Source/ProjectProject01/Public/ProjectCustomMatchRules.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ProjectAffixes.h"
#include "Math/RandomStream.h"

// Custom game "playground" rules (Docs/CustomGamePlayground.md). Every field is required on the wire; the backend
// (Tools/ProjectProject01Backend/Game/CustomRoomRules.cs) and the game server apply the same checks.
namespace ProjectPlaygroundRules
{
    constexpr int32 MaxAffixes=6,MaxAffixesPerGroup=3;
    constexpr int32 HideRevealMin=20,HideRevealMax=120,HideRevealStep=5,HideRevealDefaultOn=40;
    constexpr int32 InfiniteLivesMinTimeLimit=300,InfectionMaxMannequins=8;
    // Phase 2 (Docs/CustomGamePlayground.md 12.1). 0 turns each off.
    constexpr int32 GazeLimitMin=3,GazeLimitMax=15,GazeLimitDefaultOn=8;
    constexpr int32 DarknessMax=3;                       // 0 off, 1 slight, 2 dark, 3 very dark (both sides).
    constexpr int32 SoundOnlyMin=4,SoundOnlyMax=15,SoundOnlyDefaultOn=8; // metres a body stays visible.
    enum class EEscapeMode:int32{Normal=0,Together=1,LastSavesAll=2,FirstOutWins=3};
    constexpr int32 EscapeModeMax=3;
    // A rule is accepted with a non-default value only once its game-server runtime exists. Each flag flips with the
    // phase-1 step that implements it; until then the rule must stay off everywhere.
    constexpr bool bAffixesLive=true,bItemCardsLive=true,bInfiniteLivesLive=true,bInfectionLive=true,bGhostsLive=true,bHideRevealLive=true;
    constexpr bool bGazeLimitLive=true,bDarknessLive=true,bSoundOnlyLive=true,bEscapeModeLive=true,bSpreadingFireLive=true;
    // Personal handicap (12.1): per player, every stat that does not decide how the match ends. Shown to everyone.
    constexpr int32 MaxHandicaps=4,HandicapDeathMax=2,HandicapSpeedMax=10,HandicapStaminaMax=20,HandicapGazeMax=10,HandicapBodySpeedMax=10,HandicapAttackRangeMax=15;
    constexpr bool bHandicapLive=true;
    // Phase 3 (12.2), shipped as recommended presets.
    constexpr int32 HideAndSeekSeconds=60;            // Bodies frozen and the mannequin screen dark while survivors hide.
    constexpr int32 WaveStartBodies=3,WaveMaxBodies=8,WaveEverySeconds=120,WaveSurviveSeconds=720;
    constexpr int32 VoteCards=3,VoteSeconds=15;
    constexpr bool bHideAndSeekLive=true,bWavesLive=true,bRuleVoteLive=true,bRoleSwapLive=true;
    constexpr int32 RoleSwapRounds=3; // Matches in one role-swap series; the backend picks the mannequin (RoleSwap.cs).
    // Phase 4 (Docs/CustomGamePlayground.md 15): two rules that bend "a body freezes while watched".
    constexpr int32 PairGazeMax=3,PairGazeDefaultOn=1;    // Marked bodies that freeze only while both survivors watch.
    constexpr float FlashlightBatterySeconds=30.f,FlashlightRechargePerSecond=1.f,FlashlightEmptyLockSeconds=3.f;
    constexpr float FlashlightHalfAngleDegrees=20.f,FlashlightRangeCm=1500.f; // The gaze only freezes inside the beam.
    constexpr bool bPairGazeLive=true,bFlashlightLive=true;
    // Escort (Docs/CustomGamePlayground.md 16): survivors walk an AI escapee to each floor's way out, one round per floor.
    constexpr int32 EscortWinsNeeded=2;                    // Best of three floors.
    constexpr float EscortSpeedScale=.6f;                  // The escapee walks at 60% of a survivor's walk.
    constexpr float EscortCompanionCm=800.f,EscortFearSightCm=1200.f,EscortFearHalfAngleDegrees=60.f;
    constexpr int32 EscortDegradeSpeedStep=5,EscortDegradeSpeedMax=20,EscortDegradeStaminaStep=10,EscortDegradeStaminaMax=40;
    constexpr float EscortHoldSeconds=5.f,EscortPingFollowSeconds=20.f,EscortArriveCm=200.f;
    constexpr bool bEscortLive=true;
    // Broken escalators (17): of several ways to the next floor, only 1..N work (N = 1 or 2, rolled per floor).
    constexpr int32 EscalatorWorkingMax=2,EscalatorWorkingDefaultOn=1;
    constexpr bool bEscalatorsLive=true;
    // Team handicap and per-floor rules (18).
    constexpr int32 FloorRulesMax=3;
    constexpr bool bTeamHandicapLive=true,bFloorRulesLive=true;
}

/**
 * "Hiding too long reveals you" (Docs/CustomGamePlayground.md 4.4), one survivor. Time counts only while the match
 * runs (the caller passes match-time steps) and only within HideRevealRadius of where the survivor settled.
 */
struct FPlaygroundHideTracker
{
    enum class EStep:uint8{None,Warn,Reveal};
    static constexpr float Radius=400.f;      // cm; moving further starts over.
    static constexpr float WarnBefore=10.f;   // s before the reveal the survivor is told.
    FVector Anchor=FVector::ZeroVector;float Stayed=0;bool bHasAnchor=false,bWarned=false;
    void Reset(){bHasAnchor=false;Stayed=0;bWarned=false;}
    EStep Step(const FVector& Here,float Seconds,int32 Limit)
    {
        if(Limit<=0||Seconds<=0.f)return EStep::None;
        if(!bHasAnchor||FVector::Dist2D(Anchor,Here)>Radius){Anchor=Here;bHasAnchor=true;Stayed=0;bWarned=false;return EStep::None;}
        Stayed+=Seconds;
        if(Stayed>=float(Limit)){Anchor=Here;Stayed=0;bWarned=false;return EStep::Reveal;}
        if(!bWarned&&Stayed>=float(Limit)-WarnBefore){bWarned=true;return EStep::Warn;}
        return EStep::None;
    }
};

/** One player's handicap. Survivor values apply when they play a survivor, body/attack values when they play the mannequin. */
struct FProjectPlaygroundHandicap
{
    FString UserId;
    int32 DeathCount=0,SpeedPct=0,StaminaPct=0,GazePct=0,BodySpeedPct=0,AttackRangePct=0;
    bool IsNeutral()const{return DeathCount==0&&SpeedPct==0&&StaminaPct==0&&GazePct==0&&BodySpeedPct==0&&AttackRangePct==0;}
    bool HasValidRanges()const
    {
        using namespace ProjectPlaygroundRules;
        return FMath::Abs(DeathCount)<=HandicapDeathMax&&FMath::Abs(SpeedPct)<=HandicapSpeedMax&&FMath::Abs(StaminaPct)<=HandicapStaminaMax
            &&FMath::Abs(GazePct)<=HandicapGazeMax&&FMath::Abs(BodySpeedPct)<=HandicapBodySpeedMax&&FMath::Abs(AttackRangePct)<=HandicapAttackRangeMax;
    }
    bool IsWellFormed()const
    {
        if(UserId.IsEmpty()||UserId.Len()>32)return false;
        for(const TCHAR C:UserId)if(!FChar::IsAlnum(C)&&C!=TEXT('-')&&C!=TEXT('_'))return false;
        return HasValidRanges();
    }
    /** Personal + team, each value kept inside the handicap range (Docs/CustomGamePlayground.md 18). */
    static FProjectPlaygroundHandicap Combine(const FProjectPlaygroundHandicap* Personal,const FProjectPlaygroundHandicap& Team)
    {
        using namespace ProjectPlaygroundRules;
        FProjectPlaygroundHandicap H;if(Personal)H.UserId=Personal->UserId;
        auto Sum=[](int32 A,int32 B,int32 Max){return FMath::Clamp(A+B,-Max,Max);};
        H.DeathCount=Sum(Personal?Personal->DeathCount:0,Team.DeathCount,HandicapDeathMax);H.SpeedPct=Sum(Personal?Personal->SpeedPct:0,Team.SpeedPct,HandicapSpeedMax);
        H.StaminaPct=Sum(Personal?Personal->StaminaPct:0,Team.StaminaPct,HandicapStaminaMax);H.GazePct=Sum(Personal?Personal->GazePct:0,Team.GazePct,HandicapGazeMax);
        H.BodySpeedPct=Sum(Personal?Personal->BodySpeedPct:0,Team.BodySpeedPct,HandicapBodySpeedMax);H.AttackRangePct=Sum(Personal?Personal->AttackRangePct:0,Team.AttackRangePct,HandicapAttackRangeMax);
        return H;
    }
    bool operator==(const FProjectPlaygroundHandicap& R)const
    {return UserId==R.UserId&&DeathCount==R.DeathCount&&SpeedPct==R.SpeedPct&&StaminaPct==R.StaminaPct&&GazePct==R.GazePct&&BodySpeedPct==R.BodySpeedPct&&AttackRangePct==R.AttackRangePct;}
    /** Short public text, e.g. "데스 +1 · 속도 -5%". */
    FString Summary()const
    {
        TArray<FString> Parts;
        auto Add=[&Parts](const TCHAR* Label,int32 V,bool bPct){if(V!=0)Parts.Add(FString::Printf(TEXT("%s %+d%s"),Label,V,bPct?TEXT("%"):TEXT("")));};
        Add(TEXT("데스"),DeathCount,false);Add(TEXT("속도"),SpeedPct,true);Add(TEXT("스태미나"),StaminaPct,true);Add(TEXT("시선"),GazePct,true);
        Add(TEXT("몸체"),BodySpeedPct,true);Add(TEXT("사거리"),AttackRangePct,true);
        return FString::Join(Parts,TEXT(" · "));
    }
};

struct FProjectCustomMatchRules
{
    bool bEnabled=false;int32 DeathCount=3,MannequinCount=10,TimeLimitSeconds=1800;
    TArray<FString> AffixIds;
    bool bItemCards=false,bInfiniteLives=false,bInfection=false,bGhosts=false;
    int32 HideRevealSeconds=0; // 0 = off.
    int32 GazeLimitSeconds=0,Darkness=0,SoundOnlyMeters=0,EscapeMode=0; // Phase 2; 0 = off / normal.
    bool bSpreadingFire=false;
    bool bHideAndSeek=false,bWaves=false,bRuleVote=false,bRoleSwap=false; // Phase 3.
    int32 PairGazeBodies=0; bool bFlashlight=false; // Phase 4.
    bool bEscort=false; // Escort mode (16).
    int32 EscalatorWorking=0; // Broken escalators (17): 0 off, else the most that work per floor.
    /** Team handicap (18): survivor fields apply to every survivor, body/attack fields to the mannequin side. No user id;
     *  unlike personal handicaps it belongs to the rules, so presets and rule codes keep it. */
    FProjectPlaygroundHandicap TeamHandicap;
    int32 FloorRules=0; // Per-floor rules (18): 0 off, else how many of the host's sight/environment rules each floor draws.
    TArray<FProjectPlaygroundHandicap> Handicaps; // Per player (never part of presets or rule codes).
    const FProjectPlaygroundHandicap* FindHandicap(const FString& UserId)const
    {if(!bEnabled||UserId.IsEmpty())return nullptr;return Handicaps.FindByPredicate([&UserId](const FProjectPlaygroundHandicap& H){return H.UserId==UserId;});}
    /** What applies to this player: their own handicap plus the team handicap (neutral with custom rules off). */
    FProjectPlaygroundHandicap EffectiveHandicap(const FString& UserId)const
    {return bEnabled?FProjectPlaygroundHandicap::Combine(FindHandicap(UserId),TeamHandicap):FProjectPlaygroundHandicap();}

    bool HasPlaygroundRules()const{return !AffixIds.IsEmpty()||bItemCards||bInfiniteLives||bInfection||bGhosts||HideRevealSeconds!=0||GazeLimitSeconds!=0||Darkness!=0||SoundOnlyMeters!=0||EscapeMode!=0||bSpreadingFire||!Handicaps.IsEmpty()||bHideAndSeek||bWaves||bRuleVote||bRoleSwap||PairGazeBodies!=0||bFlashlight||bEscort||EscalatorWorking!=0||!TeamHandicap.IsNeutral()||FloorRules!=0;}
    /** Ranges and combinations (Docs/CustomGamePlayground.md 2.1-2.2), regardless of which rules are implemented yet. */
    bool IsWellFormed()const
    {
        using namespace ProjectPlaygroundRules;
        if(DeathCount<1||DeathCount>20||MannequinCount<1||MannequinCount>10||TimeLimitSeconds<0||TimeLimitSeconds>7200||TimeLimitSeconds%60!=0)return false;
        if(!bEnabled&&HasPlaygroundRules())return false;
        if(HideRevealSeconds!=0&&(HideRevealSeconds<HideRevealMin||HideRevealSeconds>HideRevealMax||HideRevealSeconds%HideRevealStep!=0))return false;
        if(bInfiniteLives&&TimeLimitSeconds<InfiniteLivesMinTimeLimit)return false; // 0 (unlimited) is also too short here.
        if(bGhosts&&(bInfection||bInfiniteLives))return false;
        if(bInfection&&MannequinCount>InfectionMaxMannequins)return false;
        if(GazeLimitSeconds!=0&&(GazeLimitSeconds<GazeLimitMin||GazeLimitSeconds>GazeLimitMax))return false;
        if(Darkness<0||Darkness>DarknessMax||EscapeMode<0||EscapeMode>EscapeModeMax)return false;
        if(SoundOnlyMeters!=0&&(SoundOnlyMeters<SoundOnlyMin||SoundOnlyMeters>SoundOnlyMax))return false;
        if(bWaves&&TimeLimitSeconds!=WaveSurviveSeconds)return false; // Waves: survive exactly 12 minutes.
        if(PairGazeBodies<0||PairGazeBodies>PairGazeMax||PairGazeBodies>MannequinCount)return false;
        // Escort decides the winner by rounds and gives survivors endless respawns itself, so rules that decide the end
        // another way (or give lives) cannot ride with it.
        if(bEscort&&(bInfiniteLives||bInfection||bGhosts||bWaves||bHideAndSeek||EscapeMode!=0||bRoleSwap))return false;
        if(EscalatorWorking<0||EscalatorWorking>EscalatorWorkingMax)return false;
        if(!TeamHandicap.UserId.IsEmpty()||!TeamHandicap.HasValidRanges()||FloorRules<0||FloorRules>FloorRulesMax)return false;
        if(Handicaps.Num()>MaxHandicaps)return false;
        {TSet<FString> Seen;for(const auto& H:Handicaps){if(!H.IsWellFormed()||Seen.Contains(H.UserId))return false;Seen.Add(H.UserId);}}
        if(AffixIds.Num()>MaxAffixes)return false;
        TSet<FString> Seen;int32 Groups[3]{};
        for(const FString& Id:AffixIds)
        {
            const auto* Info=ProjectAffixes::Find(Id);
            if(!Info||!Info->bPlayable||Seen.Contains(Id)||++Groups[uint8(Info->Group)]>MaxAffixesPerGroup)return false;
            Seen.Add(Id);
        }
        return true;
    }
    /** Well formed and only uses rules the game server already runs. */
    bool IsValid()const
    {
        using namespace ProjectPlaygroundRules;
        return IsWellFormed()&&(bAffixesLive||AffixIds.IsEmpty())&&(bItemCardsLive||!bItemCards)&&(bInfiniteLivesLive||!bInfiniteLives)
            &&(bInfectionLive||!bInfection)&&(bGhostsLive||!bGhosts)&&(bHideRevealLive||HideRevealSeconds==0)
            &&(bGazeLimitLive||GazeLimitSeconds==0)&&(bDarknessLive||Darkness==0)&&(bSoundOnlyLive||SoundOnlyMeters==0)&&(bEscapeModeLive||EscapeMode==0)&&(bSpreadingFireLive||!bSpreadingFire)&&(bHandicapLive||Handicaps.IsEmpty())
            &&(bHideAndSeekLive||!bHideAndSeek)&&(bWavesLive||!bWaves)&&(bRuleVoteLive||!bRuleVote)&&(bRoleSwapLive||!bRoleSwap)
            &&(bPairGazeLive||PairGazeBodies==0)&&(bFlashlightLive||!bFlashlight)&&(bEscortLive||!bEscort)&&(bEscalatorsLive||EscalatorWorking==0)&&(bTeamHandicapLive||TeamHandicap.IsNeutral())&&(bFloorRulesLive||FloorRules==0);
    }
    bool operator==(const FProjectCustomMatchRules& R)const
    {
        return bEnabled==R.bEnabled&&DeathCount==R.DeathCount&&MannequinCount==R.MannequinCount&&TimeLimitSeconds==R.TimeLimitSeconds&&AffixIds==R.AffixIds
            &&bItemCards==R.bItemCards&&bInfiniteLives==R.bInfiniteLives&&bInfection==R.bInfection&&bGhosts==R.bGhosts&&HideRevealSeconds==R.HideRevealSeconds&&GazeLimitSeconds==R.GazeLimitSeconds&&Darkness==R.Darkness&&SoundOnlyMeters==R.SoundOnlyMeters&&EscapeMode==R.EscapeMode&&bSpreadingFire==R.bSpreadingFire&&Handicaps==R.Handicaps&&bHideAndSeek==R.bHideAndSeek&&bWaves==R.bWaves&&bRuleVote==R.bRuleVote&&bRoleSwap==R.bRoleSwap&&PairGazeBodies==R.PairGazeBodies&&bFlashlight==R.bFlashlight&&bEscort==R.bEscort&&EscalatorWorking==R.EscalatorWorking&&TeamHandicap==R.TeamHandicap&&FloorRules==R.FloorRules;
    }
    /** Strict and atomic: every field must be present and valid, otherwise Out is left unchanged. */
    static bool Parse(const TSharedPtr<FJsonObject>& Json,FProjectCustomMatchRules& Out)
    {
        if(!Json)return false;
        FProjectCustomMatchRules R;
        auto Whole=[&Json](const TCHAR* Key,int32& Value)
        {
            double D=0;
            if(!Json->TryGetNumberField(Key,D)||!FMath::IsFinite(D)||D!=FMath::FloorToDouble(D)||D<double(MIN_int32)||D>double(MAX_int32))return false;
            Value=int32(D);return true;
        };
        if(!Json->TryGetBoolField(TEXT("enabled"),R.bEnabled)||!Whole(TEXT("deathCount"),R.DeathCount)||!Whole(TEXT("mannequinCount"),R.MannequinCount)||!Whole(TEXT("timeLimitSeconds"),R.TimeLimitSeconds)
            ||!Json->TryGetBoolField(TEXT("itemCards"),R.bItemCards)||!Json->TryGetBoolField(TEXT("infiniteLives"),R.bInfiniteLives)||!Json->TryGetBoolField(TEXT("infection"),R.bInfection)
            ||!Json->TryGetBoolField(TEXT("ghosts"),R.bGhosts)||!Whole(TEXT("hideRevealSeconds"),R.HideRevealSeconds)
            ||!Whole(TEXT("gazeLimitSeconds"),R.GazeLimitSeconds)||!Whole(TEXT("darkness"),R.Darkness)||!Whole(TEXT("soundOnlyMeters"),R.SoundOnlyMeters)||!Whole(TEXT("escapeMode"),R.EscapeMode)
            ||!Json->TryGetBoolField(TEXT("spreadingFire"),R.bSpreadingFire)
            ||!Json->TryGetBoolField(TEXT("hideAndSeek"),R.bHideAndSeek)||!Json->TryGetBoolField(TEXT("waves"),R.bWaves)
            ||!Json->TryGetBoolField(TEXT("ruleVote"),R.bRuleVote)||!Json->TryGetBoolField(TEXT("roleSwap"),R.bRoleSwap)
            ||!Json->TryGetNumberField(TEXT("pairGazeBodies"),R.PairGazeBodies)||!Json->TryGetBoolField(TEXT("flashlight"),R.bFlashlight)
            ||!Json->TryGetBoolField(TEXT("escort"),R.bEscort)||!Json->TryGetNumberField(TEXT("escalatorWorking"),R.EscalatorWorking)
            ||!Json->TryGetNumberField(TEXT("floorRules"),R.FloorRules))return false;
        {
            const TSharedPtr<FJsonObject>* T=nullptr;if(!Json->TryGetObjectField(TEXT("teamHandicap"),T)||!T)return false;
            auto TeamWhole=[&T](const TCHAR* Key,int32& Out){double D=0;if(!(*T)->TryGetNumberField(Key,D)||!FMath::IsFinite(D)||D!=FMath::FloorToDouble(D)||FMath::Abs(D)>1000.0)return false;Out=int32(D);return true;};
            if(!TeamWhole(TEXT("deathCount"),R.TeamHandicap.DeathCount)||!TeamWhole(TEXT("speedPct"),R.TeamHandicap.SpeedPct)||!TeamWhole(TEXT("staminaPct"),R.TeamHandicap.StaminaPct)
                ||!TeamWhole(TEXT("gazePct"),R.TeamHandicap.GazePct)||!TeamWhole(TEXT("bodySpeedPct"),R.TeamHandicap.BodySpeedPct)||!TeamWhole(TEXT("attackRangePct"),R.TeamHandicap.AttackRangePct))return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* HandicapValues=nullptr;
        if(!Json->TryGetArrayField(TEXT("handicaps"),HandicapValues)||!HandicapValues||HandicapValues->Num()>ProjectPlaygroundRules::MaxHandicaps)return false;
        for(const auto& V:*HandicapValues)
        {
            const TSharedPtr<FJsonObject>* O=nullptr;FProjectPlaygroundHandicap H;
            if(!V.IsValid()||!V->TryGetObject(O)||!O||!(*O)->TryGetStringField(TEXT("userId"),H.UserId))return false;
            const TSharedPtr<FJsonObject> Obj=*O;
            auto WholeIn=[&Obj](const TCHAR* Key,int32& Out){double D=0;if(!Obj->TryGetNumberField(Key,D)||!FMath::IsFinite(D)||D!=FMath::FloorToDouble(D)||FMath::Abs(D)>1000.0)return false;Out=int32(D);return true;};
            if(!WholeIn(TEXT("deathCount"),H.DeathCount)||!WholeIn(TEXT("speedPct"),H.SpeedPct)||!WholeIn(TEXT("staminaPct"),H.StaminaPct)||!WholeIn(TEXT("gazePct"),H.GazePct)
                ||!WholeIn(TEXT("bodySpeedPct"),H.BodySpeedPct)||!WholeIn(TEXT("attackRangePct"),H.AttackRangePct))return false;
            R.Handicaps.Add(MoveTemp(H));
        }
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if(!Json->TryGetArrayField(TEXT("affixIds"),Values)||!Values||Values->Num()>ProjectPlaygroundRules::MaxAffixes)return false;
        for(const auto& V:*Values){FString Id;if(!V.IsValid()||V->Type!=EJson::String||!V->TryGetString(Id))return false;R.AffixIds.Add(MoveTemp(Id));}
        if(!R.IsValid())return false;
        Out=MoveTemp(R);return true;
    }
    TSharedRef<FJsonObject> ToJson()const
    {
        auto J=MakeShared<FJsonObject>();
        J->SetBoolField(TEXT("enabled"),bEnabled);J->SetNumberField(TEXT("deathCount"),DeathCount);J->SetNumberField(TEXT("mannequinCount"),MannequinCount);J->SetNumberField(TEXT("timeLimitSeconds"),TimeLimitSeconds);
        TArray<TSharedPtr<FJsonValue>> Values;for(const FString& Id:AffixIds)Values.Add(MakeShared<FJsonValueString>(Id));
        J->SetArrayField(TEXT("affixIds"),Values);J->SetBoolField(TEXT("itemCards"),bItemCards);J->SetBoolField(TEXT("infiniteLives"),bInfiniteLives);
        J->SetBoolField(TEXT("infection"),bInfection);J->SetBoolField(TEXT("ghosts"),bGhosts);J->SetNumberField(TEXT("hideRevealSeconds"),HideRevealSeconds);
        J->SetNumberField(TEXT("gazeLimitSeconds"),GazeLimitSeconds);J->SetNumberField(TEXT("darkness"),Darkness);J->SetNumberField(TEXT("soundOnlyMeters"),SoundOnlyMeters);
        J->SetNumberField(TEXT("escapeMode"),EscapeMode);J->SetBoolField(TEXT("spreadingFire"),bSpreadingFire);
        TArray<TSharedPtr<FJsonValue>> HandicapValues;
        for(const auto& H:Handicaps)
        {
            auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("userId"),H.UserId);O->SetNumberField(TEXT("deathCount"),H.DeathCount);O->SetNumberField(TEXT("speedPct"),H.SpeedPct);
            O->SetNumberField(TEXT("staminaPct"),H.StaminaPct);O->SetNumberField(TEXT("gazePct"),H.GazePct);O->SetNumberField(TEXT("bodySpeedPct"),H.BodySpeedPct);O->SetNumberField(TEXT("attackRangePct"),H.AttackRangePct);
            HandicapValues.Add(MakeShared<FJsonValueObject>(O));
        }
        J->SetArrayField(TEXT("handicaps"),HandicapValues);
        J->SetBoolField(TEXT("hideAndSeek"),bHideAndSeek);J->SetBoolField(TEXT("waves"),bWaves);J->SetBoolField(TEXT("ruleVote"),bRuleVote);J->SetBoolField(TEXT("roleSwap"),bRoleSwap);
        J->SetNumberField(TEXT("pairGazeBodies"),PairGazeBodies);J->SetBoolField(TEXT("flashlight"),bFlashlight);J->SetBoolField(TEXT("escort"),bEscort);J->SetNumberField(TEXT("escalatorWorking"),EscalatorWorking);
        {
            auto T=MakeShared<FJsonObject>();T->SetNumberField(TEXT("deathCount"),TeamHandicap.DeathCount);T->SetNumberField(TEXT("speedPct"),TeamHandicap.SpeedPct);
            T->SetNumberField(TEXT("staminaPct"),TeamHandicap.StaminaPct);T->SetNumberField(TEXT("gazePct"),TeamHandicap.GazePct);
            T->SetNumberField(TEXT("bodySpeedPct"),TeamHandicap.BodySpeedPct);T->SetNumberField(TEXT("attackRangePct"),TeamHandicap.AttackRangePct);
            J->SetObjectField(TEXT("teamHandicap"),T);J->SetNumberField(TEXT("floorRules"),FloorRules);
        }
        return J;
    }
};

// Rule-card vote (Docs/CustomGamePlayground.md 12.2): before the match the server shows three phase 1/2 rules that are
// not on yet, everyone (mannequin player too) votes for 15 s, ties are broken at random. Mix switches (affixes, item
// cards) are not cards: the ticket fixes them for the affix and item runtimes.
namespace ProjectPlaygroundVote
{
    struct FCard{const TCHAR* Id;const TCHAR* Name;const TCHAR* Explain;};
    inline const TArray<FCard>& Cards()
    {
        static const TArray<FCard> All={
            {TEXT("infiniteLives"),TEXT("무한 목숨"),TEXT("잡혀도 층 시작 지점에서 다시 시작합니다.")},
            {TEXT("infection"),TEXT("감염"),TEXT("탈락한 생존자가 마네킹 몸체 하나를 직접 조종합니다.")},
            {TEXT("ghosts"),TEXT("유령 생존자"),TEXT("탈락해도 유령으로 남아 핑과 귓속말로 돕습니다.")},
            {TEXT("hideReveal"),TEXT("숨으면 들킴"),TEXT("40초 넘게 한자리에 있으면 위치가 드러납니다.")},
            {TEXT("gazeLimit"),TEXT("바라보기 시간 제한"),TEXT("같은 몸체를 8초 넘게 보면 시선을 무시하고 움직입니다.")},
            {TEXT("darkness"),TEXT("어둠·연기"),TEXT("양쪽 화면이 어두워지고 20m 앞까지만 보입니다.")},
            {TEXT("soundOnly"),TEXT("소리만 들리는 마네킹"),TEXT("8m보다 먼 몸체는 보이지 않고 발소리만 들립니다.")},
            {TEXT("spreadingFire"),TEXT("번지는 불"),TEXT("2분 뒤 불이 나서 30초마다 번집니다.")},
            {TEXT("escapeTogether"),TEXT("함께 탈출"),TEXT("남은 생존자가 모두 출구에 모여야 탈출합니다.")},
            {TEXT("escapeLastSavesAll"),TEXT("마지막 한 명이 모두를 구한다"),TEXT("마지막 생존자가 탈출하면 탈락자도 함께 이깁니다.")},
            {TEXT("escapeFirstOut"),TEXT("먼저 나간 사람 승리"),TEXT("가장 먼저 탈출한 한 사람만 이깁니다.")},
            {TEXT("pairGaze"),TEXT("둘이 봐야 멈추는 몸체"),TEXT("표시된 몸체 하나는 두 사람이 함께 봐야 멈춥니다.")},
            {TEXT("flashlight"),TEXT("손전등 시선"),TEXT("손전등 빛 안에서만 몸체가 멈춥니다. 배터리 30초.")},
            {TEXT("escalators"),TEXT("고장 난 에스컬레이터"),TEXT("다음 층으로 가는 길 중 하나만 움직입니다. 찾아야 합니다.")}};
        return All;
    }
    inline const FCard* Find(const FString& Id){for(const FCard& C:Cards())if(Id==C.Id)return &C;return nullptr;}
    /** Turns the card's rule on. False when the id is unknown or the rule is already on. */
    inline bool Apply(FProjectCustomMatchRules& R,const FString& Id)
    {
        using namespace ProjectPlaygroundRules;
        const FProjectCustomMatchRules Before=R;
        auto Escape=[&R](EEscapeMode M){if(R.EscapeMode==0)R.EscapeMode=int32(M);};
        if(Id==TEXT("infiniteLives"))R.bInfiniteLives=true;
        else if(Id==TEXT("infection"))R.bInfection=true;
        else if(Id==TEXT("ghosts"))R.bGhosts=true;
        else if(Id==TEXT("hideReveal")){if(R.HideRevealSeconds==0)R.HideRevealSeconds=HideRevealDefaultOn;}
        else if(Id==TEXT("gazeLimit")){if(R.GazeLimitSeconds==0)R.GazeLimitSeconds=GazeLimitDefaultOn;}
        else if(Id==TEXT("darkness")){if(R.Darkness==0)R.Darkness=2;}
        else if(Id==TEXT("soundOnly")){if(R.SoundOnlyMeters==0)R.SoundOnlyMeters=SoundOnlyDefaultOn;}
        else if(Id==TEXT("spreadingFire"))R.bSpreadingFire=true;
        else if(Id==TEXT("escapeTogether"))Escape(EEscapeMode::Together);
        else if(Id==TEXT("escapeLastSavesAll"))Escape(EEscapeMode::LastSavesAll);
        else if(Id==TEXT("escapeFirstOut"))Escape(EEscapeMode::FirstOutWins);
        else if(Id==TEXT("pairGaze")){if(R.PairGazeBodies==0)R.PairGazeBodies=PairGazeDefaultOn;}
        else if(Id==TEXT("flashlight"))R.bFlashlight=true;
        else if(Id==TEXT("escalators")){if(R.EscalatorWorking==0)R.EscalatorWorking=EscalatorWorkingDefaultOn;}
        return !(R==Before);
    }
    /** Cards that would add a rule and keep the rules valid (live, allowed together). */
    inline TArray<FString> Candidates(const FProjectCustomMatchRules& R)
    {
        TArray<FString> Out;
        for(const FCard& C:Cards()){FProjectCustomMatchRules Copy=R;if(Apply(Copy,C.Id)&&Copy.IsValid())Out.Add(C.Id);}
        return Out;
    }
}

// Per-floor rules (Docs/CustomGamePlayground.md 18): the host's sight/environment rules are the pool; each floor keeps a
// random few of them (never the same set as the floor before when another set exists). Match-wide rules are never drawn.
namespace ProjectPlaygroundFloorRules
{
    inline const TArray<FString>& Ids()
    {static const TArray<FString> All={TEXT("gazeLimit"),TEXT("darkness"),TEXT("soundOnly"),TEXT("spreadingFire"),TEXT("flashlight"),TEXT("pairGaze"),TEXT("hideReveal")};return All;}
    inline bool IsOn(const FProjectCustomMatchRules& R,const FString& Id)
    {
        if(Id==TEXT("gazeLimit"))return R.GazeLimitSeconds>0;if(Id==TEXT("darkness"))return R.Darkness>0;if(Id==TEXT("soundOnly"))return R.SoundOnlyMeters>0;
        if(Id==TEXT("spreadingFire"))return R.bSpreadingFire;if(Id==TEXT("flashlight"))return R.bFlashlight;if(Id==TEXT("pairGaze"))return R.PairGazeBodies>0;
        if(Id==TEXT("hideReveal"))return R.HideRevealSeconds>0;return false;
    }
    inline void TurnOff(FProjectCustomMatchRules& R,const FString& Id)
    {
        if(Id==TEXT("gazeLimit"))R.GazeLimitSeconds=0;else if(Id==TEXT("darkness"))R.Darkness=0;else if(Id==TEXT("soundOnly"))R.SoundOnlyMeters=0;
        else if(Id==TEXT("spreadingFire"))R.bSpreadingFire=false;else if(Id==TEXT("flashlight"))R.bFlashlight=false;else if(Id==TEXT("pairGaze"))R.PairGazeBodies=0;
        else if(Id==TEXT("hideReveal"))R.HideRevealSeconds=0;
    }
    /** The pool: the drawable rules the host turned on. */
    inline TArray<FString> Pool(const FProjectCustomMatchRules& R){TArray<FString> Out;for(const FString& Id:Ids())if(IsOn(R,Id))Out.Add(Id);return Out;}
    /** A random set of Count from the pool, not equal to Previous while another set exists. */
    inline TArray<FString> Draw(const TArray<FString>& Pool,int32 Count,const TArray<FString>& Previous,FRandomStream& Rng)
    {
        if(Count<=0||Pool.Num()<=Count)return Pool;
        auto Same=[](TArray<FString> A,TArray<FString> B){A.Sort();B.Sort();return A==B;};
        TArray<FString> Out;
        for(int32 Try=0;Try<16;++Try)
        {
            TArray<FString> Bag=Pool;Out.Reset();
            while(Out.Num()<Count){const int32 I=Rng.RandRange(0,Bag.Num()-1);Out.Add(Bag[I]);Bag.RemoveAtSwap(I);}
            if(!Same(Out,Previous))break;
        }
        return Out;
    }
    /** Keeps only the drawn rules of the pool on (values as the host set them). */
    inline void Apply(FProjectCustomMatchRules& R,const TArray<FString>& Pool,const TArray<FString>& Drawn){for(const FString& Id:Pool)if(!Drawn.Contains(Id))TurnOff(R,Id);}
    inline FString Names(const TArray<FString>& Drawn)
    {TArray<FString> N;for(const FString& Id:Drawn)if(const auto* C=ProjectPlaygroundVote::Find(Id))N.Add(C->Name);return N.IsEmpty()?FString(TEXT("없음")):FString::Join(N,TEXT(", "));}
}

// Recommended presets (Docs/CustomGamePlayground.md 5). Read-only; loading one fills the editor and the host still
// applies it. A theme may come in tiers that only stack agreed rules. Presets whose rules are not live yet are hidden.
namespace ProjectPlaygroundPresets
{
    struct FPreset{FString Name;FString Summary;FProjectCustomMatchRules Rules;};
    inline TArray<FPreset> All()
    {
        auto Make=[](int32 Deaths,int32 Mannequins,int32 Minutes){FProjectCustomMatchRules R;R.bEnabled=true;R.DeathCount=Deaths;R.MannequinCount=Mannequins;R.TimeLimitSeconds=Minutes*60;return R;};
        TArray<FPreset> Out;
        {auto R=Make(3,3,10);R.bInfection=true;R.bInfiniteLives=true;Out.Add({TEXT("좀비 백화점 1"),TEXT("잡히면 마네킹 편이 됩니다. 10분 안에 탈출하세요."),R});}
        {auto R=Make(3,3,10);R.bInfection=true;R.bInfiniteLives=true;R.AffixIds={TEXT("B09"),TEXT("B10")};Out.Add({TEXT("좀비 백화점 2"),TEXT("1단계에 깨어나는 진열·사냥의 열기를 더했습니다."),R});}
        {auto R=Make(3,3,10);R.bInfection=true;R.bInfiniteLives=true;R.AffixIds={TEXT("B09"),TEXT("B10")};R.Darkness=2;Out.Add({TEXT("좀비 백화점 3"),TEXT("2단계에 어둠·연기를 더했습니다. 양쪽 모두 20m 앞까지만 보입니다."),R});}
        {auto R=Make(1,6,20);R.bGhosts=true;R.HideRevealSeconds=30;Out.Add({TEXT("유령 백화점"),TEXT("한 번에 탈락하지만 유령으로 남아 동료를 돕습니다. 30초 숨으면 들킵니다."),R});}
        {auto R=Make(3,6,20);R.HideRevealSeconds=20;R.AffixIds={TEXT("B05")};R.bItemCards=true;Out.Add({TEXT("폐점 순찰"),TEXT("20초 넘게 숨으면 들킵니다. 계속 움직여야 합니다. 아이템 카드."),R});}
        {auto R=Make(3,6,20);R.bItemCards=true;R.AffixIds={TEXT("C01"),TEXT("C05")};Out.Add({TEXT("카드 난장"),TEXT("아이템 카드와 깜짝 마주침·러시아워로 양쪽 모두 빨라집니다."),R});}
        {auto R=Make(3,6,15);R.bHideAndSeek=true;R.HideRevealSeconds=30;Out.Add({TEXT("숨바꼭질"),TEXT("60초 동안 숨고, 그 뒤에는 30초 넘게 한자리에 있으면 들킵니다."),R});}
        {auto R=Make(3,3,12);R.bWaves=true;Out.Add({TEXT("웨이브 생존"),TEXT("마네킹 3개로 시작해 2분마다 하나씩 늘어납니다. 12분을 버티면 승리."),R});}
        {auto R=Make(3,6,15);R.bRuleVote=true;Out.Add({TEXT("규칙 카드 투표"),TEXT("경기 전에 규칙 카드 3장 중 하나를 모두가 투표로 고릅니다."),R});}
        {auto R=Make(3,6,10);R.bRoleSwap=true;Out.Add({TEXT("역할 바꾸기"),TEXT("3판 동안 세 사람이 한 번씩 마네킹을 맡습니다."),R});}
        {auto R=Make(3,5,15);R.PairGazeBodies=2;Out.Add({TEXT("둘이서 봐야 해"),TEXT("표시된 몸체 2개는 두 사람이 함께 봐야 멈춥니다. 떨어지지 마세요."),R});}
        {auto R=Make(3,5,15);R.bFlashlight=true;R.Darkness=2;Out.Add({TEXT("정전된 백화점"),TEXT("어둠 속에서 손전등 빛 안의 몸체만 멈춥니다. 배터리를 아끼세요."),R});}
        {auto R=Make(3,4,15);R.SoundOnlyMeters=8;R.GazeLimitSeconds=8;Out.Add({TEXT("보이지 않는 사냥꾼"),TEXT("8m 안에 와야 보이고, 8초 넘게 쳐다보면 시선을 무시합니다. 발소리를 들으세요."),R});}
        return Out;
    }
    /** Only presets the game server can run now. */
    inline TArray<FPreset> Available(){TArray<FPreset> Out;for(auto& P:All())if(P.Rules.IsValid())Out.Add(MoveTemp(P));return Out;}
}

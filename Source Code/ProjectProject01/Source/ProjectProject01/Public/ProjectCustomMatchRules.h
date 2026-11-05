// File: Source/ProjectProject01/Public/ProjectCustomMatchRules.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ProjectAffixes.h"

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
    constexpr bool bGazeLimitLive=true,bDarknessLive=true,bSoundOnlyLive=true,bEscapeModeLive=false,bSpreadingFireLive=false;
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

struct FProjectCustomMatchRules
{
    bool bEnabled=false;int32 DeathCount=3,MannequinCount=10,TimeLimitSeconds=1800;
    TArray<FString> AffixIds;
    bool bItemCards=false,bInfiniteLives=false,bInfection=false,bGhosts=false;
    int32 HideRevealSeconds=0; // 0 = off.
    int32 GazeLimitSeconds=0,Darkness=0,SoundOnlyMeters=0,EscapeMode=0; // Phase 2; 0 = off / normal.
    bool bSpreadingFire=false;

    bool HasPlaygroundRules()const{return !AffixIds.IsEmpty()||bItemCards||bInfiniteLives||bInfection||bGhosts||HideRevealSeconds!=0||GazeLimitSeconds!=0||Darkness!=0||SoundOnlyMeters!=0||EscapeMode!=0||bSpreadingFire;}
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
            &&(bGazeLimitLive||GazeLimitSeconds==0)&&(bDarknessLive||Darkness==0)&&(bSoundOnlyLive||SoundOnlyMeters==0)&&(bEscapeModeLive||EscapeMode==0)&&(bSpreadingFireLive||!bSpreadingFire);
    }
    bool operator==(const FProjectCustomMatchRules& R)const
    {
        return bEnabled==R.bEnabled&&DeathCount==R.DeathCount&&MannequinCount==R.MannequinCount&&TimeLimitSeconds==R.TimeLimitSeconds&&AffixIds==R.AffixIds
            &&bItemCards==R.bItemCards&&bInfiniteLives==R.bInfiniteLives&&bInfection==R.bInfection&&bGhosts==R.bGhosts&&HideRevealSeconds==R.HideRevealSeconds&&GazeLimitSeconds==R.GazeLimitSeconds&&Darkness==R.Darkness&&SoundOnlyMeters==R.SoundOnlyMeters&&EscapeMode==R.EscapeMode&&bSpreadingFire==R.bSpreadingFire;
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
            ||!Json->TryGetBoolField(TEXT("spreadingFire"),R.bSpreadingFire))return false;
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
        return J;
    }
};

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
        return Out;
    }
    /** Only presets the game server can run now. */
    inline TArray<FPreset> Available(){TArray<FPreset> Out;for(auto& P:All())if(P.Rules.IsValid())Out.Add(MoveTemp(P));return Out;}
}

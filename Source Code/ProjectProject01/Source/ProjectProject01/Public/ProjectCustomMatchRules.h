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
    bool IsWellFormed()const
    {
        using namespace ProjectPlaygroundRules;
        if(UserId.IsEmpty()||UserId.Len()>32)return false;
        for(const TCHAR C:UserId)if(!FChar::IsAlnum(C)&&C!=TEXT('-')&&C!=TEXT('_'))return false;
        return FMath::Abs(DeathCount)<=HandicapDeathMax&&FMath::Abs(SpeedPct)<=HandicapSpeedMax&&FMath::Abs(StaminaPct)<=HandicapStaminaMax
            &&FMath::Abs(GazePct)<=HandicapGazeMax&&FMath::Abs(BodySpeedPct)<=HandicapBodySpeedMax&&FMath::Abs(AttackRangePct)<=HandicapAttackRangeMax;
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
    TArray<FProjectPlaygroundHandicap> Handicaps; // Per player (never part of presets or rule codes).
    const FProjectPlaygroundHandicap* FindHandicap(const FString& UserId)const
    {if(!bEnabled||UserId.IsEmpty())return nullptr;return Handicaps.FindByPredicate([&UserId](const FProjectPlaygroundHandicap& H){return H.UserId==UserId;});}

    bool HasPlaygroundRules()const{return !AffixIds.IsEmpty()||bItemCards||bInfiniteLives||bInfection||bGhosts||HideRevealSeconds!=0||GazeLimitSeconds!=0||Darkness!=0||SoundOnlyMeters!=0||EscapeMode!=0||bSpreadingFire||!Handicaps.IsEmpty()||bHideAndSeek||bWaves||bRuleVote||bRoleSwap;}
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
            &&(bHideAndSeekLive||!bHideAndSeek)&&(bWavesLive||!bWaves)&&(bRuleVoteLive||!bRuleVote)&&(bRoleSwapLive||!bRoleSwap);
    }
    bool operator==(const FProjectCustomMatchRules& R)const
    {
        return bEnabled==R.bEnabled&&DeathCount==R.DeathCount&&MannequinCount==R.MannequinCount&&TimeLimitSeconds==R.TimeLimitSeconds&&AffixIds==R.AffixIds
            &&bItemCards==R.bItemCards&&bInfiniteLives==R.bInfiniteLives&&bInfection==R.bInfection&&bGhosts==R.bGhosts&&HideRevealSeconds==R.HideRevealSeconds&&GazeLimitSeconds==R.GazeLimitSeconds&&Darkness==R.Darkness&&SoundOnlyMeters==R.SoundOnlyMeters&&EscapeMode==R.EscapeMode&&bSpreadingFire==R.bSpreadingFire&&Handicaps==R.Handicaps&&bHideAndSeek==R.bHideAndSeek&&bWaves==R.bWaves&&bRuleVote==R.bRuleVote&&bRoleSwap==R.bRoleSwap;
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
            ||!Json->TryGetBoolField(TEXT("ruleVote"),R.bRuleVote)||!Json->TryGetBoolField(TEXT("roleSwap"),R.bRoleSwap))return false;
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
            {TEXT("escapeFirstOut"),TEXT("먼저 나간 사람 승리"),TEXT("가장 먼저 탈출한 한 사람만 이깁니다.")}};
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
        return Out;
    }
    /** Only presets the game server can run now. */
    inline TArray<FPreset> Available(){TArray<FPreset> Out;for(auto& P:All())if(P.Rules.IsValid())Out.Add(MoveTemp(P));return Out;}
}

// File: Source/ProjectProject01/Public/ProjectAffixRuntime.h
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MultiplayMannequinTypes.h"
#include "ProjectAffixRuntime.generated.h"

class AMultiplayTestGameMode;
class AMannequinAICharacter;
class APlayerCharacter;

/**
 * Server-side rules of the match affixes (ProjectAffixes.h, Docs/멀티플레이_어픽스와_탐사_퀘스트_기획초안.md).
 * Runs only while the match is in progress and only for the affixes the match ticket carried. Every effect shows a
 * cue (a glow on the body or survivor, a short light at a place, or a notice for floor-wide rules), and every speed
 * change goes through one place: the strongest slow (at most 30%) and the strongest boost (at most 25%).
 */
UCLASS()
class PROJECTPROJECT01_API UProjectAffixRuntime : public UObject
{
    GENERATED_BODY()
public:
    void Tick(AMultiplayTestGameMode* Mode,float DeltaSeconds);
    void Reset();

    // Hooks from the game mode and characters (all server side).
    /** Who looks at this body now. Returns whether it stays frozen (A09 keeps a doubly watched body a moment longer). */
    bool OnWatched(AMannequinAICharacter* Body,const TArray<APlayerCharacter*>& Watchers,bool bObserved);
    void OnCatch(AMannequinAICharacter* Catcher);
    void OnManualAttack(AMannequinAICharacter* Body,bool bHit);
    void OnCommand(AMannequinAICharacter* Body,EMultiplayMannequinCommand Command);
    void OnSurvivorPing(const FVector& Location);
    bool CanManualAttack(const AMannequinAICharacter* Body)const;
    bool CanPossess(const AMannequinAICharacter* Body)const;
    bool CanCommand(const AMannequinAICharacter* Body)const;
    bool IsSprintBlocked()const{return bShift;}
    /** C10: bots hear footsteps over this fraction of the usual range while the broadcast plays. */
    float HearingScale()const{return bBroadcast?BroadcastHearing:1.f;}
    /** C02: bot attack intervals stretch by this. */
    float BotAttackIntervalScale()const;
    bool IsRushActive()const{return bRush;}

    static constexpr float MaxSlow=.3f;
    static constexpr float MaxBoost=.25f;
    static float SpeedScale(float StrongestSlow,float StrongestBoost)
    {return (1.f-FMath::Clamp(StrongestSlow,0.f,MaxSlow))*(1.f+FMath::Clamp(StrongestBoost,0.f,MaxBoost));}

    // A02 추격 과열
    static constexpr float HeatSeconds=8.f;
    static constexpr float OverheatSlowSeconds=2.f;
    static constexpr float OverheatSlow=.3f;
    // B09 깨어나는 진열
    static constexpr float WakeRadius=600.f;
    static constexpr float WakeTellSeconds=1.f;
    static constexpr float WakeCooldownSeconds=10.f;
    // C05 러시아워: every RushPeriod seconds (first one at RushPeriod), a RushLength rush after a RushWarning notice.
    static constexpr double RushPeriod=60.0;
    static constexpr double RushLength=15.0;
    static constexpr double RushWarning=3.0;
    static constexpr float RushBoost=.12f;
    static constexpr float RushStaminaDrain=.5f;
    // C04 교대 시간, C08 진동, C10 대피 방송, C06 마감 시간: periods and lengths (seconds).
    static constexpr double ShiftPeriod=90.0;
    static constexpr double ShiftLength=10.0;
    static constexpr double QuakePeriod=45.0;
    static constexpr double QuakeWarning=2.0;
    static constexpr double QuakeLength=1.5;
    static constexpr double BroadcastPeriod=90.0;
    static constexpr double BroadcastOffset=45.0;
    static constexpr double BroadcastLength=10.0;
    static constexpr float BroadcastHearing=.4f;
    static constexpr double ClosingAt=360.0;
    static constexpr double ClosingStage=30.0;

private:
    struct FBody
    {
        float Heat=0;double SlowUntil=-1,SlowAmount=0,WakeAt=-1,WakeCooldownUntil=-1;
        double FlashUntil=-1;FLinearColor Flash=FLinearColor::Black;
        double ClusterSince=-1,FrozenSince=-1,LastSeen=-1,DoubleUntil=-1,BoostUntil=-1;float BoostAmount=0;
        double AttackBlockedUntil=-1;int32 Watchers=0;bool bHandedOff=false,bLocked=false;
    };
    struct FSurvivor
    {
        TWeakObjectPtr<AMannequinAICharacter> WatchTarget;double WatchSince=-1;
        FVector Anchor=FVector::ZeroVector;double AnchorSince=-1,ApartSince=-1,StaminaFreeUntil=-1,RecoveryDelayUntil=-1,NextPulse=-1,FlashUntil=-1;
        float LastStamina=-1;bool bWatching=false;
    };
    TMap<TWeakObjectPtr<AMannequinAICharacter>,FBody> Bodies;
    TMap<TWeakObjectPtr<APlayerCharacter>,FSurvivor> Survivors;
    TMap<TPair<TWeakObjectPtr<APlayerCharacter>,TWeakObjectPtr<AMannequinAICharacter>>,double> LastMet; // C01
    TMap<TWeakObjectPtr<class AMultiplayTestPlayerController>,TWeakObjectPtr<AMannequinAICharacter>> Viewed; // A01
    TWeakObjectPtr<AMultiplayTestGameMode> Owner;
    double Now=0,HuntUntil=-1;
    bool bRush=false,bShift=false,bQuake=false,bBroadcast=false,bLockedPicked=false;int32 ClosingStageShown=-1;
    TMap<FName,int32> Announced; // Which cycle each periodic notice last went out for.
    void Notify(const FString& Message,float Hold=2.f)const;
    void Pulse(const FVector& Location,const FLinearColor& Color,float Seconds,float Radius)const;
    bool Has(const TCHAR* Id)const;
    bool Cycle(FName Key,double Match,double Period,double Offset,double Length,double Warning,const FString& WarnText,const FString& StartText,float StartHold);
};

// File: Source/ProjectProject01/Private/ProjectAffixRuntime.cpp
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#include "ProjectAffixRuntime.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"
#include "ProjectAffixes.h"
#include "ProjectProject01LevelTransitionTrigger.h"
#include "Engine/TriggerBox.h"
#include "EngineUtils.h"

namespace
{
    const FLinearColor Ember(1.f,.18f,.05f),Spark(.55f,.85f,1.f),White(.92f,.95f,1.f),Violet(.55f,.35f,.9f),Amber(1.f,.62f,.12f),
        Gold(1.f,.85f,.35f),Dust(.75f,.62f,.42f),Alarm(1.f,.08f,.06f),Grey(.6f,.6f,.62f),Crack(1.f,.35f,.1f);
}

bool UProjectAffixRuntime::Has(const TCHAR* Id)const{const auto* Mode=Owner.Get();return Mode&&Mode->HasAffixRule(Id);}

void UProjectAffixRuntime::Reset()
{
    for(auto& Pair:Bodies)if(auto* Body=Pair.Key.Get()){Body->SetAffixSpeedScale(1.f);Body->SetAffixCue(FLinearColor::Black,0.f);Body->SetAffixHeat(0.f);Body->SetAffixBeam(false,0.f);}
    for(auto& Pair:Survivors)if(auto* Survivor=Pair.Key.Get())
    {Survivor->SetAffixStaminaDrainScale(1.f);Survivor->SetAffixStaminaRecoveryScale(1.f);Survivor->SetAffixSpeedScale(1.f);Survivor->SetAffixSprintBlocked(false);Survivor->SetAffixCue(FLinearColor::Black,0.f);}
    Bodies.Reset();Survivors.Reset();LastMet.Reset();Viewed.Reset();Announced.Reset();
    bRush=bShift=bQuake=bBroadcast=bLockedPicked=false;ClosingStageShown=-1;HuntUntil=-1;
    LastCatchAt=FeedCommandBlockUntil=BatonLockUntil=FeedUntil=BatonUntil=-1;BatonLast.Reset();FeedBody.Reset();ExitWatchCooldown.Reset();bLastStandNotified=false;
}

void UProjectAffixRuntime::Notify(const FString& Message,float Hold)const
{
    if(const auto* Mode=Owner.Get())for(const auto& Pair:Mode->MatchPlayerRecords)
        if(auto* Controller=Pair.Key.Get();Controller&&!Controller->IsServerControlledBot())Controller->ClientShowAffixNotice(Message,Hold);
}

void UProjectAffixRuntime::Pulse(const FVector& Location,const FLinearColor& Color,float Seconds,float Radius)const
{
    if(const auto* Mode=Owner.Get())for(const auto& Pair:Mode->MatchPlayerRecords)
        if(auto* Controller=Pair.Key.Get();Controller&&!Controller->IsServerControlledBot())Controller->ClientAffixPulse(Location,Color.ToFColor(true),Seconds,Radius);
}

// A periodic floor-wide rule: a warning notice, then Length seconds on. Returns whether it is on now.
bool UProjectAffixRuntime::Cycle(FName Key,double Match,double Period,double Offset,double Length,double Warning,const FString& WarnText,const FString& StartText,float StartHold)
{
    const double T=Match-Offset;if(T<0)return false;
    const int32 Index=int32(T/Period);const double Into=T-Index*Period;
    const bool On=Index>=1&&Into<Length;
    int32& Last=Announced.FindOrAdd(Key,-1);
    const int32 Next=int32((T+Warning)/Period);
    if(Warning>0&&Next>=1&&Next*2>Last&&T>=Next*Period-Warning&&T<Next*Period){Last=Next*2;Notify(WarnText);}
    if(On&&Index*2+1>Last){Last=Index*2+1;Notify(StartText,StartHold);}
    return On;
}

bool UProjectAffixRuntime::OnWatched(AMannequinAICharacter* Body,const TArray<APlayerCharacter*>& Watchers,bool bObserved)
{
    auto* Mode=Owner.Get();if(!Mode||!Body||!Mode->AreAffixRulesActive())return bObserved;
    FBody& State=Bodies.FindOrAdd(Body);State.Watchers=Watchers.Num();
    if(bObserved)State.LastSeen=Now;
    for(APlayerCharacter* Survivor:Watchers)
    {
        FSurvivor& S=Survivors.FindOrAdd(Survivor);S.bWatching=true;
        if(S.WatchTarget.Get()!=Body){S.WatchTarget=Body;S.WatchSince=Now;}
        // C01: the first look in a while; whoever had the other in view first gets the burst.
        if(Has(TEXT("C01")))
        {
            double& Met=LastMet.FindOrAdd({Survivor,Body},-100.0);
            if(Now-Met>10.0)
            {
                const bool BodyFirst=Body->GetMultiplayActivity()==EMultiplayMannequinActivity::Chasing;
                if(BodyFirst){State.BoostUntil=Now+2.0;State.BoostAmount=FMath::Max(State.BoostAmount,.2f);State.FlashUntil=Now+.5;State.Flash=Gold;}
                else{S.StaminaFreeUntil=Now+2.0;S.FlashUntil=Now+.5;}
            }
            Met=Now;
        }
    }
    // A09: two survivors on one body keep it frozen a second after both look away.
    if(Has(TEXT("A09"))&&Watchers.Num()>=2)State.DoubleUntil=Now+1.0;
    // Item roguelike S07: a body held by a taped survivor's gaze stays frozen half a second after release.
    State.bTapeWatched=false;
    if(bObserved)for(const APlayerCharacter* Survivor:Watchers)if(Item(Survivor,TEXT("S07"))){State.TapeUntil=Now+.5;State.bTapeWatched=true;break;}
    return bObserved||(Has(TEXT("A09"))&&Now<State.DoubleUntil)||Now<State.TapeUntil;
}

void UProjectAffixRuntime::OnCatch(AMannequinAICharacter* Catcher)
{
    if(!Owner.IsValid()||!Owner->AreAffixRulesActive())return;
    if(Catcher&&Has(TEXT("A05"))){FBody& State=Bodies.FindOrAdd(Catcher);State.SlowUntil=Now+3.0;State.SlowAmount=.3f;State.FlashUntil=Now+3.0;State.Flash=Spark;}
    if(Has(TEXT("B10"))){HuntUntil=Now+10.0;Notify(TEXT("사냥의 열기 · 10초"));}
    LastCatchAt=Now; // M09 starts over.
    // M10: the catching body feeds: faster for 5 s, but no orders meanwhile.
    if(Catcher&&MannequinItem(TEXT("M10"))){FeedBody=Catcher;FeedUntil=Now+5.0;FeedCommandBlockUntil=Now+5.0;}
}

void UProjectAffixRuntime::OnManualAttack(AMannequinAICharacter* Body,bool bHit)
{
    if(!Body||!Owner.IsValid()||!Owner->AreAffixRulesActive())return;
    FBody& State=Bodies.FindOrAdd(Body);
    if(Has(TEXT("C02")))State.AttackBlockedUntil=FMath::Max(State.AttackBlockedUntil,Now+1.0);
    // A04: a missed lunge staggers the body (visible) and costs a recovery before the next attack.
    if(!bHit&&Has(TEXT("A04"))){State.AttackBlockedUntil=FMath::Max(State.AttackBlockedUntil,Now+1.5);State.SlowUntil=Now+.6;State.SlowAmount=.3f;State.FlashUntil=Now+.6;State.Flash=Amber;}
    // Item roguelike M04: the long reach shows on the arm and costs half a second more before the next swing.
    if(MannequinItem(TEXT("M04"))){State.AttackBlockedUntil=FMath::Max(State.AttackBlockedUntil,Now+.5);if(Now>=State.FlashUntil){State.FlashUntil=Now+.3;State.Flash=Amber;}}
}

void UProjectAffixRuntime::OnCommand(AMannequinAICharacter* Body,EMultiplayMannequinCommand Command)
{
    if(!Body||!Has(TEXT("A07"))||Command==EMultiplayMannequinCommand::HoldPosition)return;
    FBody& State=Bodies.FindOrAdd(Body);State.FlashUntil=Now+.35;State.Flash=White;
}

void UProjectAffixRuntime::OnSurvivorPing(const FVector&)
{
    // C07 is delivered by the game mode (the marker reaches the mannequin player and the mannequin bots hear it).
}

bool UProjectAffixRuntime::CanManualAttack(const AMannequinAICharacter* Body)const
{const FBody* State=Body?Bodies.Find(Body):nullptr;return !State||Now>=State->AttackBlockedUntil;}
bool UProjectAffixRuntime::CanPossess(const AMannequinAICharacter* Body)const
{const FBody* State=Body?Bodies.Find(Body):nullptr;return !bShift&&!(State&&State->bLocked)&&Now>=BatonLockUntil;} // M03 wait
bool UProjectAffixRuntime::CanCommand(const AMannequinAICharacter* Body)const
{const FBody* State=Body?Bodies.Find(Body):nullptr;return !(State&&State->bLocked)&&Now>=FeedCommandBlockUntil;} // M10 feeding
float UProjectAffixRuntime::BotAttackIntervalScale()const{return Has(TEXT("C02"))?1.3f:1.f;}

void UProjectAffixRuntime::Tick(AMultiplayTestGameMode* Mode,float DeltaSeconds)
{
    if(!Mode||DeltaSeconds<=0.f)return;
    Owner=Mode;
    if(!Mode->AreAffixRulesActive())return;
    UWorld* World=Mode->GetWorld();Now=World->GetTimeSeconds();const double Match=Mode->GetElapsedMatchSeconds();
    RogueTickStart(Mode);

    // Floor-wide rules (C group): warned on screen, then on for a fixed time.
    bRush=Has(TEXT("C05"))&&Cycle(TEXT("C05"),Match,RushPeriod,0,RushLength,RushWarning,TEXT("러시아워 3초 전"),TEXT("러시아워 시작 · 15초"),2.f);
    bShift=Has(TEXT("C04"))&&Cycle(TEXT("C04"),Match,ShiftPeriod,0,ShiftLength,3.0,TEXT("교대 시간 3초 전 · 몸체 전환과 달리기가 막힙니다"),TEXT("교대 시간 · 10초"),ShiftLength);
    bQuake=Has(TEXT("C08"))&&Cycle(TEXT("C08"),Match,QuakePeriod,0,QuakeLength,QuakeWarning,TEXT("진동 2초 전"),TEXT("진동"),1.5f);
    const bool WasBroadcast=bBroadcast;
    bBroadcast=Has(TEXT("C10"))&&Cycle(TEXT("C10"),Match,BroadcastPeriod,BroadcastOffset,BroadcastLength,0,FString(),TEXT("대피 방송 중 · 발소리가 잘 들리지 않습니다"),BroadcastLength);
    if(bBroadcast&&!WasBroadcast)for(const auto& Pair:Mode->MatchPlayerRecords)if(auto* C=Pair.Key.Get();C&&!C->IsServerControlledBot())C->ClientSetAffixMuffle(BroadcastLength);
    // C06: a minute's countdown, then every 30 s one more stage (max 4): bodies faster, survivors recover faster.
    int32 Closing=0;
    if(Has(TEXT("C06")))
    {
        if(Match>=ClosingAt-60.0&&ClosingStageShown<0){ClosingStageShown=0;Notify(TEXT("마감 60초 전"),3.f);}
        if(Match>=ClosingAt)Closing=FMath::Min(4,1+int32((Match-ClosingAt)/ClosingStage));
        if(Closing>ClosingStageShown){ClosingStageShown=Closing;Notify(FString::Printf(TEXT("마감 %d단계"),Closing),2.f);}
    }

    // C09: two bodies crack at the start; nobody can steer them, and they roam faster on their own.
    if(Has(TEXT("C09"))&&!bLockedPicked)
    {
        TArray<AMannequinAICharacter*> Pool;for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(IsValid(*It)&&!It->IsHidden()&&!It->IsManualControlEnabled())Pool.Add(*It);
        if(Pool.Num()>=4)
        {
            bLockedPicked=true;
            for(int32 I=0;I<2;++I){const int32 Pick=FMath::RandRange(I,Pool.Num()-1);Pool.Swap(I,Pick);Mode->AssignMannequinCommand(Pool[I],EMultiplayMannequinCommand::Patrol);Bodies.FindOrAdd(Pool[I]).bLocked=true;}
        }
    }

    // A01: a body taken over by a mannequin player sparks.
    if(Has(TEXT("A01")))for(const auto& Pair:Mode->MatchPlayerRecords)
        if(auto* C=Pair.Key.Get();C&&Pair.Value.Role==TEXT("Mannequin"))
        {
            AMannequinAICharacter* Body=C->GetViewedMannequin();auto& Last=Viewed.FindOrAdd(C);
            if(Body&&Last.Get()!=Body&&Last.IsValid()){FBody& State=Bodies.FindOrAdd(Body);State.FlashUntil=Now+.6;State.Flash=Spark;}
            Last=Body;
        }

    // Survivors.
    TArray<APlayerCharacter*,TInlineAllocator<4>> Active,Sprinters;
    for(TActorIterator<APlayerCharacter> It(World);It;++It)if(IsValid(*It)&&!It->IsGameOver())Active.Add(*It);
    TArray<FVector,TInlineAllocator<8>> Exits;
    if(Has(TEXT("B04")))
    {
        for(TActorIterator<ATriggerBox> It(World);It;++It)if(It->ActorHasTag(Mode->EscapeTriggerActorTag))Exits.Add(It->GetActorLocation());
        for(TActorIterator<AProjectProject01LevelTransitionTrigger> It(World);It;++It)Exits.Add(It->GetActorLocation());
    }
    for(APlayerCharacter* Survivor:Active)
    {
        FSurvivor& S=Survivors.FindOrAdd(Survivor);
        const FVector Here=Survivor->GetActorLocation();const bool Sprinting=Survivor->IsSprintingForBreathing();if(Sprinting)Sprinters.Add(Survivor);
        const bool Still=Survivor->GetVelocity().Size2D()<60.f;
        float Drain=1.f,Recovery=1.f,Slow=0.f,Boost=0.f;FLinearColor Cue=FLinearColor::Black;float CueStrength=0.f;
        if(!S.bWatching){S.WatchTarget.Reset();S.WatchSince=-1;}
        if(bRush)Drain*=RushStaminaDrain;
        if(Now<S.StaminaFreeUntil)Drain=0.f; // C01
        if(Has(TEXT("C02")))Recovery*=.7f;
        if(Closing>0)Recovery*=1.f+.15f*Closing;
        // A10: holding a body with the eyes while standing still: breath shows, stamina comes back fast.
        if(Has(TEXT("A10"))&&S.bWatching&&Still){Recovery*=2.f;Cue=White;CueStrength=.5f;}
        // B01: running dry stalls recovery for 2 s, shown as a grey breath.
        if(Has(TEXT("B01")))
        {
            const float Stamina=Survivor->GetCurrentStamina();
            if(Stamina<=.01f&&S.LastStamina>.01f)S.RecoveryDelayUntil=Now+2.0;
            S.LastStamina=Stamina;
            if(Now<S.RecoveryDelayUntil){Recovery=0.f;Cue=Grey;CueStrength=.6f;}
        }
        // B07: a long stare (5 s on one body) lights the starer's face.
        if(Has(TEXT("B07"))&&S.bWatching&&S.WatchSince>=0&&Now-S.WatchSince>5.0){Cue=White;CueStrength=1.f;}
        // B03: apart from an active partner for 20 s: a blinking red light.
        if(Has(TEXT("B03")))
        {
            bool Apart=false;for(APlayerCharacter* Other:Active)if(Other!=Survivor)Apart|=FVector::Dist2D(Other->GetActorLocation(),Here)>1200.f;
            const bool Partnered=Active.Num()>=2;
            if(Partnered&&Apart){if(S.ApartSince<0)S.ApartSince=Now;}else S.ApartSince=-1;
            if(S.ApartSince>=0&&Now-S.ApartSince>20.0&&FMath::Fmod(Now,1.0)<.5){Cue=Alarm;CueStrength=.8f;}
        }
        // B05: staying put 10 s: dust starts to fall from above.
        if(Has(TEXT("B05")))
        {
            if(S.AnchorSince<0||FVector::Dist2D(S.Anchor,Here)>200.f){S.Anchor=Here;S.AnchorSince=Now;}
            if(Now-S.AnchorSince>10.0&&Now>=S.NextPulse){S.NextPulse=Now+.7;Pulse(Here+FVector(0,0,220),Dust,.6f,160.f);}
        }
        // B04: near an exit, the exit sign flashes.
        if(!Exits.IsEmpty()&&Now>=S.NextPulse)for(const FVector& Exit:Exits)if(FVector::Dist2D(Exit,Here)<500.f){S.NextPulse=Now+.4;Pulse(Exit+FVector(0,0,180),Alarm,.3f,260.f);break;}
        if(Now<S.FlashUntil){Cue=Gold;CueStrength=1.f;} // C01 startle
        RogueSurvivor(Mode,Survivor,S,Active,Drain,Recovery,Slow,Boost,Cue,CueStrength); // Item roguelike items.
        if(bQuake)Slow=FMath::Max(Slow,.3f);
        Survivor->SetAffixStaminaDrainScale(Drain);Survivor->SetAffixStaminaRecoveryScale(Recovery);
        Survivor->SetAffixSpeedScale(SpeedScale(Slow,Boost));Survivor->SetAffixSprintBlocked(bShift);
        Survivor->SetAffixCue(Cue,CueStrength);
        S.bWatching=false; // Refilled by the next vision pass.
    }
    // B02: two survivors running side by side kick up dust above the shelves.
    if(Has(TEXT("B02")))for(int32 I=0;I<Sprinters.Num();++I)for(int32 J=I+1;J<Sprinters.Num();++J)
        if(FVector::Dist2D(Sprinters[I]->GetActorLocation(),Sprinters[J]->GetActorLocation())<200.f)
        {
            FSurvivor& S=Survivors.FindOrAdd(Sprinters[I]);
            if(Now>=S.NextPulse){S.NextPulse=Now+.5;Pulse((Sprinters[I]->GetActorLocation()+Sprinters[J]->GetActorLocation())*.5f+FVector(0,0,260),Dust,.6f,260.f);}
        }

    // Bodies.
    TArray<AMannequinAICharacter*,TInlineAllocator<10>> All;
    for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(IsValid(*It)&&!It->IsHidden())All.Add(*It);
    for(AMannequinAICharacter* Body:All)
    {
        FBody& State=Bodies.FindOrAdd(Body);
        float Slow=0.f,Boost=0.f;FLinearColor Cue=FLinearColor::Black;float CueStrength=0.f;
        const bool Frozen=Body->IsFrozenBySurvivorVision();
        if(Now<State.SlowUntil)Slow=FMath::Max<float>(Slow,State.SlowAmount);
        if(Now<State.BoostUntil)Boost=FMath::Max(Boost,State.BoostAmount);else State.BoostAmount=0;

        // A02 추격 과열
        if(Has(TEXT("A02")))
        {
            const bool Fast=Body->GetVelocity().Size2D()>.6f*Body->GetBaseWalkSpeed();
            if(Now>=State.SlowUntil)
            {
                State.Heat=FMath::Clamp(State.Heat+(Fast?DeltaSeconds:-DeltaSeconds),0.f,HeatSeconds);
                if(State.Heat>=HeatSeconds){State.SlowUntil=Now+OverheatSlowSeconds;State.SlowAmount=OverheatSlow;State.Heat=0.f;Slow=FMath::Max(Slow,OverheatSlow);}
            }
            const float Glow=Now<State.SlowUntil&&State.SlowAmount>=OverheatSlow?1.f:State.Heat/HeatSeconds;
            Body->SetAffixHeat(Glow);if(Glow>.15f&&Glow>CueStrength){Cue=Ember;CueStrength=Glow;}
        }
        // A03 군집 간섭
        if(Has(TEXT("A03")))
        {
            int32 Near=0;for(AMannequinAICharacter* Other:All)if(Other!=Body&&FVector::DistSquared(Other->GetActorLocation(),Body->GetActorLocation())<FMath::Square(300.f))++Near;
            if(Near>=2){if(State.ClusterSince<0)State.ClusterSince=Now;}else State.ClusterSince=-1;
            if(State.ClusterSince>=0&&Now-State.ClusterSince>2.0){Slow=FMath::Max(Slow,.25f);if(CueStrength<.8f){Cue=Violet;CueStrength=.8f;}}
        }
        // Frozen time: A06 lingering stiffness, B08 hand-off, B06 forgotten bodies.
        if(Frozen){if(State.FrozenSince<0){State.FrozenSince=Now;State.bHandedOff=false;}}
        else if(State.FrozenSince>=0)
        {
            if(Has(TEXT("A06"))&&Now-State.FrozenSince>=3.0){State.SlowUntil=Now+.6;State.SlowAmount=.3f;State.FlashUntil=Now+.6;State.Flash=White;}
            State.FrozenSince=-1;
        }
        if(Has(TEXT("A06"))&&Frozen&&Now-State.FrozenSince>=3.0&&CueStrength<.5f){Cue=White;CueStrength=.5f;}
        if(Has(TEXT("B08"))&&Frozen&&!State.bHandedOff&&Now-State.FrozenSince>3.0)
        {
            AMannequinAICharacter* Next=nullptr;float Best=FMath::Square(1500.f);
            for(AMannequinAICharacter* Other:All)if(Other!=Body&&!Other->IsFrozenBySurvivorVision()){const float D=FVector::DistSquared(Other->GetActorLocation(),Body->GetActorLocation());if(D<Best){Best=D;Next=Other;}}
            State.bHandedOff=true;
            if(Next){FBody& N=Bodies.FindOrAdd(Next);N.BoostUntil=Now+4.0;N.BoostAmount=FMath::Max(N.BoostAmount,.2f);N.FlashUntil=Now+4.0;N.Flash=Amber;}
        }
        if(Has(TEXT("B06"))&&!Frozen)
        {
            if(State.LastSeen<0)State.LastSeen=Now;
            if(Now-State.LastSeen>10.0){Boost=FMath::Max(Boost,.12f);if(CueStrength<.35f){Cue=Violet;CueStrength=.35f;}}
        }
        // B09 깨어나는 진열
        if(Has(TEXT("B09")))
        {
            const bool Held=Body->GetMultiplayCommand()==EMultiplayMannequinCommand::HoldPosition&&!Body->IsManualControlEnabled()&&!Frozen;
            if(!Held)State.WakeAt=-1;
            else if(State.WakeAt>=0)
            {
                if(Now>=State.WakeAt){Mode->AssignMannequinCommand(Body,EMultiplayMannequinCommand::Patrol);State.WakeAt=-1;State.WakeCooldownUntil=Now+WakeCooldownSeconds;}
                else{Cue=White;CueStrength=1.f;}
            }
            else if(Now>=State.WakeCooldownUntil)
                for(const APlayerCharacter* Survivor:Sprinters)
                    if(FVector::DistSquared2D(Survivor->GetActorLocation(),Body->GetActorLocation())<FMath::Square(WakeRadius))
                    {State.WakeAt=Now+WakeTellSeconds;Cue=White;CueStrength=1.f;break;}
        }
        // A09 doubled gaze glow.
        if(Has(TEXT("A09"))&&(State.Watchers>=2||Now<State.DoubleUntil)&&CueStrength<.6f){Cue=White;CueStrength=.6f;}
        if(Now<HuntUntil){Boost=FMath::Max(Boost,.15f);if(CueStrength<.9f){Cue=Ember;CueStrength=.9f;}} // B10
        if(bRush)Boost=FMath::Max(Boost,RushBoost);
        if(Closing>0)Boost=FMath::Max(Boost,.05f*Closing);
        if(State.bLocked){Boost=FMath::Max(Boost,.2f);if(CueStrength<.7f){Cue=Crack;CueStrength=.7f;}} // C09
        RogueBody(Mode,Body,State,Active,Slow,Boost,Cue,CueStrength); // Item roguelike items.
        if(bQuake)Slow=FMath::Max(Slow,.3f);
        if(Now<State.FlashUntil){Cue=State.Flash;CueStrength=1.f;}
        Body->SetAffixSpeedScale(SpeedScale(Slow,Boost));
        Body->SetAffixCue(Cue,CueStrength);
        Body->SetAffixBeam(Has(TEXT("A08"))&&!Frozen,Mode->MultiplayerDetectionRadius);
        State.Watchers=0;
    }
}

// File: Source/ProjectProject01/Private/ProjectAffixRuntimeItems.cpp
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
// Item roguelike items (ProjectItemRoguelike.h, Docs/ItemRoguelikeMode.md), run inside the affix runtime so every speed
// change still goes through its one strongest-slow / strongest-boost rule. Every item shows a cue to the other side (a
// glow on the body or survivor, a light at a place, a louder or quieter footstep) and has a weak spot. Server only.
#include "ProjectAffixRuntime.h"
#include "ProjectItemRoguelikeRuntime.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"
#include "ProjectProject01LevelTransitionTrigger.h"
#include "Engine/TriggerBox.h"
#include "EngineUtils.h"

namespace
{
    const FLinearColor Ember(1.f,.18f,.05f),Spark(.55f,.85f,1.f),White(.92f,.95f,1.f),Amber(1.f,.62f,.12f),Gold(1.f,.85f,.35f),
        Dust(.75f,.62f,.42f),Alarm(1.f,.08f,.06f),Grey(.6f,.6f,.62f),Radio(.35f,1.f,.45f),Water(.35f,.6f,1.f),Frost(.75f,.9f,1.f),Chalk(.95f,.95f,.9f),Rain(.45f,.7f,1.f);
    void Raise(FLinearColor& Cue,float& Strength,const FLinearColor& C,float S){if(S>Strength){Cue=C;Strength=S;}}
    FString Direction(const FVector& Forward,const FVector& To)
    {
        const FVector F=Forward.GetSafeNormal2D(),D=To.GetSafeNormal2D();
        const float Angle=FMath::RadiansToDegrees(FMath::Atan2(FVector::CrossProduct(F,D).Z,FVector::DotProduct(F,D)));
        const float A=FMath::Abs(Angle);
        if(A<30)return TEXT("앞쪽");if(A>150)return TEXT("뒤쪽");
        if(A<75)return Angle>0?TEXT("앞 오른쪽"):TEXT("앞 왼쪽");
        if(A<115)return Angle>0?TEXT("오른쪽"):TEXT("왼쪽");
        return Angle>0?TEXT("뒤 오른쪽"):TEXT("뒤 왼쪽");
    }
}

const UProjectItemRoguelikeRuntime* UProjectAffixRuntime::Rogue()const
{const auto* Mode=Owner.Get();return Mode&&Mode->IsItemRoguelikeMatch()?Mode->GetItemRoguelike():nullptr;}

bool UProjectAffixRuntime::Item(const APlayerCharacter* Survivor,const TCHAR* Id)const
{
    const auto* R=Rogue();const auto* Mode=Owner.Get();if(!R||!Survivor)return false;
    const FMultiplayTestServerPlayerRecord* Record=Mode->FindSurvivorRecord(Survivor);
    return Record&&R->HasItem(UProjectItemRoguelikeRuntime::KeyOf(*Record),Id);
}

bool UProjectAffixRuntime::MannequinItem(const TCHAR* Id)const
{
    const auto* R=Rogue();if(!R)return false;
    for(const auto& Pair:Owner->MatchPlayerRecords)
        if((ProjectItemRoguelike::IsMannequinRole(Pair.Value.Role)||Pair.Value.bInfected)&&R->HasItem(UProjectItemRoguelikeRuntime::KeyOf(Pair.Value),Id))return true; // Infected players are on the mannequin side.
    return false;
}

float UProjectAffixRuntime::GazeAngleScale(const APlayerCharacter* Survivor)const
{
    if(!Rogue()||!Survivor)return 1.f;
    return (Item(Survivor,TEXT("S13"))?.9f:1.f)*(Item(Survivor,TEXT("S15"))?1.1f:1.f);
}
float UProjectAffixRuntime::ManualAttackRangeScale()const{return MannequinItem(TEXT("M04"))?1.15f:1.f;}
bool UProjectAffixRuntime::MannequinSeesPings()const{return MannequinItem(TEXT("M11"));}

float UProjectAffixRuntime::BotHearingScale(const APawn* Listener,const AActor* Source)const
{
    if(!Rogue())return 1.f;
    if(const auto* Survivor=Cast<APlayerCharacter>(Source))
        return FMath::Clamp(Survivor->GetRogueFootstepScale()*(Cast<AMannequinAICharacter>(Listener)?Survivor->GetRogueMannequinHearingScale():1.f),.05f,2.f);
    if(const auto* Body=Cast<AMannequinAICharacter>(Source))return FMath::Clamp(Body->GetRogueFootstepScale(),.05f,2.f);
    return 1.f;
}

void UProjectAffixRuntime::NotifyTo(AMultiplayTestPlayerController* Controller,const FString& Message,float Hold)const
{if(Controller&&!Controller->IsServerControlledBot())Controller->ClientShowAffixNotice(Message,Hold);}
void UProjectAffixRuntime::NotifyRole(bool bMannequins,const FString& Message,float Hold)const
{
    if(const auto* Mode=Owner.Get())for(const auto& Pair:Mode->MatchPlayerRecords)
        if(ProjectItemRoguelike::IsMannequinRole(Pair.Value.Role)==bMannequins)NotifyTo(Pair.Key.Get(),Message,Hold);
}
void UProjectAffixRuntime::PulseTo(AMultiplayTestPlayerController* Controller,const FVector& Location,const FLinearColor& Color,float Seconds,float Radius)const
{if(Controller&&!Controller->IsServerControlledBot())Controller->ClientAffixPulse(Location,Color.ToFColor(true),Seconds,Radius);}
void UProjectAffixRuntime::PulseRole(bool bMannequins,const FVector& Location,const FLinearColor& Color,float Seconds,float Radius)const
{
    if(const auto* Mode=Owner.Get())for(const auto& Pair:Mode->MatchPlayerRecords)
        if(ProjectItemRoguelike::IsMannequinRole(Pair.Value.Role)==bMannequins)PulseTo(Pair.Key.Get(),Location,Color,Seconds,Radius);
}

void UProjectAffixRuntime::OnSurvivorPingFrom(AMultiplayTestPlayerController* Sender,const FVector& Location)
{
    // S12: a chalk mark at the ping stays for the floor (6 per survivor); everyone, the mannequin too, can see it.
    auto* Survivor=Sender?Cast<APlayerCharacter>(Sender->GetPawn()):nullptr;
    if(!Rogue()||!Survivor||!Item(Survivor,TEXT("S12")))return;
    FSurvivor& S=Survivors.FindOrAdd(Survivor);if(S.ChalkMarks>=6)return;
    ++S.ChalkMarks;FRogueMark& M=RogueMarks.AddDefaulted_GetRef();M.At=Location+FVector(0,0,25);M.Color=Chalk;M.Radius=110;M.Until=Now+3600;M.NextAt=Now;
}

void UProjectAffixRuntime::OnSurvivorHit(APlayerCharacter* Survivor,bool bEliminated)
{
    if(!Rogue()||!Survivor)return;
    const FVector Here=Survivor->GetActorLocation();
    // S08: the bell rings where the survivor stands; nearby bodies falter for 2 s and everyone sees the place.
    if(!bEliminated&&Item(Survivor,TEXT("S08")))
    {
        for(TActorIterator<AMannequinAICharacter> It(Owner->GetWorld());It;++It)if(IsValid(*It)&&FVector::DistSquared(It->GetActorLocation(),Here)<FMath::Square(600.f))
        {FBody& B=Bodies.FindOrAdd(*It);B.SlowUntil=Now+2.0;B.SlowAmount=MaxSlow;B.FlashUntil=Now+2.0;B.Flash=Gold;}
        Pulse(Here+FVector(0,0,120),Gold,2.f,600.f);NotifyRole(true,TEXT("방범 벨이 울렸습니다"),2.f);
    }
    // S14: a teammate is caught: 10 s without stamina cost for whoever carries the injection.
    for(TActorIterator<APlayerCharacter> It(Owner->GetWorld());It;++It)
        if(*It!=Survivor&&IsValid(*It)&&!It->IsGameOver()&&Item(*It,TEXT("S14")))Survivors.FindOrAdd(*It).AdrenalineUntil=Now+10.0;
}

bool UProjectAffixRuntime::TryShieldCatch(APlayerCharacter* Survivor,AMannequinAICharacter* Catcher)
{
    auto* Mode=Owner.Get();if(!Rogue()||!Survivor||!Mode||!Mode->IsAuthoritativeMatchInProgress()||!Item(Survivor,TEXT("S03")))return false;
    const FMultiplayTestServerPlayerRecord* Record=Mode->FindSurvivorRecord(Survivor);
    if(!Record||!Mode->GetItemRoguelike()->Spend(UProjectItemRoguelikeRuntime::KeyOf(*Record),TEXT("S03")))return false;
    Pulse(Survivor->GetActorLocation()+FVector(0,0,90),Spark,1.f,300.f);
    if(Catcher){FBody& B=Bodies.FindOrAdd(Catcher);B.FlashUntil=Now+.6;B.Flash=Spark;}
    NotifyRole(true,TEXT("방화복이 포획을 막았습니다"),2.f);
    NotifyTo(Cast<AMultiplayTestPlayerController>(Survivor->GetController()),TEXT("방화복 조각이 포획을 막았습니다"),2.f);
    return true;
}

void UProjectAffixRuntime::RogueTickStart(AMultiplayTestGameMode* Mode)
{
    UProjectItemRoguelikeRuntime* R=Mode&&Mode->IsItemRoguelikeMatch()?Mode->GetItemRoguelike():nullptr;if(!R)return;
    UWorld* World=Mode->GetWorld();
    if(RogueStartAt<0)RogueStartAt=Now;
    if(R->ConsumeFloorStart())
    {
        RogueExits.Reset();
        for(TActorIterator<ATriggerBox> It(World);It;++It)if(It->ActorHasTag(Mode->EscapeTriggerActorTag))RogueExits.Add(It->GetActorLocation());
        for(TActorIterator<AProjectProject01LevelTransitionTrigger> It(World);It;++It)RogueExits.Add(It->GetActorLocation());
        // S06: where the nearest exit is (for 10 s), and the mannequin sees that exit light up for 3 s.
        for(TActorIterator<APlayerCharacter> It(World);It;++It)if(IsValid(*It)&&!It->IsGameOver()&&Item(*It,TEXT("S06"))&&!RogueExits.IsEmpty())
        {
            const FVector Here=It->GetActorLocation();const FVector* Best=nullptr;
            for(const FVector& E:RogueExits)if(!Best||FVector::DistSquared(E,Here)<FVector::DistSquared(*Best,Here))Best=&E;
            auto* PC=Cast<AMultiplayTestPlayerController>(It->GetController());
            NotifyTo(PC,FString::Printf(TEXT("매장 안내도: 가까운 출구는 %s %.0fm"),*Direction(It->GetActorForwardVector(),*Best-Here),FVector::Dist(*Best,Here)/100.f),5.f);
            FRogueMark& M=RogueMarks.AddDefaulted_GetRef();M.At=*Best+FVector(0,0,180);M.Color=Gold;M.Radius=400;M.Until=Now+10;M.NextAt=Now;M.Only=PC;
            PulseRole(true,*Best+FVector(0,0,180),Alarm,3.f,400.f);
        }
        if(MannequinItem(TEXT("M11")))NotifyRole(false,TEXT("유리 눈: 핑 위치가 마네킹에게 보입니다"),4.f);
    }
    // Long marks (chalk, the store map): sent again before each client light fades.
    for(int32 I=RogueMarks.Num()-1;I>=0;--I)
    {
        FRogueMark& M=RogueMarks[I];if(Now>=M.Until){RogueMarks.RemoveAtSwap(I);continue;}
        if(Now<M.NextAt)continue;M.NextAt=Now+2.6;const float Left=float(FMath::Min(3.0,M.Until-Now));
        if(M.Only.IsValid())PulseTo(M.Only.Get(),M.At,M.Color,Left,M.Radius);else Pulse(M.At,M.Color,Left,M.Radius);
    }
    // M03: the body just taken over surges for 2 s (and sparks); the next switch waits 8 s.
    if(MannequinItem(TEXT("M03")))for(const auto& Pair:Mode->MatchPlayerRecords)
        if(auto* C=Pair.Key.Get();C&&ProjectItemRoguelike::IsMannequinRole(Pair.Value.Role))
        {
            AMannequinAICharacter* Body=C->GetViewedMannequin();
            if(Body&&BatonLast.IsValid()&&BatonLast.Get()!=Body)
            {FBody& B=Bodies.FindOrAdd(Body);B.BoostUntil=Now+2.0;B.BoostAmount=FMath::Max(B.BoostAmount,.15f);B.FlashUntil=Now+.6;B.Flash=Spark;BatonLockUntil=Now+8.0;}
            if(Body)BatonLast=Body;
        }
    // M15: a survivor close to an exit is reported to the mannequin, and that exit glows red for everyone.
    if(MannequinItem(TEXT("M15")))for(TActorIterator<APlayerCharacter> It(World);It;++It)if(IsValid(*It)&&!It->IsGameOver()&&!It->IsHidden())
        for(const FVector& E:RogueExits)if(FVector::DistSquared2D(E,It->GetActorLocation())<FMath::Square(1000.f))
        {
            double& Until=ExitWatchCooldown.FindOrAdd(E,-1);if(Now<Until)continue;Until=Now+30.0;
            NotifyRole(true,TEXT("출구 지기: 생존자가 출구 가까이 왔습니다"),2.f);Pulse(E+FVector(0,0,180),Alarm,3.f,400.f);
        }
    // M14: one survivor left of two or more: all bodies quicken; the last survivor is told once.
    if(MannequinItem(TEXT("M14"))&&!bLastStandNotified)
    {
        int32 Total=0,InPlay=0;for(const auto& Pair:Mode->MatchPlayerRecords)if(ProjectItemRoguelike::IsSurvivorRole(Pair.Value.Role))
        {++Total;InPlay+=Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active?1:0;}
        if(Total>=2&&InPlay==1){bLastStandNotified=true;NotifyRole(false,TEXT("마지막 진열: 몸체들이 빨라집니다"),3.f);}
    }
}

void UProjectAffixRuntime::RogueSurvivor(AMultiplayTestGameMode* Mode,APlayerCharacter* Survivor,FSurvivor& S,const TArray<APlayerCharacter*,TInlineAllocator<4>>& Active,
    float& Drain,float& Recovery,float& Slow,float& Boost,FLinearColor& Cue,float& CueStrength)
{
    if(!Rogue())return;
    UWorld* World=Mode->GetWorld();auto* PC=Cast<AMultiplayTestPlayerController>(Survivor->GetController());
    const FVector Here=Survivor->GetActorLocation();const float Speed=Survivor->GetVelocity().Size2D();
    const bool Sprinting=Survivor->IsSprintingForBreathing(),Still=Speed<60.f;
    float Foot=1.f,ForMannequin=1.f;
    TArray<AMannequinAICharacter*,TInlineAllocator<10>> Near;bool HoldNear=false,AnyClose=false;
    for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(IsValid(*It)&&!It->IsHidden())
    {
        const float D=FVector::Dist(It->GetActorLocation(),Here);
        if(D<800.f)Near.Add(*It);
        AnyClose|=D<300.f;
        HoldNear|=D<1000.f&&It->GetMultiplayCommand()==EMultiplayMannequinCommand::HoldPosition&&!It->IsManualControlEnabled();
    }
    if(Item(Survivor,TEXT("S01"))&&Sprinting){Drain*=.75f;Foot*=1.3f;}
    if(Item(Survivor,TEXT("S02"))&&S.bWatching&&Still){Recovery*=2.f;Raise(Cue,CueStrength,White,.5f);}
    const bool Suit=Item(Survivor,TEXT("S03"));
    Survivor->SetRogueMaxStaminaScale(Suit?.85f:1.f);
    if(const FMultiplayTestServerPlayerRecord* Record=Suit?Mode->FindSurvivorRecord(Survivor):nullptr;
        Record&&!Mode->GetItemRoguelike()->IsSpent(UProjectItemRoguelikeRuntime::KeyOf(*Record),TEXT("S03")))Raise(Cue,CueStrength,Amber,.2f); // The scorched suit, until used.
    // S04: the body a teammate holds glows for me (a light on it only I get; walls still hide it).
    if(Item(Survivor,TEXT("S04")))for(APlayerCharacter* Other:Active)if(Other!=Survivor)
        if(const FSurvivor* O=Survivors.Find(Other))
        {
            AMannequinAICharacter* Target=O->WatchSince>=0?O->WatchTarget.Get():nullptr;
            if(Target!=S.SharedTarget.Get()){S.SharedTarget=Target;if(Target)Pulse(Here+FVector(0,0,60),Radio,.4f,120.f);}
            if(Target&&Target!=S.WatchTarget.Get()&&Now>=S.NextSharePulseAt){S.NextSharePulseAt=Now+.45;PulseTo(PC,Target->GetActorLocation()+FVector(0,0,100),Radio,.5f,180.f);}
        }
    // S05: a body moving close behind is named by direction; the mirror catches the light.
    if(Item(Survivor,TEXT("S05"))&&Now>=S.MirrorCooldownUntil)for(AMannequinAICharacter* Body:Near)
    {
        const FVector To=Body->GetActorLocation()-Here;
        if(Body->GetVelocity().Size2D()>60.f&&FVector::DotProduct(Survivor->GetActorForwardVector().GetSafeNormal2D(),To.GetSafeNormal2D())<-.3f)
        {S.MirrorCooldownUntil=Now+3.0;S.GlintUntil=Now+.5;NotifyTo(PC,FString::Printf(TEXT("손거울: %s에서 움직임"),*Direction(Survivor->GetActorForwardVector(),To)),1.5f);break;}
    }
    if(Now<S.GlintUntil)Raise(Cue,CueStrength,White,.6f);
    // S07 weak spot: a long stare (3 s) lights the starer's face.
    if(Item(Survivor,TEXT("S07"))&&S.bWatching&&S.WatchSince>=0&&Now-S.WatchSince>3.0)Raise(Cue,CueStrength,White,.9f);
    if(Item(Survivor,TEXT("S09"))){if(Sprinting)Slow=FMath::Max(Slow,.08f);else Foot*=.1f;}
    if(Item(Survivor,TEXT("S10"))&&!S.bDrankThisFloor&&Survivor->GetCurrentStamina()<=.01f)
    {S.bDrankThisFloor=true;Survivor->AddRogueStamina(50.f);Pulse(Here+FVector(0,0,60),Amber,.8f,220.f);}
    if(Item(Survivor,TEXT("S11")))
    {
        Recovery*=1.3f;
        if(!Still&&Now>=S.NextTrailAt){S.NextTrailAt=Now+.6;Pulse(Here-FVector(0,0,80),Water,3.f,70.f);}
    }
    if(Item(Survivor,TEXT("S13"))&&AnyClose){Drain*=.5f;Raise(Cue,CueStrength,Grey,.3f);}
    if(Now<S.AdrenalineUntil){Drain=0.f;Raise(Cue,CueStrength,Ember,.6f);}
    if(Item(Survivor,TEXT("S15"))){Slow=FMath::Max(Slow,.05f);Raise(Cue,CueStrength,Rain,.15f);}
    if(MannequinItem(TEXT("M06"))&&HoldNear)ForMannequin=1.6f;
    if(MannequinItem(TEXT("M13"))&&AnyClose)Recovery=0.f;
    Survivor->SetRogueFootstepScale(Foot,ForMannequin);
    (void)Boost;
}

void UProjectAffixRuntime::RogueBody(AMultiplayTestGameMode* Mode,AMannequinAICharacter* Body,FBody& State,const TArray<APlayerCharacter*,TInlineAllocator<4>>& Active,
    float& Slow,float& Boost,FLinearColor& Cue,float& CueStrength)
{
    if(!Rogue())return;
    const bool Manual=Body->IsManualControlEnabled(),Frozen=Body->IsFrozenBySurvivorVision();
    const EMultiplayMannequinCommand Command=Body->GetMultiplayCommand();
    const bool Hold=Command==EMultiplayMannequinCommand::HoldPosition&&!Manual;
    const FVector Here=Body->GetActorLocation();const float Speed=Body->GetVelocity().Size2D();
    float Foot=1.f;
    if(MannequinItem(TEXT("M01"))&&Manual){Boost=FMath::Max(Boost,.10f);Foot*=1.4f;}
    if(MannequinItem(TEXT("M08"))&&Manual){Slow=FMath::Max(Slow,.10f);if(Speed<700.f)Foot=0.f;}
    // M02: a survivor crossing in front of a standing body is pointed out to the mannequin; the body's eyes light.
    if(MannequinItem(TEXT("M02"))&&Hold&&Now>=State.WatchAlertCooldownUntil)for(const APlayerCharacter* Survivor:Active)
    {
        const FVector To=Survivor->GetActorLocation()-Here;
        if(To.Size2D()<400.f&&FVector::DotProduct(Body->GetActorForwardVector().GetSafeNormal2D(),To.GetSafeNormal2D())>.5f)
        {State.WatchAlertCooldownUntil=Now+3.0;State.FlashUntil=Now+1.0;State.Flash=White;PulseRole(true,Here+FVector(0,0,120),White,1.f,160.f);break;}
    }
    if(MannequinItem(TEXT("M05"))&&Command==EMultiplayMannequinCommand::FollowMannequinPlayer&&!Manual)
    {
        Boost=FMath::Max(Boost,.08f);
        if(Speed>60.f&&Now>=State.NextDustAt){State.NextDustAt=Now+1.0;Pulse(Here-FVector(0,0,80),Dust,.8f,140.f);}
    }
    if(MannequinItem(TEXT("M06"))&&Hold)Raise(Cue,CueStrength,White,.25f);
    // M07: a standing body let go by the eyes lurches for 1 s (10 s between).
    if(MannequinItem(TEXT("M07"))&&Hold&&State.bWasFrozen&&!Frozen&&Now>=State.ClockCooldownUntil)
    {State.ClockCooldownUntil=Now+10.0;State.BoostUntil=Now+1.0;State.BoostAmount=FMath::Max(State.BoostAmount,.15f);State.FlashUntil=Now+.4;State.Flash=Amber;}
    State.bWasFrozen=Frozen;
    // M09: the longer nobody is caught, the redder the joints; after 60 s every body is a little faster.
    if(MannequinItem(TEXT("M09")))
    {
        const double Since=Now-(LastCatchAt>=0?LastCatchAt:RogueStartAt);const float Hunger=FMath::Clamp(float(Since/60.0),0.f,1.f);
        if(Hunger>.2f)Raise(Cue,CueStrength,Ember,.5f*Hunger);
        if(Hunger>=1.f)Boost=FMath::Max(Boost,.05f);
    }
    if(MannequinItem(TEXT("M10"))&&FeedBody.Get()==Body&&Now<FeedUntil){Boost=FMath::Max(Boost,.15f);Raise(Cue,CueStrength,Ember,.8f);}
    if(MannequinItem(TEXT("M12")))for(const APlayerCharacter* Survivor:Active)
        if(Survivor->IsSprintingForBreathing()&&FVector::DistSquared(Survivor->GetActorLocation(),Here)<FMath::Square(1000.f))
        {Boost=FMath::Max(Boost,.06f);Raise(Cue,CueStrength,Ember,.3f);break;}
    if(MannequinItem(TEXT("M13")))
    {
        Slow=FMath::Max(Slow,.05f);
        bool Close=false;for(const APlayerCharacter* Survivor:Active)Close|=FVector::DistSquared(Survivor->GetActorLocation(),Here)<FMath::Square(400.f);
        if(Close&&Now>=State.NextFrostAt){State.NextFrostAt=Now+1.0;Pulse(Here-FVector(0,0,80),Frost,1.f,300.f);}
    }
    if(MannequinItem(TEXT("M14"))&&bLastStandNotified)Boost=FMath::Max(Boost,.06f);
    Body->SetRogueFootstepScale(Foot);
}

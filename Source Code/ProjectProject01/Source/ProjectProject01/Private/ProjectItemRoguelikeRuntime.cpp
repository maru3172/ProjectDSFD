// File: Source/ProjectProject01/Private/ProjectItemRoguelikeRuntime.cpp
// Target: ProjectProject01 / ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#include "ProjectItemRoguelikeRuntime.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectRoguePick.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectRogue,Log,All);

bool UProjectItemRoguelikeRuntime::Accept(const FString& Match,EProjectMatchMode InMode,FString& Error)
{
    // Fixed for the whole match like the custom rules: the first ticket decides, later tickets must agree.
    if(!MatchId.IsEmpty()&&MatchId==Match){if(Mode==InMode)return true;Error=TEXT("Match mode changed after the match started.");return false;}
    if(!MatchId.IsEmpty()&&MatchId!=Match&&(bPicking||Rounds>0)){Error=TEXT("A different match is already running.");return false;}
    if(MatchId!=Match){Players.Reset();Buffs.Reset();Rounds=0;bPicking=bFloorDone=false;}
    MatchId=Match;Mode=InMode;
    Rng.Initialize(int32(GetTypeHash(Match)^uint32(FPlatformTime::Cycles())));
    return true;
}

bool UProjectItemRoguelikeRuntime::InPlay(const FMultiplayTestServerPlayerRecord& R)
{
    if(R.bForfeited)return false;
    if(ProjectItemRoguelike::IsMannequinRole(R.Role))return true;
    return ProjectItemRoguelike::IsSurvivorRole(R.Role)&&(R.SurvivorState==EMultiplayTestSurvivorState::Active||R.SurvivorState==EMultiplayTestSurvivorState::TransitionWaiting);
}
FString UProjectItemRoguelikeRuntime::KeyOf(const FMultiplayTestServerPlayerRecord& R){return R.UserId.IsEmpty()?R.DisplayName:R.UserId;}

bool UProjectItemRoguelikeRuntime::HasItem(const FString& UserId,const TCHAR* Id)const
{const auto* P=IsActive()?Players.Find(UserId):nullptr;return P&&P->Picks.Contains(Id);}

TArray<FString> UProjectItemRoguelikeRuntime::TeammateBuffsOffered(const FProjectRoguePlayer& Except)const
{
    // Two survivors pick in the same round: their buff cards never overlap, so both picks take effect.
    TArray<FString> Out=Buffs;
    for(const auto& Pair:Players)if(Pair.Key!=Except.UserId&&Pair.Value.Role==Except.Role&&!Pair.Value.bPicked)
        for(const FString& Id:Pair.Value.Offer)if(ProjectAffixes::Find(Id))Out.AddUnique(Id);
    return Out;
}

void UProjectItemRoguelikeRuntime::RollFor(FProjectRoguePlayer& P,const TArray<FString>& Avoid)
{
    P.Offer=ProjectItemRoguelike::Roll(ProjectItemRoguelike::IsSurvivorRole(P.Role),P.Picks,TeammateBuffsOffered(P),Rng,Avoid);
    ++P.Serial;
}

void UProjectItemRoguelikeRuntime::Open(AMultiplayTestGameMode* GM,const TArray<const FMultiplayTestServerPlayerRecord*>& Records,double RealNow)
{
    bPicking=true;EndsAt=RealNow+ProjectItemRoguelike::PickSeconds;NextStatusAt=RealNow;
    for(auto& Pair:Players)Pair.Value.bPicked=true; // Out of play (caught, escaped): no card this floor.
    for(const auto* R:Records)
    {
        const FString Key=R?KeyOf(*R):FString();
        if(!R||!InPlay(*R)||Key.IsEmpty())continue;
        FProjectRoguePlayer& P=Players.FindOrAdd(Key);
        P.UserId=Key;P.Name=R->DisplayName;P.Role=R->Role;P.bBot=R->bServerBot;
        if(P.Picks.Num()>=ProjectItemRoguelike::MaxFloors)continue;
        P.bPicked=false;P.bRerollUsed=false;P.Offer.Reset();
    }
    // Rolled one by one so teammates' buff cards never overlap.
    for(auto& Pair:Players)if(!Pair.Value.bPicked)
    {
        RollFor(Pair.Value,{});Pair.Value.FirstOffer=Pair.Value.Offer;
        if(Pair.Value.Offer.IsEmpty()){Pair.Value.bPicked=true;continue;} // Nothing left to offer.
    }
    // Server bots pick at once (a random card), like a player who chose quickly.
    for(auto& Pair:Players)if(!Pair.Value.bPicked&&Pair.Value.bBot)Choose(Pair.Value,Rng.RandRange(0,Pair.Value.Offer.Num()-1),false);
    UE_LOG(LogProjectRogue,Log,TEXT("Item roguelike round %d opened for %d player(s)."),Rounds,WaitingCount());
    if(GM)SendAll(GM,RealNow);
}

bool UProjectItemRoguelikeRuntime::Choose(FProjectRoguePlayer& P,int32 Index,bool bAuto)
{
    if(P.bPicked||!P.Offer.IsValidIndex(Index))return false;
    const FString Id=P.Offer[Index];
    const bool bSurvivor=ProjectItemRoguelike::IsSurvivorRole(P.Role);
    if(!ProjectItemRoguelike::IsCardFor(Id,bSurvivor)||P.Picks.Contains(Id))return false;
    P.Picks.Add(Id);P.Auto.Add(bAuto);P.bPicked=true;P.Offer.Reset();++P.Serial;
    if(ProjectAffixes::Find(Id))Buffs.AddUnique(Id);
    UE_LOG(LogProjectRogue,Log,TEXT("Round %d: %s picked %s%s."),Rounds,*P.Name,*Id,bAuto?TEXT(" (time out)"):TEXT(""));
    return true;
}

int32 UProjectItemRoguelikeRuntime::WaitingCount()const
{int32 N=0;for(const auto& Pair:Players)N+=Pair.Value.bPicked?0:1;return N;}

bool UProjectItemRoguelikeRuntime::TickOpen(AMultiplayTestGameMode* GM,double RealNow)
{
    if(!bPicking)return false;
    if(RealNow>=EndsAt)for(auto& Pair:Players)if(!Pair.Value.bPicked&&!Pair.Value.Offer.IsEmpty())
        Choose(Pair.Value,Rng.RandRange(0,Pair.Value.Offer.Num()-1),true);
    if(WaitingCount()==0){Close(GM);return false;}
    if(GM&&RealNow>=NextStatusAt){NextStatusAt=RealNow+1.0;SendAll(GM,RealNow);}
    return true;
}

void UProjectItemRoguelikeRuntime::Close(AMultiplayTestGameMode* GM)
{
    bPicking=false;bFloorDone=true;bFloorStartPending=true;++Rounds;
    if(!GM)return;
    for(const auto& Pair:Players)if(auto* PC=FindController(GM,Pair.Key))if(auto* C=PC->FindComponentByClass<UProjectRoguePickComponent>())C->ClientClosePick();
    UE_LOG(LogProjectRogue,Log,TEXT("Item roguelike round %d closed; %d buff(s) in play."),Rounds-1,Buffs.Num());
}

bool UProjectItemRoguelikeRuntime::HoldGate(AMultiplayTestGameMode* GM,double RealNow)
{
    if(!IsActive()||!GM)return false;
    if(!bPicking&&!bFloorDone)
    {
        TArray<const FMultiplayTestServerPlayerRecord*> Records;
        for(const auto& Pair:GM->MatchPlayerRecords)Records.Add(&Pair.Value);
        for(const auto& Pair:GM->DisconnectedMatchRecords)Records.Add(&Pair.Value);
        Open(GM,Records,RealNow);
    }
    return TickOpen(GM,RealNow);
}

FProjectRoguePlayer* UProjectItemRoguelikeRuntime::PlayerOf(AMultiplayTestGameMode* GM,AMultiplayTestPlayerController* PC)
{
    if(!GM||!PC)return nullptr;
    const auto* Record=GM->MatchPlayerRecords.Find(PC);
    // Only the owning, registered player may answer its own offer.
    return Record&&!Record->bDisconnected?Players.Find(KeyOf(*Record)):nullptr;
}

bool UProjectItemRoguelikeRuntime::Pick(AMultiplayTestGameMode* GM,AMultiplayTestPlayerController* PC,int32 Serial,int32 Index)
{
    FProjectRoguePlayer* P=bPicking?PlayerOf(GM,PC):nullptr;
    if(!P||P->Serial!=Serial||!Choose(*P,Index,false))return false;
    TickOpen(GM,FPlatformTime::Seconds());
    if(bPicking)SendAll(GM,FPlatformTime::Seconds());
    return true;
}

bool UProjectItemRoguelikeRuntime::Reroll(AMultiplayTestGameMode* GM,AMultiplayTestPlayerController* PC,int32 Serial)
{
    FProjectRoguePlayer* P=bPicking?PlayerOf(GM,PC):nullptr;
    if(!P||P->bPicked||P->bRerollUsed||P->Serial!=Serial)return false;
    const TArray<FString> Old=P->Offer;
    P->Offer.Reset(); // So this player's own buffs do not block their new cards.
    RollFor(*P,Old);P->bRerollUsed=true;
    if(P->Offer.IsEmpty()){P->Offer=Old;} // Nothing new left: keep the old cards.
    Send(GM,*P,FPlatformTime::Seconds());
    return true;
}

void UProjectItemRoguelikeRuntime::Resend(AMultiplayTestGameMode* GM,AMultiplayTestPlayerController* PC)
{
    if(!bPicking||!GM||!PC)return;
    if(const auto* Record=GM->MatchPlayerRecords.Find(PC))if(const auto* P=Players.Find(KeyOf(*Record)))Send(GM,*P,FPlatformTime::Seconds());
}

AMultiplayTestPlayerController* UProjectItemRoguelikeRuntime::FindController(AMultiplayTestGameMode* GM,const FString& UserId)const
{
    if(GM)for(const auto& Pair:GM->MatchPlayerRecords)if(KeyOf(Pair.Value)==UserId&&!Pair.Value.bServerBot)return Pair.Key.Get();
    return nullptr;
}

void UProjectItemRoguelikeRuntime::Send(AMultiplayTestGameMode* GM,const FProjectRoguePlayer& P,double RealNow)const
{
    auto* PC=FindController(GM,P.UserId);auto* C=PC?PC->FindComponentByClass<UProjectRoguePickComponent>():nullptr;if(!C)return;
    FProjectRogueOffer O;O.Serial=P.Serial;O.Floor=Rounds;O.Cards=P.Offer;O.Owned=P.Picks;O.bRerollUsed=P.bRerollUsed;O.bPicked=P.bPicked;
    O.PickedId=P.bPicked&&P.Picks.Num()>Rounds?P.Picks.Last():FString();
    O.SecondsLeft=float(FMath::Max(0.0,EndsAt-RealNow));O.Waiting=WaitingCount();
    C->ClientShowOffer(O);
}

void UProjectItemRoguelikeRuntime::SendAll(AMultiplayTestGameMode* GM,double RealNow)const
{for(const auto& Pair:Players)if(!Pair.Value.bBot)Send(GM,Pair.Value,RealNow);}

FProjectRoguePickRecord UProjectItemRoguelikeRuntime::RecordFor(const FString& UserId)const
{
    FProjectRoguePickRecord R;
    if(const auto* P=IsActive()?Players.Find(UserId):nullptr){R.Name=P->Name;R.Role=P->Role;R.Ids=P->Picks;R.Auto=P->Auto;}
    return R;
}

TArray<FProjectRoguePickRecord> UProjectItemRoguelikeRuntime::Records()const
{
    TArray<FProjectRoguePickRecord> Out;if(!IsActive())return Out;
    for(const auto& Pair:Players)if(!Pair.Value.Picks.IsEmpty())Out.Add(RecordFor(Pair.Key));
    Out.StableSort([](const auto& A,const auto& B){return ProjectItemRoguelike::IsMannequinRole(A.Role)&&!ProjectItemRoguelike::IsMannequinRole(B.Role);});
    return Out;
}

FProjectRogueCarry UProjectItemRoguelikeRuntime::Export()const
{
    FProjectRogueCarry C;C.Mode=Mode;C.MatchId=MatchId;C.Players=Players;C.Buffs=Buffs;C.Rounds=Rounds;
    for(auto& Pair:C.Players){Pair.Value.Offer.Reset();Pair.Value.bPicked=true;}
    return C;
}

void UProjectItemRoguelikeRuntime::Import(const FProjectRogueCarry& C)
{
    // The next floor's world: same match, its own pick round.
    Mode=C.Mode;MatchId=C.MatchId;Players=C.Players;Buffs=C.Buffs;Rounds=C.Rounds;bPicking=bFloorDone=false;
    Rng.Initialize(int32(GetTypeHash(MatchId)^uint32(FPlatformTime::Cycles())^uint32(Rounds)));
}

void UProjectItemRoguelikeRuntime::OpenForTest(const TArray<FMultiplayTestServerPlayerRecord>& Records,double RealNow)
{
    TArray<const FMultiplayTestServerPlayerRecord*> Ptrs;for(const auto& R:Records)Ptrs.Add(&R);
    bFloorDone=false;Open(nullptr,Ptrs,RealNow);
}
bool UProjectItemRoguelikeRuntime::PickForTest(const FString& UserId,int32 Serial,int32 Index)
{
    auto* P=bPicking?Players.Find(UserId):nullptr;
    if(!P||P->Serial!=Serial||!Choose(*P,Index,false))return false;
    TickOpen(nullptr,0);return true;
}
bool UProjectItemRoguelikeRuntime::RerollForTest(const FString& UserId,int32 Serial)
{
    auto* P=bPicking?Players.Find(UserId):nullptr;
    if(!P||P->bPicked||P->bRerollUsed||P->Serial!=Serial)return false;
    const TArray<FString> Old=P->Offer;P->Offer.Reset();RollFor(*P,Old);P->bRerollUsed=true;if(P->Offer.IsEmpty())P->Offer=Old;return true;
}

#include "ProjectMatchBots.h"
#include "MultiplayTestGameMode.h"
#include "MannequinAICharacter.h"
#include "AIController.h"
#include "Engine/World.h"
#include "Engine/TriggerBox.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Kismet/GameplayStatics.h"
#include "ProjectProject01LevelTransitionTrigger.h"
#include "ProjectProject01Elevator.h"
#include "MultiplayTestPlayerController.h"

namespace
{
    ProjectBots::Vec V(const FVector& P){return {float(P.X),float(P.Y),float(P.Z)};}
    FVector U(ProjectBots::Vec P){return FVector(P.X,P.Y,P.Z);}
    bool Sees(APawn* Observer,AActor* Target)
    {
        if(!Observer||!Target||Target->IsHidden()||!Observer->GetController())return false;
        FVector Eye;FRotator Facing;Observer->GetActorEyesViewPoint(Eye,Facing);
        const FVector Offset=Target->GetActorLocation()-Eye;
        return Offset.SizeSquared()<FMath::Square(2400.f)&&FVector::DotProduct(Facing.Vector(),Offset.GetSafeNormal())>.573576f&&Observer->GetController()->LineOfSightTo(Target);
    }
}
void UProjectMatchBotMovement::TickComponent(float Delta,ELevelTick Kind,FActorComponentTickFunction* TickFunction)
{ServerInput=GetPendingInputVector();Super::TickComponent(Delta,Kind,TickFunction);}
void UProjectMatchBotMovement::ServerAutonomousProxyTick(float Delta)
{
    const auto* PC=HasValidData()?Cast<AMultiplayTestPlayerController>(CharacterOwner->GetController()):nullptr;
    if(PC&&PC->IsServerControlledBot()&&CharacterOwner->HasAuthority())ControlledCharacterMove(ServerInput,Delta);
}
AProjectMatchBotCharacter::AProjectMatchBotCharacter(const FObjectInitializer& Initializer)
    :Super(Initializer.SetDefaultSubobjectClass<UProjectMatchBotMovement>(ACharacter::CharacterMovementComponentName)){}
bool AProjectMatchBotCharacter::ApplyDefaultPawnAppearance()
{
    if(bAppearanceApplied)return true;
    // Clients learn the game mode class from the replicated GameState; it may arrive after this pawn.
    const UWorld* World=GetWorld();const AGameStateBase* State=World?World->GetGameState():nullptr;
    const AGameModeBase* ModeDefaults=State&&State->GameModeClass?State->GameModeClass->GetDefaultObject<AGameModeBase>():nullptr;
    const ACharacter* Template=ModeDefaults&&ModeDefaults->DefaultPawnClass?Cast<ACharacter>(ModeDefaults->DefaultPawnClass->GetDefaultObject()):nullptr;
    if(!Template||Template->GetClass()==GetClass()||!GetMesh()||!Template->GetMesh()||!GetCapsuleComponent()||!Template->GetCapsuleComponent())return false;
    auto* Body=GetMesh();auto* Source=Template->GetMesh(); // CDO component: read only.
    Body->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());Body->SetAnimInstanceClass(Source->GetAnimClass());Body->SetRelativeTransform(Source->GetRelativeTransform());
    // Network smoothing on other clients snaps the mesh back to the offset cached at spawn; without
    // re-caching, the old (C++ default) offset makes the bot float above the floor on players' screens.
    CacheInitialMeshOffset(Source->GetRelativeLocation(),Source->GetRelativeRotation());
    for(int32 I=0;I<Source->GetNumMaterials();++I)Body->SetMaterial(I,Source->GetMaterial(I));
    GetCapsuleComponent()->SetCapsuleSize(Template->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),Template->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
    bAppearanceApplied=true;return true;
}
void AProjectMatchBotCharacter::BeginPlay()
{
    Super::BeginPlay();
    ApplyDefaultPawnAppearance();
}
void AProjectMatchBotCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!bAppearanceApplied)ApplyDefaultPawnAppearance(); // Late GameState on a client.
}
AMultiplayTestGameMode* UProjectMatchBots::Mode()const{return Cast<AMultiplayTestGameMode>(GetOuter());}

TSharedPtr<FProjectBotLearning> UProjectMatchBots::ExportLearning()const
{
    auto* GM=Mode();if(!GM||!GM->GetWorld()||Specs.IsEmpty())return nullptr;
    auto Learning=MakeShared<FProjectBotLearning>();
    Learning->MannequinRelief=MannequinRelief;Learning->SurvivorRelief=SurvivorRelief;
    Learning->SavedWorldSeconds=GM->GetWorld()->GetTimeSeconds();Learning->SavedRealSeconds=FPlatformTime::Seconds();
    // Pawn ids change on the next floor; the player's identity does not.
    for(const auto& Entry:Shared.Entries)if(Entry.Seen.Team==1&&Entry.Seen.Target)
        for(const auto& Pair:GM->MatchPlayerRecords)
            if(const auto* Survivor=Pair.Value.SurvivorPawn.Get();Survivor&&Survivor->GetUniqueID()==Entry.Seen.Target)
            {Learning->SurvivorHabits.Add(Pair.Value.UserId,Entry.Statistics);break;}
    // Habits not yet re-seen on this floor are still worth keeping.
    for(const auto& Pair:PendingSurvivorHabits)if(!Learning->SurvivorHabits.Contains(Pair.Key))Learning->SurvivorHabits.Add(Pair.Key,Pair.Value);
    for(const auto& Pair:Plays)Learning->Experience.Add(Pair.Key,{Pair.Value.Combos,Pair.Value.Tuning});
    Learning->FloorDepth=FloorDepth;
    return Learning;
}
void UProjectMatchBots::TrackLearning(FBotPlayState& State,const ProjectBots::Difficulty& Level,double Now,bool Worked)
{
    using namespace ProjectBots;
    uint32 Mask=0;
    for(const auto& Slot:State.Director.Slots)if(Slot.Kind!=Play::None)Mask|=1u<<static_cast<uint32>(Slot.Kind);
    const uint32 Ended=State.ActiveMask&~Mask,Started=Mask&~State.ActiveMask;State.ActiveMask=Mask;
    for(uint32 I=1;I<PlayCount;++I)
    {
        const Play Kind=static_cast<Play>(I);
        if(Ended&(1u<<I)){State.Combos.Ended(Kind,Worked,Now,Level);State.Tuning.End(Kind,Worked,Level);}
        if(Started&(1u<<I))
        {
            // Trial variant for this run (already begun if the play's start used its reach); the tell stays as is.
            State.Tuning.Begin(Kind,Level,FMath::FRand(),FMath::FRand());
            for(auto& Slot:State.Director.Slots)if(Slot.Kind==Kind)Slot.Ends=Slot.SignalEnds+(Slot.Ends-Slot.SignalEnds)*State.Tuning.DurationScale(Kind);
        }
    }
}
uint8 UProjectMatchBots::StyleOf(const FProjectMatchBotSpec& Spec)
{
    using namespace ProjectBots;
    return static_cast<uint8>(StyleFor(Persona::From(GetTypeHash(Spec.BotId)*2654435761u+1u),Spec.Role==TEXT("Mannequin")));
}
void UProjectMatchBots::LoadExperience(FBotPlayState& State,const FProjectMatchBotSpec& Spec,const ProjectBots::Difficulty& Level)
{
    if(State.ExperienceLoaded)return;
    State.ExperienceLoaded=true;
    TMap<uint8,FProjectBotExperience> Pool;
    if(Spec.Experience.IsEmpty()||!DecodeExperience(Spec.Experience,Pool))return;
    if(const auto* Mine=Pool.Find(StyleOf(Spec)))
    {
        // Level caps always apply, whatever was stored.
        State.Combos=Mine->Combos;State.Combos.CarryOver(Level);
        State.Tuning=Mine->Tuning;State.Tuning.CarryOver(Level);
    }
}
FString UProjectMatchBots::EncodeExperience(const TMap<uint8,FProjectBotExperience>& Styles)
{
    using namespace ProjectBots;
    FString Text=TEXT("v1");
    TArray<uint8> Keys;Styles.GetKeys(Keys);Keys.Sort();
    for(uint8 Key:Keys)
    {
        const auto& Entry=Styles[Key];FString Combos,Knobs;
        for(const auto& Link:Entry.Combos.Links)if(Link.From!=Play::None&&Link.To!=Play::None&&Link.Score>0)
            Combos+=FString::Printf(TEXT("%s%d-%d-%d"),Combos.IsEmpty()?TEXT(""):TEXT(","),int32(Link.From),int32(Link.To),FMath::Clamp(FMath::RoundToInt(Link.Score*10),1,60));
        for(size_t I=1;I<PlayCount;++I)
        {
            const auto& K=Entry.Tuning.Knobs[I];
            const int32 Dur=FMath::Clamp(FMath::RoundToInt(K.Duration*100),50,150),Reach=FMath::Clamp(FMath::RoundToInt(K.Reach*100),50,150);
            if(Dur!=100||Reach!=100)Knobs+=FString::Printf(TEXT("%s%d-%d-%d"),Knobs.IsEmpty()?TEXT(""):TEXT(","),int32(I),Dur,Reach);
        }
        Text+=FString::Printf(TEXT("|%d;%s;%s"),int32(Key),*Combos,*Knobs);
    }
    return Text.Len()<=2048?Text:FString(TEXT("v1"));
}
bool UProjectMatchBots::DecodeExperience(const FString& Text,TMap<uint8,FProjectBotExperience>& Styles)
{
    using namespace ProjectBots;
    Styles.Reset();
    if(!FProjectMatchBotSpec::IsExperienceText(Text))return false;
    TArray<FString> Entries;Text.RightChop(2).ParseIntoArray(Entries,TEXT("|"),true);
    auto Numbers=[](const FString& Item,int32 (&Out)[3]){TArray<FString> Parts;Item.ParseIntoArray(Parts,TEXT("-"),false);
        if(Parts.Num()!=3)return false;for(int32 I=0;I<3;++I){if(Parts[I].IsEmpty()||Parts[I].Len()>3)return false;Out[I]=FCString::Atoi(*Parts[I]);}return true;};
    for(const FString& Entry:Entries)
    {
        TArray<FString> Fields;Entry.ParseIntoArray(Fields,TEXT(";"),false);
        if(Fields.Num()!=3||Fields[0].IsEmpty()||Fields[0].Len()>2)return false;
        const int32 Key=FCString::Atoi(*Fields[0]);if(Key<0||Key>7)return false;
        FProjectBotExperience Experience;int32 N[3];
        TArray<FString> Items;Fields[1].ParseIntoArray(Items,TEXT(","),true);
        int32 Slot=0;
        for(const FString& Item:Items)
        {
            if(!Numbers(Item,N)||N[0]<1||N[0]>=int32(PlayCount)||N[1]<1||N[1]>=int32(PlayCount)||N[0]==N[1]||N[2]<1||N[2]>60)return false;
            if(Slot<int32(Experience.Combos.Links.size()))Experience.Combos.Links[Slot++]={static_cast<Play>(N[0]),static_cast<Play>(N[1]),N[2]/10.f};
        }
        Items.Reset();Fields[2].ParseIntoArray(Items,TEXT(","),true);
        for(const FString& Item:Items)
        {
            if(!Numbers(Item,N)||N[0]<1||N[0]>=int32(PlayCount)||N[1]<50||N[1]>150||N[2]<50||N[2]>150)return false;
            auto& K=Experience.Tuning.Knobs[N[0]];K.Duration=K.TrialDuration=N[1]/100.f;K.Reach=K.TrialReach=N[2]/100.f;
        }
        Styles.Add(static_cast<uint8>(Key),Experience);
    }
    return true;
}
TArray<FProjectBotExperienceUpload> UProjectMatchBots::ExportExperience()const
{
    // One pool per role and level: start from what the bots arrived with, then write this match's styles over it.
    TMap<FString,TMap<uint8,FProjectBotExperience>> Pools;TMap<FString,FProjectMatchBotSpec> Keys;
    for(const auto& Spec:Specs)
    {
        const FString Key=FString::Printf(TEXT("%s:%d"),*Spec.Role,Spec.Difficulty);
        if(!Pools.Contains(Key)){auto& Pool=Pools.Add(Key);DecodeExperience(Spec.Experience,Pool);Keys.Add(Key,Spec);}
        const auto* State=Plays.Find(Spec.BotId);if(!State)continue;
        const auto Level=ProjectBots::Difficulty::For(Spec.Difficulty);
        FProjectBotExperience Grown{State->Combos,State->Tuning};
        // Level caps again on the way out, so a stored pool can never exceed its level.
        Grown.Combos.Cap(Level);
        for(auto& K:Grown.Tuning.Knobs){K.Trial=false;const float R=ProjectBots::PlayTuning::Range(Level);K.Duration=FMath::Clamp(K.Duration,1-R,1+R);K.Reach=FMath::Clamp(K.Reach,1-R,1+R);}
        Pools[Key].Add(StyleOf(Spec),Grown);
    }
    TArray<FProjectBotExperienceUpload> Out;
    for(const auto& Pair:Pools){const auto& Spec=Keys[Pair.Key];Out.Add({Spec.Role,EncodeExperience(Pair.Value),Spec.Difficulty});}
    // One line per match for tallying outcomes by plan (e.g. grep "[BotScript]" in the server log).
    if(const auto* GM=Mode())
    {
        int32 Escaped=0,Caught=0;FString Levels;
        for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Value.Role==TEXT("Survivor"))
        {Escaped+=Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Escaped;Caught+=Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Eliminated;}
        for(const auto& Spec:Specs)Levels+=FString::Printf(TEXT("%s%s%d"),Levels.IsEmpty()?TEXT(""):TEXT(","),Spec.Role==TEXT("Mannequin")?TEXT("M"):TEXT("S"),Spec.Difficulty);
        const auto Script=ScriptOf();
        UE_LOG(LogTemp,Display,TEXT("[BotScript] Match=%s Mannequin=%s Survivors=%s Bots=%s Floor=%d Escaped=%d Caught=%d Seconds=%.0f"),*MatchId,
            ANSI_TO_TCHAR(ProjectBots::MatchScript::Name(Script.Mannequin)),ANSI_TO_TCHAR(ProjectBots::MatchScript::Name(Script.Survivors)),*Levels,FloorDepth,Escaped,Caught,GM->GetElapsedMatchSeconds());
    }
    return Out;
}
void UProjectMatchBots::ImportLearning(const FProjectBotLearning& Learning)
{
    auto* GM=Mode();if(!GM||!GM->GetWorld())return;
    // The new map's clock started over; shift saved times so habits and relief age by the real travel time.
    const double Offset=GM->GetWorld()->GetTimeSeconds()-(Learning.SavedWorldSeconds+FMath::Max(0.,FPlatformTime::Seconds()-Learning.SavedRealSeconds));
    MannequinRelief=Learning.MannequinRelief;MannequinRelief.Rebase(Offset);
    SurvivorRelief=Learning.SurvivorRelief;SurvivorRelief.Rebase(Offset);
    PendingSurvivorHabits=Learning.SurvivorHabits;
    FloorDepth=FMath::Clamp(Learning.FloorDepth+1,0,32); // One floor further down.
    for(auto& Pair:PendingSurvivorHabits)Pair.Value.Rebase(Offset); // Also drops the old map's rest spots.
    for(const auto& Pair:Learning.Experience)
    {
        // Next floor: same bot, same growth; only the timing and the unfinished trials are dropped.
        auto& State=Plays.FindOrAdd(Pair.Key);State.Combos=Pair.Value.Combos;State.Combos.Rebase();
        State.Tuning=Pair.Value.Tuning;for(auto& K:State.Tuning.Knobs)K.Trial=false;State.ExperienceLoaded=true;
    }
}

bool UProjectMatchBots::AcceptRoster(const FString& Room,const FString& Match,const TArray<FProjectMatchBotSpec>& Bots,FString& Error)
{
    auto* GM=Mode();
    if(!GM||!GM->HasAuthority()){Error=TEXT("Bot roster requires server authority.");return false;}
#if !UE_BUILD_SHIPPING
    // Balance comparison runs only (-BotSimEvolution=2 = the earlier, slower habit growth). Never in shipping.
    {float Evolution=1.f;FParse::Value(FCommandLine::Get(),TEXT("BotSimEvolution="),Evolution);ProjectBots::Difficulty::EvolutionScale()=Evolution;}
#endif
    if(Bots.IsEmpty()){if(Specs.IsEmpty())return true;Error=TEXT("Missing frozen bot roster.");return false;}
    FGuid R,M;TSet<FString> Ids;int32 Mannequins=0,Survivors=0;
    if(Bots.Num()>3||!FGuid::Parse(Room,R)||!R.IsValid()||!FGuid::Parse(Match,M)||!M.IsValid()){Error=TEXT("Invalid bot match identity.");return false;}
    for(const auto& Bot:Bots)
    {
        FGuid Id;
        if(!FGuid::Parse(Bot.BotId,Id)||!Id.IsValid()||Ids.Contains(Id.ToString())||Bot.Difficulty<1||Bot.Difficulty>5||
            Bot.DisplayName.IsEmpty()||Bot.DisplayName.Len()>48||(Bot.Role!=TEXT("Mannequin")&&Bot.Role!=TEXT("Survivor")))
        {Error=TEXT("Invalid frozen bot roster.");return false;}
        Ids.Add(Id.ToString());Mannequins+=Bot.Role==TEXT("Mannequin");Survivors+=Bot.Role==TEXT("Survivor");
    }
    if(Mannequins>1||Survivors>2){Error=TEXT("Invalid bot role allocation.");return false;}
    auto Sorted=Bots;Sorted.Sort([](const auto& A,const auto& B){return A.BotId<B.BotId;});
    if(!Specs.IsEmpty())
    {if(SourceRoom!=Room||MatchId!=Match||Specs!=Sorted){Error=TEXT("Bot roster changed after admission.");return false;}return true;}
    SourceRoom=Room;MatchId=Match;Specs=MoveTemp(Sorted);Error.Reset();return true;
}

bool UProjectMatchBots::SpawnParticipants()
{
    auto* GM=Mode();auto* World=GM?GM->GetWorld():nullptr;
    if(!World||!GM->bServerWorldInitialized||!World->HasBegunPlay())return false;
    bool HasHuman=false;for(const auto& Pair:GM->MatchPlayerRecords)HasHuman|=Pair.Key.IsValid()&&!Pair.Value.bServerBot;
    if(!HasHuman&&!GM->HasOpenObservers())return false; // A pre-login reservation cannot start an unattended match.
    for(const auto& Spec:Specs)
    {
        const FString Id=TEXT("Bot:")+Spec.BotId;
        AMultiplayTestPlayerController* PC=nullptr;
        for(TActorIterator<AMultiplayTestPlayerController> It(World);It;++It)
            if(It->IsServerControlledBot()&&It->GetAuthenticatedUserId()==Id&&It->GetAuthenticatedMatchId()==MatchId){PC=*It;break;}
        if(!PC)
        {
            PC=World->SpawnActorDeferred<AMultiplayTestPlayerController>(AMultiplayTestPlayerController::StaticClass(),FTransform::Identity,GM,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!PC){Failed=true;return false;}PC->bServerControlledBot=true;
            FProjectProject01ValidatedJoinClaim Claim;Claim.UserId=Id;Claim.DisplayName=Spec.DisplayName;Claim.Role=Spec.Role;Claim.MatchId=MatchId;Claim.RoomId=SourceRoom;
            PC->SetAuthenticatedLobbyIdentity(Claim);UGameplayStatics::FinishSpawningActor(PC,FTransform::Identity);
        }
        GM->ExplicitRoleControllers.Add(PC);
        if(Spec.Role==TEXT("Mannequin")){GM->RequestedMannequinControllers.Add(PC);GM->MannequinController=PC;}
        const FMultiplayTestServerPlayerRecord* Previous=GM->MatchPlayerRecords.Find(PC);
        if(!Previous&&GM->MatchCarryover)Previous=GM->MatchCarryover->Players.Find(Id);
        if(!Previous)if(auto* GI=GM->GetGameInstance<UProjectProject01GameInstance>();GI&&GI->ServerMatchCarryover&&GI->ServerMatchCarryover->MatchId==MatchId)
        {GM->MatchCarryover=GI->ServerMatchCarryover;Previous=GM->MatchCarryover->Players.Find(Id);}
        const bool Alive=!Previous||(!Previous->bForfeited&&
            (Previous->SurvivorState==EMultiplayTestSurvivorState::Active||Previous->SurvivorState==EMultiplayTestSurvivorState::TransitionWaiting));
        if(Previous)PC->PrepareLevelTransitionCarryover(Spec.Role==TEXT("Mannequin")||Alive);
        if(Spec.Role==TEXT("Survivor")&&!Alive)
            if(APawn* UnexpectedPawn=PC->GetPawn()){PC->UnPossess();UnexpectedPawn->Destroy();}
        if(Spec.Role==TEXT("Survivor")&&Alive&&!Cast<AProjectMatchBotCharacter>(PC->GetPawn()))
        {
            // Seamless travel may restart the controller with DefaultPawnClass. Replace only
            // that bot body, otherwise it has no server-driven movement and stands still.
            APawn* OldPawn=PC->GetPawn();if(OldPawn){PC->UnPossess();OldPawn->Destroy();}
            AActor* Start=GM->ChoosePlayerStart(PC);if(!Start){Failed=true;return false;}
            FVector Location=Start->GetActorLocation();const auto* Template=GM->DefaultPawnClass?Cast<ACharacter>(GM->DefaultPawnClass->GetDefaultObject()):GetDefault<AProjectMatchBotCharacter>();
            const float Half=Template?Template->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight():88.f;
            if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
            {FNavLocation Floor;if(Nav->ProjectPointToNavigation(Location,Floor,FVector(180,180,300)))Location=Floor.Location+FVector(0,0,Half+2);}
            FActorSpawnParameters Params;Params.Owner=PC;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
            auto* Pawn=World->SpawnActor<AProjectMatchBotCharacter>(AProjectMatchBotCharacter::StaticClass(),Location,Start->GetActorRotation(),Params);
            if(!Pawn){Failed=true;return false;}
            Pawn->ApplyDefaultPawnAppearance(); // Same call runs on every client in the pawn's BeginPlay/Tick.
            PC->Possess(Pawn);Pawn->SetAutonomousProxy(true);GM->SetPlayerDefaults(Pawn);PC->SetControlRotation(Start->GetActorRotation());
            if(Previous)Pawn->SetRemainingDeathCountForGameMode(Previous->RemainingDeathCount);
            Pawn->GetCharacterMovement()->bCanWalkOffLedges=false;
        }
        if(Spec.Role==TEXT("Mannequin"))GM->AssignRandomInitialMannequinView(PC);
        GM->RegisterMatchPlayer(PC);GM->InitializeLoadingGate(PC);Controllers.Add(Spec.BotId,PC);
        if(Spec.Role==TEXT("Survivor"))if(auto* Record=GM->MatchPlayerRecords.Find(PC))
            Record->SurvivorPawn=Cast<APlayerCharacter>(PC->GetPawn());
    }
    Spawned=true;GM->TryStartAuthoritativeMatch();return true;
}

void UProjectMatchBots::NoteProgress(double Now)
{
    auto* GM=Mode();if(!GM)return;
    // A catch, an escape or someone through the exit counts as progress for both sides.
    int32 Outcomes=0;
    for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Value.Role==TEXT("Survivor"))Outcomes+=Pair.Value.SurvivorState!=EMultiplayTestSurvivorState::Active;
    if(LastProgressAt<0||Outcomes!=LastOutcomeCount){LastProgressAt=Now;LastOutcomeCount=Outcomes;}
    // So does any survivor (bot or human) getting 4 m closer to its nearest exit than it has been so far.
    ScanObjectives();
    for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Value.Role==TEXT("Survivor")&&Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active)
        if(const APawn* Pawn=Pair.Value.SurvivorPawn.Get())
        {
            AActor* Exit=nullptr;float D=MAX_flt;
            for(const auto& Entry:Objectives)if(AActor* Objective=Entry.Get()){const float To=FVector::Dist2D(Pawn->GetActorLocation(),Objective->GetActorLocation());if(To<D){D=To;Exit=Objective;}}
            if(!Exit)continue;
            auto& Best=ClosestToExit.FindOrAdd(Pair.Value.UserId,D);auto& For=ProgressExit.FindOrAdd(Pair.Value.UserId);
            if(For.Get()!=Exit){For=Exit;Best=D;continue;}
            if(D<Best-400.f){Best=D;LastProgressAt=Now;}
        }
}
void UProjectMatchBots::Tick(float Delta)
{
    auto* GM=Mode();if(!GM||!GM->HasAuthority()||Specs.IsEmpty())return;
    if(Failed)
    {
        if(GM->AuthoritativeMatchPhase==EMultiplayTestMatchPhase::Ending||GM->AuthoritativeMatchPhase==EMultiplayTestMatchPhase::Results)return;
        // An infrastructure failure is not a victory, defeat or ranked result.
        GM->SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Ending);
        for(const auto& Pair:GM->MatchPlayerRecords)if(auto* PC=Pair.Key.Get();PC&&!PC->IsServerControlledBot())
            PC->ClientFailLoadingGate(TEXT("봇 참가자를 준비하지 못했습니다. 다시 로그인해 주세요."));
        if(IsRunningDedicatedServer())GM->GetWorldTimerManager().SetTimerForNextTick([](){FPlatformMisc::RequestExit(false);});
        return;
    }
    if(!Spawned&&!SpawnParticipants())return;
    if(!GM->IsAuthoritativeMatchInProgress())return;
    NoteProgress(GM->GetWorld()->GetTimeSeconds());
    if(const double Now=GM->GetWorld()->GetTimeSeconds();Now>=PruneBrainsAt)
    {
        // Destroyed bodies (capture, level travel) leave stale weak keys behind.
        PruneBrainsAt=Now+5.;
        for(auto It=Brains.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    }
    if(const double Now=GM->GetWorld()->GetTimeSeconds();Now>=ReliefScanAt){ReliefScanAt=Now+.5;TrackRelief(Now);}
    for(const auto& Spec:Specs)
    {
        auto* PC=Controllers.FindRef(Spec.BotId).Get();if(!PC){Failed=true;return;}
        if(Spec.Role==TEXT("Mannequin"))TickMannequin(PC,Spec,Delta);else TickSurvivor(PC,Spec,Delta);
    }
}
void UProjectMatchBots::Sense(APawn* Observer,AActor* Target,uint32 Team,FProjectMatchBotBrain& B,bool Visible,bool HumanAlly)
{
    const double Now=Mode()->GetWorld()->GetTimeSeconds();
    B.Visible=Visible&&IsValid(Target);if(!B.Visible)return;
    const bool Fresh=B.Target.Get()!=Target||Now-B.SeenAt>B.Policy.Profile.Memory;
    auto* Pawn=Cast<APawn>(Target);const FRotator Facing=Pawn?Pawn->GetViewRotation():Target->GetActorRotation();
    const float Turn=Fresh?0:FMath::FindDeltaAngleDegrees(B.PreviousYaw,Facing.Yaw)/90.f;B.PreviousYaw=Facing.Yaw;
    if(Fresh)
    {
        B.ReactAt=Now+B.Policy.Profile.Reaction*(HumanAlly?1.25:1.)*(.85+.4*B.Policy.Random());
        // A sudden close encounter costs a human an extra beat ("whoa"), less for skilled and bold players.
        B.ReactAt+=ProjectBots::StartleSeconds(B.Policy.Profile,B.Policy.Character,FVector::Dist(Observer->GetActorLocation(),Target->GetActorLocation()));
    }
    B.Target=Target;B.Known=Target->GetActorLocation();B.SeenAt=Now;
    const FVector Velocity=Target->GetVelocity();const bool Watching=FVector::DotProduct(Facing.Vector(),(Observer->GetActorLocation()-B.Known).GetSafeNormal())>.6f;
    // First sighting on a new floor: re-attach the habits learned about this survivor on earlier floors.
    if(Team==1&&!PendingSurvivorHabits.IsEmpty())
        for(const auto& Pair:Mode()->MatchPlayerRecords)if(Pair.Value.SurvivorPawn.Get()==Target)
        {
            if(const auto* Habits=PendingSurvivorHabits.Find(Pair.Value.UserId))
            {Shared.SeedHabits(Team,Target->GetUniqueID(),*Habits);PendingSurvivorHabits.Remove(Pair.Value.UserId);}
            break;
        }
    const auto ObserverAt=V(Observer->GetActorLocation());
    Shared.Publish({Team,Observer->GetUniqueID(),Target->GetUniqueID(),V(B.Known),V(Velocity),Now},true,.2f,Velocity.SizeSquared2D()<1225,Velocity.SizeSquared2D()>122500,Watching,Turn,&ObserverAt);
}
ProjectBots::Context UProjectMatchBots::Context(const FProjectMatchBotBrain& B,bool Mannequin)const
{
    ProjectBots::Context C;C.Playing=Mode()->IsAuthoritativeMatchInProgress();C.Mannequin=Mannequin;
    C.MatchSeconds=Mode()->GetElapsedMatchSeconds(); // Carried across floors: evolution spans the whole match.
    const double Now=Mode()->GetWorld()->GetTimeSeconds();const bool Known=B.Target.IsValid()&&Now>=B.ReactAt&&Now-B.SeenAt<=B.Policy.Profile.Memory;
    C.Visible=Known&&B.Visible;C.Remembered=Known;
    if(Known)if(const auto* Entry=Shared.Find(Mannequin?1:2,B.Target->GetUniqueID(),Now,B.Policy.Profile.Memory))C.Opponent=Entry->Statistics.Read(Now);
    return C;
}
void UProjectMatchBots::InitializeBrain(FProjectMatchBotBrain& B,const FProjectMatchBotSpec& Spec,uint32 Salt)
{
    if(B.Initialized)return;
    B.Initialized=true;B.Policy.Reset(Spec.Difficulty,GetTypeHash(Spec.BotId)^Salt);
    // One personality per bot identity, shared by every mannequin body that bot controls.
    B.Policy.Character=ProjectBots::Persona::From(GetTypeHash(Spec.BotId)*2654435761u+1u);
    B.Policy.Character.PlayStyle=static_cast<uint8>(ProjectBots::StyleFor(B.Policy.Character,Spec.Role==TEXT("Mannequin")));
}
FRotator UProjectMatchBots::HumanLook(FProjectMatchBotBrain& B,const FRotator& Current,const FRotator& Target,float Delta)
{
    // Re-sync when something else (AI controller, possession, teleport) turned the body meanwhile.
    if(!B.Gaze.Initialized||FMath::Abs(FRotator::NormalizeAxis(Current.Yaw-B.Gaze.Yaw))>25.f)B.Gaze.Snap(Current.Yaw,FRotator::NormalizeAxis(Current.Pitch));
    B.Gaze.Step(Target.Yaw,FRotator::NormalizeAxis(Target.Pitch),Delta,B.Policy.Profile,B.Policy.Character,B.Policy.Random()*2-1,B.Policy.Random()*2-1);
    return FRotator(B.Gaze.Pitch,B.Gaze.Yaw,0);
}
bool UProjectMatchBots::Move(APawn* Pawn,FProjectMatchBotBrain& B,const FVector& Goal,float Delta,bool FaceTravel,bool Calm)
{
    auto* World=Mode()->GetWorld();const double Now=World->GetTimeSeconds();
    // Corner peek: stand still briefly and look where the path turns. Moving=true keeps the plan alive.
    if(Now<B.PauseUntil){if(FaceTravel){const FRotator Look=HumanLook(B,Pawn->GetActorRotation(),B.PeekDirection.Rotation(),Delta);Pawn->SetActorRotation(FRotator(0,Look.Yaw,0));}return true;}
    if(Now>=B.PathAt)
    {
        B.PathAt=Now+FMath::Max(.4f,B.Policy.Profile.Reaction);
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);FNavLocation Projected;B.Path.Reset();B.PathIndex=1;
        if(!Nav||!Nav->ProjectPointToNavigation(Goal,Projected,FVector(180,180,250)))return false;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Pawn->GetActorLocation(),Projected.Location,Pawn);
        if(!Path||!Path->IsValid()||Path->IsPartial())return false;B.Path=Path->PathPoints;
    }
    const auto& Quirks=B.Policy.Character;const float Mistake=ProjectBots::MistakeScale(B.Policy.Profile);
    // Corner style: some people cut corners wide, others hug them (50-110 cm switch radius).
    const float Switch=50.f+60.f*Quirks.Wobble;
    if(B.Path.IsValidIndex(B.PathIndex)&&FVector::DistSquared2D(Pawn->GetActorLocation(),B.Path[B.PathIndex])<FMath::Square(Switch))
    {
        ++B.PathIndex;
        // Hesitation: when calm, a curious bot sometimes stops at a sharp turn to look around it first.
        if(Calm&&Now>=B.PeekCooldown&&B.Path.IsValidIndex(B.PathIndex)&&B.PathIndex>=1)
        {
            const FVector In=(B.Path[B.PathIndex-1]-Pawn->GetActorLocation()).GetSafeNormal2D(),Out=(B.Path[B.PathIndex]-B.Path[B.PathIndex-1]).GetSafeNormal2D();
            if(FVector::DotProduct(In,Out)<.55f&&B.Policy.Random()<Quirks.Curiosity*(.25f+.35f*Mistake))
            {
                B.PeekDirection=Out;B.PeekCooldown=Now+6;
                B.PauseUntil=Now+(.25f+.45f*B.Policy.Random())*(1.2f-.4f*Quirks.Patience);
                if(auto* Movement=Pawn->GetMovementComponent())Movement->StopMovementImmediately();return true;
            }
        }
    }
    if(!B.Path.IsValidIndex(B.PathIndex)){B.PathAt=Now;return false;}
    // Small lateral sway: nobody walks a navmesh polyline exactly. Larger and lazier at low levels.
    const float Sway=FMath::Sin(static_cast<float>(Now)*(1.9f+1.6f*Quirks.Wobble)+Quirks.Phase)*(2.f+6.f*Mistake)*(.5f+Quirks.Wobble);
    const FVector Direction=(B.Path[B.PathIndex]-Pawn->GetActorLocation()).GetSafeNormal2D().RotateAngleAxis(Sway,FVector::UpVector);
    ProjectBots::Vec Neighbours[12];size_t Count=0;
    // Steer only reacts inside the separation radius: skip far bodies so near ones are never crowded out by the cap.
    for(TActorIterator<ACharacter> It(World);It&&Count<12;++It)
        if(*It!=Pawn&&!It->IsHidden()&&FVector::DistSquared2D(It->GetActorLocation(),Pawn->GetActorLocation())<FMath::Square(160.f))Neighbours[Count++]=V(It->GetActorLocation());
    const float Remaining=B.PathIndex==B.Path.Num()-1?FVector::Dist2D(Pawn->GetActorLocation(),B.Path.Last()):200.f;
    const auto Velocity=ProjectBots::Steer(V(Pawn->GetActorLocation()),V(Direction),Neighbours,Count,160,1,Remaining);
    FVector Travel=U(Velocity).GetSafeNormal2D();auto* Character=Cast<ACharacter>(Pawn);if(!Character)return false;
    const auto* Capsule=Character->GetCapsuleComponent();FCollisionQueryParams Query(SCENE_QUERY_STAT(MatchBotStep),false,Pawn);FHitResult Hit;
    auto Clear=[&](FVector D){return !World->SweepSingleByChannel(Hit,Pawn->GetActorLocation(),Pawn->GetActorLocation()+D*85,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),FMath::Max(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()-8)),Query);};
    if(!Clear(Travel))Travel=Direction;
    if(!Clear(Travel)){Travel=(B.Path[B.PathIndex]-Pawn->GetActorLocation()).GetSafeNormal2D();} // Sway never blocks a clear path.
    if(!Clear(Travel)){B.Path.Reset();B.PathAt=Now+.3;return false;}
    Pawn->AddMovementInput(Travel,Velocity.Length());
    if(FaceTravel){const FRotator Look=HumanLook(B,Pawn->GetActorRotation(),Travel.Rotation(),Delta);Pawn->SetActorRotation(FRotator(0,Look.Yaw,0));}
    return true;
}
bool UProjectMatchBots::ChooseTacticPoint(APawn* Pawn,FProjectMatchBotBrain& B,ProjectBots::Action Tactic,bool Mannequin,float Radius,FVector& Goal,
    const FVector* Origin,const FVector* Preferred,bool WantsSight,uint8 CacheKey)
{
    using namespace ProjectBots;
    auto* World=Mode()->GetWorld();const double Now=World->GetTimeSeconds();
    const uint8 Key=CacheKey!=255?CacheKey:static_cast<uint8>(Tactic);
    // Re-plan at most about once per second, or when the opponent clearly moved.
    if(B.TacticFor==Key&&Now<B.TacticAt&&FVector::DistSquared2D(B.TacticKnown,B.Known)<FMath::Square(200.f))
    {if(B.TacticFound)Goal=B.TacticGoal;return B.TacticFound;}
    B.TacticFor=Key;B.TacticKnown=B.Known;B.TacticFound=false;B.TacticAt=Now+.5;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav)return false;
    const auto& Profile=B.Policy.Profile;
    TacticQuery Q;Q.Tactic=Tactic;Q.Self=V(Pawn->GetActorLocation());Q.Target=V(B.Known);Q.Origin=Mannequin?Q.Target:Q.Self;
    if(Origin){Q.Origin=V(*Origin);if(!B.Target.IsValid())Q.Target=Q.Origin;}
    Q.Radius=Radius;Q.FlankDegrees=Mannequin?85.f:65.f;Q.WantsSight=WantsSight||(!Mannequin&&(Tactic==Action::Cover||Tactic==Action::Observe));
    if(Preferred)Q.Preferred=V(Preferred->GetSafeNormal2D());
    else if(Mannequin&&Profile.AllowsDeception())
        if(const auto* Habits=Shared.FindHabits(1,B.Target.IsValid()?B.Target->GetUniqueID():0))
            Q.Preferred=PredictFlee(Q.Self,Q.Target,B.Policy.Perceived(Habits->Read(Now),Mode()->GetElapsedMatchSeconds()));
    struct FScored{FVector Point;float Score;};TArray<FScored,TInlineAllocator<12>> Scored;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MatchBotTacticPoint),false,Pawn);if(AActor* Target=B.Target.Get())Query.AddIgnoredActor(Target);
    const FVector Eye=B.Known+FVector(0,0,60);const int32 Samples=Profile.TacticSamples();const float Start=B.Policy.Random()*360.f;
    for(int32 I=0;I<Samples;++I)
    {
        const FVector Raw=U(Q.Origin)+U(RotateYaw({1,0,0},Start+360.f*I/Samples))*Radius*(I%2?.7f:1.f);
        FNavLocation Projected;if(!Nav->ProjectPointToNavigation(Raw,Projected,FVector(150,150,250)))continue;
        if(FMath::Abs(Projected.Location.Z-Pawn->GetActorLocation().Z)>200)continue; // Same storey only.
        TacticCandidate Candidate;Candidate.Point=V(Projected.Location);Candidate.Reachable=true;
        FHitResult Hit;Candidate.Hidden=World->LineTraceSingleByChannel(Hit,Eye,Projected.Location+FVector(0,0,60),ECC_Visibility,Query);
        if(const float Score=ScoreTacticPoint(Q,Candidate,Profile);Score>-.9f)Scored.Add({Projected.Location,Score});
    }
    Scored.Sort([](const FScored& A,const FScored& C){return A.Score>C.Score;});
    // Only the best few pay for a real path: unreachable corners are discarded, not walked into.
    for(int32 I=0;I<FMath::Min(3,Scored.Num());++I)
    {
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Pawn->GetActorLocation(),Scored[I].Point,Pawn);
        if(!Path||!Path->IsValid()||Path->IsPartial())continue;
        B.Path=Path->PathPoints;B.PathIndex=1;B.PathAt=Now+FMath::Max(.4f,Profile.Reaction);
        B.TacticGoal=Goal=Scored[I].Point;B.TacticFound=true;B.TacticAt=Now+FMath::Max(1.f,Profile.Reaction*1.5f);
        return true;
    }
    return false;
}

bool UProjectMatchBots::Hear(APawn* Listener,AActor* Source,float Mistake,FVector& Heard)const
{
    if(!Listener||!IsValid(Source)||Source->IsHidden())return false;
    const float Speed=Source->GetVelocity().Size2D(),Open=ProjectBots::HearingRadius(Speed,false);
    if(Open<=0)return false;
    const FVector From=Listener->GetActorLocation(),At=Source->GetActorLocation();const float Distance=FVector::Dist(From,At);
    if(Distance>Open)return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MatchBotHearing),false,Listener);Query.AddIgnoredActor(Source);FHitResult Hit;
    // Walls muffle footsteps: only trace when the occluded radius would make a difference.
    if(Distance>ProjectBots::HearingRadius(Speed,true)&&Mode()->GetWorld()->LineTraceSingleByChannel(Hit,From,At,ECC_Visibility,Query))return false;
    // Sound gives a direction, not a position: rougher far away and for flustered players.
    const float Error=(60.f+240.f*Mistake)*(.5f+Distance/Open);
    Heard=At+FVector(FMath::FRandRange(-Error,Error),FMath::FRandRange(-Error,Error),0);
    return true;
}
void UProjectMatchBots::ScanObjectives()
{
    if(ScannedObjectives)return;
    ScannedObjectives=true;auto* GM=Mode();
    // Map-authored exits/transition volumes are objectives, not secret opponent knowledge.
    for(TActorIterator<AActor> It(GM->GetWorld());It;++It)
        if(It->IsA<AProjectProject01LevelTransitionTrigger>()||(It->IsA<ATriggerBox>()&&It->ActorHasTag(GM->EscapeTriggerActorTag)))Objectives.Add(*It);
    FString Names;
    for(const auto& Entry:Objectives)if(const AActor* Objective=Entry.Get())
    {
        const auto* Transition=Cast<AProjectProject01LevelTransitionTrigger>(Objective);
        Names+=FString::Printf(TEXT(" %s(%s)"),*Objective->GetName(),Transition?(Transition->IsAutomaticBotObjective()?TEXT("auto"):TEXT("not-auto")):TEXT("escape"));
    }
    UE_LOG(LogTemp,Display,TEXT("[BotExits] %s: %d exit(s):%s"),*GetNameSafe(GM->GetWorld()),Objectives.Num(),*Names);
}
bool UProjectMatchBots::KnowsExit(FBotPlayState& State,const FProjectMatchBotSpec& Spec,const AActor* Exit,const ProjectBots::Difficulty& Level)
{
    if(!Exit)return false;
    if(!State.ExitsSeeded)
    {
        // A new floor: how much of this building the bot already knows depends on its level.
        ScanObjectives();State.ExitsSeeded=true;
        for(const auto& Entry:Objectives)if(const AActor* Objective=Entry.Get())
            if(ProjectBots::KnowsExitAtStart(Level,HashCombineFast(GetTypeHash(Spec.BotId),GetTypeHash(Objective->GetFName()))))State.KnownExits.Add(Entry);
    }
    return State.KnownExits.Contains(Exit);
}
AActor* UProjectMatchBots::DiscoverExits(FBotPlayState& State,const FProjectMatchBotSpec& Spec,TArrayView<APawn* const> Eyes,const ProjectBots::Difficulty& Level,double Now)
{
    if(Now<State.ExitScanAt)return nullptr;
    State.ExitScanAt=Now+.5;ScanObjectives();AActor* Found=nullptr;
    for(const auto& Entry:Objectives)if(AActor* Objective=Entry.Get())
    {
        if(KnowsExit(State,Spec,Objective,Level))continue;
        // Exits are noticed with peripheral vision too: distance and a clear line, no view cone.
        for(APawn* Eye:Eyes)if(Eye&&Eye->GetController()&&FVector::DistSquared(Eye->GetActorLocation(),Objective->GetActorLocation())<FMath::Square(1800.f)&&
            Eye->GetController()->LineOfSightTo(Objective))
        {State.KnownExits.Add(Entry);if(!Found)Found=Objective;break;}
    }
    return Found;
}
AActor* UProjectMatchBots::NearestKnownExit(FBotPlayState& State,const FProjectMatchBotSpec& Spec,const FVector& From,const ProjectBots::Difficulty& Level)
{
    ScanObjectives();AActor* Exit=nullptr;float Best=MAX_flt;
    for(const auto& Entry:Objectives)if(AActor* Objective=Entry.Get())if(KnowsExit(State,Spec,Objective,Level))
    {const float D=FVector::Dist2D(Objective->GetActorLocation(),From);if(D<Best){Best=D;Exit=Objective;}}
    return Exit;
}
bool UProjectMatchBots::ChooseExploreGoal(APawn* Pawn,FBotPlayState& State,double Now,FVector& Goal)
{
    const FVector Here=Pawn->GetActorLocation();
    if(State.Visited.IsEmpty()||FVector::DistSquared2D(State.Visited.Last(),Here)>FMath::Square(600.f))
    {State.Visited.Add(Here);if(State.Visited.Num()>8)State.Visited.RemoveAt(0);}
    if(Now<State.ExploreUntil&&FVector::DistSquared2D(Here,State.ExploreGoal)>FMath::Square(200.f)){Goal=State.ExploreGoal;return true;}
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn->GetWorld());if(!Nav)return false;
    // Wander like someone looking for the way out: somewhere a fair distance off, not where they have been.
    FVector Best=FVector::ZeroVector;float BestScore=-1;
    for(int32 Try=0;Try<6;++Try)
    {
        FNavLocation Point;if(!Nav->GetRandomReachablePointInRadius(Here,3200.f,Point))continue;
        const float Far=FVector::Dist2D(Point.Location,Here);if(Far<1200.f)continue;
        float Nearest=MAX_flt;for(const FVector& Seen:State.Visited)Nearest=FMath::Min(Nearest,FVector::Dist2D(Seen,Point.Location));
        const float Score=FMath::Min(Nearest,2000.f)+.2f*Far;
        if(Score>BestScore){BestScore=Score;Best=Point.Location;}
    }
    if(BestScore<0)return false;
    State.ExploreGoal=Best;State.ExploreUntil=Now+20;Goal=Best;return true;
}
void UProjectMatchBots::QueuePing(FBotPlayState& State,const ProjectBots::Difficulty& Level,const ProjectBots::Persona& Who,uint8 Kind,const FVector& At,double Now,float U)
{
    // One call-out at a time, a few seconds apart; sometimes the bot simply does not think of it.
    if(Now<State.PingReadyAt||State.PingDueAt>=0||FMath::FRand()>ProjectBots::PingHabit(Level,Who))return;
    const float Error=ProjectBots::PingError(Level);
    State.PingKind=Kind;State.PingPoint=At+FVector(FMath::FRandRange(-Error,Error),FMath::FRandRange(-Error,Error),0);
    State.PingDueAt=Now+ProjectBots::PingDelay(Level,U);
}
void UProjectMatchBots::FlushPing(AMultiplayTestPlayerController* PC,FBotPlayState& State,double Now)
{
    if(State.PingDueAt<0||Now<State.PingDueAt)return;
    State.PingDueAt=-1;State.PingReadyAt=Now+8+4*FMath::FRand();
    if(auto* GM=Mode())GM->TryBroadcastSurvivorPing(PC,static_cast<EProjectProject01PingType>(State.PingKind),State.PingPoint);
}
float UProjectMatchBots::ConditionOf(const FProjectMatchBotSpec& Spec)const
{
    const auto* GM=Mode();
    return ProjectBots::MatchCondition(HashCombineFast(GetTypeHash(Spec.BotId),GetTypeHash(MatchId)),GM?GM->GetElapsedMatchSeconds():0.);
}
void UProjectMatchBots::HearPing(AMultiplayTestPlayerController* Sender,uint8 Type,const FVector& Location)
{
    auto* GM=Mode();if(!GM||!Sender)return;
    ScanObjectives();
    const double Now=GM->GetWorld()->GetTimeSeconds();const APawn* From=Sender->GetPawn();
    for(const auto& Spec:Specs)if(Spec.Role==TEXT("Survivor"))
    {
        auto* PC=Controllers.FindRef(Spec.BotId).Get();if(!PC||PC==Sender)continue;
        APawn* Pawn=PC->GetPawn();auto* B=Pawn?Brains.Find(Pawn):nullptr;if(!B||!B->Initialized)continue;
        auto& State=Plays.FindOrAdd(Spec.BotId);const auto& Level=B->Policy.Profile;
        if(FMath::FRand()>ProjectBots::PingResponse(Level))continue; // Missed it.
        // Turn to look at the marker after a human beat.
        B->GlanceDirection=(Location-Pawn->GetActorLocation()).GetSafeNormal2D();
        B->GlanceUntil=Now+Level.Reaction+.8+.6*FMath::FRand();
        // A marker on an exit teaches it; a danger marker near a mannequin is a teammate's sighting.
        for(const auto& Entry:Objectives)if(const AActor* Objective=Entry.Get())
            if(FVector::DistSquared2D(Objective->GetActorLocation(),Location)<FMath::Square(600.f)){KnowsExit(State,Spec,Objective,Level);State.KnownExits.Add(Entry);}
        if(Type==static_cast<uint8>(EProjectProject01PingType::Danger))
            for(TActorIterator<AMannequinAICharacter> It(GM->GetWorld());It;++It)if(FVector::DistSquared2D(It->GetActorLocation(),Location)<FMath::Square(500.f))
            {
                auto* Slot=B->Threats.FindByPredicate([&](const FProjectMatchBotBrain::FSeenThreat& T){return T.Actor.Get()==*It;});
                if(!Slot&&B->Threats.Num()<6)Slot=&B->Threats.AddDefaulted_GetRef();
                if(Slot){Slot->Actor=*It;Slot->Position=Location;Slot->At=Now;}
                break;
            }
        if(Type==static_cast<uint8>(EProjectProject01PingType::Help)&&From){State.HelpPoint=From->GetActorLocation();State.HelpUntil=Now+8;}
    }
}
FVector UProjectMatchBots::InsideGoal(const AActor* Objective)
{
    if(!Objective)return FVector::ZeroVector;
    if(const FVector* Cached=InsideGoals.Find(Objective))return *Cached;
    const FBox Box=Objective->GetComponentsBoundingBox(true);FVector Result=Objective->GetActorLocation();
    if(Box.IsValid)if(auto* Nav=Mode()?FNavigationSystem::GetCurrent<UNavigationSystemV1>(Mode()->GetWorld()):nullptr)
    {
        // Centre first, then a 3x3 grid; keep the first walkable point that really is inside the box.
        const FVector Center=Box.GetCenter(),Extent=Box.GetExtent();bool Found=false;
        for(int32 I=0;I<10&&!Found;++I)
        {
            const FVector Try=I==0?Center:Center+FVector(Extent.X*.6f*((I-1)%3-1),Extent.Y*.6f*((I-1)/3-1),0);
            FNavLocation Point;
            if(Nav->ProjectPointToNavigation(FVector(Try.X,Try.Y,Box.Min.Z+60.f),Point,FVector(60,60,Extent.Z+300.f))&&
                FMath::Abs(Point.Location.X-Center.X)<Extent.X-20.f&&FMath::Abs(Point.Location.Y-Center.Y)<Extent.Y-20.f)
            {Result=Point.Location;Found=true;}
        }
        if(!Found)Result=FVector(Center.X,Center.Y,Box.Min.Z+60.f);
    }
    InsideGoals.Add(Objective,Result);
    return Result;
}
AActor* UProjectMatchBots::ChooseObjective(APawn* Pawn,const FProjectMatchBotBrain& B,const FProjectMatchBotSpec& Spec,bool Varied,const AActor* Avoid,FVector& Goal,FVector& Look)
{
    ScanObjectives();
    const double Now=Mode()->GetWorld()->GetTimeSeconds();
    // Danger is the last real sighting of the mannequin, kept a little longer for route planning.
    const bool Danger=B.Target.IsValid()&&Now-B.SeenAt<15.;
    AActor* Best=nullptr;float BestScore=MAX_flt;auto& Knowledge=Plays.FindOrAdd(Spec.BotId);
    for(const auto& Entry:Objectives)if(auto* Objective=Entry.Get())
    {
        if(!KnowsExit(Knowledge,Spec,Objective,B.Policy.Profile))continue; // Only exits it has found.
        if(const double* Until=Knowledge.UnreachableUntil.Find(Objective);Until&&Now<*Until)continue; // No way there just now.
        const auto* Transition=Cast<AProjectProject01LevelTransitionTrigger>(Objective);
        auto* Elevator=Cast<AProjectProject01Elevator>(Objective);FVector Candidate=Objective->GetActorLocation(),Facing=Candidate;
        if(Elevator){if(!Elevator->GetBotObjective(Pawn,Candidate,Facing))continue;}
        else if(Transition&&!Transition->IsAutomaticBotObjective())continue;
        // Volumes trigger on overlap: aim at a walkable point inside the box, not the centre snapped to the floor.
        if(!Elevator)Candidate=Facing=InsideGoal(Objective);
        float Score=FVector::Dist(Pawn->GetActorLocation(),Candidate);
        if(Varied)
        {
            // Experienced players weigh danger and spread out instead of all running to the nearest exit.
            if(Danger&&FVector::Dist2D(Candidate,B.Known)<900.f)Score+=1500.f;
            if(Objective==Avoid)Score+=600.f;
            Score+=static_cast<float>((GetTypeHash(Spec.BotId)^Objective->GetUniqueID())%1000u)*.3f; // Personal route taste.
        }
        if(Score<BestScore){BestScore=Score;Best=Objective;Goal=Candidate;Look=Facing;}
    }
    return Best;
}
int32 UProjectMatchBots::TeamPlays(const FString& Role,const FString& Except)const
{
    int32 Count=0;
    for(const auto& Spec:Specs)if(Spec.Role==Role&&Spec.BotId!=Except)if(const auto* State=Plays.Find(Spec.BotId))Count+=State->Director.ActiveCount();
    return Count;
}
void UProjectMatchBots::TrackRelief(double Now)
{
    auto* GM=Mode();
    for(const auto& Pair:GM->MatchPlayerRecords)
    {
        const auto& Record=Pair.Value;auto& Watch=RecordWatch.FindOrAdd(Record.UserId);
        const auto State=static_cast<uint8>(Record.SurvivorState);
        const int32 Deaths=Record.SurvivorPawn.IsValid()?Record.SurvivorPawn->GetRemainingDeathCount():Watch.Deaths;
        constexpr auto Active=static_cast<uint8>(EMultiplayTestSurvivorState::Active);
        if(!Record.bServerBot&&Record.Role==TEXT("Survivor"))
        {
            // A human survivor losing lives or getting eliminated is a setback against the mannequin bot.
            if(Watch.Deaths>=0&&Deaths<Watch.Deaths)MannequinRelief.Add(Now,1.f);
            if(Watch.State==Active&&Record.SurvivorState==EMultiplayTestSurvivorState::Eliminated)MannequinRelief.Add(Now,1.5f);
        }
        if(Record.bServerBot&&Record.Role==TEXT("Survivor")&&Watch.State==Active&&
            (Record.SurvivorState==EMultiplayTestSurvivorState::Escaped||Record.SurvivorState==EMultiplayTestSurvivorState::TransitionWaiting))
            SurvivorRelief.Add(Now,1.2f); // A bot survivor got away from the human mannequin.
        if(!Record.bServerBot&&Record.Role==TEXT("Mannequin"))
        {
            // A human mannequin with no catch for four minutes is also struggling.
            if(Record.CaptureCount!=Watch.Captures||Watch.CapturedAt<=0){Watch.Captures=Record.CaptureCount;Watch.CapturedAt=Now;}
            else if(Now-Watch.CapturedAt>240.){SurvivorRelief.Add(Now,.8f);Watch.CapturedAt=Now;}
        }
        Watch.Deaths=Deaths;Watch.State=State;
    }
}

void UProjectMatchBots::TickSurvivor(AMultiplayTestPlayerController* PC,const FProjectMatchBotSpec& Spec,float Delta)
{
    using namespace ProjectBots;
    auto* Pawn=Cast<AProjectMatchBotCharacter>(PC->GetPawn());if(!Pawn||Pawn->IsGameOver()||PC->GetSurvivorState()!=EMultiplayTestSurvivorState::Active)return;
    auto* GM=Mode();auto* World=GM->GetWorld();const double Now=World->GetTimeSeconds();auto& B=Brains.FindOrAdd(Pawn);
    InitializeBrain(B,Spec,0);
    // Today's form: a small swing inside the level's band (never reaching a neighbouring level).
    if(Now>=B.ConditionAt){B.ConditionAt=Now+5;B.Policy.Profile=Difficulty::For(Spec.Difficulty,ConditionOf(Spec));}
    const auto Level=B.Policy.Profile;const float ReliefNow=SurvivorRelief.Value(Now,Level);B.Policy.Relief=ReliefNow;
    auto& Mine=Plays.FindOrAdd(Spec.BotId);LoadExperience(Mine,Spec,Level);auto& Dir=Mine.Director;Dir.Update(Level,Now,ReliefNow);
    // Play style decides how readily this bot reaches for each play; the Director still enforces level, tells and cooldowns.
    const uint8 StyleId=static_cast<uint8>(StyleFor(Persona::From(GetTypeHash(Spec.BotId)*2654435761u+1u),Spec.Role==TEXT("Mannequin")));
    // Self-made combos add eagerness for the learned follow-up right after its opener.
    // In the endgame the last survivor reaches for the running plays and hides less.
    float Urgency=0;
    // The endgame push and the team's match plan scale the style's own preference (multiplied, not added),
    // so a disliked play stays disliked. The plan strength is set once the team is known.
    const MatchScript Script=ScriptOf();float PlanStrength=0;
    auto Want=[&](Play Kind){return FMath::FRand()<PlayAffinity(StyleId,Kind)*(1.f+(Kind==Play::RunForIt||Kind==Play::ZigZag?.5f*Urgency:Kind==Play::HideStill?-.5f*Urgency:0.f))*
        PlanAffinity(Script,false,Kind,PlanStrength)+Mine.Combos.FollowUp(Kind,Now,Level);};
    APlayerCharacter* Buddy=nullptr;AMultiplayTestPlayerController* BuddyPC=nullptr;bool HumanAlly=false,BotBuddy=false;FString BuddyBotId;
    for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Key.Get()!=PC&&Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active&&Pair.Value.SurvivorPawn.IsValid())
    {
        Buddy=Pair.Value.SurvivorPawn.Get();BuddyPC=Pair.Key.Get();HumanAlly=!Pair.Value.bServerBot;BotBuddy=Pair.Value.bServerBot;
        if(BotBuddy&&Pair.Value.UserId.StartsWith(TEXT("Bot:")))BuddyBotId=Pair.Value.UserId.RightChop(4);
        break;
    }
    // Level-scaled team tactics and psychological plays only when no human is on this team.
    // Beside a human, bots stay helpers (see Policy: no deception, short cover).
    const bool TeamPlay=!HumanAlly;
    // Survivor plan: together suits careful/supportive characters, split suits bold runners. Beside a human the
    // bot stays a helper, so the plan shows only lightly.
    {
        const bool Together=Script.Survivors==SurvivorPlan::Together;
        PlanStrength=MatchScript::Commit(Level)*MatchScript::Fit(B.Policy.Character,!Together)*(TeamPlay?1.f:.4f);
        B.Policy.Cohesion=(Together?1.f:-1.f)*PlanStrength;
    }
    // Endgame: the other survivor is caught, escaped or through the exit -> hurry out.
    {
        int32 Survivors=0,ActiveSurvivors=0;
        for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Value.Role==TEXT("Survivor")){++Survivors;ActiveSurvivors+=Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active;}
        Urgency=EndgameUrgency(Level,Survivors,ActiveSurvivors);B.Policy.Urgency=Urgency;
    }
    const bool Rush=Urgency>0;
    // Deeper floors: firmer judgement and a little boldness all the time (the endgame rush stays separate).
    const float Resolve=FloorResolve(Level,FloorDepth);B.Policy.Resolve=Resolve;
    // Near a known exit: the last stretch is run with commitment, and with tunnel vision (fewer looks back,
    // less care with the route) that a mannequin waiting by the exit can punish.
    float ExitPull=0;
    if(const AActor* Exit=Mine.Objective.Get();Exit&&KnowsExit(Mine,Spec,Exit,Level))
        ExitPull=FMath::Min(1.f,ExitProximityBoldness(Level,FVector::Dist2D(Exit->GetActorLocation(),Pawn->GetActorLocation()))*BoldnessScale(B.Policy.Character));
    // Last survivor: choose a way out (ChooseLoneApproach) and rethink it when its exit becomes guarded or clears.
    LoneApproach Way=LoneApproach::Dash;
    if(Rush)
    {
        if(Now>=Mine.LoneCheckAt)
        {
            Mine.LoneCheckAt=Now+1.;
            AActor* Target=Mine.Objective.Get();if(!Target||!KnowsExit(Mine,Spec,Target,Level))Target=NearestKnownExit(Mine,Spec,Pawn->GetActorLocation(),Level);
            // Guarded: a mannequin it still remembers stands within 15 m of that exit.
            bool Guarded=false;if(Target)for(const auto& Threat:B.Threats)Guarded|=FVector::Dist2D(Threat.Position,Target->GetActorLocation())<1500.f;
            int32 KnownExits=0;for(const auto& Entry:Objectives)KnownExits+=Entry.IsValid()&&KnowsExit(Mine,Spec,Entry.Get(),Level);
            const bool Rethink=Mine.Lone==255||(Guarded&&!Mine.LoneGuarded);
            if(Rethink)
            {
                Mine.Lone=static_cast<uint8>(ChooseLoneApproach(Level,B.Policy.Character,Guarded,KnownExits>=2,B.Policy.Random()));
                Mine.LoneSince=Now;Mine.LoneAvoid=Guarded?Target:nullptr;
                const FVector Here0=Pawn->GetActorLocation(),ExitAt=Target?Target->GetActorLocation():Here0;FVector Point;
                if(Mine.Lone==static_cast<uint8>(LoneApproach::Lurk))
                {
                    // A covered spot short of the exit, out of the guard's sight.
                    const FVector Short=Here0+(ExitAt-Here0)*.55f;
                    Mine.LonePoint=ChooseTacticPoint(Pawn,B,Action::Cover,false,700.f,Point,&Short,nullptr,false,223)?Point:Short;
                }
                else if(Mine.Lone==static_cast<uint8>(LoneApproach::Decoy))
                {
                    // Somewhere loud and away from the exit to pull the guard after it.
                    const FVector Away=(Here0-ExitAt).GetSafeNormal2D();
                    Mine.LonePoint=ChooseTacticPoint(Pawn,B,Action::Hold,false,900.f,Point,nullptr,&Away,false,224)?Point:Here0+Away*900.f;
                }
            }
            else if(!Guarded&&Mine.LoneGuarded&&(Mine.Lone==static_cast<uint8>(LoneApproach::Lurk)||Mine.Lone==static_cast<uint8>(LoneApproach::Decoy)))
                Mine.Lone=static_cast<uint8>(LoneApproach::Dash); // The guard left (or is forgotten): go now.
            Mine.LoneGuarded=Guarded;
            // A lurker only waits so long; a decoy run ends at its point or after 8 s.
            if(Mine.Lone==static_cast<uint8>(LoneApproach::Lurk)&&Now-Mine.LoneSince>LurkPatience(Level,B.Policy.Character))Mine.Lone=static_cast<uint8>(LoneApproach::Dash);
            if(Mine.Lone==static_cast<uint8>(LoneApproach::Decoy)&&(Now-Mine.LoneSince>8.||FVector::DistSquared2D(Pawn->GetActorLocation(),Mine.LonePoint)<FMath::Square(250.f)))
                Mine.Lone=static_cast<uint8>(LoneApproach::Dash);
        }
        Way=Mine.Lone<4?static_cast<LoneApproach>(Mine.Lone):LoneApproach::Dash;
    }
    else Mine.Lone=255;
    // Careful characters (sneaks) still hurry at the end, but at a walk unless something is after them.
    // A decoy run is loud on purpose; lurking and detouring are done quietly.
    const bool RushRun=Rush&&(Way==LoneApproach::Decoy||(Way==LoneApproach::Dash&&(BoldnessScale(B.Policy.Character)>=.85f||B.Visible)));
    const bool Lurking=Rush&&Way==LoneApproach::Lurk&&FVector::DistSquared2D(Pawn->GetActorLocation(),Mine.LonePoint)<FMath::Square(150.f);
    B.Policy.Urgency=FMath::Max3(Urgency,.3f*Resolve,.6f*ExitPull);
    // The emergency elevator only comes up when called, so a bot uses it only if it has found it like any exit.
    if(Now>=B.SenseAt)
    {
        B.SenseAt=Now+.2;AMannequinAICharacter* Target=nullptr;float Best=MAX_flt;
        for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(Sees(Pawn,*It))
        {
            float D=FVector::DistSquared(Pawn->GetActorLocation(),It->GetActorLocation());if(D<Best){Best=D;Target=*It;}
            // Remember every mannequin in sight, not only the nearest.
            auto* Slot=B.Threats.FindByPredicate([&](const FProjectMatchBotBrain::FSeenThreat& T){return T.Actor.Get()==*It;});
            if(!Slot&&B.Threats.Num()<6)Slot=&B.Threats.AddDefaulted_GetRef();
            if(Slot){Slot->Actor=*It;Slot->Position=It->GetActorLocation();Slot->At=Now;}
        }
        // Forget threats past this level's memory; note which ones a teammate is freezing right now.
        B.Threats.RemoveAll([&](const FProjectMatchBotBrain::FSeenThreat& T){return !T.Actor.IsValid()||Now-T.At>B.Policy.Profile.Memory;});
        for(auto& Threat:B.Threats)
        {
            const auto* Body=Cast<AMannequinAICharacter>(Threat.Actor.Get());
            Threat.Covered=Body&&BuddyPC&&GM->CanSurvivorSeeMannequin(BuddyPC,Body);
        }
        Sense(Pawn,Target,2,B,Target!=nullptr,HumanAlly);
        if(!B.Visible&&B.Policy.Random()<B.Policy.Profile.Sharing)
            for(const auto& Entry:Shared.Entries)if(Entry.Seen.Team==2&&Entry.Seen.Target&&Entry.Seen.At>=0&&Now-Entry.Seen.At<=B.Policy.Profile.Memory)
                for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(It->GetUniqueID()==Entry.Seen.Target)
                {if(B.Target.Get()!=*It||Now-B.SeenAt>B.Policy.Profile.Memory)B.ReactAt=Now+B.Policy.Profile.Reaction*(HumanAlly?1.25:1.);B.Target=*It;B.Known=U(Entry.Seen.Position);B.SeenAt=Entry.Seen.At;break;}
    }
    // Combo feedback: a play worked for the survivor if it ended with no unfrozen mannequin in reach.
    {
        bool Safe=true;
        for(const auto& Threat:B.Threats)if(!Threat.Covered&&FVector::Dist2D(Threat.Position,Pawn->GetActorLocation())<GM->MultiplayerDetectionRadius+250.f){Safe=false;break;}
        TrackLearning(Mine,Level,Now,Safe);
    }
    // Map knowledge: exits it sees; a bot partner's finds pass on by level. A human partner hears about it by ping.
    {
        const bool Scan=Now>=Mine.ExitScanAt;APawn* Eye=Pawn;
        if(AActor* Found=DiscoverExits(Mine,Spec,TArrayView<APawn* const>(&Eye,1),Level,Now);Found&&HumanAlly)
            QueuePing(Mine,Level,B.Policy.Character,static_cast<uint8>(EProjectProject01PingType::Location),Found->GetActorLocation(),Now,FMath::FRand());
        if(Scan&&BotBuddy)if(const auto* Partner=Plays.Find(BuddyBotId))
            for(const auto& Exit:Partner->KnownExits)if(Exit.IsValid()&&!KnowsExit(Mine,Spec,Exit.Get(),Level)&&B.Policy.Random()<.2f*Level.Sharing)Mine.KnownExits.Add(Exit);
    }
    FlushPing(PC,Mine,Now);
    const float Mistake=MistakeScale(Level);
    // Hearing: a moving mannequin's footsteps make a survivor stop and look that way.
    if(!B.Visible&&Now>=B.HearAt)
    {
        B.HearAt=Now+.25;
        for(TActorIterator<AMannequinAICharacter> It(World);It;++It)
        {
            FVector Heard;
            if(Hear(Pawn,*It,Mistake,Heard))
            {
                // Hide still (2+, mostly sneaks): freeze and go silent instead of turning to look.
                if((Want(Play::HideStill)&&Dir.Start(Play::HideStill,Level,Now,ReliefNow,0))||(Want(Play::Listen)&&Dir.Start(Play::Listen,Level,Now,ReliefNow,0)))Mine.Point=Heard;
                break;
            }
        }
    }
    auto C=Context(B,false);C.HasAlly=Buddy!=nullptr;C.HumanAlly=HumanAlly;C.Stamina=Pawn->GetCurrentStamina()/FMath::Max(1.f,Pawn->GetMaxStamina());
    // Multiplayer has zero EXTRA death counts: zero means alive until caught, not critically hurt.
    C.Health=Pawn->IsGameOver()?0.f:1.f;
    if(C.Visible||C.Remembered)C.Range=FMath::Clamp(FVector::Dist2D(Pawn->GetActorLocation(),B.Known)/2400.f,0.f,1.f);
    if(Buddy){C.AllyDistance=FMath::Clamp(FVector::Dist2D(Pawn->GetActorLocation(),Buddy->GetActorLocation())/1200.f,0.f,1.f);C.AllyThreat=Buddy->GetRemainingDeathCount()<Pawn->GetRemainingDeathCount()?.8f:.3f;}
    // Bait runner partner: while the other bot draws the mannequin off, this one just runs the exit.
    const bool Runner=TeamPlay&&Now<BaitUntil&&!BaitBotId.IsEmpty()&&BaitBotId==BuddyBotId;
    uint16 Allowed=static_cast<uint16>(0x3ff&~(1<<static_cast<int>(Action::Pressure)));
    if(Runner)Allowed=static_cast<uint16>((1<<static_cast<int>(Action::Search))|(1<<static_cast<int>(Action::Recover)));
    const auto Tactic=B.Policy.Tick(C,Now,Allowed).Tactic;
    const bool Knows=C.Visible||C.Remembered;
    const auto& Quirks=B.Policy.Character;const float Skill=Level.Skill;
    // Fear: a near miss or a sudden close sighting startles; composure decides how much of it shows.
    Mine.Mood.Decay(Now);
    {
        float Nearest=MAX_flt;FVector NearestAt=FVector::ZeroVector;
        for(const auto& Threat:B.Threats)if(!Threat.Covered){const float D=FVector::Dist2D(Threat.Position,Pawn->GetActorLocation());if(D<Nearest){Nearest=D;NearestAt=Threat.Position;}}
        const bool Near=Nearest<GM->MultiplayerManualAttackRange+250.f;
        if(Near&&!B.WasNear)Mine.Mood.Startle(Now,.7f);
        else if(C.Visible&&!B.HadKnowledge&&Nearest<1200.f)Mine.Mood.Startle(Now,.35f);
        // Calling it out to a human partner: a fresh sighting, or a call for help when cornered.
        if(HumanAlly&&C.Visible&&!B.HadKnowledge&&Nearest<MAX_flt)
            QueuePing(Mine,Level,Quirks,static_cast<uint8>(EProjectProject01PingType::Danger),NearestAt,Now,FMath::FRand());
        if(HumanAlly&&Near&&Mine.Mood.Panic(Level,Quirks)>.5f)
            QueuePing(Mine,Level,Quirks,static_cast<uint8>(EProjectProject01PingType::Help),Pawn->GetActorLocation(),Now,FMath::FRand());
        B.WasNear=Near;
    }
    const float Panic=Mine.Mood.Panic(Level,Quirks);
    if(Panic>.35f)B.Shaken=true;
    // "Phew": once the scare fades with nothing in sight, a short stop to catch breath and a look back.
    if(B.Shaken&&Mine.Mood.Fear<.15f&&!Knows)
    {
        B.Shaken=false;B.BreatheUntil=Now+.4f+.5f*B.Policy.Random();
        B.GlanceDirection=(B.Known-Pawn->GetActorLocation()).GetSafeNormal2D();B.GlanceUntil=B.BreatheUntil+.3f;
    }
    // Lost sight: look back once at where the threat vanished, like a person double-checking.
    if(B.HadKnowledge&&!Knows){B.GlanceDirection=(B.Known-Pawn->GetActorLocation()).GetSafeNormal2D();B.GlanceUntil=Now+.6f+.6f*Quirks.Curiosity;}
    B.HadKnowledge=Knows;
    // Nervous shoulder checks while nothing is known. Beginners check far more often than experts.
    // A scared person checks over the shoulder far more often.
    // Near the exit the eyes stay ahead: fewer shoulder checks (tunnel vision).
    auto ScheduleGlance=[&](){B.NextGlanceAt=Now+(1.8f+3.5f*Skill)*(1.4f-.8f*Quirks.Curiosity)*(.7f+.6f*B.Policy.Random())*(1.f-.6f*Panic)*(1.f+ExitPull);};
    if(B.NextGlanceAt<=0)ScheduleGlance();
    else if(!Knows&&Now>=B.NextGlanceAt&&Now>=B.GlanceUntil)
    {
        const float Turn=(100.f+70.f*B.Policy.Random())*(B.Policy.Random()<.5f+.25f*Quirks.Handedness?1.f:-1.f);
        B.GlanceDirection=Pawn->GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(Turn,FVector::UpVector);
        B.GlanceUntil=Now+.35f+.4f*B.Policy.Random();ScheduleGlance();
    }
    // Start team plays (each with its own tell and cooldown). Checked twice a second; experts commit more readily.
    if(TeamPlay&&Now>=B.PlayCheckAt)
    {
        B.PlayCheckAt=Now+.5;
        const int32 Team=TeamPlays(Spec.Role,Spec.BotId);
        if(B.Policy.Random()<.3f+.4f*Skill)
        {
            if(!Knows&&BotBuddy&&FVector::Dist2D(Pawn->GetActorLocation(),Buddy->GetActorLocation())<700.f)Want(Play::BackToBack)&&Dir.Start(Play::BackToBack,Level,Now,ReliefNow,Team);
            else if(Knows&&BotBuddy&&Now>=BaitUntil&&FVector::Dist2D(Pawn->GetActorLocation(),B.Known)<=FVector::Dist2D(Buddy->GetActorLocation(),B.Known))
            {
                // The partner nearer the mannequin draws it off.
                if(Want(Play::BaitRunner)&&Dir.Start(Play::BaitRunner,Level,Now,ReliefNow,Team)){BaitBotId=Spec.BotId;BaitUntil=Dir.Find(Play::BaitRunner)->Ends;}
            }
            else if(!Runner&&B.Target.IsValid()&&Now-B.SeenAt<10.)
            {
                FVector GoalA,LookA,GoalB,LookB;
                if(AActor* First=ChooseObjective(Pawn,B,Spec,false,nullptr,GoalA,LookA))
                    if(AActor* Second=ChooseObjective(Pawn,B,Spec,true,First,GoalB,LookB);Second&&Second!=First&&Want(Play::ExitFeint)&&Dir.Start(Play::ExitFeint,Level,Now,ReliefNow,Team))
                    {Mine.Point=GoalA;Mine.Point2=GoalB;Mine.Objective=Second;}
            }
        }
    }
    // Peek -> dash: the corner peek is the tell; the sprint across follows it.
    const bool Peeking=Now<B.PauseUntil;
    if(B.WasPeeking&&!Peeking&&!Knows)Want(Play::PeekDash)&&Dir.Start(Play::PeekDash,Level,Now,ReliefNow,TeamPlays(Spec.Role,Spec.BotId));
    B.WasPeeking=Peeking;
    if(Knows&&Dir.Active(Play::BackToBack))Dir.Finish(Play::BackToBack,Level,Now,ReliefNow);
    if(Knows&&Dir.Active(Play::HideStill))Dir.Finish(Play::HideStill,Level,Now,ReliefNow); // Spotted: hiding is over.
    if(Dir.Active(Play::BaitRunner)&&(!Knows||!Buddy)){Dir.Finish(Play::BaitRunner,Level,Now,ReliefNow);if(BaitBotId==Spec.BotId)BaitUntil=0;}
    if(Dir.Executing(Play::Listen,Now)&&(Knows||Now>=Dir.Find(Play::Listen)->SignalEnds+1.5))Dir.Finish(Play::Listen,Level,Now,ReliefNow);

    // Survivor craft (see WatchDiscipline): keep a mannequin in view while moving, back away from danger,
    // leave threats a teammate is already freezing to them. Discipline is re-rolled per short window by
    // level; lapses are the deliberate openings the mannequin can punish.
    // Panic breaks discipline (forgets to keep it in view, forgets to check the route).
    if(Now>=B.DisciplineUntil){B.DisciplineUntil=Now+2.5+1.5*B.Policy.Random();B.Disciplined=B.Policy.Random()<WatchDiscipline(Level)*(1.f-.6f*Panic)*(1.f-.3f*ExitPull);}
    TArray<ThreatView,TInlineAllocator<6>> Views;
    for(const auto& Threat:B.Threats)Views.Add({V(Threat.Position),static_cast<float>(FVector::Dist2D(Pawn->GetActorLocation(),Threat.Position)),Threat.Covered});
    const int32 WatchIndex=PickWatchTarget(Views.GetData(),Views.Num(),2000.f);
    // Same gaze budget as the policy's cover limit: hold the stare, then look away for a beat. Without it a
    // disciplined pair keeps the mannequin frozen for good and the match stalls at every level.
    bool Watching=WatchIndex>=0&&B.Disciplined&&!InWatchLapse(Now,Quirks.Phase,Level)&&Now>=B.LookAwayUntil;
    if(!Watching){if(Now>=B.LookAwayUntil)B.WatchSince=-1;}
    else if(B.WatchSince<0)B.WatchSince=Now;
    else if(Now-B.WatchSince>GazeHoldLimit(Level,HumanAlly))
    {
        B.LookAwayUntil=Now+LookAwaySeconds(Level)*(.8f+.4f*B.Policy.Random());B.WatchSince=-1;Watching=false;
        // Eyes go to the way ahead (or a quick look around), not back to the mannequin; skilled survivors also check behind.
        B.GlanceDirection=Pawn->GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis((B.Policy.Random()-.5f)*(120.f+180.f*Level.Skill),FVector::UpVector);
    }
    const bool LookingAway=Now<B.LookAwayUntil;
    const FVector ObjectiveAt=Mine.Objective.IsValid()?Mine.Objective->GetActorLocation():Pawn->GetActorLocation();
    const float DangerRadius=GM->MultiplayerDetectionRadius+250.f+350.f*Level.Skill;
    const Vec Retreat=B.Disciplined?RetreatDirection(V(Pawn->GetActorLocation()),Views.GetData(),Views.Num(),DangerRadius,V(ObjectiveAt)):Vec{};
    const bool Danger=Retreat.Length()>.5f;
    // Partner relay: every nearby threat is being frozen by the teammate, so this one makes ground.
    const bool Relay=!Danger&&!Views.IsEmpty()&&WatchIndex<0&&Buddy;
    // Out of its reach and only watching it, or nothing in sight at all: keep making for the exit instead of
    // standing in a staring contest or looking around on the spot. A helper beside a human still gives its
    // short cover while a mannequin is in view; catching breath with nothing around is done at a walk.
    const bool Progress=!Danger&&(Tactic==Action::Observe||Tactic==Action::Hold||(Tactic==Action::Cover&&(!HumanAlly||!Knows))||(!Knows&&Tactic==Action::Recover));
    const FVector Here=Pawn->GetActorLocation();
    // Run headings never lead into another known mannequin; only a disciplined moment checks (level-scaled).
    // A panicked run drifts off the chosen line.
    const float PanicDrift=50.f*Panic*FMath::Sin(static_cast<float>(Now)*.7f+Quirks.Phase);
    auto SafeHeading=[&](const FVector& Wanted){return (B.Disciplined?U(ClearHeading(V(Here),V(Wanted),Views.GetData(),Views.Num(),1500.f+700.f*Level.Skill)):Wanted.GetSafeNormal2D()).RotateAngleAxis(PanicDrift,FVector::UpVector);};
    auto ThreatOf=[&](const AActor* Actor)->const FProjectMatchBotBrain::FSeenThreat*{return Actor?B.Threats.FindByPredicate([&](const FProjectMatchBotBrain::FSeenThreat& T){return T.Actor.Get()==Actor;}):nullptr;};

    // Self-tuned main distance of a play; starts this run's trial variant if it has not begun yet.
    auto Reach=[&](Play Kind){Mine.Tuning.Begin(Kind,Level,FMath::FRand(),FMath::FRand());return Mine.Tuning.ReachScale(Kind);};
    // Pair plays run on both partners: the starter's own slot, or the partner's call recorded on this bot.
    FBotPlayState* PartnerState=BotBuddy&&!BuddyBotId.IsEmpty()?Plays.Find(BuddyBotId):nullptr;
    auto Called=[&](Play Kind){return Mine.PartnerPlay==static_cast<uint8>(Kind)&&Now<Mine.PartnerUntil;};
    auto PairActive=[&](Play Kind){return Dir.Active(Kind)||Called(Kind);};
    auto PairSignalling=[&](Play Kind){return Dir.Signalling(Kind,Now)||(Called(Kind)&&Now<Mine.PartnerStart);};
    auto PairStart=[&](Play Kind)->double{const auto* Slot=Dir.Find(Kind);return Slot?Slot->SignalEnds:Mine.PartnerStart;};
    auto CallPartner=[&](Play Kind,float Side,const FVector& PartnerPoint)
    {
        const auto* Slot=Dir.Find(Kind);if(!PartnerState||!Slot)return;
        PartnerState->PartnerPlay=static_cast<uint8>(Kind);PartnerState->PartnerStart=Slot->SignalEnds;PartnerState->PartnerUntil=Slot->Ends;
        PartnerState->PartnerLead=false;PartnerState->PartnerSide=Side;PartnerState->Point=PartnerPoint;
    };
    auto EndPair=[&](Play Kind)
    {
        if(Dir.Active(Kind)){Dir.Finish(Kind,Level,Now,ReliefNow);if(PartnerState&&PartnerState->PartnerPlay==static_cast<uint8>(Kind))PartnerState->PartnerUntil=0;}
        if(Called(Kind))Mine.PartnerUntil=0;
    };
    const float ToBuddy=Buddy?FVector::Dist2D(Here,Buddy->GetActorLocation()):MAX_flt;
    // Survivor plays (solo always, pair only with a bot partner). Same rhythm as the team check.
    if(Now>=B.SoloCheckAt)
    {
        B.SoloCheckAt=Now+.5;
        const int32 Team=TeamPlays(Spec.Role,Spec.BotId);
        const auto* Seen=WatchIndex>=0?&B.Threats[WatchIndex]:nullptr;const float SeenDistance=WatchIndex>=0?Views[WatchIndex].Distance:MAX_flt;
        const bool PartnerFree=PartnerState&&!PartnerState->Director.BodyBusy()&&Now>=PartnerState->PartnerUntil;
        if(B.Policy.Random()<.3f+.4f*Skill)
        {
            // Run for it (2+): nothing unfrozen within reach of the way -> stop, turn to the exit, then sprint all the way (loud).
            bool Clear=!Danger&&C.Stamina>.5f&&!B.Recovering;for(const auto& View:Views)Clear&=View.CoveredByPartner||View.Distance>1500.f;
            FVector ExitGoal,ExitLook;
            if(Clear)if(AActor* Exit=ChooseObjective(Pawn,B,Spec,false,nullptr,ExitGoal,ExitLook))
            {
                const float D=FVector::Dist2D(Here,ExitGoal);
                if(D>800.f&&D<4000.f*Reach(Play::RunForIt)&&Want(Play::RunForIt)&&Dir.Start(Play::RunForIt,Level,Now,ReliefNow,Team)){Mine.Point=ExitGoal;Mine.Objective=Exit;}
            }
            if(TeamPlay&&BotBuddy&&PartnerFree&&Seen&&!Danger)
            {
                const FVector ToThreat=(Seen->Position-Here).GetSafeNormal2D();
                // Split run (4+, psych): it closes on both together -> stop facing each other, then run apart and meet at the exit.
                if(SeenDistance<DangerRadius+700.f&&FVector::Dist2D(Buddy->GetActorLocation(),Seen->Position)<DangerRadius+700.f&&ToBuddy<700.f&&
                    Want(Play::SplitRun)&&Dir.Start(Play::SplitRun,Level,Now,ReliefNow,Team))
                {
                    const FVector Side=ToThreat.RotateAngleAxis(90.f,FVector::UpVector)*700.f*Reach(Play::SplitRun);
                    Mine.Point=Here+Side;Mine.Subject=Seen->Actor;CallPartner(Play::SplitRun,-1.f,Buddy->GetActorLocation()-Side);
                }
                // Leap frog (3+): a mannequin in view, partner close -> one stands facing back while the other moves, then swap.
                else if(SeenDistance>800.f&&SeenDistance<2500.f&&ToBuddy<900.f&&Mine.Objective.IsValid()&&Want(Play::LeapFrog)&&Dir.Start(Play::LeapFrog,Level,Now,ReliefNow,Team))
                {Mine.Subject=Seen->Actor;Mine.PartnerLead=true;CallPartner(Play::LeapFrog,0,FVector::ZeroVector);}
            }
        }
        if(Seen&&B.Policy.Random()<.3f+.4f*Skill)
        {
            const FVector ToThreat=(Seen->Position-Here).GetSafeNormal2D(),ToObjective=(ObjectiveAt-Here).GetSafeNormal2D();
            // Watch retreat (3+): a close mannequin -> stop, face it, back away keeping it frozen.
            if(Danger&&SeenDistance<DangerRadius&&Want(Play::WatchRetreat)&&Dir.Start(Play::WatchRetreat,Level,Now,ReliefNow,Team))Mine.Subject=Seen->Actor;
            // Zigzag (3+): chased by one it is not freezing -> a sharp side-step, then a zigzag run.
            else if(Danger&&SeenDistance<DangerRadius&&!Watching&&Want(Play::ZigZag)&&Dir.Start(Play::ZigZag,Level,Now,ReliefNow,Team)){Mine.Subject=Seen->Actor;Mine.Sweep=B.Policy.Random()<.5f?1:-1;}
            // False retreat (4+): the mannequin blocks the exit -> run off in plain view, then double back.
            else if(!Danger&&Mine.Objective.IsValid()&&SeenDistance>900.f&&SeenDistance<2500.f&&SeenDistance<FVector::Dist2D(Here,ObjectiveAt)&&
                FVector::DotProduct(ToThreat,ToObjective)>.6f&&Want(Play::FalseRetreat)&&Dir.Start(Play::FalseRetreat,Level,Now,ReliefNow,Team))
            {Mine.Point=Here+SafeHeading(-ToThreat)*900.f*Reach(Play::FalseRetreat);Mine.Point2=ObjectiveAt;Mine.Subject=Seen->Actor;}
            else if(TeamPlay&&BotBuddy&&!Danger)
            {
                const float BuddyToThreat=FVector::Dist2D(Buddy->GetActorLocation(),Seen->Position);
                // Noise lure (4+): it is closing on the partner -> run loudly the other way, then go silent.
                if(BuddyToThreat<1200.f&&SeenDistance>800.f&&SeenDistance<2200.f&&ToBuddy>600.f&&Want(Play::NoiseLure)&&Dir.Start(Play::NoiseLure,Level,Now,ReliefNow,Team))
                {Mine.Point=Here+SafeHeading(Here-Buddy->GetActorLocation())*700.f*Reach(Play::NoiseLure);Mine.Subject=Seen->Actor;}
                // Cover run (3+): freeze it with a stare so the partner can run.
                else if(SeenDistance>600.f&&SeenDistance<1800.f*Mine.Tuning.ReachScale(Play::CoverRun)&&ToBuddy<1500.f&&Want(Play::CoverRun)&&Dir.Start(Play::CoverRun,Level,Now,ReliefNow,Team))Mine.Subject=Seen->Actor;
            }
        }
    }
    // Ending rules: lost the mannequin, or a new danger takes over (only the watch retreat handles danger).
    const auto* Subject=ThreatOf(Mine.Subject.Get());
    const bool SubjectSeen=Subject&&Now-Subject->At<.6;
    if(Dir.Executing(Play::WatchRetreat,Now)&&(!Subject||FVector::Dist2D(Subject->Position,Here)>DangerRadius+400.f))Dir.Finish(Play::WatchRetreat,Level,Now,ReliefNow);
    if(Dir.Active(Play::CoverRun)&&(!SubjectSeen||Danger))Dir.Finish(Play::CoverRun,Level,Now,ReliefNow);
    if(Dir.Active(Play::NoiseLure)&&Danger)Dir.Finish(Play::NoiseLure,Level,Now,ReliefNow);
    if(Dir.Active(Play::FalseRetreat)&&(Danger||(Dir.Executing(Play::FalseRetreat,Now)&&FVector::DistSquared2D(Here,Mine.Point2)<FMath::Square(200.f))))Dir.Finish(Play::FalseRetreat,Level,Now,ReliefNow);
    if(Dir.Active(Play::RunForIt)&&(Danger||FVector::DistSquared2D(Here,Mine.Point)<FMath::Square(220.f)))Dir.Finish(Play::RunForIt,Level,Now,ReliefNow);
    if(Dir.Active(Play::ZigZag)&&!Danger&&Dir.Executing(Play::ZigZag,Now))Dir.Finish(Play::ZigZag,Level,Now,ReliefNow); // Got clear.
    if(PairActive(Play::LeapFrog)&&(Danger||!Buddy||ToBuddy>1600.f))EndPair(Play::LeapFrog);
    if(PairActive(Play::SplitRun)&&!Buddy)EndPair(Play::SplitRun);
    // Stalemate: drop the deliberate stand-still pair holds and get moving (hiding and listening stay).
    const bool Stalled=IsStalled(Level,Now);
    if(Stalled)
    {
        for(Play Kind:{Play::CoverRun,Play::BackToBack})if(Dir.Active(Kind))Dir.Finish(Kind,Level,Now,ReliefNow);
        EndPair(Play::LeapFrog);
    }
    const bool WatchRetreating=Dir.Active(Play::WatchRetreat)&&Subject;
    // Leap frog: alternating 2.2 s legs; the starter moves first. The other half stands facing back.
    const bool Leaping=PairActive(Play::LeapFrog)&&!PairSignalling(Play::LeapFrog);
    const bool LeapMove=Leaping&&((static_cast<int32>((Now-PairStart(Play::LeapFrog))/2.2)%2==0)==(Dir.Active(Play::LeapFrog)?true:Mine.PartnerLead));
    const bool LeapCover=Leaping&&!LeapMove;
    // Split run: the first 3 s run apart, then both make for the exit.
    const bool Splitting=PairActive(Play::SplitRun)&&!PairSignalling(Play::SplitRun)&&Now-PairStart(Play::SplitRun)<3.;

    FVector Goal=Pawn->GetActorLocation(),ObjectiveLook=FVector::ZeroVector;bool HasGoal=false,PlayMove=false;
    AProjectProject01Elevator* SelectedElevator=nullptr;
    const bool Baiting=Dir.Active(Play::BaitRunner);
    if(WatchRetreating)
    {
        // Tell: stops and turns to it. Then walks backwards, eyes on it, away from its reach.
        const FVector Away=SafeHeading(Here-Subject->Position);FVector Point;const float Back=500.f*Reach(Play::WatchRetreat);
        Goal=ChooseTacticPoint(Pawn,B,Action::Hold,false,Back,Point,nullptr,&Away,false,222)?Point:Here+Away*Back;
        HasGoal=PlayMove=true;
    }
    else if(Danger)
    {
        // Without the watch retreat: turn and run (beginners) - the heading still avoids other known mannequins.
        FVector Away=SafeHeading(U(Retreat));FVector Point;
        if(Dir.Active(Play::ZigZag))
        {
            // Tell: one sharp side-step. Then legs of ~0.8 s alternating either side of the escape line.
            const bool Left=Dir.Signalling(Play::ZigZag,Now)?Mine.Sweep>0:(static_cast<int32>(Now/.8)%2==0)==(Mine.Sweep>0);
            Away=Away.RotateAngleAxis((Dir.Signalling(Play::ZigZag,Now)?80.f:50.f)*(Left?-1.f:1.f),FVector::UpVector);
            Goal=Here+Away*350.f*Reach(Play::ZigZag);
        }
        else Goal=ChooseTacticPoint(Pawn,B,Action::Hold,false,500.f,Point,nullptr,&Away,false,220)?Point:Here+Away*500.f;
        HasGoal=PlayMove=true;
    }
    // Watchdog: was stuck in one spot -> head for a fresh place for a few seconds.
    else if(Now<B.UnstickUntil){Goal=Mine.ExploreGoal;HasGoal=PlayMove=true;}
    else if(Dir.Active(Play::RunForIt))
    {
        // Tell: stops and turns to the exit. Then the full sprint.
        Goal=Dir.Signalling(Play::RunForIt,Now)?Here:Mine.Point;HasGoal=true;PlayMove=Dir.Signalling(Play::RunForIt,Now);
        if(!PlayMove)if(auto* Elevator=Cast<AProjectProject01Elevator>(Mine.Objective.Get());Elevator&&Elevator->GetBotObjective(Pawn,Goal,ObjectiveLook))SelectedElevator=Elevator;
    }
    else if(PairActive(Play::SplitRun)&&(PairSignalling(Play::SplitRun)||Splitting))
    {Goal=PairSignalling(Play::SplitRun)?Here:Mine.Point;HasGoal=PlayMove=true;}
    else if(LeapCover||PairSignalling(Play::LeapFrog)){Goal=Here;HasGoal=PlayMove=true;}
    else if(Dir.Active(Play::CoverRun)){Goal=Here;HasGoal=PlayMove=true;} // Holds the stare (see Holding).
    else if(Dir.Active(Play::NoiseLure))
    {
        // Tell: the loud run away from the partner. Then stands silent where the steps stop.
        Goal=Dir.Signalling(Play::NoiseLure,Now)?Mine.Point:Here;HasGoal=PlayMove=true;
    }
    else if(Dir.Active(Play::FalseRetreat))
    {
        // Tell: the visible run away. Then back to the exit by another way (the route check below).
        const bool Back=Dir.Executing(Play::FalseRetreat,Now);Goal=Back?Mine.Point2:Mine.Point;HasGoal=true;PlayMove=!Back;
        if(Back)if(auto* Elevator=Cast<AProjectProject01Elevator>(Mine.Objective.Get());Elevator&&Elevator->GetBotObjective(Pawn,Goal,ObjectiveLook))SelectedElevator=Elevator;
    }
    else if(Baiting&&Dir.Executing(Play::BaitRunner,Now)&&Buddy)
    {
        // After standing in plain view (the tell), drift away from the partner's route while staying visible.
        const FVector Away=Pawn->GetActorLocation()-Buddy->GetActorLocation();FVector Point;
        if(ChooseTacticPoint(Pawn,B,Action::Hold,false,600.f,Point,nullptr,&Away,true,210)){Goal=Point;HasGoal=PlayMove=true;}
    }
    else if(Dir.Active(Play::ExitFeint))
    {
        // Tell: a visible run toward the first exit. Then the switch to the other one.
        const bool Switched=Dir.Executing(Play::ExitFeint,Now);Goal=Switched?Mine.Point2:Mine.Point;HasGoal=PlayMove=true;
        if(Switched)if(auto* Elevator=Cast<AProjectProject01Elevator>(Mine.Objective.Get());Elevator&&Elevator->GetBotObjective(Pawn,Goal,ObjectiveLook))SelectedElevator=Elevator;
        if(Switched&&FVector::DistSquared2D(Pawn->GetActorLocation(),Goal)<FMath::Square(200.f))Dir.Finish(Play::ExitFeint,Level,Now,ReliefNow);
    }
    // A teammate called for help: head over unless it is too dangerous right here.
    else if(Now<Mine.HelpUntil&&FVector::DistSquared2D(Here,Mine.HelpPoint)>FMath::Square(400.f)){Goal=Mine.HelpPoint;HasGoal=true;}
    else if(Buddy&&(Tactic==Action::Regroup||Dir.Signalling(Play::BackToBack,Now))){Goal=Buddy->GetActorLocation()+FVector(0,220,0);HasGoal=true;}
    else if(!Rush&&Knows&&(Tactic==Action::FlankLeft||Tactic==Action::FlankRight||Tactic==Action::Lure))
    {
        FVector Point;
        if(ChooseTacticPoint(Pawn,B,Tactic,false,600.f,Point))Goal=Point;
        else Goal+=(Goal-B.Known).GetSafeNormal2D().RotateAngleAxis(Tactic==Action::FlankLeft?-65.f:Tactic==Action::FlankRight?65.f:0,FVector::UpVector)*600;
        HasGoal=true;
    }
    // Last survivor lurking or drawing the guard off: go to that spot first.
    else if(Rush&&(Way==LoneApproach::Lurk||Way==LoneApproach::Decoy)){Goal=Mine.LonePoint;HasGoal=PlayMove=true;}
    else if(Tactic==Action::Search||Progress||Rush||Stalled||ExitPull>.4f||LeapMove||(PairActive(Play::SplitRun)&&!Splitting))
    {
        // Levels 3+ in a bot-only team pick exits by danger and spread out; lower levels run to the nearest (readable).
        const auto* BuddyPlay=BuddyBotId.IsEmpty()?nullptr:Plays.Find(BuddyBotId);
        FVector Look;
        // A together plan heads for the partner's exit; otherwise partners spread out. A detour avoids the guarded exit.
        const bool Detour=Rush&&Way==LoneApproach::Detour;
        const AActor* AvoidExit=Detour?Mine.LoneAvoid.Get():BuddyPlay&&Script.Survivors!=SurvivorPlan::Together?BuddyPlay->Objective.Get():nullptr;
        if(AActor* Objective=ChooseObjective(Pawn,B,Spec,(TeamPlay&&Level.Level>=3)||Detour,AvoidExit,Goal,Look))
        {ObjectiveLook=Look;SelectedElevator=Cast<AProjectProject01Elevator>(Objective);Mine.Objective=Objective;HasGoal=true;}
        // No exit found yet: stick with a human partner (who may know the way), otherwise go looking.
        if(!HasGoal&&Buddy&&HumanAlly){Goal=Buddy->GetActorLocation()+FVector(220,0,0);HasGoal=true;}
        if(!HasGoal){FVector Explore;if(ChooseExploreGoal(Pawn,Mine,Now,Explore)){Goal=Explore;HasGoal=true;}}
        if(!HasGoal&&Buddy){Goal=Buddy->GetActorLocation()+FVector(220,0,0);HasGoal=true;}
        if(!HasGoal)if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
        {FNavLocation Point;if(Now>=B.PathAt&&Nav->GetRandomReachablePointInRadius(Goal,1000,Point)){Goal=Point.Location;HasGoal=true;}else if(B.Path.Num()){Goal=B.Path.Last();HasGoal=true;}}
    }
    // Route check (2+): the way to the goal runs straight into a mannequin nobody is freezing -> step around it.
    if(!Danger&&!PlayMove&&HasGoal&&B.Disciplined&&Level.Level>=2)
    {
        const FVector ToGoal=(Goal-Here).GetSafeNormal2D();const float GoalDistance=FVector::Dist2D(Goal,Here);
        for(const auto& Threat:B.Threats)
        {
            const FVector ToThreat=Threat.Position-Here;const float D=ToThreat.Size2D();
            if(Threat.Covered||D>1300.f||D>GoalDistance||FVector::DotProduct(ToGoal,ToThreat.GetSafeNormal2D())<.75f)continue;
            // Pass on the side the goal already leans to, keeping clear of the mannequin's reach.
            const float Lean=ToThreat.X*ToGoal.Y-ToThreat.Y*ToGoal.X;
            const FVector Around=(ToThreat.GetSafeNormal2D().RotateAngleAxis(Lean>=0?90.f:-90.f,FVector::UpVector)*.7f+ToGoal*.3f).GetSafeNormal2D();
            FVector Point;Goal=ChooseTacticPoint(Pawn,B,Action::Hold,false,600.f,Point,nullptr,&Around,false,221)?Point:Here+Around*600.f;
            PlayMove=true;break;
        }
    }
    // Tells: freezes to listen; hiding keeps it frozen and silent (no footsteps) until the steps pass.
    const bool Listening=Dir.Signalling(Play::Listen,Now)||Dir.Active(Play::HideStill);
    const bool BaitTell=Baiting&&Dir.Signalling(Play::BaitRunner,Now);
    // Play holds: the watch-retreat turn, the cover stare and the silent stop after a noise lure.
    const bool PlayHold=Dir.Signalling(Play::WatchRetreat,Now)||Dir.Active(Play::CoverRun)||Dir.Executing(Play::NoiseLure,Now)||
        (!Danger&&(Dir.Signalling(Play::RunForIt,Now)||PairSignalling(Play::SplitRun)||PairSignalling(Play::LeapFrog)||LeapCover||Lurking));
    // Danger overrides standing still: never hold position inside a mannequin's reach.
    // Catching breath after a scare is a short stop too.
    const bool Breathing=!Danger&&Now<B.BreatheUntil;
    const bool Holding=PlayHold||Breathing||(!Danger&&(Listening||BaitTell||(!PlayMove&&!Progress&&!Rush&&!Stalled&&(Tactic==Action::Cover||Tactic==Action::Observe||Tactic==Action::Recover||Tactic==Action::Hold))));
    if(Holding)Pawn->GetCharacterMovement()->StopMovementImmediately();
    const bool Moving=!Holding&&HasGoal&&Move(Pawn,B,Goal,Delta,false,!Knows)&&Now>=B.PauseUntil;
    // A goal it cannot path to (an elevator button off the walkable area, a blocked route): after 2.5 s give that
    // exit up for 25 s and let the next pick go elsewhere, instead of standing there.
    if(HasGoal&&!Moving&&!Holding&&Now>=B.PauseUntil&&FVector::DistSquared2D(Here,Goal)>FMath::Square(300.f))
    {
        if(B.NoPathSince<0)B.NoPathSince=Now;
        else if(Now-B.NoPathSince>2.5)
        {
            // Only blame the exit when the goal really was that exit; other goals just get re-picked.
            AActor* Exit=Mine.Objective.Get();
            const bool ForExit=Exit&&(SelectedElevator||FVector::DistSquared2D(Goal,InsideGoal(Exit))<FMath::Square(400.f));
            if(ForExit){Mine.UnreachableUntil.Add(Exit,Now+25.);Mine.Objective.Reset();}
            Mine.ExploreUntil=0;B.Path.Reset();B.NoPathSince=-1;
        }
    }
    else B.NoPathSince=-1;
    // At an exit volume's goal but not through yet: step straight into the box (it triggers on overlap).
    if(HasGoal&&!SelectedElevator&&!Holding&&FVector::DistSquared2D(Here,Goal)<FMath::Square(200.f))
        if(const AActor* Exit=Mine.Objective.Get();Exit&&FVector::DistSquared2D(Goal,InsideGoal(Exit))<FMath::Square(50.f))
            Pawn->AddMovementInput((Exit->GetComponentsBoundingBox(true).GetCenter()-Here).GetSafeNormal2D(),1.f);
    // Fidgets while waiting with nothing in sight (never during a silent hide, a stare or a play hold).
    const bool AtElevator=SelectedElevator&&FVector::DistSquared2D(Here,Goal)<FMath::Square(160.f);
    // Only a real wait (the elevator after pressing) gets fidgets; anything else keeps moving.
    const bool Waiting=!Knows&&!Danger&&!PlayHold&&!Listening&&!BaitTell&&!Breathing&&AtElevator&&Now-B.PressedAt<15.;
    // Stuck watchdog: hardly left a small area for 6 s outside a deliberate hold or an elevator wait -> go elsewhere.
    {
        // In a stalemate a long play no longer excuses standing in one spot.
        const bool Deliberate=(!Stalled&&(Dir.BodyBusy()||Now<Mine.PartnerUntil))||Listening||Lurking||(AtElevator&&Now-B.PressedAt<20.);
        if(B.AnchorTime<0||Deliberate||FVector::DistSquared2D(Here,B.AnchorAt)>FMath::Square(450.f)){B.AnchorAt=Here;B.AnchorTime=Now;}
        else if(Now-B.AnchorTime>6.)
        {
            B.AnchorAt=Here;B.AnchorTime=Now;Mine.ExploreUntil=0;B.Path.Reset();
            FVector Fresh;if(ChooseExploreGoal(Pawn,Mine,Now,Fresh))B.UnstickUntil=Now+4;
        }
    }
    if(!Waiting)B.IdleSince=-1;else if(B.IdleSince<0)B.IdleSince=Now;
    const bool Fidgeting=B.IdleSince>=0&&Now-B.IdleSince>1.2;
    if(Fidgeting&&Now>=B.FidgetAt)
    {
        B.FidgetAt=Now+FidgetInterval(Quirks,B.Policy.Random());const float Roll=B.Policy.Random();
        if(Buddy&&Roll<.25f)B.FidgetLook=Buddy->GetActorLocation()-Pawn->GetPawnViewLocation();
        else B.FidgetLook=Pawn->GetActorForwardVector().RotateAngleAxis((60.f+90.f*B.Policy.Random())*(B.Policy.Random()<.5f?-1.f:1.f),FVector::UpVector)*500.f;
        // A small shuffle on the spot now and then (more often for less careful players).
        if(Roll>1.f-.25f*Mistake){B.ShuffleTo=Here+FVector(B.Policy.Random()-.5f,B.Policy.Random()-.5f,0).GetSafeNormal2D()*70.f;B.ShuffleUntil=Now+.5;}
    }
    if(Fidgeting&&Now<B.ShuffleUntil)Pawn->AddMovementInput((B.ShuffleTo-Here).GetSafeNormal2D(),.35f);
    // Stamina habit: greedy players run themselves dry, careful ones stop early. Cornered or panicking, everyone keeps running.
    const float StopAt=.01f+.12f*(1-Quirks.StaminaGreed),ResumeAt=.25f+.3f*(1-Quirks.StaminaGreed);
    const bool Cornered=(Knows&&C.Range<.2f)||Panic>.4f||RushRun;
    if(C.Stamina<.01f||(C.Stamina<StopAt&&!Cornered))B.Recovering=true;else if(C.Stamina>ResumeAt)B.Recovering=false;
    const bool Dash=Dir.Executing(Play::PeekDash,Now)||Runner||Dir.Active(Play::ExitFeint)||Danger||Relay||
        Dir.Signalling(Play::FalseRetreat,Now)||Dir.Signalling(Play::NoiseLure,Now);
    // Backing away while watching is a walk: the stare keeps it frozen, the stamina is saved.
    // Walking toward the exit with eyes on a frozen mannequin needs no sprint either.
    // Running plays: the exit sprint, a leap-frog leg, the split.
    const bool PlayRun=Dir.Executing(Play::RunForIt,Now)||LeapMove||(PairActive(Play::SplitRun)&&!PairSignalling(Play::SplitRun))||((RushRun||ExitPull>.4f||(Stalled&&!Watching))&&!WatchRetreating);
    // Stalemate diagnostics: why this survivor is where it is (grep "[BotStall]").
    if(Stalled&&Now>=Mine.StallLogAt)
    {
        Mine.StallLogAt=Now+10.;
        UE_LOG(LogTemp,Display,TEXT("[BotStall] Survivor=%s L%d Tactic=%d Knows=%d Watching=%d Danger=%d Holding=%d PlayHold=%d Listening=%d Lurking=%d Moving=%d HasGoal=%d Goal=%.0fm Exit=%s Plays=%d Stamina=%.2f"),
            *Spec.BotId.Left(8),Level.Level,static_cast<int32>(Tactic),Knows,Watching,Danger,Holding,PlayHold,Listening,Lurking,Moving,HasGoal,FVector::Dist2D(Here,Goal)/100.f,
            *GetNameSafe(Mine.Objective.Get()),Dir.ActiveCount(),C.Stamina);
    }
    Pawn->SetBotSprint(Moving&&!B.Recovering&&(PlayRun||(!WatchRetreating&&!(Progress&&Watching)&&((Knows&&Tactic!=Action::Search&&!Progress)||Dash))));
    FVector Look=Pawn->GetVelocity();
    if(Now<B.PauseUntil)Look=B.PeekDirection;
    else if(!Knows&&Now<B.GlanceUntil)Look=B.GlanceDirection;
    // Back to back: alternate between the route and the partner's blind side.
    if(!Knows&&Buddy&&Dir.Executing(Play::BackToBack,Now)&&FMath::Sin(static_cast<float>(Now)*1.1f+Quirks.Phase)>0.f)Look=-Buddy->GetActorForwardVector();
    if(!Knows&&(Dir.Active(Play::Listen)||Dir.Active(Play::HideStill)))Look=Mine.Point-Pawn->GetActorLocation();
    if(!LookingAway&&Knows&&(Tactic==Action::Cover||Tactic==Action::Observe||Tactic==Action::Lure||Baiting))Look=B.Known-Pawn->GetActorLocation();
    if(LookingAway&&Holding)Look=B.GlanceDirection;
    // Keep the chosen mannequin in view while moving (it freezes while watched); movement is independent of facing.
    // Fleeing without the watch retreat faces the run (the opening lower levels give away).
    if(Watching&&!Danger)Look=U(Views[WatchIndex].Position)-Pawn->GetPawnViewLocation();
    if(Subject&&(WatchRetreating||Dir.Active(Play::CoverRun)||Dir.Executing(Play::NoiseLure,Now)))Look=Subject->Position-Pawn->GetPawnViewLocation();
    // Play tells and holds: turn to the exit before the run, face the partner before the split, face back while covering a leap.
    if(Dir.Signalling(Play::RunForIt,Now))Look=Mine.Point-Pawn->GetPawnViewLocation();
    // Lurking: watch the exit (and its guard) from cover.
    if(Lurking&&!Watching)if(const AActor* Watched=Mine.LoneAvoid.IsValid()?Mine.LoneAvoid.Get():Mine.Objective.Get())Look=Watched->GetActorLocation()-Pawn->GetPawnViewLocation();
    if(Buddy&&PairSignalling(Play::SplitRun))Look=Buddy->GetActorLocation()-Pawn->GetActorLocation();
    if(LeapCover||PairSignalling(Play::LeapFrog))
    {
        if(WatchIndex>=0)Look=U(Views[WatchIndex].Position)-Pawn->GetPawnViewLocation();
        else if(Knows)Look=B.Known-Pawn->GetActorLocation();
        else if(Buddy)Look=Pawn->GetActorLocation()-Buddy->GetActorLocation(); // The partner's back.
    }
    if(!Watching&&SelectedElevator&&FVector::DistSquared2D(Pawn->GetActorLocation(),Goal)<FMath::Square(160.f))Look=ObjectiveLook-Pawn->GetPawnViewLocation();
    // Looking around while waiting: the first part of each fidget beat, then back to the default.
    if(Fidgeting&&!Watching&&B.FidgetAt-Now>.45*FidgetInterval(Quirks,.5f))Look=B.FidgetLook;
    if(!Look.IsNearlyZero()){const FRotator Facing=HumanLook(B,PC->GetControlRotation(),Look.Rotation(),Delta);PC->SetControlRotation(Facing);Pawn->SetActorRotation(FRotator(0,Facing.Yaw,0));}
    if(SelectedElevator&&Now>=B.CommandAt){B.CommandAt=Now+FMath::Max(.5f,B.Policy.Profile.Reaction);if(SelectedElevator->TryBotInteraction(Pawn))B.PressedAt=Now;}
}

void UProjectMatchBots::TickMannequin(AMultiplayTestPlayerController* PC,const FProjectMatchBotSpec& Spec,float Delta)
{
    using namespace ProjectBots;
    auto* GM=Mode();auto* World=GM->GetWorld();const double Now=World->GetTimeSeconds();TArray<AMannequinAICharacter*> Bodies;
    for(TActorIterator<AMannequinAICharacter> It(World);It&&Bodies.Num()<10;++It)if(!It->IsHidden())Bodies.Add(*It);
    // Today's form: a small swing inside the level's band (never reaching a neighbouring level).
    const Difficulty Level=Difficulty::For(Spec.Difficulty,ConditionOf(Spec));const float ReliefNow=MannequinRelief.Value(Now,Level);
    auto& Mine=Plays.FindOrAdd(Spec.BotId);LoadExperience(Mine,Spec,Level);auto& Dir=Mine.Director;Dir.Update(Level,Now,ReliefNow);
    // Play style decides how readily this bot reaches for each play; the Director still enforces level, tells and cooldowns.
    const uint8 StyleId=static_cast<uint8>(StyleFor(Persona::From(GetTypeHash(Spec.BotId)*2654435761u+1u),Spec.Role==TEXT("Mannequin")));
    // Endgame: only one survivor left in play -> commit to catching it.
    float Urgency=0;
    {
        int32 Total=0,ActiveCount=0;
        for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Value.Role==TEXT("Survivor")){++Total;ActiveCount+=Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active;}
        Urgency=EndgameUrgency(Level,Total,ActiveCount);
    }
    // Deeper floors: firmer judgement; part of it shows as steady pressure even before the endgame.
    const float Resolve=FloorResolve(Level,FloorDepth);
    // A survivor last seen close to an exit this mannequin knows: hurry to stop it. It charges in straighter
    // as a result, which is easier to stare down or bait.
    float ExitThreat=0;
    for(const auto& Entry:Shared.Entries)if(Entry.Seen.Team==1&&Entry.Seen.Target&&Entry.Seen.At>=0&&Now-Entry.Seen.At<=Level.Memory)
        if(const AActor* Exit=NearestKnownExit(Mine,Spec,U(Entry.Seen.Position),Level))
            ExitThreat=FMath::Max(ExitThreat,ExitProximityBoldness(Level,FVector::Dist2D(Exit->GetActorLocation(),U(Entry.Seen.Position))));
    // This match's plan: Blitz (converge, go straight in) or Shadow (posts, statues, creeping), committed by level
    // and by how well it suits this character.
    const MatchScript Script=ScriptOf();const bool Blitz=Script.Mannequin==MannequinPlan::Blitz;
    Persona Who=Persona::From(GetTypeHash(Spec.BotId)*2654435761u+1u);Who.PlayStyle=StyleId;
    const float PlanStrength=MatchScript::Commit(Level)*MatchScript::Fit(Who,Blitz);
    // Stalemate: stop waiting it out (see the body settling and waiting-play endings below).
    const bool Stalled=IsStalled(Level,Now);
    const float Drive=FMath::Max(FMath::Max3(Urgency,.3f*Resolve,.6f*ExitThreat),FMath::Max(Blitz?.1f*PlanStrength:0.f,Stalled?.6f:0.f));
    // Self-made combos add eagerness for the learned follow-up right after its opener.
    // In the endgame the direct plays come first; waiting plays less.
    // The pushes scale the style's own preference (multiplied, not added), so a disliked play stays disliked.
    auto Want=[&](Play Kind){return FMath::FRand()<PlayAffinity(StyleId,Kind)*(1.f+
        (Kind==Play::Herding||Kind==Play::ExitAmbush?.4f*FMath::Max(Urgency,ExitThreat):
         Kind==Play::Pincer||Kind==Play::CoverBreak||Kind==Play::StepApproach?.4f*Urgency:
         Kind==Play::StillAmbush||Kind==Play::FakeRetreat||Kind==Play::ExitSweep?-.3f*Urgency:0.f))*
        PlanAffinity(Script,true,Kind,PlanStrength)+Mine.Combos.FollowUp(Kind,Now,Level);};
    auto IsParked=[&](const AMannequinAICharacter* X){return Mine.Parked.ContainsByPredicate([X](const TWeakObjectPtr<AMannequinAICharacter>& W){return W.Get()==X;});};
    struct FSurvivorView{APlayerCharacter* Pawn;AMultiplayTestPlayerController* Controller;};
    TArray<FSurvivorView,TInlineAllocator<4>> Survivors;
    for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active)
        if(auto* Survivor=Pair.Value.SurvivorPawn.Get())Survivors.Add({Survivor,Pair.Key.Get()});
    // Who is freezing a mannequin close to another survivor, i.e. covering an ally (the game's closest thing to a rescue).
    if(Level.Level>=3&&Now>=CoverScanAt)
    {
        CoverScanAt=Now+.3;CoveringSurvivors.Reset();
        for(const auto& S:Survivors)if(S.Controller)for(auto* X:Bodies)if(GM->CanSurvivorSeeMannequin(S.Controller,X))
        {
            bool Ally=false;for(const auto& Other:Survivors)Ally|=Other.Pawn!=S.Pawn&&FVector::Dist2D(Other.Pawn->GetActorLocation(),X->GetActorLocation())<900.f;
            if(Ally){CoveringSurvivors.Add(S.Pawn);break;}
        }
    }
    for(auto* Body:Bodies)
    {
        auto& B=Brains.FindOrAdd(Body);InitializeBrain(B,Spec,Body->GetUniqueID());
        if(Now>=B.ConditionAt){B.ConditionAt=Now+5;B.Policy.Profile=Level;}
        if(Now<B.SenseAt)continue;B.SenseAt=Now+.2;
        // Several survivors detected: who to go for depends on level (ChooseTarget). Retargeting is
        // re-judged about once a second, so even an indecisive beginner does not jitter every frame.
        TargetCandidate Candidates[4];APlayerCharacter* Pawns[4]={};int32 Count=0,Kept=-1;
        for(const auto& S:Survivors)if(Count<4&&GM->CanMannequinDetectSurvivor(Body,S.Pawn))
        {
            auto& Candidate=Candidates[Count];
            Candidate.Distance=FVector::Dist(Body->GetActorLocation(),S.Pawn->GetActorLocation());
            Candidate.Stamina=S.Pawn->GetCurrentStamina()/FMath::Max(1.f,S.Pawn->GetMaxStamina());
            float Nearest=MAX_flt;
            for(const auto& Other:Survivors)if(Other.Pawn!=S.Pawn)Nearest=FMath::Min(Nearest,FVector::Dist2D(Other.Pawn->GetActorLocation(),S.Pawn->GetActorLocation()));
            Candidate.Isolated=Nearest>900.f;Candidate.Covering=CoveringSurvivors.Contains(S.Pawn);Candidate.Current=B.Target.Get()==S.Pawn;
            if(Candidate.Current)Kept=Count;
            Pawns[Count++]=S.Pawn;
        }
        APlayerCharacter* Target=nullptr;
        if(Count>0)
        {
            if(Kept>=0&&Now<B.RetargetAt)Target=Pawns[Kept];
            else{const int32 Pick=ChooseTarget(B.Policy.Profile,Candidates,Count,B.Policy.Random());Target=Pick>=0?Pawns[Pick]:nullptr;B.RetargetAt=Now+.8+.8*B.Policy.Random();}
        }
        Sense(Body,Target,1,B,Target!=nullptr);
        if(!B.Visible&&B.Policy.Random()<B.Policy.Profile.Sharing)
            for(const auto& Entry:Shared.Entries)if(Entry.Seen.Team==1&&Entry.Seen.Target&&Entry.Seen.At>=0&&Now-Entry.Seen.At<=B.Policy.Profile.Memory)
                for(const auto& Pair:GM->MatchPlayerRecords)if(auto* Known=Pair.Value.SurvivorPawn.Get();Known&&Known->GetUniqueID()==Entry.Seen.Target)
                {if(B.Target.Get()!=Known||Now-B.SeenAt>B.Policy.Profile.Memory)B.ReactAt=Now+B.Policy.Profile.Reaction;B.Target=Known;B.Known=U(Entry.Seen.Position);B.SeenAt=Entry.Seen.At;break;}
    }
    auto* Body=PC->GetViewedMannequin();
    // Gaze relay (3+): the watched body is frozen; after a readable beat an unwatched body nearer the watcher takes over.
    if(Body&&Body->IsFrozenBySurvivorVision())
    {
        auto& Watched=Brains.FindOrAdd(Body);if(Watched.FrozenSince<0)Watched.FrozenSince=Now;
        if(Now-Watched.FrozenSince>.5&&Dir.Ready(Play::GazeRelay,Level,Now))
        {
            AMannequinAICharacter* Relay=nullptr;float Best=1600.f;
            for(const auto& S:Survivors)if(S.Controller&&GM->CanSurvivorSeeMannequin(S.Controller,Body))
                for(auto* X:Bodies)if(X!=Body&&!X->IsFrozenBySurvivorVision())
                {const float D=FVector::Dist2D(X->GetActorLocation(),S.Pawn->GetActorLocation());if(D<Best){Best=D;Relay=X;}}
            if(Relay&&Want(Play::GazeRelay)&&Dir.Start(Play::GazeRelay,Level,Now,ReliefNow))Mine.Helper=Relay;
        }
    }
    else if(Body)if(auto* Free=Brains.Find(Body))Free->FrozenSince=-1;
    if(Dir.Executing(Play::GazeRelay,Now))
        if(auto* Relay=Cast<AMannequinAICharacter>(Mine.Helper.Get());Relay&&Relay!=Body&&!Relay->IsFrozenBySurvivorVision())
        {
            // Team knowledge: the frozen body saw its watcher; the relay body acts on that shared sighting.
            TWeakObjectPtr<AActor> Seen;FVector SeenAt=FVector::ZeroVector;double SeenTime=-100;
            if(const auto* Watched=Body?Brains.Find(Body):nullptr){Seen=Watched->Target;SeenAt=Watched->Known;SeenTime=Watched->SeenAt;}
            if(Body){Body->SetManualControlEnabled(false);GM->QueueDefaultAIControllerRestore(Body);}
            GM->TryPossessMannequin(PC,Relay->GetControlSlot());Body=PC->GetViewedMannequin();Mine.Helper.Reset();
            SwitchBodyAt=Now+Info(Play::GazeRelay).Duration;
            if(Body&&Seen.IsValid()){auto& Next=Brains.FindOrAdd(Body);InitializeBrain(Next,Spec,Body->GetUniqueID());Next.Target=Seen;Next.Known=SeenAt;Next.SeenAt=SeenTime;Next.ReactAt=Now;}
        }
    // Held frozen for a while: a person switches to a body that can move (sooner at higher level).
    const auto* Held=Body?Brains.Find(Body):nullptr;
    const bool StuckFrozen=Body&&Body->IsFrozenBySurvivorVision()&&Held&&Held->FrozenSince>=0&&Now-Held->FrozenSince>4.0-3.0*Level.Skill;
    // While one body is held, send a free one in on the watcher (more often with coordination, i.e. level).
    if(StuckFrozen&&Now-Mine.HelperSentAt>4.&&FMath::FRand()<Level.Coordination+(Blitz?.1f*PlanStrength:0.f))
    {
        AMannequinAICharacter* Free=nullptr;float Best=MAX_flt;
        for(auto* X:Bodies)if(X!=Body&&!X->IsFrozenBySurvivorVision()&&!IsParked(X)){const float D=FVector::DistSquared2D(X->GetActorLocation(),Held->Known);if(D<Best){Best=D;Free=X;}}
        if(Free){GM->AssignMannequinCommand(Free,EMultiplayMannequinCommand::ChaseInDetectionRange);Mine.HelperSentAt=Now;}
    }
    // Stalemate: settle on the free body closest to where a survivor was last seen and send the rest in
    // (near ones chase, far ones follow), instead of flicking between bodies.
    AMannequinAICharacter* Settle=nullptr;
    if(Stalled)
    {
        FVector LastSeen=FVector::ZeroVector;double SeenAt=-1;
        for(const auto& Entry:Shared.Entries)if(Entry.Seen.Team==1&&Entry.Seen.Target&&Entry.Seen.At>SeenAt){SeenAt=Entry.Seen.At;LastSeen=U(Entry.Seen.Position);}
        if(SeenAt>=0)
        {
            float Best=MAX_flt;
            for(auto* X:Bodies)if(!X->IsFrozenBySurvivorVision()){const float D=FVector::DistSquared2D(X->GetActorLocation(),LastSeen);if(D<Best){Best=D;Settle=X;}}
            if(Now-Mine.HelperSentAt>4.)
            {
                // Only the two nearest free bodies: sending them all bunches them up where one stare freezes the lot.
                Mine.HelperSentAt=Now;TArray<AMannequinAICharacter*,TInlineAllocator<10>> Free;
                for(auto* X:Bodies)if(X!=Settle&&X!=Body&&!X->IsFrozenBySurvivorVision()&&!IsParked(X))Free.Add(X);
                Free.Sort([&](const AMannequinAICharacter& A,const AMannequinAICharacter& C){return FVector::DistSquared2D(A.GetActorLocation(),LastSeen)<FVector::DistSquared2D(C.GetActorLocation(),LastSeen);});
                for(int32 I=0;I<FMath::Min(2,Free.Num());++I)
                    GM->AssignMannequinCommand(Free[I],FVector::DistSquared2D(Free[I]->GetActorLocation(),LastSeen)<FMath::Square(2000.f)?EMultiplayMannequinCommand::ChaseInDetectionRange:EMultiplayMannequinCommand::FollowMannequinPlayer);
            }
        }
    }
    // Move off the current body only when it is held or its turn is up, so settling does not flicker either.
    const bool SettleSwitch=Settle&&Settle!=Body&&!Dir.BodyBusy()&&(!Body||Body->IsFrozenBySurvivorVision()||Now>=SwitchBodyAt);
    if(!Body||(Now>=SwitchBodyAt&&!Dir.BodyBusy()&&!Stalled)||(StuckFrozen&&!Dir.BodyBusy()&&!Stalled)||SettleSwitch)
    {
        AMannequinAICharacter* Pick=SettleSwitch?Settle:nullptr;float Total=0;
        if(!Pick)for(auto* Candidate:Bodies)
        {
            if(StuckFrozen&&Candidate==Body)continue; // Leave the held body.
            const auto& Other=Brains.FindChecked(Candidate);float Weight=(Candidate->IsFrozenBySurvivorVision()?.1f:.5f)+(Other.Target.IsValid()&&Now-Other.SeenAt<=Other.Policy.Profile.Memory?1.5f:0);
            if(Weight<=0)continue;Total+=Weight;if(FMath::FRandRange(0,Total)<Weight)Pick=Candidate;
        }
        if(Pick&&Pick!=Body)
        {if(Body){Body->SetManualControlEnabled(false);GM->QueueDefaultAIControllerRestore(Body);}GM->TryPossessMannequin(PC,Pick->GetControlSlot());Body=PC->GetViewedMannequin();}
        // A person stays absorbed in a body that is on someone's trail; patient characters stay longer.
        const auto* Focus=Body?Brains.Find(Body):nullptr;
        const bool Hunting=Focus&&Focus->Target.IsValid()&&Now-Focus->SeenAt<=Focus->Policy.Profile.Memory;
        SwitchBodyAt=Now+Level.Reaction*FMath::FRandRange(2.f,4.f)*(Hunting?1.f+Focus->Policy.Character.Patience:1.f);
    }
    if(!Body)return;
    // Lower levels leave some bodies parked (the existing Hold command): the ones farthest from where survivors
    // were last seen, as a beginner does not manage them all. Reviewed every 1.5 s (a body just left behind is parked
    // again promptly instead of hunting on its own); the rest keep their AI.
    if(Now>=Mine.ParkCheckAt)
    {
        Mine.ParkCheckAt=Now+1.5;
        const int32 ParkCount=ParkedBodies(Level,Urgency>0);
        FVector Ref=Body->GetActorLocation();double RefAt=-1;
        for(const auto& Entry:Shared.Entries)if(Entry.Seen.Team==1&&Entry.Seen.Target&&Entry.Seen.At>RefAt){RefAt=Entry.Seen.At;Ref=U(Entry.Seen.Position);}
        TArray<AMannequinAICharacter*,TInlineAllocator<10>> Order;for(auto* X:Bodies)if(X!=Body)Order.Add(X);
        Order.Sort([&](const AMannequinAICharacter& A,const AMannequinAICharacter& C){return FVector::DistSquared2D(A.GetActorLocation(),Ref)>FVector::DistSquared2D(C.GetActorLocation(),Ref);});
        TArray<TWeakObjectPtr<AMannequinAICharacter>> Next;
        for(int32 I=0;I<FMath::Min(ParkCount,Order.Num());++I)
        {
            Next.Add(Order[I]);
            if(!Mine.Parked.ContainsByPredicate([&](const TWeakObjectPtr<AMannequinAICharacter>& W){return W.Get()==Order[I];}))
                GM->AssignMannequinCommand(Order[I],EMultiplayMannequinCommand::HoldPosition);
        }
        for(const auto& Old:Mine.Parked)if(AMannequinAICharacter* X=Old.Get();X&&X!=Body&&!Next.Contains(Old))
            GM->AssignMannequinCommand(X,EMultiplayMannequinCommand::Patrol);
        Mine.Parked=MoveTemp(Next);
    }
    if(Stalled&&Now>=Mine.StallLogAt)
    {
        Mine.StallLogAt=Now+10.;int32 Frozen=0;for(auto* X:Bodies)Frozen+=X->IsFrozenBySurvivorVision();
        const auto* Focus=Brains.Find(Body);
        UE_LOG(LogTemp,Display,TEXT("[BotStall] Mannequin=%s L%d Plan=%s Bodies=%d Frozen=%d ViewedFrozen=%d Knows=%d Manual=%d Plays=%d Settle=%d"),
            *Spec.BotId.Left(8),Level.Level,ANSI_TO_TCHAR(MatchScript::Name(Script.Mannequin)),Bodies.Num(),Frozen,Body->IsFrozenBySurvivorVision(),
            Focus&&Focus->Target.IsValid()&&Now-Focus->SeenAt<=Level.Memory,Body->IsManualControlEnabled(),Dir.ActiveCount(),Settle!=nullptr);
    }
    // Every body is this player's eyes for finding exits.
    {TArray<APawn*,TInlineAllocator<10>> Eyes;for(auto* X:Bodies)Eyes.Add(X);DiscoverExits(Mine,Spec,Eyes,Level,Now);}
    // Combo feedback: a play worked for the mannequin if it ended close on a known survivor.
    if(const auto* Focus=Brains.Find(Body))
        TrackLearning(Mine,Level,Now,Focus->Target.IsValid()&&Now-Focus->SeenAt<=Focus->Policy.Profile.Memory&&FVector::Dist2D(Body->GetActorLocation(),Focus->Known)<900.f);
    // The viewed body can be outside Bodies (hidden or past the 10-body cap): never assert on it.
    auto& B=Brains.FindOrAdd(Body);InitializeBrain(B,Spec,Body->GetUniqueID());B.Policy.Relief=ReliefNow;
    auto C=Context(B,true);C.Frozen=Body->IsFrozenBySurvivorVision();C.HasAlly=Bodies.Num()>1;
    if(C.Visible||C.Remembered)C.Range=FMath::Clamp(FVector::Dist2D(Body->GetActorLocation(),B.Known)/2400.f,0.f,1.f);
    {
        // Habit reading (turn bias, escape side) is only exploited inside a telegraphed HabitFlank window.
        const auto Seen=B.Policy.Perceived(C.Opponent,C.MatchSeconds);
        if((C.Visible||C.Remembered)&&(FMath::Abs(Seen.LeftBias)>.3f||Seen.EscapeConfidence>.4f))Want(Play::HabitFlank)&&Dir.Start(Play::HabitFlank,Level,Now,ReliefNow);
        if(!Dir.Executing(Play::HabitFlank,Now)){C.Opponent.LeftBias=0;C.Opponent.EscapeSide=0;C.Opponent.EscapeConfidence=0;}
    }
    // Frustration shows as rushing in; it builds while the body is held frozen and when the prey slips away.
    Mine.Mood.Decay(Now);
    // Blitz's weak spot: it boils over sooner when held (frustration builds faster), which makes it easier to bait.
    if(C.Frozen&&B.FrozenSince>=0&&Now-B.FrozenSince>2.){Mine.Mood.Frustrate(Now,.08f*Delta*(Blitz?1.f+.8f*PlanStrength:1.f));B.LongFreezeAt=Now;}
    B.Policy.Tilt=Mine.Mood.Tilt(Level,B.Policy.Character);B.Policy.Urgency=Drive;B.Policy.Resolve=Resolve;
    B.Policy.Stealth=Blitz||Stalled?0.f:PlanStrength;
    const auto Tactic=B.Policy.Tick(C,Now).Tactic;
    auto* AI=Cast<AAIController>(Body->GetController());if(!AI)return;
    if(C.Frozen){AI->StopMovement();Body->GetCharacterMovement()->StopMovementImmediately();Body->ConsumeMovementInputVector();return;}
    const bool Knows=C.Visible||C.Remembered;
    const auto& Quirks=B.Policy.Character;const float Mistake=MistakeScale(B.Policy.Profile);
    // Lost the survivor: go check where it was heading, as a person would. Flustered players
    // (more often at low levels) guess the wrong side of the corner.
    if(B.HadKnowledge&&!Knows&&B.Target.IsValid())
    {
        FVector Heading=FVector::ZeroVector;
        if(const auto* Entry=Shared.Find(1,B.Target->GetUniqueID(),Now,8.f))Heading=U(Entry->Seen.Velocity);
        FVector Guess=Heading.GetSafeNormal2D()*FMath::Min(Heading.Size2D()*1.2f,600.f);
        if(B.Policy.Random()<.45f*Mistake)Guess=Guess.RotateAngleAxis(B.Policy.Random()<.5f?-70.f:70.f,FVector::UpVector);
        B.LostCheck=B.Known+Guess;B.LostCheckUntil=Now+3.f+2.f*Quirks.Curiosity;
        Mine.Mood.Frustrate(Now,.3f); // "Where did it go?"
    }
    B.HadKnowledge=Knows;
    // Hearing: footsteps reveal roughly where a survivor is (running carries far, walking barely).
    if(!Knows&&Now>=B.HearAt)
    {
        B.HearAt=Now+.25;
        for(const auto& S:Survivors){FVector Heard;if(Hear(Body,S.Pawn,Mistake,Heard)){if(Want(Play::Listen)&&Dir.Start(Play::Listen,Level,Now,ReliefNow)){Mine.Point=Heard;B.CommandAt=Now;}break;}}
    }
    APlayerCharacter* Prey=Cast<APlayerCharacter>(B.Target.Get());
    if(Now>=B.PlayCheckAt&&!Dir.BodyBusy())
    {
        B.PlayCheckAt=Now+.5;
        if(B.Policy.Random()<.3f+.4f*Level.Skill)
        {
            auto NearestHelper=[&](const FVector& At,float Range)->AMannequinAICharacter*
            {
                AMannequinAICharacter* Found=nullptr;float Best=Range;
                for(auto* X:Bodies)if(X!=Body&&!X->IsFrozenBySurvivorVision()){const float D=FVector::Dist2D(X->GetActorLocation(),At);if(D<Best){Best=D;Found=X;}}
                return Found;
            };
            bool Started=false;
            // Cover break (3+): go for the survivor who is freezing a mannequin to protect an ally.
            for(const auto& S:Survivors)if(!Started&&CoveringSurvivors.Contains(S.Pawn))
                if(const auto* Entry=Shared.Find(1,S.Pawn->GetUniqueID(),Now,B.Policy.Profile.Memory))
                    if(Want(Play::CoverBreak)&&Dir.Start(Play::CoverBreak,Level,Now,ReliefNow)){Mine.Subject=S.Pawn;Mine.Point=U(Entry->Seen.Position);Started=true;}
            if(!Started&&Knows&&Prey&&B.Visible)
            {
                const FVector ToBody=Body->GetActorLocation()-B.Known;const float Distance=ToBody.Size2D();const FVector Velocity=Prey->GetVelocity();
                // Still ambush (4+): a survivor walking toward this unwatched body -> stand like a display piece.
                if(Velocity.Size2D()>100.f&&FVector::DotProduct(Velocity.GetSafeNormal2D(),ToBody.GetSafeNormal2D())>.5f&&Distance>500.f&&Distance<1600.f)
                    Started=Want(Play::StillAmbush)&&Dir.Start(Play::StillAmbush,Level,Now,ReliefNow);
                // Pincer (3+): another body is near the survivor; the controlled one closes from the far side.
                if(!Started)if(auto* Helper=NearestHelper(B.Known,1600.f);Helper&&Distance<2000.f&&Want(Play::Pincer)&&Dir.Start(Play::Pincer,Level,Now,ReliefNow))
                {Mine.Helper=Helper;GM->AssignMannequinCommand(Helper,EMultiplayMannequinCommand::Patrol);Started=true;}
                // Decoy body (4+): the lure now pays off - the exposed body holds the gaze while another moves in.
                if(!Started&&Tactic==Action::Lure)if(auto* Helper=NearestHelper(B.Known,2200.f);Helper&&Want(Play::DecoyBody)&&Dir.Start(Play::DecoyBody,Level,Now,ReliefNow))
                {Mine.Helper=Helper;Started=true;}
                // Feint flank (5): first steps go to the other side.
                if(!Started&&(Tactic==Action::FlankLeft||Tactic==Action::FlankRight)&&Want(Play::FeintFlank)&&Dir.Start(Play::FeintFlank,Level,Now,ReliefNow)){Mine.Side=Tactic;Started=true;}
                // Step approach (2+): at mid range, creep in short bursts while unwatched.
                if(!Started&&Distance>600.f&&Distance<2000.f&&Want(Play::StepApproach)&&Dir.Start(Play::StepApproach,Level,Now,ReliefNow))
                {Mine.Stepping=false;Mine.StepUntil=0;Started=true;}
                // Fake retreat (4+): just released from a long stare -> back off in plain view, then turn and charge.
                if(!Started&&Now-B.LongFreezeAt<1.5&&Distance<1600.f&&Want(Play::FakeRetreat)&&Dir.Start(Play::FakeRetreat,Level,Now,ReliefNow))
                {Mine.Point=Body->GetActorLocation()+ToBody.GetSafeNormal2D()*450.f*Mine.Tuning.ReachScale(Play::FakeRetreat);Started=true;}
                // Herding (3+): the survivor is making for a known exit -> cut across between it and the exit.
                if(!Started&&Distance>700.f&&Distance<2200.f&&Velocity.Size2D()>100.f)
                    if(AActor* Exit=NearestKnownExit(Mine,Spec,B.Known,Level))
                    {
                        const FVector ToExit=Exit->GetActorLocation()-B.Known;
                        if(ToExit.Size2D()<2500.f&&FVector::DotProduct(Velocity.GetSafeNormal2D(),ToExit.GetSafeNormal2D())>.4f&&Want(Play::Herding)&&Dir.Start(Play::Herding,Level,Now,ReliefNow))
                        {Mine.Objective=Exit;Started=true;}
                    }
            }
            // Search sweep (2+): lost the survivor a moment ago -> search the area around the last sighting, point by point.
            if(!Started&&!Knows&&B.SeenAt>0&&Now-B.SeenAt>3.&&Now-B.SeenAt<20.&&Want(Play::SearchSweep)&&Dir.Start(Play::SearchSweep,Level,Now,ReliefNow))
            {Mine.Point=B.Known;Mine.Sweep=0;Mine.StepUntil=0;Started=true;}
            // Exit ambush (2+): lost everyone -> wait by the exit nearest the last sighting.
            // Master also cuts off a survivor seen running toward an exit.
            if(!Started&&!Survivors.IsEmpty()&&B.SeenAt>0)
            {
                // Only an exit this mannequin player has found can be guarded.
                AActor* Exit=NearestKnownExit(Mine,Spec,B.Known,Level);
                const bool Lost=!Knows&&Now-B.SeenAt>8.;bool Cut=false;
                if(Exit&&Prey&&B.Visible&&Level.Level>=5)
                {
                    const FVector ToExit=Exit->GetActorLocation()-B.Known,Velocity=Prey->GetVelocity();
                    Cut=Velocity.Size2D()>150.f&&FVector::DotProduct(Velocity.GetSafeNormal2D(),ToExit.GetSafeNormal2D())>.6f&&ToExit.Size2D()<2500.f;
                }
                if(Exit&&(Lost||Cut)&&Want(Play::ExitAmbush)&&Dir.Start(Play::ExitAmbush,Level,Now,ReliefNow)){Mine.Objective=Exit;Mine.Point=FVector::ZeroVector;Started=true;}
            }
            // Quiet plan: with nobody in sight (yet), post at a known exit early rather than wait to lose someone.
            if(!Started&&!Blitz&&!Knows&&(B.SeenAt<=0||Now-B.SeenAt>4.))
                if(AActor* Exit=NearestKnownExit(Mine,Spec,Body->GetActorLocation(),Level);Exit&&Want(Play::ExitAmbush)&&Dir.Start(Play::ExitAmbush,Level,Now,ReliefNow))
                {Mine.Objective=Exit;Mine.Point=FVector::ZeroVector;Started=true;}
            // Exit sweep (3+): lost for a while and knows two or more exits -> walk a round between them.
            if(!Started&&!Knows&&B.SeenAt>0&&Now-B.SeenAt>12.)
            {
                int32 KnownCount=0;for(const auto& Entry:Objectives)KnownCount+=Entry.IsValid()&&KnowsExit(Mine,Spec,Entry.Get(),Level);
                if(KnownCount>=2&&Want(Play::ExitSweep)&&Dir.Start(Play::ExitSweep,Level,Now,ReliefNow)){Mine.Sweep=0;Mine.StepUntil=0;Started=true;}
            }
            if(Started)B.CommandAt=Now;
        }
    }
    // Plays end when their premise is gone.
    if(Knows&&Dir.Active(Play::Listen))Dir.Finish(Play::Listen,Level,Now,ReliefNow);
    if(Dir.Active(Play::CoverBreak))if(auto* Subject=Cast<APlayerCharacter>(Mine.Subject.Get());!Subject||Subject->IsGameOver())Dir.Finish(Play::CoverBreak,Level,Now,ReliefNow);
    if(!Knows)for(Play Kind:{Play::Pincer,Play::StillAmbush,Play::DecoyBody,Play::FeintFlank,Play::StepApproach,Play::Herding,Play::FakeRetreat})if(Dir.Active(Kind))Dir.Finish(Kind,Level,Now,ReliefNow);
    if(Knows)for(Play Kind:{Play::SearchSweep,Play::ExitSweep})if(Dir.Active(Kind))Dir.Finish(Kind,Level,Now,ReliefNow); // Found someone: chase instead.
    // Stalemate: waiting plays end; the hunt goes on.
    if(Stalled)for(Play Kind:{Play::ExitAmbush,Play::StillAmbush,Play::ExitSweep})if(Dir.Active(Kind))Dir.Finish(Kind,Level,Now,ReliefNow);
    if(Dir.Active(Play::FeintFlank)&&Tactic!=Action::FlankLeft&&Tactic!=Action::FlankRight)Dir.Finish(Play::FeintFlank,Level,Now,ReliefNow);
    if(Dir.Active(Play::ExitAmbush)&&B.Visible&&FVector::Dist2D(Body->GetActorLocation(),B.Known)<1200.f)Dir.Finish(Play::ExitAmbush,Level,Now,ReliefNow);
    const bool PlayControl=Dir.BodyBusy()&&!Dir.Active(Play::GazeRelay);
    FVector Spot=FVector::ZeroVector;bool HuntSpot=false;
    const bool LostCheck=!PlayControl&&!Knows&&Tactic==Action::Search&&Now<B.LostCheckUntil;
    if(LostCheck){Spot=B.LostCheck;HuntSpot=true;}
    // Expert/master, once unlocked by match progress: search where this survivor was actually seen resting.
    else if(!PlayControl&&!Knows&&Tactic==Action::Search&&Now>=B.SpotIgnoreUntil&&B.Policy.Profile.AllowsDeception())
        if(const auto* Habits=Shared.FindHabits(1,B.Target.IsValid()?B.Target->GetUniqueID():0))
            if(const auto Seen=B.Policy.Perceived(Habits->Read(Now),C.MatchSeconds);Seen.RestConfidence>.4f){Spot=U(Seen.RestSpot);HuntSpot=true;}
    if(Now>=B.CommandAt)
    {
        B.CommandAt=Now+B.Policy.Profile.Reaction*FMath::FRandRange(1.5f,3.f);
        if(Tactic==Action::Search&&!HuntSpot&&!PlayControl){Body->SetManualControlEnabled(false);GM->TryIssueMannequinCommand(PC,EMultiplayMannequinCommand::Patrol);B.Path.Reset();}
        else
        {
            AI->StopMovement();
            // Commands are committed while AI-owned, then the server adapter supplies movement.
            Body->SetManualControlEnabled(false);
            GM->TryIssueMannequinCommand(PC,Tactic==Action::Observe&&!PlayControl?EMultiplayMannequinCommand::ChaseInDetectionRange:EMultiplayMannequinCommand::Patrol);
            Body->SetManualControlEnabled(true);
        }
        // In the endgame the other bodies are pulled in on the hunt more often.
        if((Tactic==Action::Cover||Tactic==Action::Regroup||Urgency>0)&&B.Policy.Random()<B.Policy.Profile.Coordination*(1+Urgency))
        {
            for(auto* Other:Bodies)if(Other!=Body&&!IsParked(Other)){GM->AssignMannequinCommand(Other,EMultiplayMannequinCommand::FollowMannequinPlayer);break;}
        }
        else if(Tactic==Action::Search&&B.Policy.Random()<.2f)GM->TryResetFollowingMannequins(PC);
        if(B.Policy.Random()<.025f)GM->ToggleMannequinCatchDamage(PC);
    }
    if(!Body->IsManualControlEnabled()||!(Knows||HuntSpot||PlayControl))return;
    GM->MaintainMannequinFloorSafety(Body);
    // Tells: listening, pincer, cover break, still ambush and habit reading all start with a visible stop and a turn of the head.
    auto FaceAndHold=[&](const FVector& At)
    {
        AI->StopMovement();Body->GetCharacterMovement()->StopMovementImmediately();
        const FRotator Look=HumanLook(B,Body->GetActorRotation(),(At-Body->GetActorLocation()).Rotation(),Delta);Body->SetActorRotation(FRotator(0,Look.Yaw,0));
    };
    if(Dir.Active(Play::Listen))
    {
        if(Dir.Signalling(Play::Listen,Now)){FaceAndHold(Mine.Point);return;}
        if(!Move(Body,B,Mine.Point,Delta,true,true)||FVector::DistSquared2D(Body->GetActorLocation(),Mine.Point)<FMath::Square(200.f))Dir.Finish(Play::Listen,Level,Now,ReliefNow);
        return;
    }
    if(Dir.Active(Play::ExitAmbush))
    {
        AActor* Exit=Mine.Objective.Get();if(!Exit){Dir.Finish(Play::ExitAmbush,Level,Now,ReliefNow);return;}
        const FVector ExitAt=Exit->GetActorLocation();
        if(Mine.Point.IsZero())
        {FVector Point;if(ChooseTacticPoint(Body,B,Action::Hold,true,450.f*Mine.Tuning.ReachScale(Play::ExitAmbush),Point,&ExitAt,nullptr,false,200))Mine.Point=Point;else{Dir.Finish(Play::ExitAmbush,Level,Now,ReliefNow);return;}}
        // Walks there (footsteps are the tell), then waits by the exit facing it.
        if(FVector::DistSquared2D(Body->GetActorLocation(),Mine.Point)>FMath::Square(150.f)){if(!Move(Body,B,Mine.Point,Delta))Dir.Finish(Play::ExitAmbush,Level,Now,ReliefNow);}
        else FaceAndHold(ExitAt);
        return;
    }
    if(Dir.Active(Play::SearchSweep))
    {
        // Tell: stands listening where the survivor vanished. Then visits points around it, pausing at each.
        if(Dir.Signalling(Play::SearchSweep,Now)){FaceAndHold(Mine.Point);return;}
        const FVector Around=Mine.Point+FVector(550.f*Mine.Tuning.ReachScale(Play::SearchSweep),0,0).RotateAngleAxis(120.f*Mine.Sweep+static_cast<float>(GetTypeHash(Spec.BotId)%120u),FVector::UpVector);
        if(Now<Mine.StepUntil){FaceAndHold(Around+(Around-Mine.Point));return;}
        if(!Move(Body,B,Around,Delta,true,true)||FVector::DistSquared2D(Body->GetActorLocation(),Around)<FMath::Square(180.f))
        {++Mine.Sweep;Mine.StepUntil=Now+.6+.6*FMath::FRand();}
        return;
    }
    if(Dir.Active(Play::ExitSweep))
    {
        // A round between the exits it knows: walks to one (footsteps), looks it over, moves on.
        TArray<AActor*,TInlineAllocator<8>> Known;
        for(const auto& Entry:Objectives)if(AActor* Objective=Entry.Get())if(KnowsExit(Mine,Spec,Objective,Level))Known.Add(Objective);
        if(Known.Num()<2){Dir.Finish(Play::ExitSweep,Level,Now,ReliefNow);return;}
        const FVector ExitAt=Known[Mine.Sweep%Known.Num()]->GetActorLocation();
        if(Dir.Signalling(Play::ExitSweep,Now)||Now<Mine.StepUntil){FaceAndHold(ExitAt);return;}
        if(!Move(Body,B,ExitAt,Delta)||FVector::DistSquared2D(Body->GetActorLocation(),ExitAt)<FMath::Square(300.f))
        {++Mine.Sweep;Mine.StepUntil=Now+.8+.8*FMath::FRand();}
        return;
    }
    if(HuntSpot)
    {
        constexpr uint8 HuntingSpot=254; // Drop a stale chase path once, when the hunt starts.
        if(B.TacticFor!=HuntingSpot){B.TacticFor=HuntingSpot;B.Path.Reset();B.PathAt=Now;}
        // A visited spot is checked once, then the body returns to ordinary patrol.
        if(!Move(Body,B,Spot,Delta,true,true)||FVector::DistSquared2D(Body->GetActorLocation(),Spot)<FMath::Square(200.f))
        {if(LostCheck)B.LostCheckUntil=0;else B.SpotIgnoreUntil=Now+25;B.CommandAt=Now;B.TacticFor=255;}
        return;
    }
    if(Dir.Signalling(Play::HabitFlank,Now)&&Knows){FaceAndHold(B.Known);return;} // "Reading you" pause.
    FVector Goal=B.Known;bool Planned=false;const bool Decoy=Dir.Active(Play::DecoyBody);
    if(Dir.Active(Play::CoverBreak))
    {
        auto* Subject=Cast<APlayerCharacter>(Mine.Subject.Get());
        if(const auto* Entry=Subject?Shared.Find(1,Subject->GetUniqueID(),Now,B.Policy.Profile.Memory):nullptr)Mine.Point=U(Entry->Seen.Position);
        if(Dir.Signalling(Play::CoverBreak,Now)){FaceAndHold(Mine.Point);return;}
        // Close in on the coverer from a point it cannot see.
        FVector Point;Goal=ChooseTacticPoint(Body,B,Action::Hold,true,250.f,Point,&Mine.Point,nullptr,false,203)?Point:Mine.Point;Planned=true;
    }
    else if(Dir.Active(Play::StillAmbush))
    {
        const float Distance=FVector::Dist2D(Body->GetActorLocation(),B.Known);
        const bool Close=B.Visible&&Distance<=320.f;
        const bool Past=B.Visible&&Prey&&Distance<450.f&&FVector::DotProduct(Prey->GetVelocity(),Body->GetActorLocation()-B.Known)<0.f;
        // Holds like a display piece until the survivor is right there or has walked past, then strikes.
        if(Dir.Signalling(Play::StillAmbush,Now)||!(Close||Past)){FaceAndHold(B.Known);return;}
        Dir.Finish(Play::StillAmbush,Level,Now,ReliefNow);B.AttackAt=FMath::Min(B.AttackAt,Now);
    }
    else if(Dir.Active(Play::Pincer))
    {
        if(Dir.Signalling(Play::Pincer,Now)){FaceAndHold(B.Known);return;} // The helper shows up on one side first.
        if(auto* Helper=Mine.Helper.Get())
        {const FVector Far=B.Known-Helper->GetActorLocation();FVector Point;if(ChooseTacticPoint(Body,B,Action::Hold,true,380.f,Point,nullptr,&Far,false,202)){Goal=Point;Planned=true;}}
    }
    else if(Dir.Active(Play::StepApproach))
    {
        if(Dir.Signalling(Play::StepApproach,Now)){FaceAndHold(B.Known);return;}
        // A short advance, then a statue pause: every time they look back it is a little closer.
        if(Now>=Mine.StepUntil)
        {
            Mine.Stepping=!Mine.Stepping;
            Mine.StepUntil=Now+(Mine.Stepping?(.5f+.4f*FMath::FRand())*Mine.Tuning.ReachScale(Play::StepApproach):(.7f+.8f*FMath::FRand())*(1.2f-.4f*Level.Skill));
        }
        if(!Mine.Stepping){FaceAndHold(B.Known);return;}
        if(FVector::Dist2D(Body->GetActorLocation(),B.Known)<GM->MultiplayerManualAttackRange+60.f)Dir.Finish(Play::StepApproach,Level,Now,ReliefNow);
        Goal=B.Known;Planned=true;
    }
    else if(Dir.Active(Play::Herding))
    {
        // Tell: a pause facing the survivor. Then it heads for a point between the survivor and the exit, not at the survivor.
        if(Dir.Signalling(Play::Herding,Now)){FaceAndHold(B.Known);return;}
        if(const AActor* Exit=Mine.Objective.Get())
        {
            const FVector Cut=B.Known+(Exit->GetActorLocation()-B.Known)*.45f*Mine.Tuning.ReachScale(Play::Herding);
            Goal=FVector::DistSquared2D(Body->GetActorLocation(),Cut)>FMath::Square(250.f)?Cut:B.Known;Planned=true;
        }
    }
    else if(Dir.Active(Play::FakeRetreat))
    {
        // Tell: visibly backs off. Then turns and charges straight in.
        Goal=Dir.Signalling(Play::FakeRetreat,Now)?Mine.Point:B.Known;Planned=true;
    }
    else if(Decoy)
    {
        // Tell: steps into plain view in front of the survivor. Then holds that gaze while the helper patrols in.
        if(Dir.Executing(Play::DecoyBody,Now))if(auto* Helper=Cast<AMannequinAICharacter>(Mine.Helper.Get()))
        {GM->AssignMannequinCommand(Helper,EMultiplayMannequinCommand::Patrol);Mine.Helper.Reset();}
        const FVector Front=FRotator(0,B.PreviousYaw,0).Vector();FVector Point;
        if(ChooseTacticPoint(Body,B,Action::Hold,true,900.f*Mine.Tuning.ReachScale(Play::DecoyBody),Point,nullptr,&Front,true,201)){Goal=Point;Planned=true;}
    }
    if(!Planned)
    {
        if(Tactic==Action::Hold||Tactic==Action::Recover){Body->GetCharacterMovement()->StopMovementImmediately();return;}
        // Feint (5): the first steps head for the opposite flank, then it commits to the real one.
        Action Side=Tactic;
        if(Dir.Signalling(Play::FeintFlank,Now))Side=Mine.Side==Action::FlankLeft?Action::FlankRight:Action::FlankLeft;
        else if(Dir.Executing(Play::FeintFlank,Now))Side=Mine.Side;
        const FVector Away=(Body->GetActorLocation()-Goal).GetSafeNormal2D();
        const bool Positioning=Side==Action::FlankLeft||Side==Action::FlankRight||Side==Action::Lure||Side==Action::Observe;
        const float Radius=Side==Action::Lure?GM->MultiplayerManualAttackRange+300:Side==Action::Observe?GM->MultiplayerManualAttackRange+160:380.f;
        FVector Point;
        if(Positioning&&ChooseTacticPoint(Body,B,Side,true,Radius,Point))Goal=Point;
        else if(Side==Action::FlankLeft||Side==Action::FlankRight)Goal+=Away.RotateAngleAxis(Side==Action::FlankLeft?-85.f:85.f,FVector::UpVector)*380;
        else if(Side==Action::Lure||Side==Action::Observe)Goal+=Away*Radius;
    }
    Move(Body,B,Goal,Delta);
    auto* Target=Cast<APlayerCharacter>(B.Target.Get());
    if(Target&&B.Visible&&!Target->IsGameOver()&&Tactic!=Action::Lure&&!Decoy&&Now>=B.AttackAt)
    {
        B.AttackAt=Now+FMath::Lerp(2.1f,.46f,B.Policy.Profile.Skill)*FMath::FRandRange(.85f,1.3f)*(1+.3f*ReliefNow)*(1-.25f*Urgency);const FVector Offset=Target->GetActorLocation()-Body->GetActorLocation();
        bool Watched=false;for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Key.IsValid()&&Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active)Watched|=GM->CanSurvivorSeeMannequin(Pair.Key.Get(),Body);
        if(!Watched&&!Body->IsFrozenBySurvivorVision()&&Offset.SizeSquared()<=FMath::Square(GM->MultiplayerManualAttackRange)&&FVector::DotProduct(Body->GetActorForwardVector(),Offset.GetSafeNormal())>=.25f&&AI->LineOfSightTo(Target)&&Target->HandleMannequinCatch(Body))
        {
            B.Policy.Relax(Now,(1.5f+3.f*Mistake)*(.6f+.8f*Quirks.Boldness)); // Brief "got one" complacency.
            // Balance diagnostics: what the caught survivor was doing (grep "[BotCatch]").
            if(const auto* Victim=Brains.Find(Target))
            {
                const FVector Facing=Target->GetActorForwardVector().GetSafeNormal2D(),FromVictim=(Body->GetActorLocation()-Target->GetActorLocation()).GetSafeNormal2D();
                UE_LOG(LogTemp,Display,TEXT("[BotCatch] Plan=%s Play=%s Floor=%d Seconds=%.0f VictimKnows=%d Visible=%d Disciplined=%d LookingAway=%d Threats=%d CatcherInFront=%.2f Speed=%.0f"),
                    ANSI_TO_TCHAR(MatchScript::Name(Script.Mannequin)),Dir.ActiveCount()>0?TEXT("yes"):TEXT("no"),FloorDepth,Now,
                    Victim->Target.IsValid()&&Now-Victim->SeenAt<=Victim->Policy.Profile.Memory,Victim->Visible,Victim->Disciplined,Now<Victim->LookAwayUntil,
                    Victim->Threats.Num(),FVector::DotProduct(Facing,FromVictim),Target->GetVelocity().Size2D());
            }
        }
    }
}

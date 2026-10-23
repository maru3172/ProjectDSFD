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
    return Learning;
}
void UProjectMatchBots::ImportLearning(const FProjectBotLearning& Learning)
{
    auto* GM=Mode();if(!GM||!GM->GetWorld())return;
    // The new map's clock started over; shift saved times so habits and relief age by the real travel time.
    const double Offset=GM->GetWorld()->GetTimeSeconds()-(Learning.SavedWorldSeconds+FMath::Max(0.,FPlatformTime::Seconds()-Learning.SavedRealSeconds));
    MannequinRelief=Learning.MannequinRelief;MannequinRelief.Rebase(Offset);
    SurvivorRelief=Learning.SurvivorRelief;SurvivorRelief.Rebase(Offset);
    PendingSurvivorHabits=Learning.SurvivorHabits;
    for(auto& Pair:PendingSurvivorHabits)Pair.Value.Rebase(Offset); // Also drops the old map's rest spots.
}

bool UProjectMatchBots::AcceptRoster(const FString& Room,const FString& Match,const TArray<FProjectMatchBotSpec>& Bots,FString& Error)
{
    auto* GM=Mode();
    if(!GM||!GM->HasAuthority()){Error=TEXT("Bot roster requires server authority.");return false;}
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
}
AActor* UProjectMatchBots::ChooseObjective(APawn* Pawn,const FProjectMatchBotBrain& B,const FProjectMatchBotSpec& Spec,bool Varied,const AActor* Avoid,FVector& Goal,FVector& Look)
{
    ScanObjectives();
    const double Now=Mode()->GetWorld()->GetTimeSeconds();
    // Danger is the last real sighting of the mannequin, kept a little longer for route planning.
    const bool Danger=B.Target.IsValid()&&Now-B.SeenAt<15.;
    AActor* Best=nullptr;float BestScore=MAX_flt;
    for(const auto& Entry:Objectives)if(auto* Objective=Entry.Get())
    {
        const auto* Transition=Cast<AProjectProject01LevelTransitionTrigger>(Objective);
        auto* Elevator=Cast<AProjectProject01Elevator>(Objective);FVector Candidate=Objective->GetActorLocation(),Facing=Candidate;
        if(Elevator){if(!Elevator->GetBotObjective(Pawn,Candidate,Facing))continue;}
        else if(Transition&&!Transition->IsAutomaticBotObjective())continue;
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
    const auto Level=B.Policy.Profile;const float ReliefNow=SurvivorRelief.Value(Now,Level);B.Policy.Relief=ReliefNow;
    auto& Mine=Plays.FindOrAdd(Spec.BotId);auto& Dir=Mine.Director;Dir.Update(Level,Now,ReliefNow);
    // Play style decides how readily this bot reaches for each play; the Director still enforces level, tells and cooldowns.
    const uint8 StyleId=static_cast<uint8>(StyleFor(Persona::From(GetTypeHash(Spec.BotId)*2654435761u+1u),Spec.Role==TEXT("Mannequin")));
    auto Want=[StyleId](Play Kind){return FMath::FRand()<PlayAffinity(StyleId,Kind);};
    APlayerCharacter* Buddy=nullptr;bool HumanAlly=false,BotBuddy=false;FString BuddyBotId;
    for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Key.Get()!=PC&&Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active&&Pair.Value.SurvivorPawn.IsValid())
    {
        Buddy=Pair.Value.SurvivorPawn.Get();HumanAlly=!Pair.Value.bServerBot;BotBuddy=Pair.Value.bServerBot;
        if(BotBuddy&&Pair.Value.UserId.StartsWith(TEXT("Bot:")))BuddyBotId=Pair.Value.UserId.RightChop(4);
        break;
    }
    // Level-scaled team tactics and psychological plays only when no human is on this team.
    // Beside a human, bots stay helpers (see Policy: no deception, short cover).
    const bool TeamPlay=!HumanAlly;
    if(Now>=B.SenseAt)
    {
        B.SenseAt=Now+.2;AMannequinAICharacter* Target=nullptr;float Best=MAX_flt;
        for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(Sees(Pawn,*It))
        {float D=FVector::DistSquared(Pawn->GetActorLocation(),It->GetActorLocation());if(D<Best){Best=D;Target=*It;}}
        Sense(Pawn,Target,2,B,Target!=nullptr,HumanAlly);
        if(!B.Visible&&B.Policy.Random()<B.Policy.Profile.Sharing)
            for(const auto& Entry:Shared.Entries)if(Entry.Seen.Team==2&&Entry.Seen.Target&&Entry.Seen.At>=0&&Now-Entry.Seen.At<=B.Policy.Profile.Memory)
                for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(It->GetUniqueID()==Entry.Seen.Target)
                {if(B.Target.Get()!=*It||Now-B.SeenAt>B.Policy.Profile.Memory)B.ReactAt=Now+B.Policy.Profile.Reaction*(HumanAlly?1.25:1.);B.Target=*It;B.Known=U(Entry.Seen.Position);B.SeenAt=Entry.Seen.At;break;}
    }
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
    // Lost sight: look back once at where the threat vanished, like a person double-checking.
    if(B.HadKnowledge&&!Knows){B.GlanceDirection=(B.Known-Pawn->GetActorLocation()).GetSafeNormal2D();B.GlanceUntil=Now+.6f+.6f*Quirks.Curiosity;}
    B.HadKnowledge=Knows;
    // Nervous shoulder checks while nothing is known. Beginners check far more often than experts.
    auto ScheduleGlance=[&](){B.NextGlanceAt=Now+(1.8f+3.5f*Skill)*(1.4f-.8f*Quirks.Curiosity)*(.7f+.6f*B.Policy.Random());};
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

    FVector Goal=Pawn->GetActorLocation(),ObjectiveLook=FVector::ZeroVector;bool HasGoal=false,PlayMove=false;
    AProjectProject01Elevator* SelectedElevator=nullptr;
    const bool Baiting=Dir.Active(Play::BaitRunner);
    if(Baiting&&Dir.Executing(Play::BaitRunner,Now)&&Buddy)
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
    else if(Buddy&&(Tactic==Action::Regroup||Dir.Signalling(Play::BackToBack,Now))){Goal=Buddy->GetActorLocation()+FVector(0,220,0);HasGoal=true;}
    else if(Knows&&(Tactic==Action::FlankLeft||Tactic==Action::FlankRight||Tactic==Action::Lure))
    {
        FVector Point;
        if(ChooseTacticPoint(Pawn,B,Tactic,false,600.f,Point))Goal=Point;
        else Goal+=(Goal-B.Known).GetSafeNormal2D().RotateAngleAxis(Tactic==Action::FlankLeft?-65.f:Tactic==Action::FlankRight?65.f:0,FVector::UpVector)*600;
        HasGoal=true;
    }
    else if(Tactic==Action::Search)
    {
        // Levels 3+ in a bot-only team pick exits by danger and spread out; lower levels run to the nearest (readable).
        const auto* BuddyPlay=BuddyBotId.IsEmpty()?nullptr:Plays.Find(BuddyBotId);
        FVector Look;
        if(AActor* Objective=ChooseObjective(Pawn,B,Spec,TeamPlay&&Level.Level>=3,BuddyPlay?BuddyPlay->Objective.Get():nullptr,Goal,Look))
        {ObjectiveLook=Look;SelectedElevator=Cast<AProjectProject01Elevator>(Objective);Mine.Objective=Objective;HasGoal=true;}
        if(!HasGoal&&Buddy){Goal=Buddy->GetActorLocation()+FVector(220,0,0);HasGoal=true;}
        if(!HasGoal)if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
        {FNavLocation Point;if(Now>=B.PathAt&&Nav->GetRandomReachablePointInRadius(Goal,1000,Point)){Goal=Point.Location;HasGoal=true;}else if(B.Path.Num()){Goal=B.Path.Last();HasGoal=true;}}
    }
    // Tells: freezes to listen; hiding keeps it frozen and silent (no footsteps) until the steps pass.
    const bool Listening=Dir.Signalling(Play::Listen,Now)||Dir.Active(Play::HideStill);
    const bool BaitTell=Baiting&&Dir.Signalling(Play::BaitRunner,Now);
    const bool Holding=Listening||BaitTell||(!PlayMove&&(Tactic==Action::Cover||Tactic==Action::Observe||Tactic==Action::Recover||Tactic==Action::Hold));
    if(Holding)Pawn->GetCharacterMovement()->StopMovementImmediately();
    const bool Moving=!Holding&&HasGoal&&Move(Pawn,B,Goal,Delta,false,!Knows)&&Now>=B.PauseUntil;
    // Stamina habit: greedy players run themselves dry, careful ones stop early. Cornered, everyone keeps running.
    const float StopAt=.01f+.12f*(1-Quirks.StaminaGreed),ResumeAt=.25f+.3f*(1-Quirks.StaminaGreed);
    const bool Cornered=Knows&&C.Range<.2f;
    if(C.Stamina<.01f||(C.Stamina<StopAt&&!Cornered))B.Recovering=true;else if(C.Stamina>ResumeAt)B.Recovering=false;
    const bool Dash=Dir.Executing(Play::PeekDash,Now)||Runner||Dir.Active(Play::ExitFeint);
    Pawn->SetBotSprint(Moving&&!B.Recovering&&((Knows&&Tactic!=Action::Search)||Dash));
    FVector Look=Pawn->GetVelocity();
    if(Now<B.PauseUntil)Look=B.PeekDirection;
    else if(!Knows&&Now<B.GlanceUntil)Look=B.GlanceDirection;
    // Back to back: alternate between the route and the partner's blind side.
    if(!Knows&&Buddy&&Dir.Executing(Play::BackToBack,Now)&&FMath::Sin(static_cast<float>(Now)*1.1f+Quirks.Phase)>0.f)Look=-Buddy->GetActorForwardVector();
    if(!Knows&&(Dir.Active(Play::Listen)||Dir.Active(Play::HideStill)))Look=Mine.Point-Pawn->GetActorLocation();
    if(Knows&&(Tactic==Action::Cover||Tactic==Action::Observe||Tactic==Action::Lure||Baiting))Look=B.Known-Pawn->GetActorLocation();
    if(SelectedElevator&&FVector::DistSquared2D(Pawn->GetActorLocation(),Goal)<FMath::Square(160.f))Look=ObjectiveLook-Pawn->GetPawnViewLocation();
    if(!Look.IsNearlyZero()){const FRotator Facing=HumanLook(B,PC->GetControlRotation(),Look.Rotation(),Delta);PC->SetControlRotation(Facing);Pawn->SetActorRotation(FRotator(0,Facing.Yaw,0));}
    if(SelectedElevator&&Now>=B.CommandAt){B.CommandAt=Now+FMath::Max(.5f,B.Policy.Profile.Reaction);SelectedElevator->TryBotInteraction(Pawn);}
}

void UProjectMatchBots::TickMannequin(AMultiplayTestPlayerController* PC,const FProjectMatchBotSpec& Spec,float Delta)
{
    using namespace ProjectBots;
    auto* GM=Mode();auto* World=GM->GetWorld();const double Now=World->GetTimeSeconds();TArray<AMannequinAICharacter*> Bodies;
    for(TActorIterator<AMannequinAICharacter> It(World);It&&Bodies.Num()<10;++It)if(!It->IsHidden())Bodies.Add(*It);
    const Difficulty Level=Difficulty::For(Spec.Difficulty);const float ReliefNow=MannequinRelief.Value(Now,Level);
    auto& Mine=Plays.FindOrAdd(Spec.BotId);auto& Dir=Mine.Director;Dir.Update(Level,Now,ReliefNow);
    // Play style decides how readily this bot reaches for each play; the Director still enforces level, tells and cooldowns.
    const uint8 StyleId=static_cast<uint8>(StyleFor(Persona::From(GetTypeHash(Spec.BotId)*2654435761u+1u),Spec.Role==TEXT("Mannequin")));
    auto Want=[StyleId](Play Kind){return FMath::FRand()<PlayAffinity(StyleId,Kind);};
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
    if(!Body||(Now>=SwitchBodyAt&&!Dir.BodyBusy()))
    {
        AMannequinAICharacter* Pick=nullptr;float Total=0;
        for(auto* Candidate:Bodies)
        {const auto& Other=Brains.FindChecked(Candidate);float Weight=(Candidate->IsFrozenBySurvivorVision()?.1f:.5f)+(Other.Target.IsValid()&&Now-Other.SeenAt<=Other.Policy.Profile.Memory?1.5f:0);Total+=Weight;if(FMath::FRandRange(0,Total)<Weight)Pick=Candidate;}
        if(Pick&&Pick!=Body)
        {if(Body){Body->SetManualControlEnabled(false);GM->QueueDefaultAIControllerRestore(Body);}GM->TryPossessMannequin(PC,Pick->GetControlSlot());Body=PC->GetViewedMannequin();}
        // A person stays absorbed in a body that is on someone's trail; patient characters stay longer.
        const auto* Focus=Body?Brains.Find(Body):nullptr;
        const bool Hunting=Focus&&Focus->Target.IsValid()&&Now-Focus->SeenAt<=Focus->Policy.Profile.Memory;
        SwitchBodyAt=Now+Level.Reaction*FMath::FRandRange(2.f,4.f)*(Hunting?1.f+Focus->Policy.Character.Patience:1.f);
    }
    if(!Body)return;
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
            }
            // Exit ambush (2+): lost everyone -> wait by the exit nearest the last sighting.
            // Master also cuts off a survivor seen running toward an exit.
            if(!Started&&!Survivors.IsEmpty()&&B.SeenAt>0)
            {
                ScanObjectives();AActor* Exit=nullptr;float Best=MAX_flt;
                for(const auto& Entry:Objectives)if(auto* Objective=Entry.Get())
                {const float D=FVector::Dist2D(Objective->GetActorLocation(),B.Known);if(D<Best){Best=D;Exit=Objective;}}
                const bool Lost=!Knows&&Now-B.SeenAt>8.;bool Cut=false;
                if(Exit&&Prey&&B.Visible&&Level.Level>=5)
                {
                    const FVector ToExit=Exit->GetActorLocation()-B.Known,Velocity=Prey->GetVelocity();
                    Cut=Velocity.Size2D()>150.f&&FVector::DotProduct(Velocity.GetSafeNormal2D(),ToExit.GetSafeNormal2D())>.6f&&ToExit.Size2D()<2500.f;
                }
                if(Exit&&(Lost||Cut)&&Want(Play::ExitAmbush)&&Dir.Start(Play::ExitAmbush,Level,Now,ReliefNow)){Mine.Objective=Exit;Mine.Point=FVector::ZeroVector;Started=true;}
            }
            if(Started)B.CommandAt=Now;
        }
    }
    // Plays end when their premise is gone.
    if(Knows&&Dir.Active(Play::Listen))Dir.Finish(Play::Listen,Level,Now,ReliefNow);
    if(Dir.Active(Play::CoverBreak))if(auto* Subject=Cast<APlayerCharacter>(Mine.Subject.Get());!Subject||Subject->IsGameOver())Dir.Finish(Play::CoverBreak,Level,Now,ReliefNow);
    if(!Knows)for(Play Kind:{Play::Pincer,Play::StillAmbush,Play::DecoyBody,Play::FeintFlank,Play::StepApproach})if(Dir.Active(Kind))Dir.Finish(Kind,Level,Now,ReliefNow);
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
        if((Tactic==Action::Cover||Tactic==Action::Regroup)&&B.Policy.Random()<B.Policy.Profile.Coordination)
        {
            for(auto* Other:Bodies)if(Other!=Body){GM->AssignMannequinCommand(Other,EMultiplayMannequinCommand::FollowMannequinPlayer);break;}
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
        {FVector Point;if(ChooseTacticPoint(Body,B,Action::Hold,true,450.f,Point,&ExitAt,nullptr,false,200))Mine.Point=Point;else{Dir.Finish(Play::ExitAmbush,Level,Now,ReliefNow);return;}}
        // Walks there (footsteps are the tell), then waits by the exit facing it.
        if(FVector::DistSquared2D(Body->GetActorLocation(),Mine.Point)>FMath::Square(150.f)){if(!Move(Body,B,Mine.Point,Delta))Dir.Finish(Play::ExitAmbush,Level,Now,ReliefNow);}
        else FaceAndHold(ExitAt);
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
            Mine.StepUntil=Now+(Mine.Stepping?.5f+.4f*FMath::FRand():(.7f+.8f*FMath::FRand())*(1.2f-.4f*Level.Skill));
        }
        if(!Mine.Stepping){FaceAndHold(B.Known);return;}
        if(FVector::Dist2D(Body->GetActorLocation(),B.Known)<GM->MultiplayerManualAttackRange+60.f)Dir.Finish(Play::StepApproach,Level,Now,ReliefNow);
        Goal=B.Known;Planned=true;
    }
    else if(Decoy)
    {
        // Tell: steps into plain view in front of the survivor. Then holds that gaze while the helper patrols in.
        if(Dir.Executing(Play::DecoyBody,Now))if(auto* Helper=Cast<AMannequinAICharacter>(Mine.Helper.Get()))
        {GM->AssignMannequinCommand(Helper,EMultiplayMannequinCommand::Patrol);Mine.Helper.Reset();}
        const FVector Front=FRotator(0,B.PreviousYaw,0).Vector();FVector Point;
        if(ChooseTacticPoint(Body,B,Action::Hold,true,900.f,Point,nullptr,&Front,true,201)){Goal=Point;Planned=true;}
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
        B.AttackAt=Now+FMath::Lerp(1.2f,.5f,B.Policy.Profile.Skill)*FMath::FRandRange(.85f,1.3f)*(1+.3f*ReliefNow);const FVector Offset=Target->GetActorLocation()-Body->GetActorLocation();
        bool Watched=false;for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Key.IsValid()&&Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active)Watched|=GM->CanSurvivorSeeMannequin(Pair.Key.Get(),Body);
        if(!Watched&&!Body->IsFrozenBySurvivorVision()&&Offset.SizeSquared()<=FMath::Square(GM->MultiplayerManualAttackRange)&&FVector::DotProduct(Body->GetActorForwardVector(),Offset.GetSafeNormal())>=.25f&&AI->LineOfSightTo(Target)&&Target->HandleMannequinCatch(Body))
            B.Policy.Relax(Now,(1.5f+3.f*Mistake)*(.6f+.8f*Quirks.Boldness)); // Brief "got one" complacency.
    }
}

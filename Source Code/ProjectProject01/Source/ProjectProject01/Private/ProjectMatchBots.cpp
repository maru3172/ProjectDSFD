#include "ProjectMatchBots.h"
#include "MultiplayTestGameMode.h"
#include "MannequinAICharacter.h"
#include "AIController.h"
#include "Engine/World.h"
#include "Engine/TriggerBox.h"
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
AMultiplayTestGameMode* UProjectMatchBots::Mode()const{return Cast<AMultiplayTestGameMode>(GetOuter());}

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
            if(Template)
            {
                auto* Mesh=Pawn->GetMesh();auto* Source=Template->GetMesh();Mesh->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());Mesh->SetAnimInstanceClass(Source->GetAnimClass());Mesh->SetRelativeTransform(Source->GetRelativeTransform());
                for(int32 I=0;I<Source->GetNumMaterials();++I)Mesh->SetMaterial(I,Source->GetMaterial(I));
                Pawn->GetCapsuleComponent()->SetCapsuleSize(Template->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),Half);
            }
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
    if(Fresh)B.ReactAt=Now+B.Policy.Profile.Reaction*(HumanAlly?1.25:1.)*(.85+.4*B.Policy.Random());
    B.Target=Target;B.Known=Target->GetActorLocation();B.SeenAt=Now;
    const FVector Velocity=Target->GetVelocity();const bool Watching=FVector::DotProduct(Facing.Vector(),(Observer->GetActorLocation()-B.Known).GetSafeNormal())>.6f;
    Shared.Publish({Team,Observer->GetUniqueID(),Target->GetUniqueID(),V(B.Known),V(Velocity),Now},true,.2f,Velocity.SizeSquared2D()<1225,Velocity.SizeSquared2D()>122500,Watching,Turn);
}
ProjectBots::Context UProjectMatchBots::Context(const FProjectMatchBotBrain& B,bool Mannequin)const
{
    ProjectBots::Context C;C.Playing=Mode()->IsAuthoritativeMatchInProgress();C.Mannequin=Mannequin;
    const double Now=Mode()->GetWorld()->GetTimeSeconds();const bool Known=B.Target.IsValid()&&Now>=B.ReactAt&&Now-B.SeenAt<=B.Policy.Profile.Memory;
    C.Visible=Known&&B.Visible;C.Remembered=Known;
    if(Known)if(const auto* Entry=Shared.Find(Mannequin?1:2,B.Target->GetUniqueID(),Now,B.Policy.Profile.Memory))C.Opponent=Entry->Statistics.Read(Now);
    return C;
}
bool UProjectMatchBots::Move(APawn* Pawn,FProjectMatchBotBrain& B,const FVector& Goal,float Delta,bool FaceTravel)
{
    auto* World=Mode()->GetWorld();const double Now=World->GetTimeSeconds();
    if(Now>=B.PathAt)
    {
        B.PathAt=Now+FMath::Max(.4f,B.Policy.Profile.Reaction);
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);FNavLocation Projected;B.Path.Reset();B.PathIndex=1;
        if(!Nav||!Nav->ProjectPointToNavigation(Goal,Projected,FVector(180,180,250)))return false;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Pawn->GetActorLocation(),Projected.Location,Pawn);
        if(!Path||!Path->IsValid()||Path->IsPartial())return false;B.Path=Path->PathPoints;
    }
    if(B.Path.IsValidIndex(B.PathIndex)&&FVector::DistSquared2D(Pawn->GetActorLocation(),B.Path[B.PathIndex])<4900)++B.PathIndex;
    if(!B.Path.IsValidIndex(B.PathIndex)){B.PathAt=Now;return false;}
    const FVector Direction=(B.Path[B.PathIndex]-Pawn->GetActorLocation()).GetSafeNormal2D();
    ProjectBots::Vec Neighbours[12];size_t Count=0;
    for(TActorIterator<ACharacter> It(World);It&&Count<12;++It)if(*It!=Pawn&&!It->IsHidden())Neighbours[Count++]=V(It->GetActorLocation());
    const float Remaining=B.PathIndex==B.Path.Num()-1?FVector::Dist2D(Pawn->GetActorLocation(),B.Path.Last()):200.f;
    const auto Velocity=ProjectBots::Steer(V(Pawn->GetActorLocation()),V(Direction),Neighbours,Count,160,1,Remaining);
    FVector Travel=U(Velocity).GetSafeNormal2D();auto* Character=Cast<ACharacter>(Pawn);if(!Character)return false;
    const auto* Capsule=Character->GetCapsuleComponent();FCollisionQueryParams Query(SCENE_QUERY_STAT(MatchBotStep),false,Pawn);FHitResult Hit;
    auto Clear=[&](FVector D){return !World->SweepSingleByChannel(Hit,Pawn->GetActorLocation(),Pawn->GetActorLocation()+D*85,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),FMath::Max(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()-8)),Query);};
    if(!Clear(Travel))Travel=Direction;
    if(!Clear(Travel)){B.Path.Reset();B.PathAt=Now+.3;return false;}
    Pawn->AddMovementInput(Travel,Velocity.Length());
    if(FaceTravel)Pawn->SetActorRotation(FMath::RInterpConstantTo(Pawn->GetActorRotation(),Travel.Rotation(),Delta,B.Policy.Profile.TurnRate));
    return true;
}

void UProjectMatchBots::TickSurvivor(AMultiplayTestPlayerController* PC,const FProjectMatchBotSpec& Spec,float Delta)
{
    auto* Pawn=Cast<AProjectMatchBotCharacter>(PC->GetPawn());if(!Pawn||Pawn->IsGameOver()||PC->GetSurvivorState()!=EMultiplayTestSurvivorState::Active)return;
    auto* GM=Mode();auto* World=GM->GetWorld();const double Now=World->GetTimeSeconds();auto& B=Brains.FindOrAdd(Pawn);
    if(!B.Initialized){B.Initialized=true;B.Policy.Reset(Spec.Difficulty,GetTypeHash(Spec.BotId));}
    APlayerCharacter* Buddy=nullptr;bool HumanAlly=false;
    for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Key.Get()!=PC&&Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active&&Pair.Value.SurvivorPawn.IsValid())
    {Buddy=Pair.Value.SurvivorPawn.Get();HumanAlly=!Pair.Value.bServerBot;break;}
    if(Now>=B.SenseAt)
    {
        B.SenseAt=Now+.2;AMannequinAICharacter* Target=nullptr;float Best=MAX_flt;
        for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(Sees(Pawn,*It))
        {float D=FVector::DistSquared(Pawn->GetActorLocation(),It->GetActorLocation());if(D<Best){Best=D;Target=*It;}}
        Sense(Pawn,Target,2,B,Target!=nullptr,HumanAlly);
        if(!B.Visible&&B.Policy.Random()<B.Policy.Profile.Sharing)
            for(const auto& Entry:Shared.Entries)if(Entry.Seen.Team==2&&Entry.Seen.Target&&Now-Entry.Seen.At<=B.Policy.Profile.Memory)
                for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(It->GetUniqueID()==Entry.Seen.Target)
                {if(B.Target.Get()!=*It||Now-B.SeenAt>B.Policy.Profile.Memory)B.ReactAt=Now+B.Policy.Profile.Reaction*(HumanAlly?1.25:1.);B.Target=*It;B.Known=U(Entry.Seen.Position);B.SeenAt=Entry.Seen.At;break;}
    }
    auto C=Context(B,false);C.HasAlly=Buddy!=nullptr;C.HumanAlly=HumanAlly;C.Stamina=Pawn->GetCurrentStamina()/FMath::Max(1.f,Pawn->GetMaxStamina());
    // Multiplayer has zero EXTRA death counts: zero means alive until caught, not critically hurt.
    C.Health=Pawn->IsGameOver()?0.f:1.f;
    if(C.Visible||C.Remembered)C.Range=FMath::Clamp(FVector::Dist2D(Pawn->GetActorLocation(),B.Known)/2400.f,0.f,1.f);
    if(Buddy){C.AllyDistance=FMath::Clamp(FVector::Dist2D(Pawn->GetActorLocation(),Buddy->GetActorLocation())/1200.f,0.f,1.f);C.AllyThreat=Buddy->GetRemainingDeathCount()<Pawn->GetRemainingDeathCount()?.8f:.3f;}
    using ProjectBots::Action;const auto Tactic=B.Policy.Tick(C,Now,0x3ff&~(1<<static_cast<int>(Action::Pressure))).Tactic;
    const bool Knows=C.Visible||C.Remembered;
    FVector Goal=Pawn->GetActorLocation(),ObjectiveLook=FVector::ZeroVector;bool HasGoal=false;
    AProjectProject01Elevator* SelectedElevator=nullptr;
    if(Buddy&&Tactic==Action::Regroup){Goal=Buddy->GetActorLocation()+FVector(0,220,0);HasGoal=true;}
    else if(Knows&&(Tactic==Action::FlankLeft||Tactic==Action::FlankRight||Tactic==Action::Lure))
    {Goal+=(Goal-B.Known).GetSafeNormal2D().RotateAngleAxis(Tactic==Action::FlankLeft?-65.f:Tactic==Action::FlankRight?65.f:0,FVector::UpVector)*600;HasGoal=true;}
    else if(Tactic==Action::Search)
    {
        float Best=MAX_flt;
        // Map-authored exits/transition volumes are objectives, not secret opponent knowledge.
        if(!ScannedObjectives)
        {
            ScannedObjectives=true;
            for(TActorIterator<AActor> It(World);It;++It)
                if(It->IsA<AProjectProject01LevelTransitionTrigger>()||(It->IsA<ATriggerBox>()&&It->ActorHasTag(GM->EscapeTriggerActorTag)))Objectives.Add(*It);
        }
        for(const auto& Entry:Objectives)if(auto* Objective=Entry.Get())
        {
            const auto* Transition=Cast<AProjectProject01LevelTransitionTrigger>(Objective);
            auto* Elevator=Cast<AProjectProject01Elevator>(Objective);FVector Candidate=Objective->GetActorLocation(),Look=Candidate;
            if(Elevator){if(!Elevator->GetBotObjective(Pawn,Candidate,Look))continue;}
            else if(Transition&&!Transition->IsAutomaticBotObjective())continue;
            float D=FVector::DistSquared(Pawn->GetActorLocation(),Candidate);
            if(D<Best){Best=D;Goal=Candidate;ObjectiveLook=Look;SelectedElevator=Elevator;HasGoal=true;}
        }
        if(!HasGoal&&Buddy){Goal=Buddy->GetActorLocation()+FVector(220,0,0);HasGoal=true;}
        if(!HasGoal)if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
        {FNavLocation Point;if(Now>=B.PathAt&&Nav->GetRandomReachablePointInRadius(Goal,1000,Point)){Goal=Point.Location;HasGoal=true;}else if(B.Path.Num()){Goal=B.Path.Last();HasGoal=true;}}
    }
    const bool Holding=Tactic==Action::Cover||Tactic==Action::Observe||Tactic==Action::Recover||Tactic==Action::Hold;
    if(Holding)Pawn->GetCharacterMovement()->StopMovementImmediately();
    const bool Moving=!Holding&&HasGoal&&Move(Pawn,B,Goal,Delta,false);
    if(C.Stamina<.01f)B.Recovering=true;else if(C.Stamina>.35f)B.Recovering=false;
    Pawn->SetBotSprint(Moving&&Knows&&!B.Recovering&&Tactic!=Action::Search);
    FVector Look=Pawn->GetVelocity();if(Knows&&(Tactic==Action::Cover||Tactic==Action::Observe||Tactic==Action::Lure))Look=B.Known-Pawn->GetActorLocation();
    if(SelectedElevator&&FVector::DistSquared2D(Pawn->GetActorLocation(),Goal)<FMath::Square(160.f))Look=ObjectiveLook-Pawn->GetPawnViewLocation();
    if(!Look.IsNearlyZero()){const FRotator Facing=FMath::RInterpConstantTo(PC->GetControlRotation(),Look.Rotation(),Delta,B.Policy.Profile.TurnRate);PC->SetControlRotation(Facing);Pawn->SetActorRotation(FRotator(0,Facing.Yaw,0));}
    if(SelectedElevator&&Now>=B.CommandAt){B.CommandAt=Now+FMath::Max(.5f,B.Policy.Profile.Reaction);SelectedElevator->TryBotInteraction(Pawn);}
}

void UProjectMatchBots::TickMannequin(AMultiplayTestPlayerController* PC,const FProjectMatchBotSpec& Spec,float Delta)
{
    auto* GM=Mode();auto* World=GM->GetWorld();const double Now=World->GetTimeSeconds();TArray<AMannequinAICharacter*> Bodies;
    for(TActorIterator<AMannequinAICharacter> It(World);It&&Bodies.Num()<10;++It)if(!It->IsHidden())Bodies.Add(*It);
    for(auto* Body:Bodies)
    {
        auto& B=Brains.FindOrAdd(Body);if(!B.Initialized){B.Initialized=true;B.Policy.Reset(Spec.Difficulty,GetTypeHash(Spec.BotId)^Body->GetUniqueID());}
        if(Now<B.SenseAt)continue;B.SenseAt=Now+.2;APlayerCharacter* Target=nullptr;float Best=MAX_flt;
        for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active)
            if(auto* Candidate=Pair.Value.SurvivorPawn.Get();Candidate&&GM->CanMannequinDetectSurvivor(Body,Candidate))
            {float D=FVector::DistSquared(Body->GetActorLocation(),Candidate->GetActorLocation());if(D<Best){Best=D;Target=Candidate;}}
        Sense(Body,Target,1,B,Target!=nullptr);
        if(!B.Visible&&B.Policy.Random()<B.Policy.Profile.Sharing)
            for(const auto& Entry:Shared.Entries)if(Entry.Seen.Team==1&&Entry.Seen.Target&&Now-Entry.Seen.At<=B.Policy.Profile.Memory)
                for(const auto& Pair:GM->MatchPlayerRecords)if(auto* Known=Pair.Value.SurvivorPawn.Get();Known&&Known->GetUniqueID()==Entry.Seen.Target)
                {if(B.Target.Get()!=Known||Now-B.SeenAt>B.Policy.Profile.Memory)B.ReactAt=Now+B.Policy.Profile.Reaction;B.Target=Known;B.Known=U(Entry.Seen.Position);B.SeenAt=Entry.Seen.At;break;}
    }
    auto* Body=PC->GetViewedMannequin();
    if(!Body||Now>=SwitchBodyAt)
    {
        AMannequinAICharacter* Pick=nullptr;float Total=0;
        for(auto* Candidate:Bodies)
        {const auto& B=Brains.FindChecked(Candidate);float Weight=(Candidate->IsFrozenBySurvivorVision()?.1f:.5f)+(B.Target.IsValid()&&Now-B.SeenAt<=B.Policy.Profile.Memory?1.5f:0);Total+=Weight;if(FMath::FRandRange(0,Total)<Weight)Pick=Candidate;}
        if(Pick&&Pick!=Body)
        {if(Body){Body->SetManualControlEnabled(false);GM->QueueDefaultAIControllerRestore(Body);}GM->TryPossessMannequin(PC,Pick->GetControlSlot());Body=PC->GetViewedMannequin();}
        SwitchBodyAt=Now+ProjectBots::Difficulty::For(Spec.Difficulty).Reaction*FMath::FRandRange(2.f,4.f);
    }
    if(!Body)return;auto& B=Brains.FindChecked(Body);auto C=Context(B,true);C.Frozen=Body->IsFrozenBySurvivorVision();C.HasAlly=Bodies.Num()>1;
    if(C.Visible||C.Remembered)C.Range=FMath::Clamp(FVector::Dist2D(Body->GetActorLocation(),B.Known)/2400.f,0.f,1.f);
    using ProjectBots::Action;const auto Decision=B.Policy.Tick(C,Now);const auto Tactic=Decision.Tactic;
    auto* AI=Cast<AAIController>(Body->GetController());if(!AI)return;
    if(C.Frozen){AI->StopMovement();Body->GetCharacterMovement()->StopMovementImmediately();Body->ConsumeMovementInputVector();return;}
    if(Now>=B.CommandAt)
    {
        B.CommandAt=Now+B.Policy.Profile.Reaction*FMath::FRandRange(1.5f,3.f);
        if(Tactic==Action::Search){Body->SetManualControlEnabled(false);GM->TryIssueMannequinCommand(PC,EMultiplayMannequinCommand::Patrol);B.Path.Reset();}
        else
        {
            AI->StopMovement();
            // Commands are committed while AI-owned, then the server adapter supplies movement.
            Body->SetManualControlEnabled(false);
            GM->TryIssueMannequinCommand(PC,Tactic==Action::Observe?EMultiplayMannequinCommand::ChaseInDetectionRange:EMultiplayMannequinCommand::Patrol);
            Body->SetManualControlEnabled(true);
        }
        if((Tactic==Action::Cover||Tactic==Action::Regroup)&&B.Policy.Random()<B.Policy.Profile.Coordination)
        {
            for(auto* Other:Bodies)if(Other!=Body){GM->AssignMannequinCommand(Other,EMultiplayMannequinCommand::FollowMannequinPlayer);break;}
        }
        else if(Tactic==Action::Search&&B.Policy.Random()<.2f)GM->TryResetFollowingMannequins(PC);
        if(B.Policy.Random()<.025f)GM->ToggleMannequinCatchDamage(PC);
    }
    if(!Body->IsManualControlEnabled()||!(C.Visible||C.Remembered))return;
    if(Tactic==Action::Hold||Tactic==Action::Recover){Body->GetCharacterMovement()->StopMovementImmediately();return;}
    GM->MaintainMannequinFloorSafety(Body);FVector Goal=B.Known;const FVector Away=(Body->GetActorLocation()-Goal).GetSafeNormal2D();
    if(Tactic==Action::FlankLeft||Tactic==Action::FlankRight)Goal+=Away.RotateAngleAxis(Tactic==Action::FlankLeft?-85.f:85.f,FVector::UpVector)*380;
    else if(Tactic==Action::Lure)Goal+=Away*(GM->MultiplayerManualAttackRange+300);
    else if(Tactic==Action::Observe)Goal+=Away*(GM->MultiplayerManualAttackRange+160);
    Move(Body,B,Goal,Delta);
    auto* Target=Cast<APlayerCharacter>(B.Target.Get());
    if(Target&&B.Visible&&!Target->IsGameOver()&&Tactic!=Action::Lure&&Now>=B.AttackAt)
    {
        B.AttackAt=Now+FMath::Lerp(1.2f,.5f,B.Policy.Profile.Skill)*FMath::FRandRange(.85f,1.3f);const FVector Offset=Target->GetActorLocation()-Body->GetActorLocation();
        bool Watched=false;for(const auto& Pair:GM->MatchPlayerRecords)if(Pair.Key.IsValid()&&Pair.Value.SurvivorState==EMultiplayTestSurvivorState::Active)Watched|=GM->CanSurvivorSeeMannequin(Pair.Key.Get(),Body);
        if(!Watched&&!Body->IsFrozenBySurvivorVision()&&Offset.SizeSquared()<=FMath::Square(GM->MultiplayerManualAttackRange)&&FVector::DotProduct(Body->GetActorForwardVector(),Offset.GetSafeNormal())>=.25f&&AI->LineOfSightTo(Target))Target->HandleMannequinCatch(Body);
    }
}

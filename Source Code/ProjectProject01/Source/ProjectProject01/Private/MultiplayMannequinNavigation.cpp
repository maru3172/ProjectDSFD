#include "MultiplayTestGameMode.h"

#include "AIController.h"
#include "MannequinAICharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"

bool AMultiplayTestGameMode::FindSupportedMannequinLocation(
    const AMannequinAICharacter* Pawn, const FVector& DesiredCenter, FVector& OutCenter) const
{
    if (!IsValid(Pawn) || !GetWorld() || DesiredCenter.ContainsNaN()) return false;
    const auto* Capsule = Pawn->GetCapsuleComponent();
    const auto* Movement = Pawn->GetCharacterMovement();
    if (!Capsule || !Movement) return false;
    const float Radius = Capsule->GetScaledCapsuleRadius();
    const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MultiplayerMannequinFloor), true, Pawn);
    const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
    FHitResult Floor;
    // Bounded to this floor: do not silently move an invalid spawn onto the storey below.
    if (!GetWorld()->LineTraceSingleByChannel(Floor, DesiredCenter + FVector(0,0,Movement->MaxStepHeight),
        DesiredCenter - FVector(0,0,HalfHeight + Movement->MaxStepHeight + 10),
        Capsule->GetCollisionObjectType(), Query, Response) || Floor.bStartPenetrating ||
        Floor.ImpactNormal.Z < Movement->GetWalkableFloorZ()) return false;
    OutCenter = Floor.ImpactPoint + FVector(0,0,HalfHeight + 2.5f);
    return !GetWorld()->OverlapBlockingTestByChannel(OutCenter, FQuat::Identity,
        Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius, HalfHeight), Query, Response);
}

void AMultiplayTestGameMode::MaintainMannequinFloorSafety(AMannequinAICharacter* Pawn)
{
    if (!IsValid(Pawn) || Pawn->IsManualControlEnabled()) return;
    auto* Movement = Pawn->GetCharacterMovement();
    if (!Movement) return;
    // RVO can steer outside the NavMesh. Multiplayer AI follows navigation paths instead
    // and must not walk off unconnected edges. The single-player controller is untouched.
    Movement->SetAvoidanceEnabled(false);
    Movement->bCanWalkOffLedges = false;
    Movement->bCanWalkOffLedgesWhenCrouching = false;
    const FVector Current = Pawn->GetActorLocation();
    const FVector* LastSafe = MultiplayerLastSupportedLocations.Find(Pawn);
    const bool bDroppedBelowSafeFloor = LastSafe &&
        Current.Z < LastSafe->Z - FMath::Max(120.f, Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*2);
    FFindFloorResult Floor;
    Movement->FindFloor(Current, Floor, false);
    if (!bDroppedBelowSafeFloor && Floor.IsWalkableFloor() && !Floor.HitResult.bStartPenetrating && Floor.FloorDist <= Movement->MaxStepHeight)
    {
        MultiplayerLastSupportedLocations.FindOrAdd(Pawn) = Current;
        return;
    }
    if (!bDroppedBelowSafeFloor) return;
    FVector Supported;
    if (!FindSupportedMannequinLocation(Pawn, *LastSafe, Supported)) return;
    StopMannequinAI(Pawn);
    Pawn->SetActorLocation(Supported, false, nullptr, ETeleportType::TeleportPhysics);
    if (!Pawn->IsFrozenBySurvivorVision()) Movement->SetMovementMode(MOVE_Walking);
    Movement->bForceNextFloorCheck = true;
    MultiplayerNavigationStates.Remove(Pawn);
    MultiplayerPatrolTargets.Remove(Pawn);
    MultiplayerPatrolNextTargets.Remove(Pawn);
    UE_LOG(LogProjectProject01Multiplayer, Warning,
        TEXT("Recovered multiplayer mannequin %s from unsupported floor at %s to %s. Check map collision/NavMesh at the departure location."),
        *GetNameSafe(Pawn), *Current.ToCompactString(), *Supported.ToCompactString());
}

bool AMultiplayTestGameMode::FindReachableMannequinGoal(AMannequinAICharacter* Pawn,
    const FVector& DesiredFeet, float FallbackRadius, FVector& OutGoal, int32 Retry) const
{
    auto* Navigation = UNavigationSystemV1::GetCurrent(GetWorld());
    if (!IsValid(Pawn) || !Navigation || DesiredFeet.ContainsNaN()) return false;
    const float HalfHeight = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const auto& Agent = Pawn->GetNavAgentPropertiesRef();
    const int32 Attempts = FallbackRadius > 0 ? 13 : 1;
    for (int32 Index=0; Index<Attempts; ++Index)
    {
        if (Index==0 && Retry>0 && Attempts>1) continue;
        FVector Candidate = DesiredFeet;
        if (Index>0)
        {
            const float Angle = FMath::DegreesToRadians((Index-1)*137.508f + Retry*53.f);
            const float Distance = FallbackRadius * (Index<=4 ? .35f : Index<=8 ? .7f : 1.f);
            Candidate += FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,0);
        }
        FNavLocation Projected;
        // A tight vertical projection prevents picking a different floor in multistorey maps.
        if (!Navigation->ProjectPointToNavigation(Candidate, Projected, FVector(80,80,100), &Agent) ||
            FMath::Abs(Projected.Location.Z - DesiredFeet.Z)>100.f) continue;
        FVector Supported;
        if (!FindSupportedMannequinLocation(Pawn, Projected.Location+FVector(0,0,HalfHeight+2.5f), Supported)) continue;
        auto* Path = Navigation->FindPathToLocationSynchronously(GetWorld(), Pawn->GetNavAgentLocation(), Projected.Location, Pawn);
        if (!IsValid(Path) || !Path->IsValid() || Path->IsPartial()) continue;
        OutGoal = Projected.Location;
        return true;
    }
    return false;
}

void AMultiplayTestGameMode::MoveMannequinOnCompletePath(AMannequinAICharacter* Pawn,
    AAIController* Controller, const FVector& DesiredFeet, float FallbackRadius)
{
    if (!IsValid(Pawn) || !IsValid(Controller)) return;
    auto& State = MultiplayerNavigationStates.FindOrAdd(Pawn);
    const double Now = GetWorld()->GetTimeSeconds();
    const FVector Current = Pawn->GetActorLocation();
    const bool bNewRequest = State.LastRequestSeconds<0;
    if (bNewRequest || FVector::DistSquared2D(Current, State.ProgressLocation)>=FMath::Square(30.f))
    {
        State.ProgressLocation=Current;
        State.LastProgressSeconds=Now;
    }
    const bool bStalled = Controller->GetMoveStatus()==EPathFollowingStatus::Moving &&
        Now-State.LastProgressSeconds>=FMath::Max(0.5f,MultiplayerPatrolStuckTimeoutSeconds);
    const bool bGoalChanged = FVector::DistSquared(DesiredFeet,State.RequestedGoal)>FMath::Square(100.f);
    if (!bNewRequest && !bGoalChanged && !bStalled && Controller->GetMoveStatus()==EPathFollowingStatus::Moving) return;
    // Failed requests are bounded; never start a path search every render frame.
    if (!bNewRequest && !bGoalChanged && Now-State.LastRequestSeconds<.5) return;
    if (bStalled)
    {
        Controller->StopMovement();
        ++State.Retry;
        State.LastProgressSeconds=Now;
    }
    FVector Goal;
    State.LastRequestSeconds=Now;
    State.RequestedGoal=DesiredFeet;
    if (!FindReachableMannequinGoal(Pawn,DesiredFeet,FallbackRadius,Goal,State.Retry))
    {
        Controller->StopMovement();
        ++State.Retry;
        return; // Keep the player's command, retry when the route/leader changes.
    }
    State.ResolvedGoal=Goal;
    const float DeadZone=FMath::Max(MultiplayerAcceptanceRadius,MultiplayerFollowDistance*.15f);
    if (FVector::DistSquared(Pawn->GetNavAgentLocation(),Goal)<=FMath::Square(DeadZone))
    {
        Controller->StopMovement();
        State.LastProgressSeconds=Now;
        return;
    }
    // No partial route, no direct-line fallback through a wall. A* can detour through doors/corridors.
    if (Controller->GetMoveStatus()!=EPathFollowingStatus::Moving) State.LastProgressSeconds=Now;
    const auto Result=Controller->MoveToLocation(Goal,MultiplayerAcceptanceRadius,false,true,false,true,nullptr,false);
    if (Result==EPathFollowingRequestResult::Failed) ++State.Retry;
}

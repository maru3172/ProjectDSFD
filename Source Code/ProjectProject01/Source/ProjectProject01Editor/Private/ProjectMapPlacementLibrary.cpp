#include "ProjectMapPlacementLibrary.h"

#include "AssetCompilingManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FireShutterActor.h"
#include "MannequinReactiveLight.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "ProjectProject01RoleSpawnPoint.h"
#include "ProjectProject01LevelTransitionTrigger.h"

namespace
{
bool Blocked(UWorld& World, const FVector& At, const float Clearance)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FixtureClearance), true);
	for (TActorIterator<AFireShutterActor> It(&World); It; ++It) Params.AddIgnoredActor(*It);
	for (TActorIterator<AMannequinReactiveLight> It(&World); It; ++It) Params.AddIgnoredActor(*It);
	for (int32 D = 0; D < 8; ++D)
	{
		FHitResult Hit;
		if (World.LineTraceSingleByChannel(Hit, At, At + FRotator(0, D * 45.f, 0).Vector() * Clearance, ECC_Visibility, Params)) return true;
	}
	return false;
}
}

FString UProjectMapPlacementLibrary::MoveFixturesOutOfWalls(UObject* WorldContext, const float Clearance)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return TEXT("0,0");
	FAssetCompilingManager::Get().FinishAllCompilation(); // meshes build asynchronously after a load: no collision before
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	int32 Moved = 0, Removed = 0;
	TArray<AMannequinReactiveLight*> Lights;
	for (TActorIterator<AMannequinReactiveLight> It(World); It; ++It)
		if (It->Tags.Contains(FName(TEXT("ShutterLayoutAuto")))) Lights.Add(*It);
	for (AMannequinReactiveLight* Light : Lights)
	{
		const FVector Home = Light->GetActorLocation();
		const FVector Probe(0, 0, -20); // at the housing, just under the ceiling
		if (!Blocked(*World, Home + Probe, Clearance)) continue;
		bool bPlaced = false;
		for (float R = 100.f; R <= 500.f && !bPlaced; R += 100.f)
			for (int32 D = 0; D < 16 && !bPlaced; ++D)
			{
				const FVector Try = Home + FRotator(0, D * 22.5f, 0).Vector() * R;
				FNavLocation Floor;
				if (Nav && !Nav->ProjectPointToNavigation(FVector(Try.X, Try.Y, Home.Z - 550.f), Floor, FVector(30, 30, 300))) continue;
				if (Blocked(*World, Try + Probe, Clearance)) continue;
				Light->Modify();
				Light->SetActorLocation(Try, false, nullptr, ETeleportType::TeleportPhysics);
				bPlaced = true; ++Moved;
			}
		if (!bPlaced) { World->DestroyActor(Light); ++Removed; }
	}
	return FString::Printf(TEXT("%d,%d"), Moved, Removed);
}

FString UProjectMapPlacementLibrary::FitShuttersToWalls(UObject* WorldContext, const float CeilingHeight)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return TEXT("0,0,0");
	FAssetCompilingManager::Get().FinishAllCompilation(); // collision only exists once the meshes have built
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShutterFit), true);
	TArray<AFireShutterActor*> Shutters;
	for (TActorIterator<AFireShutterActor> It(World); It; ++It) { Params.AddIgnoredActor(*It); if (It->Tags.Contains(FName(TEXT("ShutterLayoutAuto")))) Shutters.Add(*It); }
	for (TActorIterator<AMannequinReactiveLight> It(World); It; ++It) Params.AddIgnoredActor(*It);
	int32 Narrowed = 0, Lowered = 0, Removed = 0;
	for (AFireShutterActor* Shutter : Shutters)
	{
		const FVector Base = Shutter->GetActorLocation(); // the panel's bottom centre (pivot at the floor)
		const FVector Right = Shutter->GetActorRightVector().GetSafeNormal2D();
		const FVector Scale = Shutter->GetActorScale3D();
		const float Half = 150.f * FMath::Abs(Scale.Y);
		// Width: from the centre along the panel at several heights, how far is clear on each side.
		float ClearLeft = Half, ClearRight = Half;
		for (const float Z : { 60.f, 200.f, 350.f })
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FVector From = Base + FVector(0, 0, Z), Dir = Side == 0 ? -Right : Right;
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, From, From + Dir * (Half + 5.f), ECC_Visibility, Params))
					(Side == 0 ? ClearLeft : ClearRight) = FMath::Min(Side == 0 ? ClearLeft : ClearRight, FMath::Max(0.f, Hit.Distance - 5.f));
			}
		if (ClearLeft + ClearRight < 60.f) { World->DestroyActor(Shutter); ++Removed; continue; }
		if (ClearLeft < Half - 1.f || ClearRight < Half - 1.f)
		{
			Shutter->Modify();
			Shutter->SetActorLocation(Base + Right * ((ClearRight - ClearLeft) * .5f));
			Shutter->SetActorScale3D(FVector(Scale.X, (ClearLeft + ClearRight) / 300.f, Scale.Z));
			++Narrowed;
		}
		// Height: straight up from a few points across the panel; the lowest hit (a lintel, a beam) caps the panel.
		const FVector Centre = Shutter->GetActorLocation();
		const float NewHalf = 150.f * FMath::Abs(Shutter->GetActorScale3D().Y);
		float Top = CeilingHeight + 10.f;
		for (const float T : { -.8f, 0.f, .8f })
		{
			const FVector From = Centre + Right * (NewHalf * T) + FVector(0, 0, 40.f);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, From, From + FVector(0, 0, CeilingHeight), ECC_Visibility, Params))
				Top = FMath::Min(Top, 40.f + Hit.Distance - 5.f);
		}
		if (Top < CeilingHeight)
		{
			if (Top < 200.f) { World->DestroyActor(Shutter); ++Removed; continue; } // a passage lower than a person: not a shutter spot
			Shutter->Modify();
			const FVector S = Shutter->GetActorScale3D();
			Shutter->SetActorScale3D(FVector(S.X, S.Y, Top / 400.f));
			++Lowered;
		}
	}
	return FString::Printf(TEXT("%d,%d,%d"), Narrowed, Lowered, Removed);
}

FString UProjectMapPlacementLibrary::RebuildNavigation(UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Nav) return TEXT("0,0,0");
	FAssetCompilingManager::Get().FinishAllCompilation(); // collision of freshly loaded meshes
	const uint8 Locks = ENavigationBuildLock::NoUpdateInEditor | ENavigationBuildLock::InitialLock | ENavigationBuildLock::Custom | ENavigationBuildLock::AsyncLoadLock;
	Nav->RemoveNavigationBuildLock(Locks, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	Nav->Build(); // waits for the build to finish
	int32 Tiles = 0;
	for (TActorIterator<ARecastNavMesh> It(World); It; ++It) Tiles += It->GetNavMeshTilesCount();
	TArray<FVector> Starts, Exits;
	for (TActorIterator<AProjectProject01RoleSpawnPoint> It(World); It; ++It) if (It->GetSpawnRole() == EProjectProject01SpawnRole::Survivor) Starts.Add(It->GetActorLocation());
	for (TActorIterator<AProjectProject01LevelTransitionTrigger> It(World); It; ++It) Exits.Add(It->GetActorLocation());
	int32 Ok = 0, Checked = 0;
	for (const FVector& From : Starts)
		for (const FVector& To : Exits)
		{
			++Checked;
			FNavLocation A, B;
			if (!Nav->ProjectPointToNavigation(From, A, FVector(200, 200, 300)) || !Nav->ProjectPointToNavigation(To, B, FVector(300, 300, 400))) continue;
			const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, A.Location, B.Location);
			Ok += Path && Path->IsValid() && !Path->IsPartial();
		}
	return FString::Printf(TEXT("%d,%d,%d"), Tiles, Ok, Checked);
}

namespace
{
constexpr float SpotRadius = 50.f, SpotHalfHeight = 92.f;

// The trap rules every role spawn spot must pass: floor under a navmesh point, room for a capsule, and a complete path to
// the nearest exit (a sealed room or a cut-off patch has none). Returns the capsule centre to stand the spot at.
bool PassesSpotRules(UWorld& World, UNavigationSystemV1& Nav, const TArray<FVector>& Exits, const FVector& NavPoint,
	const FCollisionQueryParams& Query, FVector& OutCenter, const TCHAR*& OutReason)
{
	FHitResult Floor;
	if (!World.LineTraceSingleByChannel(Floor, NavPoint + FVector(0, 0, 100), NavPoint - FVector(0, 0, 150), ECC_Pawn, Query) ||
		Floor.bStartPenetrating || Floor.ImpactNormal.Z < .7f) { OutReason = TEXT("no-floor"); return false; }
	OutCenter = Floor.ImpactPoint + FVector(0, 0, SpotHalfHeight + 2.f);
	if (World.OverlapBlockingTestByChannel(OutCenter, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(SpotRadius, SpotHalfHeight), Query))
	{ OutReason = TEXT("no-room"); return false; }
	// Any way out the bots use counts (a side trip such as the basement stairs is not one): nearest first.
	TArray<FVector> Ways = Exits;
	Ways.Sort([&NavPoint](const FVector& A, const FVector& B) { return FVector::DistSquared(A, NavPoint) < FVector::DistSquared(B, NavPoint); });
	OutReason = Ways.IsEmpty() ? TEXT("no-exit") : TEXT("no-path-out");
	for (const FVector& Exit : Ways)
	{
		FNavLocation Goal;
		if (!Nav.ProjectPointToNavigation(Exit, Goal, FVector(300, 300, 400))) continue;
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(&World, NavPoint, Goal.Location);
		if (Path && Path->IsValid() && !Path->IsPartial()) return true;
	}
	return false;
}

// Survivor spots (user, 2026-11-22): at least 20 m from every exit and escalator trigger on this storey, so nobody starts
// next to a way out, and at least Spacing from the other survivor spots, so the starts are spread over different areas.
constexpr float SurvivorExitClearance = 2000.f, SurvivorSpacing = 1500.f;
bool SurvivorSpotRoom(const TArray<FVector>& Triggers, const TArray<FVector>& Survivors, const FVector& At, const float Spacing)
{
	for (const FVector& T : Triggers)
		if (FMath::Abs(T.Z - At.Z) < 400.f && FVector::DistSquared2D(T, At) < FMath::Square(SurvivorExitClearance)) return false;
	for (const FVector& S : Survivors)
		if (FMath::Abs(S.Z - At.Z) < 400.f && FVector::DistSquared2D(S, At) < FMath::Square(Spacing)) return false;
	return true;
}

// Every shutter slot on the map, used or not this match: the runtime layout picks which come down, so a spot must keep
// clear of all of them (user, 2026-11-22). Panel footprints (wide, thin) plus 2 m, on this storey whatever the panel height.
TArray<FBox> ShutterFootprints(UWorld& World)
{
	TArray<FBox> Out;
	for (TActorIterator<AFireShutterActor> It(&World); It; ++It)
		if (const FBox Box = It->GetComponentsBoundingBox(true); Box.IsValid) Out.Add(Box.ExpandBy(FVector(200.f, 200.f, 0.f)));
	return Out;
}
bool InShutterFootprint(const TArray<FBox>& Footprints, const FVector& At)
{
	for (const FBox& B : Footprints)
		if (At.X >= B.Min.X && At.X <= B.Max.X && At.Y >= B.Min.Y && At.Y <= B.Max.Y && At.Z > B.Min.Z - 600.f && At.Z < B.Max.Z + 300.f) return true;
	return false;
}

// The exits the bots head for; every trigger when none is marked.
TArray<FVector> BotExits(UWorld& World)
{
	TArray<FVector> Objective, All;
	for (TActorIterator<AProjectProject01LevelTransitionTrigger> It(&World); It; ++It)
	{
		All.Add(It->GetActorLocation());
		if (It->IsAutomaticBotObjective()) Objective.Add(It->GetActorLocation());
	}
	return Objective.IsEmpty() ? All : Objective;
}
}

FString UProjectMapPlacementLibrary::CheckRoleSpawnSpots(UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Nav) return TEXT("error:no navigation");
	FAssetCompilingManager::Get().FinishAllCompilation();
	const TArray<FVector> Exits = BotExits(*World);
	TArray<FVector> Triggers;
	for (TActorIterator<AProjectProject01LevelTransitionTrigger> It(World); It; ++It) Triggers.Add(It->GetActorLocation());
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RoleSpawnCheck), true);
	for (TActorIterator<AFireShutterActor> It(World); It; ++It) Query.AddIgnoredActor(*It);
	const TArray<FBox> Footprints = ShutterFootprints(*World);
	// Survivor spots also keep away from every exit and escalator and from each other (SurvivorSpotRoom).
	TArray<AProjectProject01RoleSpawnPoint*> Spots;
	for (TActorIterator<AProjectProject01RoleSpawnPoint> It(World); It; ++It) Spots.Add(*It);
	Spots.StableSort([](const AProjectProject01RoleSpawnPoint& A, const AProjectProject01RoleSpawnPoint& B) { return A.GetName() < B.GetName(); });
	TArray<FVector> Survivors;
	FString Out;
	for (AProjectProject01RoleSpawnPoint* Spot : Spots)
	{
		const bool bSurvivor = Spot->GetSpawnRole() == EProjectProject01SpawnRole::Survivor;
		const FVector At = Spot->GetActorLocation();
		const auto Passes = [&](const FVector& NavPoint, FVector& Center, const TCHAR*& Reason)
		{
			if (!PassesSpotRules(*World, *Nav, Exits, NavPoint, Query, Center, Reason)) return false;
			if (InShutterFootprint(Footprints, Center)) { Reason = TEXT("in-shutter-slot"); return false; }
			if (bSurvivor && !SurvivorSpotRoom(Triggers, Survivors, Center, SurvivorSpacing)) { Reason = TEXT("near-exit-or-survivor-spot"); return false; }
			return true;
		};
		FVector Center;
		const TCHAR* Reason = TEXT("off-nav");
		FNavLocation On;
		if (Nav->ProjectPointToNavigation(At, On, FVector(60, 60, 200)) && Passes(On.Location, Center, Reason))
		{
			if (bSurvivor) Survivors.Add(Center);
			Out += FVector::Dist(Center, At) < 60.f ? FString::Printf(TEXT("%s ok;"), *Spot->GetName())
				: FString::Printf(TEXT("%s move %.0f %.0f %.0f;"), *Spot->GetName(), Center.X, Center.Y, Center.Z);
			continue;
		}
		// The nearest passing spot on the same storey: within 5 m, or 40 m for a survivor spot that is too close to an
		// exit, an escalator or another survivor spot.
		const TCHAR* FirstReason = Reason;
		const float Range = bSurvivor ? 4000.f : 500.f;
		float Best = TNumericLimits<float>::Max();
		FVector BestCenter = FVector::ZeroVector;
		for (int32 Try = 0; Try < (bSurvivor ? 1500 : 200); ++Try)
		{
			FNavLocation Sample;
			if (!Nav->GetRandomPointInNavigableRadius(At, Range, Sample) || FMath::Abs(Sample.Location.Z + SpotHalfHeight - At.Z) > 120.f) continue;
			const float D = FVector::Dist2D(Sample.Location, At);
			if (D < Best && Passes(Sample.Location, Center, Reason)) { Best = D; BestCenter = Center; }
		}
		if (Best <= Range + 100.f)
		{
			if (bSurvivor) Survivors.Add(BestCenter);
			Out += FString::Printf(TEXT("%s move %.0f %.0f %.0f;"), *Spot->GetName(), BestCenter.X, BestCenter.Y, BestCenter.Z);
		}
		else Out += FString::Printf(TEXT("%s fail %s;"), *Spot->GetName(), FirstReason);
	}
	return Out;
}

FString UProjectMapPlacementLibrary::PickRoleSpawnSpots(UObject* WorldContext, const int32 MannequinTotal, const int32 SurvivorTotal, const int32 Seed)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Nav) return TEXT("error:no navigation");
	FAssetCompilingManager::Get().FinishAllCompilation(); // collision of freshly loaded meshes
	TArray<FVector> Mannequins, Survivors, Starts, Exits, Shutters;
	for (TActorIterator<AProjectProject01RoleSpawnPoint> It(World); It; ++It)
		(It->GetSpawnRole() == EProjectProject01SpawnRole::Survivor ? Survivors : Mannequins).Add(It->GetActorLocation());
	for (TActorIterator<APlayerStart> It(World); It; ++It) Starts.Add(It->GetActorLocation()); // role spots and arrival spots
	for (TActorIterator<AProjectProject01LevelTransitionTrigger> It(World); It; ++It) Exits.Add(It->GetActorLocation());
	for (TActorIterator<AFireShutterActor> It(World); It; ++It) Shutters.Add(It->GetActorLocation());
	if (Survivors.IsEmpty() || Exits.IsEmpty()) return TEXT("error:no survivor start or exit");
	// This storey and the play area: the box of the hand-placed spawns and exits plus 3 m.
	TArray<float> Heights;
	for (const FVector& P : Survivors) Heights.Add(P.Z);
	for (const FVector& P : Mannequins) Heights.Add(P.Z);
	Heights.Sort();
	const float StoreyZ = Heights[Heights.Num() / 2];
	FBox2D Area(ForceInit);
	for (const FVector& P : Survivors) Area += FVector2D(P);
	for (const FVector& P : Mannequins) Area += FVector2D(P);
	for (const FVector& P : Exits) Area += FVector2D(P);
	Area = Area.ExpandBy(300.f);
	const auto Near = [](const TArray<FVector>& Set, const FVector& At, const float Range)
	{
		for (const FVector& P : Set) if (FVector::DistSquared2D(P, At) < FMath::Square(Range) && FMath::Abs(P.Z - At.Z) < 250.f) return true;
		return false;
	};
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RoleSpawnSpot), true);
	for (TActorIterator<AFireShutterActor> It(World); It; ++It) Query.AddIgnoredActor(*It); // runtime doors, not walls
	const TArray<FVector> WaysOut = BotExits(*World);
	const TArray<FBox> ShutterSlots = ShutterFootprints(*World);
	TArray<FVector> Pool;
	for (int32 Try = 0; Try < 6000 && Pool.Num() < 1500; ++Try)
	{
		FNavLocation Sample;
		if (!Nav->GetRandomReachablePointInRadius(Survivors[Try % Survivors.Num()], 12000.f, Sample)) continue;
		const FVector P = Sample.Location;
		if (!Area.IsInside(FVector2D(P)) || FMath::Abs(P.Z + SpotHalfHeight - StoreyZ) > 150.f) continue;
		if (Near(Exits, P, 800.f) || InShutterFootprint(ShutterSlots, P) || Near(Starts, P, 600.f) || Near(Pool, P, 150.f)) continue;
		FVector Center;
		const TCHAR* Reason = nullptr;
		if (PassesSpotRules(*World, *Nav, WaysOut, P, Query, Center, Reason)) Pool.Add(Center);
	}
	// Farthest-apart first: each pick is the pool spot whose nearest existing or picked spot is farthest away.
	FRandomStream Random(Seed);
	FString Out;
	int32 Placed[2] = {0, 0};
	// Allowed(P) says where this role may stand at all; MinGap is the least distance to the same role's spots.
	const auto Pick = [&](TArray<FVector>& Same, const int32 Count, const TCHAR* Role, int32& Done, const float MinGap, const TFunctionRef<bool(const FVector&)> Allowed)
	{
		for (int32 N = 0; N < Count && !Pool.IsEmpty(); ++N)
		{
			int32 Best = INDEX_NONE;
			float BestGap = 0.f;
			for (int32 I = 0; I < Pool.Num(); ++I)
			{
				if (!Allowed(Pool[I])) continue;
				float Gap = TNumericLimits<float>::Max();
				for (const FVector& P : Same) Gap = FMath::Min(Gap, FVector::Dist2D(P, Pool[I]));
				for (const FVector& P : Starts) if (FVector::Dist2D(P, Pool[I]) < 600.f) Gap = 0.f;
				if (Gap > BestGap) { BestGap = Gap; Best = I; }
			}
			if (Best == INDEX_NONE || BestGap < MinGap) break;
			const FVector At = Pool[Best];
			Pool.RemoveAtSwap(Best);
			Same.Add(At);
			Starts.Add(At);
			++Done;
			Out += FString::Printf(TEXT("%s %.0f %.0f %.0f %.0f;"), Role, At.X, At.Y, At.Z, Random.FRandRange(-180.f, 180.f));
		}
	};
	Pick(Mannequins, FMath::Max(0, MannequinTotal - Mannequins.Num()), TEXT("M"), Placed[0], 600.f, [](const FVector&) { return true; });
	// Survivor spots only 20 m or more from every exit and escalator, spread apart; the spacing relaxes to 10 m, then 6 m,
	// only if a crowded storey cannot fit them all at 15 m.
	for (const float Spacing : {SurvivorSpacing, 1000.f, 600.f})
	{
		if (Survivors.Num() >= SurvivorTotal) break;
		const TArray<FVector> NoSurvivors;
		Pick(Survivors, SurvivorTotal - Survivors.Num(), TEXT("S"), Placed[1], Spacing,
			[&](const FVector& P) { return SurvivorSpotRoom(Exits, NoSurvivors, P, Spacing); });
	}
	if (Survivors.Num() < SurvivorTotal)
		UE_LOG(LogTemp, Warning, TEXT("[RoleSpawnSpots] only %d of %d survivor spots fit the exit clearance"), Survivors.Num(), SurvivorTotal);
	UE_LOG(LogTemp, Display, TEXT("[RoleSpawnSpots] pool=%d mannequin+%d survivor+%d storeyZ=%.0f"), Pool.Num() + Placed[0] + Placed[1], Placed[0], Placed[1], StoreyZ);
	return Out;
}

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

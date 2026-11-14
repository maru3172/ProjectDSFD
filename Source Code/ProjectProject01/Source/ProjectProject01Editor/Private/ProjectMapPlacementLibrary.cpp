#include "ProjectMapPlacementLibrary.h"

#include "AssetCompilingManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FireShutterActor.h"
#include "MannequinReactiveLight.h"
#include "NavigationSystem.h"

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

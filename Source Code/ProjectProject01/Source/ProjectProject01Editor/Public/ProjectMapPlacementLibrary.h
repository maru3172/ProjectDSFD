#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectMapPlacementLibrary.generated.h"

/** Editor helpers the map placement script (Tools/PlaceFireShutterLayout.py) calls where Python has no collision. */
UCLASS()
class UProjectMapPlacementLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/**
	 * Light fixtures the script placed (tag ShutterLayoutAuto) that touch modelled geometry at their own height (a lintel over
	 * a store front, a soffit) move to the nearest clear spot within 5 m over walkable floor; ones with none are removed.
	 * Builds pending meshes first so collision exists. Returns "moved,removed".
	 */
	UFUNCTION(BlueprintCallable, Category = "Project|Placement", meta = (WorldContext = "WorldContext"))
	static FString MoveFixturesOutOfWalls(UObject* WorldContext, float Clearance = 80.f);

	/**
	 * Shutters the script placed (tag ShutterLayoutAuto) that run into modelled geometry: a panel whose width reaches a
	 * wall or column is narrowed to the clear part, one under a lintel or beam is lowered to just below it. Panels left
	 * narrower than 60 cm are removed. Returns "narrowed,lowered,removed".
	 */
	UFUNCTION(BlueprintCallable, Category = "Project|Placement", meta = (WorldContext = "WorldContext"))
	static FString FitShuttersToWalls(UObject* WorldContext, float CeilingHeight = 600.f);

	/** Full navigation build of the loaded editor map (works from a commandlet: lifts the editor's load lock first), then
	 *  checks a complete path from every survivor start to every exit. Returns "tiles,paths ok,paths checked". Dynamic
	 *  Modifiers Only maps need this after any change, or tiles around runtime shutters come back empty. */
	UFUNCTION(BlueprintCallable, Category = "Project|Placement", meta = (WorldContext = "WorldContext"))
	static FString RebuildNavigation(UObject* WorldContext);

	/**
	 * Picks extra role spawn spots for the loaded match floor (user, 2026-11-22: 20 mannequin + 10 survivor spots per floor).
	 * Candidates are navmesh points reachable from a survivor start with a complete path to an exit (no sealed rooms), inside
	 * the box of the existing spawns and exits plus a small margin (no outskirts), on this storey, with floor support and
	 * capsule room, and clear of exits, shutters and every other start. The farthest-apart candidates win, so the new spots
	 * fill the gaps. Places nothing: returns "M x y z yaw;S x y z yaw;..." (the script spawns and tags them), or "error:...".
	 */
	UFUNCTION(BlueprintCallable, Category = "Project|Placement", meta = (WorldContext = "WorldContext"))
	static FString PickRoleSpawnSpots(UObject* WorldContext, int32 MannequinTotal = 20, int32 SurvivorTotal = 10, int32 Seed = 1);

	/**
	 * Checks every role spawn spot already on the map against the trap rules of PickRoleSpawnSpots (on the navmesh, floor
	 * support, capsule room, a complete path to an exit). Nothing is moved here: returns one "name ok", "name move x y z" (the
	 * nearest passing spot within 5 m) or "name fail <reason>" entry per spot, separated by ';'.
	 */
	UFUNCTION(BlueprintCallable, Category = "Project|Placement", meta = (WorldContext = "WorldContext"))
	static FString CheckRoleSpawnSpots(UObject* WorldContext);
};
